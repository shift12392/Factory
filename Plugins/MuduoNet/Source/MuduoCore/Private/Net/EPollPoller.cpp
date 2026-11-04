#include "Net/Poller.h"

#include "MuduoNetLog.h"
#include "Net/Channel.h"
#include "Platform/PlatformSocketIncludes.h"

#if MUDUONET_PLATFORM_LINUX

#include <sys/epoll.h>

namespace mnet
{
	namespace
	{
		constexpr int32 kNew = -1;
		constexpr int32 kAdded = 1;
		constexpr int32 kDeleted = 2;

		uint32 ToEpollEvents(uint32 InEvents)
		{
			uint32 Result = 0;
			if ((InEvents & FChannel::ReadEvent) != 0)
			{
				Result |= EPOLLIN | EPOLLPRI | EPOLLRDHUP;
			}
			if ((InEvents & FChannel::WriteEvent) != 0)
			{
				Result |= EPOLLOUT;
			}
			return Result;
		}

		uint32 FromEpollEvents(uint32 InEvents)
		{
			uint32 Result = FChannel::NoneEvent;
			if ((InEvents & (EPOLLIN | EPOLLPRI)) != 0)
			{
				Result |= FChannel::ReadEvent;
			}
			if ((InEvents & EPOLLOUT) != 0)
			{
				Result |= FChannel::WriteEvent;
			}
			if ((InEvents & EPOLLERR) != 0)
			{
				Result |= FChannel::ErrorEvent;
			}
			// EPOLLRDHUP 同时标记可读，交给 TcpConnection 读到 0 后再走关闭流程，避免丢数据。
			if ((InEvents & EPOLLRDHUP) != 0)
			{
				Result |= FChannel::ReadEvent | FChannel::HangUpEvent;
			}
			if ((InEvents & EPOLLHUP) != 0)
			{
				Result |= FChannel::HangUpEvent;
			}
			return Result;
		}
	}

	/**
	 * Linux 实现：epoll + 水平触发（与 muduo 默认一致）。
	 * 使用 data.ptr 直接回传 Channel，省掉一次 fd -> Channel 的查表。
	 */
	class FEPollPoller final : public FPoller
	{
	public:
		FEPollPoller()
			: EpollFd(::epoll_create1(EPOLL_CLOEXEC))
		{
			checkf(EpollFd >= 0, TEXT("MuduoNet: epoll_create1 失败"));
			Events.SetNum(InitialEventCapacity);
		}

		virtual ~FEPollPoller() override
		{
			if (EpollFd >= 0)
			{
				::close(EpollFd);
			}
		}

		virtual void Poll(int32 TimeoutMs, FChannelList& OutActiveChannels) override
		{
			AssertInLoopThread();

			const int32 NumEvents = ::epoll_wait(EpollFd, Events.GetData(), Events.Num(), TimeoutMs);
			if (NumEvents > 0)
			{
				for (int32 Index = 0; Index < NumEvents; ++Index)
				{
					FChannel* Channel = static_cast<FChannel*>(Events[Index].data.ptr);
					if (Channel != nullptr)
					{
						FillActiveChannel(Channel, FromEpollEvents(Events[Index].events), OutActiveChannels);
					}
				}

				if (NumEvents == Events.Num())
				{
					Events.SetNum(Events.Num() * 2);
				}
			}
			else if (NumEvents < 0)
			{
				const int32 SavedErrno = errno;
				if (SavedErrno != EINTR)
				{
					UE_LOG(LogMuduoNet, Error, TEXT("epoll_wait 失败，errno=%d"), SavedErrno);
				}
			}
		}

		virtual void UpdateChannel(FChannel* Channel) override
		{
			AssertInLoopThread();

			const int32 Index = Channel->GetIndex();
			if (Index == kNew || Index == kDeleted)
			{
				if (Index == kNew)
				{
					checkf(!Channels.Contains(Channel->GetFd()), TEXT("MuduoNet: 重复注册 Channel"));
					Channels.Add(Channel->GetFd(), Channel);
				}
				else
				{
					checkf(Channels.FindRef(Channel->GetFd()) == Channel, TEXT("MuduoNet: Channel 状态不一致"));
				}

				Channel->SetIndex(kAdded);
				UpdateEpoll(EPOLL_CTL_ADD, Channel);
			}
			else
			{
				checkf(Channels.FindRef(Channel->GetFd()) == Channel, TEXT("MuduoNet: Channel 未注册"));
				if (Channel->IsNoneEvent())
				{
					UpdateEpoll(EPOLL_CTL_DEL, Channel);
					Channel->SetIndex(kDeleted);
				}
				else
				{
					UpdateEpoll(EPOLL_CTL_MOD, Channel);
				}
			}
		}

		virtual void RemoveChannel(FChannel* Channel) override
		{
			AssertInLoopThread();

			if (Channels.Remove(Channel->GetFd()) == 0)
			{
				return;
			}

			if (Channel->GetIndex() == kAdded)
			{
				UpdateEpoll(EPOLL_CTL_DEL, Channel);
			}
			Channel->SetIndex(kNew);
		}

	private:
		void UpdateEpoll(int32 Operation, FChannel* Channel)
		{
			epoll_event Event;
			FMemory::Memzero(Event);
			Event.events = ToEpollEvents(Channel->GetEvents());
			Event.data.ptr = Channel;

			if (::epoll_ctl(EpollFd, Operation, Channel->GetFd(), &Event) < 0)
			{
				const int32 SavedErrno = errno;
				if (Operation == EPOLL_CTL_DEL)
				{
					UE_LOG(LogMuduoNet, Warning, TEXT("epoll_ctl(DEL) 失败，fd=%d, errno=%d"), Channel->GetFd(), SavedErrno);
				}
				else
				{
					UE_LOG(LogMuduoNet, Error, TEXT("epoll_ctl(%d) 失败，fd=%d, errno=%d"), Operation, Channel->GetFd(), SavedErrno);
				}
			}
		}

		static constexpr int32 InitialEventCapacity = 16;

		int32 EpollFd = -1;
		TArray<epoll_event> Events;
	};

	TUniquePtr<FPoller> FPoller::CreateDefaultPoller()
	{
		return MakeUnique<FEPollPoller>();
	}
}

#endif // MUDUONET_PLATFORM_LINUX
