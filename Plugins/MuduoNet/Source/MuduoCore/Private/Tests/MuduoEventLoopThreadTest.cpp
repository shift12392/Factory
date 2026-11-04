#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTLS.h"
#include "Misc/AutomationTest.h"
#include "Templates/Function.h"

#include "EventLoop.h"
#include "EventLoopThread.h"
#include "EventLoopThreadPool.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	bool WaitUntilMuduo(const TFunction<bool()>& Predicate, double TimeoutSeconds)
	{
		const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
		for (;;)
		{
			if (Predicate())
			{
				return true;
			}
			if (FPlatformTime::Seconds() >= Deadline)
			{
				return false;
			}
			FPlatformProcess::Sleep(0.001f);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMuduoEventLoopThreadTest, "MuduoNet.Core.EventLoopThread",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMuduoEventLoopThreadTest::RunTest(const FString& Parameters)
{
	using namespace mnet;

	FEventLoopThread Thread(TEXT("MuduoNet Test IO"));
	FEventLoop* Loop = Thread.StartLoop();
	TestTrue(TEXT("启动 IO 线程"), Loop != nullptr);
	if (Loop == nullptr)
	{
		return false;
	}

	TestTrue(TEXT("线程名正确"), Thread.GetThreadName() == TEXT("MuduoNet Test IO"));

	// loop 运行在独立线程
	std::atomic<uint32> LoopThreadId{ 0 };
	std::atomic<bool> bExecuted{ false };
	Loop->RunInLoop([&LoopThreadId, &bExecuted]()
	{
		LoopThreadId.store(FPlatformTLS::GetCurrentThreadId());
		bExecuted.store(true);
	});

	TestTrue(TEXT("任务已执行"), WaitUntilMuduo([&bExecuted]() { return bExecuted.load(); }, 2.0));
	TestTrue(TEXT("任务不在测试线程执行"), LoopThreadId.load() != FPlatformTLS::GetCurrentThreadId());

	// 定时器跑在 IO 线程
	std::atomic<int32> TimerCount{ 0 };
	std::atomic<uint32> TimerThreadId{ 0 };
	FTimerId TimerId = Loop->RunEvery(0.01, [&TimerCount, &TimerThreadId]()
	{
		TimerThreadId.store(FPlatformTLS::GetCurrentThreadId());
		TimerCount.fetch_add(1);
	});
	TestTrue(TEXT("定时器至少触发两次"), WaitUntilMuduo([&TimerCount]() { return TimerCount.load() >= 2; }, 2.0));
	Loop->CancelTimer(TimerId);
	TestTrue(TEXT("定时器在 IO 线程执行"), TimerThreadId.load() == LoopThreadId.load());

	// Stop 可重复调用，停止后 loop 指针失效，不再使用
	Thread.Stop();
	Thread.Stop();
	TestFalse(TEXT("停止后不再处于启动状态"), Thread.IsStarted());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMuduoEventLoopThreadPoolTest, "MuduoNet.Core.EventLoopThreadPool",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMuduoEventLoopThreadPoolTest::RunTest(const FString& Parameters)
{
	using namespace mnet;

	FEventLoop BaseLoop;

	FEventLoopThreadPool Pool(&BaseLoop, TEXT("MuduoNet Test Pool"));
	Pool.SetThreadNum(3);
	Pool.Start();

	TestTrue(TEXT("线程池已启动"), Pool.IsStarted());
	TestEqual(TEXT("创建了 3 个 IO loop"), Pool.GetAllLoops().Num(), static_cast<int32>(3));

	const TArray<FEventLoop*>& Loops = Pool.GetAllLoops();
	TSet<uint32> ThreadIds;
	for (FEventLoop* Loop : Loops)
	{
		std::atomic<uint32> LoopThreadId{ 0 };
		std::atomic<bool> bDone{ false };
		Loop->RunInLoop([&LoopThreadId, &bDone]()
		{
			LoopThreadId.store(FPlatformTLS::GetCurrentThreadId());
			bDone.store(true);
		});

		WaitUntilMuduo([&bDone]() { return bDone.load(); }, 2.0);
		ThreadIds.Add(LoopThreadId.load());
	}

	TestEqual(TEXT("每个 loop 运行在不同线程"), ThreadIds.Num(), static_cast<int32>(3));

	// 轮询分配
	FEventLoop* First = Pool.GetNextLoop();
	FEventLoop* Second = Pool.GetNextLoop();
	FEventLoop* Third = Pool.GetNextLoop();
	FEventLoop* Fourth = Pool.GetNextLoop();

	TestTrue(TEXT("相邻两次分配不同"), First != Second && Second != Third);
	TestTrue(TEXT("轮询回到起点"), First == Fourth);
	TestTrue(TEXT("GetLoopForHash 稳定"), Pool.GetLoopForHash(42) == Pool.GetLoopForHash(42));

	Pool.Stop();
	TestFalse(TEXT("线程池已停止"), Pool.IsStarted());

	// 线程数为 0 时退回 base loop
	FEventLoopThreadPool SingleThreadPool(&BaseLoop, TEXT("MuduoNet Test Single"));
	SingleThreadPool.SetThreadNum(0);
	SingleThreadPool.Start();
	TestTrue(TEXT("0 线程时返回 base loop"), SingleThreadPool.GetNextLoop() == &BaseLoop);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
