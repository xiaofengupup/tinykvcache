#include "test_utils.h"

#include "tinykv/core/kv_store.h"

#include <thread>
#include <iostream>

namespace {

void TestSetAndGet()
{
    tinykv::KVStore store;

    std::string value;
    TINYKV_CHECK(!store.Get("name", value));

    store.Set("name", "xiaofeng");
    TINYKV_CHECK(store.Get("name", value));
    TINYKV_CHECK(value == "xiaofeng");
}

void TestSetOverwriteValue()
{
    tinykv::KVStore store;

    store.Set("name", "xiaofeng");
    store.Set("name", "han");

    std::string value;
    TINYKV_CHECK(store.Get("name", value));
    TINYKV_CHECK(value == "han");
    TINYKV_CHECK(store.Size() == 1);
}

void TestDel()
{
    tinykv::KVStore store;

    store.Set("name", "xiaofeng");
    TINYKV_CHECK(store.Del("name"));
    TINYKV_CHECK(!store.Del("name"));
}

void TestTtlForMissingKey()
{
    tinykv::KVStore store;
    TINYKV_CHECK(store.Ttl("mising") == -2);
}

void TestTtlForPersistentKey()
{
    tinykv::KVStore store;

    store.Set("name", "xiaofeng");
    TINYKV_CHECK(store.Ttl("name") == -1);
}

void TestExpireForMissingKeyShouldFail()
{
    tinykv::KVStore store;
    TINYKV_CHECK(!store.Expire("missing", 10));
}

void TestExpireWithInvalidSecondsShouldFail()
{
    tinykv::KVStore store;

    store.Set("name", "xiaofeng");
    TINYKV_CHECK(!store.Expire("name", 0));
    TINYKV_CHECK(!store.Expire("name", -1));

    // 失败的 expire 不应该删除 key。
    std::string value;
    TINYKV_CHECK(store.Get("name", value));
    TINYKV_CHECK(value == "xiaofeng");
}

void TestExpireAndTtl()
{
    tinykv::KVStore store;

    store.Set("name", "xiaofeng");
    TINYKV_CHECK(store.Expire("name", 2));


    // 刚设置 2 秒过期，剩余 TTL 应该在 1 到 2 秒之间。
    const int remain = store.Ttl("name");
    TINYKV_CHECK(remain >= 1);
    TINYKV_CHECK(remain <= 2);
}

void TestKeyShouldExpire()
{
    tinykv::KVStore store;

    store.Set("temp", "value");
    TINYKV_CHECK(store.Expire("temp", 1));

    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    
    std::string value;
    TINYKV_CHECK(!store.Get("temp", value));
    TINYKV_CHECK(store.Ttl("temp") == -2);
    TINYKV_CHECK(store.Size() == 0);
}

void TestSetShouldClearOldTtl()
{
    tinykv::KVStore store;

    store.Set("name", "xiaofeng");
    TINYKV_CHECK(store.Expire("name", 1));

    store.Set("name", "han");
    TINYKV_CHECK(store.Ttl("name") == -1);

    std::this_thread::sleep_for(std::chrono::milliseconds(1200));

    std::string value;
    TINYKV_CHECK(store.Get("name", value));
    TINYKV_CHECK(value == "han");
}

void TestSweepExpired()
{
    tinykv::KVStore store;

    store.Set("a", "1");
    store.Set("b", "2");
    store.Set("c", "3");

    TINYKV_CHECK(store.Expire("a", 1));
    TINYKV_CHECK(store.Expire("b", 1));

    std::this_thread::sleep_for(std::chrono::milliseconds(1200));

    const std::size_t removed = store.SweepExpired();
    TINYKV_CHECK(removed == 2);
    TINYKV_CHECK(store.Size() == 1);

    std::string value;
    TINYKV_CHECK(store.Get("c", value));
    TINYKV_CHECK(value == "3");
}

void TestStats()
{
    tinykv::KVStore store;

    store.Set("a", "1");
    store.Set("b", "2");
    store.Set("c", "3");

    TINYKV_CHECK(store.Expire("b", 10));
    TINYKV_CHECK(store.Expire("c", 10));

    const auto stats = store.Stats();
    TINYKV_CHECK(stats.keys == 3);
    TINYKV_CHECK(stats.persistentKeys == 1);
    TINYKV_CHECK(stats.expiringKeys == 2);
}


void TestExpiredKeyShouldNotBeDeletedAsExistingKey()
{
    tinykv::KVStore store;

    store.Set("temp", "value");
    TINYKV_CHECK(store.Expire("temp", 1));

    std::this_thread::sleep_for(std::chrono::milliseconds(1200));

    // key 已经过期，此时 del 应该返回 false。
    TINYKV_CHECK(!store.Del("temp"));
    TINYKV_CHECK(store.Size() == 0);
}
    
} // namespace

int main()
{
    TestSetAndGet();
    TestSetOverwriteValue();
    TestDel();

    TestTtlForMissingKey();
    TestTtlForPersistentKey();

    TestExpireForMissingKeyShouldFail();
    TestExpireWithInvalidSecondsShouldFail();
    TestExpireAndTtl();
    TestKeyShouldExpire();
    TestSetShouldClearOldTtl();

    TestSweepExpired();
    TestStats();
    TestExpiredKeyShouldNotBeDeletedAsExistingKey();

    std::cout << "kv store tests passed\n";
    return 0;
}
