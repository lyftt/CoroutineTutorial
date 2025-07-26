/****************************************************************************
* 
* C++20 Э��1
* coroutine����
* û���κ����������⣬������boost��ֻ��Ҫ������֧��C++20
* 
*
*
*****************************************************************************/


#include <coroutine>
#include <iostream>
#include <stdexcept>
#include <thread>


/**********************************************
* Э�̺����ķ���ֵ����coro_ret
* 
* coro_ret�ڲ���Ҫ����promise_type����
* 
* T��Э���ڷ���ֵ�ڲ���ֵ����
* 
***********************************************/
template <typename T>
struct coro_ret
{
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    //Э�̾��
    handle_type coro_handle_;

    //���캯��
    coro_ret(handle_type h) : coro_handle_(h)
    {
    }

    //������������
    coro_ret(const coro_ret&) = delete;
    //��������
    coro_ret(coro_ret&& s) : coro_handle_(s.coro_)
    {
        s.coro_handle_ = nullptr;
    }

    //��������
    ~coro_ret()
    {
        //!��������
        if (coro_handle_)
            coro_handle_.destroy();   //Э�̾��������
    }

    //����������ֵ
    coro_ret& operator=(const coro_ret&) = delete;

    //�ƶ�����
    coro_ret& operator=(coro_ret&& s)
    {
        coro_handle_ = s.coro_handle_;
        s.coro_handle_ = nullptr;
        return *this;
    }

    //�ָ�Э�̣������Ƿ����
    bool move_next()
    {
        coro_handle_.resume();       //����Э�̾���Ļָ�����
        return coro_handle_.done();  //�ж�Э���Ƿ����
    }

    //ͨ��promise��ȡ���ݻ�ȡЭ���ڲ���ֵ
    T get()
    {
        return coro_handle_.promise().return_data_;
    }

    //promise_type���ǳ�ŵ���󣬳�ŵ��������Э�����⽻��
    struct promise_type
    {
        //Ĭ�Ϲ�����������
        promise_type() = default;
        ~promise_type() = default;

        //����Э�̺����ķ���ֵ(����coro_ret)
        //����Э�̺�����ʱ��ͻ�����������
        auto get_return_object()
        {
            return coro_ret<T> { handle_type::from_promise(*this) };   //��promise����Э�̾��
        }

        //ע���������,���صľ���awaiter����������Э�̳�ʼ��֮�����Ϊ
        //�������std::suspend_never{}���Ͳ�����
        //����std::suspend_always{} ����
        //��Ȼ��Ҳ���Է�������awaiter
        auto initial_suspend()
        {
            //return std::suspend_never{};
            return std::suspend_always{};  //��ʼ��֮�����
        }

        //co_return ����������ᱻ���ã�����co_return�Ľ��
        void return_value(T v)
        {
            return_data_ = v;
            return;
        }

        //co_yield֮�������������������co_yield�Ľ��
        auto yield_value(T v)
        {
            std::cout << "yield_value invoked." << std::endl;
            return_data_ = v;
            return std::suspend_always{};  //����
        }

        //��Э������˳�����õĽӿڡ�
        //�� final_suspend ���� std::suspend_always ����Ҫ�û����е���
        //handle.destroy() �������٣���ע��final_suspend������ʱЭ���Ѿ�����
        //����std::suspend_always���������Э�̣�ʵ�� VSC++ 2022��
        auto final_suspend() noexcept
        {
            std::cout << "final_suspend invoked." << std::endl;
            return std::suspend_always{};
        }

        //
        void unhandled_exception()
        {
            std::exit(1);
        }

        //����ֵ
        T return_data_;
    };
};


/*****************************************************
* �����һ��Э�̺���
* 
* 
*****************************************************/
coro_ret<int> coroutine_7in7out()
{
    //����Э�̿�initial_suspend������std::suspend_always{};����һ�ι���
    std::cout << "Coroutine co_await std::suspend_never" << std::endl;

    //co_await std::suspend_never{} �������
    co_await std::suspend_never{};

    std::cout << "Coroutine co_await std::suspend_always" << std::endl;
    co_await std::suspend_always{};

    std::cout << "Coroutine stage 1 ,co_yield" << std::endl;
    co_yield 101;

    std::cout << "Coroutine stage 2 ,co_yield" << std::endl;
    co_yield 202;

    std::cout << "Coroutine stage 3 ,co_yield" << std::endl;
    co_yield 303;

    std::cout << "Coroutine stage end, co_return" << std::endl;
    co_return 808;
}

int main(int argc, char* argv[])
{
    bool done = false;
    std::cout << "Start coroutine_7in7out ()\n";

    //����Э��,c_r����Э�̵ķ���ֵ����coro_ret�����
    //Э�̹���֮��ͻ�����ﷵ��
    auto c_r = coroutine_7in7out();

    //��һ��ֹͣ��Ϊinitial_suspend ���ص���suspend_always������Э�̴���֮���ֱ�ӹ���
    //��ʱû�н���Stage 1
    std::cout << "Coroutine " << (done ? "is done " : "isn't done ") << "ret =" << c_r.get() << std::endl;
    done = c_r.move_next();

    //��ʱ�ǣ�co_await std::suspend_always{}
    std::cout << "Coroutine " << (done ? "is done " : "isn't done ") << "ret =" << c_r.get() << std::endl;
    done = c_r.move_next();

    //��ʱ��ӡStage 1
    std::cout << "Coroutine " << (done ? "is done " : "isn't done ") << "ret =" << c_r.get() << std::endl;
    done = c_r.move_next();

    std::cout << "Coroutine " << (done ? "is done " : "isn't done ") << "ret =" << c_r.get() << std::endl;
    done = c_r.move_next();

    std::cout << "Coroutine " << (done ? "is done " : "isn't done ") << "ret =" << c_r.get() << std::endl;
    done = c_r.move_next();

    std::cout << "Coroutine " << (done ? "is done " : "isn't done ") << "ret =" << c_r.get() << std::endl;

    return 0;
}