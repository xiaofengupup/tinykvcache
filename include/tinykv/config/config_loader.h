/**
 * 配置加载器
 */
#pragma once

#include "tinykv/config/app_config.h"
#include <string>
#include <toml++/toml.h>

namespace tinykv {

class ConfigLoader {
public:
    /**
     * 从文件中加载配置
     */
    static AppConfig LoadFromFile(const std::string &path);

    /**
     * 验证配置
     */
    static void Validate(const AppConfig &config);

    /**
     * 构建配置字符串
     */
    static std::string BuildConfigStr(const AppConfig &config) noexcept;

private:
    static void ReadServerConfig(AppConfig& config, const toml::table& table);
    static void ReadNetworkConfig(AppConfig& config, const toml::table& table);
    static void ReadReactorConfig(AppConfig& config, const toml::table& table);
    static void ReadShutdownConfig(AppConfig& config, const toml::table& table);
    static void ReadTTLConfig(AppConfig& config, const toml::table& table);
    static void ReadLogConfig(AppConfig& config, const toml::table& table);
};

} // namespace tinykv