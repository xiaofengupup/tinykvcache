/**
 * 命令行配置参数解析
 */
#pragma once

#include "tinykv/config/app_config.h"

#include <optional>
#include <string>

namespace tinykv {

std::optional<std::string> FindConfigPath(int argc, char *argv[]);

/**
 * CLI 启动参数：
 *  --config
 *  --host
 *  --port
 *  --log-level
 *  --sub-reactors
 *  --poller
 */
void ApplyCommandLineOverrides(int argc, char *argv[], AppConfig &config);

} // namespace tinykv