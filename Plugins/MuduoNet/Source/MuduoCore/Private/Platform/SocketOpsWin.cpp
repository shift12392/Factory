#include "SocketOps.h"

#include "MuduoNetLog.h"
#include "Platform/PlatformSocketIncludes.h"

#if MUDUONET_PLATFORM_WINDOWS

namespace mnet
{
	namespace
	{
		ESocketError FromWinsockError(int32 ErrorCode)
		{
			switch (ErrorCode)
			{
			case 0:						return ESocketError::None;
			case WSAEWOULDBLOCK:		return ESocketError::TryAgain;
			case WSAEINTR:				return ESocketError::Interrupted;
			case WSAEINPROGRESS:		return ESocketError::InProgress;
			case WSAEALREADY:			return ESocketError::AlreadyInProgress;
			case WSAECONNRESET:			return ESocketError::ConnectionReset;
			case WSAECONNREFUSED:		return ESocketError::ConnectionRefused;
			case WSAECONNABORTED:		return ESocketError::ConnectionAborted;
			case WSAENOTCONN:			return ESocketError::NotConnected;
			case WSAESHUTDOWN:			return ESocketError::BrokenPipe;
			case WSAENETUNREACH:		return ESocketError::NetworkUnreachable;
			case WSAEHOSTUNREACH:		return ESocketError::HostUnreachable;
			case WSAETIMEDOUT:			return ESocketError::TimedOut;
			case WSAEADDRINUSE:			return ESocketError::AddressInUse;
			case WSAEADDRNOTAVAIL:		return ESocketError::AddressNotAvailable;
			case WSAEMFILE:				return ESocketError::TooManyFiles;
			case WSAENOTSOCK:			return ESocketError::NotSocket;
			case WSAEINVAL:				return ESocketError::InvalidArgument;
			default:					return ESocketError::Unknown;
			}
		}

		bool FillSockaddr(const FInetAddress& Address, sockaddr_storage& OutStorage, int32& OutLength)
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

		FCriticalSection StartupLock;
		int32 StartupCount = 0;

		SOCKET ToNativeSocket(FSocketValue Value)
		{
			return static_cast<SOCKET>(Value);
		}
	}

	void CloseSocketValue(FSocketValue Value)
	{
		::closesocket(ToNativeSocket(Value));
	}

	FSocketHandle FSocketOps::CreateTcp(bool bIPv6)
	{
		const SOCKET Socket = ::socket(bIPv6 ? AF_INET6 : AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (Socket == INVALID_SOCKET)
		{
			return FSocketHandle();
		}

		FSocketHandle Handle(static_cast<FSocketValue>(Socket));
		SetNonBlocking(Handle.Get(), true);
		return Handle;
	}

	FSocketHandle FSocketOps::CreateUdp(bool bIPv6)
	{
		const SOCKET Socket = ::socket(bIPv6 ? AF_INET6 : AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (Socket == INVALID_SOCKET)
		{
			return FSocketHandle();
		}

		FSocketHandle Handle(static_cast<FSocketValue>(Socket));
		SetNonBlocking(Handle.Get(), true);
		return Handle;
	}

	bool FSocketOps::Bind(FSocketValue Socket, const FInetAddress& Address)
	{
		sockaddr_storage Storage;
		int32 Length = 0;
		if (!Address.IsValid() || !FillSockaddr(Address, Storage, Length))
		{
			return false;
		}

		return ::bind(ToNativeSocket(Socket), reinterpret_cast<const sockaddr*>(&Storage), Length) == 0;
	}

	bool FSocketOps::Listen(FSocketValue Socket, int32 Backlog)
	{
		return ::listen(ToNativeSocket(Socket), Backlog) == 0;
	}

	FSocketHandle FSocketOps::Accept(FSocketValue ListenSocket, FInetAddress& OutPeerAddress)
	{
		sockaddr_storage Storage;
		FMemory::Memzero(Storage);
		int32 Length = sizeof(Storage);

		const SOCKET Accepted = ::accept(ToNativeSocket(ListenSocket), reinterpret_cast<sockaddr*>(&Storage), &Length);
		if (Accepted == INVALID_SOCKET)
		{
			OutPeerAddress = FInetAddress();
			return FSocketHandle();
		}

		FSocketHandle Handle(static_cast<FSocketValue>(Accepted));
		SetNonBlocking(Handle.Get(), true);
		OutPeerAddress = FromSockaddr(reinterpret_cast<const sockaddr*>(&Storage));
		return Handle;
	}

	bool FSocketOps::Connect(FSocketValue Socket, const FInetAddress& Address)
	{
		sockaddr_storage Storage;
		int32 Length = 0;
		if (!Address.IsValid() || !FillSockaddr(Address, Storage, Length))
		{
			return false;
		}

		return ::connect(ToNativeSocket(Socket), reinterpret_cast<const sockaddr*>(&Storage), Length) == 0;
	}

	int32 FSocketOps::Send(FSocketValue Socket, const void* Data, int32 Length)
	{
		return ::send(ToNativeSocket(Socket), static_cast<const char*>(Data), Length, 0);
	}

	int32 FSocketOps::Recv(FSocketValue Socket, void* Data, int32 Length)
	{
		return ::recv(ToNativeSocket(Socket), static_cast<char*>(Data), Length, 0);
	}

	int32 FSocketOps::ReadIntoBuffers(FSocketValue Socket, void* Buffer, int32 BufferLength, void* ExtraBuffer, int32 ExtraBufferLength, int32& OutError)
	{
		OutError = 0;

		int32 Total = 0;
		if (Buffer != nullptr && BufferLength > 0)
		{
			const int32 Read = ::recv(ToNativeSocket(Socket), static_cast<char*>(Buffer), BufferLength, 0);
			if (Read <= 0)
			{
				OutError = (Read == 0) ? 0 : ::WSAGetLastError();
				return Read;
			}
			Total = Read;
			if (Read < BufferLength)
			{
				return Total;
			}
		}

		if (ExtraBuffer != nullptr && ExtraBufferLength > 0)
		{
			const int32 Read = ::recv(ToNativeSocket(Socket), static_cast<char*>(ExtraBuffer), ExtraBufferLength, 0);
			if (Read > 0)
			{
				Total += Read;
			}
			else if (Read < 0 && Total == 0)
			{
				OutError = ::WSAGetLastError();
				return -1;
			}
		}

		return Total;
	}

	void FSocketOps::ShutdownWrite(FSocketValue Socket)
	{
		::shutdown(ToNativeSocket(Socket), SD_SEND);
	}

	void FSocketOps::CloseSocket(FSocketValue Socket)
	{
		CloseSocketValue(Socket);
	}

	bool FSocketOps::SetNonBlocking(FSocketValue Socket, bool bNonBlocking)
	{
		u_long Mode = bNonBlocking ? 1u : 0u;
		return ::ioctlsocket(ToNativeSocket(Socket), FIONBIO, &Mode) == 0;
	}

	bool FSocketOps::SetReuseAddr(FSocketValue Socket, bool bEnabled)
	{
		const int32 Value = bEnabled ? 1 : 0;
		return ::setsockopt(ToNativeSocket(Socket), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&Value), sizeof(Value)) == 0;
	}

	bool FSocketOps::SetReusePort(FSocketValue Socket, bool bEnabled)
	{
		// Windows 没有 SO_REUSEPORT 语义，这里直接返回 false，由调用方决定是否降级。
		(void)Socket;
		(void)bEnabled;
		return false;
	}

	bool FSocketOps::SetTcpNoDelay(FSocketValue Socket, bool bEnabled)
	{
		const int32 Value = bEnabled ? 1 : 0;
		return ::setsockopt(ToNativeSocket(Socket), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&Value), sizeof(Value)) == 0;
	}

	bool FSocketOps::SetKeepAlive(FSocketValue Socket, bool bEnabled)
	{
		const int32 Value = bEnabled ? 1 : 0;
		return ::setsockopt(ToNativeSocket(Socket), SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char*>(&Value), sizeof(Value)) == 0;
	}

	FInetAddress FSocketOps::GetLocalAddress(FSocketValue Socket)
	{
		sockaddr_storage Storage;
		FMemory::Memzero(Storage);
		int32 Length = sizeof(Storage);
		if (::getsockname(ToNativeSocket(Socket), reinterpret_cast<sockaddr*>(&Storage), &Length) != 0)
		{
			return FInetAddress();
		}
		return FromSockaddr(reinterpret_cast<const sockaddr*>(&Storage));
	}

	FInetAddress FSocketOps::GetPeerAddress(FSocketValue Socket)
	{
		sockaddr_storage Storage;
		FMemory::Memzero(Storage);
		int32 Length = sizeof(Storage);
		if (::getpeername(ToNativeSocket(Socket), reinterpret_cast<sockaddr*>(&Storage), &Length) != 0)
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
		return FromWinsockError(::WSAGetLastError());
	}

	ESocketError FSocketOps::ErrorFromCode(int32 ErrorCode)
	{
		return FromWinsockError(ErrorCode);
	}

	int32 FSocketOps::GetSocketError(FSocketValue Socket)
	{
		int32 Value = 0;
		int32 Length = sizeof(Value);
		if (::getsockopt(ToNativeSocket(Socket), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&Value), &Length) != 0)
		{
			return ::WSAGetLastError();
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
		FScopeLock Lock(&StartupLock);
		if (StartupCount == 0)
		{
			WSADATA Data;
			const int32 Result = ::WSAStartup(MAKEWORD(2, 2), &Data);
			if (Result != 0)
			{
				UE_LOG(LogMuduoNet, Error, TEXT("WSAStartup 失败，错误码 %d"), Result);
				return false;
			}
		}
		++StartupCount;
		return true;
	}

	void FSocketOps::ShutdownNetwork()
	{
		FScopeLock Lock(&StartupLock);
		if (StartupCount > 0)
		{
			--StartupCount;
			if (StartupCount == 0)
			{
				::WSACleanup();
			}
		}
	}
}

#endif // MUDUONET_PLATFORM_WINDOWS
