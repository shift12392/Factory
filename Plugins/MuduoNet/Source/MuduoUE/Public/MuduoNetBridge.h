#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "TcpConnection.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"

#include <atomic>

namespace mnet
{
	class FEventLoop;
	class FEventLoopThread;
	class FTcpClient;
	class FTcpServer;

	/** 回调派发方式。 */
	enum class EDispatchMode : uint8
	{
		/** 通过 AsyncTask 派发到游戏线程（UE 默认，蓝图安全）。 */
		GameThread,
		/** 在 IO 线程直接执行（纯 C++ / 单元测试，零延迟）。 */
		Inline
	};

	/**
	 * UE 侧桥接层：把 EventLoop / TcpServer / TcpClient 的回调转换成
	 * 「端点 id + 字节流」，并按 EDispatchMode 派发。
	 *
	 * 端点 id 规则（int64）：
	 * - 服务端连接：1, 2, 3 ...（由 TcpServer 分配）
	 * - 客户端：kClientIdOffset + 1, +2 ...
	 * 因此上层可以先判断 id 属于哪一侧，再决定发往哪里。
	 *
	 * 默认启用 4 字节大端长度头（与 FLengthHeaderCodec 一致），
	 * 上层拿到的是完整消息，不用自己处理粘包。
	 */
	class MUDUOUE_API FMuduoNetBridge
	{
	public:
		using FConnectionEvent = TFunction<void(int64 EndpointId, bool bConnected)>;
		using FMessageEvent = TFunction<void(int64 EndpointId, const TArray<uint8>& Message)>;

		/** 客户端端点 id 的起始偏移，避免与服务端连接 id 冲突。 */
		static constexpr int64 kClientIdOffset = 1000000000;

		explicit FMuduoNetBridge(EDispatchMode InDispatchMode = EDispatchMode::GameThread);
		~FMuduoNetBridge();

		FMuduoNetBridge(const FMuduoNetBridge&) = delete;
		FMuduoNetBridge& operator=(const FMuduoNetBridge&) = delete;

		// ---- 服务端 ----
		/** 端口传 0 表示由系统分配；ServerThreadNum = 0 表示全部在 IO 线程。 */
		bool StartServer(uint16 Port, int32 ServerThreadNum = 0);
		void StopServer();
		bool IsServerRunning() const { return bServerRunning.load(); }
		uint16 GetServerPort() const { return static_cast<uint16>(ServerPort.load()); }
		int32 GetServerConnectionCount() const;
		bool SendToServerConnection(int64 ConnectionId, const TArray<uint8>& Data);

		// ---- 客户端 ----
		/** 返回客户端端点 id，失败返回 0。 */
		int64 ConnectToServer(const FString& Host, uint16 Port);
		void DisconnectClient(int64 ClientId);
		bool SendFromClient(int64 ClientId, const TArray<uint8>& Data);
		bool IsClientConnected(int64 ClientId) const;
		int32 GetClientCount() const;

		// ---- 事件（建议在 StartServer / ConnectToServer 之前设置）----
		void SetConnectionEvent(FConnectionEvent InCallback);
		void SetMessageEvent(FMessageEvent InCallback);

		// ---- 统计 ----
		int64 GetBytesReceived() const { return BytesReceived.load(); }
		int64 GetBytesSent() const { return BytesSent.load(); }
		/** IO 线程上一次事件处理耗时（不含 poll 等待）。 */
		double GetLastLoopBusyMs() const;
		double GetMaxLoopBusyMs() const;

		bool IsUseLengthHeaderCodec() const { return bUseLengthHeaderCodec.load(); }
		void SetUseLengthHeaderCodec(bool bEnabled) { bUseLengthHeaderCodec.store(bEnabled); }

		EDispatchMode GetDispatchMode() const { return DispatchMode; }

	private:
		void EnsureLoop();
		void StopAll();
		void DispatchConnection(int64 EndpointId, bool bConnected);
		void DispatchMessage(int64 EndpointId, const TArray<uint8>& Message);
		void HandleRawMessage(int64 EndpointId, const FTcpConnectionPtr& Conn, FBuffer* Buffer, double ReceiveTime);
		void SendOnConnection(const FTcpConnectionPtr& Conn, const TArray<uint8>& Data);

		const EDispatchMode DispatchMode;
		TSharedPtr<std::atomic<bool>> AliveFlag = MakeShared<std::atomic<bool>>(true);

		mutable FCriticalSection CallbackMutex;
		FConnectionEvent ConnectionEvent;
		FMessageEvent MessageEvent;

		TUniquePtr<FEventLoopThread> LoopThread;
		FEventLoop* Loop = nullptr;

		mutable FCriticalSection StateMutex;
		TUniquePtr<FTcpServer> Server;
		TMap<int64, TSharedPtr<FTcpClient>> Clients;
		TMap<int64, FTcpConnectionPtr> ServerConnections;
		TMap<int64, FTcpConnectionPtr> ClientConnections;

		std::atomic<bool> bServerRunning{ false };
		std::atomic<int32> ServerPort{ 0 };
		std::atomic<int64> NextClientId{ 1 };
		std::atomic<int64> BytesReceived{ 0 };
		std::atomic<int64> BytesSent{ 0 };
		std::atomic<bool> bUseLengthHeaderCodec{ true };
	};
}
