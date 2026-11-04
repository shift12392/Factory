#pragma once

#include "CoreMinimal.h"
#include "MuduoNetPlatform.h"

#if MUDUONET_PLATFORM_WINDOWS

#include "Windows/AllowWindowsPlatformTypes.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include "Windows/HideWindowsPlatformTypes.h"

#elif MUDUONET_PLATFORM_LINUX

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

#endif
