// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Buffer.h"
#include "Sockets.h"

int32 LuTCP::FBuffer::RecvFormSocket(FSocket* InSocket)
{
	//在栈上分配64k大小的Buffer，尽可能的多的读取数据。
    //线程栈默认大小为1MB。如果出现栈溢出，程序会崩，这时可以把TempBuffer的大小减小点。
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
