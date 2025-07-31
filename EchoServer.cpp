#include "EchoServer.h"
#include <iostream>
#include <functional>
EchoServer::EchoServer(EventLoop* loop, const uint16_t port, const int threadnum)
    : server_(loop, port, threadnum) {
    server_.SetNewConnectionCallback(std::bind(&EchoServer::HandleNewConnection, this, std::placeholders::_1));
    server_.SetMessageCallback(std::bind(&EchoServer::HandleMessage, this, std::placeholders::_1, std::placeholders::_2));
    server_.SetSendCompleteCallback(std::bind(&EchoServer::HandleSendComplete, this, std::placeholders::_1));
    server_.SetCloseCallback(std::bind(&EchoServer::HandleClose, this, std::placeholders::_1));
    server_.SetErrorCallback(std::bind(&EchoServer::HandleError, this, std::placeholders::_1));
}
EchoServer::~EchoServer() {
    // 这里可以添加清理资源的代码
}
void EchoServer::Start() {
    server_.Start();
}
void EchoServer::HandleNewConnection(const TcpConnectionPtr& conn) {
    std::cout << "New connection established." << std::endl;
    // 可以在这里进行连接初始化操作
}
void EchoServer::HandleMessage(const TcpConnectionPtr& conn, std::string& message) {
    std::cout << "Received message: " << message << std::endl;
    // 可以在这里进行消息处理
    std::string msg;
    msg.swap(message);
    msg.insert(0, "reply Echo: ");
    conn->Send(msg); // 发送回显消息
}
void EchoServer::HandleSendComplete(const TcpConnectionPtr& conn) {
    std::cout << "Message sent successfully." << std::endl;
    // 可以在这里进行发送完成后的处理
}
void EchoServer::HandleClose(const TcpConnectionPtr& conn) {
    std::cout << "Connection closed." << std::endl;
    // 可以在这里进行连接关闭后的处理
}
void EchoServer::HandleError(const TcpConnectionPtr& conn) {
    std::cerr << "Connection error occurred." << std::endl;
    // 可以在这里进行连接错误处理
}