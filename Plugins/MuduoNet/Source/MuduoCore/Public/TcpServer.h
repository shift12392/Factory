#pragma once

#include "CoreMinimal.h"
#include "EventLoopThreadPool.h"
#include "InetAddress.h"
#include "TcpConnection.h"
#include "Templates/UniquePtr.h"

#include <atomic>

namespace mnet
{
	class FAcceptor;
	class FEventLoop;

	/**
	 * TCP 服务端（muduo TcpServer 的移植）。
	 *
	 * - base loop 上跑 Acceptor，负责接收连接；
	 * - 每个新连接通过 EventLoopThreadPool 轮询分配到某个 IO loop；
	 * - 连接表只在 base loop 线程访问，因此不需要锁。
	 *
	 * 生命周期：对象必须在 base loop 线程创建与销毁。
	 */
	class MUDUOCORE_API FTcpServer
	{
	public:
		using FConnectionCallback = FTcpConnection::FConnectionCallback;
		using FMessageCallback = FTcpConnection::FMessageCallback;
		using FWriteCompleteCallback = FTcpConnection::FWriteCompleteCallback;
		using FThreadInitCallback = FEventLoopThreadPool::FThreadInitCallback;

		FTcpServer(FEventLoop* InBaseLoop, const FInetAddress& InListenAddress, const FString& InName, bool bInReusePort = false);
		~FTcpServer();

		FTcpServer(const FTcpServer&) = delete;
		FTcpServer& operator=(const FTcpServer&) = delete;

		/** 必须在 Start() 之前调用；0 表示所有连接都在 base loop。 */
		void SetThreadNum(int32 InNumThreads);
		void SetThreadInitCallback(const FThreadInitCallback& InCallback) { ThreadInitCallback = InCallback; }

		/** 只能在 base loop 线程调用，可重复调用但只有第一次生效。 */
		void Start();

		void SetConnectionCallback(FConnectionCallback InCallback);
		void SetMessageCallback(FMessageCallback InCallback);
		void SetWriteCompleteCallback(FWriteCompleteCallback InCallback);

		FEventLoop* GetBaseLoop() const { return BaseLoop; }
		const FString& GetName() const { return Name; }
		const FInetAddress& GetListenAddress() const { return ListenAddress; }

		/** 线程安全（原子计数）。 */
		int32 GetConnectionCount() const { return ConnectionCount.load(); }
		/** 线程安全。 */
		FEventLoop* GetNextLoop() { return ThreadPool.IsValid() ? ThreadPool->GetNextLoop() : BaseLoop; }

	private:
		void HandleNewConnection(FSocketHandle Socket, const FInetAddress& PeerAddress);
		void RemoveConnection(const FTcpConnectionPtr& Conn);
		void RemoveConnectionInLoop(const FTcpConnectionPtr& Conn);

		FEventLoop* BaseLoop = nullptr;
		FString Name;
		FInetAddress ListenAddress;

		TUniquePtr<FAcceptor> Acceptor;
		TUniquePtr<FEventLoopThreadPool> ThreadPool;

		FConnectionCallback ConnectionCallback;
		FMessageCallback MessageCallback;
		FWriteCompleteCallback WriteCompleteCallback;
		FThreadInitCallback ThreadInitCallback;

		/** 只在 base loop 线程访问。 */
		TMap<FString, FTcpConnectionPtr> Connections;
		std::atomic<int32> ConnectionCount{ 0 };
		std::atomic<bool> bStarted{ false };
		/** 析构后阻止仍排队的回调访问 this。 */
		TSharedPtr<std::atomic<bool>> AliveFlag = MakeShared<std::atomic<bool>>(true);
		int32 NextConnectionId = 1;
	};
}
