#include "tinykv/core/kv_store.h"

#include <gtest/gtest.h>
#include <thread>
#include <iostream>

namespace {

TEST(KVStoreTest, SetAndGet)
{
    tinykv::KVStore store;

    std::string value;
    EXPECT_FALSE(store.Get("name", value));

    store.Set("name", "xiaofeng");
    EXPECT_TRUE(store.Get("name", value));
    EXPECT_EQ(value, "xiaofeng");
}

TEST(KVStoreTest, SetOverwriteValue)
{
    tinykv::KVStore store;

    store.Set("name", "xiaofeng");
    store.Set("name", "han");

    std::string value;
    EXPECT_TRUE(store.Get("name", value));
    EXPECT_EQ(value, "han");
    EXPECT_EQ(store.Size(), 1);
}

TEST(KVStoreTest, DeleteValue)
{
    tinykv::KVStore store;

    store.Set("name", "xiaofeng");
    EXPECT_TRUE(store.Del("name"));
    EXPECT_FALSE(store.Del("name"));
}

TEST(KVStoreTest, TtlForMissingKey)
{
    tinykv::KVStore store;
    EXPECT_EQ(store.Ttl("missing"), -2);
}

TEST(KVStoreTest, TtlForPersistentKey)
{
    tinykv::KVStore store;

    store.Set("name", "xiaofeng");
    EXPECT_EQ(store.Ttl("name"), -1);
}

TEST(KVStoreTest, ExpireForMissingKeyShouldFail)
{
    tinykv::KVStore store;
    EXPECT_FALSE(store.Expire("missing", 10));
}

TEST(KVStoreTest, ExpireWithInvalidSecondsShouldFail)
{
    tinykv::KVStore store;

    store.Set("name", "xiaofeng");
    EXPECT_FALSE(store.Expire("name", 0));
    EXPECT_FALSE(store.Expire("name", -1));

    // 失败的 expire 不应该删除 key。
    std::string value;
    EXPECT_TRUE(store.Get("name", value));
    EXPECT_EQ(value, "xiaofeng");
}

TEST(KVStoreTest, ExpireAndTtlShouldWork)
{
    tinykv::KVStore store;

    store.Set("name", "xiaofeng");
    EXPECT_TRUE(store.Expire("name", 2));


    // 刚设置 2 秒过期，剩余 TTL 应该在 1 到 2 秒之间。
    const int remain = store.Ttl("name");
    EXPECT_GE(remain, 1);
    EXPECT_LE(remain, 2);
}

TEST(KVStoreTest, KeyShouldExpire)
{
    tinykv::KVStore store;

    store.Set("temp", "value");
    EXPECT_TRUE(store.Expire("temp", 1));

    std::this_thread::sleep_for(std::chrono::milliseconds(1200));

    std::string value;
    EXPECT_FALSE(store.Get("temp", value));
    EXPECT_EQ(store.Ttl("temp"), -2);
    EXPECT_EQ(store.Size(), 0);
}

TEST(KVStoreTest, SetShouldClearOldTtl)
{
    tinykv::KVStore store;

    store.Set("name", "xiaofeng");
    EXPECT_TRUE(store.Expire("name", 1));

    store.Set("name", "han");
    EXPECT_EQ(store.Ttl("name"), -1);

    std::this_thread::sleep_for(std::chrono::milliseconds(1200));

    std::string value;
    EXPECT_TRUE(store.Get("name", value));
    EXPECT_EQ(value, "han");
}

TEST(KVStoreTest, SweepExpired)
{
    tinykv::KVStore store;

    store.Set("a", "1");
    store.Set("b", "2");
    store.Set("c", "3");

    EXPECT_TRUE(store.Expire("a", 1));
    EXPECT_TRUE(store.Expire("b", 1));

    std::this_thread::sleep_for(std::chrono::milliseconds(1200));

    const tinykv::KVStore::SweepResult result = store.SweepExpired();
    EXPECT_EQ(result.removed, 2);
    EXPECT_EQ(store.Size(), 1);

    std::string value;
    EXPECT_TRUE(store.Get("c", value));
    EXPECT_EQ(value, "3");
}

TEST(KVStoreTest, Stats)
{
    tinykv::KVStore store;

    store.Set("a", "1");
    store.Set("b", "2");
    store.Set("c", "3");

    EXPECT_TRUE(store.Expire("b", 10));
    EXPECT_TRUE(store.Expire("c", 10));

    const auto stats = store.Stats();
    EXPECT_EQ(stats.keys, 3);
    EXPECT_EQ(stats.persistentKeys, 1);
    EXPECT_EQ(stats.expiringKeys, 2);
}

TEST(KVStoreTest, ExpiredKeyShouldNotBeDeletedAsExistingKey)
{
    tinykv::KVStore store;

    store.Set("temp", "value");
    EXPECT_TRUE(store.Expire("temp", 1));

    std::this_thread::sleep_for(std::chrono::milliseconds(1200));

    // key 已经过期，此时 del 应该返回 false。
    EXPECT_FALSE(store.Del("temp"));
    EXPECT_EQ(store.Size(), 0);
}

} // namespace
