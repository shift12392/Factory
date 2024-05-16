// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "HAL/ThreadSafeCounter.h"
#include "HAL/Runnable.h"

#include "SocketSubsystem.h"
#include "Sockets.h"
#include "Channel.h"

#include "NetEventRunnable.generated.h"

#if PLATFORM_HAS_BSD_SOCKET_FEATURE_WINSOCKETS && (PLATFORM_HAS_BSD_SOCKETS || PLATFORM_HAS_BSD_IPV6_SOCKETS)

namespace LuTCP
{
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

		void UpdateChannel_IOThread(FChannel* InChannel);   //更新网络事件订阅者
		void RemoveChannel_IOThread(FChannel* InChannel);   //移除网络事件订阅者

		//网络事件循环
		void NetRun_IOThread();
	private:
		void FillActiveChannels_IOThread(int32 InNumEvents, TArray<FChannel*>& OutActiveChannels);
		void CallPendingTasks_IOThread();
	private:
		std::atomic_bool bNetQuit = false;
		bool bNetRuning = false;
		TMap<SOCKET, FChannel*> SocketToChannels;    //网络事件订阅者们
		TArray<WSAPOLLFD> AllPollfds;                

		TArray<FChannel*> ActiveChannels;     
		bool bHandleChannels = false;
		FChannel* CurrentActiveChannel = nullptr;

		//用于唤醒网络循环
		FUniqueSocket WeakupServerSocket;
		FUniqueSocket WeakupClientSocket;
		TSharedPtr<FInternetAddr> ServerAddr;
		TUniquePtr<FChannel> WeakupChannel;

		//挂起的任务队列
		TArray<FIOThreadTask_Delegate> PendingTasks;    
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

