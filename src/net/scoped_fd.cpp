#include "tinykv/net/scoped_fd.h"
#include <unistd.h>

namespace tinykv {

ScopedFD::ScopedFD(int fd) noexcept : m_fd(fd) {}

ScopedFd::~ScopedFd()
{
    Reset();
}

ScopedFD::ScopedFD(ScopedFD &&other) noexcept : m_fd(other.Release()) {}

ScopedFd& ScopedFd::operator=(ScopedFd&& other) noexcept
{
    if (this != &other) {
        Reset(other.Release());
    }
    return *this;
}

int ScopedFD::Get() const noexcept
{
    return m_fd;
}

bool ScopedFD::Valid() const noexcept
{
    return m_fd >= 0;
}

ScopedFD::operator bool() const noexcept
{
    return Valid();
}

int ScopedFD::Release() noexcept
{
    const int oldFd = m_fd;
    m_fd = INVALID_FD;
    return oldFd;
}

void ScopedFD::Reset(int newFd) noexcept
{
    // 如果 reset 到同一个 fd，直接不做任何操作。
    if (m_fd == newFd) {
        return;
    }

    if (m_fd >= 0) {
        // close 失败时这里不抛异常。
        // 原因：reset 和析构函数都是 noexcept 语义；析构阶段不应该抛异常。
        // 对本项目来说，关闭 fd 失败不影响后续流程。
        (void)::close(m_fd);
    }
    m_fd = newFd;
}

} // namespace tinykv