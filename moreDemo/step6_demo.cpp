/************************************************************************
* Э�̴�����ԣ�Э��ʵ�ֶ�ʱ��
*
* Э�̶�ʱ�����䰸��:
* whenany:ÿ����ʱ����ʱ�䶼����
* whenall:����һ����ʱ����ʱ������
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

/*Ϊ��ʹ��1s��2s������ʱ��*/
using namespace std::chrono_literals;

template <class T = void> struct NonVoidHelper {
    using Type = T;
};

template <> struct NonVoidHelper<void> {
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

    /*
    * �ƶ�
    */
    T moveValue() {
        T ret(std::move(mValue));
        mValue.~T();
        return ret;
    }

    /*
    * ��δ��ʼ����T���͵Ĵ洢�ռ���ֱ��ԭ�ع���
    * 
    */
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

/*�ػ�*/
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
        if (coroutine.done())   //ֻҪcoroutine���Э��û�����ͼ���ִ��
            return std::noop_coroutine();
        else
            return coroutine;   //�����await_resume
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
            return mPrevious;   //�ָ�ǰһ��Э��
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
        return PreviousAwaiter(mPrevious);  //Э�̽�����ָ�ǰһ��Э�̵�ִ�У�����еĻ���
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
        return PreviousAwaiter(mPrevious);   //�ָ�ǰһ��Э��
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
    * Taskͬʱ����һ��Awaitable��ʵ����operator co_await()
    * 
    */
    struct Awaiter {
        bool await_ready() const noexcept {
            return false;
        }

        std::coroutine_handle<promise_type>
            await_suspend(std::coroutine_handle<> coroutine) const noexcept {
            mCoroutine.promise().mPrevious = coroutine;    //��¼ǰһ��Э��
            return mCoroutine;   //ִ��Awaiter���������Э�̣�����Task��Ӧ�����Э��
        }

        T await_resume() const {
            return mCoroutine.promise().result();  //co_await���ص�ʱ����������������ȡTask���Э�̵Ľ����promise_type������ڲ���
        }

        std::coroutine_handle<promise_type> mCoroutine;
    };

    //TaskתAwaiter
    auto operator co_await() const noexcept {
        return Awaiter(mCoroutine);   //Task��Ӧ��Э����Ϊ����
    }

    operator std::coroutine_handle<>() const noexcept {
        return mCoroutine;
    }

    std::coroutine_handle<promise_type> mCoroutine;
};

/*
* �¼�ѭ��
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
        //������������е�Э�̺Ͷ�ʱ�������е�Э��
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
* ���Awaiter����ͣ��Э�̼��뵽�¼�ѭ���У��Ա���ʱ�䵽��֮���¼�ѭ�����Իָ�Э�̵�ִ��
* 
*/
struct SleepAwaiter {
    bool await_ready() const noexcept {
        return false;
    }

    void await_suspend(std::coroutine_handle<> coroutine) const {
        getLoop().addTimer(mExpireTime, coroutine);   //��Ҫ�����Э����ӵ��¼�ѭ���У�������ʱ��ʱ��
    }

    void await_resume() const noexcept {}

    std::chrono::system_clock::time_point mExpireTime;
};

/*
* Э��
* 
*/
Task<void> sleep_until(std::chrono::system_clock::time_point expireTime) {
    co_await SleepAwaiter(expireTime);
}

/*
* Э��
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
* promise_type���ͣ�ʹ�����promise_type��Э�̻���Э�̽���֮��ָ�ǰһ��Э�̵�ִ��
* 
*/
struct ReturnPreviousPromise {
    auto initial_suspend() noexcept {
        return std::suspend_always();
    }

    auto final_suspend() noexcept {
        return PreviousAwaiter(mPrevious);  //�ָ�ǰһ��Э�̵�ִ��
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
        mCoroutine.destroy();
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
        for (auto const& t : mTasks.subspan(1))   //subspan(1)��ʾ��һ���Ժ��ȫҪ
            getLoop().addTask(t.mCoroutine);
        return mTasks.front().mCoroutine;   //������һ���Ż���ֱ�ӻָ�mTasks��ĵ�һ��Э�̣������Ƿŵ��¼�ѭ����֮���ٵȴ��ָ��������Ż���Ч�ʸߵ�
    }

    void await_resume() const {
        if (mControl.mException) [[unlikely]] {
            std::rethrow_exception(mControl.mException);
            }
    }

    //C++20�����ԣ������ۺϳ�ʼ�������Բ���д���캯��
    WhenAllCtlBlock& mControl;
    std::span<ReturnPreviousTask const> mTasks;   //std::span����ֱ�Ӵ�C++���鹹��
};

/*
* whenallʹ�õ�helperЭ��
* 
*/
template <class T>
ReturnPreviousTask whenAllHelper(auto const& t, WhenAllCtlBlock& control,
    Uninitialized<T>& result) {
    try {
        result.putValue(co_await t);  //�������ȥִ���û���hello1��hello2Э�̣��ȴ�Э�̽������أ�t��Task���ͣ�������һ��Awaitable������co_await���غ󣬻����Task::Awaiter::await_resume()�õ�t���Э�̵ķ���ֵ�����ｫ����洢�ں�������result��
    }
    catch (...) {
        control.mException = std::current_exception();
        co_return control.mPrevious;
    }
    --control.mCount;  //������1����Ϊ��whenall����Ҫ���ж�ʱ��Э�̶����㣬��������ֻ���ȼ�1�����������0��ָ�֮ǰ��Э�̣���whenAllImpl��
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
    WhenAllCtlBlock control{ sizeof...(Ts) };                                     //whenall���ƿ飬�����ʼ��mCount�����������ʵ����Ҫ�ȴ���Э�̵�����
    std::tuple<Uninitialized<typename AwaitableTraits<Ts>::RetType>...> result;   //ÿ��Э�̵ķ���ֵ
    ReturnPreviousTask taskArray[]{ whenAllHelper(ts, control, std::get<Is>(result))... };  //����һ��Э�����飬ÿ��Ԫ�ض���whenAllHelperЭ�̣�ʹ���˲���չ��
    co_await WhenAllAwaiter(control, taskArray);                                  //������ָ�ִ��֮�󣬱�ʾ���еĶ�ʱ���������ˣ�result�д洢�����ж�ʱ��Э�̵Ľ��
    co_return std::tuple<typename AwaitableTraits<Ts>::NonVoidRetType...>(
        std::get<Is>(result).moveValue()...);                                     //��ȡÿ��Э�̵ķ���ֵ��������whenAllImplЭ�̵ķ���ֵ��Task���ͣ�������͵�promise_typeָ����whenAllImplЭ�̽�����ʱ���ָ�ǰһ��Э�̵�ִ�У����ָ�helloЭ�̵�ִ�У�
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
* whenanyʹ��helperЭ��
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
* whenany�ľ���ʵ��Э��
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
    //whenany
    //debug(), "hello��ʼ��1��2";
    //auto v = co_await when_any(hello1(), hello2());  //hello1��hello2
    //debug(), "hello����", (int)v.index() + 1, "˯����";
    //co_return std::get<0>(v);

    //whenall
    debug(), "hello��ʼ��1��2";
    auto [i,j] = co_await when_all(hello1(), hello2());  //hello1��hello2�� �ṹ���󶨽����ã�����ָ���ʱ������Task<std::tuple<int,int>>�е�Awaiter��await_resume()��������������᷵��whenAllImplЭ�̵ķ���ֵ
    debug(), "hello����1��2��˯����";
    co_return i+j;
}

int main() {
    auto t = hello();
    getLoop().addTask(t);
    getLoop().runAll();
    debug(), "�������еõ�hello���:", t.mCoroutine.promise().result();
    return 0;
}