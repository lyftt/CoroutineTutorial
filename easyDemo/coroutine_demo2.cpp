/****************************************************************************
*
* C++20 协程2
* 
* 没有任何其他依赖库，不依赖boost，只需要编译器支持C++20
*
* 尝试自定义awaiter
*
*****************************************************************************/



#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   1
#endif
#ifndef _CRT_SECURE_NO_DEPRECATE
#define _CRT_SECURE_NO_DEPRECATE  1
#endif

#include <coroutine>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <future> 
#include <chrono>
#include <thread>


/**********************************************
* 协程函数的返回值类型coro_ret
*
* coro_ret内部需要定义promise_type类型
*
* T是协程内返回值内部的值类型
*
***********************************************/
template <typename T>
struct coro_ret
{
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    //协程句柄
    handle_type coro_handle_;

    coro_ret(handle_type h) : coro_handle_(h)
    {
    }

    //禁止拷贝
    coro_ret(const coro_ret&) = delete;

    //允许移动
    coro_ret(coro_ret&& s)
        : coro_handle_(s.coro_)
    {
        s.coro_handle_ = nullptr;
    }

    ~coro_ret()
    {
        //自行销毁
        if (coro_handle_)
            coro_handle_.destroy();
    }

    //禁止拷贝赋值
    coro_ret& operator=(const coro_ret&) = delete;

    //允许移动赋值
    coro_ret& operator=(coro_ret&& s)
    {
        coro_handle_ = s.coro_handle_;
        s.coro_handle_ = nullptr;
        return *this;
    }

    //恢复协程，返回是否结束
    bool move_next()
    {
        coro_handle_.resume();
        return coro_handle_.done();
    }

    //通过promise获取数据，返回值
    T get()
    {
        return coro_handle_.promise().return_data_;
    }

    //promise_type就是承诺对象，承诺对象用于协程内外交流
    struct promise_type
    {
        promise_type() = default;
        ~promise_type() = default;

        //生成协程返回值
        auto get_return_object()
        {
            return coro_ret<T>{handle_type::from_promise(*this)};
        }

        //注意这个函数,返回的就是awaiter
        //如果返回std::suspend_never{}，就不挂起，
        //返回std::suspend_always{} 挂起
        //当然你也可以返回其他awaiter
        auto initial_suspend()
        {
            return std::suspend_never{};    //协程初始化之后不挂起
            //return std::suspend_always{};
        }

        //co_return 后这个函数会被调用
        void return_value(T v)
        {
            return_data_ = v;
            return;
        }

        //co_yield之后会调用这个函数，保存co_yield的结果
        auto yield_value(T v)
        {
            std::cout << "yield_value invoked." << std::endl;
            return_data_ = v;
            return std::suspend_always{};
        }

        //在协程最后退出后调用的接口。
        //若 final_suspend 返回 std::suspend_always 则需要用户自行调用
        //handle.destroy() 进行销毁，但注意final_suspend被调用时协程已经结束
        //返回std::suspend_always并不会挂起协程（实测 VSC++ 2022）
        auto final_suspend() noexcept
        {
            std::cout << "final_suspend invoked." << std::endl;
            return std::suspend_always{};
        }

        //
        void unhandled_exception()
        {
            std::exit(1);
        }

        //返回值
        T return_data_;
    };
};

/***********************************************************
* 读文件函数
* 
*
************************************************************/
int read_file(const char* filename, char* buffer, size_t buf_len, size_t* read_len, std::coroutine_handle<> coro_hdl)
{
    int result = 0;
    size_t len = 0;
    *read_len = 0;

    //打开文件
    FILE* fd = ::fopen(filename, "r+");
    if (nullptr == fd)
    {
        result = -1;
        goto READ_FILE_END;
    }

    //读取内容
    len = ::fread(buffer, 1, buf_len, fd);

    ::fclose(fd);

    if (len <= 0)
    {
        result = -1;
        goto READ_FILE_END;
    }

    *read_len = len;
    result = 0;

    //到了最后一步，这儿用goto只是方便写代码。
READ_FILE_END:

    return result;
}

/***********************************************************
* 自定义的awaiter
*
*
************************************************************/
struct await_read_file
{
    await_read_file(const char* filename, char* buffer, size_t buf_len, size_t* read_len)
    {
        filename_ = filename;
        buffer_ = buffer;
        buf_len_ = buf_len;
        read_len_ = read_len;
    };

    ~await_read_file() = default;

    //是否准备好，return false就调用await_suspend
    bool await_ready()
    {
        return false;
    }

    //挂起的操作，发起异步读文件操作，然后等待返回
    //awaiting是协程句柄
    void await_suspend(std::coroutine_handle<> awaiting)
    {
        fur_ = std::async(std::launch::async,
            &read_file,
            filename_,
            buffer_,
            buf_len_,
            read_len_,
            awaiting);

        //result_ = fur_.get();   //这里会等待
        //awaiting.resume();      //这里其实没有异步，而是同步阻塞了
    }

    //协程挂起后恢复时调用的接口
    //同时其返回结果作为co_await的返回值
    int await_resume()
    {
        result_ = fur_.get();   //在协程恢复之后调用get
        return result_;
    }

    //读文件的参数，返回值
    int result_ = -1;
    const char* filename_ = nullptr;
    char* buffer_ = nullptr;
    size_t buf_len_ = 0;
    size_t* read_len_ = nullptr;
    std::future<int> fur_;

    //协程的句柄
    std::coroutine_handle<> awaiting_;
};


/******************************************************
* 协程函数
* 
* filename:要读取的文件名
* buffer:目的buffer
* buf_len:目的buffer长度
* read_len:实际读取到的长度
* 
*******************************************************/
coro_ret<int> coroutine_await(const char* filename, char* buffer, size_t buf_len, size_t* read_len)
{
    //co_await自定义的awaiter
    int ret = co_await await_read_file(filename, buffer, buf_len, read_len);

    std::cout << "await_read_file ret= " << ret << std::endl;

    if (ret == 0)
    {
        std::cout << "await_read_file read_len= " << *read_len << std::endl;
    }

    co_return 0;
}

int main(int argc, char* argv[])
{
    using namespace std::chrono_literals;

    //调用协程
    char buffer[1024];
    size_t read_len = 0;
    std::cout << "Start coroutine_await coroutine\n";

    auto c_r = coroutine_await("D:/test/aio_test_001.txt", buffer, 1024, &read_len);

    std::this_thread::sleep_for(std::chrono::seconds(2));   //延时下等待协程中的读文件操作完成

    //恢复协程
    c_r.move_next();

    std::cout << "End coroutine_await coroutine\n";

    return 0;
}