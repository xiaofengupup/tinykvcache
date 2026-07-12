#include "tinykv/net/socket_util.h"
#include "tinykv/core/frame_codec.h"

#include <sys/socket.h>
#include <iostream>

int main()
{
    auto server = tinykv::CreateListenSocket("127.0.0.1", 7777);
    std::cout << "Listening on 7777...\n";

    sockaddr_in addr {};
    socklen_t len = sizeof(addr);

    int client = ::accept(server.Get(), reinterpret_cast<sockaddr*>(&addr), &len);
    std::cout << "Accepted a connection, fd = " << client << "\n";

    std::string buffer;
    char temp[4096];

    while (true) {
        auto n = ::recv(client, temp, sizeof(temp), 0);
        if (n <= 0) {
            std::cout << "Client closed the connection or error occurred\n";
            break;
        }

        auto frames = tinykv::FrameCodec::Decode(buffer, temp, static_cast<std::size_t>(n));
        for (auto &frame : frames) {
            std::cout << "Received frame: " << frame << "\n";
            auto response = tinykv::FrameCodec::Encode("Echo: " + frame);

            ::send(client, response.data(), response.size(), 0);
        }
    }
}