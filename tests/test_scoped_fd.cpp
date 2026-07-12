#include "tinykv/net/scoped_fd.h"

#include <cassert>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <type_traits>
#include <utility>

namespace {

struct PipePair {
    tinykv::ScopedFD readFd;
    tinykv::ScopedFD writeFd;
};

PipePair MakePipePair()
{
    int fds[2] = {-1, -1};
    
    // pipe() 函数创建一个管道，并返回两个文件描述符，分别用于读和写。
    assert(::pipe(fds) == 0);

    return PipePair{tinykv::ScopedFD(fds[0]), tinykv::ScopedFD(fds[1])};
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

bool TestTypeTraits()
{
    // ScopedFd 不允许拷贝。
    static_assert(!std::is_copy_constructible<tinykv::ScopedFD>::value, "ScopedFD 不应该允许拷贝构造");
    static_assert(!std::is_copy_assignable<tinykv::ScopedFD>::value, "ScopedFD 不应该允许拷贝赋值");

    // ScopedFd 允许移动。
    static_assert(std::is_move_constructible<tinykv::ScopedFD>::value, "ScopedFD 应该允许移动构造");
    static_assert(std::is_move_assignable<tinykv::ScopedFD>::value, "ScopedFD 应该允许移动赋值");
    static_assert(std::is_nothrow_move_constructible<tinykv::ScopedFD>::value, "ScopedFd 的移动构造应该是 noexcept");
    static_assert(std::is_nothrow_move_assignable<tinykv::ScopedFD>::value, "ScopedFd 的移动赋值应该是 noexcept");
}

void TestDefaultConstructor()
{
    tinykv::ScopedFD fd;

    assert(fd.Get() == tinykv::ScopedFD::INVALID_FD);
    assert(!fd.Valid());
    assert(!fd);
}

void TestConstructorWithFd()
{
    const auto pipe = MakePipePair();

    const int readRawFd = pipe.readFd.Get();
    tinykv::ScopedFD movedFd(std::move(pipe.readFd));

    // 移动后，旧对象应该失去 fd
    assert(!pipe.readFd.Valid());
    assert(pipe.readFd.Get() == tinykv::ScopedFD::INVALID_FD);
    
    // 新对象接管原来的 fd
    assert(movedFd.Valid());
    assert(movedFd.Get() == readRawFd);
    assert(FdIsOpen(movedFd.Get()));

    // 验证移动后的 fd 真的还能正常使用
    const char input = 'A';
    const char output = '\0';

    assert(::write(pipe.writeFd.Get(), &input, 1) == 1);
    assert(::read(movedFd.Get(), &output, 1) == 1);
    assert(output == input);
}

void TestMoveAssignmentTransfersOwnershipAndClosesOldFd()
{
    auto first = MakePipePair();
    auto second = MakePipePair();

    const int oldFirstReadFd = first.readFd.Get();
    const int secondReadFd = second.readFd.Get();

    first.readFd = std::move(second.readFd);

    // first.read_end 原来持有的 fd 应该已经被关闭。
    assert(FdIsClosed(oldFirstReadFd));

    // first.read_end 现在接管 second.read_end 原来的 fd。
    assert(first.readFd.Valid());
    assert(first.readFd.Get() == secondReadFd);

    // second.read_end 移动后应该无效。
    assert(!second.readFd.Valid());
    assert(second.readFd.Get() == tinykv::ScopedFD::INVALID_FD);
}

void TestRelease()
{
    auto pipe = MakePipePair();

    const int readRawFd = pipe.readFd.Release();

    // release 后，ScopedFD 不再管理 fd
    assert(!pipe.readFd.Valid());
    assert(pipe.readFd.Get() == tinykv::ScopedFD::INVALID_FD);

    // 但 fd 本身没有关闭
    assert(FdIsOpen(readRawFd));

    // release 返回的 fd 需要调用者自己关闭。
    assert(::close(readRawFd) == 0);
    assert(FdIsClosed(readRawFd));
}

void TestResetWithoutNewFdClosesCurrentFd()
{
    auto pipe = MakePipePair();

    const int readRawFd = pipe.readFd.Get();
    pipe.readFd.Reset();

    // reset 后，ScopedFD 不再管理 fd
    assert(!pipe.readFd.Valid());
    assert(pipe.readFd.Get() == tinykv::ScopedFD::INVALID_FD);

    // 但 fd 本身已经关闭
    assert(FdIsClosed(readRawFd));
}

void TestResetToNewFd()
{
    auto firstPipe = MakePipePair();
    auto secondPipe = MakePipePair();

    const int oldFirstReadRawFd = firstPipe.readFd.Get();
    
    // release 后，second.read_end 不再负责关闭这个 fd
    // 这个 fd 后续会交给 first.read_end 管理
    const int newReadFd = secondPipe.readFd.Release();

    firstPipe.readFd.Reset(newReadFd);

    // first.read_end 原来的 fd 应该被关闭
    assert(FdIsClosed(oldFirstReadRawFd));

    // first.read_end 应该接管新的 fd
    assert(firstPipe.readFd.Valid());
    assert(firstPipe.readFd.Get() == newReadFd);
    assert(FdIsOpen(firstPipe.readFd.Get()));
}

void TestResetSameFdShouldNoLoop()
{
    auto pipe = MakePipePair();

    const int readRawFd = pipe.readFd.Get();

    // reset 为同一个 fd 时，做 no-op，避免把自己关闭后又继续持有一个已关闭 fd。
    pipe.readFd.Reset(readRawFd);

    // reset 后，ScopedFD 仍然管理同一个 fd
    assert(pipe.readFd.Valid());
    assert(pipe.readFd.Get() == readRawFd);
    assert(FdIsOpen(readRawFd));
}

int main()
{
    TestTypeTraits();
    TestDefaultConstructor();
    TestConstructorWithFd();
    TestMoveAssignmentTransfersOwnershipAndClosesOldFd();
    TestRelease();
    TestResetWithoutNewFdClosesCurrentFd();
    TestResetToNewFd();
    TestResetSameFdShouldNoLoop();

    return 0;
}

} // end namspace