#include "LengthHeaderCodec.h"

#include "Containers/StringConv.h"
#include "MuduoNetLog.h"

namespace mnet
{
	FLengthHeaderCodec::FLengthHeaderCodec(FMessageCallback InCallback)
		: MessageCallback(MoveTemp(InCallback))
	{
	}

	void FLengthHeaderCodec::SetMessageCallback(FMessageCallback InCallback)
	{
		MessageCallback = MoveTemp(InCallback);
	}

	void FLengthHeaderCodec::Encode(FBuffer& OutBuffer, const void* Data, int32 Length)
	{
		checkf(Length >= 0, TEXT("MuduoNet: 消息长度不能为负"));

		// 直接追加（而不是 prepend）：同一个 Buffer 里多次 Encode 时保持调用顺序。
		const uint8 Header[4] = {
			static_cast<uint8>((Length >> 24) & 0xFF),
			static_cast<uint8>((Length >> 16) & 0xFF),
			static_cast<uint8>((Length >> 8) & 0xFF),
			static_cast<uint8>(Length & 0xFF)
		};
		OutBuffer.Append(Header, 4);
		OutBuffer.Append(Data, Length);
	}

	void FLengthHeaderCodec::Send(const FTcpConnectionPtr& Conn, const void* Data, int32 Length)
	{
		if (!Conn.IsValid())
		{
			return;
		}

		FBuffer Buffer;
		Encode(Buffer, Data, Length);
		Conn->Send(MoveTemp(Buffer));
	}

	void FLengthHeaderCodec::Send(const FTcpConnectionPtr& Conn, const TArray<uint8>& Message)
	{
		Send(Conn, Message.GetData(), Message.Num());
	}

	void FLengthHeaderCodec::Send(const FTcpConnectionPtr& Conn, const FString& Message)
	{
		const FTCHARToUTF8 Utf8(*Message);
		Send(Conn, Utf8.Get(), Utf8.Length());
	}

	void FLengthHeaderCodec::OnMessage(const FTcpConnectionPtr& Conn, FBuffer* Buffer, double ReceiveTime)
	{
		if (Buffer == nullptr)
		{
			return;
		}

		while (Buffer->ReadableBytes() >= kHeaderLength)
		{
			const int32 Length = Buffer->PeekInt32();
			if (Length < 0 || Length > kMaxMessageLength)
			{
				UE_LOG(LogMuduoNet, Warning, TEXT("MuduoNet: 长度头非法（%d），关闭连接"), Length);
				if (Conn.IsValid())
				{
					Conn->ForceClose();
				}
				return;
			}

			// 半包：等下一次可读事件再处理。
			if (Buffer->ReadableBytes() < kHeaderLength + Length)
			{
				return;
			}

			Buffer->Retrieve(kHeaderLength);
			TArray<uint8> Message;
			Buffer->RetrieveAsArray(Length, Message);

			if (MessageCallback)
			{
				MessageCallback(Conn, Message, ReceiveTime);
			}
		}
	}
}
