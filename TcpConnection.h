#ifndef _TCPCONNECTION_H_
#define _TCPCONNECTION_H_
#include <functional>
#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <thread>
#include <memory>
#include "Channel.h"
#include "EventLoop.h"
class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
public:
  typedef std::shared_ptr<TcpConnection> spTcpConnection;
  //回调函数类型
  typedef std::function<void(const spTcpConnection&)> CallBack;
  typedef std::function<void(const spTcpConnection&, std::string&)> MessageCallBack;
  TcpConnection(EventLoop* loop, int sockfd, const struct sockaddr_in& peeraddr);
  ~TcpConnection();
  //获取当前连接的fd
  int fd() const { return sockfd_; }
  //获取当前连接所属的loop
  EventLoop* GetLoop() const { return loop_; }
  //添加本连接对应的事件到loop
  void AddChannelToLoop();
  //发送数据的函数
  void Send(const std::string& message);
  //在当前IO线程发送数据函数
  void SendInLoop();
  //主动清理连接
  void Shutdown();
  //在当前IO线程清理连接函数
  void ShutdownInLoop();
  //可读事件回调
  void HandleRead(); 
  //可写事件回调
  void HandleWrite(); 
  //错误事件回调
  void HandleError(); 
  //连接关闭事件回调
  void HandleClose();
  //设置收到数据回调函数
  void SetMessageCallBack(MessageCallBack &&cb) {
    messagecallback_ = std::move(cb);
  }
  void SetSendCompleteCallBack(CallBack &&cb) {
    sendcompletecallback_ = std::move(cb);
  }
  //设置连接关闭的回调函数
  void SetCloseCallBack(CallBack &&cb) {
    closecallback_ = std::move(cb);
  }
  //设置连接异常的回调函数
  void SetErrorCallBack(CallBack &&cb) {
    errorcallback_ = std::move(cb);
  }
  //设置连接清理函数
  void SetConnectionCleanup(CallBack &&cb) {
    connectioncleanup_ = std::move(cb);
  }
  //设置异步处理标志，开启工作线程池的时候使用
  void SetAsyncProcessing(const bool async) {
    asyncprocessing_ = async;
  }
private:
  EventLoop* loop_;//当前连接所在的loop
  std::unique_ptr<Channel> channel_;//当前连接的事件
  int sockfd_;
  struct sockaddr_in peeraddr_;//对端地址
  bool halfclose_;//是否半关闭
  bool disconnected_;//是否断开连接
  //异步调用标志位,当工作任务交给线程池时，置为true，任务完成回调时置为false
  bool asyncprocessing_;
  //读写缓冲
  std::string readbuffer_;
  std::string writebuffer_;
  //各种回调函数
  MessageCallBack messagecallback_;//消息回调
  CallBack sendcompletecallback_;//发送完成回调
  CallBack closecallback_;//关闭回调
  CallBack errorcallback_;//错误回调
  CallBack connectioncleanup_;//连接清理回调
};

#endif // !_TCPCONNECTION_H_
