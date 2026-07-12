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

# tinykv 核心库
add_library(tinykv STATIC
    src/core/frame_codec.cpp
    src/core/command_parser.cpp
    src/core/kv_store.cpp

    src/net/scoped_fd.cpp
    src/net/socket_util.cpp
)
target_include_directories(tinykv PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(tinykv PUBLIC tinykv_options)

# 测试相关
if (TINYKV_BUILD_TESTS)
    enable_testing()

    # 冒烟测试
    add_executable(test_smoke
        tests/test_smoke.cpp
    )
    target_link_libraries(test_smoke PRIVATE
        tinykv
    )
    add_test(NAME smoke COMMAND test_smoke)

    # 帧解析测试
    add_executable(test_frame_codec
        tests/test_frame_codec.cpp
    )
    target_link_libraries(test_frame_codec PRIVATE
        tinykv
    )
    add_test(NAME frame_codec COMMAND test_frame_codec)

    # 命令解析测试
    add_executable(test_command_parser
        tests/test_command_parser.cpp
    )
    target_link_libraries(test_command_parser PRIVATE
        tinykv
    )
    add_test(NAME command_parser COMMAND test_command_parser)

    # kv 内存存储测试
    add_executable(test_kv_store
        tests/test_kv_store.cpp
    )
    target_link_libraries(test_kv_store PRIVATE
        tinykv
    )
    add_test(NAME kv_store COMMAND test_kv_store)

    # ScopedFd 测试
    add_executable(test_scoped_fd
        tests/test_scoped_fd.cpp
    )
    target_link_libraries(test_scoped_fd PRIVATE
        tinykv
    )
    add_test(NAME scoped_fd COMMAND test_scoped_fd)

    # SocketUtil 测试
    add_executable(test_socket_util
        tests/test_socket_util.cpp
    )
    target_link_libraries(test_socket_util PRIVATE
        tinykv
    )
    add_test(NAME socket_util COMMAND test_socket_util)
endif()