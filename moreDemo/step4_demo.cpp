/************************************************************************
* 协程代码测试：测试执行的流程
* 没有实际意义
*
* 返回值可以是各种类型，使用模板实现
*
**************************************************************************/

#include <chrono>
#include <coroutine>
#include "debug.hpp"


/*
* 
* 
*/
struct RepeatAwaiter {
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
*
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

    void await_resume() const noexcept {}
};

/*
*promise_type的定义，这里使用了模板类型参数，表示可以适配各种类型的返回值
*
*/
template <class T>
struct Promise {
    auto initial_suspend() noexcept {
        return std::suspend_always();
    }

    auto final_suspend() noexcept {
        return PreviousAwaiter(mPrevious);
    }

    void unhandled_exception() noexcept {
        mException = std::current_exception();   //记录下异常
    }

    //返回值处理
    auto yield_value(T ret) noexcept {
        //std::construct_at(&mResult, std::move(ret));    //和placement new等价
        new (&mResult) T(std::move(ret));   //placement new
        return std::suspend_always();
    }

    //返回值处理
    void return_value(T ret) noexcept {
        new (&mResult) T(std::move(ret));
    }

    T result() {
        if (mException) [[unlikely]] {
            std::rethrow_exception(mException);
            }

        T ret = std::move(mResult);   //移动之后就可以析构了
        //std::destory_at(&mResult);  //和下面的主动调用析构函数等价
        mResult.~T();   //主动调用析构函数

        return ret;
    }

    std::coroutine_handle<Promise> get_return_object() {
        return std::coroutine_handle<Promise>::from_promise(*this);
    }

    std::coroutine_handle<> mPrevious{};
    std::exception_ptr mException{};        //可能的异常

    //返回值，这里是个技巧，包一层union之后那么mResult就不会再Promise构造的时候进行构造了（就是不会初始化了），那么就可以配合placement new进行使用了
    //所以union可以用来防止一个对象被构造
    union {
        T mResult;
    };

    Promise() noexcept {}
    Promise(Promise&&) = delete;
    ~Promise() {}
};


/*
*promise_type的特化版本
*
*/
template <>
struct Promise<void> {
    auto initial_suspend() noexcept {
        return std::suspend_always();
    }

    auto final_suspend() noexcept {
        return PreviousAwaiter(mPrevious);
    }

    void unhandled_exception() noexcept {
        mException = std::current_exception();
    }

    void return_void() noexcept {
    }

    void result() {
        if (mException) [[unlikely]] {
            std::rethrow_exception(mException);
            }
    }

    std::coroutine_handle<Promise> get_return_object() {
        return std::coroutine_handle<Promise>::from_promise(*this);
    }

    std::coroutine_handle<> mPrevious{};
    std::exception_ptr mException{};

    Promise() = default;
    Promise(Promise&&) = delete;
    ~Promise() = default;
};

template <class T>
struct Task {
    using promise_type = Promise<T>;

    Task(std::coroutine_handle<promise_type> coroutine) noexcept
        : mCoroutine(coroutine) {}

    Task(Task&&) = delete;

    ~Task() {
        mCoroutine.destroy();
    }

    /*
    * Awaiter
    * 
    */
    struct Awaiter {
        bool await_ready() const noexcept { return false; }

        std::coroutine_handle<promise_type> await_suspend(std::coroutine_handle<> coroutine) const noexcept {
            mCoroutine.promise().mPrevious = coroutine;
            return mCoroutine;
        }

        T await_resume() const {
            return mCoroutine.promise().result();
        }

        std::coroutine_handle<promise_type> mCoroutine;
    };

    /*
    * Awaitable
    * 
    */
    auto operator co_await() const noexcept {
        return Awaiter(mCoroutine);
    }

    std::coroutine_handle<promise_type> mCoroutine;   //协程句柄
};

Task<std::string> baby() {
    debug(), "baby";
    co_return "aaa\n";   //先执行promise_type.return_value(将返回值存储到promise_type中)，再执行promise_type.final_suspend(内部有co_await操作)
}

Task<double> world() {
    debug(), "world";
    co_return 3.14;
}

Task<int> hello() {
    auto ret = co_await baby();    //co_await的返回值是Awaiter对象的await_resume()的返回值
    debug(), ret;
    int i = (int)co_await world();
    debug(), "hello得到world结果为", i;
    co_return i + 1;
}

int main() {
    debug(), "main即将调用hello";
    auto t = hello();
    debug(), "main调用完了hello"; // 其实只创建了task对象，并没有真正开始执行
    while (!t.mCoroutine.done()) {
        t.mCoroutine.resume();
        debug(), "main得到hello结果为",
            t.mCoroutine.promise().result();
    }
    return 0;
}