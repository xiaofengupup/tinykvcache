#include "tinykv/core/command.h"

#include <cassert>
#include <iostream>
#include <string>
#include <limits>

namespace {

void TestEmptyCommandShouldBeUnknown()
{
    tinykv::Command command = tinykv::ParseCommand("");

    assert(command.type == tinykv::CommandType::Unknown);
    assert(command.key.empty());
    assert(command.value.empty());
    assert(command.seconds == 0);
}

void TestBlankCommandShouldBeUnknown()
{
    tinykv::Command command = tinykv::ParseCommand("    \t    ");
    assert(command.type == tinykv::CommandType::Unknown);
}

void TestPingCommand()
{
    auto c = tinykv::ParseCommand("PING");
    assert(c.type == tinykv::CommandType::Ping);
    assert(c.raw == "PING");

    c = tinykv::ParseCommand("ping");
    assert(c.type == tinykv::CommandType::Ping);

    c = tinykv::ParseCommand("PiNg");
    assert(c.type == tinykv::CommandType::Ping);

    // ping 命令不能携带额外参数
    c = tinykv::ParseCommand("Ping extra");
    assert(c.type == tinykv::CommandType::Unknown);
}

void TestGetCommand()
{
    auto c = tinykv::ParseCommand("GET name");
    assert(c.type == tinykv::CommandType::Get);
    assert(c.key == "name");

    // key 的大小写应该保留
    c = tinykv::ParseCommand(("GET UserName"));
    assert(c.type == tinykv::CommandType::Get);
    assert(c.key == "UserName");

    // GET 命令必须有一个 key
    c = tinykv::ParseCommand("GET");
    assert(c.type == tinykv::CommandType::Unknown);

    c = tinykv::ParseCommand("GET name extra");
    assert(c.type == tinykv::CommandType::Unknown);
}

void TestDelCommand()
{
    auto c = tinykv::ParseCommand("DEL name");
    assert(c.type == tinykv::CommandType::Del);
    assert(c.key == "name");

    c = tinykv::ParseCommand("del name");
    assert(c.type == tinykv::CommandType::Del);
    assert(c.key == "name");

    c = tinykv::ParseCommand("DEL");
    assert(c.type == tinykv::CommandType::Unknown);

    c = tinykv::ParseCommand("DEL name extra");
    assert(c.type == tinykv::CommandType::Unknown);
}

void TestTtlCommand()
{
    auto c = tinykv::ParseCommand("TTL name");

    assert(c.type == tinykv::CommandType::Ttl);
    assert(c.key == "name");

    c = tinykv::ParseCommand("ttl name");
    assert(c.type == tinykv::CommandType::Ttl);

    c = tinykv::ParseCommand("TTL");
    assert(c.type == tinykv::CommandType::Unknown);

    c = tinykv::ParseCommand("TTL name extra");
    assert(c.type == tinykv::CommandType::Unknown);
}

void TestStatsCommand()
{
    auto c = tinykv::ParseCommand("STATS");

    assert(c.type == tinykv::CommandType::Stats);

    c = tinykv::ParseCommand("stats");
    assert(c.type == tinykv::CommandType::Stats);

    // STATS 不应该携带额外参数。
    c = tinykv::ParseCommand("STATS extra");
    assert(c.type == tinykv::CommandType::Unknown);
}

void TestQuitCommand()
{
    auto c = tinykv::ParseCommand("QUIT");

    assert(c.type == tinykv::CommandType::Quit);

    c = tinykv::ParseCommand("quit");
    assert(c.type == tinykv::CommandType::Quit);

    // QUIT 不应该携带额外参数。
    c = tinykv::ParseCommand("QUIT now");
    assert(c.type == tinykv::CommandType::Unknown);
}

void TestSetCommandBasic()
{
    auto c = tinykv::ParseCommand("SET name xiaofeng");

    assert(c.type == tinykv::CommandType::Set);
    assert(c.key == "name");
    assert(c.value == "xiaofeng");
}



void TestSetCommandValueCanContainSpaces()
{
    auto c = tinykv::ParseCommand("SET sentence hello tiny kv cache");

    assert(c.type == tinykv::CommandType::Set);
    assert(c.key == "sentence");
    assert(c.value == "hello tiny kv cache");
}

void TestSetCommandKeepsInnerSpacesInValue()
{
    auto c = tinykv::ParseCommand("  SET   sentence   hello   tiny   kv   ");

    assert(c.type == tinykv::CommandType::Set);
    assert(c.key == "sentence");
     // 本项目约定：
    //   value 支持内部空格；
    //   value 首尾多余空白会被 trim；
    //   value 内部连续空格会保留。
    assert(c.value == "hello   tiny   kv");
}

void TestSetCommandInvalidCases()
{
    auto c = tinykv::ParseCommand("SET");

    assert(c.type == tinykv::CommandType::Unknown);

    c = tinykv::ParseCommand("SET only_key");
    assert(c.type == tinykv::CommandType::Unknown);

    c = tinykv::ParseCommand("SET key      ");
    assert(c.type == tinykv::CommandType::Unknown);
}

void TestExpireCommand() {
    auto c = tinykv::ParseCommand("EXPIRE name 10");

    assert(c.type == tinykv::CommandType::Expire);
    assert(c.key == "name");
    assert(c.seconds == 10);

    c = tinykv::ParseCommand("expire name 30");
    assert(c.type == tinykv::CommandType::Expire);
    assert(c.key == "name");
    assert(c.seconds == 30);
}

void TestExpireCommandInvalidCases()
{
    auto c = tinykv::ParseCommand("EXPIRE");
    assert(c.type == tinykv::CommandType::Unknown);

    c = tinykv::ParseCommand("EXPIRE name");
    assert(c.type == tinykv::CommandType::Unknown);

    c = tinykv::ParseCommand("EXPIRE name 0");
    assert(c.type == tinykv::CommandType::Unknown);

    c = tinykv::ParseCommand("EXPIRE name -1");
    assert(c.type == tinykv::CommandType::Unknown);

    c = tinykv::ParseCommand("EXPIRE name abc");
    assert(c.type == tinykv::CommandType::Unknown);

    c = tinykv::ParseCommand("EXPIRE name 10 extra");
    assert(c.type == tinykv::CommandType::Unknown);

    // 超过 int 范围也应该被拒绝。
    const std::string too_large =
        std::to_string(static_cast<long long>(std::numeric_limits<int>::max()) + 1LL);

    c = tinykv::ParseCommand("EXPIRE name " + too_large);
    assert(c.type == tinykv::CommandType::Unknown);
}

void TestRawCommandShouldBePreserved()
{
    const std::string raw = "  SET   name   xiaofeng  ";
    auto c = tinykv::ParseCommand(raw);
    assert(c.raw == raw);
}

void TestUnknownCommand() {
    auto c = tinykv::ParseCommand("HELLO");
    assert(c.type == tinykv::CommandType::Unknown);

    c = tinykv::ParseCommand("UNKNOWN key value");
    assert(c.type == tinykv::CommandType::Unknown);
}

} // namespace 


int main() {
    TestEmptyCommandShouldBeUnknown();
    TestBlankCommandShouldBeUnknown();

    TestPingCommand();
    TestGetCommand();
    TestDelCommand();
    TestTtlCommand();
    TestStatsCommand();
    TestQuitCommand();

    TestSetCommandBasic();
    TestSetCommandValueCanContainSpaces();
    TestSetCommandKeepsInnerSpacesInValue();
    TestSetCommandInvalidCases();

    TestExpireCommand();
    TestExpireCommandInvalidCases();

    TestRawCommandShouldBePreserved();
    TestUnknownCommand();

    std::cout << "command parser tests passed\n";
    return 0;
}
