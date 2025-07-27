/*
* io_context.h中函数的实现，基于Linux的epoll_wait实现
*
*/

#include <stdexcept>
#include <sys/epoll.h>
#include <cstring>
#include "io_context.h"
#include "socket.h"

IoContext::IoContext(): fd_(epoll_create1(0)) {
    if(fd_ == -1) {
        throw std::runtime_error{"epoll_create1"};
    }
}

void IoContext::run() {
    struct epoll_event ev, events[max_events];
    for(;;) {
        int nfds = epoll_wait(fd_, events, max_events, -1);   //等待事件就绪：可读、可写
        if(nfds == -1) {
            throw std::runtime_error{"epoll_wait"};
        }

        for(int i = 0; i < nfds; ++i) {
            auto socket = static_cast<Socket*>(events[i].data.ptr);
            if(events[i].events & EPOLLIN) {
                socket->ResumeRecv();    //这里是最核心的代码，以往的非协程模式下，这里应该调用用户的回调函数，而协程模式下则是恢复读协程的运行
            }
            if(events[i].events & EPOLLOUT) {
                socket->ResumeSend();    //这里是最核心的代码，恢复写协程的运行
            }
        }
    }
}

void IoContext::Attach(Socket* socket) {
    struct epoll_event ev;
    auto io_state = EPOLLIN | EPOLLET;
    ev.events = io_state;
    ev.data.ptr = socket;
    if(epoll_ctl(fd_, EPOLL_CTL_ADD, socket->fd_, &ev) == -1) {
        throw std::runtime_error{"epoll_ctl: attach"};
    }
    socket->io_state_ = io_state;
}

// 定义一个宏，消除重复的代码
#define UpdateState(new_state) \
    if(socket->io_state_ != new_state) { \
        struct epoll_event ev = {}; \
        ev.events = new_state; \
        ev.data.ptr = socket; \
        if(epoll_ctl(fd_, EPOLL_CTL_MOD, socket->fd_, &ev) == -1) { \
            throw std::runtime_error{"epoll_ctl: mod"}; \
        } \
        socket->io_state_ = new_state; \
    } \

void IoContext::WatchRead(Socket* socket) {
    auto new_state = socket->io_state_ | EPOLLIN;
    UpdateState(new_state);
}

void IoContext::UnwatchRead(Socket* socket) {
    auto new_state = socket->io_state_ & ~EPOLLIN;
    UpdateState(new_state);
}

void IoContext::WatchWrite(Socket* socket) {
    auto new_state = socket->io_state_ | EPOLLOUT;
    UpdateState(new_state);
}

void IoContext::UnwatchWrite(Socket* socket) {
    auto new_state = socket->io_state_ & ~EPOLLOUT;
    UpdateState(new_state);
}

void IoContext::Detach(Socket* socket) {
    if(epoll_ctl(fd_, EPOLL_CTL_DEL, socket->fd_, nullptr) == -1) {
        perror("epoll ctl: del");
        exit(EXIT_FAILURE);
    }
}
