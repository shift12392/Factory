// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "CoreMinimal.h"

namespace LuTCP
{
	// +--------------------+------------------+------------------+
	// |     Idle bytes     |  Readable bytes  |  Writable bytes  |
	// |                    |     (CONTENT)    |                  |
	// +--------------------+------------------+------------------+
	// |                    |                  |                  |
	// 0      <=      ReaderIndex   <=   WriterIndex    <=     size
	//
	// IdleBytes：空闲空间，由于用户读取了可读空间里的数据，ReaderIndex会往后移动，则前面会空闲出来一段空间。
	// WritableBytes：新的数据写入可写空间，从WriterIndex开始。
	class FBuffer
	{
		static const int8  ReserveSize = 8;    //Buffer的开始预留8个字节的大小，用户可以自定义这8个字节的用途。
		static const int32 InitialSize = 1024; //Buffer的初始大小

		TArray<uint8> Data;
		int32 ReaderIndex = ReserveSize;
		int32 WriterIndex = ReserveSize;

	public:
		FBuffer(int32 InInitialSize = InitialSize)
		{
			Data.AddUninitialized(InInitialSize);
			check(InInitialSize >= ReserveSize);
		}

		//得到可读空间大小
		int32 ReadableBytes() const
		{
			return WriterIndex - ReaderIndex;
		}
		//得到可写空间大小
		int32 WritableBytes() const
		{
			return Data.Num() - WriterIndex;
		}
		//得到空闲空间的大小
		int32 IdleBytes() const
		{
			return ReaderIndex;
		}

		const uint8* Peek() const
		{
			return Begin() + ReaderIndex;
		}

		void AddtoWriterIndex(int32 InLen)
		{
			check(InLen <= WritableBytes());
			WriterIndex += InLen;
		}

		void AddToReaderIndex(int32 InLen)
		{
			check(InLen <= ReadableBytes());
			if (InLen < ReadableBytes())
				ReaderIndex += InLen;
			else
				EmptyAll();    //可读空间中的数据读完了，恢复索引。
		}

		void EmptyAll()
		{
			ReaderIndex = ReserveSize;
			WriterIndex = ReserveSize;
		}

		void EnsureWritableBytes(int32 InLen)
		{
			if (WritableBytes() < InLen)   //可写空间太小，则整理Data数组，直至有需要长度的可写空间
			{
				MakeSpace(InLen);
			}

			check(WritableBytes() >= InLen);
		}
		//从Socket读取数据
		int32 RecvFormSocket(class FSocket* InSocket);

		void Append(const uint8* InData, int32 InLen)
		{
			EnsureWritableBytes(InLen);
			//Data.Insert(InData, InLen, WriterIndex);
			FMemory::Memcpy(Begin() + WriterIndex, InData, InLen);
			AddtoWriterIndex(InLen);
		}
	private:
		uint8* Begin()
		{
			return Data.GetData();
		}

		const uint8* Begin() const
		{
			return Data.GetData();
		}

		//整理Data数组，直至有需要长度的可写空间
		void MakeSpace(int32 InLen)
		{
			if (WritableBytes() + IdleBytes() < InLen + ReserveSize)
			{
				Data.Reserve(WriterIndex + InLen);
			}
			else
			{
				check(ReserveSize < ReaderIndex);
				const int32 RBytes = ReadableBytes();
				FMemory::Memmove(Begin() + ReserveSize, Begin() + ReaderIndex, RBytes);
				ReaderIndex = ReserveSize;
				WriterIndex = ReaderIndex + RBytes;
				check(RBytes == ReadableBytes());
			}
		}

	};
}