#pragma once

#include "CoreMinimal.h"
#include "MuduoNetPlatform.h"

namespace mnet
{
	/**
	 * 平台无关的 IP 地址 + 端口。
	 * 内部保存原始地址字节（IPv4 4 字节 / IPv6 16 字节），由 FSocketOps 负责与 sockaddr 互转。
	 */
	class MUDUOCORE_API FInetAddress
	{
	public:
		FInetAddress() = default;

		/** 解析失败时返回 !IsValid() 的地址。 */
		static FInetAddress FromIpPort(const FString& Ip, uint16 Port);
		static FInetAddress Any(uint16 Port, bool bIPv6 = false);
		static FInetAddress Loopback(uint16 Port, bool bIPv6 = false);
		static FInetAddress FromRaw(const void* Data, int32 Size, bool bIPv6, uint16 Port);

		bool IsValid() const { return bValid; }
		bool IsIPv6() const { return bIPv6; }
		uint16 GetPort() const { return Port; }

		const uint8* GetRawAddress() const { return Address; }
		int32 GetRawAddressSize() const { return bIPv6 ? 16 : 4; }

		bool IsUnspecified() const;

		FString ToIpString() const;
		FString ToIpPortString() const;

		bool operator==(const FInetAddress& Other) const;
		bool operator!=(const FInetAddress& Other) const { return !(*this == Other); }

	private:
		uint8 Address[16] = { 0, 0, 0, 0 };
		uint16 Port = 0;
		bool bIPv6 = false;
		bool bValid = false;
	};
}
