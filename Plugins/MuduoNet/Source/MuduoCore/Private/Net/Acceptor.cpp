#include "Net/Acceptor.h"

#include "EventLoop.h"
#include "MuduoNetLog.h"
#include "SocketOps.h"

#if MUDUONET_PLATFORM_LINUX
#include <fcntl.h>
#include <unistd.h>
#endif

namespace mnet
{
	FAcceptor::FAcceptor(FEventLoop* InLoop, const FInetAddress& InListenAddress, bool bInReusePort)
		: Loop(InLoop)
		, ListenAddress(InListenAddress)
		, AcceptSocket(FSocketOps::CreateTcp(InListenAddress.IsIPv6()))
		, AcceptChannel(InLoop, AcceptSocket.Get())
	{
		checkf(Loop != nullptr, TEXT("MuduoNet: FAcceptor 必须绑定 EventLoop"));

		if (!AcceptSocket.IsValid())
		{
			UE_LOG(LogMuduoNet, Error, TEXT("MuduoNet: 创建监听 socket 失败（%s）"), *InListenAddress.ToIpPortString());
			return;
		}

		FSocketOps::SetReuseAddr(AcceptSocket.Get(), true);
		if (bInReusePort)
		{
			FSocketOps::SetReusePort(AcceptSocket.Get(), true);
		}

		if (!FSocketOps::Bind(AcceptSocket.Get(), InListenAddress))
		{
			UE_LOG(LogMuduoNet, Error, TEXT("MuduoNet: 绑定 %s 失败（%s）"),
				*InListenAddress.ToIpPortString(), *FSocketOps::ErrorToString(FSocketOps::GetLastError()));
			AcceptSocket.Reset();
			return;
		}

		// 端口传 0 时系统会分配端口，这里回填实际地址。
		const FInetAddress BoundAddress = FSocketOps::GetLocalAddress(AcceptSocket.Get());
		if (BoundAddress.IsValid())
		{
			ListenAddress = BoundAddress;
		}

		AcceptChannel.SetReadCallback([this]()
		{
			HandleRead();
		});

#if MUDUONET_PLATFORM_LINUX
		IdleFd.Reset(::open("/dev/null", O_RDONLY | O_CLOEXEC));
#endif
	}

	FAcceptor::~FAcceptor()
	{
		AcceptChannel.DisableAll();
		AcceptChannel.RemoveFromPoller();
		AcceptSocket.Reset();
#if MUDUONET_PLATFORM_LINUX
		IdleFd.Reset();
#endif
	}

	void FAcceptor::SetNewConnectionCallback(FNewConnectionCallback InCallback)
	{
		NewConnectionCallback = MoveTemp(InCallback);
	}

	void FAcceptor::Listen()
	{
		Loop->AssertInLoopThread();
		if (!AcceptSocket.IsValid())
		{
			return;
		}

		if (!FSocketOps::Listen(AcceptSocket.Get(), 128))
		{
			UE_LOG(LogMuduoNet, Error, TEXT("MuduoNet: listen 失败（%s）"), *FSocketOps::ErrorToString(FSocketOps::GetLastError()));
			return;
		}

		bListening = true;
		AcceptChannel.EnableReading();
	}

	void FAcceptor::HandleRead()
	{
		Loop->AssertInLoopThread();

		// 水平触发下也要循环取，一次性把 accept 队列排空。
		for (;;)
		{
			FInetAddress PeerAddress;
			FSocketHandle NewSocket = FSocketOps::Accept(AcceptSocket.Get(), PeerAddress);
			if (NewSocket.IsValid())
			{
				if (NewConnectionCallback)
				{
					NewConnectionCallback(MoveTemp(NewSocket), PeerAddress);
				}
				continue;
			}

			const ESocketError Error = FSocketOps::GetLastError();
			if (Error == ESocketError::TryAgain)
			{
				break;
			}
			if (Error == ESocketError::Interrupted)
			{
				continue;
			}
			if (Error == ESocketError::TooManyFiles)
			{
#if MUDUONET_PLATFORM_LINUX
				if (IdleFd.IsValid())
				{
					IdleFd.Reset();
					FInetAddress OverflowPeer;
					FSocketHandle OverflowSocket = FSocketOps::Accept(AcceptSocket.Get(), OverflowPeer);
					(void)OverflowSocket;
					IdleFd.Reset(::open("/dev/null", O_RDONLY | O_CLOEXEC));
				}
#endif
				UE_LOG(LogMuduoNet, Error, TEXT("MuduoNet: 文件描述符耗尽，暂时无法 accept"));
				break;
			}

			UE_LOG(LogMuduoNet, Warning, TEXT("MuduoNet: accept 失败（%s）"), *FSocketOps::ErrorToString(Error));
			break;
		}
	}
}
