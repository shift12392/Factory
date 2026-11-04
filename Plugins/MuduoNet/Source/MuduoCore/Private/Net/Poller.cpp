#include "Net/Poller.h"

#include "HAL/PlatformTLS.h"
#include "Net/Channel.h"

namespace mnet
{
	FPoller::FPoller()
		: OwningThreadId(FPlatformTLS::GetCurrentThreadId())
	{
	}

	FPoller::~FPoller() = default;

	bool FPoller::HasChannel(const FChannel* Channel) const
	{
		AssertInLoopThread();
		FChannel* const* Found = Channels.Find(Channel->GetFd());
		return Found != nullptr && *Found == Channel;
	}

	void FPoller::AssertInLoopThread() const
	{
		checkf(OwningThreadId == FPlatformTLS::GetCurrentThreadId(),
			TEXT("MuduoNet: Poller 只能在创建它的线程中使用（期望线程 %u，实际线程 %u）"),
			OwningThreadId, FPlatformTLS::GetCurrentThreadId());
	}

	void FPoller::FillActiveChannel(FChannel* Channel, uint32 Revents, FChannelList& OutActiveChannels)
	{
		Channel->SetRevents(Revents);
		OutActiveChannels.Add(Channel);
	}
}
