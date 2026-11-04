#include "TcpServer.h"

#include "EventLoop.h"
#include "MuduoNetLog.h"
#include "Net/Acceptor.h"
#include "SocketOps.h"

namespace mnet
{
	FTcpServer::FTcpServer(FEventLoop* InBaseLoop, const FInetAddress& InListenAddress, const FString& InName, bool bInReusePort)
		: BaseLoop(InBaseLoop)
		, Name(InName)
		, ListenAddress(InListenAddress)
	{
		checkf(BaseLoop != nullptr, TEXT("MuduoNet: FTcpServer 必须绑定 base EventLoop"));

		Acceptor = MakeUnique<FAcceptor>(BaseLoop, InListenAddress, bInReusePort);
		Acceptor->SetNewConnectionCallback([this](FSocketHandle Socket, const FInetAddress& PeerAddress)
		{
			HandleNewConnection(MoveTemp(Socket), PeerAddress);
		});

		ThreadPool = MakeUnique<FEventLoopThreadPool>(BaseLoop, Name);
	}

	FTcpServer::~FTcpServer()
	{
		BaseLoop->AssertInLoopThread();
		AliveFlag->store(false);

		// 先断开所有连接（connectDestroyed 会排到各自 io loop 执行）。
		for (TPair<FString, FTcpConnectionPtr>& Pair : Connections)
		{
			FTcpConnectionPtr Conn = Pair.Value;
			Pair.Value.Reset();
			if (Conn.IsValid())
			{
				FEventLoop* IoLoop = Conn->GetLoop();
				IoLoop->RunInLoop([Conn]()
				{
					Conn->ConnectDestroyed();
				});
			}
		}
		Connections.Reset();
		ConnectionCount.store(0);

		// 停 IO 线程（析构 FEventLoopThread 会 Quit + join）。
		ThreadPool.Reset();
		Acceptor.Reset();
	}

	void FTcpServer::SetThreadNum(int32 InNumThreads)
	{
		checkf(!bStarted.load(), TEXT("MuduoNet: SetThreadNum 必须在 Start() 之前调用"));
		ThreadPool->SetThreadNum(InNumThreads);
	}

	void FTcpServer::SetConnectionCallback(FConnectionCallback InCallback)
	{
		ConnectionCallback = MoveTemp(InCallback);
	}

	void FTcpServer::SetMessageCallback(FMessageCallback InCallback)
	{
		MessageCallback = MoveTemp(InCallback);
	}

	void FTcpServer::SetWriteCompleteCallback(FWriteCompleteCallback InCallback)
	{
		WriteCompleteCallback = MoveTemp(InCallback);
	}

	void FTcpServer::Start()
	{
		BaseLoop->AssertInLoopThread();

		bool bExpected = false;
		if (!bStarted.compare_exchange_strong(bExpected, true))
		{
			return;
		}

		ThreadPool->Start(ThreadInitCallback);
		Acceptor->Listen();

		ListenAddress = Acceptor->GetListenAddress();
	}

	void FTcpServer::HandleNewConnection(FSocketHandle Socket, const FInetAddress& PeerAddress)
	{
		BaseLoop->AssertInLoopThread();

		if (!Socket.IsValid())
		{
			return;
		}

		FEventLoop* IoLoop = ThreadPool->GetNextLoop();
		if (IoLoop == nullptr)
		{
			IoLoop = BaseLoop;
		}

		const int32 ConnId = NextConnectionId++;
		const FString ConnName = FString::Printf(TEXT("%s-%s#%d"),
			*Name, *ListenAddress.ToIpPortString(), ConnId);
		const FInetAddress LocalAddress = FSocketOps::GetLocalAddress(Socket.Get());

		FTcpConnectionPtr Conn = MakeShared<FTcpConnection>(IoLoop, ConnName, MoveTemp(Socket), LocalAddress, PeerAddress);
		Conn->SetConnectionId(static_cast<int64>(ConnId));
		Conn->SetConnectionCallback(ConnectionCallback);
		Conn->SetMessageCallback(MessageCallback);
		Conn->SetWriteCompleteCallback(WriteCompleteCallback);

		TSharedPtr<std::atomic<bool>> Alive = AliveFlag;
		Conn->SetCloseCallback([this, Alive](const FTcpConnectionPtr& Closed)
		{
			if (!Alive->load())
			{
				return;
			}
			RemoveConnection(Closed);
		});

		Connections.Add(ConnName, Conn);
		ConnectionCount.fetch_add(1);

		IoLoop->RunInLoop([Conn]()
		{
			Conn->ConnectEstablished();
		});
	}

	void FTcpServer::RemoveConnection(const FTcpConnectionPtr& Conn)
	{
		TSharedPtr<std::atomic<bool>> Alive = AliveFlag;
		BaseLoop->RunInLoop([this, Conn, Alive]()
		{
			if (!Alive->load())
			{
				return;
			}
			RemoveConnectionInLoop(Conn);
		});
	}

	void FTcpServer::RemoveConnectionInLoop(const FTcpConnectionPtr& Conn)
	{
		BaseLoop->AssertInLoopThread();

		Connections.Remove(Conn->GetName());
		ConnectionCount.fetch_sub(1);

		// 连接对象可能正在回调栈上，销毁推迟到它的 io loop。
		FEventLoop* IoLoop = Conn->GetLoop();
		IoLoop->QueueInLoop([Conn]()
		{
			Conn->ConnectDestroyed();
		});
	}
}
