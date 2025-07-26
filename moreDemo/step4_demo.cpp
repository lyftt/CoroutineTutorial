/************************************************************************
* Э�̴�����ԣ�����ִ�е�����
* û��ʵ������
*
* ����ֵ�����Ǹ������ͣ�ʹ��ģ��ʵ��
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
*promise_type�Ķ��壬����ʹ����ģ�����Ͳ�������ʾ��������������͵ķ���ֵ
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
        mException = std::current_exception();   //��¼���쳣
    }

    //����ֵ����
    auto yield_value(T ret) noexcept {
        //std::construct_at(&mResult, std::move(ret));    //��placement new�ȼ�
        new (&mResult) T(std::move(ret));   //placement new
        return std::suspend_always();
    }

    //����ֵ����
    void return_value(T ret) noexcept {
        new (&mResult) T(std::move(ret));
    }

    T result() {
        if (mException) [[unlikely]] {
            std::rethrow_exception(mException);
            }

        T ret = std::move(mResult);   //�ƶ�֮��Ϳ���������
        //std::destory_at(&mResult);  //������������������������ȼ�
        mResult.~T();   //����������������

        return ret;
    }

    std::coroutine_handle<Promise> get_return_object() {
        return std::coroutine_handle<Promise>::from_promise(*this);
    }

    std::coroutine_handle<> mPrevious{};
    std::exception_ptr mException{};        //���ܵ��쳣

    //����ֵ�������Ǹ����ɣ���һ��union֮����ômResult�Ͳ�����Promise�����ʱ����й����ˣ����ǲ����ʼ���ˣ�����ô�Ϳ������placement new����ʹ����
    //����union����������ֹһ�����󱻹���
    union {
        T mResult;
    };

    Promise() noexcept {}
    Promise(Promise&&) = delete;
    ~Promise() {}
};


/*
*promise_type���ػ��汾
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

    std::coroutine_handle<promise_type> mCoroutine;   //Э�̾��
};

Task<std::string> baby() {
    debug(), "baby";
    co_return "aaa\n";   //��ִ��promise_type.return_value(������ֵ�洢��promise_type��)����ִ��promise_type.final_suspend(�ڲ���co_await����)
}

Task<double> world() {
    debug(), "world";
    co_return 3.14;
}

Task<int> hello() {
    auto ret = co_await baby();    //co_await�ķ���ֵ��Awaiter�����await_resume()�ķ���ֵ
    debug(), ret;
    int i = (int)co_await world();
    debug(), "hello�õ�world���Ϊ", i;
    co_return i + 1;
}

int main() {
    debug(), "main��������hello";
    auto t = hello();
    debug(), "main��������hello"; // ��ʵֻ������task���󣬲�û��������ʼִ��
    while (!t.mCoroutine.done()) {
        t.mCoroutine.resume();
        debug(), "main�õ�hello���Ϊ",
            t.mCoroutine.promise().result();
    }
    return 0;
}