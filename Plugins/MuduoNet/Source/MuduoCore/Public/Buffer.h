#pragma once

#include "CoreMinimal.h"
#include "SocketHandle.h"

namespace mnet
{
	/**
	 * muduo Buffer 的移植版本。
	 *
	 * +------------------+------------------+------------------+
	 * | prependable bytes|  readable bytes  |  writable bytes  |
	 * |                  |    (CONTENT)     |                  |
	 * +------------------+------------------+------------------+
	 * 0      <=      ReaderIndex   <=   WriterIndex    <=     Size
	 *
	 * 预留 kCheapPrepend 字节给 LengthHeaderCodec 做 prepend，避免每次编码都搬移数据。
	 * 非线程安全：只在所属 IO 线程使用；回调期间有效，跨线程请自行拷贝。
	 */
	class MUDUOCORE_API FBuffer
	{
	public:
		static constexpr int32 kCheapPrepend = 8;
		static constexpr int32 kInitialSize = 1024;

		explicit FBuffer(int32 InitialSize = kInitialSize);

		int32 ReadableBytes() const { return WriterIndex - ReaderIndex; }
		int32 WritableBytes() const { return Storage.Num() - WriterIndex; }
		int32 PrependableBytes() const { return ReaderIndex; }

		const uint8* Peek() const { return Storage.GetData() + ReaderIndex; }
		uint8* PeekMutable() { return Storage.GetData() + ReaderIndex; }
		const uint8* Begin() const { return Storage.GetData(); }
		int32 Capacity() const { return Storage.Num(); }

		void Retrieve(int32 Length);
		void RetrieveAll();
		TArray<uint8> RetrieveAllAsArray();
		FString RetrieveAllAsString();
		void RetrieveAsArray(int32 Length, TArray<uint8>& Out);

		void Append(const void* Data, int32 Length);
		void EnsureWritableBytes(int32 Length);
		void Prepend(const void* Data, int32 Length);

		/** 按网络字节序（大端）在可读区之前写入。 */
		void PrependInt8(int8 Value);
		void PrependInt16(int16 Value);
		void PrependInt32(int32 Value);
		void PrependInt64(int64 Value);

		/** 按网络字节序（大端）读取并消费。 */
		int8 ReadInt8();
		int16 ReadInt16();
		int32 ReadInt32();
		int64 ReadInt64();

		/** 按网络字节序（大端）读取但不消费，长度不足时返回 0。 */
		int32 PeekInt32() const;

		/** 从 socket 读一次数据到缓冲区，返回读取字节数；<0 时 OutError 为平台错误码。 */
		int32 ReadFd(FSocketValue Fd, int32& OutError);

		/** 把可读数据搬到头部并缩容到 Reserve + 可读长度。 */
		void Shrink(int32 Reserve);

		void Swap(FBuffer& Other);

	private:
		void MakeSpace(int32 Length);

		TArray<uint8> Storage;
		int32 ReaderIndex = kCheapPrepend;
		int32 WriterIndex = kCheapPrepend;
	};
}
