#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"
#include "Timer.h"

namespace mnet
{
	class FEventLoop;

	/**
	 * 定时器队列（muduo TimerQueue 的移植）。
	 *
	 * 与 muduo 的差异：muduo 用 timerfd 作为唤醒源，这里统一用「poller 超时 + 有序表」，
	 * 两个平台行为完全一致，也不需要 Windows 上不存在的 timerfd。
	 *
	 * 线程模型：
	 * - AddTimer / Cancel 可以从任意线程调用，内部通过 EventLoop::RunInLoop 投递。
	 * - HandleExpired / GetNextTimeoutMs 只能在 IO 线程调用。
	 */
	class MUDUOCORE_API FTimerQueue
	{
	public:
		explicit FTimerQueue(FEventLoop* InLoop);
		~FTimerQueue();

		FTimerQueue(const FTimerQueue&) = delete;
		FTimerQueue& operator=(const FTimerQueue&) = delete;

		/** 线程安全。IntervalSeconds > 0 表示重复定时器。 */
		FTimerId AddTimer(TFunction<void()> Callback, double ExpirationSeconds, double IntervalSeconds = 0.0);
		/** 线程安全；对已经触发的定时器取消是安全的（不会重复触发）。 */
		void Cancel(FTimerId Id);

		/** 执行所有到期定时器，只能在 IO 线程调用。 */
		void HandleExpired(double NowSeconds);

		/** 距离下一个定时器的毫秒数（>=0），没有定时器时返回 MaxTimeoutMs。 */
		int32 GetNextTimeoutMs(double NowSeconds, int32 MaxTimeoutMs) const;

		int32 GetTimerCount() const { return ActiveTimers.Num(); }

	private:
		void AddTimerInLoop(const TSharedPtr<FTimer>& Timer);
		void CancelInLoop(FTimerId Id);
		void InsertSorted(const TSharedPtr<FTimer>& Timer);
		static bool IsEarlier(const TSharedPtr<FTimer>& Left, const TSharedPtr<FTimer>& Right);

		FEventLoop* Loop = nullptr;
		/** 按 (Expiration, Sequence) 升序。 */
		TArray<TSharedPtr<FTimer>> SortedTimers;
		/** 存活定时器，用于取消和重复定时器续期。 */
		TMap<int64, TSharedPtr<FTimer>> ActiveTimers;
		/** 回调执行期间被取消的定时器编号。 */
		TSet<int64> CancelledDuringCallbacks;
		bool bRunningCallbacks = false;
		int64 NextSequence = 0;
	};
}
