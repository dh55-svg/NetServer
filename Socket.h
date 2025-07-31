#ifndef _SOCKET_H_
#define _SOCKET_H_
//服务器socket类，封装socket描述符及相关的初始化操作
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
class Socket {
public:
  Socket();
  ~Socket();

  int fd()const{return fd_;}
  void setSocketOption();//socket设置
  void SetReuseAddr();//设置地址复用
  void Setnonblocking();//设置非阻塞
  bool BindAddress(int serverport);//绑定地址
  bool Listen();//监听端口
  int Accept(struct sockaddr_in& peeraddr);//接受连接
  bool Close();//关闭socket
private:
    int fd_;//服务器socket文件描述符
};

#endif // !_SOCKET_H_
