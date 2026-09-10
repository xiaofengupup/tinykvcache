#include "test_utils.h"

#include "tinykv/core/command_parser.h"
#include "tinykv/core/command_executor.h"
#include "tinykv/core/kv_store.h"

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

void TestPingAndUnknown()
{
    tinykv::KVStore store;

    TINYKV_CHECK(ExecuteLine(store, "PING") == "+PONG");
    TINYKV_CHECK(ExecuteLine(store, "ping") == "+PONG");

    TINYKV_CHECK(ExecuteLine(store, "HELLO") == "-ERR unknown command");
    TINYKV_CHECK(ExecuteLine(store, "SET only_key") == "-ERR unknown command");
}

void TestSetAndGet()
{
    tinykv::KVStore store;

    TINYKV_CHECK(ExecuteLine(store, "SET name xiaofeng") == "+OK");
    TINYKV_CHECK(ExecuteLine(store, "GET name") == "$xiaofeng");
}

void TestSetValueWithSpaces()
{
    tinykv::KVStore store;

    TINYKV_CHECK(ExecuteLine(store, "SET sentence hello tiny kv cache") == "+OK");
    TINYKV_CHECK(ExecuteLine(store, "GET sentence") == "$hello tiny kv cache");
}

void TestDel()
{
    tinykv::KVStore store;

    TINYKV_CHECK(ExecuteLine(store, "SET name xiaofeng") == "+OK");
    TINYKV_CHECK(ExecuteLine(store, "DEL name") == "+OK");
    TINYKV_CHECK(ExecuteLine(store, "GET name") == "$nil");

    // 删除不存在的 key，返回 $nil。
    TINYKV_CHECK(ExecuteLine(store, "DEL name") == "$nil");
}

void TestExpireAndTtl()
{
    tinykv::KVStore store;

    TINYKV_CHECK(ExecuteLine(store, "SET temp value") == "+OK");
    TINYKV_CHECK(ExecuteLine(store, "EXPIRE temp 2") == "+OK");

    const std::string ttl_response = ExecuteLine(store, "TTL temp");

    TINYKV_CHECK(!ttl_response.empty());
    TINYKV_CHECK(ttl_response[0] == '$');

    const int ttl = std::stoi(ttl_response.substr(1));
    TINYKV_CHECK(ttl >= 1);
    TINYKV_CHECK(ttl <= 2);
}

void TestExpiredKeyShouldReturnNil()
{
    tinykv::KVStore store;

    TINYKV_CHECK(ExecuteLine(store, "SET temp value") == "+OK");
    TINYKV_CHECK(ExecuteLine(store, "EXPIRE temp 1") == "+OK");

    std::this_thread::sleep_for(std::chrono::milliseconds(1200));

    TINYKV_CHECK(ExecuteLine(store, "GET temp") == "$nil");
    TINYKV_CHECK(ExecuteLine(store, "TTL temp") == "$-2");
}

void TestExpireMissingKey()
{
    tinykv::KVStore store;

    TINYKV_CHECK(ExecuteLine(store, "EXPIRE missing 10") == "$nil");
}

void TestStats() {
    tinykv::KVStore store;

    TINYKV_CHECK(ExecuteLine(store, "SET a 1") == "+OK");
    TINYKV_CHECK(ExecuteLine(store, "SET b 2") == "+OK");
    TINYKV_CHECK(ExecuteLine(store, "EXPIRE b 10") == "+OK");

    const std::string response = ExecuteLine(store, "STATS");

    TINYKV_CHECK(response == "+keys=2,persistent=1,expiring=1");
}

void TestQuit()
{
    tinykv::KVStore store;

    TINYKV_CHECK(ExecuteLine(store, "QUIT") == "+BYE");
}

}  // namespace

int main() {
    TestPingAndUnknown();

    TestSetAndGet();
    TestSetValueWithSpaces();

    TestDel();

    TestExpireAndTtl();
    TestExpiredKeyShouldReturnNil();
    TestExpireMissingKey();

    TestStats();
    TestQuit();

    std::cout << "command executor tests passed\n";
    return 0;
}