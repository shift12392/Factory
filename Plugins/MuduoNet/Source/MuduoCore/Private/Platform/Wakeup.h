#pragma once

#include "CoreMinimal.h"
#include "SocketHandle.h"

namespace mnet
{
	/**
	 * 唤醒源：让处于 poll 等待中的 loop 立刻返回。
	 *
	 * - Linux：eventfd（muduo 原方案）
	 * - Windows：环回 UDP socketpair（WSAPoll 没有 eventfd 可用）
	 *
	 * Notify() 可从任意线程调用；Drain() 只能在 loop 线程调用。
	 */
	class MUDUOCORE_API FWakeup
	{
	public:
		FWakeup();
		~FWakeup();

		FWakeup(const FWakeup&) = delete;
		FWakeup& operator=(const FWakeup&) = delete;

		bool IsValid() const;
		/** 注册到 poller 的读端句柄。 */
		FSocketValue GetReadHandle() const;
		void Notify();
		void Drain();

	private:
#if MUDUONET_PLATFORM_WINDOWS
		FSocketHandle ServerSocket;
		FSocketHandle ClientSocket;
#else
		FSocketHandle EventFd;
#endif
	};
}
