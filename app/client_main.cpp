#include "tinykv/net/socket_util.h"
#include "tinykv/core/frame_codec.h"

#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void SendAll(int fd, const std::string &data)
{
    // TCP是字节流，不保证一次 send 就能发送完所有数据，所以需要循环发送，直到所有数据都发送完毕
    std::size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, 0);
        if (n <= 0) {
            throw std::runtime_error("Failed to send data");
        }
        sent += static_cast<std::size_t>(n);
    }
}

std::string RecvFrame(int fd)
{
    std::string buffer; // 主要用于处理 TCP 半包
    char temp[4096];

    while (true) {
        const ssize_t n = ::recv(fd, temp, sizeof(temp), 0);
        if (n <= 0) {
            throw std::runtime_error("Failed to receive data");
        }

        auto frames = tinykv::FrameCodec::Decode(buffer, temp, static_cast<std::size_t>(n));
        if (!frames.empty()) {
            return frames.front(); // 返回第一条完整的 payload
        }
    }
}

}

/**
 * 客户端仍然是阻塞 IO
 * 
 * 原因是：客户端逻辑简单，输入一个命令，等待一个响应，不需要同时处理多个连接，也不需要高并发。
 */
int main(int argc, char *argv[])
{
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <host> <port>\n";
        return 1;
    }

    const std::string host = argv[1];
    const int port = std::stoi(argv[2]);

    try {
        auto socket = tinykv::ConnectToServer(host, port);
        std::cout << "Connected to server at " << host << ":" << port << "\n";

        std::string line;
        while (true) {
            std::cout << "> " << std::flush;

            // 读取用户输入
            if (!std::getline(std::cin, line)) {
                break; // EOF or error
            }

            if (line.empty()) {
                continue; // 忽略空行
            }

            // 编码并发送数据
            const std::string frame = tinykv::FrameCodec::Encode(line);
            SendAll(socket.Get(), frame);

            // 接收并解码响应
            const std::string response = RecvFrame(socket.Get());
            std::cout << response << "\n";

            if (line == "QUIT" || line == "quit") {
                break; // 用户输入 QUIT，退出循环
            }
        }
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}