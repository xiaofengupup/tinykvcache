#include "tinykv/core/command.h"

#include <cctype>
#include <limits>
#include <sstream>
#include <string>
#include <vector>
#include <functional>

namespace tinykv {

namespace {

bool IsSpace(char ch)
{
    return std::isspace(static_cast<unsigned char>(ch)) != 0;
}

bool IsDigit(char ch)
{
    return std::isdigit(static_cast<unsigned char>(ch)) != 0;
}

std::string Trim(const std::string& text)
{
    std::size_t begin = 0;
    while (begin < text.size() && IsSpace(text[begin])) {
        ++begin;
    }

    std::size_t end = text.size();
    while (end > begin && IsSpace(text[end - 1])) {
        end--;
    }

    return text.substr(begin, end - begin);
}

std::string UpperCopy(std::string text)
{
    for (char &ch : text) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return text;
}

std::vector<std::string> SplitWords(const std::string &text)
{
    std::vector<std::string> words;

    std::istringstream input(text);
    std::string word;
    while (input >> word) {
        words.push_back(word);
    }

    return words;
}

std::string RestAfterNWords(const std::string& text, std::size_t word_count)
{
    std::size_t pos = 0;
    for (std::size_t i = 0; i < word_count; ++i) {
        while (pos < text.size() && IsSpace(text[pos])) {
            ++pos;
        }

        if (pos == text.size()) {
            return "";
        }

        while (pos < text.size() && !IsSpace(text[pos])) {
            ++pos;
        }
    }

    while (pos < text.size() && IsSpace(text[pos])) {
        ++pos;
    }
    if (pos == text.size()) {
        return "";
    }

    // 本项目约定：
    //   SET 的 value 支持内部空格；
    //   value 首尾多余空白会被去掉；
    //   value 内部连续空格会保留。
    return Trim(text.substr(pos));
}

bool ParsePositiveInt(const std::string &text, int &value)
{
    if (text.empty()) {
        return false;
    }

    long long result = 0;
    for (char ch : text) {
        if (!IsDigit(ch)) {
            return false;
        }

        result = result * 10 + static_cast<long long>(ch - '0');
        if (result > std::numeric_limits<int>::max()) {
            return false;
        }
    }

    if (result <= 0) {
        return false;
    }

    value = static_cast<int>(result);
    return true;
}

Command MakeUnknow(const std::string& raw)
{
    Command cmd;
    cmd.type = CommandType::Unknown;
    cmd.raw = raw;

    return cmd;
}

}

Command ParseCommand(const std::string &line)
{
    Command cmd;
    cmd.raw = line;

    const std::string text = Trim(line); // 去除首尾的空格
    if (text.empty()) {
        return cmd;
    }

    const std::vector<std::string> parts = SplitWords(text);
    if (parts.empty()) {
        return cmd;
    }

    // 解析命令
    const std::string op = UpperCopy(parts[0]); // 命令
    const size_t argsCount = parts.size();      // 参数数量
    // 辅助 Lambda：校验参数长度并设置类型
    auto FillBasicCmd = [&](CommandType type, size_t expectedArgCount) {
        if (argsCount != expectedArgCount) {
            return false;
        }
        cmd.type = type;
        if (expectedArgCount >= 2) {
            cmd.key = parts[1];
        }
        return true;
    };

    // 1.简单无参命令
    if (op == "PING")  return FillBasicCmd(CommandType::Ping, 1)  ? cmd : MakeUnknow(line);
    if (op == "STATS") return FillBasicCmd(CommandType::Stats, 1) ? cmd : MakeUnknow(line);
    if (op == "QUIT")  return FillBasicCmd(CommandType::Quit, 1)  ? cmd : MakeUnknow(line);

    // 2.单 Key 命令
    if (op == "GET") return FillBasicCmd(CommandType::Get, 2) ? cmd : MakeUnknow(line);
    if (op == "DEL") return FillBasicCmd(CommandType::Del, 2) ? cmd : MakeUnknow(line);
    if (op == "TTL") return FillBasicCmd(CommandType::Ttl, 2) ? cmd : MakeUnknow(line);

    // 3.set 命令
    if (op == "SET") {
        if (argsCount < 3) {
            return MakeUnknow(line);
        }
        // 获取 value 值
        std::string value = RestAfterNWords(text, 2);
        if (value.empty()) {
            return MakeUnknow(line);
        }

        cmd.type = CommandType::Set;
        cmd.key = parts[1];
        cmd.value = std::move(value);
        return cmd;
    }

    // 4.expire 命令
    if (op == "EXPIRE") {
        if (argsCount != 3) {
            return MakeUnknow(line);
        }
        int seconds = 0;
        if (!ParsePositiveInt(parts[2], seconds)) {
            return MakeUnknow(line);
        }
        cmd.type = CommandType::Expire;
        cmd.key = parts[1];
        cmd.seconds = seconds;
        return cmd;
    }

    return cmd;
}

} // end namespace tinykv