#ifndef _EVENTLOOPTHREAD_H_
#define _EVENTLOOPTHREAD_H_
#include <iostream>
#include <string>
#include <thread>
#include "EventLoop.h"

class EventLoopThread {
public:
  EventLoopThread();
  ~EventLoopThread();
  EventLoop *GetLoop();
  void Start();
  void ThreadFunc();
private:
  std::thread thread_;
  std::thread::id threadid_;
  std::string threadname_;
  EventLoop *loop_;
};
#endif // !_EVENTLOOPTHREAD_H_