#include "Platform/Wakeup.h"

#include "MuduoNetLog.h"
#include "Platform/PlatformSocketIncludes.h"

#if MUDUONET_PLATFORM_LINUX

#include <sys/eventfd.h>

namespace mnet
{
	FWakeup::FWakeup()
	{
		const int32 Fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
		if (Fd < 0)
		{
			UE_LOG(LogMuduoNet, Error, TEXT("MuduoNet: eventfd 创建失败，errno=%d"), errno);
			return;
		}
		EventFd.Reset(Fd);
	}

	FWakeup::~FWakeup() = default;

	bool FWakeup::IsValid() const
	{
		return EventFd.IsValid();
	}

	FSocketValue FWakeup::GetReadHandle() const
	{
		return EventFd.Get();
	}

	void FWakeup::Notify()
	{
		if (!EventFd.IsValid())
		{
			return;
		}

		const uint64 One = 1;
		// 事件计数累加，不会丢失唤醒。
		const ssize_t Written = ::write(EventFd.Get(), &One, sizeof(One));
		(void)Written;
	}

	void FWakeup::Drain()
	{
		if (!EventFd.IsValid())
		{
			return;
		}

		uint64 Value = 0;
		const ssize_t Read = ::read(EventFd.Get(), &Value, sizeof(Value));
		(void)Read;
	}
}

#endif // MUDUONET_PLATFORM_LINUX
