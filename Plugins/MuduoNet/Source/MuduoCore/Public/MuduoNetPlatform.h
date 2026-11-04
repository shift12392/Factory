#pragma once

#include "CoreMinimal.h"
#include "HAL/Platform.h"

// Build.cs 会为受支持的平台定义 MUDUONET_PLATFORM_*；这里给出兜底映射，
// 保证单独编译某个 .cpp 或 IDE 解析时不会把平台判断误判成 0。
#ifndef MUDUONET_PLATFORM_WINDOWS
	#if PLATFORM_WINDOWS
		#define MUDUONET_PLATFORM_WINDOWS 1
	#else
		#define MUDUONET_PLATFORM_WINDOWS 0
	#endif
#endif

#ifndef MUDUONET_PLATFORM_LINUX
	#if PLATFORM_LINUX
		#define MUDUONET_PLATFORM_LINUX 1
	#else
		#define MUDUONET_PLATFORM_LINUX 0
	#endif
#endif

#if !MUDUONET_PLATFORM_WINDOWS && !MUDUONET_PLATFORM_LINUX
	#error "MuduoNet: 当前平台尚未实现（当前仅支持 Win64 与 Linux）。请补充 Platform/SocketOps*.cpp、Poller 与 Wakeup 实现后放开。"
#endif
