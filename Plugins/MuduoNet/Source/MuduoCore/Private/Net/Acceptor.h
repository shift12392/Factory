#pragma once

#include "CoreMinimal.h"
#include "InetAddress.h"
#include "MuduoNetPlatform.h"
#include "Net/Channel.h"
#include "SocketHandle.h"
#include "Templates/Function.h"

namespace mnet
{
	class FEventLoop;

	/**
	 * 监听端口并接受连接（muduo Acceptor 的移植）。
	 * 只在所属 EventLoop 线程中使用。
	 */
	class MUDUOCORE_API FAcceptor
	{
	public:
		using FNewConnectionCallback = TFunction<void(FSocketHandle, const FInetAddress&)>;

		FAcceptor(FEventLoop* InLoop, const FInetAddress& InListenAddress, bool bInReusePort = false);
		~FAcceptor();

		FAcceptor(const FAcceptor&) = delete;
		FAcceptor& operator=(const FAcceptor&) = delete;

		void SetNewConnectionCallback(FNewConnectionCallback InCallback);
		void Listen();

		bool IsListening() const { return bListening; }
		const FInetAddress& GetListenAddress() const { return ListenAddress; }

	private:
		void HandleRead();

		FEventLoop* Loop = nullptr;
		FInetAddress ListenAddress;
		FSocketHandle AcceptSocket;
		FChannel AcceptChannel;
		FNewConnectionCallback NewConnectionCallback;
		bool bListening = false;
#if MUDUONET_PLATFORM_LINUX
		/** EMFILE 时的占位 fd：先回收它，accept 掉堆积的连接再关掉，避免空转。 */
		FSocketHandle IdleFd;
#endif
	};
}
