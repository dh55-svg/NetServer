#include "Poller.h"
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#define MAXEVENTS 4096 //最大触发事件数量
#define TIMEOUT 1000  //epoll_wait 超时时间设置
Poller::Poller()
  :epollfd_(-1),
  events_(MAXEVENTS),
  channels_(),
  mutex_() {
    epollfd_ = epoll_create1(EPOLL_CLOEXEC);
    if(epollfd_==-1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }
    std::cout << "Poller created with epollfd: " << epollfd_ << std::endl;
}

Poller::~Poller() {
    if (epollfd_ != -1) {
        close(epollfd_);
    }
}
//等待I/O事件
void Poller::poll(ChannelList &activeChannels) {
  int timeout= TIMEOUT; // 设置超时时间
  int nfds = epoll_wait(epollfd_, &*events_.begin(),static_cast<int>(events_.capacity()), timeout);
  if (nfds == -1) {
    perror("epoll_wait");
  }
  //处理就绪事件
  for (int i = 0; i < nfds; ++i) {
      Channel *channel = static_cast<Channel*>(events_[i].data.ptr); // 获取事件关联的 Channel
      int events=events_[i].events;// 获取触发的事件类型（如 EPOLLIN、EPOLLOUT 等）
      int fd = channel->GetFd(); // 获取 Channel 关联的文件描述符
      // 线程安全地查找 Channel 是否在 Poller 的监控列表中
      std::map<int,Channel*>::const_iterator iter;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        iter = channels_.find(fd);// channels_ 是 Poller 维护的 fd→Channel 映射表
      }
      if(iter!=channels_.end()) {// 找到对应的 Channel
          channel->SetEvents(events);// 将触发的事件类型存入 Channel
          activeChannels.push_back(channel);// 将 Channel 加入活跃列表
      } else {
          std::cerr << "Channel not found for fd: " << fd << std::endl;
      }
  }
  if(nfds==static_cast<int>(events_.capacity())) {
      events_.resize(events_.capacity() * 2); // 扩展事件数组
  }
}
void Poller::addChannel(Channel *channel) {
    int fd=channel->GetFd();
    struct epoll_event ev;
    ev.data.ptr = channel;
    ev.events = channel->GetEvents();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      channels_[fd]=channel;
    }
    if (epoll_ctl(epollfd_, EPOLL_CTL_ADD, fd, &ev) == -1) {
        perror("epoll_ctl: add");
        exit(-1);
    }
}
void Poller::removeChannel(Channel *channel) {
    int fd=channel->GetFd();
    struct epoll_event ev;
    ev.data.ptr = channel;
    ev.events = channel->GetEvents();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      channels_.erase(fd);
    }
    if (epoll_ctl(epollfd_, EPOLL_CTL_DEL, fd, NULL) == -1) {
        perror("epoll_ctl: del");
        exit(-1);
    }
}
void Poller::updateChannel(Channel *channel) {
    int fd=channel->GetFd();
    struct epoll_event ev;
    ev.data.ptr = channel;
    ev.events = channel->GetEvents();
    if (epoll_ctl(epollfd_, EPOLL_CTL_MOD, fd, &ev) == -1) {
        perror("epoll_ctl: mod");
        exit(-1);
    }
}