/**
 * KVStore 内存存储模块
 * 
 * 一个线程安全的 key-vlaue 存储模块，支持 SET/GET/DEL/EXPIRE/TTL/STATS 的底层数据操作
 */
#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <optional>
#include <mutex>
#include <unordered_map>

namespace tinykv {
    
class KVStore {
public:
    /**
     * KVStore 统计信息
     */
    struct InnerStats {
        std::size_t keys {0};           // 当前有效 keys 总数
        std::size_t persistentKeys {0}; // 没有设置过期时间的 keys 总数
        std::size_t expiringKeys {0};   // 设置了过期时间的 keys 总数
    };

    KVStore() = default;

    // KVStore 内部有 mutex，不允许拷贝
    KVStore(const KVStore&) = delete;
    KVStore& operator=(const KVStore&) = delete;

    /**
     * 设置 key-value
     * 
     * 如果 key 已存在，会覆盖旧的 value
     * 本项目约定：SET 会清除 key 原来的过期时间
     */
    void Set(const std::string &key, const std::string &value);

    /**
     * 获取 key 对应的 value
     * 
     * 返回 true：key 存在且未过期，value 输出参数会被赋值
     * 返回 false：key 不存在或者已经过期
     */
    bool Get(const std::string& key, std::string& value);

    /**
     * 删除 key
     * 
     * 返回 true：key 存在且被删除
     * 返回 false：key 不存在或者 key 已经过期
     */
    bool Del(const std::string& key);

    /**
     * 设置 key 的过期时间
     * 
     * 返回 true：设置成功
     * 返回 false：key 不存在、key 已过期、seconds 非法
     */
    bool Expire(const std::string& key, int seconds);

    /**
     * 查询 key 的剩余过期时间
     * 
     * 返回值约定：
     *    -2：key 不存在或者 key 已过期
     *    -1：key 存在但没有设置过期时间
     *    >=0：key 剩余过期秒数
     */
    int Ttl(const std::string& key);

    /**
     * 返回当前有效 key 的数量
     * 
     * 调用时会顺便清理已过期 key。
     */
    std::size_t Size();

    /**
     * 返回当前有效 key 的统计信息
     * 
     * 调用时会顺便清理已过期 key。
     * 
     */
    InnerStats Stats();

    /**
     * 主动清理已过期 key
     * 
     * 返回本次清理掉的 key 数量。
     * 后续阶段可以由后台线程周期性调用这个方法。
     */
    std::size_t SweepExpired();

private:
    /**
     * steady_clock 为单调时钟，可以理解为一个“绝对匀速、只增不减的秒表“
     * 只要涉及“测量时间间隔”，请永远首选 steady_clock。
     */
    using Clock = std::chrono::steady_clock;
    
    /**
     * steady_clock 返回的一个时间点对象，相当于你按下秒表“开始”或“结束”那一瞬间的刻度记录。
     * 它本身不包含具体的时间数值，而是记录距离 steady_clock 起点的“滴答数（ticks）”
     */
    using TimePoint = Clock::time_point;

    /**
     * 
     * 
     */
    struct Entry {
        std::string value;
        std::optional<TimePoint> expireAt;
    };

    bool IsExpired(const Entry &entry, TimePoint now) const;
    std::size_t SweepExpiredLocked(TimePoint now);
private:
    std::mutex m_mutex;
    std::unordered_map<std::string, Entry> m_data; 
};

} // namespace tinykv
