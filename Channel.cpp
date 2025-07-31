#include "Channel.h"
#include <iostream>
#include <sys/epoll.h>
Channel::Channel() : fd_(-1) {}
Channel::~Channel() {}
void Channel::HandleEvent() {
    //读事件，对端有数据或者正常关闭
    if (events_ & (EPOLLIN | EPOLLPRI)) {
        if (readhandler_) {
            readhandler_();
        }
    }
    //写事件
    if (events_ & EPOLLOUT) {
        if (writehandler_) {
            writehandler_();
        }
    }
    if (events_ & EPOLLERR) {
        if (errorhandler_) {
            errorhandler_();
        }
    }
    //对方异常关闭事件，或者半关闭事件
    if (events_ & EPOLLHUP) {
        if (closehandler_) {
            closehandler_();
        }
    }
}