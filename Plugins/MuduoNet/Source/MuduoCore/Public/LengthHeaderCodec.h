#pragma once

#include "Buffer.h"
#include "CoreMinimal.h"
#include "TcpConnection.h"
#include "Templates/Function.h"

namespace mnet
{
	/**
	 * 4 字节大端长度头 + 消息体的编解码器（muduo LengthHeaderCodec 的移植）。
	 *
	 * 用法：
	 *   Conn->SetMessageCallback([&Codec](auto& C, FBuffer* B, double T) { Codec.OnMessage(C, B, T); });
	 *   Codec.Send(Conn, Data, Length);
	 *
	 * 只负责拆包/粘包，回调里拿到的是一整条消息。
	 */
	class MUDUOCORE_API FLengthHeaderCodec
	{
	public:
		using FMessageCallback = TFunction<void(const FTcpConnectionPtr&, const TArray<uint8>&, double)>;

		static constexpr int32 kHeaderLength = 4;
		/** 防止长度头被伪造导致超大分配。 */
		static constexpr int32 kMaxMessageLength = 64 * 1024 * 1024;

		explicit FLengthHeaderCodec(FMessageCallback InCallback = nullptr);

		void SetMessageCallback(FMessageCallback InCallback);

		/** 作为 FTcpConnection 的 MessageCallback 使用。 */
		void OnMessage(const FTcpConnectionPtr& Conn, FBuffer* Buffer, double ReceiveTime);

		void Send(const FTcpConnectionPtr& Conn, const void* Data, int32 Length);
		void Send(const FTcpConnectionPtr& Conn, const TArray<uint8>& Message);
		void Send(const FTcpConnectionPtr& Conn, const FString& Message);

		/** 只做编码，便于单测。 */
		static void Encode(FBuffer& OutBuffer, const void* Data, int32 Length);

	private:
		FMessageCallback MessageCallback;
	};
}
