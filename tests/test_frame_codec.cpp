#include "tinykv/core/frame_codec.h"

#include <gtest/gtest.h>
#include <arpa/inet.h>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <string>
#include <cstring>

namespace {

TEST(FrameCodecTest, EncodeHeaderAndPayload)
{
    const std::string payload = "PING";
    const std::string frame = tinykv::FrameCodec::Encode(payload);

    // frame 总长度是 4 字节长度头 + payload 长度
    EXPECT_EQ(frame.size(), 4 + payload.size());

    // 检查前 4 字节是否正确表示 payload 的长度
    std::uint32_t netLen = 0;
    std::memcpy(&netLen, frame.data(), sizeof(netLen));

    const std::uint32_t hostLen = ntohl(netLen);
    EXPECT_EQ(hostLen, payload.size());

    // 检查后面内容是否仍然是原始 payload
    EXPECT_EQ(frame.substr(4), payload);
}

TEST(FrameCodecTest, DecodeSingleCompleteFrame)
{
    std::string buffer;
    const std::string frame = tinykv::FrameCodec::Encode("PING");
    const auto frames = tinykv::FrameCodec::Decode(buffer, frame.data(), frame.size());

    EXPECT_EQ(frames.size(), 1);
    EXPECT_EQ(frames[0], "PING");
    EXPECT_TRUE(buffer.empty());
}

TEST(FrameCodecTest, DecodeStickyPackets)
{
    std::string buffer;
    const std::string frame1 = tinykv::FrameCodec::Encode("PING");
    const std::string frame2 = tinykv::FrameCodec::Encode("GET name");

    // 模拟粘包：两条完整消息一次性到达。
    const std::string joined = frame1 + frame2;
    const auto frames = tinykv::FrameCodec::Decode(buffer, joined.data(), joined.size());

    EXPECT_EQ(frames.size(), 2);
    EXPECT_EQ(frames[0], "PING");
    EXPECT_EQ(frames[1], "GET name");
    EXPECT_TRUE(buffer.empty());
}

TEST(FrameCodecTest, DecodeHalfPacket)
{
    std::string buffer;
    const std::string frame = tinykv::FrameCodec::Encode("SET name xiaofeng");

    // 模拟半包：第一次只收到前 5 个字节
    // 前 4 字节是长度头，第 5 个字节是 payload 的一部分。
    const std::string part1 = frame.substr(0, 5);
    const std::string part2 = frame.substr(5);

    auto frames = tinykv::FrameCodec::Decode(buffer, part1.data(), part1.size());
    // 数据还不完整，不能解析出frame
    EXPECT_TRUE(frames.empty());
    EXPECT_FALSE(buffer.empty());

    frames = tinykv::FrameCodec::Decode(buffer, part2.data(), part2.size());
    EXPECT_EQ(frames.size(), 1);
    EXPECT_EQ(frames[0], "SET name xiaofeng");
    EXPECT_TRUE(buffer.empty());
}

TEST(FrameCodecTest, DecodeByteByByte)
{
    std::string buffer;
    const std::string frame = tinykv::FrameCodec::Encode("SET a 1");

    // 模拟极端半包：每次只收到 1 个字节。
    std::vector<std::string> collected;
    for (char ch : frame) {
        auto frames = tinykv::FrameCodec::Decode(buffer, &ch, 1);
        collected.insert(collected.end(), frames.begin(), frames.end());
    }

    EXPECT_EQ(collected.size(), 1);
    EXPECT_EQ(collected[0], "SET a 1");
    EXPECT_TRUE(buffer.empty());
}

TEST(FrameCodec, DecodeCompleteFrameWithPartialTail)
{
    std::string buffer;

    const std::string frame1 = tinykv::FrameCodec::Encode("PING");
    const std::string frame2 = tinykv::FrameCodec::Encode("GET a");
    const std::string frame3 = tinykv::FrameCodec::Encode("SET b 2");

    // 模拟一次收到两条完整消息，以及第三条消息的一部分。
    const std::string mixed = frame1 + frame2 + frame3.substr(0, 5);

    auto frames = tinykv::FrameCodec::Decode(buffer, mixed.data(), mixed.size());
    EXPECT_EQ(frames.size(), 2);
    EXPECT_EQ(frames[0], "PING");
    EXPECT_EQ(frames[1], "GET a");
    EXPECT_EQ(buffer.size(), 5); // 第三条消息还没收完整，所以应该留在 buffer 里。

    frames = tinykv::FrameCodec::Decode(buffer, frame3.data() + 5, frame3.size() - 5);
    EXPECT_EQ(frames.size(), 1);
    EXPECT_EQ(frames[0], "SET b 2");
    EXPECT_TRUE(buffer.empty());
}

TEST(FrameCodecTest, DecodeEmptyPayload)
{
    std::string buffer;
    
    const std::string frame = tinykv::FrameCodec::Encode("");
    const auto frames = tinykv::FrameCodec::Decode(buffer, frame.data(), frame.size());

    // 协议层允许空 payload
    // 至于空 payload 是否是合法命令，由后续 CommandParser 决定。
    EXPECT_EQ(frames.size(), 1);
    EXPECT_EQ(frames[0], "");
    EXPECT_TRUE(buffer.empty());
}

TEST(FrameCodecTest, EncodeTooLargePayloadShouldThrow)
{
    const std::string hugePayload(tinykv::FrameCodec::MAX_FRAME_SIZE + 1, 'x');

    bool thrown = false;
    try {
        (void)tinykv::FrameCodec::Encode(hugePayload);
    } catch (const std::runtime_error&) {
        thrown = true;
    }

    EXPECT_TRUE(thrown);
}

TEST(FrameCodecTest, DecodeTooLargePayloadShouldThrow)
{
    std::string buffer;

    // 构造一个异常 frame：长度字段超过最大限制，但不需要真的构造超大 body。
    std::uint32_t tooLarge = htonl(tinykv::FrameCodec::MAX_FRAME_SIZE + 1);

    std::string badFrame;
    badFrame.resize(sizeof(tooLarge));
    std::memcpy(&badFrame[0], &tooLarge, sizeof(tooLarge));

    bool thrown = false;
    try {
        (void)tinykv::FrameCodec::Decode(buffer, badFrame.data(), badFrame.size());
    } catch (const std::runtime_error&) {
        thrown = true;
    }

    EXPECT_TRUE(thrown);
}

} // namespace