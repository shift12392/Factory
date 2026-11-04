#include "Net/Channel.h"

#include "EventLoop.h"
#include "Net/Poller.h"

namespace mnet
{
	FChannel::FChannel(FPoller* InPoller, FSocketValue InFd)
		: Poller(InPoller)
		, Fd(InFd)
	{
	}

	FChannel::FChannel(FEventLoop* InLoop, FSocketValue InFd)
		: Loop(InLoop)
		, Fd(InFd)
	{
	}

	FChannel::~FChannel()
	{
		ReadCallback = nullptr;
		WriteCallback = nullptr;
		CloseCallback = nullptr;
		ErrorCallback = nullptr;
	}

	void FChannel::SetReadCallback(TFunction<void()> InCallback)
	{
		ReadCallback = MoveTemp(InCallback);
	}

	void FChannel::SetWriteCallback(TFunction<void()> InCallback)
	{
		WriteCallback = MoveTemp(InCallback);
	}

	void FChannel::SetCloseCallback(TFunction<void()> InCallback)
	{
		CloseCallback = MoveTemp(InCallback);
	}

	void FChannel::SetErrorCallback(TFunction<void()> InCallback)
	{
		ErrorCallback = MoveTemp(InCallback);
	}

	void FChannel::Tie(const TWeakPtr<FChannelTieTarget>& InTarget)
	{
		TieTarget = InTarget;
	}

	void FChannel::EnableReading()
	{
		Events |= ReadEvent;
		Update();
	}

	void FChannel::DisableReading()
	{
		Events &= ~ReadEvent;
		Update();
	}

	void FChannel::EnableWriting()
	{
		Events |= WriteEvent;
		Update();
	}

	void FChannel::DisableWriting()
	{
		Events &= ~WriteEvent;
		Update();
	}

	void FChannel::DisableAll()
	{
		Events = NoneEvent;
		Update();
	}

	void FChannel::Update()
	{
		// 从未注册过、且不再关心任何事件时不需要打扰 poller。
		if (Index < 0 && IsNoneEvent())
		{
			return;
		}

		if (Loop != nullptr)
		{
			Loop->UpdateChannel(this);
		}
		else if (Poller != nullptr)
		{
			Poller->UpdateChannel(this);
		}
	}

	void FChannel::RemoveFromPoller()
	{
		if (Loop != nullptr)
		{
			Loop->RemoveChannel(this);
		}
		else if (Poller != nullptr)
		{
			Poller->RemoveChannel(this);
		}
	}

	void FChannel::HandleEvent()
	{
		TSharedPtr<FChannelTieTarget> Guard;
		if (TieTarget.IsValid())
		{
			Guard = TieTarget.Pin();
			if (!Guard.IsValid())
			{
				// 宿主已销毁，丢弃这次事件。
				return;
			}
		}

		bEventHandling = true;

		const uint32 PendingEvents = Revents;
		if ((PendingEvents & HangUpEvent) && !(PendingEvents & ReadEvent))
		{
			if (CloseCallback)
			{
				CloseCallback();
			}
		}

		if (PendingEvents & ErrorEvent)
		{
			if (ErrorCallback)
			{
				ErrorCallback();
			}
		}

		if (PendingEvents & ReadEvent)
		{
			if (ReadCallback)
			{
				ReadCallback();
			}
		}

		if (PendingEvents & WriteEvent)
		{
			if (WriteCallback)
			{
				WriteCallback();
			}
		}

		bEventHandling = false;
	}
}
