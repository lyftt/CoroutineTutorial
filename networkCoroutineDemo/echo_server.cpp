#include "io_context.h"
#include "awaiters.h"

/*
* co_await的作用：形成嵌套协程的调用链
* coroutine_handle<promise_type>.resume()的作用：唤醒协程的执行，之后会回到这个调用点继续向下执行
*
* |accept()| <----> |Socket::accept()| <----> io_context
*
* |echo_socket()| <----> |inside_loop()| <----> io_context
*
*/

task<bool> inside_loop(Socket& socket) {
    char buffer[1024] = {0};
    ssize_t recv_len = co_await socket.recv(buffer, sizeof(buffer));
    ssize_t send_len = 0;
    while(send_len < recv_len) {
        ssize_t res = co_await socket.send(buffer + send_len, recv_len - send_len);
        if(res <= 0) {
            co_return false;
        }
        send_len += res;
    }

    std::cout<<"Done send "<<send_len<<"\n";
    if(recv_len <= 0) {
        co_return false;
    }
    printf("%s\n", buffer);
    co_return true;
}

task<> echo_socket(std::shared_ptr<Socket> socket) {
    for(;;) {
        std::cout<<"BEGIN\n";
        bool b = co_await inside_loop(*socket);
        if(!b) break;
        std::cout<<"END\n";
    }
}

task<> accept(Socket& listen) {
    for(;;) {
        auto socket = co_await listen.accept();
        auto t = echo_socket(socket);
        t.resume();    //这里用的不是co_await，所以不会和echo_socket协程有调用链关系
    }
}

int main() {
    //创建上下文
    IoContext io_context;

    Socket listen{"10009", io_context};

    //启动协程
    auto t = accept(listen);
    t.resume();       //这里用的不是co_await，所以不会和echo_socket协程有调用链关系，当co_await listen.accept();挂起之后就会回到这里继续往下执行

    io_context.run(); // 启动事件循环
}
