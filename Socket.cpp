#include "Socket.h"
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdlib.h>
#include <cstring>
Socket::Socket() {
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ ==-1) {
        perror("socket error");
        exit(EXIT_FAILURE);
    }
    std::cout << "Socket created with fd: " << fd_ << std::endl;
}
Socket::~Socket() {
    close(fd_);
    std::cout << "Socket with fd: " << fd_ << " destroyed." << std::endl;
}
void Socket::setSocketOption() {
    ;
}
void Socket::SetReuseAddr() {
    int on=1;
    setsockopt(fd_,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on));
}
void Socket::Setnonblocking() {
    int flags = fcntl(fd_, F_GETFL);
    if (flags <0) {
        perror("fcntl get flags error");
        exit(EXIT_FAILURE);
    }
    if (fcntl(fd_, F_SETFL, flags | O_NONBLOCK) <0) {
        perror("fcntl set non-blocking error");
        exit(EXIT_FAILURE);
    }
}
bool Socket::BindAddress(int serverport) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(serverport);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd_, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
      close(fd_);
      perror("bind error");
      exit(-1);
    }
    std::cout << "Socket bound to port: " << serverport << std::endl;
    return true;
}
bool Socket::Listen() {
    if (listen(fd_, 5) <0) {
        perror("listen error");
        close(fd_);
        exit(EXIT_FAILURE);
    }
    std::cout << "Socket is now listening." << std::endl;
    return true;
}
int Socket::Accept(struct sockaddr_in& peeraddr) {
    socklen_t addrlen = sizeof(peeraddr);
    int connfd = accept(fd_, (struct sockaddr*)&peeraddr, &addrlen);
    if (connfd < 0) {
      if(errno==EAGAIN)
        return 0;
      return connfd; // Return -1 on error, 0 if no connection is available
    }
    std::cout << "Accepted connection on fd: " << connfd << std::endl;
    return connfd;
}
bool Socket::Close() {
    close(fd_);
    std::cout << "Socket closed." << std::endl;
    return true;
}