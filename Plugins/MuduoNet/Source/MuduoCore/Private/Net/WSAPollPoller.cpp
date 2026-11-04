#include "Net/Poller.h"

#include "HAL/PlatformProcess.h"
#include "MuduoNetLog.h"
#include "Net/Channel.h"
#include "Platform/PlatformSocketIncludes.h"

#if MUDUONET_PLATFORM_WINDOWS

namespace mnet
{
	namespace
	{
		uint32 ToPollEvents(uint32 InEvents)
		{
			uint32 Result = 0;
			if ((InEvents & FChannel::ReadEvent) != 0)
			{
				Result |= POLLIN | POLLRDNORM;
			}
			if ((InEvents & FChannel::WriteEvent) != 0)
			{
				Result |= POLLOUT | POLLWRNORM;
			}
			return Result;
		}

		uint32 FromPollEvents(int16 InEvents)
		{
			uint32 Result = FChannel::NoneEvent;
			if ((InEvents & (POLLIN | POLLRDNORM)) != 0)
			{
				Result |= FChannel::ReadEvent;
			}
			if ((InEvents & (POLLOUT | POLLWRNORM)) != 0)
			{
				Result |= FChannel::WriteEvent;
			}
			if ((InEvents & POLLERR) != 0)
			{
				Result |= FChannel::ErrorEvent;
			}
			if ((InEvents & POLLHUP) != 0)
			{
				Result |= FChannel::HangUpEvent;
			}
			if ((InEvents & POLLNVAL) != 0)
			{
				// 句柄已失效，按错误 + 挂断处理，交给上层关闭连接。
				Result |= FChannel::ErrorEvent | FChannel::HangUpEvent;
			}
			return Result;
		}
	}

	/**
	 * Windows 实现：WSAPoll + 水平触发（Windows 不支持边沿触发）。
	 *
	 * 注意：如果 poll 数组里有已经 close 的句柄，WSAPoll 会整体返回 WSAENOTSOCK。
	 * 所以约定 socket 的关闭必须延后到 loop 线程（QueueInLoop），这一点在 TcpConnection 里保证。
	 */
	class FWSAPollPoller final : public FPoller
	{
	public:
		FWSAPollPoller() = default;
		virtual ~FWSAPollPoller() override = default;

		virtual void Poll(int32 TimeoutMs, FChannelList& OutActiveChannels) override
		{
			AssertInLoopThread();

			// WSAPoll 在 fds == 0 时返回 WSAEINVAL，这里直接退化成 sleep。
			if (PollFds.Num() == 0)
			{
				if (TimeoutMs > 0)
				{
					FPlatformProcess::Sleep(static_cast<float>(TimeoutMs) / 1000.0f);
				}
				return;
			}

			const int32 NumEvents = ::WSAPoll(PollFds.GetData(), static_cast<ULONG>(PollFds.Num()), TimeoutMs);
			if (NumEvents > 0)
			{
				int32 Remaining = NumEvents;
				for (int32 Index = 0; Index < PollFds.Num() && Remaining > 0; ++Index)
				{
					const WSAPOLLFD& PollFd = PollFds[Index];
					if (PollFd.revents == 0)
					{
						continue;
					}

					FChannel* Channel = Channels.FindRef(static_cast<FSocketValue>(PollFd.fd));
					if (Channel != nullptr)
					{
						FillActiveChannel(Channel, FromPollEvents(PollFd.revents), OutActiveChannels);
					}
					--Remaining;
				}
			}
			else if (NumEvents < 0)
			{
				const int32 SavedError = ::WSAGetLastError();
				if (SavedError != WSAEINTR)
				{
					UE_LOG(LogMuduoNet, Error, TEXT("WSAPoll 失败，错误码 %d"), SavedError);
				}
			}
		}

		virtual void UpdateChannel(FChannel* Channel) override
		{
			AssertInLoopThread();

			const int32 Index = Channel->GetIndex();
			if (Index < 0)
			{
				checkf(!Channel->IsNoneEvent(), TEXT("MuduoNet: 不能用空事件注册 Channel"));
				checkf(!Channels.Contains(Channel->GetFd()), TEXT("MuduoNet: 重复注册 Channel"));

				WSAPOLLFD PollFd;
				FMemory::Memzero(PollFd);
				PollFd.fd = static_cast<SOCKET>(Channel->GetFd());
				PollFd.events = static_cast<SHORT>(ToPollEvents(Channel->GetEvents()));
				PollFd.revents = 0;

				Channels.Add(Channel->GetFd(), Channel);
				Channel->SetIndex(PollFds.Add(PollFd));
			}
			else
			{
				checkf(Channels.FindRef(Channel->GetFd()) == Channel, TEXT("MuduoNet: Channel 未注册"));
				checkf(Index < PollFds.Num(), TEXT("MuduoNet: Channel 索引越界"));

				WSAPOLLFD& PollFd = PollFds[Index];
				PollFd.fd = static_cast<SOCKET>(Channel->GetFd());
				PollFd.events = static_cast<SHORT>(ToPollEvents(Channel->GetEvents()));
				PollFd.revents = 0;
			}
		}

		virtual void RemoveChannel(FChannel* Channel) override
		{
			AssertInLoopThread();

			const int32 Index = Channel->GetIndex();
			if (Index < 0 || Channels.Remove(Channel->GetFd()) == 0)
			{
				return;
			}

			checkf(Index < PollFds.Num(), TEXT("MuduoNet: Channel 索引越界"));

			if (Index == PollFds.Num() - 1)
			{
				PollFds.Pop();
			}
			else
			{
				// 和最后一个元素交换后再移除，避免整体搬移。
				const FSocketValue LastFd = static_cast<FSocketValue>(PollFds.Last().fd);
				PollFds.Swap(Index, PollFds.Num() - 1);
				PollFds.Pop();

				if (FChannel* const* Moved = Channels.Find(LastFd))
				{
					(*Moved)->SetIndex(Index);
				}
			}

			Channel->SetIndex(-1);
		}

	private:
		TArray<WSAPOLLFD> PollFds;
	};

	TUniquePtr<FPoller> FPoller::CreateDefaultPoller()
	{
		return MakeUnique<FWSAPollPoller>();
	}
}

#endif // MUDUONET_PLATFORM_WINDOWS
