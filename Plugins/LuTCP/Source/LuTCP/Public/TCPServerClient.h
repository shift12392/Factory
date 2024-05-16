// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "CoreMinimal.h"
#include "CoreTypes.h"
#include "Misc/Timespan.h"
#include "SocketTypes.h"
#include "UObject/NameTypes.h"
#include "UObject/UnrealNames.h"
#include "NetEventRunnable.h"
#include "Buffer.h"
#include "Acceptor.h"
#include "Connector.h"

namespace LuTCP
{
	DECLARE_DELEGATE_OneParam(FNetConnection_Delegate, TSharedPtr<class FNetConnection>);
	DECLARE_DELEGATE_OneParam(FWriteComplete_Delegate, TSharedPtr<class FNetConnection>);
	DECLARE_DELEGATE_OneParam(FCloseConnection_Delegate, TSharedPtr<class FNetConnection>);
	DECLARE_DELEGATE_TwoParams(FMessage_Delegate,TSharedPtr<class FNetConnection>, FBuffer);
	DECLARE_DELEGATE_TwoParams(FHighWaterMark_Delegate, TSharedPtr<class FNetConnection>, int32);


	class LUTCP_API FNetConnection final : public TSharedFromThis<FNetConnection>
	{
	public:
		DECLARE_DELEGATE_TwoParams(FClientMessage_Delegate, TSharedPtr<class FNetConnection>, FBuffer&);

	public:
		FNetConnection(FName InConnectionName, FNetEventPublisher* InNetEventPublisher, FUniqueSocket InSocket,
			TSharedPtr<FInternetAddr> InLocalAddr,
			TSharedPtr<FInternetAddr> InPeerAddr);

		~FNetConnection();

		FNetConnection_Delegate NetConnectionCallback;
		FWriteComplete_Delegate WriteCompleteCallback;
		FCloseConnection_Delegate CloseConnectionCallback;
		FClientMessage_Delegate       MessageCallback;
		FHighWaterMark_Delegate HighWaterMarkCallback;
	public:
		FName GetName() const { return ConnectionName; }
		FSocket* GetSocket() const { return Socket.Get(); }
		bool IsConnected() const { return State == EConnectionState::EConnected; }
		bool IsDisconnected() const { return State == EConnectionState::EDisconnected; }

		void Send(FBuffer &&InBuffer);
		void Send_IOThread(const uint8* InData, int32 InLen);
		void ForceClose();
		void ForceClose_IOThread();


		void OnConnectionEstablished_IOThread();
		void OnConnectionDestroyed_IOThread();
	private:
		void OnHandleRead_IOThread();
		void OnHandleWrite_IOThread();
		void OnHandleClose_IOThread();
		void OnHandleError_IOThread();

	private:
		enum class EConnectionState : uint8
		{
			EDisconnected,
			EConnecting,
			EConnected,
			EDisconnecting
		};
		std::atomic<EConnectionState> State = EConnectionState::EConnecting;   //UE的TAtomic<>已弃用
		FName ConnectionName;
		FNetEventPublisher* NetEventPublisher = nullptr;
		FUniqueSocket Socket;
		TUniquePtr<FChannel> SocketChannel;
		TSharedPtr<FInternetAddr> LocalAddr;
		TSharedPtr<FInternetAddr> PeerAddr;
		FBuffer InputBuffer;      
		FBuffer OutputBuffer;
		int32 HighWaterMark = 64 * 1024 * 1024;
	};

	class LUTCP_API FTCPServer final
	{
	public:
		FTCPServer(FNetEventPublisher* InNetEventPublisher, TSharedPtr<FInternetAddr> InAddr);
		~FTCPServer();

		void Start();
		void Start_IOThread();
		void Quit() { bQuit = true; }

		FNetConnection_Delegate NetConnectionCallback;   //连接建立/断开事件
		FWriteComplete_Delegate WriteCompleteCallback;   //写完成事件
		FMessage_Delegate       MessageCallback;         //来自客户端的消息事件
	private:
		FTCPServer(const FTCPServer& InOther) = delete;
		FTCPServer& operator=(const FTCPServer& InOther) = delete;

		void OnNetConnection_IOThread(TSharedPtr<FNetConnection> InNetConn);
		void OnMessage_IOThread(TSharedPtr<FNetConnection> InNetConn, FBuffer& InBuffer);
		void OnWriteComplete_IOThread(TSharedPtr<FNetConnection> InNetConn);

		//收到新的客户端连接。来自IO线程的事件回调。
		void OnNewConnection_IOThread(FUniqueSocket InSocket, TSharedPtr<FInternetAddr> InAddr);
		//移除客户端连接
		void OnRemoveConnection_IOThread(TSharedPtr<FNetConnection> InNetCon);
	private:
		FNetEventPublisher *NetEventPublisher;
		TUniquePtr<FAcceptor> Acceptor;
		TMap<FName, TSharedPtr<FNetConnection> >  AllConnections;
		int32 NetConnectionId = 0;

		std::atomic_bool bQuit = false;
	};

	class LUTCP_API FTCPClient final
	{
	public:
		FTCPClient(FNetEventPublisher* InNetEventPublisher, TSharedPtr<FInternetAddr> InServerAddr);
		~FTCPClient();

		void Connect();
		void Connect_I0Thread();
		//void Stop();
		void Quit() { bQuit = true; }

		FNetConnection_Delegate NetConnectionCallback;
		FWriteComplete_Delegate WriteCompleteCallback;
		FMessage_Delegate       MessageCallback;
	private:
		FTCPClient(const FTCPClient& InOther) = delete;
		FTCPClient& operator=(const FTCPClient& InOther) = delete;

		void OnNetConnection_IOThread(TSharedPtr<FNetConnection> InNetConn);
		void OnMessage_IOThread(TSharedPtr<class FNetConnection> InNetConn, FBuffer& InBuffer);
		void OnWriteComplete_IOThread(TSharedPtr<class FNetConnection> InNetConn);

		//收到新的客户端连接。来自IO线程的事件回调。
		void OnNewConnection_IOThread(FUniqueSocket InSocket);
		void OnConnectFailed_IOThread();
		//移除客户端连接
		void OnRemoveConnection_IOThread(TSharedPtr<FNetConnection> InNetCon);

	private:
		FNetEventPublisher*    NetEventPublisher;
		TSharedPtr<FConnector> Connector;
		TSharedPtr<FNetConnection> ServerConn;
		int32 NetConnectionId = 0;
		std::atomic_bool bQuit = false;
	};


	class LUTCP_API FServerIOThreadRunnable final : public FRunnable
	{
	public:
		FServerIOThreadRunnable(TUniquePtr<FTCPServer> InServer, TUniquePtr<FNetEventPublisher> InNetEventPublisher);

		void StartIOThread();
		void WaitStopThread();

	private:
		//--  FRunnable接口
		virtual bool Init() override
		{
			return true;
		}
		virtual uint32 Run() override;
		virtual void Stop() override { }
		virtual void Exit() override { }
		//-- FRunnable接口

		void Quit_IOThread();
	private:
		FRunnableThread* Thread = nullptr;
		TUniquePtr<FTCPServer> TCPServer = nullptr;
		TUniquePtr<FNetEventPublisher> NetEventPublisher = nullptr;
	};

	class LUTCP_API FClientIOThreadRunnable final : public FRunnable
	{
	public:
		FClientIOThreadRunnable(TUniquePtr<FTCPClient> InClient, TUniquePtr<FNetEventPublisher> InNetEventPublisher);

		void StartIOThread();
		void WaitStopThread();

	private:
		//--  FRunnable接口
		virtual bool Init() override
		{
			return true;
		}
		virtual uint32 Run() override;
		virtual void Stop() override { }
		virtual void Exit() override { }
		//-- FRunnable接口

		void Quit_IOThread();
	private:
		FRunnableThread* Thread = nullptr;
		TUniquePtr<FTCPClient> TCPClient = nullptr;
		TUniquePtr<FNetEventPublisher> NetEventPublisher = nullptr;
	};
}