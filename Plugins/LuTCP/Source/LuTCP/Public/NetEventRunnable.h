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

#include "NetEventRunnable.generated.h"

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
		FNetEvent_Delegate ReadCallback;
		FNetEvent_Delegate WriteCallback;
		FNetEvent_Delegate CloseCallback;
		FNetEvent_Delegate ErrorCallback;

	private:
		bool bAddToNetEventPublisher = false;
		bool EventHandling = false;
		int32 Index = -1;
		SOCKET SocketHandle = SOCKET_ERROR;
		int16 Events = 0;
		int16 REvents = 0;
		class FNetEventPublisher* NetEventPublisher = nullptr;
		//bool bSetOwner = false;
		//TWeakPtr<void> Owner;
	};

	//接受新的客户端连接
	class LUTCP_API FAcceptor final : public FNoncopyable
	{
	public:
		DECLARE_DELEGATE_TwoParams(FNewConnection_Delegate, FUniqueSocket, TSharedPtr<FInternetAddr>);

		FAcceptor(class FNetEventPublisher* InNetEventPublisher, TSharedPtr<FInternetAddr> InAddr);
		~FAcceptor();

		void Listen();
		void OnHandleRead();
		bool IsListening() const { return bListening; }
		FNewConnection_Delegate NewConnectionCallback;
	private:
		class FNetEventPublisher* NetEventPublisher = nullptr;
		FUniqueSocket ListenSocket;
		FChannel ListenChannel;
		bool bListening = false;
	};

	//连接到服务器
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
		TSharedPtr<FInternetAddr> ServerAddr;
		std::atomic<bool> bConnect = false;
		std::atomic<EConnectionState> State = EConnectionState::EDisconnected;
		TUniquePtr<FChannel> Channel;
	public:
		FNewConnection_Delegate NewConnectionCallback;
		FConnectFailed_Delegate ConnectFailedCallback;
	public:
		FConnector(class FNetEventPublisher* InNetEventPublisher, 
			TSharedPtr<FInternetAddr> InServerAddr);
		~FConnector();

		//void Start();
		void Start_IOThread();
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

	//网络事件发布者
	class LUTCP_API FNetEventPublisher final : public FNoncopyable
	{
	public:
		FNetEventPublisher();
		~FNetEventPublisher();

		void Quit() { bNetQuit = true; }

		//因为WSAPoll网络事件时，会等待10ms，如果添加任务时，IO线程正在等待，需要通知IO线程结束等待。
		void WeakUp();

		//添加任务到任务队列
		void QueueTask(const FIOThreadTask_Delegate& InTask);
		void QueueTask_IOThread(const FIOThreadTask_Delegate& InTask);
		
		void Poll_IOThread(int32 InTimeout, TArray<FChannel*>& OutActiveChannels);
		void UpdateChannel_IOThread(FChannel* InChannel);
		void RemoveChannel_IOThread(FChannel* InChannel);
		void NetRun_IOThread();
	private:
		void FillActiveChannels_IOThread(int32 InNumEvents, TArray<FChannel*>& OutActiveChannels);
		void CallPendingTasks_IOThread();
	private:
		std::atomic_bool bNetQuit = false;
		bool bNetRuning = false;
		TMap<SOCKET, FChannel*> SocketToChannels;
		TArray<WSAPOLLFD> AllPollfds;

		TArray<FChannel*> ActiveChannels;
		bool bHandleChannels = false;
		FChannel* CurrentActiveChannel = nullptr;

		//用于唤醒网络循环
		FUniqueSocket WeakupServerSocket;
		FUniqueSocket WeakupClientSocket;
		TSharedPtr<FInternetAddr> ServerAddr;
		TUniquePtr<FChannel> WeakupChannel;

		TArray<FIOThreadTask_Delegate> PendingTasks;    //挂起的任务队列
		FCriticalSection Mutex;
		bool bPendingCallTask = false;
	};

}


 
#endif

/**
 * 
 */
UCLASS()
class LUTCP_API UNetEventRunnable : public UObject
{
	GENERATED_BODY()
	
};

