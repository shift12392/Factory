#pragma once

#include "CoreMinimal.h"
#include "InetAddress.h"
#include "SocketHandle.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"
#include "Timer.h"

#include <atomic>

namespace mnet
{
	class FChannel;
	class FEventLoop;

	/**
	 * 非阻塞连接器（muduo Connector 的移植）：
	 * connect → 等待可写 → SO_ERROR 校验 → 自连接检测 → 失败按 500ms 起步、最大 30s 指数退避重试。
	 *
	 * 生命周期：由 TcpClient 持有 TSharedPtr，回调里用 self 引用保证析构安全。
	 */
	class MUDUOCORE_API FConnector final : public TSharedFromThis<FConnector>
	{
	public:
		using FNewConnectionCallback = TFunction<void(FSocketHandle, const FInetAddress& LocalAddress)>;
		using FConnectFailedCallback = TFunction<void()>;

		FConnector(FEventLoop* InLoop, const FInetAddress& InServerAddress);
		~FConnector();

		FConnector(const FConnector&) = delete;
		FConnector& operator=(const FConnector&) = delete;

		/** 线程安全：开始（或继续）连接。 */
		void Start();
		/** 线程安全：停止连接并取消重试。 */
		void Stop();
		/** 只能在 loop 线程调用：重置退避并重新连接。 */
		void Restart();

		void SetNewConnectionCallback(FNewConnectionCallback InCallback);
		void SetConnectFailedCallback(FConnectFailedCallback InCallback);

		FEventLoop* GetLoop() const { return Loop; }
		const FInetAddress& GetServerAddress() const { return ServerAddress; }
		/** 当前重试间隔，便于测试与诊断。 */
		int32 GetRetryDelayMs() const { return RetryDelayMs; }

	private:
		enum class EState : uint8
		{
			Disconnected,
			Connecting,
			Connected
		};

		void StartInLoop();
		void StopInLoop();
		void Connect();
		void BeginConnecting();
		void HandleWrite();
		void HandleError();
		void Retry();
		void RemoveAndResetChannel();
		void ResetChannel();

		static constexpr int32 kInitRetryDelayMs = 500;
		static constexpr int32 kMaxRetryDelayMs = 30 * 1000;

		FEventLoop* Loop = nullptr;
		FInetAddress ServerAddress;
		std::atomic<bool> bConnect{ false };
		EState State = EState::Disconnected;
		FSocketHandle Socket;
		TUniquePtr<FChannel> Channel;
		FNewConnectionCallback NewConnectionCallback;
		FConnectFailedCallback ConnectFailedCallback;
		int32 RetryDelayMs = kInitRetryDelayMs;
		FTimerId RetryTimer;
	};
}
