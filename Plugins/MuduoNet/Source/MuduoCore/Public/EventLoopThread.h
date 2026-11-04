#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

#include <atomic>

class FRunnableThread;
class FEvent;

namespace mnet
{
	class FEventLoop;

	/**
	 * 「one loop per thread」的线程封装（muduo EventLoopThread 的移植）。
	 * 内部用 FRunnable + FRunnableThread，EventLoop 对象在 IO 线程的栈上创建与销毁。
	 */
	class MUDUOCORE_API FEventLoopThread
	{
	public:
		using FThreadInitCallback = TFunction<void(FEventLoop*)>;

		explicit FEventLoopThread(const FString& InThreadName = TEXT("MuduoNet-IO"));
		~FEventLoopThread();

		FEventLoopThread(const FEventLoopThread&) = delete;
		FEventLoopThread& operator=(const FEventLoopThread&) = delete;

		/** 启动线程并等待 EventLoop 就绪；失败返回 nullptr。只能调用一次。 */
		FEventLoop* StartLoop(const FThreadInitCallback& InInitCallback = nullptr);

		/** 停止 loop 并等待线程退出，可重复调用。 */
		void Stop();

		bool IsStarted() const { return Thread != nullptr; }
		const FString& GetThreadName() const { return ThreadName; }

	private:
		class FRunnableImpl;

		FString ThreadName;
		FRunnableThread* Thread = nullptr;
		FRunnableImpl* Runnable = nullptr;
		FEvent* ReadyEvent = nullptr;
		std::atomic<FEventLoop*> LoopPtr{ nullptr };
	};
}
