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

# 配置依赖项
set(
    FETCHCONTENT_BASE_DIR
    "${CMAKE_SOURCE_DIR}/.deps"
    CACHE PATH
    "Directory to download dependencies"
)

include(FetchContent)

# 配置 fmt 库
set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(FMT_DOC OFF CACHE BOOL "" FORCE)
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 12.2.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(fmt)

# 配置 toml
FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG v3.4.0
)
FetchContent_MakeAvailable(tomlplusplus)

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
    src/net/sub_reactor.cpp

    src/net/poll/poller_factory.cpp
    src/net/poll/poller_common.cpp
    src/net/poll/poll_poller.cpp

    src/net/wakeup/wakeup_channel.cpp
    src/net/wakeup/termination_signal_handler.cpp

    src/observability/logger.cpp
    src/observability/server_metrics.cpp

    src/common/posix_error.cpp

    src/config/config_loader.cpp
    src/config/command_line.cpp
    src/config/config_parser.cpp
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
    fmt::fmt
    tomlplusplus::tomlplusplus
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

    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.17.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(googletest)

    add_executable(tinykv_unit_tests
        tests/test_command_executor.cpp
        tests/test_command_parser.cpp
        tests/test_frame_codec.cpp
        tests/test_kv_store.cpp
        tests/test_output_buffer.cpp
        tests/test_poller.cpp
        tests/test_scoped_fd.cpp
        tests/test_server_metrics.cpp
        tests/test_socket_util.cpp
        tests/test_wakeup_channel.cpp
    )

    target_link_libraries(tinykv_unit_tests PRIVATE
        tinykv
        GTest::gtest_main
    )

    target_compile_options(tinykv_unit_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    # CMake 官方则推荐使用 gtest_discover_tests()，它会从测试程序中自动发现每个 TEST，并分别注册到 CTest
    include(GoogleTest)
    gtest_discover_tests(tinykv_unit_tests
        PROPERTIES
            TIMEOUT 30
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
            --poller
            auto
    )

    set_tests_properties(
        graceful_shutdown
        PROPERTIES
            LABELS "integration"
            TIMEOUT 20
    )

    add_test(
        NAME load_configuration
        COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_config_loading.py
        --server $<TARGET_FILE:tinykv_server>
    )

    set_tests_properties(
        graceful_shutdown
        PROPERTIES
            LABELS "integration"
            TIMEOUT 20
    )

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_test(
            NAME graceful_shutdown_epoll
            COMMAND
                ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_graceful_shutdown.py
                --server
                $<TARGET_FILE:tinykv_server>
                --poller epoll
        )

        set_tests_properties(
            graceful_shutdown_epoll
            PROPERTIES
                LABELS "integration"
                TIMEOUT 15
        )
    endif()
endif()