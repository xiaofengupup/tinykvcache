#include "test_utils.h"
#include "tinykv/net/output_buffer.h"

#include <stdexcept>
#include <string>

namespace {

std::string PendingData(const tinykv::OutputBuffer& buffer)
{
    return std::string(buffer.Data(), buffer.Size());
}

void TestDefaultState()
{
    tinykv::OutputBuffer buffer;

    TINYKV_CHECK(buffer.Empty());
    TINYKV_CHECK(buffer.Size() == 0);
}

void TestAppend()
{
    tinykv::OutputBuffer buffer;

    buffer.Append("hello");
    buffer.Append(" world");

    TINYKV_CHECK(!buffer.Empty());
    TINYKV_CHECK(buffer.Size() == 11);
    TINYKV_CHECK(PendingData(buffer) == "hello world");
}

void TestPartialConsume()
{
    tinykv::OutputBuffer buffer;

    buffer.Append("abcdef");
    buffer.Consume(2);

    TINYKV_CHECK(buffer.Size() == 4);
    TINYKV_CHECK(PendingData(buffer) == "cdef");
}

void TestAppendAfterPartialConsume()
{
    tinykv::OutputBuffer buffer;
    
    buffer.Append("abcdef");
    buffer.Consume(2);
    buffer.Append("gh");

    TINYKV_CHECK(buffer.Size() == 6);
    TINYKV_CHECK(PendingData(buffer) == "cdefgh");
}

void TestConsumeAll()
{
    tinykv::OutputBuffer buffer;

    buffer.Append("abc");
    buffer.Consume(3);

    TINYKV_CHECK(buffer.Empty());
    TINYKV_CHECK(buffer.Size() == 0);
}

void TestClear()
{
    tinykv::OutputBuffer buffer;

    buffer.Append("hello");
    buffer.Clear();

    TINYKV_CHECK(buffer.Empty());
    TINYKV_CHECK(buffer.Size() == 0);
}

void TestConsumeTooMuchShouldThrow()
{
    tinykv::OutputBuffer buffer;
    buffer.Append("abc");

    bool thrown = false;
    try {
        buffer.Consume(4);
    } catch (std::out_of_range&) {
        thrown = true;
    }

    TINYKV_CHECK(thrown);
}

void TestRepeatedAppendAndConsume()
{
    tinykv::OutputBuffer buffer;

    for (int i = 0; i < 1000; ++i) {
        buffer.Append("0123456789");
        buffer.Consume(10);
    }

    TINYKV_CHECK(buffer.Empty());
}

}

int main()
{
    TestDefaultState();
    TestAppend();
    TestPartialConsume();
    TestAppendAfterPartialConsume();
    TestConsumeAll();
    TestClear();
    TestConsumeTooMuchShouldThrow();
    TestRepeatedAppendAndConsume();

    std::cout << "output buffer tests passed\n";
    return 0;
}