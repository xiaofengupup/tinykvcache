#include "tinykv/core/kv_store.h"

#include <chrono>

namespace tinykv {

bool KVStore::IsExpired(const Entry& entry, TimePoint now) const
{
    if (!entry.expireAt.has_value()) {
        return false;
    }

    return now >= entry.expireAt.value();
}

std::size_t KVStore::SweepExpiredLocked(TimePoint now)
{
    std::size_t removed = 0;

    for (auto it = m_data.begin(); it != m_data.end();) {
        if (IsExpired(it->second, now)) {
            it = m_data.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }

    return removed;
}

void KVStore::Set(const std::string& key, const std::string& value)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    Entry entry;
    entry.value = value;

    m_data[key] = std::move(entry);
}

bool KVStore::Get(const std::string &key, std::string &value)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto now = Clock::now();

    auto it = m_data.find(key);
    if (it == m_data.end()) {
        return false;
    }

    if (IsExpired(it->second, now)) {
        m_data.erase(it);
        return false;
    }

    value = it->second.value;
    return true;
}

bool KVStore::Del(const std::string &key)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto now = Clock::now();
    
    auto it = m_data.find(key);
    if (it == m_data.end()) {
        return false;
    }

    if (IsExpired(it->second, now)) {
        m_data.erase(it);
        return false;
    }

    m_data.erase(it);
    return true;
}

bool KVStore::Expire(const std::string& key, int seconds)
{
    if (seconds <= 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    const auto now = Clock::now();

    auto it = m_data.find(key);
    if (it == m_data.end()) {
        return false;
    }

    if (IsExpired(it->second, now)) {
        m_data.erase(it);
        return false;
    }

    it->second.expireAt = now + std::chrono::seconds(seconds);
    return true;
}

int KVStore::Ttl(const std::string& key)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto now = Clock::now();

    auto it = m_data.find(key);
    if (it == m_data.end()) {
        return -2;
    }
    if (IsExpired(it->second, now)) {
        m_data.erase(it);
        return -2;
    }

    if (!it->second.expireAt.has_value()) {
        return -1;
    }

    const auto remaining = it->second.expireAt.value() - now;
    const auto remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();
    if (remainingMs <= 0) {
        m_data.erase(it);
        return -2;
    }

    // 向上取整到秒。
    // 例如还剩 1500ms，返回 2；
    // 还剩 1ms，也返回 1。
    return static_cast<int>((remainingMs + 999) / 1000);
}

std::size_t KVStore::Size()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto now = Clock::now();

    SweepExpiredLocked(now);
    
    return m_data.size();
}

KVStore::InnerStats KVStore::Stats()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto now = Clock::now();

    SweepExpiredLocked(now);

    InnerStats result;
    result.keys = m_data.size();

    for (const auto &item : m_data) {
        const Entry& entry = item.second;

        if (entry.expireAt.has_value()) {
            ++result.expiringKeys;
        } else {
            ++result.persistentKeys;
        }
    }

    return result;
}

std::size_t KVStore::SweepExpired()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto now = Clock::now();

    return SweepExpiredLocked(now);
}

} // namespace tinykv
