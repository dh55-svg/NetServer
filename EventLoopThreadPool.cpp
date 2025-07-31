#include "EventLoopThreadPool.h"
EventLoopThreadPool::EventLoopThreadPool(EventLoop *mainloop, int threadnum)
    : mainloop_(mainloop), threadnum_(threadnum), threads_(),index_(0) {
  for(int i=0;i<threadnum_;i++) {
    EventLoopThread *thread = new EventLoopThread();
    threads_.push_back(thread);
  }
}
EventLoopThreadPool::~EventLoopThreadPool() {
  std::cout << "EventLoopThreadPool destructor called." << std::endl;
  for(auto &thread : threads_) {
    delete thread;
  }
  threads_.clear();
}
void EventLoopThreadPool::Start() {
  if(threadnum_>0)
  {
    for (int i = 0; i < threadnum_; i++)
    {
      threads_[i]->Start();
    }
    
  }else
  {
    std::cout << "No threads to start in EventLoopThreadPool." << std::endl;
  }
}
EventLoop *EventLoopThreadPool::GetNextLoop() {
  if (threads_.empty()) {
    return mainloop_;
  }
  EventLoop *nextLoop = threads_[index_]->GetLoop();
  index_ = (index_ + 1) % threadnum_;
  return nextLoop;
}