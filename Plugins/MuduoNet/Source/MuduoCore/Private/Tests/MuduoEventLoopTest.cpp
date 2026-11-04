#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTLS.h"
#include "Misc/AutomationTest.h"

#include "Async/Async.h"
#include "EventLoop.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMuduoEventLoopTest, "MuduoNet.Core.EventLoop",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMuduoEventLoopTest::RunTest(const FString& Parameters)
{
	using namespace mnet;

	FEventLoop Loop;
	const uint32 LoopThreadId = FPlatformTLS::GetCurrentThreadId();

	// 1. 在 loop 线程调用 RunInLoop 应当立即执行
	bool bImmediateExecuted = false;
	Loop.RunInLoop([&bImmediateExecuted]()
	{
		bImmediateExecuted = true;
	});
	TestTrue(TEXT("RunInLoop 在 loop 线程立即执行"), bImmediateExecuted);

	// 2. 跨线程 QueueInLoop：应当被唤醒并从 loop 线程执行
	std::atomic<bool> bCrossThreadExecuted{ false };
	std::atomic<uint32> CrossThreadId{ 0 };
	TFuture<void> CrossThreadTask = Async(EAsyncExecution::Thread, [&Loop, &bCrossThreadExecuted, &CrossThreadId]()
	{
		Loop.QueueInLoop([&bCrossThreadExecuted, &CrossThreadId]()
		{
			bCrossThreadExecuted.store(true);
			CrossThreadId.store(FPlatformTLS::GetCurrentThreadId());
		});
	});

	// 3. 定时器：单次 + 重复 + 回调内取消自己
	std::atomic<int32> AfterFired{ 0 };
	std::atomic<int32> RepeatFired{ 0 };
	std::atomic<bool> bRepeatCancelled{ false };

	Loop.RunAfter(0.02, [&AfterFired]()
	{
		AfterFired.fetch_add(1);
	});

	FTimerId RepeatId;
	RepeatId = Loop.RunEvery(0.02, [&RepeatFired, &bRepeatCancelled, &Loop, &RepeatId]()
	{
		if (RepeatFired.fetch_add(1) + 1 >= 3)
		{
			Loop.CancelTimer(RepeatId);
			bRepeatCancelled.store(true);
		}
	});

	// 4. 条件满足后退出；另有硬超时兜底
	FTimerId CheckId;
	CheckId = Loop.RunEvery(0.01, [&]()
	{
		if (bCrossThreadExecuted.load() && AfterFired.load() >= 1 && RepeatFired.load() >= 3)
		{
			Loop.CancelTimer(CheckId);
			Loop.Quit();
		}
	});
	Loop.RunAfter(3.0, [&Loop]()
	{
		Loop.Quit();
	});

	const double StartTime = FPlatformTime::Seconds();
	Loop.Loop();
	const double ElapsedSeconds = FPlatformTime::Seconds() - StartTime;
	CrossThreadTask.Wait();

	TestTrue(TEXT("跨线程任务已执行"), bCrossThreadExecuted.load());
	TestEqual(TEXT("跨线程任务在 loop 线程执行"), CrossThreadId.load(), LoopThreadId);
	TestEqual(TEXT("单次定时器只触发一次"), AfterFired.load(), static_cast<int32>(1));
	TestTrue(TEXT("重复定时器至少触发 3 次"), RepeatFired.load() >= 3);
	TestTrue(TEXT("重复定时器在回调内取消成功"), bRepeatCancelled.load());
	TestTrue(FString::Printf(TEXT("loop 及时退出（耗时 %.2fs）"), ElapsedSeconds), ElapsedSeconds < 2.0);
	TestTrue(TEXT("loop 至少迭代过一次"), Loop.GetIteration() > 0);
	TestEqual(TEXT("退出后没有残留任务"), Loop.GetPendingTaskCount(), static_cast<int32>(0));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
