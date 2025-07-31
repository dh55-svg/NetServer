#include "TcpServer.h"
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <memory>

void SetNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl get flags");
        exit(EXIT_FAILURE);
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl set non-blocking");
        exit(EXIT_FAILURE);
    }
}
TcpServer::TcpServer(EventLoop* loop, const int port, const int threadnum)
    : loop_(loop), socket_(), acceptchannel_(), conncount_(0), threadpool_(loop, threadnum) {
    // 设置服务器套接字选项
    socket_.SetReuseAddr();
    // 绑定地址
    socket_.BindAddress(port);
    socket_.Setnonblocking();
    socket_.Listen();
    acceptchannel_.SetFd(socket_.fd());
    acceptchannel_.setReadHandler(std::bind(&TcpServer::OnNewConnection, this));
    acceptchannel_.setErrorHandler(std::bind(&TcpServer::OnConnectionError, this));
}
TcpServer::~TcpServer() {
    
}
void TcpServer::Start() {
    // 启动线程池
    threadpool_.Start();
    acceptchannel_.SetEvents(EPOLLIN | EPOLLET); // 设置为边缘触发模式
    // 将acceptchannel添加到事件循环中
    loop_->AddChannelToPoller(&acceptchannel_);
    std::cout << "TcpServer started on port " << socket_.fd() << std::endl;
}
void TcpServer::OnNewConnection() {
  //循环调用accept，获取所有的建立好连接的客户端fd
    struct sockaddr_in peeraddr;
    int connfd = socket_.Accept(peeraddr);
    while(connfd>0)
    {
      std::cout<<"new connection from Ip:"<<inet_ntoa(peeraddr.sin_addr)<<":"<<ntohs(peeraddr.sin_port)<<std::endl;
      if(++conncount_ > MAX_CONNECTIONS) {
        std::cerr << "Max connections reached, closing new connection." << std::endl;
        close(connfd);
        continue;
      }
      SetNonBlocking(connfd); // 设置新连接为非阻塞
      EventLoop* loop = threadpool_.GetNextLoop();
      auto conn = std::make_shared<TcpConnection>(loop, connfd, peeraddr);
      conn->SetMessageCallBack(std::move(messagecallback_));
      conn->SetSendCompleteCallBack(std::move(sendcompletecallback_));
      conn->SetCloseCallBack(std::move(closecallback_));
      conn->SetErrorCallBack(std::move(errorcallback_));
      conn->SetConnectionCleanup(std::bind(&TcpServer::RemoveConnection, this, std::placeholders::_1));
      {
        std::lock_guard<std::mutex> lock(connmap_mutex_);
        connmap_[connfd] = conn; // 添加到连接映射表
      }
      newconnectioncallback_(conn); // 调用新连接回调
      conn->AddChannelToLoop(); // 将连接的事件添加到对应的事件循环中

    }
}
void TcpServer::RemoveConnection(const TcpConnectionPtr& conn) {
    std::lock_guard<std::mutex> lock(connmap_mutex_);
    --conncount_;
    connmap_.erase(conn->fd()); // 从连接映射表中移除
}
void TcpServer::OnConnectionError() {
    std::cout << "Connection error occurred." << std::endl;
    socket_.Close(); // 关闭服务器套接字
}
