cmake_minimum_required(VERSION 3.16)

project(TinyKVCache
    VERSION 0.0.1
    DESCRIPTION "A tiny C++17 TCP key-value cache server"
    LANGUAGES CXX)

# 生成 compile_commands.json，方便 clangd、clang-tidy 和 IDE 使用。
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TINYKV_WARNINGS_AS_ERRORS "将编译告警视为错误" ON)
option(TINYKV_ENABLE_ASAN "启用 AddressSanitizer" OFF)
option(TINYKV_ENABLE_UBSAN "启用 UndefinedBehaviorSanitizer" OFF)
option(TINYKV_ENABLE_TSAN "启用 ThreadSanitizer" OFF)
option(TINYKV_BUILD_INTEGRATION_TESTS "构建并运行端到端集成测试" ON)

list(APPEND CMAKE_MODULE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
)

include(compiler_warnings)
include(sanitizers)

find_package(Threads REQUIRED)

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
endif()

add_library(tinykv_warnings INTERFACE)
tinykv_config_warnings(tinykv_warnings)

add_library(tinykv_sanitizers INTERFACE)
tinykv_configure_sanitizers(tinykv_sanitizers)

# tinykv 核心库
add_library(tinykv STATIC
    src/core/frame_codec.cpp
    src/core/command_parser.cpp
    src/core/kv_store.cpp
    src/core/command_executor.cpp

    src/net/scoped_fd.cpp
    src/net/socket_util.cpp
    src/net/tcp_server.cpp
    src/net/output_buffer.cpp

    src/net/poll/poller_factory.cpp
    src/net/poll/poller_common.cpp
    src/net/poll/poll_poller.cpp

    src/net/wakeup/wakeup_channel.cpp
    src/net/wakeup/termination_signal_handler.cpp
)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(tinykv PRIVATE
        src/net/poll/epoll_poller.cpp
    )
    target_compile_definitions(tinykv PUBLIC
        TINYKV_HAS_EPOLL=1
    )
else()
    target_compile_definitions(tinykv PUBLIC
        TINYKV_HAS_EPOLL=0
    )
endif()

target_include_directories(tinykv PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(tinykv PUBLIC 
    tinykv_options
    tinykv_warnings
    tinykv_sanitizers
    Threads::Threads
)

add_executable(tinykv_client
    app/client_main.cpp
)
target_link_libraries(tinykv_client PRIVATE
    tinykv
)

add_executable(tinykv_server
    app/server_main.cpp
)
target_link_libraries(tinykv_server PRIVATE
    tinykv
)

# 测试相关
if (TINYKV_BUILD_TESTS)
    enable_testing()

    add_library(tinykv_test_options INTERFACE)
    target_include_directories(tinykv_test_options INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    # 冒烟测试
    add_executable(test_smoke
        tests/test_smoke.cpp
    )
    target_link_libraries(test_smoke PRIVATE
        tinykv tinykv_test_options
    )
    add_test(NAME smoke COMMAND test_smoke)

    # 帧解析测试
    add_executable(test_frame_codec
        tests/test_frame_codec.cpp
    )
    target_link_libraries(test_frame_codec PRIVATE
        tinykv tinykv_test_options
    )
    add_test(NAME frame_codec COMMAND test_frame_codec)

    # 命令解析测试
    add_executable(test_command_parser
        tests/test_command_parser.cpp
    )
    target_link_libraries(test_command_parser PRIVATE
        tinykv tinykv_test_options
    )
    add_test(NAME command_parser COMMAND test_command_parser)

    # kv 内存存储测试
    add_executable(test_kv_store
        tests/test_kv_store.cpp
    )
    target_link_libraries(test_kv_store PRIVATE
        tinykv tinykv_test_options
    )
    add_test(NAME kv_store COMMAND test_kv_store)

    # ScopedFd 测试
    add_executable(test_scoped_fd
        tests/test_scoped_fd.cpp
    )
    target_link_libraries(test_scoped_fd PRIVATE
        tinykv tinykv_test_options
    )
    add_test(NAME scoped_fd COMMAND test_scoped_fd)

    # SocketUtil 测试
    add_executable(test_socket_util
        tests/test_socket_util.cpp
    )
    target_link_libraries(test_socket_util PRIVATE
        tinykv tinykv_test_options
    )
    add_test(NAME socket_util COMMAND test_socket_util)

    # 测试命令执行器
    add_executable(test_command_executor
        tests/test_command_executor.cpp
    )
    target_link_libraries(test_command_executor PRIVATE
        tinykv tinykv_test_options
    )
    add_test(NAME command_executor COMMAND test_command_executor)

    # 测试 OutputBuffer
    add_executable(test_output_buffer
        tests/test_output_buffer.cpp
    )
    target_link_libraries(test_output_buffer PRIVATE
        tinykv tinykv_test_options
    )
    add_test(NAME output_buffer COMMAND test_output_buffer)

    # 测试 Poller
    add_executable(test_poller
        tests/test_poller.cpp
    )
    target_link_libraries(test_poller PRIVATE
        tinykv tinykv_test_options
    )
    add_test(NAME poller COMMAND test_poller)

    # 测试 wakeup channel
    add_executable(test_wakeup_channel
        tests/test_wakeup_channel.cpp
    )
    target_link_libraries(test_wakeup_channel PRIVATE
        tinykv tinykv_test_options
    )
    add_test(NAME wakeup_channel COMMAND test_wakeup_channel)

    set_tests_properties(
        smoke
        frame_codec
        command_parser
        command_executor
        kv_store
        scoped_fd
        socket_util
        output_buffer
        poller
        wakeup_channel
        PROPERTIES
            LABELS "unit"
            TIMEOUT 10
    )
endif()

# 集成测试
if (TINYKV_BUILD_TESTS AND TINYKV_BUILD_INTEGRATION_TESTS)
    find_package(Python3
        COMPONENTS Interpreter
        REQUIRED
    )

    add_test(
        NAME server_e2e
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_server_e2e.py
            --server
            $<TARGET_FILE:tinykv_server>
    )
    set_tests_properties(
        server_e2e
        PROPERTIES
            LABELS "integration"
            TIMEOUT 20
    )

    add_test(
        NAME graceful_shutdown
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_graceful_shutdown.py
            --server
            $<TARGET_FILE:tinykv_server>
            --backend
            auto
    )

    set_tests_properties(
        graceful_shutdown
        PROPERTIES
            LABELS "integration"
            TIMEOUT 15
    )

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_test(
            NAME graceful_shutdown_epoll
            COMMAND
                ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_graceful_shutdown.py
                --server
                $<TARGET_FILE:tinykv_server>
                --backend
                epoll
        )

        set_tests_properties(
            graceful_shutdown_epoll
            PROPERTIES
                LABELS "integration"
                TIMEOUT 15
        )
    endif()
endif()