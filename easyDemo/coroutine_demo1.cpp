/****************************************************************************
* 
* C++20 协程1
* coroutine初试
* 没有任何其他依赖库，不依赖boost，只需要编译器支持C++20
* 
*
*
*****************************************************************************/


#include <coroutine>
#include <iostream>
#include <stdexcept>
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

    //构造函数
    coro_ret(handle_type h) : coro_handle_(h)
    {
    }

    //不允许拷贝构造
    coro_ret(const coro_ret&) = delete;
    //拷贝构造
    coro_ret(coro_ret&& s) : coro_handle_(s.coro_)
    {
        s.coro_handle_ = nullptr;
    }

    //析构函数
    ~coro_ret()
    {
        //!自行销毁
        if (coro_handle_)
            coro_handle_.destroy();   //协程句柄的销毁
    }

    //不允许拷贝赋值
    coro_ret& operator=(const coro_ret&) = delete;

    //移动构造
    coro_ret& operator=(coro_ret&& s)
    {
        coro_handle_ = s.coro_handle_;
        s.coro_handle_ = nullptr;
        return *this;
    }

    //恢复协程，返回是否结束
    bool move_next()
    {
        coro_handle_.resume();       //调用协程句柄的恢复函数
        return coro_handle_.done();  //判断协程是否结束
    }

    //通过promise获取数据获取协程内部的值
    T get()
    {
        return coro_handle_.promise().return_data_;
    }

    //promise_type就是承诺对象，承诺对象用于协程内外交流
    struct promise_type
    {
        //默认构造析构函数
        promise_type() = default;
        ~promise_type() = default;

        //生成协程函数的返回值(就是coro_ret)
        //调用协程函数的时候就会调用这个函数
        auto get_return_object()
        {
            return coro_ret<T> { handle_type::from_promise(*this) };   //用promise创建协程句柄
        }

        //注意这个函数,返回的就是awaiter，用来决定协程初始化之后的行为
        //如果返回std::suspend_never{}，就不挂起，
        //返回std::suspend_always{} 挂起
        //当然你也可以返回其他awaiter
        auto initial_suspend()
        {
            //return std::suspend_never{};
            return std::suspend_always{};  //初始化之后挂起
        }

        //co_return 后这个函数会被调用，保存co_return的结果
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
            return std::suspend_always{};  //挂起
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


/*****************************************************
* 这就是一个协程函数
* 
* 
*****************************************************/
coro_ret<int> coroutine_7in7out()
{
    //进入协程看initial_suspend，返回std::suspend_always{};会有一次挂起
    std::cout << "Coroutine co_await std::suspend_never" << std::endl;

    //co_await std::suspend_never{} 不会挂起
    co_await std::suspend_never{};

    std::cout << "Coroutine co_await std::suspend_always" << std::endl;
    co_await std::suspend_always{};

    std::cout << "Coroutine stage 1 ,co_yield" << std::endl;
    co_yield 101;

    std::cout << "Coroutine stage 2 ,co_yield" << std::endl;
    co_yield 202;

    std::cout << "Coroutine stage 3 ,co_yield" << std::endl;
    co_yield 303;

    std::cout << "Coroutine stage end, co_return" << std::endl;
    co_return 808;
}

int main(int argc, char* argv[])
{
    bool done = false;
    std::cout << "Start coroutine_7in7out ()\n";

    //调用协程,c_r就是协程的返回值，是coro_ret类对象
    //协程挂起之后就会从这里返回
    auto c_r = coroutine_7in7out();

    //第一次停止因为initial_suspend 返回的是suspend_always，所以协程创建之后会直接挂起
    //此时没有进入Stage 1
    std::cout << "Coroutine " << (done ? "is done " : "isn't done ") << "ret =" << c_r.get() << std::endl;
    done = c_r.move_next();

    //此时是，co_await std::suspend_always{}
    std::cout << "Coroutine " << (done ? "is done " : "isn't done ") << "ret =" << c_r.get() << std::endl;
    done = c_r.move_next();

    //此时打印Stage 1
    std::cout << "Coroutine " << (done ? "is done " : "isn't done ") << "ret =" << c_r.get() << std::endl;
    done = c_r.move_next();

    std::cout << "Coroutine " << (done ? "is done " : "isn't done ") << "ret =" << c_r.get() << std::endl;
    done = c_r.move_next();

    std::cout << "Coroutine " << (done ? "is done " : "isn't done ") << "ret =" << c_r.get() << std::endl;
    done = c_r.move_next();

    std::cout << "Coroutine " << (done ? "is done " : "isn't done ") << "ret =" << c_r.get() << std::endl;

    return 0;
}