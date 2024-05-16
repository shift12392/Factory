// Fill out your copyright notice in the Description page of Project Settings.


#include "Acceptor.h"
#include "NetEventRunnable.h"


#if PLATFORM_HAS_BSD_SOCKET_FEATURE_WINSOCKETS && (PLATFORM_HAS_BSD_SOCKETS || PLATFORM_HAS_BSD_IPV6_SOCKETS)
#include "NativeSocketExporter.h"



LuTCP::FAcceptor::FAcceptor(FNetEventPublisher* InNetEventPublisher, TSharedPtr<FInternetAddr> InAddr)
	:NetEventPublisher(InNetEventPublisher),
	ListenSocket(ISocketSubsystem::Get()->CreateUniqueSocket(NAME_Stream, TEXT("LuTCP Server listen socket"), InAddr->GetProtocolType())),
	ListenChannel(InNetEventPublisher, (SOCKET)GetNativeSocket(ListenSocket.Get())),
	bListening(false)
{
	ListenSocket->SetReuseAddr(true);
	bool bBindSceesss = ListenSocket->Bind(*InAddr);
	if (!bBindSceesss)
		UE_LOG(LogLuTCP, Fatal, TEXT("绑定端口失败，端口：%d，LastError = %d"), InAddr->GetPort(),::WSAGetLastError());
	ListenChannel.ReadCallback.BindRaw(this, &FAcceptor::OnHandleRead);
}

LuTCP::FAcceptor::~FAcceptor()
{
	bListening = false;
	ListenChannel.DisableAll();
	ListenChannel.RemoveFromPublisher();
}

void LuTCP::FAcceptor::Listen()
{
	bListening = true;
	if(!ListenSocket->Listen(SOMAXCONN))
		UE_LOG(LogLuTCP, Fatal, TEXT("Listen失败，LastError = %d"), ::WSAGetLastError());

	ListenChannel.EnableReading();
}

void LuTCP::FAcceptor::OnHandleRead()
{
	FUniqueSocket ClientSocket(ListenSocket->Accept(TEXT("LuTCP Client Socket")),FSocketDeleter(ISocketSubsystem::Get()));
	if (ClientSocket.IsValid())
	{
		TSharedRef<FInternetAddr> ClientAddr = ISocketSubsystem::Get()->CreateInternetAddr();
		ClientSocket->GetPeerAddress(*ClientAddr);
		if (NewConnectionCallback.IsBound())
		{
			NewConnectionCallback.Execute(MoveTemp(ClientSocket), ClientAddr);
		}
		else
		{
			ClientSocket->Close();
		}
	}
	else
	{
		UE_LOG(LogLuTCP, Error, TEXT("获取客户端连接失败"));
	}
}

#endif	//PLATFORM_HAS_BSD_SOCKET_FEATURE_WINSOCKETS && (PLATFORM_HAS_BSD_SOCKETS || PLATFORM_HAS_BSD_IPV6_SOCKETS)

