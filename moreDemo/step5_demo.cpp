/************************************************************************
* Э�̴�����ԣ�Э��ʵ�ֶ�ʱ��
* 
* ��1��ִ�йؼ����̣�helloЭ��ͨ����һ�������sleepЭ�̽��Լ�����֮��sleepЭ�̵���SleepAwaier��sleepЭ�̹��𣬹����ʱ��sleepЭ�̼��뵽��ʱ��ѭ���У�
* ��2����ʱ��ѭ����ʱ�䳬ʱ�󽫻�ָ�sleepЭ�̵�ִ�У�sleepЭ��ֱ��co_return��Ȼ����final_suspend�е���RepeatAwaiter���ָ�sleepЭ�̵�ǰһ��Э�̵�ִ�У�����helloЭ�̣���
* ��3��ͨ�������ķ�ʽ���˶�ʱ�����ã�
*
* ˯����ʵ����2��������
* ��1��sleep_until��
* ��2��sleep_for��
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
* promise_type���� 
* 
*/
template <class T>
struct Promise {
    auto initial_suspend() noexcept {
        return std::suspend_always();
    }

    //Э�̽�����ʱ��ָ�ǰһ��Э�̵�ִ��
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
* promise_type���ػ��汾
* 
*/
template <>
struct Promise<void> {
    auto initial_suspend() noexcept {
        return std::suspend_always();
    }

    //Э�̽�����ʱ��ָ���һ��Э�̵�ִ��
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
* Э�̷���ֵ����
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

    //Ϊ��Loop.AddTask()������Task��Ҫһ������ת�����������
    //Ϊ����ʽת��
    operator std::coroutine_handle<>() const noexcept {
        return mCoroutine;
    }

    std::coroutine_handle<promise_type> mCoroutine;
};

/*
* ��ʱ��ѭ��
* 
*/
struct Loop {
    //����Э�̶��У�ÿ��Ԫ�ض���Э�̾������������ִ�е�Э��
    //deque�ĺô�����β���ܿ��ٲ���
    std::deque<std::coroutine_handle<>> mReadyQueue;

    //��ʱ����
    struct TimerEntry {
        std::chrono::system_clock::time_point expireTime;   //����ʱ��
        std::coroutine_handle<> coroutine;                  //���ں�Ҫ�ָ���ʱ��

        //�Զ���ȽϺ���������std::priority_queue��ΪС����
        bool operator<(TimerEntry const& that) const noexcept {
            return expireTime > that.expireTime;
        }
    };

    //С���ѣ�Ĭ���Ǵ󶥶ѣ�����TimerEntry�ڲ������Զ���ȽϺ�����ת��С����
    std::priority_queue<TimerEntry> mTimerHeap;

    //��Ӿ�����Э��
    void addTask(std::coroutine_handle<> coroutine) {
        mReadyQueue.push_front(coroutine);
    }

    //��Ӷ�ʱ��
    void addTimer(std::chrono::system_clock::time_point expireTime, std::coroutine_handle<> coroutine) {
        mTimerHeap.push({ expireTime, coroutine });
    }

    //��ʱ��ѭ��
    void runAll() {
        while (!mTimerHeap.empty() || !mReadyQueue.empty()) {

            // ������Э��ȡ���ָ�ִ��
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
                    std::this_thread::sleep_until(timer.expireTime); //��С��û������ֱ��˯��
                }
            }
        }
    }

    //������ɿ����ñ���ɾ��5�����ֳ�Ա����������Ĭ�Ϲ��캯����
    //�������ɾ�ƶ����캯������ô���Ĭ�Ϲ��캯��Ҳɾ�� 
    Loop& operator=(Loop&&) = delete; 
};

/*
* ����ģʽ��ȡ��ʱ��ѭ��Loop
* 
*/
Loop& getLoop() {
    static Loop loop;   //�������ɣ�C++11�ܱ�֤�̰߳�ȫ
    return loop;
}


/*
* ����ʱ���õ�Awaiter����Awaiter��coroutine���뵽��ʱ��ѭ���У���ʱ��ѭ�����ڳ�ʱʱ��֮��ָ�Э�̵�ִ��
* 
*/
struct SleepAwaiter {
    bool await_ready() const noexcept {
        return false;
    }

    //����Э��coroutine��ʱ�򣬽��������Э�̼��뵽��ʱ��ѭ����
    void await_suspend(std::coroutine_handle<> coroutine) const {
        getLoop().addTimer(mExpireTime, coroutine);
    }

    void await_resume() const noexcept {
    }

    std::chrono::system_clock::time_point mExpireTime;   //��ʱʱ���
};

/*
* sleepЭ��
* 
*/
Task<void> sleep_until(std::chrono::system_clock::time_point expireTime) {
    co_await SleepAwaiter(expireTime);
    co_return;
}


/*
* sleepЭ��
* 
*/
Task<void> sleep_for(std::chrono::system_clock::duration duration) {
    co_await SleepAwaiter(std::chrono::system_clock::now() + duration);
    co_return;
}

Task<int> hello1() {
    debug(), "hello1��ʼ˯1��";
    co_await sleep_for(1s); // 1s �ȼ��� std::chrono::seconds(1)��operator""s()����������������ռ�std::chrono_literals��
    debug(), "hello1˯����";
    co_return 1;
}

Task<int> hello2() {
    debug(), "hello2��ʼ˯2��";
    co_await sleep_for(2s); // 2s �ȼ��� std::chrono::seconds(2)
    debug(), "hello2˯����";
    co_return 2;
}

int main() {
    auto t1 = hello1();
    auto t2 = hello2();
    getLoop().addTask(t1);
    getLoop().addTask(t2);
    getLoop().runAll();
    debug(), "�������еõ�hello1���:", t1.mCoroutine.promise().result();
    debug(), "�������еõ�hello2���:", t2.mCoroutine.promise().result();
    return 0;
}