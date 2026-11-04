#pragma once

#include "CoreMinimal.h"
#include "InetAddress.h"
#include "SocketHandle.h"

namespace mnet
{
	/** 把平台错误码归一化后的结果（WSAEWOULDBLOCK / EAGAIN 等都映射到这里）。 */
	enum class ESocketError : uint8
	{
		None = 0,
		TryAgain,
		Interrupted,
		InProgress,
		AlreadyInProgress,
		ConnectionReset,
		ConnectionRefused,
		ConnectionAborted,
		NotConnected,
		BrokenPipe,
		NetworkUnreachable,
		HostUnreachable,
		TimedOut,
		AddressInUse,
		AddressNotAvailable,
		TooManyFiles,
		NotSocket,
		InvalidArgument,
		Unknown,
	};

	/**
	 * 跨平台 socket 操作集合。
	 *
	 * 约束：
	 * - 公共接口只使用 mnet 自己的类型，winsock2.h / sys/socket.h 不会泄漏到调用方。
	 * - 不抛异常（UE 关闭了异常），失败通过返回值 + GetLastError 表达。
	 * - 本身不加锁，只允许在所属 IO 线程调用（EnsureNetworkInitialized 除外）。
	 */
	class MUDUOCORE_API FSocketOps
	{
	public:
		static FSocketHandle CreateTcp(bool bIPv6 = false);
		static FSocketHandle CreateUdp(bool bIPv6 = false);

		static bool Bind(FSocketValue Socket, const FInetAddress& Address);
		static bool Listen(FSocketValue Socket, int32 Backlog = 128);
		static FSocketHandle Accept(FSocketValue ListenSocket, FInetAddress& OutPeerAddress);
		static bool Connect(FSocketValue Socket, const FInetAddress& Address);

		static int32 Send(FSocketValue Socket, const void* Data, int32 Length);
		static int32 Recv(FSocketValue Socket, void* Data, int32 Length);

		/**
		 * 一次系统调用尽量多读：Linux 用 readv 同时写两块缓冲，Windows 连续两次 recv。
		 * 返回读取字节数；<0 表示错误，此时 OutError 是平台错误码。
		 */
		static int32 ReadIntoBuffers(FSocketValue Socket, void* Buffer, int32 BufferLength, void* ExtraBuffer, int32 ExtraBufferLength, int32& OutError);

		static void ShutdownWrite(FSocketValue Socket);
		static void CloseSocket(FSocketValue Socket);

		static bool SetNonBlocking(FSocketValue Socket, bool bNonBlocking);
		static bool SetReuseAddr(FSocketValue Socket, bool bEnabled);
		static bool SetReusePort(FSocketValue Socket, bool bEnabled);
		static bool SetTcpNoDelay(FSocketValue Socket, bool bEnabled);
		static bool SetKeepAlive(FSocketValue Socket, bool bEnabled);

		static FInetAddress GetLocalAddress(FSocketValue Socket);
		static FInetAddress GetPeerAddress(FSocketValue Socket);
		static bool IsSelfConnect(FSocketValue Socket);

		static ESocketError GetLastError();
		static ESocketError ErrorFromCode(int32 ErrorCode);
		static int32 GetSocketError(FSocketValue Socket);
		static FString ErrorToString(ESocketError Error);

		static bool IsWouldBlock(ESocketError Error) { return Error == ESocketError::TryAgain; }
		static bool IsInterrupted(ESocketError Error) { return Error == ESocketError::Interrupted; }

		/** Windows 上做 WSAStartup（可重复调用），POSIX 上忽略 SIGPIPE。 */
		static bool EnsureNetworkInitialized();
		static void ShutdownNetwork();
	};
}
