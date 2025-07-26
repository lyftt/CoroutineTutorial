/************************************************************************
* Э�̴�����ԣ�����ִ�е�����
* û��ʵ������
* 
* 
**************************************************************************/


#include "debug.hpp"
#include <coroutine>
#include <chrono>

/*
* Awaiter�������ڲ�����Ҫ��await_ready��await_suspend��await_resume������ֻ���������ܳ�ΪAwaiter
* ����һ���������Awaitable
* 
* ���Awaiter�������ǲ�����һֱִ�У�֪��Э�̽���
*/
struct Awaiter
{
	bool await_ready()
	{
		/*
		* 1.�������true����ֱ�ӵ���await_resume
		* 2.�������false���ͻ�ֱ�ӵ���await_suspend
		*/
		//return true;
		return false;
	}

	/*
	* 1.���await_suspend��������void ��true����ֱ�ӹ��𣬽�ִ��Ȩ���ظ�������
	* 2.���await_suspend��������false����ֱ�ӻָ�ִ�У������await_resume
	* 3.���await_suspend��������ĳ��Э�̾������ôҲ��ֱ�ӻָ�ִ�У������await_resume
	*/
	std::coroutine_handle<> await_suspend(std::coroutine_handle<> coroutine) const noexcept {
		if (coroutine.done())
			return std::noop_coroutine();   //����������noop_coroutine����ôֱ�ӻ����Э�̣������ǻָ�
		else
			return coroutine;
	}

	/*
	* Э�ָ̻�ִ�е�ʱ����ã�����ǰ����Э�̹�����ͨ��co_await expr���й���ģ������Э�̴�����ʱ��ͨ��initial_suspend����ģ���ô֮��ָ���ʱ���ǲ���������await_resume������
	* 
	*/
	void await_resume() const noexcept {
		debug(), "resume";
	}
};

/*
* Awaitable������Awaiter��һ������
* 1.Awaiterһ����Awaitable
* 2.����Awaitable��һ����Awaiter����ΪAwaitable���Բ�����await_ready��await_suspend��await_resume��3������������ͨ������operator co_await��ת����Awaiter
*	�������ڿ���Awaitable��operator co_await���غ����󣬾ͻ����operator co_await�����䷵��ֵ��ΪAwaiter
* 
* 
*/
struct Awaitable
{
	Awaiter operator co_await()
	{
		return Awaiter();
	}
};



/*
* Э��Ӳ������Ҫ��promise_type�������ڲ�����Ҫ��get_return_object��initial_suspend��final_suspend��yield_value��return_void�Ƚӿں�������
* 
*/
struct Promise
{
	auto get_return_object()
	{
		return std::coroutine_handle<Promise>::from_promise(*this);
	}

	auto initial_suspend()
	{
		return std::suspend_always();
	}

	auto final_suspend() noexcept
	{
		return std::suspend_always();
	}

	auto yield_value(int ret)
	{
		mRetVal = ret;
		//return std::suspend_always();
		return Awaiter();
	}

	/*void return_value(int ret)
	{
		mRetVal = ret;
	}*/

	void return_void()
	{
		//mRetVal = 0;
	}

	void unhandled_exception() {
		throw;
	}

	int mRetVal;
};

/*
* Э�̺����ķ���ֵ�����ڲ�����Ҫ��promise_type�������
* 
* 
*/
struct Task
{
	using promise_type = Promise;

	Task(std::coroutine_handle<promise_type> c) :mCoroutine(c) {}

	std::coroutine_handle<promise_type> mCoroutine;
};

/*
* Э�� 
*
*/
Task hello()
{
	debug(), "hello 1";
	co_yield 1;

	debug(), "hello 2";
	co_yield 2;

	co_return;
}

int main()
{
	debug(), "before generate hello coroutine";
	auto t = hello();
	debug(), "after generate hello coroutine";

	/*Э��ֻҪ����������һֱ�ָ�ִ��*/
	while (!t.mCoroutine.done())
		t.mCoroutine.resume();

	debug(), "final result:", t.mCoroutine.promise().mRetVal;   //ͨ��std::coroutine_handle�����ȡpromise����

	return 0;
}