#include "tinykv/net/tcp_server.h"
#include "tinykv/net/wakeup/termination_signal_handler.h"
#include "tinykv/observability/logger.h"

#include <iostream>
#include <exception>
#include <string>

int main(int argc, char *argv[])
{
    if (argc < 3 || argc > 5) {
        std::cerr << "Usage: " << argv[0]
                  << " <host> <port> [auto|poll|epoll] [debug|info|warn|error|off]"
                  << std::endl;
        return 1;
    }

    try {
        const std::string host = argv[1];
        const int port = std::stoi(argv[2]);

        tinykv::TcpServerOptions options;
        if (argc >=4) {
            options.pollerBackend = tinykv::PollerFactory::ParsePollerBackend(argv[3]);
        }
        if (argc >=5) {
            tinykv::Logger::Instance().SetLevel(tinykv::ParseLogLevel(argv[4]));
        }
        
        // signalHandler 必须在 server 之后构造。
        // 局部对象逆序析构，因此会先恢复信号处理器，再销毁 TcpServer 和 WakeupChannel。
        tinykv::TcpServer server(host, port, std::chrono::seconds(5), options);
        tinykv::TerminationSignalHandler signals(server.StopNotificationFd());
        server.Run(); 
    } catch (const std::exception &e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}