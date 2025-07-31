#ifndef _ECHOSERVER_H_
#define _ECHOSERVER_H_
#include <string>
#include "TcpServer.h"
#include "EventLoop.h"
#include "TcpConnection.h"
#include "Timer.h"
class EchoServer {
public:
  typedef std::shared_ptr<TcpConnection> TcpConnectionPtr;
  typedef std::shared_ptr<Timer> TimerPtr;
  EchoServer(EventLoop* loop,const uint16_t port,const int threadnum);
  ~EchoServer();
  void Start();
private:
  void HandleNewConnection(const TcpConnectionPtr& conn);
  void HandleMessage(const TcpConnectionPtr& conn,std::string& message);
  void HandleSendComplete(const TcpConnectionPtr& conn);
  void HandleClose(const TcpConnectionPtr& conn);
  void HandleError(const TcpConnectionPtr& conn);
  TcpServer server_;
};

#endif // !1