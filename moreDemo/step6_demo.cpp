/************************************************************************
* 协程代码测试：协程实现定时器
*
* 协程定时器经典案例:
* whenany:每个定时器的时间都满足
* whenall:任意一个定时器的时间满足
* 
**************************************************************************/

#include <chrono>
#include <coroutine>
#include <deque>
#include <queue>
#include <span>
#include <variant>
#include <thread>
#include "debug.hpp"

/*为了使用1s、2s这样的时间*/
using namespace std::chrono_literals;

template <class T = void> struct NonVoidHelper {
    using Type = T;
};

template <> struct NonVoidHelper<void> {
    using Type = NonVoidHelper;

    explicit NonVoidHelper() = default;
};

/*
* 未初始化的对象，对象内部的数据类型为T，之所以未初始化，是因为使用了union
* 
*/
template <class T> 
struct Uninitialized {
    union {
        T mValue;
    };

    Uninitialized() noexcept {}
    Uninitialized(Uninitialized&&) = delete;
    ~Uninitialized() noexcept {}

    /*
    * 移动
    */
    T moveValue() {
        T ret(std::move(mValue));
        mValue.~T();
        return ret;
    }

    /*
    * 在未初始化的T类型的存储空间上直接原地构造
    * 
    */
    template <class... Ts> void putValue(Ts &&...args) {
        new (std::addressof(mValue)) T(std::forward<Ts>(args)...);
    }
};

/*特化*/
template <> 
struct Uninitialized<void> {
    auto moveValue() {
        return NonVoidHelper<>{};
    }

    void putValue(NonVoidHelper<>) {}
};

/*特化*/
template <class T> 
struct Uninitialized<T const> : Uninitialized<T> {};

template <class T>
struct Uninitialized<T&> : Uninitialized<std::reference_wrapper<T>> {};

template <class T> 
struct Uninitialized<T&&> : Uninitialized<T> {};

/*
* C++20的concept，这个Awaiter就是表示我们之前提到的Awaiter对象，这个对象内部要有await_ready、await_suspend、await_resume三个函数
* 
*/
template <class A>
concept Awaiter = requires(A a, std::coroutine_handle<> h) {
    { a.await_ready() };
    { a.await_suspend(h) };
    { a.await_resume() };
};

/*
* C++20的concept，这个Awaitable就是我们之前提到的Awaitable对象，内部重载有有operator co_await()，这个类型转换运算符函数会返回一个Awaiter对象
* 
*/
template <class A>
concept Awaitable = Awaiter<A> || requires(A a) {
    { a.operator co_await() } -> Awaiter;
};

template <class A> 
struct AwaitableTraits;

template <Awaiter A> 
struct AwaitableTraits<A> {
    using RetType = decltype(std::declval<A>().await_resume());
    using NonVoidRetType = NonVoidHelper<RetType>::Type;
};

template <class A>
    requires(!Awaiter<A>&& Awaitable<A>)
struct AwaitableTraits<A>
    : AwaitableTraits<decltype(std::declval<A>().operator co_await())> {};

/*
* 继续执行的Awaiter
* 
*/
struct RepeatAwaiter {
    bool await_ready() const noexcept {
        return false;
    }

    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<> coroutine) const noexcept {
        if (coroutine.done())   //只要coroutine这个协程没结束就继续执行
            return std::noop_coroutine();
        else
            return coroutine;   //会调用await_resume
    }

    void await_resume() const noexcept {}
};

/*(
* 执行前一个协程的Awaiter，很多协程在切换的时候，会记录下前一个协程，这个Awaiter的作用就是恢复前一个协程的执行，往往用在协程promise_type内部的final_suspend()中(即协程结束之后恢复前一个协程的执行)
* 
*/
struct PreviousAwaiter {
    std::coroutine_handle<> mPrevious;

    bool await_ready() const noexcept {
        return false;
    }

    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<> coroutine) const noexcept {
        if (mPrevious)
            return mPrevious;   //恢复前一个协程
        else
            return std::noop_coroutine();
    }

    void await_resume() const noexcept {}
};

/*
* promise_type
* 
*/
template <class T> struct Promise {
    auto initial_suspend() noexcept {
        return std::suspend_always();
    }

    auto final_suspend() noexcept {
        return PreviousAwaiter(mPrevious);  //协程结束后恢复前一个协程的执行（如果有的话）
    }

    void unhandled_exception() noexcept {
        mException = std::current_exception();
    }

    void return_value(T&& ret) {
        mResult.putValue(std::move(ret));
    }

    void return_value(T const& ret) {
        mResult.putValue(ret);
    }

    T result() {
        if (mException) [[unlikely]] {
            std::rethrow_exception(mException);
            }
        return mResult.moveValue();
    }

    auto get_return_object() {
        return std::coroutine_handle<Promise>::from_promise(*this);
    }

    std::coroutine_handle<> mPrevious{};
    std::exception_ptr mException{};
    Uninitialized<T> mResult;

    Promise& operator=(Promise&&) = delete;
};

/*
* 特化版本
*/
template <> 
struct Promise<void> {
    auto initial_suspend() noexcept {
        return std::suspend_always();
    }

    auto final_suspend() noexcept {
        return PreviousAwaiter(mPrevious);   //恢复前一个协程
    }

    void unhandled_exception() noexcept {
        mException = std::current_exception();
    }

    void return_void() noexcept {}

    void result() {
        if (mException) [[unlikely]] {
            std::rethrow_exception(mException);
            }
    }

    auto get_return_object() {
        return std::coroutine_handle<Promise>::from_promise(*this);
    }

    std::coroutine_handle<> mPrevious{};
    std::exception_ptr mException{};

    Promise& operator=(Promise&&) = delete;
};

/*
* 协程的返回值类型，这个类型内部有协程句柄std::coroutine_handle，以便之后恢复协程的执行
* 
* 这些协程返回值内部使用的promise_type是Promise类型，这个Promise内部定义了Task对应的协程在结束的时候会恢复前一个协程的执行
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

    /*
    * Task同时还是一个Awaitable，实现了operator co_await()
    * 
    */
    struct Awaiter {
        bool await_ready() const noexcept {
            return false;
        }

        std::coroutine_handle<promise_type>
            await_suspend(std::coroutine_handle<> coroutine) const noexcept {
            mCoroutine.promise().mPrevious = coroutine;    //记录前一个协程
            return mCoroutine;   //执行Awaiter所属的这个协程，就是Task对应的这个协程
        }

        T await_resume() const {
            return mCoroutine.promise().result();  //co_await返回的时候调用这个函数，获取Task这个协程的结果（promise_type类对象内部）
        }

        std::coroutine_handle<promise_type> mCoroutine;
    };

    //Task转Awaiter
    auto operator co_await() const noexcept {
        return Awaiter(mCoroutine);   //Task对应的协程作为参数
    }

    operator std::coroutine_handle<>() const noexcept {
        return mCoroutine;
    }

    std::coroutine_handle<promise_type> mCoroutine;
};

/*
* 事件循环
* 
*/
struct Loop {
    std::deque<std::coroutine_handle<>> mReadyQueue;

    struct TimerEntry {
        std::chrono::system_clock::time_point expireTime;
        std::coroutine_handle<> coroutine;

        bool operator<(TimerEntry const& that) const noexcept {
            return expireTime > that.expireTime;
        }
    };

    std::priority_queue<TimerEntry> mTimerHeap;

    void addTask(std::coroutine_handle<> coroutine) {
        mReadyQueue.push_front(coroutine);
    }

    void addTimer(std::chrono::system_clock::time_point expireTime,
        std::coroutine_handle<> coroutine) {
        mTimerHeap.push({ expireTime, coroutine });
    }

    void runAll() {
        //处理就绪队列中的协程和定时器队列中的协程
        while (!mTimerHeap.empty() || !mReadyQueue.empty()) {
            while (!mReadyQueue.empty()) {
                auto coroutine = mReadyQueue.front();
                debug(), "pop";
                mReadyQueue.pop_front();
                coroutine.resume();
            }
            if (!mTimerHeap.empty()) {
                auto nowTime = std::chrono::system_clock::now();
                auto timer = std::move(mTimerHeap.top());
                if (timer.expireTime < nowTime) {
                    mTimerHeap.pop();
                    timer.coroutine.resume();
                }
                else {
                    std::this_thread::sleep_until(timer.expireTime);
                }
            }
        }
    }

    Loop& operator=(Loop&&) = delete;
};

Loop& getLoop() {
    static Loop loop;
    return loop;
}

/*
* 这个Awaiter将暂停的协程加入到事件循环中，以便在时间到达之后，事件循环可以恢复协程的执行
* 
*/
struct SleepAwaiter {
    bool await_ready() const noexcept {
        return false;
    }

    void await_suspend(std::coroutine_handle<> coroutine) const {
        getLoop().addTimer(mExpireTime, coroutine);   //将要挂起的协程添加到事件循环中，包括超时的时间
    }

    void await_resume() const noexcept {}

    std::chrono::system_clock::time_point mExpireTime;
};

/*
* 协程
* 
*/
Task<void> sleep_until(std::chrono::system_clock::time_point expireTime) {
    co_await SleepAwaiter(expireTime);
}

/*
* 协程
* 
*/
Task<void> sleep_for(std::chrono::system_clock::duration duration) {
    co_await SleepAwaiter(std::chrono::system_clock::now() + duration);
}

/*
* Awaiter
* 
*/
struct CurrentCoroutineAwaiter {
    bool await_ready() const noexcept {
        return false;
    }

    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<> coroutine) noexcept {
        mCurrent = coroutine;
        return coroutine;
    }

    auto await_resume() const noexcept {
        return mCurrent;
    }

    std::coroutine_handle<> mCurrent;
};

/*
* promise_type类型，使用这个promise_type的协程会在协程结束之后恢复前一个协程的执行
* 
*/
struct ReturnPreviousPromise {
    auto initial_suspend() noexcept {
        return std::suspend_always();
    }

    auto final_suspend() noexcept {
        return PreviousAwaiter(mPrevious);  //恢复前一个协程的执行
    }

    void unhandled_exception() {
        throw;
    }

    void return_value(std::coroutine_handle<> previous) noexcept {
        mPrevious = previous;
    }

    auto get_return_object() {
        return std::coroutine_handle<ReturnPreviousPromise>::from_promise(
            *this);
    }

    std::coroutine_handle<> mPrevious{};

    ReturnPreviousPromise& operator=(ReturnPreviousPromise&&) = delete;
};

/*
* 协程返回值，其内部使用了ReturnPreviousPromise这个作为promise_type类型
* 
*/
struct ReturnPreviousTask {
    using promise_type = ReturnPreviousPromise;

    ReturnPreviousTask(std::coroutine_handle<promise_type> coroutine) noexcept
        : mCoroutine(coroutine) {}

    ReturnPreviousTask(ReturnPreviousTask&&) = delete;

    ~ReturnPreviousTask() {
        mCoroutine.destroy();
    }

    std::coroutine_handle<promise_type> mCoroutine;
};

/*
* whenall使用的控制块
* 
*/
struct WhenAllCtlBlock {
    std::size_t mCount;
    std::coroutine_handle<> mPrevious{};
    std::exception_ptr mException{};
};

/*
* whenall使用的Awaiter
* 
*/
struct WhenAllAwaiter {
    bool await_ready() const noexcept {
        return false;
    }

    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<> coroutine) const {
        if (mTasks.empty()) return coroutine;
        mControl.mPrevious = coroutine;
        for (auto const& t : mTasks.subspan(1))   //subspan(1)表示第一个以后的全要
            getLoop().addTask(t.mCoroutine);
        return mTasks.front().mCoroutine;   //这里是一种优化，直接恢复mTasks里的第一个协程，而不是放到事件循环里之后再等待恢复，这样优化后效率高点
    }

    void await_resume() const {
        if (mControl.mException) [[unlikely]] {
            std::rethrow_exception(mControl.mException);
            }
    }

    //C++20的特性，叫做聚合初始化，可以不用写构造函数
    WhenAllCtlBlock& mControl;
    std::span<ReturnPreviousTask const> mTasks;   //std::span可以直接从C++数组构造
};

/*
* whenall使用的helper协程
* 
*/
template <class T>
ReturnPreviousTask whenAllHelper(auto const& t, WhenAllCtlBlock& control,
    Uninitialized<T>& result) {
    try {
        result.putValue(co_await t);  //这里就是去执行用户的hello1、hello2协程，等待协程结束返回，t是Task类型，本身是一个Awaitable，这里co_await返回后，会调用Task::Awaiter::await_resume()得到t这个协程的返回值，这里将结果存储在函数参数result中
    }
    catch (...) {
        control.mException = std::current_exception();
        co_return control.mPrevious;
    }
    --control.mCount;  //数量减1，因为是whenall，需要所有定时器协程都满足，所以这里只能先减1，如果数量到0则恢复之前的协程（即whenAllImpl）
    if (control.mCount == 0) {   
        co_return control.mPrevious;
    }
    co_return nullptr;
}


/*
* whenall的具体实现协程
* 
*/
template <std::size_t... Is, class... Ts>
Task<std::tuple<typename AwaitableTraits<Ts>::NonVoidRetType...>>
whenAllImpl(std::index_sequence<Is...>, Ts &&...ts) {
    WhenAllCtlBlock control{ sizeof...(Ts) };                                     //whenall控制块，这里初始化mCount这个参数，其实就是要等待的协程的数量
    std::tuple<Uninitialized<typename AwaitableTraits<Ts>::RetType>...> result;   //每个协程的返回值
    ReturnPreviousTask taskArray[]{ whenAllHelper(ts, control, std::get<Is>(result))... };  //创建一个协程数组，每个元素都是whenAllHelper协程，使用了参数展开
    co_await WhenAllAwaiter(control, taskArray);                                  //从这里恢复执行之后，表示所有的定时器都满足了，result中存储了所有定时器协程的结果
    co_return std::tuple<typename AwaitableTraits<Ts>::NonVoidRetType...>(
        std::get<Is>(result).moveValue()...);                                     //获取每个协程的返回值，且由于whenAllImpl协程的返回值是Task类型，这个类型的promise_type指明了whenAllImpl协程结束的时候会恢复前一个协程的执行（即恢复hello协程的执行）
}

template <Awaitable... Ts>
    requires(sizeof...(Ts) != 0)
auto when_all(Ts &&...ts) {
    return whenAllImpl(std::make_index_sequence<sizeof...(Ts)>{},
        std::forward<Ts>(ts)...);
}

/*
* whenany使用的控制块
* 
*/
struct WhenAnyCtlBlock {
    static constexpr std::size_t kNullIndex = std::size_t(-1);

    std::size_t mIndex{ kNullIndex };
    std::coroutine_handle<> mPrevious{};
    std::exception_ptr mException{};
};

/*
* whenany使用的Awaier
* 
*/
struct WhenAnyAwaiter {
    bool await_ready() const noexcept {
        return false;
    }

    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<> coroutine) const {
        if (mTasks.empty()) return coroutine;
        mControl.mPrevious = coroutine;
        for (auto const& t : mTasks.subspan(1))
            getLoop().addTask(t.mCoroutine);
        return mTasks.front().mCoroutine;
    }

    void await_resume() const {
        if (mControl.mException) [[unlikely]] {
            std::rethrow_exception(mControl.mException);
            }
    }

    WhenAnyCtlBlock& mControl;
    std::span<ReturnPreviousTask const> mTasks;
};

/*
* whenany使用helper协程
* 
*/
template <class T>
ReturnPreviousTask whenAnyHelper(auto const& t, WhenAnyCtlBlock& control,
    Uninitialized<T>& result, std::size_t index) {
    try {
        result.putValue(co_await t);
    }
    catch (...) {
        control.mException = std::current_exception();
        co_return control.mPrevious;
    }
    --control.mIndex = index;
    co_return control.mPrevious;
}

/*
* whenany的具体实现协程
* 
*/
template <std::size_t... Is, class... Ts>
Task<std::variant<typename AwaitableTraits<Ts>::NonVoidRetType...>>
whenAnyImpl(std::index_sequence<Is...>, Ts &&...ts) {
    WhenAnyCtlBlock control{};
    std::tuple<Uninitialized<typename AwaitableTraits<Ts>::RetType>...> result;
    ReturnPreviousTask taskArray[]{ whenAnyHelper(ts, control, std::get<Is>(result), Is)... };
    co_await WhenAnyAwaiter(control, taskArray);
    Uninitialized<std::variant<typename AwaitableTraits<Ts>::NonVoidRetType...>> varResult;
    ((control.mIndex == Is && (varResult.putValue(
        std::in_place_index<Is>, std::get<Is>(result).moveValue()), 0)), ...);
    co_return varResult.moveValue();
}

template <Awaitable... Ts>
    requires(sizeof...(Ts) != 0)
auto when_any(Ts &&...ts) {
    return whenAnyImpl(std::make_index_sequence<sizeof...(Ts)>{},
        std::forward<Ts>(ts)...); 
}

Task<int> hello1() {
    debug(), "hello1开始睡1秒";
    co_await sleep_for(1s); // 1s 等价于 std::chrono::seconds(1)
    debug(), "hello1睡醒了";
    co_return 1;
}

Task<int> hello2() {
    debug(), "hello2开始睡2秒";
    co_await sleep_for(2s); // 2s 等价于 std::chrono::seconds(2)
    debug(), "hello2睡醒了";
    co_return 2;
}

Task<int> hello() {
    //whenany
    //debug(), "hello开始等1和2";
    //auto v = co_await when_any(hello1(), hello2());  //hello1和hello2
    //debug(), "hello看到", (int)v.index() + 1, "睡醒了";
    //co_return std::get<0>(v);

    //whenall
    debug(), "hello开始等1和2";
    auto [i,j] = co_await when_all(hello1(), hello2());  //hello1和hello2， 结构化绑定解引用，这里恢复的时候会调用Task<std::tuple<int,int>>中的Awaiter的await_resume()函数，这个函数会返回whenAllImpl协程的返回值
    debug(), "hello看到1和2都睡醒了";
    co_return i+j;
}

int main() {
    auto t = hello();
    getLoop().addTask(t);
    getLoop().runAll();
    debug(), "主函数中得到hello结果:", t.mCoroutine.promise().result();
    return 0;
}