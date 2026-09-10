#include "tinykv/net/output_buffer.h"

#include <stdexcept>

namespace tinykv {
    
void OutputBuffer::Append(std::string_view data)
{
    if (data.empty()) {
        return;
    }

    // 追加前尝试整理已经消费的最大前缀，避免 m_storage 长期保留大量无效空间。
    CompactIfNeed();

    m_storage.append(data.data(), data.size());
}

const char* OutputBuffer::Data() const noexcept
{
    return m_storage.data() + m_readPos;
}

std::size_t OutputBuffer::Size() const noexcept
{
    return m_storage.size() - m_readPos;
}

bool OutputBuffer::Empty() const noexcept
{
    return Size() == 0;
}

void OutputBuffer::Consume(std::size_t bytes)
{
    if (bytes > Size()) {
        throw std::out_of_range("OutputBuffer::Consume exceeds pending bytes");
    }

    m_readPos += bytes;
    if (m_readPos == m_storage.size()) {
        Clear();
        return;
    }

    CompactIfNeed();
}

void OutputBuffer::Clear() noexcept
{
    m_storage.clear();
    m_readPos = 0;
}

void OutputBuffer::CompactIfNeed()
{
    if (m_readPos < COMPACT_THRESHOLD) {
        return;
    }

    // 已消费部分至少占整个 storage_ 的一半时才整理，这样避免频繁 erase。
    if (m_readPos < m_storage.size() / 2U) {
        return;
    }

    m_storage.erase(0, m_readPos);
    m_readPos = 0;
}

} // namespace tinykv
