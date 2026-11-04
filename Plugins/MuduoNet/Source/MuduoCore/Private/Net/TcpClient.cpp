#include "TcpClient.h"

#include "EventLoop.h"
#include "MuduoNetLog.h"
#include "Net/Connector.h"
#include "SocketOps.h"

namespace mnet
{
	FTcpClient::FTcpClient(FEventLoop* InLoop, const FInetAddress& InServerAddress, const FString& InName)
		: Loop(InLoop)
		, ServerAddress(InServerAddress)
		, Name(InName)
	{
		checkf(Loop != nullptr, TEXT("MuduoNet: FTcpClient 必须绑定 EventLoop"));

		Connector = MakeShared<FConnector>(Loop, ServerAddress);

		TSharedPtr<std::atomic<bool>> Alive = AliveFlag;
		Connector->SetNewConnectionCallback([this, Alive](FSocketHandle Socket, const FInetAddress& LocalAddress)
		{
			if (!Alive->load())
			{
				return;
			}
			NewConnection(MoveTemp(Socket), LocalAddress);
		});
		Connector->SetConnectFailedCallback([this, Alive]()
		{
			if (!Alive->load())
			{
				return;
			}
			if (ConnectFailedCallback)
			{
				ConnectFailedCallback();
			}
		});
	}

	FTcpClient::~FTcpClient()
	{
		AliveFlag->store(false);
		Stop();
		Connector.Reset();

		FScopeLock Lock(&Mutex);
		Connection.Reset();
	}

	void FTcpClient::SetConnectionCallback(FConnectionCallback InCallback)
	{
		ConnectionCallback = MoveTemp(InCallback);
	}

	void FTcpClient::SetMessageCallback(FMessageCallback InCallback)
	{
		MessageCallback = MoveTemp(InCallback);
	}

	void FTcpClient::SetWriteCompleteCallback(FWriteCompleteCallback InCallback)
	{
		WriteCompleteCallback = MoveTemp(InCallback);
	}

	void FTcpClient::SetConnectFailedCallback(FConnectFailedCallback InCallback)
	{
		ConnectFailedCallback = MoveTemp(InCallback);
	}

	void FTcpClient::Connect()
	{
		bConnect.store(true);
		if (Connector.IsValid())
		{
			Connector->Start();
		}
	}

	void FTcpClient::Disconnect()
	{
		bConnect.store(false);

		FTcpConnectionPtr Conn;
		{
			FScopeLock Lock(&Mutex);
			Conn = Connection;
		}

		if (Conn.IsValid())
		{
			Conn->Shutdown();
		}
	}

	void FTcpClient::Stop()
	{
		bConnect.store(false);

		if (Connector.IsValid())
		{
			Connector->Stop();
		}

		FTcpConnectionPtr Conn;
		{
			FScopeLock Lock(&Mutex);
			Conn = Connection;
		}

		if (Conn.IsValid())
		{
			Conn->ForceClose();
		}
	}

	bool FTcpClient::IsConnected() const
	{
		FScopeLock Lock(&Mutex);
		return Connection.IsValid() && Connection->IsConnected();
	}

	FTcpConnectionPtr FTcpClient::GetConnection() const
	{
		FScopeLock Lock(&Mutex);
		return Connection;
	}

	void FTcpClient::NewConnection(FSocketHandle Socket, const FInetAddress& LocalAddress)
	{
		Loop->AssertInLoopThread();

		if (!Socket.IsValid())
		{
			return;
		}

		const FInetAddress PeerAddress = FSocketOps::GetPeerAddress(Socket.Get());
		const FString ConnName = FString::Printf(TEXT("%s:%s#%d"), *Name, *PeerAddress.ToIpPortString(), NextConnectionId++);

		FTcpConnectionPtr Conn = MakeShared<FTcpConnection>(Loop, ConnName, MoveTemp(Socket), LocalAddress, PeerAddress);
		Conn->SetConnectionCallback(ConnectionCallback);
		Conn->SetMessageCallback(MessageCallback);
		Conn->SetWriteCompleteCallback(WriteCompleteCallback);
		Conn->SetCloseCallback([this](const FTcpConnectionPtr& Closed)
		{
			RemoveConnection(Closed);
		});

		{
			FScopeLock Lock(&Mutex);
			Connection = Conn;
		}

		Conn->ConnectEstablished();
	}

	void FTcpClient::RemoveConnection(const FTcpConnectionPtr& Conn)
	{
		Loop->AssertInLoopThread();

		{
			FScopeLock Lock(&Mutex);
			if (Connection == Conn)
			{
				Connection.Reset();
			}
		}

		// 回调栈上不能销毁 Channel，推迟到本轮事件处理之后。
		Loop->QueueInLoop([Conn]()
		{
			Conn->ConnectDestroyed();
		});

		if (bConnect.load() && bRetry.load() && Connector.IsValid())
		{
			Connector->Restart();
		}
	}
}
