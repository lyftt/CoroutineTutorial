/************************************************************************
* Э�̴�����ԣ�����ִ�е�����
* û��ʵ������
*
* ������co_await�Ĳ��ԣ�Awaitabel��Awaiter����
*
* �ܽἸ�㣺
* 1.co_yield  ��promise_type.yield_value()�����Ƿ����Э�̣��Լ������Ĳ���
*
* 2.co_return Э�̺��������
*
* 3.co_await ��Awaitable��Awaiter������Ч��await_ready()��await_suspend()�����Ƿ���𣬹����Ĳ�����co_await�����з���ֵ;
*            ���Awaitable��Awaiter�����Ǹ�Э�̾������ô���൱�ڻָ����Э�̾����Ӧ��Э��
**************************************************************************/

#include <chrono>
#include <coroutine>
#include "debug.hpp"

/*
* ֻҪ��Э��û�н������Ͳ�����𣬼�һֱִ��
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
* ����Э�̣�������һ��Э��
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
* Э�̷���ֵ��Ҫ��promise_type
* 
*/
struct Promise {
    auto initial_suspend() {
        return std::suspend_always();
    }

    auto final_suspend() noexcept {
        return PreviousAwaiter(mPrevious);
    }

    //���Э�����׳��쳣����ô�ᱻ����ӿڽ��յ�
    void unhandled_exception() {
        debug(), "unhandled_exception:";
        //std::rethrow_exception(std::current_exception());   //��ȡ��ǰ�쳣�������׳�
        //throw;   //�����׳�
        mExceptionPtr = std::current_exception();   //���ｫ�쳣��������

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

    int& result()  //��������
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
* Э�̵ķ���ֵ���������Ҫ�м���������
* 1.promise_type
* 2.Э�̾��
* 
* ע�⣬Э�̷���ֵҲ������Awaitable��Awaiter������������һ��Э��co_await���Э�̷���ֵ��ʱ�򣬾Ϳ���ͬʱ��2��Э�̵ľ����
* 1.await_suspend(std::coroutine_handle<> coroutine)�еĲ���coroutine�ǵ�����Э�̵ľ��
* 2.Awaiter�Լ����Ǳ�����Э�̵ķ���ֵ�������б���Э�̵ľ��
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

        //std::coroutine_handle<xx>ʵ������ģ���඼��operator coroutine_handle<>() �������ת�������
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> coroutine) const {
            mCoroutine.promise().mPrevious = coroutine;
            return mCoroutine;
        }

        //�����������Awaiter��Э�̱��ָ���ʱ������ӿھͻᱻ���ã�����ֵ����co_await�ķ���ֵ
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
    throw "error";   //�׳��쳣���ᱻpromise_type��unhandled_exception���յ��������׳��쳣֮��,�����co_return�Ͳ���ִ���ˣ�ֱ��ִ��final_suspend
    co_return 41;
}

Task hello() {
    int i = co_await world();   //co_await�����з���ֵ������ֵ���ǵ�ǰЭ�̱����ѵ�ʱ��֮ǰco_await��Awaiter�����await_resume()�������ã�����ӿڵķ���ֵ����co_await�ķ���ֵ
    debug(), "hello�õ�world���Ϊ", i;
    co_return i + 1;
}

int main() {
    debug(), "before generate hello";
    Task t = hello();
    debug(), "after generate hello"; // ��ʵֻ������task���󣬲�û��������ʼִ��

    while (!t.mCoroutine.done()) {
        t.mCoroutine.resume();
        debug(), "main�õ�hello���Ϊ", t.mCoroutine.promise().result();
    }
    return 0;
}