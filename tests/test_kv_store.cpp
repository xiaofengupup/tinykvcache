#include "tinykv/core/kv_store.h"

#include <cassert>
#include <thread>
#include <iostream>

namespace {

void TestSetAndGet()
{
    tinykv::KVStore store;

    std::string value;
    assert(!store.Get("name", value));

    store.Set("name", "xiaofeng");
    assert(store.Get("name", value));
    assert(value == "xiaofeng");
}

void TestSetOverwriteValue()
{
    tinykv::KVStore store;

    store.Set("name", "xiaofeng");
    store.Set("name", "han");

    std::string value;
    assert(store.Get("name", value));
    assert(value == "han");
    assert(store.Size() == 1);
}

void TestDel()
{
    tinykv::KVStore store;

    store.Set("name", "xiaofeng");
    assert(store.Del("name"));
    assert(!store.Del("name"));
}

void TestTtlForMissingKey()
{
    tinykv::KVStore store;
    assert(store.Ttl("mising") == -2);
}

void TestTtlForPersistentKey()
{
    tinykv::KVStore store;

    store.Set("name", "xiaofeng");
    assert(store.Ttl("name") == -1);
}

void TestExpireForMissingKeyShouldFail()
{
    tinykv::KVStore store;
    assert(!store.Expire("missing", 10));
}

void TestExpireWithInvalidSecondsShouldFail()
{
    tinykv::KVStore store;

    store.Set("name", "xiaofeng");
    assert(!store.Expire("name", 0));
    assert(!store.Expire("name", -1));

    // 失败的 expire 不应该删除 key。
    std::string value;
    assert(store.Get("name", value));
    assert(value == "xiaofeng");
}

void TestExpireAndTtl()
{
    tinykv::KVStore store;

    store.Set("name", "xiaofeng");
    assert(store.Expire("name", 2));


    // 刚设置 2 秒过期，剩余 TTL 应该在 1 到 2 秒之间。
    const int remain = store.Ttl("name");
    assert(remain >= 1);
    assert(remain <= 2);
}

void TestKeyShouldExpire()
{
    tinykv::KVStore store;

    store.Set("temp", "value");
    assert(store.Expire("temp", 1));

    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    
    std::string value;
    assert(!store.Get("temp", value));
    assert(store.Ttl("temp") == -2);
    assert(store.Size() == 0);
}

void TestSetShouldClearOldTtl()
{
    tinykv::KVStore store;

    store.Set("name", "xiaofeng");
    assert(store.Expire("name", 1));

    store.Set("name", "han");
    assert(store.Ttl("name") == -1);

    std::this_thread::sleep_for(std::chrono::milliseconds(1200));

    std::string value;
    assert(store.Get("name", value));
    assert(value == "han");
}

void TestSweepExpired()
{
    tinykv::KVStore store;

    store.Set("a", "1");
    store.Set("b", "2");
    store.Set("c", "3");

    assert(store.Expire("a", 1));
    assert(store.Expire("b", 1));

    std::this_thread::sleep_for(std::chrono::milliseconds(1200));

    const std::size_t removed = store.SweepExpired();
    assert(removed == 2);
    assert(store.Size() == 1);

    std::string value;
    assert(store.Get("c", value));
    assert(value == "3");
}

void TestStats()
{
    tinykv::KVStore store;

    store.Set("a", "1");
    store.Set("b", "2");
    store.Set("c", "3");

    assert(store.Expire("b", 10));
    assert(store.Expire("c", 10));

    const auto stats = store.Stats();
    assert(stats.keys == 3);
    assert(stats.persistentKeys == 1);
    assert(stats.expiringKeys == 2);
}


void TestExpiredKeyShouldNotBeDeletedAsExistingKey()
{
    tinykv::KVStore store;

    store.Set("temp", "value");
    assert(store.Expire("temp", 1));

    std::this_thread::sleep_for(std::chrono::milliseconds(1200));

    // key 已经过期，此时 del 应该返回 false。
    assert(!store.Del("temp"));
    assert(store.Size() == 0);
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
