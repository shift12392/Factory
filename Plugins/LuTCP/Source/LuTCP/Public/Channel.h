// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "HAL/ThreadSafeCounter.h"
#include "HAL/Runnable.h"

#include "SocketSubsystem.h"
#include "Sockets.h"

#if PLATFORM_HAS_BSD_SOCKET_FEATURE_WINSOCKETS && (PLATFORM_HAS_BSD_SOCKETS || PLATFORM_HAS_BSD_IPV6_SOCKETS)

#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"

#include <winsock2.h>
#include <ws2tcpip.h>

//typedef int32 SOCKLEN;

#include "Windows/HideWindowsPlatformTypes.h"

#else
#error LuTCP仅仅支持Window平台 !!!   
#endif


#if PLATFORM_HAS_BSD_SOCKET_FEATURE_WINSOCKETS && (PLATFORM_HAS_BSD_SOCKETS || PLATFORM_HAS_BSD_IPV6_SOCKETS)

DECLARE_LOG_CATEGORY_EXTERN(LogLuTCP, Log, Log);

namespace LuTCP
{
	class FNoncopyable
	{
	public:
		FNoncopyable(const FNoncopyable&) = delete;
		void operator=(const FNoncopyable&) = delete;

	protected:
		FNoncopyable() = default;
		~FNoncopyable() = default;
	};

	DECLARE_DELEGATE(FNetEvent_Delegate);
	DECLARE_DELEGATE(FIOThreadTask_Delegate);

	//网络事件订阅者
	//处理传入的Socket的网络事件
	//注意这个类和FNetEventPublisher要在一个线程，其他线程不要调用这个类的函数。
	class LUTCP_API FChannel final : public FNoncopyable   //不能复制
	{
	public:
		FChannel(class FNetEventPublisher* InNetEventPublisher, SOCKET InSocket);
		~FChannel();
	public:
		int32 GetIndex() const { return Index; }
		void SetIndex(int32 InIndex) { Index = InIndex; }

		SOCKET GetSocket()const { return SocketHandle; }
		int16 GetEvents() const { return Events; }
		int16 GetREvents() const { return REvents; }
		void SetREvents(int16 InREvents) { REvents = InREvents; }
		
		//void SetOwner(const TSharedPtr<void>& InOwner);

		void EnableReading() { Events |= POLLIN; UpdateToPublisher(); }
		void DisableReading() { Events &= ~POLLIN; UpdateToPublisher(); }
		void EnableWriting() { Events |= POLLOUT; UpdateToPublisher(); }
		void DisableWriting() { Events &= ~POLLOUT; UpdateToPublisher(); }
		void DisableAll() { Events = 0; UpdateToPublisher(); }
		bool IsWriting() const { return Events & POLLOUT; }
		bool IsReading() const { return Events & POLLIN; }
		bool IsNoEvent() const { return Events == 0; }
		void RemoveFromPublisher();

		void HandleEvent();   
	private:
		void UpdateToPublisher();
		FString EventsToString(SOCKET InFD, int32 InEv);
	public:
		FNetEvent_Delegate ReadCallback;   //Socket可读事件
		FNetEvent_Delegate WriteCallback;  //Socket可写事件
		FNetEvent_Delegate CloseCallback;  //Socket断开连接事件
		FNetEvent_Delegate ErrorCallback;  //Socket出错事件

	private:
		bool bAddToNetEventPublisher = false;
		bool EventHandling = false;
		int32 Index = -1;    //在FNetEventPublisher::AllPollfds中的索引
		SOCKET SocketHandle = SOCKET_ERROR;    //要处理的Socket
		int16 Events = 0;   //关注Socket的哪类事件
		int16 REvents = 0;  //可以处理哪类事件
		class FNetEventPublisher* NetEventPublisher = nullptr;
	};


}
#endif

