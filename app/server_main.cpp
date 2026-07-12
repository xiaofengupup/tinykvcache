#include "tinykv/net/socket_util.h"
#include "tinykv/core/frame_codec.h"
#include "tinykv/core/command_parser.h"
#include "tinykv/core/command_executor.h"
#include "tinykv/core/kv_store.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <stdexcept>

namespace {

void SendResponse(int fd, const std::string &response)
{
    const auto frame = tinykv::FrameCodec::Encode(response);

    std::size_t sent = 0;
    while (sent < frame.size()) {
        auto n = ::send(fd, frame.data() + sent, frame.size() - sent, 0);
        if (n <= 0) {
            throw std::runtime_error("send failed");
        }
        
        sent += static_cast<std::size_t>(n);
    }
}

}

int main(int argc, char *argv[])
{
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " host port" << std::endl;
        return 1;
    }

    const std::string host = argv[1];
    const int port = std::stoi(argv[2]);

    try {
        auto listenFd = tinykv::CreateListenSocket(host, port);
        std::cout << "Server listening on " << host << ":" << port << std::endl;

        tinykv::KVStore store;

        while (true) {
            sockaddr_in clientAddr;
            socklen_t len = sizeof(clientAddr);

            int clientFd = ::accept(listenFd, reinterpret_cast<sockaddr *>(&clientAddr), &len);
            if (clientFd < 0) {
                throw std::runtime_error("accept failed");
            }

            tinykv::ScopedFd client(clientFd);
            std::cout << "client connected: " << clientFd << std::endl;

            std::string buffer;
            char temp[4096];
            while (true) {
                const ssize_t n = ::recv(client.Get(), temp, sizeof(temp), 0);
                if (n <= 0) {
                    break;
                }
                
                auto frames = tinykv::FrameCodec::Decode(buffer, temp, static_cast<size_t>(n));
                for (const auto &frame : frames) {
                    auto command = tinykv::ParseCommand(frame);
                    auto response = tinkv::CommandExecutor::Execute(store, command);

                    SendResponse(client.Get(), response);
                }

                if(command.type == tinykv::CommandType::Quit) {
                    return 0;
                }
            }
        }

        std::cout << "client disconnected\n";
    } catch (const std::exception &e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}