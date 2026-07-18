#include "tinykv/net/tcp_server.h"

#include <iostream>
#include <exception>
#include <string>

int main(int argc, char *argv[])
{
    if (argc < 3 || argc > 4) {
        std::cerr << "Usage: " << argv[0] << " <host> <port> [auto|poll|epoll]" << std::endl;
        return 1;
    }

    try {
        const std::string host = argv[1];
        const int port = std::stoi(argv[2]);

        tinykv::TcpServerOptions options;
        if (argc == 4) {
            options.pollerBackend = tinykv::ParsePollerBackend(argv[3]);
        }
        
        tinykv::TcpServer server(host, port, std::chrono::seconds(5), options);
        server.Run(); 
    } catch (const std::exception &e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}