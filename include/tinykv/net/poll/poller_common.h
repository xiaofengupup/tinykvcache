/**
 * Poller 封装相关的一些公共方法
 */
#pragma once

#include "tinykv/net/poll/io_event.h"
#include <chrono>
#include <string>

namespace tinykv {
    
void ValidateInterests(int fd, IoEvent interests);
int ToTimeoutMilliseconds(std::chrono::milliseconds timeout);
std::string ErrorMessage(const char* prefix);

} // namespace tinykv
