/**
 * 应用层协议编解码器
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tinykv {

class FrameCodec {
public:
    // 单个 payload 最大允许 1M，防止异常客户端声明一个特别大的长度，导致服务端内存压力过大
    static constexpr std::uint32_t MAX_FRAME_SIZE = 1024 * 1024;

    /*
     * 把一条业务的 payload 编码成完整 frame
     * frame 格式定义如下：
     *   前 4 个字节：payload 长度，使用网络字节序，也就是大端序
     *   后 N 个字节：payload 原始内容
     */
    static std::string Encode(const std::string &payload);

    /*
     * 把新收到的数据添加到 buffer，然后尽可能解析出完整的 payload
     * 注意：
     *   buffer 必须是连接级别的缓冲区
     *   因为 TCP 是字节流，一次 recv 可能只收到半条消息，也可能收到多条消息
     */
    static std::vector<std::string> Decode(std::string &buffer, const char *data, std::size_t size);

    /*
     * 从已有 buffer 中尽可能解析出完整的 payload
     * 这个重载主要方便测试，也方便后续网络层在 read_buffer 已经累积数据后直接解析。
     */
    static std::vector<std::string> Decode(std::string &buffer);
};
    
} // namespace tinykv
