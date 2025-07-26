/************************************************************************
* 协程代码测试：协程实现定时器
* 
* （1）执行关键流程：hello协程通过另一个额外的sleep协程将自己挂起，之后sleep协程调用SleepAwaier将sleep协程挂起，挂起的时候将sleep协程加入到定时器循环中；
* （2）定时器循环在时间超时后将会恢复sleep协程的执行，sleep协程直接co_return，然后在final_suspend中调用RepeatAwaiter，恢复sleep协程的前一个协程的执行（就是hello协程）；
* （3）通过上述的方式起到了定时的作用；
*
* 睡眠其实包括2个函数：
* （1）sleep_until；
* （2）sleep_for；
* 
**************************************************************************/

#include <chrono>
#include <coroutine>
#include <deque>
#include <queue>
#include <thread>
#include "debug.hpp"

using namespace std::chrono_literals;

/*
* Awaiter
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
* Awaiter
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
* promise_type类型 
* 
*/
template <class T>
struct Promise {
    auto initial_suspend() noexcept {
        return std::suspend_always();
    }

    //协程结束的时候恢复前一个协程的执行
    auto final_suspend() noexcept {
        return PreviousAwaiter(mPrevious);
    }

    void unhandled_exception() noexcept {
        mException = std::current_exception();
    }

    auto yield_value(T ret) noexcept {
        new (&mResult) T(std::move(ret));
        return std::suspend_always();
    }

    void return_value(T ret) noexcept {
        new (&mResult) T(std::move(ret));
    }

    T result() {
        if (mException) [[unlikely]] {
            std::rethrow_exception(mException);
            }
        T ret = std::move(mResult);
        mResult.~T();
        return ret;
    }

    std::coroutine_handle<Promise> get_return_object() {
        return std::coroutine_handle<Promise>::from_promise(*this);
    }

    std::coroutine_handle<> mPrevious{};
    std::exception_ptr mException{};
    union {
        T mResult;
    };

    Promise() noexcept {}
    Promise(Promise&&) = delete;
    ~Promise() {}
};

/*
* promise_type的特化版本
* 
*/
template <>
struct Promise<void> {
    auto initial_suspend() noexcept {
        return std::suspend_always();
    }

    //协程结束的时候恢复上一个协程的执行
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

/*
* 协程返回值类型
* 
*/
template <class T = void>
struct Task {
    using promise_type = Promise<T>;

    Task(std::coroutine_handle<promise_type> coroutine) noexcept
        : mCoroutine(coroutine) {}

    Task(Task&&) = delete;

    ~Task() {
        mCoroutine.destroy();
    }

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

    auto operator co_await() const noexcept {
        return Awaiter(mCoroutine);
    }

    //为了Loop.AddTask()函数，Task需要一个类型转换运算符函数
    //为了隐式转换
    operator std::coroutine_handle<>() const noexcept {
        return mCoroutine;
    }

    std::coroutine_handle<promise_type> mCoroutine;
};

/*
* 定时器循环
* 
*/
struct Loop {
    //就绪协程队列，每个元素都是协程句柄，可以立即执行的协程
    //deque的好处：首尾都能快速插入
    std::deque<std::coroutine_handle<>> mReadyQueue;

    //定时器项
    struct TimerEntry {
        std::chrono::system_clock::time_point expireTime;   //过期时间
        std::coroutine_handle<> coroutine;                  //过期后要恢复的时间

        //自定义比较函数，辅助std::priority_queue称为小顶堆
        bool operator<(TimerEntry const& that) const noexcept {
            return expireTime > that.expireTime;
        }
    };

    //小顶堆，默认是大顶堆，但是TimerEntry内部可以自定义比较函数反转成小顶堆
    std::priority_queue<TimerEntry> mTimerHeap;

    //添加就绪的协程
    void addTask(std::coroutine_handle<> coroutine) {
        mReadyQueue.push_front(coroutine);
    }

    //添加定时器
    void addTimer(std::chrono::system_clock::time_point expireTime, std::coroutine_handle<> coroutine) {
        mTimerHeap.push({ expireTime, coroutine });
    }

    //定时器循环
    void runAll() {
        while (!mTimerHeap.empty() || !mReadyQueue.empty()) {

            // 将就绪协程取出恢复执行
            while (!mReadyQueue.empty()) {
                auto coroutine = mReadyQueue.front();
                mReadyQueue.pop_front();
                coroutine.resume();
            }

            //
            if (!mTimerHeap.empty()) {
                auto nowTime = std::chrono::system_clock::now();
                auto timer = std::move(mTimerHeap.top());
                if (timer.expireTime <= nowTime) {
                    mTimerHeap.pop();
                    timer.coroutine.resume();
                }
                else {
                    std::this_thread::sleep_until(timer.expireTime); //最小的没过期则直接睡眠
                }
            }
        }
    }

    //这个技巧可以让编译删除5个特种成员函数（除了默认构造函数）
    //如果这里删移动构造函数，那么会把默认构造函数也删除 
    Loop& operator=(Loop&&) = delete; 
};

/*
* 单例模式获取定时器循环Loop
* 
*/
Loop& getLoop() {
    static Loop loop;   //单例技巧，C++11能保证线程安全
    return loop;
}


/*
* 给定时器用的Awaiter，该Awaiter将coroutine加入到定时器循环中，定时器循环将在超时时间之后恢复协程的执行
* 
*/
struct SleepAwaiter {
    bool await_ready() const noexcept {
        return false;
    }

    //挂起协程coroutine的时候，将被挂起的协程加入到定时器循环中
    void await_suspend(std::coroutine_handle<> coroutine) const {
        getLoop().addTimer(mExpireTime, coroutine);
    }

    void await_resume() const noexcept {
    }

    std::chrono::system_clock::time_point mExpireTime;   //超时时间点
};

/*
* sleep协程
* 
*/
Task<void> sleep_until(std::chrono::system_clock::time_point expireTime) {
    co_await SleepAwaiter(expireTime);
    co_return;
}


/*
* sleep协程
* 
*/
Task<void> sleep_for(std::chrono::system_clock::duration duration) {
    co_await SleepAwaiter(std::chrono::system_clock::now() + duration);
    co_return;
}

Task<int> hello1() {
    debug(), "hello1开始睡1秒";
    co_await sleep_for(1s); // 1s 等价于 std::chrono::seconds(1)，operator""s()这个函数，在命名空间std::chrono_literals中
    debug(), "hello1睡醒了";
    co_return 1;
}

Task<int> hello2() {
    debug(), "hello2开始睡2秒";
    co_await sleep_for(2s); // 2s 等价于 std::chrono::seconds(2)
    debug(), "hello2睡醒了";
    co_return 2;
}

int main() {
    auto t1 = hello1();
    auto t2 = hello2();
    getLoop().addTask(t1);
    getLoop().addTask(t2);
    getLoop().runAll();
    debug(), "主函数中得到hello1结果:", t1.mCoroutine.promise().result();
    debug(), "主函数中得到hello2结果:", t2.mCoroutine.promise().result();
    return 0;
}