#pragma once

/*
* 所有的waiter定义
* 这里的Accept、Send、Recv这几个waiter的实现几乎一样
*
*/

#include <type_traits>
#include <iostream>
#include <memory>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#include "task.h"
#include "socket.h"

/*
* 所有Waiter的行为是类似的
* （1）await_ready()返回false，不可能直接就绪；
* （2）await_suspend()是最核心的部分，判断系统调用是否可以直接返回结果，如果可以则不需要挂起，会返回false，否则就返回true挂起，并在挂起之前把socket的接收协程设置好，便于后续唤醒；
* （3）await_resume()是核心的部分，判断之前是否因为系统调用条件不满足而挂起，如果是则调用系统调用并返回系统调用的结果，之前等待的协程就能从这得到返回结果；
* 
* 
* 
*/


/*
* 系统调用waiter
*
*/
template<typename Syscall, typename ReturnValue>
class AsyncSyscall {
public:
    AsyncSyscall() : suspended_(false) {}

    bool await_ready() const noexcept { return false; }

    //这里的函数参数h代表所在协程的句柄，就是调用co_await的协程序
    bool await_suspend(std::coroutine_handle<> h) noexcept {
        static_assert(std::is_base_of_v<AsyncSyscall, Syscall>);
        handle_ = h;
        value_ = static_cast<Syscall*>(this)->Syscall();

        //这里判断是否需要挂起等待，决定waiter是否会挂起所在协程，例如socket的缓冲区没有数据、发送缓冲区数据满了
        suspended_ = value_ == -1 && (errno == EAGAIN || errno == EWOULDBLOCK);

        if(suspended_) {
            // 设置每个操作的coroutine handle，recv/send在适当的epoll事件发生后才能正常调用
            static_cast<Syscall*>(this)->SetCoroHandle();
        }
        return suspended_;
    }

    //事件就绪之后，waiter的这个函数会调用系统调用，然后把系统调用的结果返回
    ReturnValue await_resume() noexcept {
        std::cout<<"await_resume\n";
        if(suspended_) {
            value_ = static_cast<Syscall*>(this)->Syscall();
        }
        return value_;
    }
protected:
    bool suspended_;   //是否需要挂起协程

    // 当前awaiter所在协程的handle，需要设置给socket的coro_recv_或是coro_send_来读写数据
    // handle_不是在构造函数中设置的，所以在子类的构造函数中也无法获取，必须在await_suspend以后才能设置
    std::coroutine_handle<> handle_;    //记录下被挂起的协程，后面epoll可读可写事件就绪之后需要把协程恢复执行
    ReturnValue value_;                 //waiter的返回值，即co_await的返回值
};

class Socket;

/*
* 等待连接的waiter
*
*/
class Accept : public AsyncSyscall<Accept, int> {
public:
    Accept(Socket* socket) : AsyncSyscall{}, socket_(socket) {
        socket_->io_context_.WatchRead(socket_);
        std::cout<<" socket accept opertion\n";
    }

    ~Accept() {
        socket_->io_context_.UnwatchRead(socket_);
        std::cout<<"~socket accept operation\n";
    }

    int Syscall() {
        struct sockaddr_storage addr;
        socklen_t addr_size = sizeof(addr);
        std::cout<<"accept "<<socket_->fd_<<"\n";
        return ::accept(socket_->fd_, (struct sockaddr*)&addr, &addr_size);
    }

    //将被waiter挂起的协程记录下来到socket对象中,后面epoll会事件就绪后会唤醒这个协程
    void SetCoroHandle() {
        socket_->coro_recv_ = handle_;
    }
private:
    Socket* socket_;
    void* buffer_;
    std::size_t len_;
};

/*
* 发送waiter
*
*/
class Send : public AsyncSyscall<Send, ssize_t> {
public:
    Send(Socket* socket, void* buffer, std::size_t len) : AsyncSyscall(),
        socket_(socket), buffer_(buffer), len_(len) {
        socket_->io_context_.WatchWrite(socket_);
        std::cout<<"socket send operation\n";
    }
    ~Send() {
        socket_->io_context_.UnwatchWrite(socket_);
        std::cout<<"~ socket send operation\n";
    }

    ssize_t Syscall() {
        std::cout<<"send"<<socket_->fd_<<"\n";
        return ::send(socket_->fd_, buffer_, len_, 0);
    }

    void SetCoroHandle() {
        socket_->coro_send_ = handle_;
    }
private:
    Socket* socket_;
    void* buffer_;
    std::size_t len_;
};

/*
* 接受数据waiter
*
*/
class Recv : public AsyncSyscall<Recv, int> {
public:
    Recv(Socket* socket, void* buffer, size_t len): AsyncSyscall(), 
        socket_(socket), buffer_(buffer), len_(len) {
        socket_->io_context_.WatchRead(socket_);
        std::cout<<"socket recv operation\n";
    }

    ~Recv() {
        socket_->io_context_.UnwatchRead(socket_);
        std::cout<<"~socket recv operation\n";
    }

    ssize_t Syscall() {
        std::cout<<"recv fd="<<socket_->fd_<<"\n";
        return ::recv(socket_->fd_, buffer_, len_, 0);
    }
    
    void SetCoroHandle() {
        socket_->coro_recv_ = handle_;
    }
private:
    Socket* socket_;
    void* buffer_;
    std::size_t len_;
};



