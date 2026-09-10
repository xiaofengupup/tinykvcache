/**
 * 命令解析
 */
#pragma once

#include <string>

namespace tinykv {

/*
 * 命令类型
 * 
 * Unknown 表示无法识别或者参数不合法的命令。
 */
enum class CommandType {
    Ping,
    Set,
    Get,
    Del,
    Expire,
    Ttl,
    Stats,
    Quit,
    Help,
    Unknown
};

/*
 * 解析后的命令对象
 * 
 * CommandParser 只负责将字符串解析成结构化命令
 */
struct Command {
    CommandType type {CommandType::Unknown};
    std::string key;    // SET GET DEL EXPIRE TTL 使用的 key
    std::string value;  // SET 使用的 value，允许包含空格
    int seconds{0};     // EXPIRE 使用的过期秒数
    std::string raw;    // 原始命令文本，方便后续调试和日志记录。
};

/*
 * 解析一行文本命令
 *
 * 如果命令无法识别，或者参数数量不合法，返回 type = Unknown。
 */
Command ParseCommand(const std::string &line);

} // namespace tinykv
