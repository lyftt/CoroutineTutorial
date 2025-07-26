/************************************************************************
* Э�̴�����ԣ�Э��ʵ�ֶ�ʱ��
*
* Э�̶�ʱ�����䰸��:
* whenany:ÿ����ʱ����ʱ�䶼����
* whenall:����һ����ʱ����ʱ������
* 
* ���step6_demo����ʱ���¼�ѭ��ʹ�õ���std::priority_queue����ʱ����ڱ������⣬��Э�̽�������֮�󣬶�ʱ��ѭ��������Э�̾��
* step7_demo��ʹ���˺���������ﶨʱ���¼�ѭ����������������std::priority_queue���У���ͬʱ������˱������⣬��Э�̽���֮����Զ��Ӷ�ʱ���¼�ѭ����ȥ��
* 
* �ǳ���Ҫ��һ��֪ʶ�㣺��һ��Э�̺���A�ڲ�����������һ��Э��B�����һ���������Э��B�ķ���ֵ�����Э��Bִ����һ��֮�󷵻ص�A����Bδ��������Aִ�н�����û�лָ�B��ִ�У���ôB�ķ���ֵ��ֱ��������
* ���֪ʶ�������������������Ĺؼ�
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

/*Ϊ��ʹ��1s��2s������ʱ��*/
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
* δ��ʼ���Ķ��󣬶����ڲ�����������ΪT��֮����δ��ʼ��������Ϊʹ����union
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

/*�ػ�*/
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
* C++20��concept�����Awaiter���Ǳ�ʾ����֮ǰ�ᵽ��Awaiter������������ڲ�Ҫ��await_ready��await_suspend��await_resume��������
*
*/
template <class A>
concept Awaiter = requires(A a, std::coroutine_handle<> h) {
    { a.await_ready() };
    { a.await_suspend(h) };
    { a.await_resume() };
};

/*
* C++20��concept�����Awaitable��������֮ǰ�ᵽ��Awaitable�����ڲ���������operator co_await()���������ת������������᷵��һ��Awaiter����
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
* ����ִ�е�Awaiter
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
* ִ��ǰһ��Э�̵�Awaiter���ܶ�Э�����л���ʱ�򣬻��¼��ǰһ��Э�̣����Awaiter�����þ��ǻָ�ǰһ��Э�̵�ִ�У���������Э��promise_type�ڲ���final_suspend()��(��Э�̽���֮��ָ�ǰһ��Э�̵�ִ��)
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
* �ػ��汾
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
* Э�̵ķ���ֵ���ͣ���������ڲ���Э�̾��std::coroutine_handle���Ա�֮��ָ�Э�̵�ִ��
*
* ��ЩЭ�̷���ֵ�ڲ�ʹ�õ�promise_type��Promise���ͣ����Promise�ڲ�������Task��Ӧ��Э���ڽ�����ʱ���ָ�ǰһ��Э�̵�ִ��
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
* sleep_until��sleep_forʹ�õ�promise����
* 
* �̳��˺�����ڵ��Promise����
* ��1��Promise���;�����Э�̽�����ʱ���ָ�ǰһ��Э�̵�ִ��
* ��2��RbNode�����˿�����Ϊ������Ľڵ�
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
* �¼�ѭ��
*
*/
struct Loop {
    RbTree<SleepUntilPromise> mRbTimer{};   //�����

    void addTimer(SleepUntilPromise& promise) {
        mRbTimer.insert(promise);   //���������Э��SleepUntilPromise������ΪSleepUntilPromise�̳��˺�����ڵ�
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
                        mRbTimer.erase(promise);    //�Ƴ���һ��������ڵ�
                        std::coroutine_handle<SleepUntilPromise>::from_promise(promise).resume();   //��SleepUntilPromiseת�ɶ�Ӧ��Э�̶��󣬻ָ�ִ��
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
* ���Awaiter����ͣ��Э�̼��뵽�¼�ѭ���У��Ա���ʱ�䵽��֮���¼�ѭ�����Իָ�Э�̵�ִ��
*
*/
struct SleepAwaiter {
    bool await_ready() const noexcept {
        return false;
    }

    void await_suspend(std::coroutine_handle<SleepUntilPromise> coroutine) const {
        auto& promise = coroutine.promise();
        promise.mExpireTime = mExpireTime;
        loop.addTimer(promise);   //������뵽��ʱ���¼�ѭ���еĲ���std::coroutine_handle����ˣ�����promise
    }

    void await_resume() const noexcept {}

    Loop& loop;
    std::chrono::system_clock::time_point mExpireTime;
};

/*
* ���sleep_untilЭ�̽���֮���ָ�ǰһ��Э�̵�ִ��
* 
*/
Task<void, SleepUntilPromise> sleep_until(std::chrono::system_clock::time_point expireTime) {
    auto& loop = getLoop();
    co_await SleepAwaiter(loop, expireTime);   //���Awaiter����Э�̼����¼�ѭ����
}

/*
* ���sleep_untilЭ�̽���֮���ָ�ǰһ��Э�̵�ִ��
*
*/
Task<void, SleepUntilPromise> sleep_for(std::chrono::system_clock::duration duration) {
    auto& loop = getLoop();
    co_await SleepAwaiter(loop, std::chrono::system_clock::now() + duration);  //���Awaiter����Э�̼����¼�ѭ����
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
* promise_type���ͣ�ʹ�����promise_type��Э�̻���Э�̽���֮��ָ�ǰһ��Э�̵�ִ��
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
* Э�̷���ֵ�����ڲ�ʹ����ReturnPreviousPromise�����Ϊpromise_type����
*
*/
struct ReturnPreviousTask {
    using promise_type = ReturnPreviousPromise;

    ReturnPreviousTask(std::coroutine_handle<promise_type> coroutine) noexcept
        : mCoroutine(coroutine) {}

    ReturnPreviousTask(ReturnPreviousTask&&) = delete;

    ~ReturnPreviousTask() {
        mCoroutine.destroy();   //����Э��
    }

    std::coroutine_handle<promise_type> mCoroutine;
};

/*
* whenallʹ�õĿ��ƿ�
*
*/
struct WhenAllCtlBlock {
    std::size_t mCount;
    std::coroutine_handle<> mPrevious{};
    std::exception_ptr mException{};
};

/*
* whenallʹ�õ�Awaiter
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
* whenallʹ�õ�helperЭ��
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
* whenall�ľ���ʵ��Э��
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
* whenanyʹ�õĿ��ƿ�
*
*/
struct WhenAnyCtlBlock {
    static constexpr std::size_t kNullIndex = std::size_t(-1);

    std::size_t mIndex{ kNullIndex };
    std::coroutine_handle<> mPrevious{};
    std::exception_ptr mException{};
};

/*
* whenanyʹ�õ�Awaier
*
*/
struct WhenAnyAwaiter {
    bool await_ready() const noexcept {
        return false;
    }

    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<> coroutine) const {   //coroutine��Ҫ�������Э��
        if (mTasks.empty()) return coroutine;

        mControl.mPrevious = coroutine;  //��¼��ǰһ��Э�̣�ʹ��ͬһ��WhenAnyCtlBlock���ƿ��Э��ӵ�й�ͬ��previousЭ�̣�����whenAnyImpl�����������Э�̾ͻ�ֱ�ӻָ�whenAnyImpl��ִ��
        for (auto const& t : mTasks.subspan(0, mTasks.size() - 1))
            t.mCoroutine.resume();       //ֱ�ӻָ�ÿһ��Э�̵�ִ��

        return mTasks.back().mCoroutine; //���һ��Э�ָ̻�ִ��
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
* whenanyʹ��helperЭ��
*
*/
template <class T>
ReturnPreviousTask whenAnyHelper(auto const& t, WhenAnyCtlBlock& control,
    Uninitialized<T>& result, std::size_t index) {
    try {
        result.putValue(co_await t);   //�����t��ʵ����hello1��hello2��Ӧ��Э����
    }
    catch (...) {
        control.mException = std::current_exception();
        co_return control.mPrevious;
    }
    --control.mIndex = index;    //������ɵ�Э�̵�����
    co_return control.mPrevious; //�ָ�ǰһ��Э�̵�ִ��
}

/*
* whenany�ľ���ʵ��Э��
*
*/
template <std::size_t... Is, class... Ts>
Task<std::variant<typename AwaitableTraits<Ts>::NonVoidRetType...>>
whenAnyImpl(std::index_sequence<Is...>, Ts &&...ts) {
    WhenAnyCtlBlock control{};             //���ƿ�
    std::tuple<Uninitialized<typename AwaitableTraits<Ts>::RetType>...> result;                 //3��Э�̵Ľ����ʹ��tuple
    ReturnPreviousTask taskArray[]{ whenAnyHelper(ts, control, std::get<Is>(result), Is)... };  //����Э�����飬whenAnyHelperҲ��Э�̣�ÿ��Э�̵Ľ���洢��std::get<Is>(result)�У�Is���±�����
    co_await WhenAnyAwaiter(control, taskArray);                                                //WhenAnyAwaiter��һ��Awaiter
    Uninitialized<std::variant<typename AwaitableTraits<Ts>::NonVoidRetType...>> varResult;
    ((control.mIndex == Is && (varResult.putValue(
        std::in_place_index<Is>, std::get<Is>(result).moveValue()), 0)), ...);
    co_return varResult.moveValue();
}

template <Awaitable... Ts>
    requires(sizeof...(Ts) != 0)
auto when_any(Ts &&...ts) {
    return whenAnyImpl(std::make_index_sequence<sizeof...(Ts)>{},
        std::forward<Ts>(ts)...);   //whenAnyImpl��һ��Э��
}

Task<int> hello1() {
    debug(), "hello1��ʼ˯1��";
    co_await sleep_for(1s); // 1s �ȼ��� std::chrono::seconds(1)
    debug(), "hello1˯����";
    co_return 1;
}

Task<int> hello2() {
    debug(), "hello2��ʼ˯2��";
    co_await sleep_for(2s); // 2s �ȼ��� std::chrono::seconds(2)
    debug(), "hello2˯����";
    co_return 2;
}

Task<int> hello() {
    debug(), "hello��ʼ��1��2";
    auto v = co_await when_any(hello1(), hello2());   //�ȴ���һһ��Э�̽������������������������ݺͳ�ʱ2��Э�̣�ֻҪ��һ���������������
    debug(), "hello����", (int)v.index() + 1, "˯����";
    co_return std::get<0>(v);

    //auto [i, j] = co_await when_all(hello1(), hello2());
    //debug(), "hello����1��2��˯����";
    //co_return i + j;
}

int main() {
    auto t = hello();   //������helloЭ��
    getLoop().run(t);   //�ָ�helloЭ�̵�ִ��
    debug(), "�������еõ�hello���:", t.mCoroutine.promise().result();   //�õ�helloЭ�̵Ľ��
    return 0;
}