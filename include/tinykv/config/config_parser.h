#pragma once

#include "tinykv/common/server_info.h"
#include "tinykv/observability/logger.h"

#include <string_view>

namespace tinykv {

PollerBackend ParsePollerBackend(const std::string_view value);

LogLevel ParseLogLevel(const std::string_view value);

std::string ParsePollerBackendName(PollerBackend backend);

} // namespace tinykv