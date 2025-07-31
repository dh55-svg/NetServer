## 项目框架整体梳理

该项目是一个基于 事件驱动模型 的网络服务器框架，采用 Reactor模式 结合 多线程I/O，核心功能是处理TCP连接、数据读写及连接管理。整体架构遵循模块化设计，主要分为 事件循环、I/O多路复用、TCP连接管理、线程池 等核心组件，最终通过 EchoServer 实现具体的回显业务逻辑。

## 核心组件及职责

### 1、事件循环（EventLoop）

功能：作为Reactor模式的核心，负责I/O事件的轮询、分发及任务队列处理，运行在独立线程中。
关键代码：

```c++
EventLoop.cpp:53-63
Apply
void EventLoop::loop() {
    quit_ = false;
    while (!quit_) {
      poller.poll(activechannels_);  // 调用Poller::poll获取就绪事件
      for (auto &channel : activechannels_) {
        channel->HandleEvent();  // 分发事件到Channel处理
      }
      activechannels_.clear();
      ExecuteTask();  // 执行任务队列（跨线程任务）
    }
  }核心机制：
通过 Poller 封装 epoll 系统调用，监听文件描述符（FD）事件。
维护 任务队列（functors_），支持跨线程提交任务（通过 AddTask），并通过 wakeupfd_（事件fd）唤醒阻塞的事件循环。
```

### 2、事件通道（Channel）

功能：封装单个FD及其感兴趣的事件（如 EPOLLIN/EPOLLOUT），绑定事件回调函数（读/写/错误/关闭），是EventLoop与FD的桥梁。
关键代码：

```c++
void Channel::HandleEvent() {
    // 读事件（数据到达或连接关闭）
    if (events_ & (EPOLLIN | EPOLLPRI)) {
        if (readhandler_) readhandler_();  // 调用TcpConnection::HandleRead
    }
    // 写事件（缓冲区可写）
    if (events_ & EPOLLOUT) {
        if (writehandler_) writehandler_();  // 调用TcpConnection::HandleWrite
    }
    // 错误事件
    if (events_ & EPOLLERR) {
        if (errorhandler_) errorhandler_();  // 调用TcpConnection::HandleError
    }
    // 连接关闭事件
    if (events_ & EPOLLHUP) {
        if (closehandler_) closehandler_();  // 调用TcpConnection::HandleClose
    }
}
设计要点：通过回调函数解耦事件触发与业务逻辑（如TcpConnection的读写处理）。
```

### 3、I/O多路复用（Poller）

功能：封装 epoll 操作（epoll_create/epoll_ctl/epoll_wait），管理Channel的注册、修改、删除，返回就绪事件。
关键代码：

Poller.cpp:1-20
Apply
Poller::Poller() : epollfd_(-1), events_(MAXEVENTS), channels_(), mutex_() {
    epollfd_ = epoll_create1(EPOLL_CLOEXEC);  // 创建epoll实例
    if(epollfd_==-1) { perror("epoll_create1"); exit(EXIT_FAILURE); }
}
核心操作：
addChannel：通过 epoll_ctl(EPOLL_CTL_ADD) 注册FD事件。
poll：调用 epoll_wait 阻塞等待事件，填充 activeChannels 并返回给EventLoop。

### 4、TCP服务器（TcpServer）

功能：监听端口、接受新连接、管理TCP连接生命周期，通过线程池分发连接到多线程I/O。
启动流程（TcpServer::Start）：
初始化监听socket（设置地址复用、非阻塞、绑定端口、监听）。
将监听socket的Channel注册到EventLoop（监听 EPOLLIN | EPOLLET 事件）。
启动EventLoop线程池（EventLoopThreadPool::Start）。
接受连接（TcpServer::OnNewConnection）：

TcpServer.cpp:46-74
Apply
void TcpServer::OnNewConnection() {
    struct sockaddr_in peeraddr;
    int connfd = socket_.Accept(peeraddr);  // 循环accept获取新连接FD
    while(connfd>0) {
        // 检查最大连接数、设置非阻塞
        EventLoop* loop = threadpool_.GetNextLoop();  // 从线程池获取EventLoop（轮询分发）
        auto conn = std::make_shared<TcpConnection>(loop, connfd, peeraddr);  // 创建TcpConnection
        conn->SetMessageCallBack(std::move(messagecallback_));  // 设置消息回调
        // ... 设置关闭、错误等回调
        conn->AddChannelToLoop();  // 将连接的Channel添加到EventLoop
    }
}

### 5、TCP连接（TcpConnection）

功能：封装单个TCP连接，处理数据读写、连接关闭、缓冲区管理，是业务逻辑与网络I/O的接口。
核心机制：
读数据：HandleRead 调用 recvn 读取数据到 readbuffer_，触发 messagecallback_（如EchoServer的消息处理）。

TcpConnection.cpp:86-100
Apply
void TcpConnection::HandleRead() {
    if (disconnected_) return;
    int n = recvn(sockfd_, readbuffer_);  // 读取数据（非阻塞+边缘触发）
    if (n > 0) messagecallback_(shared_from_this(), readbuffer_);  // 调用业务层回调
    else if (n == 0) HandleClose();  // 对端关闭连接
}
写数据：Send 将数据追加到 writebuffer_，通过 SendInLoop 调用 sendn 发送，未发送完则监听 EPOLLOUT 事件。
线程安全：跨线程调用通过 EventLoop::AddTask 提交任务到I/O线程执行（如 Send 方法中检查当前线程是否为I/O线程）。

### 6、线程池（EventLoopThreadPool）

功能：管理多个EventLoop线程，通过 轮询策略 分发TCP连接，实现I/O多路复用的多线程扩展。
关键代码：

EventLoopThreadPool.cpp:29-36
Apply
EventLoop *EventLoopThreadPool::GetNextLoop() {
    if (threads_.empty()) return mainloop_;  // 无线程时使用主线程EventLoop
    EventLoop *nextLoop = threads_[index_]->GetLoop();  // 轮询选择下一个EventLoop
    index_ = (index_ + 1) % threadnum_;
    return nextLoop;
}