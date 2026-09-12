#include "tinykv/net/scoped_fd.h"

#include <gtest/gtest.h>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <type_traits>
#include <utility>

namespace {

struct PipePair {
    tinykv::ScopedFd readFd;
    tinykv::ScopedFd writeFd;
};

PipePair MakePipePair()
{
    int fds[2] = {-1, -1};
    
    // pipe() 函数创建一个管道，并返回两个文件描述符，分别用于读和写。
    EXPECT_EQ(::pipe(fds), 0);

    return PipePair{tinykv::ScopedFd(fds[0]), tinykv::ScopedFd(fds[1])};
}

bool FdIsOpen(int fd)
{
    if (fd < 0) {
        return false;
    }

    // fcntl() 函数用于操作文件描述符，这里使用 F_GETFD 命令来获取文件描述符的标志。
    //如果返回值不为 -1，说明文件描述符是有效的，即文件描述符是打开的。
    errno = 0;
    return ::fcntl(fd, F_GETFD) != -1;
}

bool FdIsClosed(int fd)
{
    if (fd < 0) {
        return true;
    }

    errno = 0;
    const int ret = ::fcntl(fd, F_GETFD);
    return ret == -1 && errno == EBADF; // EBADF 表示文件描述符无效
}

TEST(ScopedFdTest, TypeTraits)
{
    // ScopedFd 不允许拷贝。
    static_assert(!std::is_copy_constructible<tinykv::ScopedFd>::value, "ScopedFd 不应该允许拷贝构造");
    static_assert(!std::is_copy_assignable<tinykv::ScopedFd>::value, "ScopedFd 不应该允许拷贝赋值");

    // ScopedFd 允许移动。
    static_assert(std::is_move_constructible<tinykv::ScopedFd>::value, "ScopedFd 应该允许移动构造");
    static_assert(std::is_move_assignable<tinykv::ScopedFd>::value, "ScopedFd 应该允许移动赋值");
    static_assert(std::is_nothrow_move_constructible<tinykv::ScopedFd>::value, "ScopedFd 的移动构造应该是 noexcept");
    static_assert(std::is_nothrow_move_assignable<tinykv::ScopedFd>::value, "ScopedFd 的移动赋值应该是 noexcept");
}

TEST(ScopedFdTest, DefaultConstructor)
{
    tinykv::ScopedFd fd;

    EXPECT_EQ(fd.Get(), tinykv::ScopedFd::INVALID_FD);
    EXPECT_FALSE(fd.Valid());
    EXPECT_FALSE(fd);
}

TEST(ScopedFdTest, ConstructorWithFd)
{
    auto pipe = MakePipePair();

    const int readRawFd = pipe.readFd.Get();
    tinykv::ScopedFd movedFd(std::move(pipe.readFd));

    // 移动后，旧对象应该失去 fd
    EXPECT_FALSE(pipe.readFd.Valid());
    EXPECT_EQ(pipe.readFd.Get(), tinykv::ScopedFd::INVALID_FD);
    
    // 新对象接管原来的 fd
    EXPECT_TRUE(movedFd.Valid());
    EXPECT_EQ(movedFd.Get(), readRawFd);
    EXPECT_TRUE(FdIsOpen(movedFd.Get()));

    // 验证移动后的 fd 真的还能正常使用
    const char input = 'A';
    char output = '\0';

    EXPECT_EQ(::write(pipe.writeFd.Get(), &input, 1), 1);
    EXPECT_EQ(::read(movedFd.Get(), &output, 1), 1);
    EXPECT_EQ(output, input);
}

TEST(ScopedFdTest, MoveAssignmentTransfersOwnershipAndClosesOldFd)
{
    auto first = MakePipePair();
    auto second = MakePipePair();

    const int oldFirstReadFd = first.readFd.Get();
    const int secondReadFd = second.readFd.Get();

    first.readFd = std::move(second.readFd);

    // first.read_end 原来持有的 fd 应该已经被关闭。
    EXPECT_TRUE(FdIsClosed(oldFirstReadFd));

    // first.read_end 现在接管 second.read_end 原来的 fd。
    EXPECT_TRUE(first.readFd.Valid());
    EXPECT_EQ(first.readFd.Get(), secondReadFd);

    // second.read_end 移动后应该无效。
    EXPECT_FALSE(second.readFd.Valid());
    EXPECT_EQ(second.readFd.Get(), tinykv::ScopedFd::INVALID_FD);
}

TEST(ScopedFdTest, Release)
{
    auto pipe = MakePipePair();

    const int readRawFd = pipe.readFd.Release();

    // release 后，ScopedFd 不再管理 fd
    EXPECT_FALSE(pipe.readFd.Valid());
    EXPECT_EQ(pipe.readFd.Get(), tinykv::ScopedFd::INVALID_FD);

    // 但 fd 本身没有关闭
    EXPECT_TRUE(FdIsOpen(readRawFd));

    // release 返回的 fd 需要调用者自己关闭。
    EXPECT_EQ(::close(readRawFd), 0);
    EXPECT_TRUE(FdIsClosed(readRawFd));
}

TEST(ScopedFdTest, ResetWithoutNewFdClosesCurrentFd)
{
    auto pipe = MakePipePair();

    const int readRawFd = pipe.readFd.Get();
    pipe.readFd.Reset();

    // reset 后，ScopedFd 不再管理 fd
    EXPECT_FALSE(pipe.readFd.Valid());
    EXPECT_EQ(pipe.readFd.Get(), tinykv::ScopedFd::INVALID_FD);

    // 但 fd 本身已经关闭
    EXPECT_TRUE(FdIsClosed(readRawFd));
}

TEST(ScopedFdTest, ResetToNewFdClosesCurrentFd)
{
    auto firstPipe = MakePipePair();
    auto secondPipe = MakePipePair();

    const int oldFirstReadRawFd = firstPipe.readFd.Get();
    
    // release 后，second.read_end 不再负责关闭这个 fd
    // 这个 fd 后续会交给 first.read_end 管理
    const int newReadFd = secondPipe.readFd.Release();

    firstPipe.readFd.Reset(newReadFd);

    // first.read_end 原来的 fd 应该被关闭
    EXPECT_TRUE(FdIsClosed(oldFirstReadRawFd));

    // first.read_end 应该接管新的 fd
    EXPECT_TRUE(firstPipe.readFd.Valid());
    EXPECT_EQ(firstPipe.readFd.Get(), newReadFd);
    EXPECT_TRUE(FdIsOpen(firstPipe.readFd.Get()));
}

TEST(ScopedFdTest, ResetSameFdShouldNoLoop)
{
    auto pipe = MakePipePair();

    const int readRawFd = pipe.readFd.Get();

    // reset 为同一个 fd 时，做 no-op，避免把自己关闭后又继续持有一个已关闭 fd。
    pipe.readFd.Reset(readRawFd);

    // reset 后，ScopedFd 仍然管理同一个 fd
    EXPECT_TRUE(pipe.readFd.Valid());
    EXPECT_EQ(pipe.readFd.Get(), readRawFd);
    EXPECT_TRUE(FdIsOpen(readRawFd));
}

} // end namspace
