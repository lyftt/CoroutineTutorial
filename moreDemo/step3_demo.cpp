/************************************************************************
* 协程代码测试：测试执行的流程
* 没有实际意义
*
* 增加了co_await的测试，Awaitabel和Awaiter概念
*
* 总结几点：
* 1.co_yield  由promise_type.yield_value()决定是否挂起协程，以及挂起后的操作
*
* 2.co_return 协程函数体结束
*
* 3.co_await 对Awaitable或Awaiter对象生效，await_ready()、await_suspend()决定是否挂起，挂起后的操作，co_await可以有返回值;
*            如果Awaitable或Awaiter本身还是个协程句柄，那么就相当于恢复这个协程句柄对应的协程
**************************************************************************/

#include <chrono>
#include <coroutine>
#include "debug.hpp"

/*
* 只要本协程没有结束，就不会挂起，即一直执行
* 
*/
struct RepeatAwaiter
{
    bool await_ready() const noexcept { return false; }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> coroutine) const noexcept {
        if (coroutine.done())
            return std::noop_coroutine();
        else
            return coroutine;
    }

    void await_resume() const noexcept {}
};

/*
* 挂起本协程，唤醒上一个协程
* 
*/
struct PreviousAwaiter {
    std::coroutine_handle<> mPrevious;

    bool await_ready() const noexcept { return false; }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> coroutine) const noexcept {
        if (mPrevious)
            return mPrevious;
        else
            return std::noop_coroutine();
    }

    void await_resume() const noexcept {
        debug(), "PreviousAwaiter resume";
    }
};

/*
* 协程返回值需要的promise_type
* 
*/
struct Promise {
    auto initial_suspend() {
        return std::suspend_always();
    }

    auto final_suspend() noexcept {
        return PreviousAwaiter(mPrevious);
    }

    //如果协程中抛出异常，那么会被这个接口接收到
    void unhandled_exception() {
        debug(), "unhandled_exception:";
        //std::rethrow_exception(std::current_exception());   //获取当前异常，继续抛出
        //throw;   //继续抛出
        mExceptionPtr = std::current_exception();   //这里将异常保存下来

    }

    auto yield_value(int ret) {
        mRetValue = ret;
        return std::suspend_always();
    }

    void return_value(int ret) {
        mRetValue = ret;
    }

    std::coroutine_handle<Promise> get_return_object() {
        return std::coroutine_handle<Promise>::from_promise(*this);
    }

    int& result()  //返回引用
    {
        if (mExceptionPtr)
        {
            std::rethrow_exception(mExceptionPtr);
        }

        return mRetValue.value();
    }

    std::optional<int> mRetValue{};
    std::coroutine_handle<> mPrevious{ nullptr };
    std::exception_ptr mExceptionPtr{nullptr};
};

/*
* 协程的返回值，里面基本要有几样东西：
* 1.promise_type
* 2.协程句柄
* 
* 注意，协程返回值也可以是Awaitable或Awaiter对象，这样在另一个协程co_await这个协程返回值的时候，就可以同时有2个协程的句柄：
* 1.await_suspend(std::coroutine_handle<> coroutine)中的参数coroutine是调用者协程的句柄
* 2.Awaiter自己又是被调用协程的返回值，里面有被调协程的句柄
* 
*/
struct Task {
    using promise_type = Promise;

    Task(std::coroutine_handle<promise_type> coroutine)
        : mCoroutine(coroutine) {}

    Task(Task&&) = delete;

    ~Task() {
        mCoroutine.destroy();
    }

    struct Awaiter {
        bool await_ready() const { return false; }

        //std::coroutine_handle<xx>实例化的模板类都有operator coroutine_handle<>() 这个类型转换运算符
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> coroutine) const {
            mCoroutine.promise().mPrevious = coroutine;
            return mCoroutine;
        }

        //当挂起在这个Awaiter的协程被恢复的时候，这个接口就会被调用，返回值就是co_await的返回值
        auto await_resume() const { return mCoroutine.promise().result(); }

        std::coroutine_handle<promise_type> mCoroutine;
    };

    auto operator co_await() const {
        return Awaiter(mCoroutine);
    }

    std::coroutine_handle<promise_type> mCoroutine;
};

Task world() {
    debug(), "world";
    throw "error";   //抛出异常，会被promise_type的unhandled_exception接收到，这里抛出异常之后,后面的co_return就不会执行了，直接执行final_suspend
    co_return 41;
}

Task hello() {
    int i = co_await world();   //co_await可以有返回值，返回值就是当前协程被唤醒的时候，之前co_await的Awaiter对象的await_resume()将被调用，这个接口的返回值就是co_await的返回值
    debug(), "hello得到world结果为", i;
    co_return i + 1;
}

int main() {
    debug(), "before generate hello";
    Task t = hello();
    debug(), "after generate hello"; // 其实只创建了task对象，并没有真正开始执行

    while (!t.mCoroutine.done()) {
        t.mCoroutine.resume();
        debug(), "main得到hello结果为", t.mCoroutine.promise().result();
    }
    return 0;
}