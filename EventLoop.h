#ifndef _EVENTLOOP_H_
#define _EVENTLOOP_H_

#include <iostream>
#include <functional>
#include <vector>
#include <thread>
#include <mutex>
#include "Poller.h"
#include "Channel.h"

class EventLoop {
public:
    //任务类型
    typedef std::function<void()> Functor;
    //事件列表类型
    typedef std::vector<Channel *> ChannelList;
    EventLoop();
    ~EventLoop();

    void loop();
    void AddChannelToPoller(Channel *channel)
    {
        poller.addChannel(channel);
    }
    void RemoveChannelFromPoller(Channel *channel)
    {
        poller.removeChannel(channel);
    }
    void UpdateChannelInPoller(Channel *channel)
    {
        poller.updateChannel(channel);
    }
    void quit()
    {
      quit_=true;
    }
    std::thread::id GetThreadId() const
    {
      return tid;
    }
    void wakeup();
    //唤醒loop后的读回调
    void HandleRead();
    //唤醒loop后的错误处理回调
    void HandleError();
    //向任务队列添加任务
    void AddTask(Functor functor)
    {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        functors_.push_back(std::move(functor));
      }
      wakeup();
    }
    //执行任务队列的任务
    void ExecuteTask(){
      std::vector<Functor> functorlist;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        functorlist.swap(functors_);
      }
      for(auto &functor:functorlist) {
        functor();
      }
      functorlist.clear();
    }
private:
    //任务列表 
    std::vector<Functor> functors_;       // 任务队列（跨线程提交的任务）
    ChannelList channels_;            // 所有注册的事件通道（Channel）
    ChannelList activechannels_;          // 就绪事件列表（epoll_wait 返回的活跃事件）
    Poller poller;                        // 封装 epoll 操作（I/O 多路复用核心）
    bool quit_;                           // 循环运行状态（控制 loop() 退出）
    std::thread::id tid;                  // 事件循环所属线程 ID（线程亲和性）
    std::mutex mutex_;                    // 保护任务队列的互斥锁
    int wakeupfd_;                        // 跨线程唤醒 FD（用于唤醒阻塞的 epoll_wait）
    Channel wakeupchannel_;               // 唤醒事件通道（监听 wakeupfd_ 的可读事件）
};

#endif // !_EVENTLOOP_H_
