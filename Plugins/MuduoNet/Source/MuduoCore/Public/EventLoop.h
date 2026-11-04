#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Timer.h"
#include "Templates/Function.h"
#include "Templates/UniquePtr.h"

#include <atomic>

namespace mnet
{
	class FChannel;
	class FPoller;
	class FTimerQueue;
	class FWakeup;

	/**
	 * Reactor 核心（muduo EventLoop 的移植）：一个线程一个 loop。
	 *
	 * 组成：
	 * - FPoller：epoll(Linux) / WSAPoll(Windows)，水平触发
	 * - FWakeup：eventfd(Linux) / 环回 UDP 对(Windows)，用于跨线程唤醒
	 * - FTimerQueue：最早期定时器决定 poll 超时
	 *
	 * 线程约定：
	 * - Loop() 必须在创建对象所在的线程调用。
	 * - RunInLoop / QueueInLoop / 定时器 / Quit 可以从任意线程调用。
	 * - Channel 的注册与注销只能在 loop 线程执行。
	 */
	class MUDUOCORE_API FEventLoop
	{
	public:
		using FFunctor = TFunction<void()>;

		FEventLoop();
		~FEventLoop();

		FEventLoop(const FEventLoop&) = delete;
		FEventLoop& operator=(const FEventLoop&) = delete;

		/** 事件循环，直到 Quit() 被调用。 */
		void Loop();
		/** 线程安全。 */
		void Quit();

		bool IsInLoopThread() const;
		void AssertInLoopThread() const;

		/** 在 loop 线程则立即执行，否则投递。线程安全。 */
		void RunInLoop(FFunctor Callback);
		/** 总是投递到队列，并唤醒 loop。线程安全。 */
		void QueueInLoop(FFunctor Callback);
		int32 GetPendingTaskCount() const;
		void WakeUp();

		double GetPollReturnTime() const { return PollReturnTime; }
		int64 GetIteration() const { return Iteration; }

		/** 上一次「poll 返回到待办任务执行完」的耗时（ms），衡量事件处理延迟。 */
		double GetLastBusyMs() const { return LastBusyMs.load(); }
		/** 历史最大忙碌耗时（ms）。 */
		double GetMaxBusyMs() const { return MaxBusyMs.load(); }

		FTimerId RunAt(double WhenSeconds, TFunction<void()> Callback);
		FTimerId RunAfter(double DelaySeconds, TFunction<void()> Callback);
		FTimerId RunEvery(double IntervalSeconds, TFunction<void()> Callback);
		void CancelTimer(FTimerId Id);

		void UpdateChannel(FChannel* Channel);
		void RemoveChannel(FChannel* Channel);

		FPoller* GetPoller() const { return Poller.Get(); }
		FTimerQueue* GetTimerQueue() const { return TimerQueue.Get(); }

		static FEventLoop* GetEventLoopOfCurrentThread();

	private:
		void DoPendingFunctors();

		static constexpr int32 kMaxPollTimeoutMs = 10000;

		uint32 ThreadId = 0;

		std::atomic<bool> bLooping{ false };
		std::atomic<bool> bQuit{ false };
		std::atomic<bool> bEventHandling{ false };
		std::atomic<bool> bCallingPendingFunctors{ false };

		int64 Iteration = 0;
		double PollReturnTime = 0.0;
		std::atomic<double> LastBusyMs{ 0.0 };
		std::atomic<double> MaxBusyMs{ 0.0 };

		TUniquePtr<FPoller> Poller;
		TUniquePtr<FWakeup> Wakeup;
		TUniquePtr<FChannel> WakeupChannel;
		TUniquePtr<FTimerQueue> TimerQueue;

		TArray<FChannel*> ActiveChannels;
		FChannel* CurrentActiveChannel = nullptr;

		mutable FCriticalSection Mutex;
		TArray<FFunctor> PendingFunctors;
	};
}
