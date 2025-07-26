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
* 3.co_await ��Awaitable��Awaiter������Ч��await_ready()��await_suspend()�����Ƿ���𣬹����Ĳ���  
**************************************************************************/


#include "debug.hpp"
#include <coroutine>
#include <chrono>

/*
* Awaiter�������ڲ�����Ҫ��await_ready��await_suspend��await_resume������ֻ���������ܳ�ΪAwaiter
* ����һ���������Awaitable
*
* ���Awaiter�������ǲ�����һֱִ�У�ֱ��Э�̽���
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
		debug(), "Awaiter resume";
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
* Awaiter�������ڲ�����Ҫ��await_ready��await_suspend��await_resume������ֻ���������ܳ�ΪAwaiter
* 
* ���Awaiter
* 
*/
struct PreviousAwaiter
{
	bool await_ready()
	{
		return false;
	}

	/*
	* 1.���await_suspend��������void ��true����ֱ�ӹ��𣬽�ִ��Ȩ���ظ�������
	* 2.���await_suspend��������false����ֱ�ӻָ�ִ�У������await_resume
	* 3.���await_suspend��������ĳ��Э�̾������ôҲ��ֱ�ӻָ�ִ�У������await_resume
	*/
	std::coroutine_handle<> await_suspend(std::coroutine_handle<> coroutine) const noexcept {
		if (mPrevious)
		{
			return mPrevious;   //ִ��ǰһ��Э��
		}
		else
		{
			return std::noop_coroutine();
		}
	}

	/*
	* Э�ָ̻�ִ�е�ʱ����ã�����ǰ����Э�̹�����ͨ��co_await expr���й���ģ������Э�̴�����ʱ��ͨ��initial_suspend����ģ���ô֮��ָ���ʱ���ǲ���������await_resume������
	*
	*/
	void await_resume() const noexcept {
		debug(), "PreviousAwaiter resume";
	}

	std::coroutine_handle<> mPrevious;
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
	std::coroutine_handle<Promise> mPrevious;   //ǰһ��Э��
};


/*
* Э�̺����ķ���ֵ������һ��Awaitable����
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

		//�����coroutine�ǵ���co_await���Ǹ�Э��
		std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> coroutine) const noexcept {
			mCoroutine.promise().mPrevious = coroutine;   //world���Э�̵�ǰһ��Э�̼�¼����
			return mCoroutine;    //����ִ�б�Э�̣���worldЭ��
		}

		void await_resume() const noexcept {
			debug(), "WorldAwaiter resume";
		}

		std::coroutine_handle<promise_type> mCoroutine;
	};

	/*
	* ת����WorldAwaiter
	*/
	auto operator co_await() {
		return WorldAwaiter(mCoroutine);
	}

	std::coroutine_handle<promise_type> mCoroutine;   //world���Э��
};

/*
* Э��world
* 
*/
WorldTask world()
{
	debug(), "world 3";
	co_yield 3;   //����worldЭ�̣��ָ�helloЭ�̵�ִ�У�������promise.yield_value(3) -> PreviousAwaiter.await_ready() -> PreviousAwaiter.await_suspend()�ýӿ����ж��Ƿ���ǰһ��Э�̣��оͻָ���û����ֻ����Э��

	debug(), "world 4";
	co_yield 4;   //����worldЭ�̣��ָ�helloЭ�̵�ִ�У�������promise.yield_value(3) -> PreviousAwaiter.await_ready() -> PreviousAwaiter.await_suspend()�ýӿ����ж��Ƿ���ǰһ��Э�̣��оͻָ���û����ֻ����Э��

	co_return;
}


/*
* Э�̺����ķ���ֵ�����ڲ�����Ҫ��promise_type�������
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
* Э��hello
*
*/
Task hello()
{
	//�����ִ�����һ��worldЭ�̣�����hello���Э���ڲ��ִ�����һ��worldЭ�̣����Э��Ҳ�Ǵ������������𷵻ص�
	debug(), "before generate world coroutine";
	auto tt = world();
	debug(), "after generate world coroutine";

	//tt��һ��Awaitable�������ڲ�ʵ����operator co_await����ת����WorldAwaiter
	co_await tt;   //����helloЭ�̣��л�����worldЭ�̣�����worldЭ���м�¼��helloЭ�̵ľ������worldЭ�̹����ʱ��ָ�helloЭ�̵�ִ��
	debug(), "hello recv world ret val:", tt.mCoroutine.promise().mRetVal;
	co_await tt;   //����helloЭ�̣��л�����worldЭ�̣�����worldЭ���м�¼��helloЭ�̵ľ������worldЭ�̹����ʱ��ָ�helloЭ�̵�ִ��
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

	/*Э��ֻҪ����������һֱ�ָ�ִ��*/
	while (!t.mCoroutine.done())
		t.mCoroutine.resume();

	debug(), "final result:", t.mCoroutine.promise().mRetVal;   //ͨ��std::coroutine_handle�����ȡpromise����

	return 0;
}