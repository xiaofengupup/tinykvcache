#include "tinykv/core/command_parser.h"

#include <gtest/gtest.h>
#include <iostream>
#include <string>
#include <limits>

namespace {

TEST(CommandParserTest, EmptyCommandShouldBeUnknown)
{
    tinykv::Command command = tinykv::ParseCommand("");

    EXPECT_EQ(command.type, tinykv::CommandType::Unknown);
    EXPECT_TRUE(command.key.empty());
    EXPECT_TRUE(command.value.empty());
    EXPECT_EQ(command.seconds, 0);
}

TEST(CommandParserTest, BlankCommandShouldBeUnknown)
{
    tinykv::Command command = tinykv::ParseCommand("    \t    ");
    EXPECT_EQ(command.type, tinykv::CommandType::Unknown);
}

TEST(CommandParserTest, PingCommand)
{
    auto c = tinykv::ParseCommand("PING");
    EXPECT_EQ(c.type, tinykv::CommandType::Ping);
    EXPECT_EQ(c.raw, "PING");

    c = tinykv::ParseCommand("ping");
    EXPECT_EQ(c.type, tinykv::CommandType::Ping);

    c = tinykv::ParseCommand("PiNg");
    EXPECT_EQ(c.type, tinykv::CommandType::Ping);

    // ping 命令不能携带额外参数
    c = tinykv::ParseCommand("Ping extra");
    EXPECT_EQ(c.type, tinykv::CommandType::Unknown);
}

TEST(CommandParser, GetCommand)
{
    auto c = tinykv::ParseCommand("GET name");
    EXPECT_EQ(c.type, tinykv::CommandType::Get);
    EXPECT_EQ(c.key,     "name");

    // key 的大小写应该保留
    c = tinykv::ParseCommand(("GET UserName"));
    EXPECT_EQ(c.type, tinykv::CommandType::Get);
    EXPECT_EQ(c.key, "UserName");

    // GET 命令必须有一个 key
    c = tinykv::ParseCommand("GET");
    EXPECT_EQ(c.type, tinykv::CommandType::Unknown);

    c = tinykv::ParseCommand("GET name extra");
    EXPECT_EQ(c.type, tinykv::CommandType::Unknown);
}

TEST(CommandParser, DelCommand)
{
    auto c = tinykv::ParseCommand("DEL name");
    EXPECT_EQ(c.type, tinykv::CommandType::Del);
    EXPECT_EQ(c.key, "name");

    c = tinykv::ParseCommand("del name");
    EXPECT_EQ(c.type, tinykv::CommandType::Del);
    EXPECT_EQ(c.key, "name");

    c = tinykv::ParseCommand("DEL");
    EXPECT_EQ(c.type, tinykv::CommandType::Unknown);

    c = tinykv::ParseCommand("DEL name extra");
    EXPECT_EQ(c.type, tinykv::CommandType::Unknown);
}

TEST(CommandParser, TtlCommand)
{
    auto c = tinykv::ParseCommand("TTL name");

    EXPECT_EQ(c.type, tinykv::CommandType::Ttl);
    EXPECT_EQ(c.key, "name");

    c = tinykv::ParseCommand("ttl name");
    EXPECT_EQ(c.type, tinykv::CommandType::Ttl);

    c = tinykv::ParseCommand("TTL");
    EXPECT_EQ(c.type, tinykv::CommandType::Unknown);

    c = tinykv::ParseCommand("TTL name extra");
    EXPECT_EQ(c.type, tinykv::CommandType::Unknown);
}

TEST(CommandParser, StatsCommand)
{
    auto c = tinykv::ParseCommand("STATS");

    EXPECT_EQ(c.type, tinykv::CommandType::Stats);

    c = tinykv::ParseCommand("stats");
    EXPECT_EQ(c.type, tinykv::CommandType::Stats);

    // STATS 不应该携带额外参数。
    c = tinykv::ParseCommand("STATS extra");
    EXPECT_EQ(c.type, tinykv::CommandType::Unknown);
}

TEST(CommandParser, QuitCommand)
{
    auto c = tinykv::ParseCommand("QUIT");

    EXPECT_EQ(c.type, tinykv::CommandType::Quit);

    c = tinykv::ParseCommand("quit");
    EXPECT_EQ(c.type, tinykv::CommandType::Quit);

    // QUIT 不应该携带额外参数。
    c = tinykv::ParseCommand("QUIT now");
    EXPECT_EQ(c.type, tinykv::CommandType::Unknown);
}

TEST(CommandParser, SetCommand)
{
    auto c = tinykv::ParseCommand("SET name xiaofeng");

    EXPECT_EQ(c.type, tinykv::CommandType::Set);
    EXPECT_EQ(c.key, "name");
    EXPECT_EQ(c.value, "xiaofeng");
}

TEST(CommandParser, SetCommandValueCanContainSpaces)
{
    auto c = tinykv::ParseCommand("SET sentence hello tiny kv cache");

    EXPECT_EQ(c.type, tinykv::CommandType::Set);
    EXPECT_EQ(c.key, "sentence");
    EXPECT_EQ(c.value, "hello tiny kv cache");
}

TEST(CommandParser, SetCommandKeepsInnerSpacesInValue)
{
    auto c = tinykv::ParseCommand("  SET   sentence   hello   tiny   kv   ");

    EXPECT_EQ(c.type, tinykv::CommandType::Set);
    EXPECT_EQ(c.key, "sentence");
    // 本项目约定：
    //   value 支持内部空格；
    //   value 首尾多余空白会被 trim；
    //   value 内部连续空格会保留。
    EXPECT_EQ(c.value, "hello   tiny   kv");
}

TEST(CommandParser, SetCommandInvalidCases)
{
    auto c = tinykv::ParseCommand("SET");

    EXPECT_EQ(c.type, tinykv::CommandType::Unknown);

    c = tinykv::ParseCommand("SET only_key");
    EXPECT_EQ(c.type, tinykv::CommandType::Unknown);

    c = tinykv::ParseCommand("SET key      ");
    EXPECT_EQ(c.type, tinykv::CommandType::Unknown);
}

TEST(CommandParser, ExpireCommand) {
    auto c = tinykv::ParseCommand("EXPIRE name 10");

    EXPECT_EQ(c.type, tinykv::CommandType::Expire);
    EXPECT_EQ(c.key, "name");
    EXPECT_EQ(c.seconds, 10);

    c = tinykv::ParseCommand("expire name 30");
    EXPECT_EQ(c.type, tinykv::CommandType::Expire);
    EXPECT_EQ(c.key, "name");
    EXPECT_EQ(c.seconds, 30);
}

TEST(CommandParser, ExpireCommandInvalidCases)
{
    auto c = tinykv::ParseCommand("EXPIRE");
    EXPECT_EQ(c.type, tinykv::CommandType::Unknown);

    c = tinykv::ParseCommand("EXPIRE name");
    EXPECT_EQ(c.type, tinykv::CommandType::Unknown);

    c = tinykv::ParseCommand("EXPIRE name 0");
    EXPECT_EQ(c.type, tinykv::CommandType::Unknown);

    c = tinykv::ParseCommand("EXPIRE name -1");
    EXPECT_EQ(c.type, tinykv::CommandType::Unknown);

    c = tinykv::ParseCommand("EXPIRE name abc");
    EXPECT_EQ(c.type, tinykv::CommandType::Unknown);

    c = tinykv::ParseCommand("EXPIRE name 10 extra");
    EXPECT_EQ(c.type, tinykv::CommandType::Unknown);

    // 超过 int 范围也应该被拒绝。
    const std::string too_large =
        std::to_string(static_cast<long long>(std::numeric_limits<int>::max()) + 1LL);

    c = tinykv::ParseCommand("EXPIRE name " + too_large);
    EXPECT_EQ(c.type, tinykv::CommandType::Unknown);
}

TEST(CommandParser, RawCommandShouldBePreserved)
{
    const std::string raw = "  SET   name   xiaofeng  ";
    auto c = tinykv::ParseCommand(raw);
    EXPECT_EQ(c.raw, raw);
}

TEST(CommandParser, UnknownCommand)
{
    auto c = tinykv::ParseCommand("HELLO");
    EXPECT_EQ(c.type, tinykv::CommandType::Unknown);

    c = tinykv::ParseCommand("UNKNOWN key value");
    EXPECT_EQ(c.type, tinykv::CommandType::Unknown);
}

} // namespace 
