#include "tinykv/net/tcp_server.h"

#include <iostream>
#include <exception>
#include <string>

int main(int argc, char *argv[])
{
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " host port" << std::endl;
        return 1;
    }

    const std::string host = argv[1];
    try {
        const int port = std::stoi(argv[2]);
        
        tinykv::TcpServer server(host, port);
        server.Run(); 
    } catch (const std::exception &e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}