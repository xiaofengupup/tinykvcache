#include "tinykv/core/kv_store.h"

#include <chrono>
#include <cassert>

namespace tinykv {

static constexpr std::size_t MIN_STALE_RECORDS_FOR_REBUILD = 1024;
static constexpr std::size_t MIN_HEAP_RECORDS_FOR_REBUILD = 4096;

bool KVStore::IsExpired(const Entry& entry, TimePoint now) const
{
    if (!entry.expireAt.has_value()) {
        return false;
    }

    return now >= entry.expireAt.value();
}

void KVStore::EraseEntryLocked(DataIterator it)
{
    if (it->second.expireAt.has_value()) {
        assert(m_expiringKeys > 0);
        --m_expiringKeys;
    } else {
        assert(m_persistentKeys > 0);
        --m_persistentKeys;
    }

    m_data.erase(it);
}

KVStore::SweepResult KVStore::SweepExpiredLocked(TimePoint now, std::size_t maxRecordsToProcess)
{
    KVStore::SweepResult result;

    while (!m_expirations.empty() && result.processed < maxRecordsToProcess) {
        const ExpirationRecord record = m_expirations.top();
        if (record.expireAt > now) {
            break;
        }

        m_expirations.pop();
        ++result.processed;

        auto it = m_data.find(record.key);
        if (it == m_data.end()) {
            continue;
        }

        Entry& entry = it->second;
        if (entry.generation != record.generation) {
            continue;
        }
        if (!entry.expireAt.has_value()) {
            continue;
        }
        if (entry.expireAt.value() != record.expireAt) {
            continue;
        }

        EraseEntryLocked(it);
        ++result.removed;
    }

    result.hasMoreExpired = (!m_expirations.empty()) && (m_expirations.top().expireAt <= now);
    return result;
}

void KVStore::Set(const std::string& key, const std::string& value)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_data.find(key);
    if (it == m_data.end()) {
        Entry entry;
        entry.value = value;
        m_data.emplace(key, std::move(entry));

        ++m_persistentKeys;
        return;
    }

    Entry& entry = it->second;
    if (entry.expireAt.has_value()) {
        --m_expiringKeys;
        ++m_persistentKeys;
    }

    entry.value = value;
    entry.expireAt.reset();
    ++entry.generation;
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
        EraseEntryLocked(it);
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
        EraseEntryLocked(it);
        return false;
    }

    EraseEntryLocked(it);
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
        EraseEntryLocked(it);
        return false;
    }

    if (!it->second.expireAt.has_value()) {
        assert(m_persistentKeys > 0);
        --m_persistentKeys;
        ++m_expiringKeys;
    }

    it->second.expireAt = now + std::chrono::seconds(seconds);
    it->second.generation = ++m_nextExpirationGeneration;
    m_expirations.push(ExpirationRecord{
        it->second.expireAt.value(),
        key,
        it->second.generation
    });

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
        EraseEntryLocked(it);
        return -2;
    }

    if (!it->second.expireAt.has_value()) {
        return -1;
    }

    const auto remaining = it->second.expireAt.value() - now;
    return static_cast<int>(std::chrono::ceil<std::chrono::seconds>(remaining).count());
}

std::size_t KVStore::Size()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_persistentKeys + m_expiringKeys;
}

KVStore::InnerStats KVStore::Stats()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    InnerStats result;
    result.keys = m_persistentKeys + m_expiringKeys;
    result.expiringKeys = m_expiringKeys;
    result.persistentKeys = m_persistentKeys;

    return result;
}

KVStore::SweepResult KVStore::SweepExpired(std::size_t maxRecordsToProcess)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    const auto now = Clock::now();
    SweepResult result = SweepExpiredLocked(now, maxRecordsToProcess);

    if (ShouldRebuildExpirationHeapLocked()) {
        RebuildExpirationHeapLocked();
        result.heapRebuilt = true;
        result.hasMoreExpired = !m_expirations.empty() && m_expirations.top().expireAt <= now;
    }

    return result;
}

bool KVStore::ShouldRebuildExpirationHeapLocked() const noexcept
{
    const std::size_t heapSize = m_expirations.size();
    if (heapSize == 0) {
        return false;
    }
    
    if (m_expiringKeys == 0) {
        return true;
    }

    if (heapSize < MIN_HEAP_RECORDS_FOR_REBUILD) {
        return false;
    }

    assert(heapSize >= m_expiringKeys);

    const std::size_t staleRecords = heapSize - m_expiringKeys;
    return staleRecords >= MIN_STALE_RECORDS_FOR_REBUILD &&
           staleRecords >= m_expiringKeys;
}

void KVStore::RebuildExpirationHeapLocked()
{
    if (m_expiringKeys == 0) {
        ExpirationQueue emptyHeap;
        m_expirations.swap(emptyHeap);
        return;
    }

    std::vector<ExpirationRecord> records;
    records.reserve(m_expiringKeys);
    for (const auto& [key, entry] : m_data) {
        if (!entry.expireAt.has_value()) {
            continue;
        }

        records.push_back({
            entry.expireAt.value(),
            key,
            entry.generation
        });
    }

    assert(records.size() == m_expiringKeys);

    ExpirationQueue rebuilt(ExpirationCompare{}, std::move(records));
    m_expirations.swap(rebuilt);
}

} // namespace tinykv
