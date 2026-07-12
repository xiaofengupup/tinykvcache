/**
 * 命令执行器
 */
#pragma once

#include "tinykv/core/command_parser.h"
#include "tinykv/core/kv_store.h"

#include <string>

namespace tinykv {

class CommandExecutor {
public:
    /**
     * 执行命令
     * 
     * @param store KVStore 实例
     * @param command 解析后的命令对象
     * @return 命令执行结果字符串
     * 
     * 响应格式约定：
     *   +xxx：普通成功响应
     *   $xxx：字符串/数值响应
     *   $nil：空结果
     *   -ERR xxx：错误响应
     */
    static std::string Execute(KVStore &store, const Command &command);
};

} // namespace tinykv