// Fill out your copyright notice in the Description page of Project Settings.

#include "Channel.h"
#include "NetEventRunnable.h"


#if PLATFORM_HAS_BSD_SOCKET_FEATURE_WINSOCKETS && (PLATFORM_HAS_BSD_SOCKETS || PLATFORM_HAS_BSD_IPV6_SOCKETS)
#include "NativeSocketExporter.h"

DEFINE_LOG_CATEGORY(LogLuTCP);

LuTCP::FChannel::FChannel(FNetEventPublisher* InNetEventPublisher, SOCKET InSocket)
	:SocketHandle(InSocket), NetEventPublisher(InNetEventPublisher)
{

}

LuTCP::FChannel::~FChannel()
{
	ReadCallback.Unbind();
	WriteCallback.Unbind();
    CloseCallback.Unbind();
	ErrorCallback.Unbind();
}

void LuTCP::FChannel::RemoveFromPublisher()
{
	check(IsNoEvent());
	bAddToNetEventPublisher = false;
	NetEventPublisher->RemoveChannel_IOThread(this);
}

void LuTCP::FChannel::HandleEvent()
{
	EventHandling = true;

	UE_LOG(LogLuTCP, Display, TEXT("FChannel::HandleEvent: %s"), *EventsToString(SocketHandle, REvents));

	if ((REvents & POLLHUP) && !(REvents & POLLIN))
	{
		CloseCallback.ExecuteIfBound();
	}

	if (REvents & POLLNVAL)
	{
		UE_LOG(LogLuTCP, Display, TEXT("FD = %d Is POLLNVAL"), POLLNVAL);
		check(false);
	}

	if (REvents & (POLLERR | POLLNVAL))
	{
		ErrorCallback.ExecuteIfBound();
	}
	if (REvents & POLLIN)
	{
		ReadCallback.ExecuteIfBound();
	}
	if (REvents & POLLOUT)
	{
		WriteCallback.ExecuteIfBound();
	}

	EventHandling = false;
}

void LuTCP::FChannel::UpdateToPublisher()
{
	bAddToNetEventPublisher = true;
	NetEventPublisher->UpdateChannel_IOThread(this);
}

FString LuTCP::FChannel::EventsToString(SOCKET InFD, int32 InEv)
{
	FString Str = FString::Printf(TEXT("SOCKET = %ld, REvent = "), InFD);

	if (InEv & POLLIN)
		Str += "IN ";
	if (InEv & POLLPRI)
		Str += "PRI ";
	if (InEv & POLLOUT)
		Str += "OUT ";
	if (InEv & POLLHUP)
		Str += "HUP ";
	if (InEv & POLLWRBAND)
		Str += "WRBAND ";
	if (InEv & POLLERR)
		Str += "ERR ";
	if (InEv & POLLNVAL)
		Str += "NVAL ";

	return Str;
}

#endif	//PLATFORM_HAS_BSD_SOCKET_FEATURE_WINSOCKETS && (PLATFORM_HAS_BSD_SOCKETS || PLATFORM_HAS_BSD_IPV6_SOCKETS)

