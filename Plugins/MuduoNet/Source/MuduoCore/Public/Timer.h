#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

namespace mnet
{
	class FTimer;

	/** 定时器句柄：只用于取消，不持有 Timer 所有权。 */
	class MUDUOCORE_API FTimerId
	{
	public:
		FTimerId() = default;
		FTimerId(const FTimer* InTimer, int64 InSequence)
			: Timer(InTimer)
			, Sequence(InSequence)
		{
		}

		bool IsValid() const { return Timer != nullptr; }
		const FTimer* GetTimer() const { return Timer; }
		int64 GetSequence() const { return Sequence; }

	private:
		const FTimer* Timer = nullptr;
		int64 Sequence = 0;
	};

	/**
	 * 单个定时器。
	 * 生命周期由 FTimerQueue 通过 TSharedPtr 管理，FTimerId 只保存指针用于身份比较，不解引用。
	 */
	class MUDUOCORE_API FTimer
	{
	public:
		FTimer(TFunction<void()> InCallback, double InExpiration, double InInterval, int64 InSequence)
			: Callback(MoveTemp(InCallback))
			, Expiration(InExpiration)
			, Interval(InInterval)
			, Sequence(InSequence)
		{
		}

		double GetExpiration() const { return Expiration; }
		double GetInterval() const { return Interval; }
		int64 GetSequence() const { return Sequence; }

		bool IsRepeat() const { return Interval > 0.0; }

		/** 重复定时器以上一次触发时间为基准重新计时（与 muduo 一致）。 */
		void Restart(double NowSeconds) { Expiration = NowSeconds + Interval; }

		void Run()
		{
			if (Callback)
			{
				Callback();
			}
		}

	private:
		TFunction<void()> Callback;
		double Expiration = 0.0;
		double Interval = 0.0;
		int64 Sequence = 0;
	};
}
