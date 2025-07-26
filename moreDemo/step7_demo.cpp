/************************************************************************
* 协程代码测试：协程实现定时器
*
* 协程定时器经典案例:
* whenany:每个定时器的时间都满足
* whenall:任意一个定时器的时间满足
* 
* 相比step6_demo，定时器事件循环使用的是std::priority_queue，当时会存在崩溃问题，即协程结束销毁之后，定时器循环还持有协程句柄
* step7_demo里使用了红黑树来处里定时器事件循环（可以做到有序，std::priority_queue不行），同时处理掉了崩溃问题，当协程结束之后会自动从定时器事件循环中去除
* 
* 非常重要的一个知识点：在一个协程函数A内部，又启动了一个协程B，并且获得了这个子协程B的返回值，如果协程B执行了一部之后返回到A（即B未结束），A执行结束都没有恢复B的执行，那么B的返回值会直接析构掉
* 这个知识点就是这里解决崩溃问题的关键
* 
**************************************************************************/

#include <chrono>
#include <coroutine>
#include <deque>
#include <queue>
#include <span>
#include <thread>
#include <variant>
#include "rbtree.hpp"
#include "debug.hpp"

/*为了使用1s、2s这样的时间*/
using namespace std::chrono_literals;

template <class T = void> 
struct NonVoidHelper {
    using Type = T;
};

template <> 
struct NonVoidHelper<void> {
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

    T moveValue() {
        T ret(std::move(mValue));
        mValue.~T();
        return ret;
    }

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

template <class To, std::derived_from<To> P>
constexpr std::coroutine_handle<To> staticHandleCast(std::coroutine_handle<P> coroutine) {
    return std::coroutine_handle<To>::from_address(coroutine.address());
}

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
        if (coroutine.done())
            return std::noop_coroutine();
        else
            return coroutine;
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
            return mPrevious;
        else
            return std::noop_coroutine();
    }

    void await_resume() const noexcept {}
};

/*
* promise_type
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
        return PreviousAwaiter(mPrevious);
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
template <class T = void, class P = Promise<T>> 
struct Task {
    using promise_type = P;

    Task(std::coroutine_handle<promise_type> coroutine) noexcept
        : mCoroutine(coroutine) {}

    Task(Task&&) = delete;

    ~Task() {
        mCoroutine.destroy();
    }

    struct Awaiter {
        bool await_ready() const noexcept {
            return false;
        }

        std::coroutine_handle<promise_type>
            await_suspend(std::coroutine_handle<> coroutine) const noexcept {
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

    operator std::coroutine_handle<>() const noexcept {
        return mCoroutine;
    }

    std::coroutine_handle<promise_type> mCoroutine;
};

/*
* sleep_until和sleep_for使用的promise类型
* 
* 继承了红黑树节点和Promise类型
* （1）Promise类型决定了协程结束的时候会恢复前一个协程的执行
* （2）RbNode决定了可以作为红黑树的节点
*/
struct SleepUntilPromise : RbTree<SleepUntilPromise>::RbNode, Promise<void> 
{
    std::chrono::system_clock::time_point mExpireTime;

    auto get_return_object() {
        return std::coroutine_handle<SleepUntilPromise>::from_promise(*this);
    }

    SleepUntilPromise& operator=(SleepUntilPromise&&) = delete;

    friend bool operator<(SleepUntilPromise const& lhs, SleepUntilPromise const& rhs) noexcept {
        return lhs.mExpireTime < rhs.mExpireTime;
    }
};

/*
* 事件循环
*
*/
struct Loop {
    RbTree<SleepUntilPromise> mRbTimer{};   //红黑树

    void addTimer(SleepUntilPromise& promise) {
        mRbTimer.insert(promise);   //红黑树插入协程SleepUntilPromise对象，因为SleepUntilPromise继承了红黑树节点
    }

    void run(std::coroutine_handle<> coroutine) {
        while (!coroutine.done()) 
        {
            coroutine.resume();
            while (!mRbTimer.empty()) 
            {
                if (!mRbTimer.empty()) 
                {
                    auto nowTime = std::chrono::system_clock::now();
                    auto& promise = mRbTimer.front();
                    if (promise.mExpireTime < nowTime) 
                    {
                        mRbTimer.erase(promise);    //移除了一个红黑树节点
                        std::coroutine_handle<SleepUntilPromise>::from_promise(promise).resume();   //由SleepUntilPromise转成对应的协程对象，恢复执行
                    }
                    else 
                    {
                        std::this_thread::sleep_until(promise.mExpireTime);
                    }
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

    void await_suspend(std::coroutine_handle<SleepUntilPromise> coroutine) const {
        auto& promise = coroutine.promise();
        promise.mExpireTime = mExpireTime;
        loop.addTimer(promise);   //这里加入到定时器事件循环中的不是std::coroutine_handle句柄了，而是promise
    }

    void await_resume() const noexcept {}

    Loop& loop;
    std::chrono::system_clock::time_point mExpireTime;
};

/*
* 这个sleep_until协程结束之后会恢复前一个协程的执行
* 
*/
Task<void, SleepUntilPromise> sleep_until(std::chrono::system_clock::time_point expireTime) {
    auto& loop = getLoop();
    co_await SleepAwaiter(loop, expireTime);   //这个Awaiter将该协程加入事件循环中
}

/*
* 这个sleep_until协程结束之后会恢复前一个协程的执行
*
*/
Task<void, SleepUntilPromise> sleep_for(std::chrono::system_clock::duration duration) {
    auto& loop = getLoop();
    co_await SleepAwaiter(loop, std::chrono::system_clock::now() + duration);  //这个Awaiter将该协程加入事件循环中
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
        return PreviousAwaiter(mPrevious);
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
        mCoroutine.destroy();   //销毁协程
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
        for (auto const& t : mTasks.subspan(0, mTasks.size() - 1))
            t.mCoroutine.resume();
        return mTasks.back().mCoroutine;
    }

    void await_resume() const {
        if (mControl.mException) [[unlikely]] {
            std::rethrow_exception(mControl.mException);
            }
    }

    WhenAllCtlBlock& mControl;
    std::span<ReturnPreviousTask const> mTasks;
};

/*
* whenall使用的helper协程
*
*/
template <class T>
ReturnPreviousTask whenAllHelper(auto const& t, WhenAllCtlBlock& control,
    Uninitialized<T>& result) {
    try {
        result.putValue(co_await t);
    }
    catch (...) {
        control.mException = std::current_exception();
        co_return control.mPrevious;
    }
    --control.mCount;
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
    WhenAllCtlBlock control{ sizeof...(Ts) };
    std::tuple<Uninitialized<typename AwaitableTraits<Ts>::RetType>...> result;
    ReturnPreviousTask taskArray[]{ whenAllHelper(ts, control, std::get<Is>(result))... };
    co_await WhenAllAwaiter(control, taskArray);
    co_return std::tuple<typename AwaitableTraits<Ts>::NonVoidRetType...>(
        std::get<Is>(result).moveValue()...);
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
        await_suspend(std::coroutine_handle<> coroutine) const {   //coroutine是要被挂起的协程
        if (mTasks.empty()) return coroutine;

        mControl.mPrevious = coroutine;  //记录下前一个协程，使用同一个WhenAnyCtlBlock控制块的协程拥有共同的previous协程，就是whenAnyImpl，最早结束的协程就会直接恢复whenAnyImpl的执行
        for (auto const& t : mTasks.subspan(0, mTasks.size() - 1))
            t.mCoroutine.resume();       //直接恢复每一个协程的执行

        return mTasks.back().mCoroutine; //最后一个协程恢复执行
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
        result.putValue(co_await t);   //这里的t其实就是hello1和hello2对应的协程了
    }
    catch (...) {
        control.mException = std::current_exception();
        co_return control.mPrevious;
    }
    --control.mIndex = index;    //最先完成的协程的索引
    co_return control.mPrevious; //恢复前一个协程的执行
}

/*
* whenany的具体实现协程
*
*/
template <std::size_t... Is, class... Ts>
Task<std::variant<typename AwaitableTraits<Ts>::NonVoidRetType...>>
whenAnyImpl(std::index_sequence<Is...>, Ts &&...ts) {
    WhenAnyCtlBlock control{};             //控制块
    std::tuple<Uninitialized<typename AwaitableTraits<Ts>::RetType>...> result;                 //3个协程的结果，使用tuple
    ReturnPreviousTask taskArray[]{ whenAnyHelper(ts, control, std::get<Is>(result), Is)... };  //创建协程数组，whenAnyHelper也是协程，每个协程的结果存储在std::get<Is>(result)中，Is是下标索引
    co_await WhenAnyAwaiter(control, taskArray);                                                //WhenAnyAwaiter是一个Awaiter
    Uninitialized<std::variant<typename AwaitableTraits<Ts>::NonVoidRetType...>> varResult;
    ((control.mIndex == Is && (varResult.putValue(
        std::in_place_index<Is>, std::get<Is>(result).moveValue()), 0)), ...);
    co_return varResult.moveValue();
}

template <Awaitable... Ts>
    requires(sizeof...(Ts) != 0)
auto when_any(Ts &&...ts) {
    return whenAnyImpl(std::make_index_sequence<sizeof...(Ts)>{},
        std::forward<Ts>(ts)...);   //whenAnyImpl是一个协程
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
    debug(), "hello开始等1和2";
    auto v = co_await when_any(hello1(), hello2());   //等待任一一个协程结束，可以想象成网络接收数据和超时2个协程，只要有一个满足就立即返回
    debug(), "hello看到", (int)v.index() + 1, "睡醒了";
    co_return std::get<0>(v);

    //auto [i, j] = co_await when_all(hello1(), hello2());
    //debug(), "hello看到1和2都睡醒了";
    //co_return i + j;
}

int main() {
    auto t = hello();   //创建了hello协程
    getLoop().run(t);   //恢复hello协程的执行
    debug(), "主函数中得到hello结果:", t.mCoroutine.promise().result();   //得到hello协程的结果
    return 0;
}