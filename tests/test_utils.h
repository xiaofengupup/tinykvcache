#pragma once

#include <cstdlib>
#include <iostream>

#define TINYKV_CHECK(condition)                                    \
    do {                                                           \
        if (!(condition)) {                                        \
            std::cerr << "测试检查失败: " << #condition             \
                      << "\n文件: " << __FILE__                      \
                      << "\n行号: " << __LINE__                      \
                      << '\n';                                      \
            std::abort();                                          \
        }                                                          \
    } while (false)