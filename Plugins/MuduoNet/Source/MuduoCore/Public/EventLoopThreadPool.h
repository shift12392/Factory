#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "Templates/UniquePtr.h"

namespace mnet
{
	class FEventLoop;
	class FEventLoopThread;

	/**
	 * IO 线程池（muduo EventLoopThreadPool 的移植）。
	 * NumThreads == 0 时不创建线程，全部连接留在 base loop（muduo 的默认行为）。
	 */
	class MUDUOCORE_API FEventLoopThreadPool
	{
	public:
		using FThreadInitCallback = TFunction<void(FEventLoop*)>;

		FEventLoopThreadPool(FEventLoop* InBaseLoop, const FString& InName);
		~FEventLoopThreadPool();

		FEventLoopThreadPool(const FEventLoopThreadPool&) = delete;
		FEventLoopThreadPool& operator=(const FEventLoopThreadPool&) = delete;

		void SetThreadNum(int32 InNumThreads);
		void Start(const FThreadInitCallback& InInitCallback = nullptr);

		/** 轮询分配，未启动或线程数为 0 时返回 base loop。 */
		FEventLoop* GetNextLoop();
		/** 同一 hash 永远落到同一个 loop。 */
		FEventLoop* GetLoopForHash(uint64 HashCode) const;

		const TArray<FEventLoop*>& GetAllLoops() const { return Loops; }
		bool IsStarted() const { return bStarted; }
		const FString& GetName() const { return Name; }

		void Stop();

	private:
		FEventLoop* BaseLoop = nullptr;
		FString Name;
		int32 NumThreads = 0;
		int32 Next = 0;
		bool bStarted = false;
		TArray<TUniquePtr<FEventLoopThread>> Threads;
		TArray<FEventLoop*> Loops;
	};
}
