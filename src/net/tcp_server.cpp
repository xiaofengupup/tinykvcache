#include "tinykv/net/tcp_server.h"

#include "tinykv/core/command_parser.h"
#include "tinykv/core/command_executor.h"
#include "tinykv/core/frame_codec.h"
#include "tinykv/net/socket_util.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <utility>
#include <chrono>
#include <thread>

namespace tinykv {

namespace {

std::string ErrorMessage(const char* prefix)
{
    return std::string(prefix) + ": " + std::strerror(errno);
}

bool IsWouldBlockError()
{   
    // 如果 recv() 没有数据，就返回：EAGAIN / EWOULDBLOCK
    return errno == EAGAIN || errno == EWOULDBLOCK;
}

}

TcpServer::TcpServer(std::string host, int port, std::chrono::seconds sweepInterval, TcpServerOptions options)
    : m_host(std::move(host)), m_port(port), m_sweepInterval(sweepInterval), m_options(options)
{
    if (m_options.maxReadBufferBytes < 4U) {
        throw std::invalid_argument("maxReadBufferBytes must be at least 4");
    }

    if (m_options.writeLowWatermarkBytes > m_options.writeHighWatermarkBytes) {
        throw std::invalid_argument("write low watermark exceeds high watermark");
    }

    if (m_options.writeHighWatermarkBytes > m_options.writeHardLimitBytes) {
        throw std::invalid_argument("write high watermark exceeds hard limit");
    }

    if (m_options.maxWriteBytesPerEvent == 0U) {
        throw std::invalid_argument("maxWriteBytesPerEvent must be greater than zero");
    }

    if (m_options.maxAcceptsPerEvent == 0U) {
        throw std::invalid_argument("maxAcceptsPerEvent must be greater than zero");
    }
}

TcpServer::~TcpServer()
{
    Stop();
    StopSweeperThread();
}

void TcpServer::Run()
{
    m_listenFd = CreateListenSocket(m_host, m_port);
    SetNonBlocking(m_listenFd.Get()); // 核心修改：将 listen fd 设置为非阻塞，这样 accept 不会卡死
    m_running = true;    
    StartSweeperThread(); // 启动 TTL 清理线程
    std::cout << "server listenint on " << m_host << ":" << m_port << "\n";


    try {
        while (m_running) {
            std::vector<pollfd> pollFds;
            pollFds.reserve(1 + m_clients.size());

            // 将 listen fd 加入 poll 关注列表
            pollfd listenPoll;
            listenPoll.fd = m_listenFd.Get();
            listenPoll.events = POLLIN;
            listenPoll.revents = 0;
            pollFds.push_back(listenPoll);

            // 将 client fd 加入 poll 关注列表
            for (auto& item : m_clients) {
                Connection &conn = item.second;
                if (conn.closed) {
                    continue;
                }

                UpdateReadBackPressure(conn);
                if (conn.closeAfterWrite && conn.writeBuffer.Empty()) {
                    MarkClosed(conn);
                    continue;
                }

                pollfd clientPoll;
                clientPoll.fd = conn.fd.Get();
                clientPoll.events = 0;
                clientPoll.revents = 0;

                // 当写缓冲区超过高水位，或者收到 QUIT 后，暂停继续读取该连接。
                if (!conn.readPaused && !conn.closeAfterWrite) {
                    clientPoll.events |= POLLIN;
                }
                if (!conn.writeBuffer.Empty()) {
                    // 只有当 write_buffer 非空时，才关注 POLLOUT。
                    // 否则大多数 socket 都会一直可写，导致 poll 频繁返回，浪费 CPU。
                    clientPoll.events |= POLLOUT;
                }
                if (clientPoll.events == 0) {
                    continue;
                }
                pollFds.push_back(clientPoll);
            }


            const int ret = ::poll(pollFds.data(), static_cast<nfds_t>(pollFds.size()), 1000);
            if (ret < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::runtime_error(ErrorMessage("poll failed"));
            }

            if (ret == 0) {
                CleanupClosedConnections();
                continue;
            }

            // 第 0 个永远是 listen fd
            if ((pollFds[0].revents & POLLIN) != 0) {
                AcceptNewClients();
            }

            // 后续都是 client fd
            for (std::size_t i = 1; i < pollFds.size(); ++i) {
                const int fd = pollFds[i].fd;
                const short revents = pollFds[i].revents;

                if (revents == 0) {
                    continue;
                }
                
                auto it = m_clients.find(fd);
                if (it == m_clients.end()) {
                    continue;
                }

                Connection &conn = it->second;
                if ((revents & (POLLERR | POLLNVAL)) != 0) {
                    MarkClosed(conn);
                    continue;
                }


                // 处理读事件
                if ((revents & POLLIN) != 0) {
                    HandleClientRead(conn);
                }
                if (conn.closed) {
                    continue;
                }

                // 处理写事件
                if ((revents & POLLOUT) != 0) {
                    HandleClientWrite(conn);
                }
                if (conn.closed) {
                    continue;
                }

                // 处理中断
                if ((revents & POLLHUP) != 0) {
                    MarkClosed(conn);
                }
            }

            CleanupClosedConnections();
        }
    } catch (...) {
        StopSweeperThread();
        m_clients.clear();
        m_listenFd.Reset();
        m_running = false;
        throw;
    }   
    
    StopSweeperThread();
    m_clients.clear();
    m_listenFd.Reset();
}

void TcpServer::Stop()
{
    m_running = false;
    m_listenFd.Reset();

    for (auto &item : m_clients) {
        MarkClosed(item.second);
    }

    StopSweeperThread();
}

void TcpServer::AcceptNewClients()
{
    // 这里采用循环的原因是：
    // 一次 poll() 通知 listen fd 可读时，可能已经有多个客户端在连接队列中，所以要一直 accept()

    std::size_t acceptedCount = 0;
    while (acceptedCount < m_options.maxAcceptsPerEvent) {
        const int clientFd = ::accept(m_listenFd.Get(), nullptr, nullptr);
        if (clientFd < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (IsWouldBlockError()) {
                return;
            }

            throw std::runtime_error(ErrorMessage("accept failed"));
        }

        ScopedFd client(clientFd);
        try {
            SetNonBlocking(client.Get());
        } catch (...) {
            throw; // client 是 ScopedFd，异常时会自动 close。
        }

        const int rawFd = client.Get();
        auto result = m_clients.emplace(rawFd, Connection(std::move(client)));
        if (!result.second) {
            throw std::runtime_error("client fd already exists");
        }
        ++acceptedCount;
        std::cout << "client connected, fd=" << rawFd << "\n";
    }
}

void TcpServer::HandleClientRead(Connection &conn)
{
    if (conn.closed || conn.readPaused || conn.closeAfterWrite) {
        return;
    }

    if (conn.readBuffer.size() >= m_options.maxReadBufferBytes) {
        RejectOversizedReadBuffer(conn);
        return;
    }

    std::array<char, 16U * 1024U> temp{};
    const std::size_t availableBytes = m_options.maxReadBufferBytes - conn.readBuffer.size();
    const std::size_t readSize = std::min(temp.size(), availableBytes);

    // 这里只执行一次 recv，是因为当前使用的是level-triggered poll：
    // socket 内核缓冲区仍然有数据 -> 下一个 poll 仍会报告 POLLIN -> 其他连接也获得一次处理机会
    ssize_t received = -1;
    do {
        received = ::recv(conn.fd.Get(), temp.data(), readSize, 0);
    } while (received < 0 && errno == EINTR);

    if (received == 0) {
        MarkClosed(conn);
        return;
    }
    if (received < 0) {
        if (IsWouldBlockError()) {
            return;
        }

        MarkClosed(conn);
        return;
    }

    std::vector<std::string> payloads;
    try {
        payloads = FrameCodec::Decode(conn.readBuffer, temp.data(), static_cast<std::size_t>(received));
    } catch (const std::exception&) {
        if (QueueResponse(conn, "-ERR protocol error")) {
            conn.closeAfterWrite = true;
        }
        return;
    }

    for (const auto &payload : payloads) {
        if (!ProcessPayload(conn, payload)) {
            return;
        }
    }
}

void TcpServer::HandleClientWrite(Connection &conn)
{
    if (conn.closed) {
        return;
    }

    if (conn.writeBuffer.Empty()) {
        UpdateReadBackPressure(conn);
        if (conn.closeAfterWrite) {
            MarkClosed(conn);
        }

        return;
    }

    const std::size_t bytesToWrite = std::min(conn.writeBuffer.Size(), m_options.maxWriteBytesPerEvent);
    
    ssize_t written = -1;
    do {
        written = ::send(conn.fd.Get(), conn.writeBuffer.Data(), bytesToWrite, 0);
    } while (written < 0 && errno == EINTR);
    
    if (written > 0) {
        conn.writeBuffer.Consume(static_cast<std::size_t>(written));
        UpdateReadBackPressure(conn);

        if (conn.writeBuffer.Empty() && conn.closeAfterWrite) {
            MarkClosed(conn);
        }
        return;
    }

    if (written == 0) {
        MarkClosed(conn);
        return;
    }

    if (IsWouldBlockError()) {
        return;
    }

    MarkClosed(conn);
}

bool TcpServer::ProcessPayload(Connection &conn, const std::string &payload)
{
    const Command cmd = ParseCommand(payload);
    const std::string response = CommandExecutor::Execute(m_store, cmd);

    if (!QueueResponse(conn, response)) {
        return false;
    }

    if (cmd.type == CommandType::Quit) {
        conn.closeAfterWrite = true; // 先把 +BYE 写完，再关闭连接。
        return false;
    }

    return true;
}

bool TcpServer::QueueResponse(Connection& conn, const std::string& response)
{
    std::string frame;
    try {
        frame = FrameCodec::Encode(response);
    } catch (const std::exception&) {
        MarkClosed(conn);
        return false;
    }

    const std::size_t pendingBytes = conn.writeBuffer.Size();
    const std::size_t hardLimit = m_options.writeHardLimitBytes;

    if (pendingBytes > hardLimit || frame.size() > hardLimit - pendingBytes) {
        std::cerr << "close slow client, fd="
            << conn.fd.Get()
            << ", pending_bytes="
            << pendingBytes
            << ", new_frame_bytes="
            << frame.size()
            << '\n';

        MarkClosed(conn);
        return false;
    }

    conn.writeBuffer.Append(frame);
    UpdateReadBackPressure(conn);
    return true;
}

void TcpServer::MarkClosed(Connection& conn)
{
    conn.closed = true;
    conn.readPaused = false;
    conn.closeAfterWrite = false;

    // 连接已经关闭或异常时，未发送数据不再保留。
    conn.readBuffer.clear();
    conn.writeBuffer.Clear();
}

void TcpServer::CleanupClosedConnections()
{
    for (auto it = m_clients.begin(); it != m_clients.end();) {
        if (it->second.closed) {
            std::cout << "client disconnected, fd=" << it->first << "\n";
            it = m_clients.erase(it);
        } else {
            ++it;
        }
    } 
}

void TcpServer::StartSweeperThread()
{
    if (m_sweepInterval.count() < 0) {
        return;
    }

    bool expected = false;
    if (!m_sweepRunning.compare_exchange_strong(expected, true)) {
        // 已经启动过了
        return;
    }

    m_sweepThread = std::thread(&TcpServer::SweeperLoop, this);
}

void TcpServer::StopSweeperThread()
{
    const bool wasRunning = m_sweepRunning.exchange(false);
    if (wasRunning) {
        m_sweepCv.notify_all();
    }

    if (m_sweepThread.joinable()) {
        m_sweepThread.join();
    }
}

void TcpServer::SweeperLoop()
{
    std::unique_lock<std::mutex> lock(m_sweepMutex);

    while (m_sweepRunning) {
        const bool shouldStop = m_sweepCv.wait_for(lock, m_sweepInterval, [this] {
            return !m_sweepRunning.load();
        });

        if (shouldStop) {
            break;
        }

        // 不要持有 sweeper_mutex_ 调用 KVStore。
        // KVStore 内部有自己的 mutex。
        lock.unlock();

        const size_t removed = m_store.SweepExpired();
        if (removed > 0) {
            std::cout << "[sweeper] removed expired keys: " << removed << "\n";
        }
        
        lock.lock();
    }
}

void TcpServer::UpdateReadBackPressure(Connection& conn)
{
    if (conn.closeAfterWrite || conn.closed) {
        return;
    }

    const std::size_t pendingBytes = conn.writeBuffer.Size();

    if (conn.readPaused) {
        if (pendingBytes <= m_options.writeLowWatermarkBytes) {
            conn.readPaused = false;
        }
        return;
    }

    if (pendingBytes >= m_options.writeHighWatermarkBytes) {
        conn.readPaused = true;
    }
}

void TcpServer::RejectOversizedReadBuffer(Connection& conn)
{
    if (QueueResponse(conn, "-ERR request buffer too large")) {
        conn.closeAfterWrite = true;
    } else {
        MarkClosed(conn);
    }
}

} // namespace tinykv
