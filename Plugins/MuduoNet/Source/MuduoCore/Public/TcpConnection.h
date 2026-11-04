#pragma once

#include "Buffer.h"
#include "ChannelTie.h"
#include "CoreMinimal.h"
#include "InetAddress.h"
#include "MuduoNetPlatform.h"
#include "SocketHandle.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"

#include <atomic>

namespace mnet
{
	class FChannel;
	class FEventLoop;
	class FTcpConnection;

	using FTcpConnectionPtr = TSharedPtr<FTcpConnection>;

	/**
	 * 一条 TCP 连接（muduo TcpConnection 的移植）。
	 *
	 * 状态机：Connecting → Connected → Disconnecting → Disconnected
	 *
	 * 线程约定：
	 * - 对象本身由 TcpServer / TcpClient 在所属 loop 线程创建、持有、销毁。
	 * - Send / Shutdown / ForceClose / StartRead / StopRead 是线程安全的。
	 * - 回调（Connection/Message/WriteComplete/HighWaterMark）都在 loop 线程执行；
	 *   Message 回调里的 FBuffer* 只在该回调期间有效。
	 */
	class MUDUOCORE_API FTcpConnection final : public TSharedFromThis<FTcpConnection>, public FChannelTieTarget
	{
	public:
		using FConnectionCallback = TFunction<void(const FTcpConnectionPtr&)>;
		using FMessageCallback = TFunction<void(const FTcpConnectionPtr&, FBuffer*, double)>;
		using FWriteCompleteCallback = TFunction<void(const FTcpConnectionPtr&)>;
		using FHighWaterMarkCallback = TFunction<void(const FTcpConnectionPtr&, int32)>;
		using FCloseCallback = TFunction<void(const FTcpConnectionPtr&)>;

		FTcpConnection(FEventLoop* InLoop, const FString& InName, FSocketHandle InSocket,
			const FInetAddress& InLocalAddress, const FInetAddress& InPeerAddress);
		~FTcpConnection();

		FTcpConnection(const FTcpConnection&) = delete;
		FTcpConnection& operator=(const FTcpConnection&) = delete;

		FEventLoop* GetLoop() const { return Loop; }
		const FString& GetName() const { return Name; }
		int64 GetConnectionId() const { return ConnectionId; }
		void SetConnectionId(int64 InId) { ConnectionId = InId; }
		const FInetAddress& GetLocalAddress() const { return LocalAddress; }
		const FInetAddress& GetPeerAddress() const { return PeerAddress; }
		bool IsConnected() const;
		bool IsDisconnected() const;

		/** 线程安全。 */
		void Send(const void* Data, int32 Length);
		void Send(const TArray<uint8>& Data);
		/** 会把 Buffer 的内容搬走（muduo send(Buffer*) 的等价形式）。 */
		void Send(FBuffer&& Buffer);
		/** 半关闭：数据发完后发送 FIN，仍可继续读取对端数据。线程安全。 */
		void Shutdown();
		/** 立即关闭。线程安全。 */
		void ForceClose();

		void StartRead();
		void StopRead();
		bool IsReading() const;

		void SetConnectionCallback(FConnectionCallback InCallback);
		void SetMessageCallback(FMessageCallback InCallback);
		void SetWriteCompleteCallback(FWriteCompleteCallback InCallback);
		void SetHighWaterMarkCallback(FHighWaterMarkCallback InCallback, int32 InHighWaterMark);

		FBuffer* GetInputBuffer() { return &InputBuffer; }
		FBuffer* GetOutputBuffer() { return &OutputBuffer; }

		int64 GetBytesReceived() const { return BytesReceived; }
		int64 GetBytesSent() const { return BytesSent; }

		/** 内部使用：TcpServer / TcpClient 调用。 */
		void SetCloseCallback(FCloseCallback InCallback);
		void ConnectEstablished();
		void ConnectDestroyed();

	private:
		enum class EState : uint8
		{
			Disconnected,
			Connecting,
			Connected,
			Disconnecting
		};

		void HandleRead(double ReceiveTime);
		void HandleWrite();
		void HandleClose();
		void HandleError();
		void SendInLoop(const void* Data, int32 Length);
		void ShutdownInLoop();
		void ForceCloseInLoop();
		void StartReadInLoop();
		void StopReadInLoop();
		const TCHAR* StateToString() const;

		FEventLoop* Loop = nullptr;
		FString Name;
		/** 由 TcpServer/TcpClient 分配，UE 适配层用来做端点 id。 */
		int64 ConnectionId = 0;
		std::atomic<EState> State{ EState::Connecting };
		std::atomic<bool> bReading{ true };

		FSocketHandle Socket;
		TUniquePtr<FChannel> Channel;
		FInetAddress LocalAddress;
		FInetAddress PeerAddress;

		FConnectionCallback ConnectionCallback;
		FMessageCallback MessageCallback;
		FWriteCompleteCallback WriteCompleteCallback;
		FHighWaterMarkCallback HighWaterMarkCallback;
		FCloseCallback CloseCallback;

		int32 HighWaterMark = 64 * 1024 * 1024;
		FBuffer InputBuffer;
		FBuffer OutputBuffer;
		int64 BytesReceived = 0;
		int64 BytesSent = 0;
	};
}
