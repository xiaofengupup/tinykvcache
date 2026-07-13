#include "test_utils.h"

#include "tinykv/core/command_parser.h"

#include <iostream>
#include <string>
#include <limits>

namespace {

void TestEmptyCommandShouldBeUnknown()
{
    tinykv::Command command = tinykv::ParseCommand("");

    TINYKV_CHECK(command.type == tinykv::CommandType::Unknown);
    TINYKV_CHECK(command.key.empty());
    TINYKV_CHECK(command.value.empty());
    TINYKV_CHECK(command.seconds == 0);
}

void TestBlankCommandShouldBeUnknown()
{
    tinykv::Command command = tinykv::ParseCommand("    \t    ");
    TINYKV_CHECK(command.type == tinykv::CommandType::Unknown);
}

void TestPingCommand()
{
    auto c = tinykv::ParseCommand("PING");
    TINYKV_CHECK(c.type == tinykv::CommandType::Ping);
    TINYKV_CHECK(c.raw == "PING");

    c = tinykv::ParseCommand("ping");
    TINYKV_CHECK(c.type == tinykv::CommandType::Ping);

    c = tinykv::ParseCommand("PiNg");
    TINYKV_CHECK(c.type == tinykv::CommandType::Ping);

    // ping 命令不能携带额外参数
    c = tinykv::ParseCommand("Ping extra");
    TINYKV_CHECK(c.type == tinykv::CommandType::Unknown);
}

void TestGetCommand()
{
    auto c = tinykv::ParseCommand("GET name");
    TINYKV_CHECK(c.type == tinykv::CommandType::Get);
    TINYKV_CHECK(c.key == "name");

    // key 的大小写应该保留
    c = tinykv::ParseCommand(("GET UserName"));
    TINYKV_CHECK(c.type == tinykv::CommandType::Get);
    TINYKV_CHECK(c.key == "UserName");

    // GET 命令必须有一个 key
    c = tinykv::ParseCommand("GET");
    TINYKV_CHECK(c.type == tinykv::CommandType::Unknown);

    c = tinykv::ParseCommand("GET name extra");
    TINYKV_CHECK(c.type == tinykv::CommandType::Unknown);
}

void TestDelCommand()
{
    auto c = tinykv::ParseCommand("DEL name");
    TINYKV_CHECK(c.type == tinykv::CommandType::Del);
    TINYKV_CHECK(c.key == "name");

    c = tinykv::ParseCommand("del name");
    TINYKV_CHECK(c.type == tinykv::CommandType::Del);
    TINYKV_CHECK(c.key == "name");

    c = tinykv::ParseCommand("DEL");
    TINYKV_CHECK(c.type == tinykv::CommandType::Unknown);

    c = tinykv::ParseCommand("DEL name extra");
    TINYKV_CHECK(c.type == tinykv::CommandType::Unknown);
}

void TestTtlCommand()
{
    auto c = tinykv::ParseCommand("TTL name");

    TINYKV_CHECK(c.type == tinykv::CommandType::Ttl);
    TINYKV_CHECK(c.key == "name");

    c = tinykv::ParseCommand("ttl name");
    TINYKV_CHECK(c.type == tinykv::CommandType::Ttl);

    c = tinykv::ParseCommand("TTL");
    TINYKV_CHECK(c.type == tinykv::CommandType::Unknown);

    c = tinykv::ParseCommand("TTL name extra");
    TINYKV_CHECK(c.type == tinykv::CommandType::Unknown);
}

void TestStatsCommand()
{
    auto c = tinykv::ParseCommand("STATS");

    TINYKV_CHECK(c.type == tinykv::CommandType::Stats);

    c = tinykv::ParseCommand("stats");
    TINYKV_CHECK(c.type == tinykv::CommandType::Stats);

    // STATS 不应该携带额外参数。
    c = tinykv::ParseCommand("STATS extra");
    TINYKV_CHECK(c.type == tinykv::CommandType::Unknown);
}

void TestQuitCommand()
{
    auto c = tinykv::ParseCommand("QUIT");

    TINYKV_CHECK(c.type == tinykv::CommandType::Quit);

    c = tinykv::ParseCommand("quit");
    TINYKV_CHECK(c.type == tinykv::CommandType::Quit);

    // QUIT 不应该携带额外参数。
    c = tinykv::ParseCommand("QUIT now");
    TINYKV_CHECK(c.type == tinykv::CommandType::Unknown);
}

void TestSetCommandBasic()
{
    auto c = tinykv::ParseCommand("SET name xiaofeng");

    TINYKV_CHECK(c.type == tinykv::CommandType::Set);
    TINYKV_CHECK(c.key == "name");
    TINYKV_CHECK(c.value == "xiaofeng");
}



void TestSetCommandValueCanContainSpaces()
{
    auto c = tinykv::ParseCommand("SET sentence hello tiny kv cache");

    TINYKV_CHECK(c.type == tinykv::CommandType::Set);
    TINYKV_CHECK(c.key == "sentence");
    TINYKV_CHECK(c.value == "hello tiny kv cache");
}

void TestSetCommandKeepsInnerSpacesInValue()
{
    auto c = tinykv::ParseCommand("  SET   sentence   hello   tiny   kv   ");

    TINYKV_CHECK(c.type == tinykv::CommandType::Set);
    TINYKV_CHECK(c.key == "sentence");
     // 本项目约定：
    //   value 支持内部空格；
    //   value 首尾多余空白会被 trim；
    //   value 内部连续空格会保留。
    TINYKV_CHECK(c.value == "hello   tiny   kv");
}

void TestSetCommandInvalidCases()
{
    auto c = tinykv::ParseCommand("SET");

    TINYKV_CHECK(c.type == tinykv::CommandType::Unknown);

    c = tinykv::ParseCommand("SET only_key");
    TINYKV_CHECK(c.type == tinykv::CommandType::Unknown);

    c = tinykv::ParseCommand("SET key      ");
    TINYKV_CHECK(c.type == tinykv::CommandType::Unknown);
}

void TestExpireCommand() {
    auto c = tinykv::ParseCommand("EXPIRE name 10");

    TINYKV_CHECK(c.type == tinykv::CommandType::Expire);
    TINYKV_CHECK(c.key == "name");
    TINYKV_CHECK(c.seconds == 10);

    c = tinykv::ParseCommand("expire name 30");
    TINYKV_CHECK(c.type == tinykv::CommandType::Expire);
    TINYKV_CHECK(c.key == "name");
    TINYKV_CHECK(c.seconds == 30);
}

void TestExpireCommandInvalidCases()
{
    auto c = tinykv::ParseCommand("EXPIRE");
    TINYKV_CHECK(c.type == tinykv::CommandType::Unknown);

    c = tinykv::ParseCommand("EXPIRE name");
    TINYKV_CHECK(c.type == tinykv::CommandType::Unknown);

    c = tinykv::ParseCommand("EXPIRE name 0");
    TINYKV_CHECK(c.type == tinykv::CommandType::Unknown);

    c = tinykv::ParseCommand("EXPIRE name -1");
    TINYKV_CHECK(c.type == tinykv::CommandType::Unknown);

    c = tinykv::ParseCommand("EXPIRE name abc");
    TINYKV_CHECK(c.type == tinykv::CommandType::Unknown);

    c = tinykv::ParseCommand("EXPIRE name 10 extra");
    TINYKV_CHECK(c.type == tinykv::CommandType::Unknown);

    // 超过 int 范围也应该被拒绝。
    const std::string too_large =
        std::to_string(static_cast<long long>(std::numeric_limits<int>::max()) + 1LL);

    c = tinykv::ParseCommand("EXPIRE name " + too_large);
    TINYKV_CHECK(c.type == tinykv::CommandType::Unknown);
}

void TestRawCommandShouldBePreserved()
{
    const std::string raw = "  SET   name   xiaofeng  ";
    auto c = tinykv::ParseCommand(raw);
    TINYKV_CHECK(c.raw == raw);
}

void TestUnknownCommand() {
    auto c = tinykv::ParseCommand("HELLO");
    TINYKV_CHECK(c.type == tinykv::CommandType::Unknown);

    c = tinykv::ParseCommand("UNKNOWN key value");
    TINYKV_CHECK(c.type == tinykv::CommandType::Unknown);
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
