#include "Net/Connector.h"

#include "EventLoop.h"
#include "MuduoNetLog.h"
#include "Net/Channel.h"
#include "SocketOps.h"

namespace mnet
{
	FConnector::FConnector(FEventLoop* InLoop, const FInetAddress& InServerAddress)
		: Loop(InLoop)
		, ServerAddress(InServerAddress)
	{
		checkf(Loop != nullptr, TEXT("MuduoNet: FConnector 必须绑定 EventLoop"));
	}

	FConnector::~FConnector()
	{
		if (Channel.IsValid())
		{
			Channel->DisableAll();
			Channel->RemoveFromPoller();
			Channel.Reset();
		}
		Socket.Reset();
	}

	void FConnector::SetNewConnectionCallback(FNewConnectionCallback InCallback)
	{
		NewConnectionCallback = MoveTemp(InCallback);
	}

	void FConnector::SetConnectFailedCallback(FConnectFailedCallback InCallback)
	{
		ConnectFailedCallback = MoveTemp(InCallback);
	}

	void FConnector::Start()
	{
		bConnect.store(true);
		TSharedPtr<FConnector> Self = AsShared();
		Loop->RunInLoop([Self]()
		{
			Self->StartInLoop();
		});
	}

	void FConnector::Stop()
	{
		bConnect.store(false);
		TSharedPtr<FConnector> Self = AsShared();
		Loop->QueueInLoop([Self]()
		{
			Self->StopInLoop();
		});
	}

	void FConnector::Restart()
	{
		Loop->AssertInLoopThread();
		State = EState::Disconnected;
		RetryDelayMs = kInitRetryDelayMs;
		bConnect.store(true);
		StartInLoop();
	}

	void FConnector::StartInLoop()
	{
		Loop->AssertInLoopThread();
		if (State == EState::Disconnected && bConnect.load())
		{
			Connect();
		}
	}

	void FConnector::StopInLoop()
	{
		Loop->AssertInLoopThread();

		if (RetryTimer.IsValid())
		{
			Loop->CancelTimer(RetryTimer);
			RetryTimer = FTimerId();
		}

		if (State == EState::Connecting)
		{
			State = EState::Disconnected;
			RemoveAndResetChannel();
			Socket.Reset();
		}
	}

	void FConnector::Connect()
	{
		Socket = FSocketOps::CreateTcp(ServerAddress.IsIPv6());
		if (!Socket.IsValid())
		{
			UE_LOG(LogMuduoNet, Warning, TEXT("MuduoNet: 创建连接 socket 失败（%s）"), *FSocketOps::ErrorToString(FSocketOps::GetLastError()));
			Retry();
			return;
		}

		const bool bConnectedImmediately = FSocketOps::Connect(Socket.Get(), ServerAddress);
		const ESocketError Error = bConnectedImmediately ? ESocketError::None : FSocketOps::GetLastError();

		switch (Error)
		{
		case ESocketError::None:
		case ESocketError::InProgress:
		case ESocketError::Interrupted:
		case ESocketError::AlreadyInProgress:
		// Windows 上非阻塞 connect 返回 WSAEWOULDBLOCK（映射为 TryAgain），同样属于「进行中」。
		case ESocketError::TryAgain:
			// 即使已经连上，也统一走「可写事件」路径，保证 SO_ERROR / 自连接检查只做一次。
			BeginConnecting();
			break;

		case ESocketError::AddressInUse:
		case ESocketError::AddressNotAvailable:
		case ESocketError::ConnectionRefused:
		case ESocketError::NetworkUnreachable:
		case ESocketError::HostUnreachable:
			Retry();
			break;

		default:
			UE_LOG(LogMuduoNet, Warning, TEXT("MuduoNet: 连接 %s 失败（%s）"),
				*ServerAddress.ToIpPortString(), *FSocketOps::ErrorToString(Error));
			Socket.Reset();
			State = EState::Disconnected;
			if (ConnectFailedCallback)
			{
				ConnectFailedCallback();
			}
			break;
		}
	}

	void FConnector::BeginConnecting()
	{
		State = EState::Connecting;
		Channel = MakeUnique<FChannel>(Loop, Socket.Get());

		TSharedPtr<FConnector> Self = AsShared();
		Channel->SetWriteCallback([Self]()
		{
			Self->HandleWrite();
		});
		Channel->SetErrorCallback([Self]()
		{
			Self->HandleError();
		});

		Channel->EnableWriting();
	}

	void FConnector::HandleWrite()
	{
		if (State != EState::Connecting)
		{
			return;
		}

		const FSocketValue Fd = Socket.Get();
		RemoveAndResetChannel();

		const int32 SocketError = FSocketOps::GetSocketError(Fd);
		if (SocketError != 0)
		{
			UE_LOG(LogMuduoNet, Warning, TEXT("MuduoNet: 连接 %s 失败（SO_ERROR=%d）"), *ServerAddress.ToIpPortString(), SocketError);
			Retry();
			return;
		}

		if (FSocketOps::IsSelfConnect(Fd))
		{
			UE_LOG(LogMuduoNet, Warning, TEXT("MuduoNet: 检测到自连接，按失败处理"));
			Retry();
			return;
		}

		State = EState::Connected;
		RetryDelayMs = kInitRetryDelayMs;

		if (bConnect.load() && NewConnectionCallback)
		{
			const FInetAddress LocalAddress = FSocketOps::GetLocalAddress(Fd);
			FSocketHandle ConnectedSocket = MoveTemp(Socket);
			NewConnectionCallback(MoveTemp(ConnectedSocket), LocalAddress);
		}
		else
		{
			Socket.Reset();
			State = EState::Disconnected;
		}
	}

	void FConnector::HandleError()
	{
		if (State != EState::Connecting)
		{
			return;
		}

		const FSocketValue Fd = Socket.Get();
		RemoveAndResetChannel();

		const int32 SocketError = FSocketOps::GetSocketError(Fd);
		UE_LOG(LogMuduoNet, Warning, TEXT("MuduoNet: 连接出错（SO_ERROR=%d）"), SocketError);
		Retry();
	}

	void FConnector::Retry()
	{
		Socket.Reset();
		State = EState::Disconnected;

		if (!bConnect.load())
		{
			if (ConnectFailedCallback)
			{
				ConnectFailedCallback();
			}
			return;
		}

		if (RetryTimer.IsValid())
		{
			Loop->CancelTimer(RetryTimer);
		}

		UE_LOG(LogMuduoNet, Verbose, TEXT("MuduoNet: %d ms 后重试连接 %s"), RetryDelayMs, *ServerAddress.ToIpPortString());

		TSharedPtr<FConnector> Self = AsShared();
		const double DelaySeconds = static_cast<double>(RetryDelayMs) / 1000.0;
		RetryTimer = Loop->RunAfter(DelaySeconds, [Self]()
		{
			Self->StartInLoop();
		});

		RetryDelayMs = FMath::Min(RetryDelayMs * 2, kMaxRetryDelayMs);
	}

	void FConnector::RemoveAndResetChannel()
	{
		if (!Channel.IsValid())
		{
			return;
		}

		Channel->DisableAll();
		Channel->RemoveFromPoller();

		// 可能正处在 Channel::HandleEvent 栈里，不能立刻销毁 Channel。
		TSharedPtr<FConnector> Self = AsShared();
		Loop->QueueInLoop([Self]()
		{
			Self->ResetChannel();
		});
	}

	void FConnector::ResetChannel()
	{
		Channel.Reset();
	}
}
