#include "EventLoop.h"

#include "HAL/PlatformTLS.h"
#include "HAL/PlatformTime.h"
#include "MuduoNetLog.h"
#include "Net/Channel.h"
#include "Net/Poller.h"
#include "Net/TimerQueue.h"
#include "Platform/Wakeup.h"

namespace mnet
{
	namespace
	{
		// DLL 导出的类不能持有 thread_local 静态成员（MSVC C2492），因此放在匿名命名空间。
		thread_local FEventLoop* GCurrentLoopInThisThread = nullptr;
	}

	FEventLoop::FEventLoop()
		: ThreadId(FPlatformTLS::GetCurrentThreadId())
	{
		checkf(GCurrentLoopInThisThread == nullptr,
			TEXT("MuduoNet: 一个线程只能有一个 EventLoop（当前线程已存在 %p）"), GCurrentLoopInThisThread);

		Poller = FPoller::CreateDefaultPoller();
		checkf(Poller.IsValid(), TEXT("MuduoNet: 创建 Poller 失败"));

		Wakeup = MakeUnique<FWakeup>();
		if (!Wakeup->IsValid())
		{
			// 没有唤醒源时 loop 仍可用，只是跨线程投递要等 poll 超时才会被处理。
			UE_LOG(LogMuduoNet, Error, TEXT("MuduoNet: 唤醒源创建失败，跨线程任务的响应会退化"));
		}
		else
		{
			WakeupChannel = MakeUnique<FChannel>(this, Wakeup->GetReadHandle());
			WakeupChannel->SetReadCallback([this]()
			{
				Wakeup->Drain();
			});
			WakeupChannel->EnableReading();
		}

		TimerQueue = MakeUnique<FTimerQueue>(this);
		GCurrentLoopInThisThread = this;
	}

	FEventLoop::~FEventLoop()
	{
		checkf(!bLooping.load(), TEXT("MuduoNet: EventLoop 不能在 Loop() 运行期间析构"));

		if (GCurrentLoopInThisThread == this)
		{
			GCurrentLoopInThisThread = nullptr;
		}

		// 先摘掉唤醒通道，再销毁 poller，避免 poller 里留下悬空 Channel。
		if (WakeupChannel.IsValid())
		{
			WakeupChannel->DisableAll();
			WakeupChannel->RemoveFromPoller();
			WakeupChannel.Reset();
		}

		TimerQueue.Reset();
		Wakeup.Reset();
		Poller.Reset();
	}

	void FEventLoop::Loop()
	{
		AssertInLoopThread();
		checkf(!bLooping.load(), TEXT("MuduoNet: EventLoop 不能重复 Loop()"));
		checkf(!bQuit.load(), TEXT("MuduoNet: 已经 Quit 的 EventLoop 不能再次 Loop()"));

		bLooping.store(true);

		while (!bQuit.load())
		{
			ActiveChannels.Reset();

			double TimeStampAfterPoll = 0.0;

			const double BeforePoll = FPlatformTime::Seconds();
			const int32 TimeoutMs = TimerQueue->GetNextTimeoutMs(BeforePoll, kMaxPollTimeoutMs);

			Poller->Poll(TimeoutMs, ActiveChannels);
			TimeStampAfterPoll = FPlatformTime::Seconds();
			PollReturnTime = TimeStampAfterPoll;
			++Iteration;

			bEventHandling.store(true);
			for (FChannel* Channel : ActiveChannels)
			{
				CurrentActiveChannel = Channel;
				Channel->HandleEvent();
			}
			CurrentActiveChannel = nullptr;
			bEventHandling.store(false);

			TimerQueue->HandleExpired(FPlatformTime::Seconds());

			DoPendingFunctors();

			// 只统计「事件处理」耗时，不含 poll 等待，便于判断 loop 是否被回调拖慢。
			const double BusyMs = (FPlatformTime::Seconds() - TimeStampAfterPoll) * 1000.0;
			LastBusyMs.store(BusyMs);
			if (BusyMs > MaxBusyMs.load())
			{
				MaxBusyMs.store(BusyMs);
			}
		}

		bLooping.store(false);
	}

	void FEventLoop::Quit()
	{
		bQuit.store(true);

		// 已经在 loop 里就不需要唤醒；否则 poll 可能还在阻塞。
		if (!IsInLoopThread())
		{
			WakeUp();
		}
	}

	bool FEventLoop::IsInLoopThread() const
	{
		return ThreadId == FPlatformTLS::GetCurrentThreadId();
	}

	void FEventLoop::AssertInLoopThread() const
	{
		checkf(IsInLoopThread(),
			TEXT("MuduoNet: 该操作必须在 EventLoop 所在线程执行（loop 线程 %u，当前线程 %u）"),
			ThreadId, FPlatformTLS::GetCurrentThreadId());
	}

	void FEventLoop::RunInLoop(FFunctor Callback)
	{
		if (IsInLoopThread())
		{
			if (Callback)
			{
				Callback();
			}
			return;
		}

		QueueInLoop(MoveTemp(Callback));
	}

	void FEventLoop::QueueInLoop(FFunctor Callback)
	{
		{
			FScopeLock Lock(&Mutex);
			PendingFunctors.Add(MoveTemp(Callback));
		}

		// 回调里再投递时也要唤醒：pending functors 是在本轮事件处理完之后才执行的。
		if (!IsInLoopThread() || bCallingPendingFunctors.load())
		{
			WakeUp();
		}
	}

	int32 FEventLoop::GetPendingTaskCount() const
	{
		FScopeLock Lock(&Mutex);
		return PendingFunctors.Num();
	}

	void FEventLoop::WakeUp()
	{
		if (Wakeup.IsValid())
		{
			Wakeup->Notify();
		}
	}

	FTimerId FEventLoop::RunAt(double WhenSeconds, TFunction<void()> Callback)
	{
		return TimerQueue->AddTimer(MoveTemp(Callback), WhenSeconds, 0.0);
	}

	FTimerId FEventLoop::RunAfter(double DelaySeconds, TFunction<void()> Callback)
	{
		return TimerQueue->AddTimer(MoveTemp(Callback), FPlatformTime::Seconds() + DelaySeconds, 0.0);
	}

	FTimerId FEventLoop::RunEvery(double IntervalSeconds, TFunction<void()> Callback)
	{
		checkf(IntervalSeconds > 0.0, TEXT("MuduoNet: RunEvery 的间隔必须大于 0"));
		return TimerQueue->AddTimer(MoveTemp(Callback), FPlatformTime::Seconds() + IntervalSeconds, IntervalSeconds);
	}

	void FEventLoop::CancelTimer(FTimerId Id)
	{
		TimerQueue->Cancel(Id);
	}

	void FEventLoop::UpdateChannel(FChannel* Channel)
	{
		AssertInLoopThread();
		Poller->UpdateChannel(Channel);
	}

	void FEventLoop::RemoveChannel(FChannel* Channel)
	{
		AssertInLoopThread();
		Poller->RemoveChannel(Channel);
	}

	FEventLoop* FEventLoop::GetEventLoopOfCurrentThread()
	{
		return GCurrentLoopInThisThread;
	}

	void FEventLoop::DoPendingFunctors()
	{
		TArray<FFunctor> Functors;
		{
			FScopeLock Lock(&Mutex);
			Swap(Functors, PendingFunctors);
		}

		bCallingPendingFunctors.store(true);
		for (FFunctor& Functor : Functors)
		{
			if (Functor)
			{
				Functor();
			}
		}
		bCallingPendingFunctors.store(false);
	}
}
