// Fill out your copyright notice in the Description page of Project Settings.

#include "Connector.h"
#include "NetEventRunnable.h"

#if PLATFORM_HAS_BSD_SOCKET_FEATURE_WINSOCKETS && (PLATFORM_HAS_BSD_SOCKETS || PLATFORM_HAS_BSD_IPV6_SOCKETS)
#include "NativeSocketExporter.h"



LuTCP::FConnector::FConnector(FNetEventPublisher* InNetEventPublisher,
	TSharedPtr<FInternetAddr> InServerAddr)
	:NetEventPublisher(InNetEventPublisher), ServerAddr(InServerAddr)
{

}

LuTCP::FConnector::~FConnector()
{
	UE_LOG(LogLuTCP, Display, TEXT("FConnector::~FConnector"));
}

//void LuTCP::FConnector::Start()
//{
//	TSharedPtr<FConnector> ThisPtr = AsShared();
//	bConnect = true;
//	NetEventPublisher->QueueTask(FIOThreadTask_Delegate::CreateLambda(
//		[ThisPtr]() {
//			ThisPtr->Connect_IOThread();
//		}));
//}

void LuTCP::FConnector::Start_IOThread()
{
	check(State.load() == EConnectionState::EDisconnected);
	bConnect = true;
	Connect_IOThread();
}

//void LuTCP::FConnector::Stop()
//{
//	bConnect = false;
//	NetEventPublisher->QueueTask(FIOThreadTask_Delegate::CreateSP(AsShared(), &FConnector::Stop_IOThread));
//}

void LuTCP::FConnector::Stop_IOThread()
{
	bConnect = false;
	if (State == EConnectionState::EConnecting)
	{
		State = EConnectionState::EDisconnected;
		RemoveChannelAndDelete_IOThread();
	}
}

void LuTCP::FConnector::Connect_IOThread()
{
	FSocket *Socket = ISocketSubsystem::Get()->CreateSocket(NAME_Stream, TEXT("Client Socket"), ServerAddr->GetProtocolType());
	Socket->SetNonBlocking();
	if (Socket->Connect(*ServerAddr))
	{
		UE_LOG(LogLuTCP, Display, TEXT("FConnector：开始连接"));
		State = EConnectionState::EConnecting;
		Channel.Reset(new FChannel(NetEventPublisher, GetNativeSocket(Socket)));

		TSharedPtr<FConnector> ThisPtr = AsShared();
		Channel->WriteCallback.BindLambda([ThisPtr, Socket]() {
			//注意：必须给TUniquePtr提供Deleter，使用FSocketDeleter，否则不会调用Socket的Close()函数。
			FUniqueSocket TempSocket(Socket, FSocketDeleter(ISocketSubsystem::Get()));
			ThisPtr->OnHandleWrite_IOThread(MoveTemp(TempSocket));
			});

		//Channel->WriteCallback.BindSP(AsShared(), &FConnector::OnHandleWrite_IOThread, MoveTemp(Socket));
		Channel->ErrorCallback.BindSP(AsShared(), &FConnector::OnHandleError_IOThread);
		Channel->EnableWriting();
	}
	else
	{
		//处理错误
		//如果错误是WSAECONNREFUSED、WSAENETUNREACH、WSAETIMEDOUT，则可以重连，这里先不处理了，都报错。
		UE_LOG(LogLuTCP, Display, TEXT("FConnector：连接失败"));
		FUniqueSocket TempSocket(Socket);
		CallErrorCallback_IOThread(MoveTemp(TempSocket));
	}
}

void LuTCP::FConnector::RemoveChannelAndDelete_IOThread()
{
	Channel->DisableAll();
	Channel->RemoveFromPublisher();
	TSharedPtr<FConnector> ConPtr = AsShared();

	//不能在这里销毁Channel
	NetEventPublisher->QueueTask_IOThread(FIOThreadTask_Delegate::CreateSP(AsShared(), &FConnector::DeleteChannel_IOThread));

}

void LuTCP::FConnector::DeleteChannel_IOThread()
{
	Channel.Reset();
}

void LuTCP::FConnector::OnHandleWrite_IOThread(FUniqueSocket InSocket)
{
	UE_LOG(LogLuTCP, Display, TEXT("FConnector：连接中"));
	if (State.load() == EConnectionState::EConnecting)
	{
		RemoveChannelAndDelete_IOThread();

		TSharedPtr<FInternetAddr> NewServerAddr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
		InSocket->GetPeerAddress(*NewServerAddr);
		TSharedPtr<FInternetAddr> NewLocalAddr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
		InSocket->GetAddress(*NewLocalAddr);
		//处理自连接
		if (*NewLocalAddr == *NewServerAddr)
		{
			UE_LOG(LogLuTCP, Warning, TEXT("FConnector：连接失败，自连接"));
			CallErrorCallback_IOThread(MoveTemp(InSocket));
			return;
		}

		State = EConnectionState::EConnected;
		if (bConnect.load())
		{
			NewConnectionCallback.Execute(MoveTemp(InSocket));
		}
		else
		{
			UE_LOG(LogLuTCP, Warning, TEXT("FConnector：连接失败，bConnect为false"));
			CallErrorCallback_IOThread(MoveTemp(InSocket));
		}
	}
}

void LuTCP::FConnector::OnHandleError_IOThread()
{
	UE_LOG(LogLuTCP, Warning, TEXT("FConnector：连接失败，出现错误"));

	bConnect = false;
	State = EConnectionState::EDisconnected;
	ConnectFailedCallback.ExecuteIfBound();
}

void LuTCP::FConnector::CallErrorCallback_IOThread(FUniqueSocket InSocket)
{
	bConnect = false;
	State = EConnectionState::EDisconnected;
	InSocket->Close();
	ConnectFailedCallback.ExecuteIfBound();
}

#endif	//PLATFORM_HAS_BSD_SOCKET_FEATURE_WINSOCKETS && (PLATFORM_HAS_BSD_SOCKETS || PLATFORM_HAS_BSD_IPV6_SOCKETS)

