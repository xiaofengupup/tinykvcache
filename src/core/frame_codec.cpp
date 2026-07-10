#include "tinykv/core/frame_codec.h"

#include <cstdint>
#include <stdexcept>
#include <arpa/inet.h>

namespace tinykv {

/*
 * 匿名命名空间：具有唯一性和内部链接性。
 * 在匿名命名空间中定义的所有变量、函数或常量，只能在当前编译单元（即当前的 .cpp 文件）中被访问。
 * 
 * 它相当于 C 语言中在全局变量前加 static 关键字，但它比 static 更现代、更通用（因为 static 不能修饰类定义，而匿名命名空间可以）。 
 */
namespace {
    constexpr std::size_t HEADER_SIZE = sizeof(std::uint32_t);
}

/*
 * 把一条业务的 payload 编码成完整 frame
 * frame 格式定义如下：
 *   前 4 个字节：payload 长度，使用网络字节序，也就是大端序
 *   后 N 个字节：payload 原始内容
 */
std::string FrameCodec::Encode(const std::string &payload)
{
    // payload 不能超过最大帧长度
    const auto payloadLen = static_cast<uint32_t>(payload.size());
    if (payloadLen > MAX_FRAME_SIZE) {
        throw std::runtime_error("The payload is too large");
    }

    // frame 前 4 字节为 payload 长度，网络字节序
    std::string frame;
    frame.resize(HEADER_SIZE + payloadLen);

    // 写入 4 字节长度头。
    const uint32_t netLen = htonl(payloadLen); // host to network long
    std::memcpy(&frame[0], &netLen, HEADER_SIZE);

    // 写入 payload，payload 允许为空，所以这里需要判断 size。
    if (!payload.empty()) {
        std::memcpy(&frame[HEADER_SIZE], payload.data(), payload.size());
    }

    return frame;
}

/*
 * 把新收到的数据添加到 buffer，然后尽可能解析出完整的 payload
 * 注意：
 *   buffer 必须是连接级别的缓冲区
 *   因为 TCP 是字节流，一次 recv 可能只收到半条消息，也可能收到多条消息
 */
std::vector<std::string> FrameCodec::Decode(std::string &buffer, const char *data, std::size_t size)
{
    if (size > 0 && data == nullptr) {
        throw std::runtime_error("decode input data is null");
    }

    // 追加到连接级缓冲区中，因为 buffer 还可能保存着上次的内容，所以不能直接覆盖，需要追加
    if (size > 0) {
        buffer.append(data, size);
    }

    return Decode(buffer);
}

/*
 * 从已有 buffer 中尽可能解析出完整的 payload
 * 这个重载主要方便测试，也方便后续网络层在 read_buffer 已经累积数据后直接解析。
 */
std::vector<std::string> FrameCodec::Decode(std::string &buffer)
{
    std::vector<std::string> frames;

    while (buffer.size() >= HEADER_SIZE) {
        uint32_t netLen = 0;

        // 读取前 4 字节长度字段
        std::memcpy(&netLen, buffer.data(), HEADER_SIZE);
        // 转换为主机字节序，获取 payload 长度
        uint32_t payloadLen = ntohl(netLen);
        if (payloadLen > MAX_FRAME_SIZE) {
            throw std::runtime_error("frame too large");
        }

        const size_t totalSize = HEADER_SIZE + static_cast<size_t>(payloadLen);
        if (buffer.size() < totalSize) {
            // 当前缓冲区的大小 小于 总体大小，说明发生了半包，等待下一次读取
            break;
        }

        // 取出完整 payload
        frames.push_back(buffer.substr(HEADER_SIZE, payloadLen));

        // 删除已经消费的完整 frame
        buffer.erase(0, totalSize);
    }

    return frames;
}

}

