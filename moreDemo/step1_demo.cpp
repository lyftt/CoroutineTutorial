/************************************************************************
* 协程代码测试：测试执行的流程
* 没有实际意义
* 
* 
**************************************************************************/


#include "debug.hpp"
#include <coroutine>
#include <chrono>

/*
* Awaiter对象，其内部必须要有await_ready、await_suspend、await_resume函数，只有这样才能称为Awaiter
* 还有一个概念叫做Awaitable
* 
* 这个Awaiter的作用是部挂起，一直执行，知道协程结束
*/
struct Awaiter
{
	bool await_ready()
	{
		/*
		* 1.如果返回true，会直接调用await_resume
		* 2.如果返回false，就会直接调用await_suspend
		*/
		//return true;
		return false;
	}

	/*
	* 1.如果await_suspend函数返回void 或true，则直接挂起，将执行权返回给调用者
	* 2.如果await_suspend函数返回false，则直接恢复执行，会调用await_resume
	* 3.如果await_suspend函数返回某个协程句柄，那么也是直接恢复执行，会调用await_resume
	*/
	std::coroutine_handle<> await_suspend(std::coroutine_handle<> coroutine) const noexcept {
		if (coroutine.done())
			return std::noop_coroutine();   //如果返回这个noop_coroutine，那么直接会挂起协程，而不是恢复
		else
			return coroutine;
	}

	/*
	* 协程恢复执行的时候调用，但是前提是协程挂起是通过co_await expr进行挂起的，如果是协程创建的时候通过initial_suspend挂起的，那么之后恢复的时候是不会调用这个await_resume函数的
	* 
	*/
	void await_resume() const noexcept {
		debug(), "resume";
	}
};

/*
* Awaitable对象，与Awaiter有一点区别：
* 1.Awaiter一定是Awaitable
* 2.但是Awaitable不一定是Awaiter，因为Awaitable可以不具有await_ready、await_suspend、await_resume这3个函数，而是通过重载operator co_await来转换成Awaiter
*	编译器在看到Awaitable有operator co_await重载函数后，就会调用operator co_await，将其返回值作为Awaiter
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
* 协程硬编码需要的promise_type对象，其内部必须要有get_return_object、initial_suspend、final_suspend、yield_value、return_void等接口函数函数
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
* 协程函数的返回值，其内部必须要有promise_type这个类型
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
* 协程 
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

	/*协程只要不结束，就一直恢复执行*/
	while (!t.mCoroutine.done())
		t.mCoroutine.resume();

	debug(), "final result:", t.mCoroutine.promise().mRetVal;   //通过std::coroutine_handle对象获取promise对象

	return 0;
}