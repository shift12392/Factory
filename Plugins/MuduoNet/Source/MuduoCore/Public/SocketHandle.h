#pragma once

#include "CoreMinimal.h"
#include "MuduoNetPlatform.h"

namespace mnet
{
#if MUDUONET_PLATFORM_WINDOWS
	/** Windows 下是 SOCKET（UINT_PTR），不能塞进 int。 */
	using FSocketValue = uint64;
	inline constexpr FSocketValue InvalidSocketValue = static_cast<FSocketValue>(-1);
#else
	/** POSIX 下是文件描述符。 */
	using FSocketValue = int32;
	inline constexpr FSocketValue InvalidSocketValue = -1;
#endif

	/** 关闭原生句柄：Windows 用 closesocket，POSIX 用 close。 */
	MUDUOCORE_API void CloseSocketValue(FSocketValue Value);

	/**
	 * 原生 socket 句柄的 RAII 封装。
	 * 只负责生命周期，所有操作都走 FSocketOps，避免把平台头泄漏给调用方。
	 */
	class MUDUOCORE_API FSocketHandle
	{
	public:
		FSocketHandle() = default;
		explicit FSocketHandle(FSocketValue InValue) : Value(InValue) {}
		~FSocketHandle() { Reset(); }

		FSocketHandle(const FSocketHandle&) = delete;
		FSocketHandle& operator=(const FSocketHandle&) = delete;

		FSocketHandle(FSocketHandle&& Other) noexcept : Value(Other.Value)
		{
			Other.Value = InvalidSocketValue;
		}

		FSocketHandle& operator=(FSocketHandle&& Other) noexcept
		{
			if (this != &Other)
			{
				Reset();
				Value = Other.Value;
				Other.Value = InvalidSocketValue;
			}
			return *this;
		}

		bool IsValid() const { return Value != InvalidSocketValue; }
		FSocketValue Get() const { return Value; }

		FSocketValue Release()
		{
			const FSocketValue Result = Value;
			Value = InvalidSocketValue;
			return Result;
		}

		void Reset(FSocketValue InValue = InvalidSocketValue)
		{
			if (Value != InvalidSocketValue)
			{
				CloseSocketValue(Value);
			}
			Value = InValue;
		}

	private:
		FSocketValue Value = InvalidSocketValue;
	};
}
