function(tinykv_configure_sanitizers target_name)
    if (NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        if (TINYKV_ENABLE_ASAN OR TINYKV_ENABLE_UBSAN OR TINYKV_ENABLE_TSAN)
            message(FATAL_ERROR "当前编译器不在本项目的 Sanitizer 支持范围内")
        endif()

        return()
    endif()

    # TSan 使用独立构建目录运行，
    # 不和 ASan/UBSan 放进同一个构建配置。
    if (TINYKV_ENABLE_TSAN AND (TINYKV_ENABLE_ASAN OR TINYKV_ENABLE_UBSAN))
        message(FATAL_ERROR "TSan 必须使用独立构建，请勿同时启用 ASan/UBSan")
    endif()

    set(enabled_sanitizers)

    if (TINYKV_ENABLE_ASAN)
        list(APPEND enabled_sanitizers address)
    endif()

    if(TINYKV_ENABLE_UBSAN)
        list(APPEND enabled_sanitizers undefined)
    endif()

    if(TINYKV_ENABLE_TSAN)
        list(APPEND enabled_sanitizers thread)
    endif()

    if (NOT enabled_sanitizers)
        return()
    endif()

    list(JOIN enabled_sanitizers "," sanitizer_list)
    message(STATUS "TinyKVCache sanitizers: ${sanitizer_list}")

    target_compile_options(${target_name} INTERFACE
        "-fsanitize=${sanitizer_list}"
        -fno-omit-frame-pointer
    )

    target_link_options(${target_name} INTERFACE
        "-fsanitize=${sanitizer_list}"
    )
endfunction()


