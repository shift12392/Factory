// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "HAL/ThreadSafeCounter.h"

#include "SocketSubsystem.h"
#include "Sockets.h"
#include "Channel.h"



#if PLATFORM_HAS_BSD_SOCKET_FEATURE_WINSOCKETS && (PLATFORM_HAS_BSD_SOCKETS || PLATFORM_HAS_BSD_IPV6_SOCKETS)

namespace LuTCP
{
	//接受新的客户端连接
	//注意这个类和FNetEventPublisher要在一个线程，其他线程不要调用这个类的函数。
	class LUTCP_API FAcceptor final : public FNoncopyable
	{
	public:
		DECLARE_DELEGATE_TwoParams(FNewConnection_Delegate, FUniqueSocket, TSharedPtr<FInternetAddr>);

		FAcceptor(class FNetEventPublisher* InNetEventPublisher, TSharedPtr<FInternetAddr> InAddr);
		~FAcceptor();

		void Listen();   //开启监听
		void OnHandleRead();
		bool IsListening() const { return bListening; }

		FNewConnection_Delegate NewConnectionCallback;    //新的客户端连接事件
	private:
		class FNetEventPublisher* NetEventPublisher = nullptr;
		FUniqueSocket ListenSocket;
		FChannel ListenChannel;
		bool bListening = false;
	};
}
 
#endif

