#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "InetAddress.h"
#include "TcpConnection.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"

#include <atomic>

namespace mnet
{
	class FEventLoop;
	class FConnector;

	/**
	 * TCP 客户端（muduo TcpClient 的移植）。
	 *
	 * - 内部用 FConnector 做非阻塞连接与指数退避重试；
	 * - 连接建立/断开都会触发 ConnectionCallback（断开时 IsConnected() == false）；
	 * - 生命周期：对象必须在所属 loop 线程创建与销毁。
	 */
	class MUDUOCORE_API FTcpClient
	{
	public:
		using FConnectionCallback = FTcpConnection::FConnectionCallback;
		using FMessageCallback = FTcpConnection::FMessageCallback;
		using FWriteCompleteCallback = FTcpConnection::FWriteCompleteCallback;
		using FConnectFailedCallback = TFunction<void()>;

		FTcpClient(FEventLoop* InLoop, const FInetAddress& InServerAddress, const FString& InName);
		~FTcpClient();

		FTcpClient(const FTcpClient&) = delete;
		FTcpClient& operator=(const FTcpClient&) = delete;

		/** 开始连接（失败会按退避重试）。线程安全。 */
		void Connect();
		/** 优雅关闭当前连接（半关闭，等对端关闭）。线程安全。 */
		void Disconnect();
		/** 停止重连并强制关闭当前连接。线程安全。 */
		void Stop();
		/** 连接断开后是否自动重连，默认关闭。 */
		void SetRetry(bool bInRetry) { bRetry.store(bInRetry); }

		bool IsConnected() const;
		/** 线程安全，返回当前连接（可能为空）。 */
		FTcpConnectionPtr GetConnection() const;

		FEventLoop* GetLoop() const { return Loop; }
		const FString& GetName() const { return Name; }
		const FInetAddress& GetServerAddress() const { return ServerAddress; }

		void SetConnectionCallback(FConnectionCallback InCallback);
		void SetMessageCallback(FMessageCallback InCallback);
		void SetWriteCompleteCallback(FWriteCompleteCallback InCallback);
		void SetConnectFailedCallback(FConnectFailedCallback InCallback);

	private:
		void NewConnection(FSocketHandle Socket, const FInetAddress& LocalAddress);
		void RemoveConnection(const FTcpConnectionPtr& Conn);

		FEventLoop* Loop = nullptr;
		FInetAddress ServerAddress;
		FString Name;

		TSharedPtr<FConnector> Connector;
		mutable FCriticalSection Mutex;
		FTcpConnectionPtr Connection;

		std::atomic<bool> bConnect{ false };
		std::atomic<bool> bRetry{ false };

		FConnectionCallback ConnectionCallback;
		FMessageCallback MessageCallback;
		FWriteCompleteCallback WriteCompleteCallback;
		FConnectFailedCallback ConnectFailedCallback;

		TSharedPtr<std::atomic<bool>> AliveFlag = MakeShared<std::atomic<bool>>(true);
		int32 NextConnectionId = 1;
	};
}
