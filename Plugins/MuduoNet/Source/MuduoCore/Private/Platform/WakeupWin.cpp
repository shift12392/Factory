#include "Platform/Wakeup.h"

#include "InetAddress.h"
#include "MuduoNetLog.h"
#include "Platform/PlatformSocketIncludes.h"
#include "SocketOps.h"

#if MUDUONET_PLATFORM_WINDOWS

namespace mnet
{
	FWakeup::FWakeup()
	{
		ServerSocket = FSocketOps::CreateUdp(false);
		if (!ServerSocket.IsValid())
		{
			UE_LOG(LogMuduoNet, Error, TEXT("MuduoNet: 创建唤醒 socket 失败，错误码 %d"), ::WSAGetLastError());
			return;
		}

		if (!FSocketOps::Bind(ServerSocket.Get(), FInetAddress::Loopback(0)))
		{
			UE_LOG(LogMuduoNet, Error, TEXT("MuduoNet: 绑定唤醒 socket 失败，错误码 %d"), ::WSAGetLastError());
			ServerSocket.Reset();
			return;
		}

		const FInetAddress BoundAddress = FSocketOps::GetLocalAddress(ServerSocket.Get());
		if (!BoundAddress.IsValid())
		{
			UE_LOG(LogMuduoNet, Error, TEXT("MuduoNet: 获取唤醒 socket 地址失败"));
			ServerSocket.Reset();
			return;
		}

		ClientSocket = FSocketOps::CreateUdp(false);
		if (!ClientSocket.IsValid())
		{
			UE_LOG(LogMuduoNet, Error, TEXT("MuduoNet: 创建唤醒发送端失败，错误码 %d"), ::WSAGetLastError());
			ServerSocket.Reset();
			return;
		}

		if (!FSocketOps::Connect(ClientSocket.Get(), BoundAddress))
		{
			const ESocketError Error = FSocketOps::GetLastError();
			if (Error != ESocketError::InProgress && Error != ESocketError::TryAgain)
			{
				UE_LOG(LogMuduoNet, Error, TEXT("MuduoNet: 连接唤醒发送端失败（%s）"), *FSocketOps::ErrorToString(Error));
				ClientSocket.Reset();
				ServerSocket.Reset();
			}
		}
	}

	FWakeup::~FWakeup() = default;

	bool FWakeup::IsValid() const
	{
		return ServerSocket.IsValid() && ClientSocket.IsValid();
	}

	FSocketValue FWakeup::GetReadHandle() const
	{
		return ServerSocket.Get();
	}

	void FWakeup::Notify()
	{
		if (!ClientSocket.IsValid())
		{
			return;
		}

		const uint8 Byte = 1;
		// 发送失败（例如缓冲满）无需处理：只要 loop 已经醒来，多一个字节没有意义。
		FSocketOps::Send(ClientSocket.Get(), &Byte, 1);
	}

	void FWakeup::Drain()
	{
		if (!ServerSocket.IsValid())
		{
			return;
		}

		uint8 Buffer[64];
		for (;;)
		{
			if (FSocketOps::Recv(ServerSocket.Get(), Buffer, static_cast<int32>(sizeof(Buffer))) <= 0)
			{
				break;
			}
		}
	}
}

#endif // MUDUONET_PLATFORM_WINDOWS
