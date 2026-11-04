#include "Buffer.h"

#include "Containers/StringConv.h"
#include "SocketOps.h"

namespace mnet
{
	namespace
	{
		void StoreBigEndian(uint8* Out, uint64 Value, int32 Bytes)
		{
			for (int32 Index = 0; Index < Bytes; ++Index)
			{
				Out[Index] = static_cast<uint8>((Value >> (8 * (Bytes - 1 - Index))) & 0xFF);
			}
		}

		uint64 LoadBigEndian(const uint8* In, int32 Bytes)
		{
			uint64 Value = 0;
			for (int32 Index = 0; Index < Bytes; ++Index)
			{
				Value = (Value << 8) | static_cast<uint64>(In[Index]);
			}
			return Value;
		}
	}

	FBuffer::FBuffer(int32 InitialSize)
	{
		checkf(InitialSize > 0, TEXT("FBuffer 初始容量必须大于 0"));
		Storage.SetNumZeroed(kCheapPrepend + InitialSize);
		ReaderIndex = kCheapPrepend;
		WriterIndex = kCheapPrepend;
	}

	void FBuffer::Retrieve(int32 Length)
	{
		checkf(Length >= 0 && Length <= ReadableBytes(), TEXT("FBuffer::Retrieve 越界：Length=%d, Readable=%d"), Length, ReadableBytes());
		if (Length < ReadableBytes())
		{
			ReaderIndex += Length;
		}
		else
		{
			RetrieveAll();
		}
	}

	void FBuffer::RetrieveAll()
	{
		ReaderIndex = kCheapPrepend;
		WriterIndex = kCheapPrepend;
	}

	TArray<uint8> FBuffer::RetrieveAllAsArray()
	{
		TArray<uint8> Result;
		Result.Append(Peek(), ReadableBytes());
		RetrieveAll();
		return Result;
	}

	FString FBuffer::RetrieveAllAsString()
	{
		FString Result;
		if (ReadableBytes() > 0)
		{
			const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Peek()), ReadableBytes());
			Result = FString(Converted.Length(), Converted.Get());
		}
		RetrieveAll();
		return Result;
	}

	void FBuffer::RetrieveAsArray(int32 Length, TArray<uint8>& Out)
	{
		checkf(Length >= 0 && Length <= ReadableBytes(), TEXT("FBuffer::RetrieveAsArray 越界"));
		Out.Reset();
		Out.Append(Peek(), Length);
		Retrieve(Length);
	}

	void FBuffer::EnsureWritableBytes(int32 Length)
	{
		if (WritableBytes() < Length)
		{
			MakeSpace(Length);
		}
		checkf(WritableBytes() >= Length, TEXT("FBuffer::EnsureWritableBytes 失败"));
	}

	void FBuffer::Append(const void* Data, int32 Length)
	{
		if (Length <= 0)
		{
			return;
		}

		EnsureWritableBytes(Length);
		FMemory::Memcpy(Storage.GetData() + WriterIndex, Data, Length);
		WriterIndex += Length;
	}

	void FBuffer::Prepend(const void* Data, int32 Length)
	{
		checkf(Length >= 0 && Length <= PrependableBytes(), TEXT("FBuffer::Prepend 预留空间不足：Length=%d, Prependable=%d"), Length, PrependableBytes());
		ReaderIndex -= Length;
		FMemory::Memcpy(Storage.GetData() + ReaderIndex, Data, Length);
	}

	void FBuffer::PrependInt8(int8 Value)
	{
		uint8 Raw = 0;
		StoreBigEndian(&Raw, static_cast<uint64>(static_cast<uint8>(Value)), 1);
		Prepend(&Raw, 1);
	}

	void FBuffer::PrependInt16(int16 Value)
	{
		uint8 Raw[2] = { 0, 0 };
		StoreBigEndian(Raw, static_cast<uint64>(static_cast<uint16>(Value)), 2);
		Prepend(Raw, 2);
	}

	void FBuffer::PrependInt32(int32 Value)
	{
		uint8 Raw[4] = { 0, 0, 0, 0 };
		StoreBigEndian(Raw, static_cast<uint64>(static_cast<uint32>(Value)), 4);
		Prepend(Raw, 4);
	}

	void FBuffer::PrependInt64(int64 Value)
	{
		uint8 Raw[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
		StoreBigEndian(Raw, static_cast<uint64>(Value), 8);
		Prepend(Raw, 8);
	}

	int8 FBuffer::ReadInt8()
	{
		checkf(ReadableBytes() >= 1, TEXT("FBuffer::ReadInt8 数据不足"));
		const int8 Value = static_cast<int8>(LoadBigEndian(Peek(), 1));
		Retrieve(1);
		return Value;
	}

	int16 FBuffer::ReadInt16()
	{
		checkf(ReadableBytes() >= 2, TEXT("FBuffer::ReadInt16 数据不足"));
		const int16 Value = static_cast<int16>(static_cast<uint16>(LoadBigEndian(Peek(), 2)));
		Retrieve(2);
		return Value;
	}

	int32 FBuffer::ReadInt32()
	{
		checkf(ReadableBytes() >= 4, TEXT("FBuffer::ReadInt32 数据不足"));
		const int32 Value = static_cast<int32>(static_cast<uint32>(LoadBigEndian(Peek(), 4)));
		Retrieve(4);
		return Value;
	}

	int64 FBuffer::ReadInt64()
	{
		checkf(ReadableBytes() >= 8, TEXT("FBuffer::ReadInt64 数据不足"));
		const int64 Value = static_cast<int64>(LoadBigEndian(Peek(), 8));
		Retrieve(8);
		return Value;
	}

	int32 FBuffer::PeekInt32() const
	{
		if (ReadableBytes() < 4)
		{
			return 0;
		}
		return static_cast<int32>(static_cast<uint32>(LoadBigEndian(Peek(), 4)));
	}

	int32 FBuffer::ReadFd(FSocketValue Fd, int32& OutError)
	{
		OutError = 0;

		// muduo 的做法：栈上 64KB 扩展缓冲，避免每次读都要扩容或多次系统调用。
		uint8 Extrabuf[65536];

		const int32 Writable = WritableBytes();
		int32 SavedError = 0;
		const int32 Read = FSocketOps::ReadIntoBuffers(
			Fd,
			Writable > 0 ? Storage.GetData() + WriterIndex : nullptr,
			Writable,
			Extrabuf,
			static_cast<int32>(sizeof(Extrabuf)),
			SavedError);

		if (Read <= 0)
		{
			OutError = SavedError;
			return Read;
		}

		if (Read <= Writable)
		{
			WriterIndex += Read;
		}
		else
		{
			WriterIndex = Storage.Num();
			Append(Extrabuf, Read - Writable);
		}

		return Read;
	}

	void FBuffer::Shrink(int32 Reserve)
	{
		FBuffer Compact(Reserve > 0 ? Reserve : 1);
		Compact.Append(Peek(), ReadableBytes());
		Swap(Compact);
	}

	void FBuffer::Swap(FBuffer& Other)
	{
		TArray<uint8> TempStorage = MoveTemp(Storage);
		Storage = MoveTemp(Other.Storage);
		Other.Storage = MoveTemp(TempStorage);

		const int32 TempReaderIndex = ReaderIndex;
		const int32 TempWriterIndex = WriterIndex;
		ReaderIndex = Other.ReaderIndex;
		WriterIndex = Other.WriterIndex;
		Other.ReaderIndex = TempReaderIndex;
		Other.WriterIndex = TempWriterIndex;
	}

	void FBuffer::MakeSpace(int32 Length)
	{
		if (WritableBytes() + PrependableBytes() < Length + kCheapPrepend)
		{
			Storage.SetNumUninitialized(WriterIndex + Length);
		}
		else
		{
			checkf(kCheapPrepend < ReaderIndex, TEXT("FBuffer::MakeSpace 前置空间异常"));
			const int32 Readable = ReadableBytes();
			FMemory::Memmove(Storage.GetData() + kCheapPrepend, Storage.GetData() + ReaderIndex, Readable);
			ReaderIndex = kCheapPrepend;
			WriterIndex = ReaderIndex + Readable;
		}
	}
}
