# NetServer高性能服务器详解

## 一、项目定位与核心

该项目是一个基于 **事件驱动模型** 的网络服务器框架，采用 **Reactor模式** 结合 **多线程I/O**，核心功能包括TCP连接管理、数据读写、事件分发及业务逻辑解耦。整体架构遵循模块化设计，通过组件化拆分实现高内聚低耦合，最终通过 `EchoServer` 实现具体的回显业务逻辑。

## 二、**核心组件及职责**

### 1、事件驱动层

概述：负责事件轮询、任务调度及跨线程通信，是Reactor模式的核心。

#### ①Channel组件详解：

EventLoop与FD之间的桥梁

核心定位：

`Channel` 是 **FD（文件描述符）的事件封装器**，也是 **EventLoop（事件循环）与 FD 之间的桥梁**。它负责：

1. 绑定一个具体的 FD（如监听 socket、客户端连接 socket）；
2. 管理该 FD 感兴趣的事件（如可读 `EPOLLIN`、可写 `EPOLLOUT`、错误 `EPOLLERR` 等）；
3. 注册事件触发时的回调函数（读/写/错误/关闭）；
4. 将 FD 事件的触发与业务逻辑解耦（通过回调机制）。

核心功能为事件分发与回调触发

```c++
/**
	核心方法为HandleEvent，根据epoll返回的就绪事件(events_)出发相应的回调函数
*/
void Channel::HandleEvent() {
  // 读事件（数据到达或连接关闭）
  if (events_ & (EPOLLIN | EPOLLPRI)) {
    if (readhandler_) readhandler_();  // 调用 TcpConnection::HandleRead
  }
  // 写事件（缓冲区可写，用于发送数据）
  if (events_ & EPOLLOUT) {
    if (writehandler_) writehandler_();  // 调用 TcpConnection::HandleWrite
  }
  // 错误事件（如连接异常）
  if (events_ & EPOLLERR) {
    if (errorhandler_) errorhandler_();  // 调用 TcpConnection::HandleError
  }
  // 连接关闭事件（对端关闭连接）
  if (events_ & EPOLLHUP) {
    if (closehandler_) closehandler_();  // 调用 TcpConnection::HandleClose
  }
}
```

工作流程：

1、Poller(封装epoll)通过epoll_wait获取就绪事件列表，每个事件包含 FD 和触发的事件类型（如 `EPOLLIN`）；

2、`EventLoop` 将就绪事件对应的 `Channel` 加入 `activechannels_` 列表；

3、`EventLoop` 遍历 `activechannels_`，调用每个 `Channel` 的 `HandleEvent()` 方法；

4、`HandleEvent()` 根据 `events_`（就绪事件类型）触发对应的回调函数（如 `readhandler_`）。

作为桥梁的具体表现：

1、连接fd与eventloop

每个 FD（如客户端连接 socket）都绑定一个 `Channel`，`Channel` 通过 `EventLoop::AddChannelToPoller` 注册到 `Poller` 中。`EventLoop` 通过管理 `Channel` 间接管理 FD 的事件监听。

eg：TcpConnection 创建时，会初始化一个 `Channel` 并绑定到自身的 socket FD，然后通过 `AddChannelToLoop` 将 `Channel` 注册到 `EventLoop` 的 `Poller` 中：

```c++
void TcpConnection::AddChannelToLoop() {
  loop_->AddTask(std::bind(&EventLoop::AddChannelToPoller, loop_, channel_.get()));
}
```

2、负责解耦事件监听与业务逻辑

`Channel` 本身不处理业务逻辑，仅负责事件分发。具体的事件处理逻辑（如读取数据、发送数据）通过回调函数注册（由 `TcpConnection` 等组件实现）。

eg：`TcpConnection` 在初始化时，会将自身的 `HandleRead`、`HandleWrite` 等方法绑定到 `Channel` 的回调中：

```c++
TcpConnection::TcpConnection(EventLoop* loop, int sockfd, const struct sockaddr_in& peeraddr)
  : loop_(loop), sockfd_(sockfd), channel_(new Channel()) {
  channel_->SetFd(sockfd_);
  channel_->SetEvents(EPOLLIN | EPOLLET);  // 监听读事件（边缘触发）
  channel_->setReadHandler(std::bind(&TcpConnection::HandleRead, this));  // 绑定读回调
  channel_->setWriteHandler(std::bind(&TcpConnection::HandleWrite, this));  // 绑定写回调
  // ... 绑定错误、关闭回调
}
```

3、统一事件管理接口

无论 FD 类型（监听 socket、客户端连接 socket、唤醒 socket 等），`EventLoop` 均通过 `Channel` 接口进行事件管理（添加、修改、删除事件监听）。

eg：`EventLoop` 的 `AddChannelToPoller`、`UpdateChannelInPoller`、`RemoveChannelFromPoller` 方法，本质是通过 `Channel` 操作 `Poller` 对 FD 事件的监听：

```c++
void AddChannelToPoller(Channel *channel) { poller.addChannel(channel); }
void UpdateChannelInPoller(Channel *channel) { poller.updateChannel(channel); }
void RemoveChannelFromPoller(Channel *channel) { poller.removeChannel(channel); }
```

#### ②Poller组件详解

I/O多路复用的核心实现

核心定位：`Poller` 是 MyNetServer 中 **I/O 多路复用的封装层**，基于 Linux `epoll` 系统调用实现。

1、管理所有注册的文件描述符（FD）及其感兴趣的事件（如可读、可写）；

2、通过 `epoll_wait` 阻塞等待就绪事件，并返回给 `EventLoop`；

3、维护 FD 与 `Channel` 的映射关系，实现事件与业务逻辑的解耦。

核心实现：

Poller本质是对epoll系统调用的封装

1.通过构造函数舒适化epoll示例

2.注册/修改/删除FD事件(epoll_ctl封装)

```c++
/*
	以addChannel为例子，通过channels_[fd] = channel 维护 FD 与 Channel 的映射，后续 epoll_wait 返回就绪事件时，可通过 FD 快速找到对应的 Channel。
*/
void Poller::addChannel(Channel *channel) {
  int fd = channel->GetFd();                  // 获取 Channel 绑定的 FD
  uint32_t events = channel->GetEvents();     // 获取 Channel 感兴趣的事件（如 EPOLLIN | EPOLLET）
  
  struct epoll_event ev;
  ev.data.fd = fd;                            // 关联 FD 到 epoll_event（或使用 void* ptr 关联 Channel）
  ev.events = events;                         // 设置事件类型

  {
    std::lock_guard<std::mutex> lock(mutex_); // 线程安全：保护 channels_ 映射表
    channels_[fd] = channel;                  // 记录 FD 与 Channel 的映射关系
  }

  // 调用 epoll_ctl 注册事件
  if (epoll_ctl(epollfd_, EPOLL_CTL_ADD, fd, &ev) == -1) {
    perror("epoll_ctl add");
    // 错误处理：如从 channels_ 中移除 FD
    std::lock_guard<std::mutex> lock(mutex_);
    channels_.erase(fd);
  }
}
```

3。等待就绪事件

```c++
/*
	Poller::poll 是核心方法，负责阻塞等待事件，并将就绪事件对应的 Channel 填充到 activeChannels 中，供 EventLoop 分发处理：
	流程：
        调用 epoll_wait 阻塞等待事件，返回就绪事件数 numEvents；
        遍历 events_ 数组，通过 data.fd 获取就绪 FD；
        查找 channels_ 映射表，找到对应的 Channel，设置其就绪事件（revents），并加入 activeChannels；
        EventLoop 拿到 activeChannels 后，调用每个 Channel::HandleEvent() 分发事件。
*/
void Poller::poll(ChannelList &activeChannels) {
  int numEvents = epoll_wait(epollfd_, events_.data(), events_.size(), -1);  // 阻塞等待事件（超时时间 -1 表示永久阻塞）
  int savedErrno = errno;  // 保存 errno，避免后续系统调用覆盖

  if (numEvents > 0) {
    // 遍历就绪事件，填充 activeChannels
    for (int i = 0; i < numEvents; ++i) {
      int fd = events_[i].data.fd;  // 获取就绪 FD
      uint32_t revents = events_[i].events;  // 获取就绪事件类型（如 EPOLLIN、EPOLLOUT）

      std::lock_guard<std::mutex> lock(mutex_);
      auto it = channels_.find(fd);  // 通过 FD 查找对应的 Channel
      if (it != channels_.end()) {
        Channel *channel = it->second;
        channel->SetEvents(revents);  // 将就绪事件类型设置到 Channel 中
        activeChannels.push_back(channel);  // 添加到活跃 Channel 列表
      }
    }
    // 若就绪事件数等于 events_ 容量，扩容避免下次溢出
    if (numEvents == events_.size()) {
      events_.resize(events_.size() * 2);
    }
  } else if (numEvents == 0) {
    // 超时（未设置超时时间时不会触发）
  } else {
    // 错误处理（如 EINTR 被信号中断）
    if (savedErrno != EINTR) {
      perror("epoll_wait error");
    }
  }
}
```

核心机制：

Poller通过map维护fd和Channel映射

- **注册时**：`addChannel` 将 FD 和 `Channel` 存入 `channels_`（map），确保后续事件触发时能找到对应的 `Channel`。
- **事件触发时**：`poll` 方法通过就绪 FD 从 `channels_` 中查找 `Channel`，并将其加入活跃列表，供 `EventLoop` 处理。
- **线程安全**：`channels_` 的读写均通过 `mutex_` 加锁，避免多线程（如 `EventLoop` 线程与其他线程）并发修改导致的竞态条件。

与其他组件的交互

Poller是eventloop与底层epoll之间的桥梁

1. **初始化**：`EventLoop` 构造时创建 `Poller` 实例，`Poller` 初始化 `epollfd_`。
2. **注册事件**：`EventLoop` 通过 `AddChannelToPoller` 调用 `Poller::addChannel`，将 `Channel`（如监听 socket 的 `Channel`、客户端连接的 `Channel`）注册到 epoll。(就是加入到map映射表中)
3. **等待事件**：`EventLoop::loop()` 循环调用 `Poller::poll`，阻塞等待就绪事件，获取 `activeChannels`。(将epoll_wait就绪的事件从map映射表中找出放到activeChannels中)
4. **事件分发**：`EventLoop` 遍历 `activeChannels`，调用 `Channel::HandleEvent()`，触发业务回调(如TcpConnection::HandleRead)

#### ③EventLoop组件详解

核心定位：

1. **I/O 事件轮询**：通过 `Poller` 封装的 `epoll` 系统调用，监听并获取就绪的文件描述符（FD）事件（如可读、可写）；
2. **事件分发**：将就绪事件分发给对应的 `Channel`，触发业务回调（如 `TcpConnection` 的读写处理）；
3. **任务队列执行**：维护跨线程任务队列，支持其他线程向事件循环提交任务，并通过 `wakeupfd_` 唤醒阻塞的事件循环。

核心流程：

`EventLoop` 的核心是 `loop()` 方法，它通过无限循环实现事件轮询、分发和任务执行

```c++
void EventLoop::loop() {
  quit_ = false;
  while (!quit_) {
    activechannels_.clear();
    poller.poll(activechannels_);  // 1. 阻塞等待就绪事件（通过epoll_wait）
    
    // 2. 分发就绪事件到Channel
    for (auto &channel : activechannels_) {
      channel->HandleEvent();  // 调用Channel的事件处理（如TcpConnection::HandleRead）
    }
    
    // 3. 执行跨线程任务队列
    ExecuteTask();
  }
}
```

1、I/O事件轮询(通过poller)

- **调用 `poller.poll(activechannels_)`**：`Poller` 封装 `epoll_wait`，阻塞等待注册的 FD 事件就绪（如客户端数据到达、新连接请求）。
- **填充 `activechannels_`**：`Poller` 将就绪事件对应的 `Channel` 存入 `activechannels_`，返回给 `EventLoop`。
- **关键**：`Poller` 是 `EventLoop` 与底层 `epoll` 的桥梁，屏蔽了 I/O 多路复用的细节。

2、事件分发(出发业务回调)

- **遍历 `activechannels_`**：对每个就绪的 `Channel`，调用 `channel->HandleEvent()`。

- **`Channel` 事件处理**：`Channel` 根据就绪事件类型（如 `EPOLLIN`、`EPOLLOUT`）触发绑定的回调函数（如 `TcpConnection::HandleRead` 处理读事件）。

  ```c++
  void Channel::HandleEvent() {
    if (events_ & EPOLLIN) { readhandler_(); }  // 读事件回调
    if (events_ & EPOLLOUT) { writehandler_(); } // 写事件回调
    // ... 错误、关闭事件处理
  }
  ```

3、跨线程任务队列执行

- **调用 `ExecuteTask()`**：处理其他线程提交的任务（如主线程向 I/O 线程提交连接清理任务）。

- 任务队列实现

  ```c++
  void ExecuteTask() {
    std::vector<Functor> functorlist;
    {
      std::lock_guard<std::mutex> lock(mutex_);  // 加锁保护任务队列
      functorlist.swap(functors_);               // 交换任务队列，减少锁持有时间
    }
    for (auto &functor : functorlist) {
      functor();  // 执行任务（如TcpConnection::SendInLoop）
    }
  }
  ```

核心机制：跨线程唤醒与任务调度

`	EventLoop` 通常阻塞在 `poller.poll()`（即 `epoll_wait`），为了能及时响应跨线程任务，需通过 `wakeupfd_` 主动唤醒事件循环。

1、wakeupfd与唤醒与原理

**创建 `wakeupfd_`**：`EventLoop` 构造时通过 `CreateEventFd()` 创建 `wakeupfd_`（基于 Linux `eventfd`），这是一个特殊的 FD，写入时会触发可读事件。

```c++
int CreateEventFd() {
  int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);  // 非阻塞+进程退出时关闭
  if (fd == -1) { perror("eventfd"); exit(EXIT_FAILURE); }
  return fd;
}
```

**`wakeupchannel_` 注册**：`EventLoop` 将 `wakeupfd_` 封装为 `wakeupchannel_`，监听 `EPOLLIN` 事件，确保写入时能唤醒 `epoll_wait`。

```c++
EventLoop::EventLoop() : wakeupfd_(CreateEventFd()), wakeupchannel_() {
  wakeupchannel_.SetFd(wakeupfd_);
  wakeupchannel_.SetEvents(EPOLLIN | EPOLLET);  // 边缘触发模式
  wakeupchannel_.setReadHandler(std::bind(&EventLoop::HandleRead, this));
  AddChannelToPoller(&wakeupchannel_);  // 注册到Poller
}
```

2、唤醒流程（AddTask与wakeup）

当其他线程通过 `AddTask` 提交任务时，`EventLoop` 会写入 `wakeupfd_` 触发唤醒：

```c++
void AddTask(Functor functor) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    functors_.push_back(std::move(functor));  // 添加任务到队列
  }
  wakeup();  // 唤醒事件循环
}

void EventLoop::wakeup() {
  uint64_t one = 1;
  ssize_t n = write(wakeupfd_, &one, sizeof(one));  // 写入8字节数据
  if (n != sizeof(one)) { perror("write wakeupfd"); }
}
```

**唤醒后处理**：`wakeupfd_` 的可读事件触发 `wakeupchannel_.HandleEvent()`，调用 `EventLoop::HandleRead()` 读取数据并清除事件：

```c++
void EventLoop::HandleRead() {
  uint64_t one = 0;
  ssize_t n = read(wakeupfd_, &one, sizeof(one));  // 读取8字节数据（清除事件）
  if (n != sizeof(one)) { perror("read wakeupfd"); }
}
```

线程的亲和性：确保事件循环在固定线程执行

`EventLoop` 具有 **线程亲和性**，即一旦启动，会绑定到创建它的线程，并通过 `tid` 记录线程 ID：

作用：避免多线程并发操作 `EventLoop` 的内部状态（如 `activechannels_`、`functors_`）。

示例：`TcpConnection::Send` 方法会检查当前线程是否为 `EventLoop` 线程，若非则通过 `AddTask` 提交任务，确保线程安全：

```c++
void TcpConnection::Send(const std::string& message) {
  if (loop_->GetThreadId() == std::this_thread::get_id()) {
    SendInLoop();  // 同线程直接执行
  } else {
    loop_->AddTask(std::bind(&TcpConnection::SendInLoop, shared_from_this()));  // 跨线程提交任务
  }
}
```

### 2、网络I/O层

#### ①、Socket组件详解

核心定位：底层网络通信的封装类

- 创建/关闭 socket 文件描述符（FD）；
- 设置 socket 选项（如地址复用、非阻塞模式）；
- 绑定端口、监听连接、接受客户端连接；
- 管理 socket 生命周期（自动释放资源）。

关键socket选项配置

（1）、地址复用：解决服务器重启时 "Address already in use" 错误，允许端口在 TIME_WAIT 状态下被重新绑定。

```c++
/*
使用场景：TcpServer 初始化时调用（TcpServer.cpp:26），确保服务器重启后能快速绑定相同端口。
*/
void Socket::SetReuseAddr() {
  int on = 1;
  setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));  // 设置SO_REUSEADDR选项
}
```

（2）、非阻塞模式：所有 socket（监听/连接）均设置为非阻塞，配合 `epoll` 边缘触发（EPOLLET）模式，避免 I/O 操作阻塞事件循环。

#### ②、TcpConnection组件详解

核心定位：`TcpConnection` 是 MyNetServer 中 **单个 TCP 连接的封装器**，是网络层与业务逻辑层的桥梁。负责

- **数据读写**：通过 `HandleRead`/`HandleWrite` 处理 TCP 数据流，与缓冲区配合实现高效收发；
- **连接生命周期管理**：从连接建立到关闭的全流程控制（包括主动关闭、被动关闭、半关闭）；
- **线程安全通信**：支持跨线程发送数据，通过 `EventLoop` 任务队列确保线程安全；
- **业务解耦**：通过回调函数（如消息处理、连接关闭）将网络事件与业务逻辑分离。

**核心功能详解：**

数据读取：从网络到业务层

触发条件：当客户端发送数据时，Channel监听的EPOLLIN事件就绪C`hannel::HandleEvent` 调用 `TcpConnection::HandleRead`。

```c++
/*
	非阻塞读取：recvn 是封装的非阻塞读函数，循环读取直到无数据或错误（边缘触发模式下需一次性读完）；
	业务回调：读取的数据通过 messagecallback_ 传递给业务层（如 EchoServer 处理后回显）；
	连接关闭检测：n=0 表示对端发送 FIN 包，触发 HandleClose。
*/
void TcpConnection::HandleRead() {
  if (disconnected_) return;  // 已断开连接，忽略事件
  int n = recvn(sockfd_, readbuffer_);  // 非阻塞读取数据到readbuffer_
  if (n > 0) {
    messagecallback_(shared_from_this(), readbuffer_);  // 调用业务层回调（如EchoServer::HandleMessage）
    readbuffer_.clear();  // 清空缓冲区（业务层已处理）
  } else if (n == 0) {
    HandleClose();  // 对端关闭连接（TCP FIN包）
  } else {
    if (errno != EAGAIN && errno != EWOULDBLOCK) {  // 非阻塞重试错误
      HandleError();  // 其他错误（如连接重置）
    }
  }
}
```

数据发送：

业务层调用 `Send` 发送数据，`TcpConnection` 通过缓冲区和事件监听实现高效发送。

```c++
/*
	线程安全保障：若当前线程不是 loop_ 所属线程（如业务线程调用 Send），通过 EventLoop::AddTask 将发送任务提交到 IO 线程执行，避免多线程操作缓冲区冲突；
	shared_from_this()：确保 TcpConnection 对象在异步发送期间不被销毁（延长生命周期）。
*/
void TcpConnection::Send(const std::string& message) {
  writebuffer_ += message;  // 数据追加到写缓冲区
  if (loop_->GetThreadId() == std::this_thread::get_id()) {
    SendInLoop();  // 同IO线程，直接发送
  } else {
    // 跨线程：通过EventLoop任务队列异步发送（避免线程安全问题）
    loop_->AddTask(std::bind(&TcpConnection::SendInLoop, shared_from_this()));
  }
}
```

IO线程内发送SendInLoop：

```c++
/*
	写缓冲区作用：当发送缓冲区满（sendn 返回部分发送字节），剩余数据暂存 writebuffer_，避免阻塞；
	EPOLLOUT 动态监听：仅当有未发送数据时才监听 EPOLLOUT 事件，数据发送完毕后立即取消，减少无效事件触发；
	发送完成回调：全部数据发送后，通过 sendcompletecallback_ 通知业务层（如 EchoServer::HandleSendComplete）。
*/
void TcpConnection::SendInLoop() {
  if (disconnected_) return;
  ssize_t n = sendn(sockfd_, writebuffer_);  // 非阻塞发送数据
  if (n > 0) {
    writebuffer_.erase(0, n);  // 移除已发送数据
    if (writebuffer_.empty()) {
      // 数据发送完毕，取消EPOLLOUT监听（避免频繁触发）
      channel_->SetEvents(channel_->GetEvents() & ~EPOLLOUT);
      loop_->UpdateChannelInPoller(channel_.get());
      sendcompletecallback_(shared_from_this());  // 通知业务层发送完成
    } else {
      // 数据未发送完，继续监听EPOLLOUT（等待缓冲区可写）
      channel_->SetEvents(channel_->GetEvents() | EPOLLOUT);
      loop_->UpdateChannelInPoller(channel_.get());
    }
  } else {
    HandleError();  // 发送错误（如连接重置）
  }
}
```

连接关闭：关闭流程：支持主动关闭（`Shutdown`）和被动关闭（对端断开），核心是 `HandleClose` 方法。

1、被动关闭：由 `HandleRead` 检测到 `n=0` 或 `Channel` 触发 `EPOLLHUP` 事件时调用：

```c++
void TcpConnection::HandleClose() {
  if (disconnected_) return;
  disconnected_ = true;
  // 异步清理连接（避免在回调中删除自身）
  loop_->AddTask(std::bind(connectioncleanup_, shared_from_this()));  // connectioncleanup_绑定TcpServer::RemoveConnection
  closecallback_(shared_from_this());  // 通知业务层连接关闭（如EchoServer::HandleClose）
  channel_->RemoveFromPoller();  // 从Poller中移除Channel
  ::close(sockfd_);  // 关闭socket FD
}
```

2、主动关闭：业务层调用 `Shutdown` 主动关闭连接，支持半关闭（发送剩余数据后关闭写端）：

```c++
void TcpConnection::Shutdown() {
  if (loop_->GetThreadId() == std::this_thread::get_id()) {
    ShutdownInLoop();  // 同IO线程直接处理
  } else {
    loop_->AddTask(std::bind(&TcpConnection::ShutdownInLoop, shared_from_this()));  // 跨线程异步处理
  }
}

void TcpConnection::ShutdownInLoop() {
  if (disconnected_) return;
  if (writebuffer_.empty()) {
    // 缓冲区无数据，直接关闭写端
    ::shutdown(sockfd_, SHUT_WR);
    halfclose_ = false;
  } else {
    // 缓冲区有数据，设置半关闭标志，发送完成后关闭
    halfclose_ = true;
  }
}
```

缓冲区管理：readbuffer_与writebuffer_

- **读缓冲区（readbuffer_）**：
  - 作用：暂存从 socket 读取的原始数据，避免频繁调用业务回调；
  - 场景：`HandleRead` 通过 `recvn` 将数据读入 `readbuffer_`，积累到完整数据包后（需业务层处理粘包）调用 `messagecallback_`。

- **写缓冲区（writebuffer_）**：
  - 作用：暂存待发送数据，应对发送缓冲区满的情况，避免阻塞 IO 线程；
  - 场景：`Send` 方法追加数据到 `writebuffer_`，`SendInLoop` 负责实际发送，未发送完的数据在 `EPOLLOUT` 事件触发时继续发送。

线程安全与生命周期管理

- **线程亲和性**：每个 `TcpConnection` 绑定到一个 `EventLoop`（子线程 IO 线程），所有操作最终在该线程执行，避免多线程竞争；
- **智能指针（shared_ptr）**：继承 `std::enable_shared_from_this<TcpConnection>`，通过 `shared_from_this()` 获取智能指针，确保异步操作（如跨线程任务）期间对象不被销毁；
- **跨线程任务队列**：通过 `EventLoop::AddTask` 提交任务，所有对 `TcpConnection` 的修改均在 IO 线程执行，保证线程安全。

#### ③、TcpServer组件详解：

**核心定位**：`TcpServer` 是 MyNetServer 的 **服务器主体组件**，承担统筹协调职责，是连接底层网络操作与上层业务逻辑的中枢。其核心使命包括：

- **监听端口与接受连接**：通过封装的 `Socket` 组件启动监听，响应客户端连接请求；
- **连接生命周期管理**：创建、维护、销毁 `TcpConnection` 对象，跟踪所有活跃连接；
- **连接分发与负载均衡**：通过 `EventLoopThreadPool` 将新连接轮询分发到子线程 `EventLoop`，实现多线程 I/O 处理；
- **业务回调桥接**：提供回调注册接口（新连接、消息、关闭等），将网络事件传递给业务层（如 `EchoServer`）。

**核心功能详解**

1、服务器启动流程

```c++
/*
	监听 socket 配置：构造函数中已调用 socket_.SetReuseAddr()、socket_.BindAddress(port)、socket_.Setnonblocking()、socket_.Listen()，完成底层 socket 初始化；
	边缘触发（EPOLLET）：监听 socket 采用边缘触发模式，配合 OnNewConnection 中的循环 accept，确保高并发下不遗漏新连接；
	主线程 EventLoop：监听 socket 的事件仅注册到主线程 EventLoop，避免多线程竞争 accept 系统调用。
*/
void TcpServer::Start() {
  // 1. 初始化监听socket（已在构造函数中完成绑定、非阻塞、监听）
  // 2. 配置监听socket的Channel（acceptchannel_）
  acceptchannel_.SetFd(socket_.fd());  // 绑定监听socket的FD
  acceptchannel_.SetEvents(EPOLLIN | EPOLLET);  // 监听读事件（边缘触发模式）
  acceptchannel_.setReadHandler(std::bind(&TcpServer::OnNewConnection, this));  // 读事件回调：接受新连接
  acceptchannel_.setErrorHandler(std::bind(&TcpServer::OnConnectionError, this));  // 错误回调

  // 3. 将监听Channel注册到主线程EventLoop
  loop_->AddChannelToPoller(&acceptchannel_);

  // 4. 启动IO线程池（创建子线程EventLoop）
  threadpool_.Start();  // EventLoopThreadPool::Start：创建threadnum个子线程，每个运行独立EventLoop
}
```

2、接受客户新连接：当客户端连接到达时，监听 socket 的 `EPOLLIN` 事件触发，主线程 `EventLoop` 调用 `OnNewConnection`，核心逻辑如下：

```c++
/*
    循环 Accept：边缘触发模式下，epoll_wait 仅在新连接到达时触发一次，需循环调用 socket_.Accept 处理所有 pending 连接，避免漏处理；
    轮询连接分发：通过 threadpool_.GetNextLoop() 按顺序选择子线程 EventLoop（如线程池有3个线程，则连接1→loop1，连接2→loop2，连接3→loop3，连接4→loop1...），实现负载均衡；
    智能指针管理：TcpConnection 以 shared_ptr 创建，确保异步操作（如跨线程任务）期间对象不被销毁，生命周期由引用计数管理。
*/
void TcpServer::OnNewConnection() {
  struct sockaddr_in peeraddr;  // 客户端地址
  int connfd = socket_.Accept(peeraddr);  // 调用Socket::Accept获取新连接FD（非阻塞）
  
  // 循环accept，处理所有pending连接（边缘触发需一次性读完）
  while (connfd > 0) {
    // 1. 检查最大连接数限制
    if (conncount_ >= maxconnections_) {
      close(connfd);  // 超过上限，直接关闭新连接
      connfd = socket_.Accept(peeraddr);
      continue;
    }

    // 2. 从线程池获取子线程EventLoop（轮询分发）
    EventLoop* ioLoop = threadpool_.GetNextLoop();  // 调用EventLoopThreadPool::GetNextLoop

    // 3. 创建TcpConnection对象（管理新连接生命周期）
    auto conn = std::make_shared<TcpConnection>(ioLoop, connfd, peeraddr);  // 绑定子线程EventLoop、FD、客户端地址

    // 4. 设置TcpConnection回调（业务层逻辑通过TcpServer传递）
    conn->SetMessageCallBack(messagecallback_);  // 消息回调（如EchoServer::HandleMessage）
    conn->SetCloseCallBack(closecallback_);      // 关闭回调（如EchoServer::HandleClose）
    conn->SetErrorCallBack(errorcallback_);      // 错误回调（如EchoServer::HandleError）
    // 设置连接清理回调（TcpServer::RemoveConnection）
    conn->SetConnectionCleanupCallBack(std::bind(&TcpServer::RemoveConnection, this, std::placeholders::_1));

    // 5. 将TcpConnection注册到子线程EventLoop（监听读写事件）
    conn->AddChannelToLoop();  // 调用TcpConnection::AddChannelToLoop

    // 6. 记录活跃连接（线程安全）
    {
      std::lock_guard<std::mutex> lock(mutex_);
      connections_[connfd] = conn;  // FD -> TcpConnectionPtr映射
      conncount_++;  // 连接计数+1
    }

    // 7. 触发新连接回调（通知业务层）
    if (newconnectioncallback_) {
      newconnectioncallback_(conn);  // 如EchoServer::HandleNewConnection
    }

    // 继续accept下一个连接
    connfd = socket_.Accept(peeraddr);
  }
}
```

### 3、线程模型

**主线程**：

核心职责：

- 运行主线程EventLoop，通过Poller监听监听socket的EPOLLIN事件
- 处理新连接建立：调用[TcpServer::OnNewConnection](https://vscode-remote+wsl-002bubuntu-002d22-002e04.vscode-resource.vscode-cdn.net/home/dh/.vscode-server/extensions/marscode.marscode-extension-1.2.38/)接受客户端连接
- 线程调度：通过EventLoopThreadPool将新连接分发到子线程

核心逻辑：

```c++
void EventLoop::loop() {
    quit_ = false;
    while (!quit_) {
        poller.poll(activechannels_);  // 阻塞等待事件
        for (auto &channel : activechannels_) {
            channel->HandleEvent();   // 处理就绪事件
        }
        ExecuteTask();                 // 执行任务队列
    }
}
```

**EventLoopThreadPool线程池**

架构设计：

- 包含N个EventLoopThread线程（N由构造函数参数指定）
- 每个线程独立运行一个EventLoop实例
- 主线程通过GetNextLoop()轮询选择子线程EventLoop

线程内部实现：

```c++
void EventLoopThread::ThreadFunc() {
  EventLoop loop;  // 每个线程创建独立的EventLoop
  loop_ = &loop;
  loop_->loop();   // 启动子线程事件循环
}
```

连接分发机制（负载均衡）

通过轮询算法将新连接均匀分配到子线程，实现负载均衡。

```c++
EventLoop *EventLoopThreadPool::GetNextLoop() {
  if (threads_.empty()) return mainloop_;
  EventLoop *nextLoop = threads_[index_]->GetLoop();
  index_ = (index_ + 1) % threadnum_;  // 轮询索引递增
  return nextLoop;
}
/*
	分发流程：
		新连接到达：主线程EventLoop检测到监听socket可读事件
        接受连接：调用TcpServer::OnNewConnection获取connfd
        选择子线程：调用GetNextLoop()轮询获取子线程EventLoop
        创建连接对象：
*/
EventLoop* loop = threadpool_.GetNextLoop();
auto conn = std::make_shared<TcpConnection>(loop, connfd, peeraddr);
conn->AddChannelToLoop();  // 注册到子线程EventLoop
```

**线程协作模型：**

1、线程职责划分：

- **主线程**：仅处理监听socket事件和新连接分发
- **子线程**：处理已建立连接的I/O事件（读写）和业务逻辑
- **优势**：避免线程间频繁切换和锁竞争

2、跨线程通信：通过EventLoop的任务队列实现线程间安全通信：

```c++
void AddTask(Functor functor) {
  { std::lock_guard<std::mutex> lock(mutex_);
    functors_.push_back(std::move(functor)); }
  wakeup();  // 唤醒事件循环
}
/*
	当需要在其他线程执行任务时（如跨线程发送数据），通过AddTask添加到目标线程的任务队列，由目标线程自行执行
*/
```

3、线程隔离：每个TcpConnection绑定到固定的子线程EventLoop

- 所有I/O操作在同一线程执行
- 避免多线程操作同一socket的竞态条件
- 简化同步机制，无需复杂锁保护

