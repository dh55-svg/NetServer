#ifndef _CHANNEL_H_
#define _CHANNEL_H_
#include <functional>
#include <cstdint>
class Channel {
public:
  typedef std::function<void()> CallBack;
  Channel();
  ~Channel();
  void SetFd(int fd) { fd_ = fd; }
  int GetFd() const { return fd_; }
  void SetEvents(uint32_t events) { events_ = events; }
  uint32_t GetEvents() const { return events_; }
  void HandleEvent();//事件分发处理
  void setReadHandler(CallBack &&cb) { readhandler_ = std::move(cb); }
  void setWriteHandler(CallBack &&cb) { writehandler_ = std::move(cb); }
  void setErrorHandler(CallBack &&cb) { errorhandler_ = std::move(cb); }
  void setCloseHandler(CallBack &&cb) { closehandler_ = std::move(cb); }  
private:
  int fd_;
  uint32_t events_;//事件，一般情况下为epoll events
  //事件触发时执行的函数，在tcpconn中注册
  CallBack readhandler_;
  CallBack writehandler_;
  CallBack errorhandler_;
  CallBack closehandler_;

};

#endif // !_CHANNEL_H_