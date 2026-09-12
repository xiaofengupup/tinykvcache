#include "tinykv/core/command_parser.h"
#include "tinykv/core/command_executor.h"
#include "tinykv/core/kv_store.h"

#include <gtest/gtest.h>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace {

std::string ExecuteLine(tinykv::KVStore& store, const std::string& line)
{
    const auto command = tinykv::ParseCommand(line);
    return tinykv::CommandExecutor::Execute(store, command);
}

TEST(CommandExecutorTest, PingAndUnknown)
{
    tinykv::KVStore store;

    EXPECT_EQ(ExecuteLine(store, "PING"), "+PONG");
    EXPECT_EQ(ExecuteLine(store, "ping"), "+PONG");

    EXPECT_EQ(ExecuteLine(store, "HELLO"), "-ERR unknown command");
    EXPECT_EQ(ExecuteLine(store, "SET only_key"), "-ERR unknown command");
}

TEST(CommandExecutorTest, SetAndGet)
{
    tinykv::KVStore store;

    EXPECT_EQ(ExecuteLine(store, "SET name xiaofeng"), "+OK");
    EXPECT_EQ(ExecuteLine(store, "GET name"), "$xiaofeng");
}

TEST(CommandExecutorTest, SetValueWithSpaces)
{
    tinykv::KVStore store;

    EXPECT_EQ(ExecuteLine(store, "SET sentence hello tiny kv cache"), "+OK");
    EXPECT_EQ(ExecuteLine(store, "GET sentence"), "$hello tiny kv cache");
}

TEST(CommandExecutorTest, Del)
{
    tinykv::KVStore store;

    EXPECT_EQ(ExecuteLine(store, "SET name xiaofeng"), "+OK");
    EXPECT_EQ(ExecuteLine(store, "DEL name"), "+OK");
    EXPECT_EQ(ExecuteLine(store, "GET name"), "$nil");
    // 删除不存在的 key，返回 $nil。
    EXPECT_EQ(ExecuteLine(store, "DEL name"), "$nil");
}

TEST(CommandExecutorTest, ExpireAndTtl)
{
    tinykv::KVStore store;

    EXPECT_EQ(ExecuteLine(store, "SET temp value"), "+OK");
    EXPECT_EQ(ExecuteLine(store, "EXPIRE temp 2"), "+OK");

    const std::string ttl_response = ExecuteLine(store, "TTL temp");

    EXPECT_FALSE(ttl_response.empty());
    EXPECT_EQ(ttl_response[0], '$');

    const int ttl = std::stoi(ttl_response.substr(1));
    EXPECT_GE(ttl, 1);
    EXPECT_LE(ttl, 2);
}

TEST(CommandExecutorTest, ExpiredKeyShouldReturnNil)
{
    tinykv::KVStore store;

    EXPECT_EQ(ExecuteLine(store, "SET temp value"), "+OK");
    EXPECT_EQ(ExecuteLine(store, "EXPIRE temp 1"), "+OK");

    std::this_thread::sleep_for(std::chrono::milliseconds(1200));

    EXPECT_EQ(ExecuteLine(store, "GET temp"), "$nil");
    EXPECT_EQ(ExecuteLine(store, "TTL temp"), "$-2");
}

TEST(CommandExecutorTest, ExpireMissingKeyShouldReturnNil)
{
    tinykv::KVStore store;

    EXPECT_EQ(ExecuteLine(store, "EXPIRE missing 10"), "$nil");
}

TEST(CommandExecutorTest, Stats)
{
    tinykv::KVStore store;

    EXPECT_EQ(ExecuteLine(store, "SET a 1"), "+OK");
    EXPECT_EQ(ExecuteLine(store, "SET b 2"), "+OK");
    EXPECT_EQ(ExecuteLine(store, "EXPIRE b 10"), "+OK");

    const std::string response = ExecuteLine(store, "STATS");

    EXPECT_EQ(response, "+keys=2,persistent=1,expiring=1");
}

TEST(CommandExecutorTest, Quit)
{
    tinykv::KVStore store;

    EXPECT_EQ(ExecuteLine(store, "QUIT"), "+BYE");
}

}  // namespace
