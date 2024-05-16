// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "HAL/ThreadSafeCounter.h"
#include "HAL/Runnable.h"

#include "SocketSubsystem.h"
#include "Sockets.h"
#include "Channel.h"

#if PLATFORM_HAS_BSD_SOCKET_FEATURE_WINSOCKETS && (PLATFORM_HAS_BSD_SOCKETS || PLATFORM_HAS_BSD_IPV6_SOCKETS)

namespace LuTCP
{
	//连接到服务器
	//注意这个类和FNetEventPublisher要在一个线程，其他线程不要调用这个类的函数。
	class LUTCP_API FConnector final : public TSharedFromThis<FConnector>
	{
	public:
		DECLARE_DELEGATE_OneParam(FNewConnection_Delegate, FUniqueSocket InSocket);
		DECLARE_DELEGATE(FConnectFailed_Delegate);
	private:
		enum class EConnectionState : uint8
		{
			EDisconnected,
			EConnecting,
			EConnected,
		};
		class FNetEventPublisher* NetEventPublisher;
		TSharedPtr<FInternetAddr> ServerAddr;   //服务端地址
		std::atomic<bool> bConnect = false;
		std::atomic<EConnectionState> State = EConnectionState::EDisconnected;
		TUniquePtr<FChannel> Channel;    
	public:
		FNewConnection_Delegate NewConnectionCallback;    //连接到服务端的事件
		FConnectFailed_Delegate ConnectFailedCallback;    //连接失败事件
	public:
		FConnector(class FNetEventPublisher* InNetEventPublisher, 
			TSharedPtr<FInternetAddr> InServerAddr);
		~FConnector();

		//void Start();
		void Start_IOThread();        //开始连接服务端
		//void Stop();
		void Stop_IOThread();
		void OnHandleWrite_IOThread(FUniqueSocket InSocket);
	private:
		
		void Connect_IOThread();
		void RemoveChannelAndDelete_IOThread();
		void DeleteChannel_IOThread();

		void OnHandleError_IOThread();
		void CallErrorCallback_IOThread(FUniqueSocket InSocket);
	};
}

#endif

