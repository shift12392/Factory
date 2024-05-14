// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Buffer.h"
#include "Sockets.h"

int32 LuTCP::FBuffer::RecvFormSocket(FSocket* InSocket)
{
	//��ջ�Ϸ���64k��С��Buffer�������ܵĶ�Ķ�ȡ���ݡ�
    //�߳�ջĬ�ϴ�СΪ1MB���������ջ���������������ʱ���԰�TempBuffer�Ĵ�С��С�㡣
	uint8 TempBuffer[65536];
	int32 BytesRead = 0;
	if (InSocket->Recv(TempBuffer, 65536, BytesRead))
	{
		if (BytesRead > 0)
		{
			const int32 WBytes = WritableBytes();
			if (BytesRead <= WBytes)
			{
				FMemory::Memcpy(Begin() + WriterIndex, TempBuffer, BytesRead);
				WriterIndex += BytesRead;
			}
			else
			{
				Append(TempBuffer, BytesRead);
			}
		}
		return BytesRead;
	}
	else
	{
		return -1;
	}
}
