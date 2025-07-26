/************************************************************************
* 协程代码测试：测试执行的流程
* 没有实际意义
*
* 增加了co_await的测试，Awaitabel和Awaiter概念
*
* 总结几点：
* 1.co_yield  由promise_type.yield_value()决定是否挂起协程，以及挂起后的操作
* 
* 2.co_return 协程函数体结束
* 
* 3.co_await 对Awaitable或Awaiter对象生效，await_ready()、await_suspend()决定是否挂起，挂起后的操作  
**************************************************************************/


#include "debug.hpp"
#include <coroutine>
#include <chrono>

/*
* Awaiter对象，其内部必须要有await_ready、await_suspend、await_resume函数，只有这样才能称为Awaiter
* 还有一个概念叫做Awaitable
*
* 这个Awaiter的作用是不挂起，一直执行，直到协程结束
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
		debug(), "Awaiter resume";
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
* Awaiter对象，其内部必须要有await_ready、await_suspend、await_resume函数，只有这样才能称为Awaiter
* 
* 这个Awaiter
* 
*/
struct PreviousAwaiter
{
	bool await_ready()
	{
		return false;
	}

	/*
	* 1.如果await_suspend函数返回void 或true，则直接挂起，将执行权返回给调用者
	* 2.如果await_suspend函数返回false，则直接恢复执行，会调用await_resume
	* 3.如果await_suspend函数返回某个协程句柄，那么也是直接恢复执行，会调用await_resume
	*/
	std::coroutine_handle<> await_suspend(std::coroutine_handle<> coroutine) const noexcept {
		if (mPrevious)
		{
			return mPrevious;   //执行前一个协程
		}
		else
		{
			return std::noop_coroutine();
		}
	}

	/*
	* 协程恢复执行的时候调用，但是前提是协程挂起是通过co_await expr进行挂起的，如果是协程创建的时候通过initial_suspend挂起的，那么之后恢复的时候是不会调用这个await_resume函数的
	*
	*/
	void await_resume() const noexcept {
		debug(), "PreviousAwaiter resume";
	}

	std::coroutine_handle<> mPrevious;
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
		//return Awaiter();
		return PreviousAwaiter(mPrevious);
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
	std::coroutine_handle<Promise> mPrevious;   //前一个协程
};


/*
* 协程函数的返回值，这是一个Awaitable对象
* 
* 
*/
struct WorldTask
{
	using promise_type = Promise;

	WorldTask(std::coroutine_handle<promise_type> c) :mCoroutine(c) {}

	~WorldTask()
	{
		mCoroutine.destroy();
	}

	struct WorldAwaiter
	{
		bool await_ready() const noexcept { return false; }

		//这里的coroutine是调用co_await的那个协程
		std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> coroutine) const noexcept {
			mCoroutine.promise().mPrevious = coroutine;   //world这个协程的前一个协程记录下来
			return mCoroutine;    //立即执行本协程，即world协程
		}

		void await_resume() const noexcept {
			debug(), "WorldAwaiter resume";
		}

		std::coroutine_handle<promise_type> mCoroutine;
	};

	/*
	* 转换成WorldAwaiter
	*/
	auto operator co_await() {
		return WorldAwaiter(mCoroutine);
	}

	std::coroutine_handle<promise_type> mCoroutine;   //world这个协程
};

/*
* 协程world
* 
*/
WorldTask world()
{
	debug(), "world 3";
	co_yield 3;   //挂起world协程，恢复hello协程的执行，经过：promise.yield_value(3) -> PreviousAwaiter.await_ready() -> PreviousAwaiter.await_suspend()该接口中判断是否有前一个协程，有就恢复，没有则只挂起本协程

	debug(), "world 4";
	co_yield 4;   //挂起world协程，恢复hello协程的执行，经过：promise.yield_value(3) -> PreviousAwaiter.await_ready() -> PreviousAwaiter.await_suspend()该接口中判断是否有前一个协程，有就恢复，没有则只挂起本协程

	co_return;
}


/*
* 协程函数的返回值，其内部必须要有promise_type这个类型
*
*
*/
struct Task
{
	using promise_type = Promise;

	Task(std::coroutine_handle<promise_type> c) :mCoroutine(c) {}

	~Task()
	{
		mCoroutine.destroy();
	}

	std::coroutine_handle<promise_type> mCoroutine;
};

/*
* 协程hello
*
*/
Task hello()
{
	//这里又创建了一个world协程，即在hello这个协程内部又创建了一个world协程，这个协程也是创建后立即挂起返回的
	debug(), "before generate world coroutine";
	auto tt = world();
	debug(), "after generate world coroutine";

	//tt是一个Awaitable对象，其内部实现了operator co_await可以转换成WorldAwaiter
	co_await tt;   //挂起hello协程，切换到了world协程，并在world协程中记录了hello协程的句柄，当world协程挂起的时候恢复hello协程的执行
	debug(), "hello recv world ret val:", tt.mCoroutine.promise().mRetVal;
	co_await tt;   //挂起hello协程，切换到了world协程，并在world协程中记录了hello协程的句柄，当world协程挂起的时候恢复hello协程的执行
	debug(), "hello recv world ret val", tt.mCoroutine.promise().mRetVal;


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