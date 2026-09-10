/**
 * 输出缓冲区管理
 * 
 * 问题场景
 *   原来 writeBuffer.earse(0, static_cast<std::size_t>(n)); 每次发送都会移动剩余内存；
 *   如果缓冲区有 1 MiB，发送了 4 KiB，erase(0, 4096) 会把后面接近 1 MiB 的数据整体向前移动。
 *   连续部分写时会产生很多内存拷贝，影响性能。
 * 
 * 解决方案：
 *   字符串本身不立即移动；
 *   只增加 readPosition；
 *   达到一定条件后再一次性 compact
 */
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace tinykv {

class OutputBuffer {

public:
    OutputBuffer() = default;

    // 禁止拷贝
    OutputBuffer(const OutputBuffer&) = delete;
    OutputBuffer& operator=(const OutputBuffer&) = delete;

    // 允许移动
    OutputBuffer(OutputBuffer&&) noexcept = default;
    OutputBuffer& operator=(OutputBuffer&&) noexcept = default;

    /**
     * 在缓冲区尾部追加数据
     */
    void Append(std::string_view data);

    /**
     * 返回当前尚未发送数据的起始地址
     */
    const char* Data() const noexcept;

    /**
     * 返回当前尚未发送的字节数
     */
    std::size_t Size() const noexcept;

    /**
     * 标记前 bytes 个字节已经发送
     * 
     * 如果 bytes 大于当前剩余数据量，会抛出异常
     * 因为这通常表示发送偏移计算存在 bug。
     */
    void Consume(std::size_t bytes);

    bool Empty() const noexcept;
    void Clear() noexcept;

private:
    void CompactIfNeed();

private:
    // 已消费前缀达到该值后，才考虑执行内存整理。
    static constexpr std::size_t COMPACT_THRESHOLD = 64U * 1024U;
    std::string m_storage;
    std::size_t m_readPos {0};
};

} // namespace tinykv