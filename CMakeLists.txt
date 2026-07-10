cmake_minimum_required(VERSION 3.16)

project(TinyKVCache
    VERSION 0.0.1
    DESCRIPTION "A tiny C++17 TCP key-value cache server"
    LANGUAGES CXX)

# 使用 C++ 17
set(CMAKE_CXX_STANDARD 17)              # 使用 C++ 17
set(CMAKE_CXX_STANDARD_REQUIRED ON)     # 必须使用 C++ 17
set(CMAKE_CXX_EXTENSIONS OFF)           # 不要使用编译器私有扩展

# 测试开关，默认构建测试
# 构建时可关闭：cmake -S . -B build -DTINYKV_BUILD_TESTS=OFF
option(TINYKV_BUILD_TESTS "Build TinyKVCache tests" ON)

# 公共编译选项
# 这是一个只携带编译选项的 target，它不生成 .a 或可执行文件，只负责给其他 target 传递：
# cxx_std_17
# -Wall
# -Wextra、
# -Wpedantic
add_library(tinykv_options INTERFACE)
target_compile_features(tinykv_options INTERFACE cxx_std_17)
if (MSVC)
    target_compile_options(tinykv_options INTERFACE /W4)
else()
    target_compile_options(tinykv_options INTERFACE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

# Header-only placeholder target for phase 0.
# In later phases, this will become a real static library with .cpp files.
add_library(tinykv INTERFACE)

target_include_directories(tinykv INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(tinykv INTERFACE tinykv_options)

# 测试相关
if (TINYKV_BUILD_TESTS)
    enable_testing()
    add_executable(test_smoke
        tests/test_smoke.cpp
    )
    target_link_libraries(test_smoke PRIVATE
        tinykv
    )

    add_test(NAME smoke COMMAND test_smoke)
endif()