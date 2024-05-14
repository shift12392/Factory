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
	// IdleBytes�����пռ䣬�����û���ȡ�˿ɶ��ռ�������ݣ�ReaderIndex�������ƶ�����ǰ�����г���һ�οռ䡣
	// WritableBytes���µ�����д���д�ռ䣬��WriterIndex��ʼ��
	class FBuffer
	{
		static const int8  ReserveSize = 8;    //Buffer�Ŀ�ʼԤ��8���ֽڵĴ�С���û������Զ�����8���ֽڵ���;��
		static const int32 InitialSize = 1024; //Buffer�ĳ�ʼ��С

		TArray<uint8> Data;
		int32 ReaderIndex = ReserveSize;
		int32 WriterIndex = ReserveSize;

	public:
		FBuffer(int32 InInitialSize = InitialSize)
		{
			Data.AddUninitialized(InInitialSize);
			check(InInitialSize >= ReserveSize);
		}

		//�õ��ɶ��ռ��С
		int32 ReadableBytes() const
		{
			return WriterIndex - ReaderIndex;
		}
		//�õ���д�ռ��С
		int32 WritableBytes() const
		{
			return Data.Num() - WriterIndex;
		}
		//�õ����пռ�Ĵ�С
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
				EmptyAll();    //�ɶ��ռ��е����ݶ����ˣ��ָ�������
		}

		void EmptyAll()
		{
			ReaderIndex = ReserveSize;
			WriterIndex = ReserveSize;
		}

		void EnsureWritableBytes(int32 InLen)
		{
			if (WritableBytes() < InLen)   //��д�ռ�̫С��������Data���飬ֱ������Ҫ���ȵĿ�д�ռ�
			{
				MakeSpace(InLen);
			}

			check(WritableBytes() >= InLen);
		}
		//��Socket��ȡ����
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

		//����Data���飬ֱ������Ҫ���ȵĿ�д�ռ�
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