#include "tinykv/net/tcp_server.h"
#include "tinykv/net/wakeup/termination_signal_handler.h"
#include "tinykv/net/poll/poller_factory.h"
#include "tinykv/observability/logger.h"
#include "tinykv/config/app_config.h"
#include "tinykv/config/config_loader.h"
#include "tinykv/config/command_line.h"

#include <iostream>
#include <exception>
#include <string>
#include <optional>

void PrintUsage(const char* program)
{
    std::cout
        << "Usage: "
        << program
        << " [options]\n\n"
        << "Options:\n"
        << "  -c, --config <path>       Configuration file\n"
        << "      --host <address>      Listen address\n"
        << "      --port <port>         Listen port\n"
        << "      --poller <backend>    auto|poll|epoll\n"
        << "      --sub-reactors <n>    0=auto, 1-16=explicit\n"
        << "      --log-level <level>   debug|info|warn|error\n"
        << "  -h, --help                Show this help\n";
}

bool NeedShowHelp(int argc, char *argv[])
{
    // --help 和 —h 只能单独出现
    bool findHelp = false;
    for (int i = 1; i < argc; i++) {
        const std::string_view argument = argv[i];
        if (argument == "--help" || argument == "-h") {
            findHelp = true;
            break;
        }
    }

    if (!findHelp) {
        return false;
    }

    if (argc == 2) {
        return true;
    } else {
        throw std::invalid_argument("--help and -h can only be used alone");
    }
}

int main(int argc, char *argv[])
{
    try {
        if (NeedShowHelp(argc, argv)) {
            PrintUsage(argv[0]);
            return 0;
        }

        tinykv::AppConfig config;
        // 先解析是否存在配置文件
        const std::optional<std::string> configPath = tinykv::FindConfigPath(argc, argv);
        if (configPath.has_value()) {
            config = tinykv::ConfigLoader::LoadFromFile(configPath.value());
        }

        // 再解析 CLI 配置参数，注意 CLI 参数可以覆盖配置文件
        tinykv::ApplyCommandLineOverrides(argc, argv, config);

        // 再次校验
        tinykv::ConfigLoader::Validate(config);

        // 启动前打印配置信息
        std::cout << "[Server Config Info]" << tinykv::ConfigLoader::BuildConfigStr(config) << std::endl;

        tinykv::Logger::Instance().SetLevel(config.logLevel);

        // signalHandler 必须在 server 之后构造。
        // 局部对象逆序析构，因此会先恢复信号处理器，再销毁 TcpServer 和 WakeupChannel。
        tinykv::TcpServer server(
            config.host, config.port, config.sweepInterval,
            config.network, config.reactor, config.shutdown);
        tinykv::TerminationSignalHandler signals(server.StopNotificationFd());
        server.Run(); 
    } catch (const std::exception &e) {
        std::cerr << "tinykv_server start error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}