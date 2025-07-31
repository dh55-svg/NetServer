#include "EventLoop.h"
#include <iostream>
#include <sys/eventfd.h>
#include <unistd.h>
#include <stdlib.h>
//参照muduo，实现跨线程唤醒
int CreateEventFd() {
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (fd == -1) {
        perror("eventfd");
        exit(EXIT_FAILURE);
    }
    return fd;
}
EventLoop::EventLoop()
    : functors_(),
      channels_(),
      activechannels_(),
      poller(),
      quit_(true),
      tid(std::this_thread::get_id()),
      wakeupfd_(CreateEventFd()),
      wakeupchannel_() {
        wakeupchannel_.SetFd(wakeupfd_);
        wakeupchannel_.SetEvents(EPOLLIN| EPOLLET);
        wakeupchannel_.setReadHandler(std::bind(&EventLoop::HandleRead, this));
        wakeupchannel_.setErrorHandler(std::bind(&EventLoop::HandleError, this));
        AddChannelToPoller(&wakeupchannel_);
      }
EventLoop::~EventLoop() {
    if (wakeupfd_ != -1) {  
        close(wakeupfd_);
    }
  }
  void EventLoop::wakeup() {
    uint64_t one = 1;
    ssize_t n = write(wakeupfd_, &one, sizeof(one));
    if (n != sizeof(one)) {
        perror("write wakeupfd");
    }
  }
  void EventLoop::HandleRead() {
    uint64_t one = 0;
    ssize_t n = read(wakeupfd_, &one, sizeof(one));
    if (n != sizeof(one)) {
        perror("read wakeupfd");
    }
  }
  void EventLoop::HandleError() {
    std::cerr << "EventLoop error occurred." << std::endl;
    // 可以添加更多错误处理逻辑
  }
  void EventLoop::loop() {
    quit_ = false;
    while (!quit_) {
      poller.poll(activechannels_);
      for (auto &channel : activechannels_) {
        channel->HandleEvent();//处理事件
      }
      activechannels_.clear();
      ExecuteTask(); //执行任务队列中的任务
    }
  }