#include "TcpConnection.h"

#include "EventLoop.h"
#include "HAL/PlatformTime.h"
#include "MuduoNetLog.h"
#include "Net/Channel.h"
#include "SocketOps.h"

namespace mnet
{
	FTcpConnection::FTcpConnection(FEventLoop* InLoop, const FString& InName, FSocketHandle InSocket,
		const FInetAddress& InLocalAddress, const FInetAddress& InPeerAddress)
		: Loop(InLoop)
		, Name(InName)
		, Socket(MoveTemp(InSocket))
		, LocalAddress(InLocalAddress)
		, PeerAddress(InPeerAddress)
	{
		checkf(Loop != nullptr, TEXT("MuduoNet: FTcpConnection 必须绑定 EventLoop"));
		checkf(Socket.IsValid(), TEXT("MuduoNet: FTcpConnection 需要有效的 socket"));

		FSocketOps::SetTcpNoDelay(Socket.Get(), true);
	}

	FTcpConnection::~FTcpConnection()
	{
		// Channel 必须在 loop 线程先移除（ConnectDestroyed 负责），这里只兜底。
		Channel.Reset();
		Socket.Reset();
	}

	bool FTcpConnection::IsConnected() const
	{
		return State.load() == EState::Connected;
	}

	bool FTcpConnection::IsDisconnected() const
	{
		return State.load() == EState::Disconnected;
	}

	void FTcpConnection::SetConnectionCallback(FConnectionCallback InCallback)
	{
		ConnectionCallback = MoveTemp(InCallback);
	}

	void FTcpConnection::SetMessageCallback(FMessageCallback InCallback)
	{
		MessageCallback = MoveTemp(InCallback);
	}

	void FTcpConnection::SetWriteCompleteCallback(FWriteCompleteCallback InCallback)
	{
		WriteCompleteCallback = MoveTemp(InCallback);
	}

	void FTcpConnection::SetHighWaterMarkCallback(FHighWaterMarkCallback InCallback, int32 InHighWaterMark)
	{
		HighWaterMarkCallback = MoveTemp(InCallback);
		HighWaterMark = FMath::Max(1, InHighWaterMark);
	}

	void FTcpConnection::SetCloseCallback(FCloseCallback InCallback)
	{
		CloseCallback = MoveTemp(InCallback);
	}

	void FTcpConnection::ConnectEstablished()
	{
		Loop->AssertInLoopThread();
		checkf(State.load() == EState::Connecting, TEXT("MuduoNet: ConnectEstablished 只能调用一次"));

		Channel = MakeUnique<FChannel>(Loop, Socket.Get());

		TSharedPtr<FChannelTieTarget> TieTarget = AsShared();
		Channel->Tie(TWeakPtr<FChannelTieTarget>(TieTarget));

		Channel->SetReadCallback([this]()
		{
			HandleRead(FPlatformTime::Seconds());
		});
		Channel->SetWriteCallback([this]()
		{
			HandleWrite();
		});
		Channel->SetCloseCallback([this]()
		{
			HandleClose();
		});
		Channel->SetErrorCallback([this]()
		{
			HandleError();
		});

		State.store(EState::Connected);
		if (bReading.load())
		{
			Channel->EnableReading();
		}

		if (ConnectionCallback)
		{
			ConnectionCallback(AsShared());
		}
	}

	void FTcpConnection::ConnectDestroyed()
	{
		Loop->AssertInLoopThread();

		if (State.load() == EState::Connected)
		{
			State.store(EState::Disconnected);
			if (Channel.IsValid())
			{
				Channel->DisableAll();
			}
			if (ConnectionCallback)
			{
				ConnectionCallback(AsShared());
			}
		}

		if (Channel.IsValid())
		{
			Channel->RemoveFromPoller();
			Channel.Reset();
		}
	}

	void FTcpConnection::Send(const void* Data, int32 Length)
	{
		if (Length <= 0 || State.load() != EState::Connected)
		{
			return;
		}

		if (Loop->IsInLoopThread())
		{
			SendInLoop(Data, Length);
			return;
		}

		TArray<uint8> Copy;
		Copy.Append(static_cast<const uint8*>(Data), Length);

		FTcpConnectionPtr Guard = AsShared();
		Loop->QueueInLoop([Guard, Copy = MoveTemp(Copy)]()
		{
			Guard->SendInLoop(Copy.GetData(), Copy.Num());
		});
	}

	void FTcpConnection::Send(const TArray<uint8>& Data)
	{
		Send(Data.GetData(), Data.Num());
	}

	void FTcpConnection::Send(FBuffer&& Buffer)
	{
		if (State.load() != EState::Connected)
		{
			return;
		}

		if (Loop->IsInLoopThread())
		{
			SendInLoop(Buffer.Peek(), Buffer.ReadableBytes());
			Buffer.RetrieveAll();
			return;
		}

		FTcpConnectionPtr Guard = AsShared();
		Loop->QueueInLoop([Guard, Moved = MoveTemp(Buffer)]() mutable
		{
			Guard->SendInLoop(Moved.Peek(), Moved.ReadableBytes());
			Moved.RetrieveAll();
		});
	}

	void FTcpConnection::SendInLoop(const void* Data, int32 Length)
	{
		Loop->AssertInLoopThread();

		if (Channel == nullptr || State.load() == EState::Disconnected)
		{
			UE_LOG(LogMuduoNet, Verbose, TEXT("MuduoNet: 连接 %s 已断开，丢弃发送"), *Name);
			return;
		}

		int32 Written = 0;
		bool bFaultError = false;

		// 输出队列为空时先尝试直接写，避免小包也要走一次缓冲。
		if (!Channel->IsWriting() && OutputBuffer.ReadableBytes() == 0)
		{
			Written = FSocketOps::Send(Socket.Get(), Data, Length);
			if (Written >= 0)
			{
				BytesSent += Written;
				if (Written == Length && WriteCompleteCallback)
				{
					FTcpConnectionPtr Guard = AsShared();
					Loop->QueueInLoop([Guard]()
					{
						if (Guard->WriteCompleteCallback)
						{
							Guard->WriteCompleteCallback(Guard);
						}
					});
				}
			}
			else
			{
				Written = 0;
				const ESocketError Error = FSocketOps::GetLastError();
				if (Error != ESocketError::TryAgain)
				{
					UE_LOG(LogMuduoNet, Warning, TEXT("MuduoNet: 连接 %s 发送失败（%s）"), *Name, *FSocketOps::ErrorToString(Error));
					if (Error == ESocketError::BrokenPipe || Error == ESocketError::ConnectionReset)
					{
						bFaultError = true;
					}
				}
			}
		}

		const int32 Remaining = Length - Written;
		if (!bFaultError && Remaining > 0)
		{
			const int32 OldBufferedBytes = OutputBuffer.ReadableBytes();
			OutputBuffer.Append(static_cast<const uint8*>(Data) + Written, Remaining);

			if (OldBufferedBytes + Remaining >= HighWaterMark && HighWaterMarkCallback)
			{
				FTcpConnectionPtr Guard = AsShared();
				const int32 BufferedBytes = OldBufferedBytes;
				Loop->QueueInLoop([Guard, BufferedBytes]()
				{
					if (Guard->HighWaterMarkCallback)
					{
						Guard->HighWaterMarkCallback(Guard, BufferedBytes);
					}
				});
			}

			if (!Channel->IsWriting())
			{
				Channel->EnableWriting();
			}
		}
	}

	void FTcpConnection::Shutdown()
	{
		if (State.load() == EState::Connected)
		{
			FTcpConnectionPtr Guard = AsShared();
			Loop->RunInLoop([Guard]()
			{
				Guard->ShutdownInLoop();
			});
		}
	}

	void FTcpConnection::ShutdownInLoop()
	{
		Loop->AssertInLoopThread();
		if (State.load() != EState::Connected || Channel == nullptr)
		{
			return;
		}

		State.store(EState::Disconnecting);
		if (!Channel->IsWriting())
		{
			FSocketOps::ShutdownWrite(Socket.Get());
		}
	}

	void FTcpConnection::ForceClose()
	{
		const EState CurrentState = State.load();
		if (CurrentState != EState::Connected && CurrentState != EState::Disconnecting)
		{
			return;
		}

		State.store(EState::Disconnecting);

		FTcpConnectionPtr Guard = AsShared();
		Loop->QueueInLoop([Guard]()
		{
			Guard->ForceCloseInLoop();
		});
	}

	void FTcpConnection::ForceCloseInLoop()
	{
		Loop->AssertInLoopThread();
		const EState CurrentState = State.load();
		if (CurrentState == EState::Connected || CurrentState == EState::Disconnecting)
		{
			State.store(EState::Disconnecting);
			HandleClose();
		}
	}

	void FTcpConnection::StartRead()
	{
		if (bReading.load())
		{
			return;
		}

		FTcpConnectionPtr Guard = AsShared();
		Loop->RunInLoop([Guard]()
		{
			Guard->StartReadInLoop();
		});
	}

	void FTcpConnection::StopRead()
	{
		if (!bReading.load())
		{
			return;
		}

		FTcpConnectionPtr Guard = AsShared();
		Loop->RunInLoop([Guard]()
		{
			Guard->StopReadInLoop();
		});
	}

	bool FTcpConnection::IsReading() const
	{
		return bReading.load();
	}

	void FTcpConnection::StartReadInLoop()
	{
		Loop->AssertInLoopThread();
		if (!bReading.load())
		{
			bReading.store(true);
			if (State.load() == EState::Connected && Channel.IsValid())
			{
				Channel->EnableReading();
			}
		}
	}

	void FTcpConnection::StopReadInLoop()
	{
		Loop->AssertInLoopThread();
		if (bReading.load())
		{
			bReading.store(false);
			if (State.load() == EState::Connected && Channel.IsValid())
			{
				Channel->DisableReading();
			}
		}
	}

	void FTcpConnection::HandleRead(double ReceiveTime)
	{
		Loop->AssertInLoopThread();

		int32 SavedError = 0;
		const int32 Read = InputBuffer.ReadFd(Socket.Get(), SavedError);

		if (Read > 0)
		{
			BytesReceived += Read;
			if (MessageCallback)
			{
				FTcpConnectionPtr Guard = AsShared();
				MessageCallback(Guard, &InputBuffer, ReceiveTime);
			}
			return;
		}

		if (Read == 0)
		{
			HandleClose();
			return;
		}

		UE_LOG(LogMuduoNet, Warning, TEXT("MuduoNet: 连接 %s 读取失败（%s）"),
			*Name, *FSocketOps::ErrorToString(FSocketOps::ErrorFromCode(SavedError)));
		HandleError();
	}

	void FTcpConnection::HandleWrite()
	{
		Loop->AssertInLoopThread();

		if (!Channel->IsWriting())
		{
			return;
		}

		const int32 Written = FSocketOps::Send(Socket.Get(), OutputBuffer.Peek(), OutputBuffer.ReadableBytes());
		if (Written > 0)
		{
			OutputBuffer.Retrieve(Written);
			BytesSent += Written;

			if (OutputBuffer.ReadableBytes() == 0)
			{
				Channel->DisableWriting();

				if (WriteCompleteCallback)
				{
					FTcpConnectionPtr Guard = AsShared();
					Loop->QueueInLoop([Guard]()
					{
						if (Guard->WriteCompleteCallback)
						{
							Guard->WriteCompleteCallback(Guard);
						}
					});
				}

				if (State.load() == EState::Disconnecting)
				{
					ShutdownInLoop();
				}
			}
		}
		else if (Written < 0)
		{
			const ESocketError Error = FSocketOps::GetLastError();
			if (Error != ESocketError::TryAgain && Error != ESocketError::Interrupted)
			{
				UE_LOG(LogMuduoNet, Warning, TEXT("MuduoNet: 连接 %s 写出错（%s）"), *Name, *FSocketOps::ErrorToString(Error));
			}
		}
	}

	void FTcpConnection::HandleClose()
	{
		Loop->AssertInLoopThread();

		const EState CurrentState = State.load();
		if (CurrentState != EState::Connected && CurrentState != EState::Disconnecting)
		{
			return;
		}

		State.store(EState::Disconnected);
		if (Channel.IsValid())
		{
			Channel->DisableAll();
		}

		FTcpConnectionPtr Guard = AsShared();

		if (ConnectionCallback)
		{
			ConnectionCallback(Guard);
		}

		// 必须是最后一行：TcpServer/TcpClient 会在这里把本对象从连接表移除。
		if (CloseCallback)
		{
			CloseCallback(Guard);
		}
	}

	void FTcpConnection::HandleError()
	{
		const int32 SocketError = FSocketOps::GetSocketError(Socket.Get());
		UE_LOG(LogMuduoNet, Warning, TEXT("MuduoNet: 连接 %s 出错，SO_ERROR=%d"), *Name, SocketError);

		// Windows 上 POLLERR 不一定伴随可读事件，这里直接进入关闭流程，避免连接悬挂。
		ForceCloseInLoop();
	}

	const TCHAR* FTcpConnection::StateToString() const
	{
		switch (State.load())
		{
		case EState::Disconnected:	return TEXT("Disconnected");
		case EState::Connecting:	return TEXT("Connecting");
		case EState::Connected:		return TEXT("Connected");
		case EState::Disconnecting:	return TEXT("Disconnecting");
		default:					return TEXT("Unknown");
		}
	}
}
