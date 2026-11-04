#include "Net/TimerQueue.h"

#include "EventLoop.h"

namespace mnet
{
	FTimerQueue::FTimerQueue(FEventLoop* InLoop)
		: Loop(InLoop)
	{
		checkf(Loop != nullptr, TEXT("MuduoNet: FTimerQueue 必须绑定 EventLoop"));
	}

	FTimerQueue::~FTimerQueue()
	{
		SortedTimers.Reset();
		ActiveTimers.Reset();
		CancelledDuringCallbacks.Reset();
	}

	FTimerId FTimerQueue::AddTimer(TFunction<void()> Callback, double ExpirationSeconds, double IntervalSeconds)
	{
		const int64 Sequence = ++NextSequence;
		TSharedPtr<FTimer> Timer = MakeShared<FTimer>(MoveTemp(Callback), ExpirationSeconds, IntervalSeconds, Sequence);

		Loop->RunInLoop([this, Timer]()
		{
			AddTimerInLoop(Timer);
		});

		return FTimerId(Timer.Get(), Sequence);
	}

	void FTimerQueue::Cancel(FTimerId Id)
	{
		if (!Id.IsValid())
		{
			return;
		}

		Loop->RunInLoop([this, Id]()
		{
			CancelInLoop(Id);
		});
	}

	void FTimerQueue::HandleExpired(double NowSeconds)
	{
		int32 ExpiredCount = 0;
		while (ExpiredCount < SortedTimers.Num() && SortedTimers[ExpiredCount]->GetExpiration() <= NowSeconds)
		{
			++ExpiredCount;
		}

		if (ExpiredCount == 0)
		{
			return;
		}

		TArray<TSharedPtr<FTimer>> Expired;
		Expired.Reserve(ExpiredCount);
		Expired.Append(SortedTimers.GetData(), ExpiredCount);
		SortedTimers.RemoveAt(0, ExpiredCount);

		// 先移出存活表：这样回调里 Cancel 自己时，不会被误判成「仍然存活」。
		for (const TSharedPtr<FTimer>& Timer : Expired)
		{
			ActiveTimers.Remove(Timer->GetSequence());
		}

		bRunningCallbacks = true;
		for (const TSharedPtr<FTimer>& Timer : Expired)
		{
			Timer->Run();

			const bool bCancelled = CancelledDuringCallbacks.Remove(Timer->GetSequence()) > 0;
			if (Timer->IsRepeat() && !bCancelled)
			{
				Timer->Restart(NowSeconds);
				InsertSorted(Timer);
				ActiveTimers.Add(Timer->GetSequence(), Timer);
			}
		}
		bRunningCallbacks = false;
		CancelledDuringCallbacks.Reset();
	}

	int32 FTimerQueue::GetNextTimeoutMs(double NowSeconds, int32 MaxTimeoutMs) const
	{
		if (SortedTimers.Num() == 0)
		{
			return MaxTimeoutMs;
		}

		const double RemainingSeconds = SortedTimers[0]->GetExpiration() - NowSeconds;
		if (RemainingSeconds <= 0.0)
		{
			return 0;
		}

		const double RemainingMs = RemainingSeconds * 1000.0;
		if (RemainingMs >= static_cast<double>(MaxTimeoutMs))
		{
			return MaxTimeoutMs;
		}

		return FMath::Clamp(FMath::CeilToInt32(static_cast<float>(RemainingMs)), 1, MaxTimeoutMs);
	}

	void FTimerQueue::AddTimerInLoop(const TSharedPtr<FTimer>& Timer)
	{
		InsertSorted(Timer);
		ActiveTimers.Add(Timer->GetSequence(), Timer);
	}

	void FTimerQueue::CancelInLoop(FTimerId Id)
	{
		const int64 Sequence = Id.GetSequence();
		if (const TSharedPtr<FTimer>* Found = ActiveTimers.Find(Sequence))
		{
			const TSharedPtr<FTimer> Timer = *Found;
			ActiveTimers.Remove(Sequence);
			SortedTimers.RemoveSingle(Timer);
			return;
		}

		if (bRunningCallbacks)
		{
			// 正在执行的回调里取消了自己：记录下来，回调结束后不再续期。
			CancelledDuringCallbacks.Add(Sequence);
		}
	}

	void FTimerQueue::InsertSorted(const TSharedPtr<FTimer>& Timer)
	{
		int32 Low = 0;
		int32 High = SortedTimers.Num();
		while (Low < High)
		{
			const int32 Mid = Low + (High - Low) / 2;
			if (IsEarlier(SortedTimers[Mid], Timer))
			{
				Low = Mid + 1;
			}
			else
			{
				High = Mid;
			}
		}

		SortedTimers.Insert(Timer, Low);
	}

	bool FTimerQueue::IsEarlier(const TSharedPtr<FTimer>& Left, const TSharedPtr<FTimer>& Right)
	{
		if (Left->GetExpiration() != Right->GetExpiration())
		{
			return Left->GetExpiration() < Right->GetExpiration();
		}
		return Left->GetSequence() < Right->GetSequence();
	}
}
