#ifndef _EVENTLOOPTHREADPOOL_H_
#define _EVENTLOOPTHREADPOOL_H_
#include <iostream>
#include <vector>
#include <string>
#include "EventLoop.h"
#include "EventLoopThread.h"
class EventLoopThreadPool {
public:
  EventLoopThreadPool(EventLoop *mainloop, int threadnum=0);
  ~EventLoopThreadPool();
  void Start();
  //获取下一个被分发的loop，依据RR轮询策略
  EventLoop *GetNextLoop();
private:
  EventLoop *mainloop_;
  int threadnum_;
  std::vector<EventLoopThread *> threads_;
  int index_; //用于轮询分发的索引
};
#endif // !_EVENTLOOPTHREAD_H_