#include <signal.h>
#include "EventLoop.h"
#include "EchoServer.h"
EventLoop* loop;
static void sighandler1(int signo) {
    exit(0);
}
static void sighandler2( int sig_no )   
{   
    loop->quit(); // 退出事件循环
}
int main(int argc,char *argv[]){
  signal(SIGUSR1, sighandler1);
  signal(SIGUSR2, sighandler2);
  signal(SIGINT, sighandler2);
  signal(SIGPIPE, SIG_IGN);  //SIG_IGN,系统函数，忽略信号的处理程序,客户端发送RST包后，服务器还调用write会触发
  int port=80;
  int iothreadnum=4;
  if(argc==3)  //如果有参数，端口号和IO线程数
  {
    port=atoi(argv[1]);
    iothreadnum=atoi(argv[2]);
  
  }
  EventLoop loop1;
  loop = &loop1; // 设置全局事件循环
  EchoServer server(&loop1, port, iothreadnum);
  server.Start();
  try
  {
    loop1.loop(); // 启动事件循环
  }
  catch(std::bad_alloc& ba)
  {
    std::cerr <<"bad_alloc caught in ThreadPool::ThreadFunc task: " << ba.what() << '\n';
  }
}