#include "SocketOps.h"

#include "MuduoNetLog.h"
#include "Platform/PlatformSocketIncludes.h"

#if MUDUONET_PLATFORM_LINUX

#ifndef MSG_NOSIGNAL
	#define MSG_NOSIGNAL 0
#endif

namespace mnet
{
	namespace
	{
		static_assert(EWOULDBLOCK == EAGAIN, "POSIX 上 EWOULDBLOCK 与 EAGAIN 必须一致");

		ESocketError FromErrno(int32 ErrorCode)
		{
			switch (ErrorCode)
			{
			case 0:				return ESocketError::None;
			case EAGAIN:		return ESocketError::TryAgain;
			case EINTR:			return ESocketError::Interrupted;
			case EINPROGRESS:	return ESocketError::InProgress;
			case EALREADY:		return ESocketError::AlreadyInProgress;
			case ECONNRESET:	return ESocketError::ConnectionReset;
			case ECONNREFUSED:	return ESocketError::ConnectionRefused;
			case ECONNABORTED:	return ESocketError::ConnectionAborted;
			case ENOTCONN:		return ESocketError::NotConnected;
			case EPIPE:			return ESocketError::BrokenPipe;
			case ENETUNREACH:	return ESocketError::NetworkUnreachable;
			case EHOSTUNREACH:	return ESocketError::HostUnreachable;
			case ETIMEDOUT:		return ESocketError::TimedOut;
			case EADDRINUSE:	return ESocketError::AddressInUse;
			case EADDRNOTAVAIL:	return ESocketError::AddressNotAvailable;
			case EMFILE:		return ESocketError::TooManyFiles;
			case ENFILE:		return ESocketError::TooManyFiles;
			case ENOTSOCK:		return ESocketError::NotSocket;
			case EINVAL:		return ESocketError::InvalidArgument;
			default:			return ESocketError::Unknown;
			}
		}

		bool FillSockaddr(const FInetAddress& Address, sockaddr_storage& OutStorage, socklen_t& OutLength)
		{
			FMemory::Memzero(OutStorage);

			if (Address.IsIPv6())
			{
				sockaddr_in6& Storage = reinterpret_cast<sockaddr_in6&>(OutStorage);
				Storage.sin6_family = AF_INET6;
				Storage.sin6_port = htons(Address.GetPort());
				FMemory::Memcpy(&Storage.sin6_addr, Address.GetRawAddress(), 16);
				OutLength = sizeof(sockaddr_in6);
				return true;
			}

			sockaddr_in& Storage = reinterpret_cast<sockaddr_in&>(OutStorage);
			Storage.sin_family = AF_INET;
			Storage.sin_port = htons(Address.GetPort());
			FMemory::Memcpy(&Storage.sin_addr, Address.GetRawAddress(), 4);
			OutLength = sizeof(sockaddr_in);
			return true;
		}

		FInetAddress FromSockaddr(const sockaddr* Address)
		{
			if (Address == nullptr)
			{
				return FInetAddress();
			}

			if (Address->sa_family == AF_INET6)
			{
				const sockaddr_in6* Address6 = reinterpret_cast<const sockaddr_in6*>(Address);
				return FInetAddress::FromRaw(&Address6->sin6_addr, 16, true, ntohs(Address6->sin6_port));
			}

			if (Address->sa_family == AF_INET)
			{
				const sockaddr_in* Address4 = reinterpret_cast<const sockaddr_in*>(Address);
				return FInetAddress::FromRaw(&Address4->sin_addr, 4, false, ntohs(Address4->sin_port));
			}

			return FInetAddress();
		}
	}

	void CloseSocketValue(FSocketValue Value)
	{
		if (Value >= 0)
		{
			::close(Value);
		}
	}

	FSocketHandle FSocketOps::CreateTcp(bool bIPv6)
	{
		const int32 Fd = ::socket(bIPv6 ? AF_INET6 : AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (Fd < 0)
		{
			return FSocketHandle();
		}

		FSocketHandle Handle(Fd);
		SetNonBlocking(Handle.Get(), true);
		::fcntl(Handle.Get(), F_SETFD, FD_CLOEXEC);
		return Handle;
	}

	FSocketHandle FSocketOps::CreateUdp(bool bIPv6)
	{
		const int32 Fd = ::socket(bIPv6 ? AF_INET6 : AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (Fd < 0)
		{
			return FSocketHandle();
		}

		FSocketHandle Handle(Fd);
		SetNonBlocking(Handle.Get(), true);
		::fcntl(Handle.Get(), F_SETFD, FD_CLOEXEC);
		return Handle;
	}

	bool FSocketOps::Bind(FSocketValue Socket, const FInetAddress& Address)
	{
		sockaddr_storage Storage;
		socklen_t Length = 0;
		if (!Address.IsValid() || !FillSockaddr(Address, Storage, Length))
		{
			return false;
		}

		return ::bind(Socket, reinterpret_cast<const sockaddr*>(&Storage), Length) == 0;
	}

	bool FSocketOps::Listen(FSocketValue Socket, int32 Backlog)
	{
		return ::listen(Socket, Backlog) == 0;
	}

	FSocketHandle FSocketOps::Accept(FSocketValue ListenSocket, FInetAddress& OutPeerAddress)
	{
		sockaddr_storage Storage;
		FMemory::Memzero(Storage);
		socklen_t Length = static_cast<socklen_t>(sizeof(Storage));

		const int32 Accepted = ::accept(ListenSocket, reinterpret_cast<sockaddr*>(&Storage), &Length);
		if (Accepted < 0)
		{
			OutPeerAddress = FInetAddress();
			return FSocketHandle();
		}

		FSocketHandle Handle(Accepted);
		SetNonBlocking(Handle.Get(), true);
		::fcntl(Handle.Get(), F_SETFD, FD_CLOEXEC);
		OutPeerAddress = FromSockaddr(reinterpret_cast<const sockaddr*>(&Storage));
		return Handle;
	}

	bool FSocketOps::Connect(FSocketValue Socket, const FInetAddress& Address)
	{
		sockaddr_storage Storage;
		socklen_t Length = 0;
		if (!Address.IsValid() || !FillSockaddr(Address, Storage, Length))
		{
			return false;
		}

		return ::connect(Socket, reinterpret_cast<const sockaddr*>(&Storage), Length) == 0;
	}

	int32 FSocketOps::Send(FSocketValue Socket, const void* Data, int32 Length)
	{
		return static_cast<int32>(::send(Socket, Data, static_cast<size_t>(Length), MSG_NOSIGNAL));
	}

	int32 FSocketOps::Recv(FSocketValue Socket, void* Data, int32 Length)
	{
		return static_cast<int32>(::recv(Socket, Data, static_cast<size_t>(Length), 0));
	}

	int32 FSocketOps::ReadIntoBuffers(FSocketValue Socket, void* Buffer, int32 BufferLength, void* ExtraBuffer, int32 ExtraBufferLength, int32& OutError)
	{
		OutError = 0;

		iovec Vectors[2];
		int32 VectorCount = 0;
		if (Buffer != nullptr && BufferLength > 0)
		{
			Vectors[VectorCount].iov_base = Buffer;
			Vectors[VectorCount].iov_len = static_cast<size_t>(BufferLength);
			++VectorCount;
		}
		if (ExtraBuffer != nullptr && ExtraBufferLength > 0)
		{
			Vectors[VectorCount].iov_base = ExtraBuffer;
			Vectors[VectorCount].iov_len = static_cast<size_t>(ExtraBufferLength);
			++VectorCount;
		}

		if (VectorCount == 0)
		{
			return 0;
		}

		const ssize_t Read = ::readv(Socket, Vectors, VectorCount);
		if (Read < 0)
		{
			OutError = errno;
			return -1;
		}
		return static_cast<int32>(Read);
	}

	void FSocketOps::ShutdownWrite(FSocketValue Socket)
	{
		::shutdown(Socket, SHUT_WR);
	}

	void FSocketOps::CloseSocket(FSocketValue Socket)
	{
		CloseSocketValue(Socket);
	}

	bool FSocketOps::SetNonBlocking(FSocketValue Socket, bool bNonBlocking)
	{
		const int32 Flags = ::fcntl(Socket, F_GETFL, 0);
		if (Flags < 0)
		{
			return false;
		}

		const int32 NewFlags = bNonBlocking ? (Flags | O_NONBLOCK) : (Flags & ~O_NONBLOCK);
		return ::fcntl(Socket, F_SETFL, NewFlags) == 0;
	}

	bool FSocketOps::SetReuseAddr(FSocketValue Socket, bool bEnabled)
	{
		const int32 Value = bEnabled ? 1 : 0;
		return ::setsockopt(Socket, SOL_SOCKET, SO_REUSEADDR, &Value, sizeof(Value)) == 0;
	}

	bool FSocketOps::SetReusePort(FSocketValue Socket, bool bEnabled)
	{
#ifdef SO_REUSEPORT
		const int32 Value = bEnabled ? 1 : 0;
		return ::setsockopt(Socket, SOL_SOCKET, SO_REUSEPORT, &Value, sizeof(Value)) == 0;
#else
		(void)Socket;
		(void)bEnabled;
		return false;
#endif
	}

	bool FSocketOps::SetTcpNoDelay(FSocketValue Socket, bool bEnabled)
	{
		const int32 Value = bEnabled ? 1 : 0;
		return ::setsockopt(Socket, IPPROTO_TCP, TCP_NODELAY, &Value, sizeof(Value)) == 0;
	}

	bool FSocketOps::SetKeepAlive(FSocketValue Socket, bool bEnabled)
	{
		const int32 Value = bEnabled ? 1 : 0;
		return ::setsockopt(Socket, SOL_SOCKET, SO_KEEPALIVE, &Value, sizeof(Value)) == 0;
	}

	FInetAddress FSocketOps::GetLocalAddress(FSocketValue Socket)
	{
		sockaddr_storage Storage;
		FMemory::Memzero(Storage);
		socklen_t Length = static_cast<socklen_t>(sizeof(Storage));
		if (::getsockname(Socket, reinterpret_cast<sockaddr*>(&Storage), &Length) != 0)
		{
			return FInetAddress();
		}
		return FromSockaddr(reinterpret_cast<const sockaddr*>(&Storage));
	}

	FInetAddress FSocketOps::GetPeerAddress(FSocketValue Socket)
	{
		sockaddr_storage Storage;
		FMemory::Memzero(Storage);
		socklen_t Length = static_cast<socklen_t>(sizeof(Storage));
		if (::getpeername(Socket, reinterpret_cast<sockaddr*>(&Storage), &Length) != 0)
		{
			return FInetAddress();
		}
		return FromSockaddr(reinterpret_cast<const sockaddr*>(&Storage));
	}

	bool FSocketOps::IsSelfConnect(FSocketValue Socket)
	{
		const FInetAddress Local = GetLocalAddress(Socket);
		const FInetAddress Peer = GetPeerAddress(Socket);
		return Local.IsValid() && Peer.IsValid() && Local == Peer;
	}

	ESocketError FSocketOps::GetLastError()
	{
		return FromErrno(errno);
	}

	ESocketError FSocketOps::ErrorFromCode(int32 ErrorCode)
	{
		return FromErrno(ErrorCode);
	}

	int32 FSocketOps::GetSocketError(FSocketValue Socket)
	{
		int32 Value = 0;
		socklen_t Length = static_cast<socklen_t>(sizeof(Value));
		if (::getsockopt(Socket, SOL_SOCKET, SO_ERROR, &Value, &Length) != 0)
		{
			return errno;
		}
		return Value;
	}

	FString FSocketOps::ErrorToString(ESocketError Error)
	{
		switch (Error)
		{
		case ESocketError::None:					return TEXT("None");
		case ESocketError::TryAgain:				return TEXT("WouldBlock");
		case ESocketError::Interrupted:				return TEXT("Interrupted");
		case ESocketError::InProgress:				return TEXT("InProgress");
		case ESocketError::AlreadyInProgress:		return TEXT("AlreadyInProgress");
		case ESocketError::ConnectionReset:			return TEXT("ConnectionReset");
		case ESocketError::ConnectionRefused:		return TEXT("ConnectionRefused");
		case ESocketError::ConnectionAborted:		return TEXT("ConnectionAborted");
		case ESocketError::NotConnected:			return TEXT("NotConnected");
		case ESocketError::BrokenPipe:				return TEXT("BrokenPipe");
		case ESocketError::NetworkUnreachable:		return TEXT("NetworkUnreachable");
		case ESocketError::HostUnreachable:			return TEXT("HostUnreachable");
		case ESocketError::TimedOut:				return TEXT("TimedOut");
		case ESocketError::AddressInUse:			return TEXT("AddressInUse");
		case ESocketError::AddressNotAvailable:		return TEXT("AddressNotAvailable");
		case ESocketError::TooManyFiles:			return TEXT("TooManyFiles");
		case ESocketError::NotSocket:				return TEXT("NotSocket");
		case ESocketError::InvalidArgument:			return TEXT("InvalidArgument");
		default:									return TEXT("Unknown");
		}
	}

	bool FSocketOps::EnsureNetworkInitialized()
	{
		static bool bSignalIgnored = false;
		if (!bSignalIgnored)
		{
			// 写已关闭的连接不能触发 SIGPIPE，否则整个进程会被杀掉。
			::signal(SIGPIPE, SIG_IGN);
			bSignalIgnored = true;
		}
		return true;
	}

	void FSocketOps::ShutdownNetwork()
	{
	}
}

#endif // MUDUONET_PLATFORM_LINUX
