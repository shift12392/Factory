#include "InetAddress.h"

#include "Containers/StringConv.h"
#include "Platform/PlatformSocketIncludes.h"

namespace mnet
{
	namespace
	{
		bool ParseIpText(const FString& Ip, bool& bOutIPv6, uint8 (&OutBytes)[16])
		{
			FTCHARToUTF8 Utf8(*Ip);
			const char* Text = Utf8.Get();
			if (Text == nullptr || Text[0] == '\0')
			{
				return false;
			}

			in_addr Address4;
			FMemory::Memzero(Address4);
			if (::inet_pton(AF_INET, Text, &Address4) == 1)
			{
				bOutIPv6 = false;
				FMemory::Memcpy(OutBytes, &Address4, 4);
				return true;
			}

			in6_addr Address6;
			FMemory::Memzero(Address6);
			if (::inet_pton(AF_INET6, Text, &Address6) == 1)
			{
				bOutIPv6 = true;
				FMemory::Memcpy(OutBytes, &Address6, 16);
				return true;
			}

			return false;
		}
	}

	FInetAddress FInetAddress::FromIpPort(const FString& Ip, uint16 Port)
	{
		uint8 Bytes[16] = { 0 };
		bool bIPv6 = false;
		if (ParseIpText(Ip, bIPv6, Bytes))
		{
			return FromRaw(Bytes, bIPv6 ? 16 : 4, bIPv6, Port);
		}
		return FInetAddress();
	}

	FInetAddress FInetAddress::Any(uint16 Port, bool bIPv6)
	{
		uint8 Bytes[16] = { 0 };
		return FromRaw(Bytes, bIPv6 ? 16 : 4, bIPv6, Port);
	}

	FInetAddress FInetAddress::Loopback(uint16 Port, bool bIPv6)
	{
		uint8 Bytes[16] = { 0 };
		if (bIPv6)
		{
			Bytes[15] = 1;
		}
		else
		{
			Bytes[0] = 127;
			Bytes[3] = 1;
		}
		return FromRaw(Bytes, bIPv6 ? 16 : 4, bIPv6, Port);
	}

	FInetAddress FInetAddress::FromRaw(const void* Data, int32 Size, bool bIPv6, uint16 Port)
	{
		FInetAddress Result;
		const int32 RequiredSize = bIPv6 ? 16 : 4;
		if (Data == nullptr || Size < RequiredSize)
		{
			return Result;
		}

		FMemory::Memcpy(Result.Address, Data, RequiredSize);
		if (!bIPv6)
		{
			FMemory::Memzero(Result.Address + 4, 12);
		}
		Result.bIPv6 = bIPv6;
		Result.Port = Port;
		Result.bValid = true;
		return Result;
	}

	bool FInetAddress::IsUnspecified() const
	{
		if (!bValid)
		{
			return false;
		}
		for (int32 Index = 0; Index < GetRawAddressSize(); ++Index)
		{
			if (Address[Index] != 0)
			{
				return false;
			}
		}
		return true;
	}

	FString FInetAddress::ToIpString() const
	{
		if (!bValid)
		{
			return TEXT("<invalid>");
		}

		char Text[INET6_ADDRSTRLEN] = { 0 };
		if (bIPv6)
		{
			in6_addr RawAddress;
			FMemory::Memcpy(&RawAddress, Address, 16);
			::inet_ntop(AF_INET6, &RawAddress, Text, static_cast<uint32>(sizeof(Text)));
		}
		else
		{
			in_addr RawAddress;
			FMemory::Memcpy(&RawAddress, Address, 4);
			::inet_ntop(AF_INET, &RawAddress, Text, static_cast<uint32>(sizeof(Text)));
		}

		return FString(UTF8_TO_TCHAR(Text));
	}

	FString FInetAddress::ToIpPortString() const
	{
		return FString::Printf(TEXT("%s:%d"), *ToIpString(), static_cast<int32>(Port));
	}

	bool FInetAddress::operator==(const FInetAddress& Other) const
	{
		if (bValid != Other.bValid || bIPv6 != Other.bIPv6 || Port != Other.Port)
		{
			return false;
		}
		if (!bValid)
		{
			return true;
		}
		return FMemory::Memcmp(Address, Other.Address, GetRawAddressSize()) == 0;
	}
}
