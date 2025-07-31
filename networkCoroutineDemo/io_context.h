#pragma once
/*
* 上下文，主要是epoll的封装，注册/取消监听事件
*
*
*/

#include <set>

class Socket;
class Send;
class Recv;
class Accept;

class IoContext {
public:
    IoContext();

    void run();
private:
    constexpr static std::size_t max_events = 10;
    const int fd_;
    friend Socket;
    friend Send;
    friend Recv;
    friend Accept;
    void Attach(Socket* socket);
    void WatchRead(Socket* socket);
    void UnwatchRead(Socket* socket);
    void WatchWrite(Socket* socket);
    void UnwatchWrite(Socket* socket);
    void Detach(Socket* socket);
};
