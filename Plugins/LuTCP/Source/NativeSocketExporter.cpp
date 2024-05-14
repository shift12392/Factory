// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NativeSocketExporter.h"

#if PLATFORM_HAS_BSD_SOCKET_FEATURE_WINSOCKETS && (PLATFORM_HAS_BSD_SOCKETS || PLATFORM_HAS_BSD_IPV6_SOCKETS)

#include "Windows/SocketsWindows.h"

uint64 LuTCP::GetNativeSocket(FSocket* InSocket)
{
	return static_cast<FSocketBSD*>(InSocket)->GetNativeSocket();
}

#endif