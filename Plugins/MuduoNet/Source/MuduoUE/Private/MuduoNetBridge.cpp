#include "MuduoNetBridge.h"

#include "Async/Async.h"
#include "EventLoop.h"
#include "EventLoopThread.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "LengthHeaderCodec.h"
#include "MuduoNetLog.h"
#include "SocketOps.h"
#include "TcpClient.h"
#include "TcpServer.h"

namespace mnet
{
	FMuduoNetBridge::FMuduoNetBridge(EDispatchMode InDispatchMode)
		: DispatchMode(InDispatchMode)
	{
	}

	FMuduoNetBridge::~FMuduoNetBridge()
	{
		// 先让排队中的回调失效，再停线程。
		AliveFlag->store(false);
		StopAll();
	}

	void FMuduoNetBridge::EnsureLoop()
	{
		if (Loop != nullptr)
		{
			return;
		}

		LoopThread = MakeUnique<FEventLoopThread>(TEXT("MuduoNet UE IO"));
		Loop = LoopThread->StartLoop();
		if (Loop == nullptr)
		{
			LoopThread.Reset();
			UE_LOG(LogMuduoNet, Error, TEXT("MuduoNet: 创建 UE IO 线程失败"));
		}
	}

	void FMuduoNetBridge::StopAll()
	{
		if (Loop == nullptr)
		{
			return;
		}

		StopServer();

		// 客户端对象必须在 loop 线程销毁。
		FEvent* Done = FPlatformProcess::GetSynchEventFromPool(true);
		Loop->RunInLoop([this, Done]()
		{
			{
				FScopeLock Lock(&StateMutex);
				Clients.Reset();
				ClientConnections.Reset();
				ServerConnections.Reset();
			}
			Done->Trigger();
		});
		Done->Wait(3000);
		FPlatformProcess::ReturnSynchEventToPool(Done);

		LoopThread->Stop();
		LoopThread.Reset();
		Loop = nullptr;
	}

	bool FMuduoNetBridge::StartServer(uint16 Port, int32 ServerThreadNum)
	{
		EnsureLoop();
		if (Loop == nullptr)
		{
			return false;
		}

		bool bStarted = false;
		uint32 BoundPort = 0;
		FEvent* Done = FPlatformProcess::GetSynchEventFromPool(true);

		Loop->RunInLoop([this, Port, ServerThreadNum, &bStarted, &BoundPort, Done]()
		{
			Server = MakeUnique<FTcpServer>(Loop, FInetAddress::Loopback(Port), TEXT("MuduoNetServer"), false);
			Server->SetThreadNum(ServerThreadNum);

			Server->SetConnectionCallback([this](const FTcpConnectionPtr& Conn)
			{
				if (!Conn.IsValid())
				{
					return;
				}

				const int64 ConnectionId = Conn->GetConnectionId();
				{
					FScopeLock Lock(&StateMutex);
					if (Conn->IsConnected())
					{
						ServerConnections.Add(ConnectionId, Conn);
					}
					else
					{
						ServerConnections.Remove(ConnectionId);
					}
				}

				DispatchConnection(ConnectionId, Conn->IsConnected());
			});

			Server->SetMessageCallback([this](const FTcpConnectionPtr& Conn, FBuffer* Buffer, double ReceiveTime)
			{
				if (Conn.IsValid())
				{
					HandleRawMessage(Conn->GetConnectionId(), Conn, Buffer, ReceiveTime);
				}
			});

			Server->Start();
			BoundPort = Server->GetListenAddress().GetPort();
			bStarted = BoundPort != 0;

			ServerPort.store(static_cast<int32>(BoundPort));
			bServerRunning.store(bStarted);
			Done->Trigger();
		});

		Done->Wait(3000);
		FPlatformProcess::ReturnSynchEventToPool(Done);

		if (!bStarted)
		{
			UE_LOG(LogMuduoNet, Error, TEXT("MuduoNet: 启动服务端失败（端口 %d）"), Port);
		}
		return bStarted;
	}

	void FMuduoNetBridge::StopServer()
	{
		if (Loop == nullptr || !bServerRunning.load())
		{
			return;
		}

		FEvent* Done = FPlatformProcess::GetSynchEventFromPool(true);
		Loop->RunInLoop([this, Done]()
		{
			{
				FScopeLock Lock(&StateMutex);
				ServerConnections.Reset();
			}

			Server.Reset();
			ServerPort.store(0);
			bServerRunning.store(false);
			Done->Trigger();
		});
		Done->Wait(3000);
		FPlatformProcess::ReturnSynchEventToPool(Done);
	}

	int32 FMuduoNetBridge::GetServerConnectionCount() const
	{
		FScopeLock Lock(&StateMutex);
		return ServerConnections.Num();
	}

	bool FMuduoNetBridge::SendToServerConnection(int64 ConnectionId, const TArray<uint8>& Data)
	{
		FTcpConnectionPtr Conn;
		{
			FScopeLock Lock(&StateMutex);
			if (const FTcpConnectionPtr* Found = ServerConnections.Find(ConnectionId))
			{
				Conn = *Found;
			}
		}

		if (!Conn.IsValid() || !Conn->IsConnected())
		{
			return false;
		}

		SendOnConnection(Conn, Data);
		return true;
	}

	int64 FMuduoNetBridge::ConnectToServer(const FString& Host, uint16 Port)
	{
		EnsureLoop();
		if (Loop == nullptr)
		{
			return 0;
		}

		const FInetAddress Address = FInetAddress::FromIpPort(Host, Port);
		if (!Address.IsValid())
		{
			UE_LOG(LogMuduoNet, Warning, TEXT("MuduoNet: 服务器地址非法 %s:%d"), *Host, Port);
			return 0;
		}

		const int64 ClientId = kClientIdOffset + NextClientId.fetch_add(1);
		bool bCreated = false;
		FEvent* Done = FPlatformProcess::GetSynchEventFromPool(true);

		Loop->RunInLoop([this, ClientId, Address, &bCreated, Done]()
		{
			TSharedPtr<FTcpClient> Client = MakeShared<FTcpClient>(
				Loop, Address, FString::Printf(TEXT("MuduoNetClient#%lld"), ClientId));

			Client->SetConnectionCallback([this, ClientId](const FTcpConnectionPtr& Conn)
			{
				if (!Conn.IsValid())
				{
					return;
				}

				{
					FScopeLock Lock(&StateMutex);
					if (Conn->IsConnected())
					{
						ClientConnections.Add(ClientId, Conn);
					}
					else
					{
						ClientConnections.Remove(ClientId);
					}
				}

				DispatchConnection(ClientId, Conn->IsConnected());
			});

			Client->SetMessageCallback([this, ClientId](const FTcpConnectionPtr& Conn, FBuffer* Buffer, double ReceiveTime)
			{
				HandleRawMessage(ClientId, Conn, Buffer, ReceiveTime);
			});

			Client->SetConnectFailedCallback([this, ClientId]()
			{
				DispatchConnection(ClientId, false);
			});

			{
				FScopeLock Lock(&StateMutex);
				Clients.Add(ClientId, Client);
			}

			Client->Connect();
			bCreated = true;
			Done->Trigger();
		});

		Done->Wait(2000);
		FPlatformProcess::ReturnSynchEventToPool(Done);

		if (!bCreated)
		{
			UE_LOG(LogMuduoNet, Error, TEXT("MuduoNet: 创建客户端失败"));
			return 0;
		}
		return ClientId;
	}

	void FMuduoNetBridge::DisconnectClient(int64 ClientId)
	{
		TSharedPtr<FTcpClient> Client;
		{
			FScopeLock Lock(&StateMutex);
			if (TSharedPtr<FTcpClient>* Found = Clients.Find(ClientId))
			{
				Client = *Found;
			}
		}

		if (Client.IsValid())
		{
			// 注意：客户端对象保留在 map 里，销毁统一放到 StopAll 的 loop 线程里做。
			Client->Stop();
		}
	}

	bool FMuduoNetBridge::SendFromClient(int64 ClientId, const TArray<uint8>& Data)
	{
		FTcpConnectionPtr Conn;
		{
			FScopeLock Lock(&StateMutex);
			if (const FTcpConnectionPtr* Found = ClientConnections.Find(ClientId))
			{
				Conn = *Found;
			}
		}

		if (!Conn.IsValid() || !Conn->IsConnected())
		{
			return false;
		}

		SendOnConnection(Conn, Data);
		return true;
	}

	bool FMuduoNetBridge::IsClientConnected(int64 ClientId) const
	{
		FScopeLock Lock(&StateMutex);
		const FTcpConnectionPtr* Found = ClientConnections.Find(ClientId);
		return Found != nullptr && Found->IsValid() && (*Found)->IsConnected();
	}

	int32 FMuduoNetBridge::GetClientCount() const
	{
		FScopeLock Lock(&StateMutex);
		return Clients.Num();
	}

	double FMuduoNetBridge::GetLastLoopBusyMs() const
	{
		return Loop != nullptr ? Loop->GetLastBusyMs() : 0.0;
	}

	double FMuduoNetBridge::GetMaxLoopBusyMs() const
	{
		return Loop != nullptr ? Loop->GetMaxBusyMs() : 0.0;
	}

	void FMuduoNetBridge::SetConnectionEvent(FConnectionEvent InCallback)
	{
		FScopeLock Lock(&CallbackMutex);
		ConnectionEvent = MoveTemp(InCallback);
	}

	void FMuduoNetBridge::SetMessageEvent(FMessageEvent InCallback)
	{
		FScopeLock Lock(&CallbackMutex);
		MessageEvent = MoveTemp(InCallback);
	}

	void FMuduoNetBridge::HandleRawMessage(int64 EndpointId, const FTcpConnectionPtr& Conn, FBuffer* Buffer, double ReceiveTime)
	{
		if (Buffer == nullptr)
		{
			return;
		}

		if (bUseLengthHeaderCodec.load())
		{
			FLengthHeaderCodec Codec([this, EndpointId](const FTcpConnectionPtr&, const TArray<uint8>& Message, double)
			{
				DispatchMessage(EndpointId, Message);
			});
			Codec.OnMessage(Conn, Buffer, ReceiveTime);
			return;
		}

		if (Buffer->ReadableBytes() <= 0)
		{
			return;
		}

		const TArray<uint8> Message = Buffer->RetrieveAllAsArray();
		DispatchMessage(EndpointId, Message);
	}

	void FMuduoNetBridge::SendOnConnection(const FTcpConnectionPtr& Conn, const TArray<uint8>& Data)
	{
		if (bUseLengthHeaderCodec.load())
		{
			FBuffer Encoded;
			FLengthHeaderCodec::Encode(Encoded, Data.GetData(), Data.Num());
			Conn->Send(MoveTemp(Encoded));
		}
		else
		{
			Conn->Send(Data);
		}

		BytesSent.fetch_add(Data.Num());
	}

	void FMuduoNetBridge::DispatchConnection(int64 EndpointId, bool bConnected)
	{
		if (DispatchMode == EDispatchMode::Inline)
		{
			FScopeLock Lock(&CallbackMutex);
			if (ConnectionEvent)
			{
				ConnectionEvent(EndpointId, bConnected);
			}
			return;
		}

		TSharedPtr<std::atomic<bool>> Alive = AliveFlag;
		AsyncTask(ENamedThreads::GameThread, [this, Alive, EndpointId, bConnected]()
		{
			if (!Alive->load())
			{
				return;
			}

			FScopeLock Lock(&CallbackMutex);
			if (ConnectionEvent)
			{
				ConnectionEvent(EndpointId, bConnected);
			}
		});
	}

	void FMuduoNetBridge::DispatchMessage(int64 EndpointId, const TArray<uint8>& Message)
	{
		BytesReceived.fetch_add(Message.Num());

		if (DispatchMode == EDispatchMode::Inline)
		{
			FScopeLock Lock(&CallbackMutex);
			if (MessageEvent)
			{
				MessageEvent(EndpointId, Message);
			}
			return;
		}

		TSharedPtr<std::atomic<bool>> Alive = AliveFlag;
		AsyncTask(ENamedThreads::GameThread, [this, Alive, EndpointId, Message]()
		{
			if (!Alive->load())
			{
				return;
			}

			FScopeLock Lock(&CallbackMutex);
			if (MessageEvent)
			{
				MessageEvent(EndpointId, Message);
			}
		});
	}
}
