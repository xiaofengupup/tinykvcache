#include "tinykv/net/output_buffer.h"

#include <gtest/gtest.h>
#include <stdexcept>
#include <string>

namespace {

std::string PendingData(const tinykv::OutputBuffer& buffer)
{
    return std::string(buffer.Data(), buffer.Size());
}

TEST(OutputBufferTest, DefaultState)
{
    tinykv::OutputBuffer buffer;

    EXPECT_TRUE(buffer.Empty());
    EXPECT_EQ(buffer.Size(), 0);
}

TEST(OutputBufferTest, Append)
{
    tinykv::OutputBuffer buffer;

    buffer.Append("hello");
    buffer.Append(" world");

    EXPECT_TRUE(!buffer.Empty());
    EXPECT_EQ(buffer.Size(), 11);
    EXPECT_EQ(PendingData(buffer), "hello world");
}

TEST(OutputBufferTest, PartialConsume)
{
    tinykv::OutputBuffer buffer;

    buffer.Append("abcdef");
    buffer.Consume(2);

    EXPECT_EQ(buffer.Size(), 4);
    EXPECT_EQ(PendingData(buffer), "cdef");
}

TEST(OutputBufferTest, AppendAfterPartialConsume)
{
    tinykv::OutputBuffer buffer;
    
    buffer.Append("abcdef");
    buffer.Consume(2);
    buffer.Append("gh");

    EXPECT_EQ(buffer.Size(), 6);
    EXPECT_EQ(PendingData(buffer), "cdefgh");
}

TEST(OutputBufferTest, ConsumeAll)
{
    tinykv::OutputBuffer buffer;

    buffer.Append("abc");
    buffer.Consume(3);

    EXPECT_TRUE(buffer.Empty());
    EXPECT_EQ(buffer.Size(), 0);
}

TEST(OutputBufferTest, Clear)
{
    tinykv::OutputBuffer buffer;

    buffer.Append("hello");
    buffer.Clear();

    EXPECT_TRUE(buffer.Empty());
    EXPECT_EQ(buffer.Size(), 0);
}

TEST(OutputBufferTest, ConsumeTooMuchShouldThrow)
{
    tinykv::OutputBuffer buffer;
    buffer.Append("abc");

    bool thrown = false;
    try {
        buffer.Consume(4);
    } catch (std::out_of_range&) {
        thrown = true;
    }

    EXPECT_TRUE(thrown);
}

TEST(OutputBufferTest, RepeatedAppendAndConsume)
{
    tinykv::OutputBuffer buffer;

    for (int i = 0; i < 1000; ++i) {
        buffer.Append("0123456789");
        buffer.Consume(10);
    }

    EXPECT_TRUE(buffer.Empty());
}

}