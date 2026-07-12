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
     */
    static std::string Execute(KVStore &store, const Command &command);
};

} // namespace tinykv