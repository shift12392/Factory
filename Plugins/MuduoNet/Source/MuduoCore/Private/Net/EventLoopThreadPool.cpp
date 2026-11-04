#include "EventLoopThreadPool.h"

#include "EventLoop.h"
#include "EventLoopThread.h"
#include "MuduoNetLog.h"

namespace mnet
{
	FEventLoopThreadPool::FEventLoopThreadPool(FEventLoop* InBaseLoop, const FString& InName)
		: BaseLoop(InBaseLoop)
		, Name(InName)
	{
	}

	FEventLoopThreadPool::~FEventLoopThreadPool()
	{
		Stop();
	}

	void FEventLoopThreadPool::SetThreadNum(int32 InNumThreads)
	{
		checkf(!bStarted, TEXT("MuduoNet: 线程池启动后不能修改线程数（%s）"), *Name);
		NumThreads = FMath::Max(0, InNumThreads);
	}

	void FEventLoopThreadPool::Start(const FThreadInitCallback& InInitCallback)
	{
		checkf(!bStarted, TEXT("MuduoNet: 线程池不能重复启动（%s）"), *Name);
		bStarted = true;

		if (NumThreads == 0)
		{
			// 单线程模式：所有 IO 都在 base loop 上。
			if (BaseLoop != nullptr)
			{
				Loops.Add(BaseLoop);
			}
			return;
		}

		Loops.Reserve(NumThreads);
		Threads.Reserve(NumThreads);

		for (int32 Index = 0; Index < NumThreads; ++Index)
		{
			TUniquePtr<FEventLoopThread> Thread = MakeUnique<FEventLoopThread>(FString::Printf(TEXT("%s-%d"), *Name, Index));
			FEventLoop* Loop = Thread->StartLoop(InInitCallback);
			if (Loop == nullptr)
			{
				UE_LOG(LogMuduoNet, Error, TEXT("MuduoNet: 线程池启动第 %d 个 IO 线程失败（%s）"), Index, *Name);
				continue;
			}

			Loops.Add(Loop);
			Threads.Add(MoveTemp(Thread));
		}
	}

	FEventLoop* FEventLoopThreadPool::GetNextLoop()
	{
		if (Loops.Num() == 0)
		{
			return BaseLoop;
		}

		FEventLoop* Loop = Loops[Next];
		Next = (Next + 1) % Loops.Num();
		return Loop;
	}

	FEventLoop* FEventLoopThreadPool::GetLoopForHash(uint64 HashCode) const
	{
		if (Loops.Num() == 0)
		{
			return BaseLoop;
		}

		const int32 Index = static_cast<int32>(HashCode % static_cast<uint64>(Loops.Num()));
		return Loops[Index];
	}

	void FEventLoopThreadPool::Stop()
	{
		Threads.Reset();
		Loops.Reset();
		Next = 0;
		bStarted = false;
	}
}
