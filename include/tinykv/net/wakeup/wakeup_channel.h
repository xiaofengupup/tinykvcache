/**
 * 事件唤醒
 */
#pragma once

#include "tinykv/net/scoped_fd.h"

namespace tinykv {

/**
 * 用于跨线程或信号处理器唤醒事件循环
 * 
 * m_readFd: 注册到 Poller。
 * m_writeFd: 向 m_writeFd 写入一个字节后，m_readFd 会变为可读。
 */
class WakeupChannel {
public:
    WakeupChannel();

    WakeupChannel(const WakeupChannel&) = delete;
    WakeupChannel& operator=(const WakeupChannel&) = delete;
    WakeupChannel(WakeupChannel&&) = delete;
    WakeupChannel& operator=(WakeupChannel&&) = delete;

    int ReadFd() const noexcept;
    int WriteFd() const noexcept;
    
    /**
     * 写入唤醒字节
     * 
     * 该方法不抛异常，可被普通线程中的 Stop() 调用
     */
    void Notify() noexcept;

    /**
     * 清空所有待处理的唤醒字节
     * 
     * 只能在普通事件循环上下文中调用，不应在信号处理函数中调用
     */
    void Drain();

private:
    ScopedFd m_readFd;
    ScopedFd m_writeFd;
};
    
} // namespace tinykv
