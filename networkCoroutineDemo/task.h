#pragma once

/*
* 这里定义了task协程的行为
* （1）task协程创建之后立即挂起；
* （2）task协程在结束之前，会将前一个协程恢复起来执行；
* （3）task协程也是一个waiter，这说明支持嵌套协程，即一个协程A支持等待另一个协程B去执行别的，协程B执行完之后再恢复A的执行；
* （4）task协程作为waiter的行为：
*      - 在即将要执行的协程B的promise中记录下正在等待的协程A，然后去执行协程B；
*      - 协程B执行完之后，waiter返回协程B的执行结果给协程A；
*
*
*    |  task A  |
                           co_await
*               -------------------------> |                task B                 | 
*                                          |            initial_suspend()          | 
*                                          |              await_suspend()          | -----> promise.continuation_ = waiter   记录下正在等待的协程
*                                          |               co_return xx            | 
*                                          |              return_value(xx)         | -----> promise.result = xx
*                                          |              final_suspend()          | -----> return promise.continuation_     恢复前一个正在等待的协程
*                <------------------------ |               await_resume()          | -----> return promise.result            结果返回给了co_await
*    |  task A  |                                      
*                                        
*
*/

#include <coroutine>
#include <iostream>

using std::coroutine_handle;
using std::suspend_always;
using std::suspend_never;

template<typename T> struct task;

namespace detail {

template<typename T>
struct promise_type_base {
    coroutine_handle<> continuation_ = std::noop_coroutine(); // who waits on this coroutine
    
    //返回协成的返回值
    task<T> get_return_object();

    //初始化协程后一定挂起
    suspend_always initial_suspend() { return {}; }

    // 当前协程运行完毕，在这里回到父协程，即continuation_
    struct final_awaiter {
        bool await_ready() noexcept { return false; }
        void await_resume() noexcept {}

        template<typename promise_type>
        coroutine_handle<> await_suspend(coroutine_handle<promise_type> coro) noexcept {
            return coro.promise().continuation_;
        }
    };

    //本协程结束之前，先把前面记录下的协程恢复起来
    auto final_suspend() noexcept {
        return final_awaiter{};
    }

    void unhandled_exception() { //TODO: 
        std::exit(-1);
    }
}; // struct promise_type_base

template<typename T>
struct promise_type final: promise_type_base<T> {
    T result;
    void return_value(T value) { result = value; }
    T await_resule() { return result; }
    task<T> get_return_object();
};

template<>
struct promise_type<void> final: promise_type_base<void> {
    void return_void() {}
    void await_resume() {}
    task<void> get_return_object();
};

} // namespace detail


/*
* 协程的返回值,类型模版参数T是真实的返回值
* 注意：这里将协程同时实现成了waiter
*/
template<typename T = void>
struct task {
    using promise_type = detail::promise_type<T>;

    task():handle_(nullptr){}
    task(coroutine_handle<promise_type> handle):handle_(handle){}

    bool await_ready() { return false; }

    //本协程作为waiter恢复的时候，把本协成的结果返回
    T await_resume() {
        return handle_.promise().result;
    }

    // 这里是对当前task对象本身调用co_await，说明一定是嵌套的协程，
    // 当前所在的协程是父协程，即await_suspend参数所代表的协程，
    // 被co_await的task对象所在的协程为子协程，即当前task.handle_，为子协程。
    coroutine_handle<> await_suspend(coroutine_handle<> waiter) {  //waiter是调用co_await的协程
        handle_.promise().continuation_ = waiter;   //当前协程记录下之前调用co_await的这个协程waiter,以便后面唤醒

        // waiter所在的协程，即当前协程挂起了，让子协程，即handle_所表示的协程恢复。子协程结束完以后又回到waiter。
        return handle_;
    }

    void resume() {
        handle_.resume();    //恢复本协程执行，因为promise_type中suspend_always initial_suspend() { return {}; }，本协程初始化之后一定会挂起
    }

    coroutine_handle<promise_type> handle_;   //本协程句柄
};

namespace detail {
template<typename T>
inline task<T> promise_type<T>::get_return_object() {
    return task<T>{ coroutine_handle<promise_type<T>>::from_promise(*this)};
}

inline task<void> promise_type<void>::get_return_object() {
    return task<void>{ coroutine_handle<promise_type<void>>::from_promise(*this)};
}
}
