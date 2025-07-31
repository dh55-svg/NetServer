#ifndef _TCPSERVER_H_
#define _TCPSERVER_H_
#include <functional>
#include <string>
#include <unordered_map>
#include <mutex>
#include "Socket.h"
#include "Channel.h"
#include "EventLoop.h"
#include "EventLoopThreadPool.h"
#include "TcpConnection.h"
#define MAX_CONNECTIONS 20000
class TcpServer {
public:
  typedef std::shared_ptr<TcpConnection> TcpConnectionPtr;
  typedef std::function<void(const TcpConnectionPtr&)> ConnectionCallback;
  typedef std::function<void(const TcpConnectionPtr&,std::string&)> MessageCallback;
  TcpServer(EventLoop* loop,const int port,const int threadnum=0);
  ~TcpServer();
  //启动服务器
  void Start();
  //设置新连接回调函数
  void SetNewConnectionCallback(ConnectionCallback cb){
    newconnectioncallback_=cb;
  }
  //设置消息处理回调函数
  void SetMessageCallback(MessageCallback cb){
    messagecallback_=cb;
  }
  //设置发送完成回调函数
  void SetSendCompleteCallback(ConnectionCallback cb){
    sendcompletecallback_=cb;
  }
  //设置连接关闭回调函数
  void SetCloseCallback(ConnectionCallback cb){
    closecallback_=cb;
  }
  //设置连接异常回调函数
  void SetErrorCallback(ConnectionCallback cb){
    errorcallback_=cb;
  }
private:
  Socket socket_; //服务器套接字
  EventLoop* loop_; //服务器所在的事件循环
  Channel acceptchannel_; //接受连接的事件
  int conncount_;//连接数量统计
  std::unordered_map<int,TcpConnectionPtr> connmap_; //连接映射表
  std::mutex connmap_mutex_; //连接映射表的互斥量保护
  EventLoopThreadPool threadpool_; //IO线程池
  ConnectionCallback newconnectioncallback_; //连接建立回调
  MessageCallback messagecallback_; //消息处理回调
  ConnectionCallback sendcompletecallback_; //发送完成回调
  ConnectionCallback closecallback_; //连接关闭回调
  ConnectionCallback errorcallback_; //连接异常回调
  void OnNewConnection();//服务器对新连接连接处理的函数
  void RemoveConnection(const TcpConnectionPtr& conn);//移除TCP连接函数
  void OnConnectionError();//连接异常处理函数
};
#endif // !_TCPSERVER_H_
