#include "TcpConnection.h"
#include <stdio.h>
#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>
#define BUFSIZE 4096
int recvn(int fd,std::string &bufferin);
int sendn(int fd, std::string &bufferout);
TcpConnection::TcpConnection(EventLoop* loop, int sockfd, const struct sockaddr_in& peeraddr)
    : loop_(loop), sockfd_(sockfd), peeraddr_(peeraddr), channel_(new Channel()),
      halfclose_(false), disconnected_(false), asyncprocessing_(false) ,readbuffer_(), writebuffer_() {
  channel_->SetFd(sockfd_);
  channel_->SetEvents(EPOLLIN |EPOLLET);
  channel_->setReadHandler(std::bind(&TcpConnection::HandleRead, this));
  channel_->setWriteHandler(std::bind(&TcpConnection::HandleWrite, this));
  channel_->setErrorHandler(std::bind(&TcpConnection::HandleError, this));
  channel_->setCloseHandler(std::bind(&TcpConnection::HandleClose, this));
}
TcpConnection::~TcpConnection() {
  loop_->RemoveChannelFromPoller(channel_.get());
  if (sockfd_ >= 0) {
    close(sockfd_); // 关闭socket
  }
}
void TcpConnection::AddChannelToLoop() {
  loop_->AddTask(std::bind(&EventLoop::AddChannelToPoller, loop_, channel_.get()));
}
void TcpConnection::Send(const std::string& message) {
  writebuffer_ += message;
  if (loop_->GetThreadId() == std::this_thread::get_id()) {
    SendInLoop();
  } else {
    asyncprocessing_ = false; // 设置异步处理标志
    loop_->AddTask(std::bind(&TcpConnection::SendInLoop, shared_from_this()));//跨线程调用,加入IO线程的任务队列，唤醒
  }
}
void TcpConnection::SendInLoop() {
  if (writebuffer_.empty()) {
    return; // 没有数据需要发送
  }
  int n = sendn(sockfd_, writebuffer_);
  if (n < 0) {
    perror("send error");
    HandleError();
  } else if(n>0){
    uint32_t events = channel_->GetEvents();
    if (writebuffer_.size()>0)
    {
      //缓冲区满了，数据没发完，就设置EPOLLOUT事件触发	
      channel_->SetEvents(events | EPOLLOUT); // 设置可写事件
      loop_->UpdateChannelInPoller(channel_.get());
    }
    else
    {
      //缓冲区空了，数据发完了，就设置EPOLLIN事件触发
      channel_->SetEvents(events & (~EPOLLOUT)); // 清除可写事件
      sendcompletecallback_(shared_from_this()); // 发送完成回调
      if(halfclose_){
        HandleClose(); // 半关闭状态，处理连接关闭
      }
    }
  }else{
    HandleClose(); // 连接关闭
  }
}
void TcpConnection::Shutdown() {
  if (loop_->GetThreadId() == std::this_thread::get_id()) {
    ShutdownInLoop();
  } else {
    //不是IO线程，则是跨线程调用,加入IO线程的任务队列，唤醒
    
    loop_->AddTask(std::bind(&TcpConnection::ShutdownInLoop, shared_from_this()));
  }
}
void TcpConnection::ShutdownInLoop() {
  if (disconnected_) {
    return; // 已经断开连接
  }
  std::cout << "TcpConnection::ShutdownInLoop" << std::endl;
  closecallback_(shared_from_this()); //应用层清理连接回调
  loop_->AddTask(std::bind(connectioncleanup_, shared_from_this()));//自己不能清理自己，交给loop执行，Tcpserver清理TcpConnection
  disconnected_ = true; // 设置为断开连接状态
}
void TcpConnection::HandleRead() {
  if (disconnected_) {
    return; // 已经断开连接
  }
  
  int n = recvn(sockfd_, readbuffer_);
  if (n < 0) {
    perror("recv error");
    HandleError();
  } else if (n == 0) {
    HandleClose(); // 对端关闭连接
  } else {
    messagecallback_(shared_from_this(), readbuffer_); // 调用消息回调
  }
}
void TcpConnection::HandleWrite() {
  int result=sendn(sockfd_, writebuffer_);
  if(result>0)
  {
    uint32_t events = channel_->GetEvents();
    if (writebuffer_.size() > 0) {
      // 缓冲区还有数据未发送，继续设置EPOLLOUT事件
      channel_->SetEvents(events | EPOLLOUT);
      loop_->UpdateChannelInPoller(channel_.get());
    } else {
      // 缓冲区已空，清除EPOLLOUT事件
      channel_->SetEvents(events & (~EPOLLOUT));
      sendcompletecallback_(shared_from_this()); // 发送完成回调
      if (halfclose_) {
        HandleClose(); // 半关闭状态，处理连接关闭
      }
    }
  } else if(result < 0) {
    HandleError(); 
  }else{
    HandleClose(); // 连接关闭
  }
}
void TcpConnection::HandleError() {
  if(disconnected_) {
    return; // 已经断开连接
  }
  errorcallback_(shared_from_this()); // 错误回调
  loop_->AddTask(std::bind(connectioncleanup_, shared_from_this()));//自己不能清理自己，交给loop执行，Tcpserver清理TcpConnection
  disconnected_ = true; // 设置为断开连接状态
}
void TcpConnection::HandleClose() {
  if (disconnected_) {
    return; // 已经断开连接
  }
  std::cout << "TcpConnection::HandleClose" << std::endl;
  if(writebuffer_.size() > 0||readbuffer_.size() > 0||asyncprocessing_) {
    halfclose_ = true; //如果还有数据待发送，则先发完,设置半关闭标志位
    //还有数据刚刚才收到，但同时又收到FIN
    if(readbuffer_.size() > 0) {
      messagecallback_(shared_from_this(), readbuffer_); // 调用消息回调
    }
  }else{
    loop_->AddTask(std::bind(connectioncleanup_, shared_from_this()));//自己不能清理自己，交给loop执行，Tcpserver清理TcpConnection
    closecallback_(shared_from_this()); // 应用层清理连接回调
    disconnected_ = true; // 设置为断开连接状态
  }
}
int recvn(int fd, std::string &bufferin) {
  int nbyte=0;
  int readsum=0;
  char buf[BUFSIZE];
  for (;;)
  {
    nbyte=read(fd, buf, BUFSIZE);
    if (nbyte >0) {
      bufferin.append(buf, nbyte);
      readsum += nbyte;
      if (nbyte < BUFSIZE) {
        return readsum; // 读取完毕,读优化，减小一次读调用，因为一次调用耗时10+us
      }else{
        continue; // 继续读取
      }
    }else if(nbyte<0)
    {
      if (errno==EAGAIN)//系统缓冲区未有数据，非阻塞返回
      {
        return readsum;
      }else if(errno==EINTR) //被信号打断，继续读取
      {
        continue;
      }else{
        perror("read error");
        return -1; // 读取错误
      }
  }else{//返回0，客户端关闭socket，FIN
    return 0;
  }
}
}
int sendn(int fd, std::string &bufferout) {
  ssize_t nbyte=0;
  int sendsum=0;
  size_t length = bufferout.size();
  if(length>= BUFSIZE) {
    length = BUFSIZE; // 限制发送缓冲区大小
  }
  for(;;)
  {
    nbyte=write(fd,bufferout.c_str(),length);
    if(nbyte>0)
    {
      sendsum += nbyte;
      bufferout.erase(0, nbyte); // 移除已发送的数据
      length=bufferout.size();
      if(length>=BUFSIZE) {
        length = BUFSIZE; // 限制发送缓冲区大小
      }
      if(length==0) {
        return sendsum; // 发送完毕
      }
    }else if(nbyte<0) {
      if(errno==EAGAIN) {
        return sendsum; // 非阻塞返回，缓冲区满
      } else if(errno==EINTR) {
        continue; // 被信号打断，继续发送
      } else if(errno==EPIPE) {
        perror("send error");
        return -1; // 发送错误
      }else {
        perror("send error");
        return -1; // 发送错误
      }
    } else { // nbyte == 0，连接关闭
      return 0;
    }
  }
}