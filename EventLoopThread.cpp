#include "EventLoopThread.h"
#include <iostream>
#include <sstream>
EventLoopThread::EventLoopThread()
    : thread_(), threadid_(-1), threadname_("IO thread"), loop_(nullptr) {}
EventLoopThread::~EventLoopThread() {
  //线程结束时清理
  std::cout << "EventLoopThread destructor called." << std::endl;
  loop_->quit();
  thread_.join();
}
EventLoop *EventLoopThread::GetLoop() {
  return loop_;
}
void EventLoopThread::Start() {
  thread_=std::thread(&EventLoopThread::ThreadFunc, this);
}
void EventLoopThread::ThreadFunc() {
  EventLoop loop;
  loop_ = &loop;
  threadid_ = std::this_thread::get_id();
  std::stringstream sin;
  sin<<threadid_;
  threadname_+= sin.str();
  std::cout << "EventLoopThread started with thread ID: " << threadid_;
  try
  {
    loop_->loop();
  }
  catch(std::bad_alloc& ba)
  {
    std::cerr << "Memory allocation failed: " << ba.what() << std::endl;
  }
  
}