#ifndef _POLLER_H_
#define _POLLER_H_

#include <vector>
#include <memory>
#include <map>
#include <mutex>
#include <sys/epoll.h>
#include <cstdint>
#include "Channel.h"

class Poller {
public:
    //事件指针数组类型
    typedef std::vector<Channel*> ChannelList;
    int epollfd_; //epoll文件描述符
    std::vector<struct epoll_event> events_; //epoll事件数组用于传递给epollwait接收就绪事件
    std::map<int, Channel*> channels_; //fd到Channel的映射
    std::mutex mutex_; //互斥锁，保护channels_和events_
    Poller();
    ~Poller();
    //等待事件，epoll_wait封装
    void poll(ChannelList &activeChannels);
    void addChannel(Channel *channel);
    void removeChannel(Channel *channel);
    void updateChannel(Channel *channel);
private:
    
};

#endif // !_POLLER_H_