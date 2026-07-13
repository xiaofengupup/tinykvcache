function(tinykv_config_warnings target_name)
    if (MSVC)
        set(project_warnings
            /W4
            /permissive-
        )

        if (TINYKV_WARNINGS_AS_ERRORS)
            list(APPEND project_warnings /WX)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        set(project_warnings
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow                # 局部变量遮蔽成员变量或者外层变量
            -Wconversion            # 隐式窄化转换
            -Wsign-conversion       # 有符号和无符号整数混用
            -Wformat=2              # printf 风格格式不匹配
            -Wundef                 # #if 中使用未定义宏
            -Wnon-virtual-dtor
            -Woverloaded-virtual
        )

        if (TINYKV_WARNINGS_AS_ERRORS)
            list(APPEND project_warnings -Werror)
        endif()
    else()
        message(WARNING "当前编译器没有配置专用警告选项：${CMAKE_CXX_COMPILER_ID}")
    endif()

    target_compile_options(${target_name} INTERFACE
        ${project_warnings}
    )
endfunction()
