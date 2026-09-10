/**
 * 封装 posix error 相关公共方法
 */
#pragma once

#include <string>

namespace tinykv {

std::string ErrorMessage(const char* prefix);

} // namespace tinykv

