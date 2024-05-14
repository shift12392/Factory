// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TCPServerClient.h"
#include "NetEventRunnable.h"

#if !(PLATFORM_HAS_BSD_SOCKET_FEATURE_WINSOCKETS && (PLATFORM_HAS_BSD_SOCKETS || PLATFORM_HAS_BSD_IPV6_SOCKETS))

#error LuTCP仅支持Window平台

#endif

#include "NativeSocketExporter.h"

LuTCP::FNetConnection::FNetConnection(FName InConnectionName, FNetEventPublisher* InNetEventPublisher,
    FUniqueSocket InSocket, TSharedPtr<FInternetAddr> InLocalAddr, TSharedPtr<FInternetAddr> InPeerAddr)
    :ConnectionName(InConnectionName), NetEventPublisher(InNetEventPublisher),
    Socket(MoveTemp(InSocket)), SocketChannel(new FChannel(InNetEventPublisher, GetNativeSocket(Socket.Get())))
    ,LocalAddr(InLocalAddr),PeerAddr(InPeerAddr)
{
    SocketChannel-> ReadCallback.BindRaw(this, &FNetConnection::OnHandleRead_IOThread);
    SocketChannel->WriteCallback.BindRaw(this, &FNetConnection::OnHandleWrite_IOThread);
    SocketChannel->CloseCallback.BindRaw(this, &FNetConnection::OnHandleClose_IOThread);
    SocketChannel->ErrorCallback.BindRaw(this, &FNetConnection::OnHandleError_IOThread);
    
    //允许为套接字连接发送保持连接数据包。 在 ATM 套接字上不支持 (会导致错误) 。
    bool opt = 1;
    ::setsockopt(GetNativeSocket(Socket.Get()), SOL_SOCKET, SO_KEEPALIVE, (char*) & opt, sizeof(opt));
}

LuTCP::FNetConnection::~FNetConnection()
{
	UE_LOG(LogLuTCP, Display, TEXT("NetConnection：%s 正在销毁"),
		*ConnectionName.ToString());

    check(State.load() == EConnectionState::EDisconnected);
}

void LuTCP::FNetConnection::Send(FBuffer&& InBuffer)
{
    if (State.load() == EConnectionState::EConnected)
    {
        TSharedPtr<FNetConnection> ThisPtr = AsShared();
        NetEventPublisher->QueueTask(FIOThreadTask_Delegate::CreateLambda([ThisPtr, InBuffer]() {
            ThisPtr->Send_IOThread(InBuffer.Peek(), InBuffer.ReadableBytes());
            }));
    }
}

void LuTCP::FNetConnection::Send_IOThread(const uint8* InData, int32 InLen)
{
    if (State.load() == EConnectionState::EDisconnected)
    {
        UE_LOG(LogLuTCP, Error, TEXT("NetConnection：%s 已经EDisconnected，不能发送数据"), 
            *ConnectionName.ToString());
        return;
    }

    bool bError = false;
    int32 BytesSend = 0;
    int32 BytesRemaining = InLen;
	// 如果输出队列中，没任何数据，则直接发送
    if (!SocketChannel->IsWriting() && OutputBuffer.ReadableBytes() == 0)
    {
        if (Socket->Send(InData, InLen, BytesSend))
        {
            BytesRemaining = InLen - BytesSend;
            if (0 == BytesRemaining && WriteCompleteCallback.IsBound())
                WriteCompleteCallback.Execute(AsShared());
        }
        else
        {
			UE_LOG(LogLuTCP, Error, TEXT("NetConnection：%s 发送数据失败，ErrorId = %d"),
				*ConnectionName.ToString(), (int32)WSAGetLastError());
            bError = true;
        }
    }

    //还有数据没有发送完
    check(BytesRemaining <= InLen);
    if (!bError && BytesRemaining > 0)
    {
        int32 OldSize = OutputBuffer.ReadableBytes();
        if (OldSize + BytesRemaining > HighWaterMark
            && OldSize < HighWaterMark
            && HighWaterMarkCallback.IsBound())
        {
            int32 NewSize = OldSize + BytesRemaining;
            TSharedPtr<FNetConnection> ThisPtr = AsShared();
            HighWaterMarkCallback.Execute(ThisPtr, NewSize);
        }

        //添加到OutputBuffer，开启写事件，后面的数据会在OnHandleWrite_IOThread中继续发送。
        OutputBuffer.Append(InData, InLen);  
        if (!SocketChannel->IsWriting())
        {
            SocketChannel->EnableWriting();
        }
    }
}

void LuTCP::FNetConnection::ForceClose()
{
    if (State.load() == EConnectionState::EConnected || State.load() == EConnectionState::EDisconnecting)
    {
        State = EConnectionState::EDisconnecting;
    }
    NetEventPublisher->QueueTask(FIOThreadTask_Delegate::CreateSP(AsShared(), &FNetConnection::ForceClose_IOThread));
}

void LuTCP::FNetConnection::ForceClose_IOThread()
{
    if (State.load() == EConnectionState::EConnected || State.load() == EConnectionState::EDisconnecting)
    {
        State = EConnectionState::EDisconnecting;
        OnHandleClose_IOThread();
    }
}

void LuTCP::FNetConnection::OnConnectionEstablished_IOThread()
{
    check(State.load() == EConnectionState::EConnecting);
    State = EConnectionState::EConnected;
    SocketChannel->EnableReading();
    NetConnectionCallback.Execute(AsShared());
}

void LuTCP::FNetConnection::OnConnectionDestroyed_IOThread()
{
    if (State.load() == EConnectionState::EConnected)
    {
        State = EConnectionState::EDisconnected;
        SocketChannel->DisableAll();
        NetConnectionCallback.Execute(AsShared());
    }
    SocketChannel->RemoveFromPublisher();
}

void LuTCP::FNetConnection::OnHandleRead_IOThread()
{
    int32 BytesRead = InputBuffer.RecvFormSocket(Socket.Get());

	UE_LOG(LogLuTCP, Display, TEXT("NetConnection：%s 收到 %d 个字节数据"), 
        *ConnectionName.ToString(), BytesRead);
    
    if (BytesRead > 0 )
    {
        MessageCallback.Execute(AsShared(), InputBuffer);
    }
	else if (BytesRead == 0)
	{
		OnHandleClose_IOThread();
	}
    else
    {
        UE_LOG(LogLuTCP, Error, TEXT("NetConnection：%s读取出错"), *ConnectionName.ToString());
        OnHandleError_IOThread();
    }
}
void LuTCP::FNetConnection::OnHandleWrite_IOThread()
{
    if (SocketChannel->IsWriting())
    {
        int32 BytesSend = 0;
        if (Socket->Send(OutputBuffer.Peek(), OutputBuffer.ReadableBytes(), BytesSend))
        {
            if (BytesSend > 0)
            {
                OutputBuffer.AddToReaderIndex(BytesSend);
            }
            if (OutputBuffer.ReadableBytes() == 0)
            {
                SocketChannel->DisableWriting();
                WriteCompleteCallback.ExecuteIfBound(AsShared());
            }

            if (State.load() == EConnectionState::EDisconnecting)
            {
                Socket->Shutdown(ESocketShutdownMode::Write);
            }
        }
        else
        {
			UE_LOG(LogLuTCP, Error, TEXT("NetConnection：%s 发送数据失败，ErrorCode = %d"),
				*ConnectionName.ToString(), (int32)WSAGetLastError());
        }
    }
    else
    {
        UE_LOG(LogLuTCP, Warning , TEXT("NetConnection：%s不能写"), *ConnectionName.ToString());
    }
}
void LuTCP::FNetConnection::OnHandleClose_IOThread()
{
    check(State == EConnectionState::EConnected || State == EConnectionState::EDisconnecting);
    State = EConnectionState::EDisconnected;
    SocketChannel->DisableAll();
    TSharedPtr<FNetConnection> NetConnection(AsShared());
    NetConnectionCallback.Execute(NetConnection);

    CloseConnectionCallback.Execute(NetConnection);
}
void LuTCP::FNetConnection::OnHandleError_IOThread()
{
	UE_LOG(LogLuTCP, Error, TEXT("NetConnection：%s 发生错误，ErrorCode = %d"),
		*ConnectionName.ToString(), (int32)WSAGetLastError());
}

static void DefaultNetConnectionCallback(TSharedPtr<LuTCP::FNetConnection> InCon)
{
	TSharedRef<FInternetAddr> LocalAddr = ISocketSubsystem::Get()->CreateInternetAddr();
    TSharedRef<FInternetAddr> PeerAddr = ISocketSubsystem::Get()->CreateInternetAddr();
	FString PeerIP = PeerAddr->ToString(true);
	InCon->GetSocket()->GetAddress(*LocalAddr);
	FString LocalIP = LocalAddr->ToString(true);

	UE_LOG(LogLuTCP, Display, TEXT("收到新的客户端连接，LocalIP = %s，PeerIP = %s"), *LocalIP, *PeerIP);
}

static void DefaultMessageCallback(TSharedPtr<LuTCP::FNetConnection> InCon, LuTCP::FBuffer InBuffer)
{
    InBuffer.EmptyAll();
}


LuTCP::FTCPServer::FTCPServer(FNetEventPublisher *InNetEventPublisher, TSharedPtr<FInternetAddr> InAddr)
    :NetEventPublisher(InNetEventPublisher),
    Acceptor(new FAcceptor(InNetEventPublisher,InAddr))
{
    Acceptor->NewConnectionCallback.BindRaw(this, &FTCPServer::OnNewConnection_IOThread);
    NetConnectionCallback.BindStatic(&DefaultNetConnectionCallback);
    MessageCallback.BindStatic(&DefaultMessageCallback);
}

LuTCP::FTCPServer::~FTCPServer()
{
    UE_LOG(LogLuTCP, Display, TEXT("FTCPServer::~FTCPServer"));

    for (auto &Ite : AllConnections)
    {
        TSharedPtr<FNetConnection> NetCon(Ite.Value);
        Ite.Value.Reset();

        NetCon->OnConnectionDestroyed_IOThread();

        /*
        NetEventPublisher->QueueTask(FIOThreadTask_Delegate::CreateLambda([NetCon]() {
            NetCon->OnConnectionDestroyed_IOThread();
            }));
        */
    }
}

void LuTCP::FTCPServer::Start()
{
    check(!Acceptor->IsListening());
    NetEventPublisher->QueueTask(FIOThreadTask_Delegate::CreateRaw(Acceptor.Get(), &FAcceptor::Listen));
}

void LuTCP::FTCPServer::Start_IOThread()
{
    check(!Acceptor->IsListening());
    Acceptor->Listen();
}

void LuTCP::FTCPServer::OnNetConnection_IOThread(TSharedPtr<FNetConnection> InNetConn)
{
	if (bQuit.load())
	{
		return;
	}

    AsyncTask(ENamedThreads::GameThread, [this,InNetConn]() {
        NetConnectionCallback.ExecuteIfBound(InNetConn);
        });
}

void LuTCP::FTCPServer::OnMessage_IOThread(TSharedPtr<FNetConnection> InNetConn, FBuffer& InBuffer)
{
	if (bQuit.load())
	{
		return;
	}

	AsyncTask(ENamedThreads::GameThread, [this, InNetConn, InBuffer]() {
		MessageCallback.ExecuteIfBound(InNetConn, InBuffer);
		});
}

void LuTCP::FTCPServer::OnWriteComplete_IOThread(TSharedPtr<FNetConnection> InNetConn)
{
    if (bQuit.load())
    {
        return;
    }

	AsyncTask(ENamedThreads::GameThread, [this, InNetConn]() {
		WriteCompleteCallback.ExecuteIfBound(InNetConn);
		});
}

void LuTCP::FTCPServer::OnNewConnection_IOThread(FUniqueSocket InSocket, TSharedPtr<FInternetAddr> InPeerAddr)
{
    if(bQuit.load() )
    {
        InSocket->Close();
        return;
    }

    TSharedRef<FInternetAddr> LocalAddr = ISocketSubsystem::Get()->CreateInternetAddr();
    FString PeerIP = InPeerAddr->ToString(true);
    InSocket->GetAddress(*LocalAddr);
    FString LocalIP = LocalAddr->ToString(true);

    UE_LOG(LogLuTCP, Display, TEXT("收到新的客户端连接，Socket = %ld，LocalIP = %s，PeerIP = %s"),
        GetNativeSocket(InSocket.Get()), *LocalIP, *PeerIP);
    
    FString Name = FString::Printf(TEXT("PeerIP:%s - ID = %d"), *PeerIP, NetConnectionId);
    ++NetConnectionId;

    UE_LOG(LogLuTCP, Display, TEXT("新的客户端连接Name = %s"), *Name);

    TSharedPtr<FNetConnection> NewConnection = MakeShared<FNetConnection>(FName(Name),
        NetEventPublisher, MoveTemp(InSocket), LocalAddr, InPeerAddr);
    AllConnections.Add(FName(Name), NewConnection);

    NewConnection->NetConnectionCallback.BindRaw(this, &FTCPServer::OnNetConnection_IOThread);
    NewConnection->WriteCompleteCallback.BindRaw(this, &FTCPServer::OnWriteComplete_IOThread);
    NewConnection->CloseConnectionCallback.BindRaw(this, &FTCPServer::OnRemoveConnection_IOThread);
    NewConnection->MessageCallback.BindRaw(this, &FTCPServer::OnMessage_IOThread);
    NewConnection->OnConnectionEstablished_IOThread();
}

void LuTCP::FTCPServer::OnRemoveConnection_IOThread(TSharedPtr<FNetConnection> InNetCon)
{
    FName Name = InNetCon->GetName();
    UE_LOG(LogLuTCP, Display, TEXT("客户端连接移除，Name = %s"), *Name.ToString());
    
    check(AllConnections.Find(Name) != nullptr);

    //注：在这里，FNetConnection对象可能不会销毁，因为用户可能还会持有FNetConnection对象
    AllConnections.Remove(Name);    

    //这里不能直接调用OnConnectionDestroyed_IOThread，因为OnRemoveConnection_IOThread执行完之后，
    //如果FNetConnection对象被析构，则FNetConnection的SocketChannel对象也被析构，但此时
    //FChannel::HandleEvent函数还没执行完，则会崩。
    NetEventPublisher->QueueTask_IOThread(FIOThreadTask_Delegate::CreateLambda([InNetCon]() {
        InNetCon->OnConnectionDestroyed_IOThread();
        }));
}

LuTCP::FTCPClient::FTCPClient(FNetEventPublisher* InNetEventPublisher, TSharedPtr<FInternetAddr> InServerAddr)
    :NetEventPublisher(InNetEventPublisher),Connector(new FConnector(InNetEventPublisher,InServerAddr))
{
    Connector->NewConnectionCallback.BindRaw(this, &FTCPClient::OnNewConnection_IOThread);
}

void LuTCP::FTCPClient::OnNetConnection_IOThread(TSharedPtr<FNetConnection> InNetConn)
{
    if (bQuit.load())
        return;

	AsyncTask(ENamedThreads::GameThread, [this, InNetConn]() {
		NetConnectionCallback.ExecuteIfBound(InNetConn);
		});
}

void LuTCP::FTCPClient::OnMessage_IOThread(TSharedPtr<FNetConnection> InNetConn, FBuffer& InBuffer)
{
	if (bQuit.load())
		return;

	AsyncTask(ENamedThreads::GameThread, [this, InNetConn, InBuffer]() {
		MessageCallback.ExecuteIfBound(InNetConn, InBuffer);
		});
}

void LuTCP::FTCPClient::OnWriteComplete_IOThread(TSharedPtr<FNetConnection> InNetConn)
{
	if (bQuit.load())
		return;

	AsyncTask(ENamedThreads::GameThread, [this, InNetConn]() {
		WriteCompleteCallback.ExecuteIfBound(InNetConn);
		});
}

void LuTCP::FTCPClient::OnNewConnection_IOThread(FUniqueSocket InSocket)
{
	TSharedRef<FInternetAddr> LocalAddr = ISocketSubsystem::Get()->CreateInternetAddr();
	TSharedRef<FInternetAddr> PeerAddr = ISocketSubsystem::Get()->CreateInternetAddr();
    InSocket->GetPeerAddress(*PeerAddr);
    FString PeerIP = PeerAddr->ToString(true);
	InSocket->GetAddress(*LocalAddr);
	FString LocalIP = LocalAddr->ToString(true);

	FString Name = FString::Printf(TEXT("PeerIP:%s - ID = %d"), *PeerIP, NetConnectionId);
	++NetConnectionId;

	UE_LOG(LogLuTCP, Display, TEXT("连接到服务端，Name = %s，LocalIP = %s，PeerIP = %s"),
        *Name, *LocalIP, *PeerIP);

    ServerConn = MakeShared<FNetConnection>(FName(Name),
		NetEventPublisher, MoveTemp(InSocket), LocalAddr, PeerAddr);

    ServerConn->NetConnectionCallback.BindRaw(this,&FTCPClient::OnNetConnection_IOThread);
    ServerConn->WriteCompleteCallback.BindRaw(this,&FTCPClient::OnWriteComplete_IOThread);
    ServerConn->CloseConnectionCallback.BindRaw(this, &FTCPClient::OnRemoveConnection_IOThread);
    ServerConn->MessageCallback.BindRaw(this,&FTCPClient::OnMessage_IOThread);
    ServerConn->OnConnectionEstablished_IOThread();
}

void LuTCP::FTCPClient::OnRemoveConnection_IOThread(TSharedPtr<FNetConnection> InNetCon)
{
    check(InNetCon == ServerConn);
	NetEventPublisher->QueueTask_IOThread(FIOThreadTask_Delegate::CreateLambda([InNetCon]() {
		InNetCon->OnConnectionDestroyed_IOThread();
		}));
    ServerConn.Reset();
}

void LuTCP::FTCPClient::OnConnectFailed_IOThread()
{
	//连接服务器出错，这里先不处理了
}

LuTCP::FTCPClient::~FTCPClient()
{
    if (ServerConn)
    {
        ServerConn->OnConnectionDestroyed_IOThread();
    }

    Connector->Stop_IOThread();
}

void LuTCP::FTCPClient::Connect()
{
    Connector->Start_IOThread();
}

void LuTCP::FTCPClient::Connect_I0Thread()
{
	Connector->Start_IOThread();
}


//void LuTCP::FTCPClient::Stop()
//{
//
//}



LuTCP::FServerIOThreadRunnable::FServerIOThreadRunnable(TUniquePtr<FTCPServer> InServer, TUniquePtr<FNetEventPublisher> InNetEventPublisher)
	:TCPServer(MoveTemp(InServer)),
    NetEventPublisher(MoveTemp(InNetEventPublisher))
{

}

uint32 LuTCP::FServerIOThreadRunnable::Run()
{
    TCPServer->Start_IOThread();
	NetEventPublisher->NetRun_IOThread();
    NetEventPublisher.Reset();
	return 0;
}

void LuTCP::FServerIOThreadRunnable::Quit_IOThread()
{
    TCPServer.Reset();
    NetEventPublisher->Quit();
}

void LuTCP::FServerIOThreadRunnable::StartIOThread()
{
	if (Thread != nullptr)
		return;

    UE_LOG(LogLuTCP, Display, TEXT("网络IO线程开始启动"));
	Thread = FRunnableThread::Create(this, TEXT("FIOThreadRunnable"));
}

void LuTCP::FServerIOThreadRunnable::WaitStopThread()
{
    TCPServer->Quit();
	NetEventPublisher->QueueTask(FIOThreadTask_Delegate::CreateRaw(this, &FServerIOThreadRunnable::Quit_IOThread));
	Thread->WaitForCompletion();
    UE_LOG(LogLuTCP, Display, TEXT("网络IO线程结束"));
}




LuTCP::FClientIOThreadRunnable::FClientIOThreadRunnable(TUniquePtr<FTCPClient> InClient, TUniquePtr<FNetEventPublisher> InNetEventPublisher)
	:TCPClient(MoveTemp(InClient)),
	NetEventPublisher(MoveTemp(InNetEventPublisher))
{

}

uint32 LuTCP::FClientIOThreadRunnable::Run()
{
    TCPClient->Connect_I0Thread();
	NetEventPublisher->NetRun_IOThread();
	NetEventPublisher.Reset();
	return 0;
}

void LuTCP::FClientIOThreadRunnable::Quit_IOThread()
{
    TCPClient.Reset();
    FNetEventPublisher* NetEvent = NetEventPublisher.Get();
    NetEventPublisher->QueueTask(FIOThreadTask_Delegate::CreateLambda( [NetEvent]() {
        NetEvent->Quit();
        }));
}

void LuTCP::FClientIOThreadRunnable::StartIOThread()
{
	if (Thread != nullptr)
		return;

	UE_LOG(LogLuTCP, Display, TEXT("网络IO线程开始启动"));
	Thread = FRunnableThread::Create(this, TEXT("FIOThreadRunnable"));
}

void LuTCP::FClientIOThreadRunnable::WaitStopThread()
{
	TCPClient->Quit();
	NetEventPublisher->QueueTask(FIOThreadTask_Delegate::CreateRaw(this, &FClientIOThreadRunnable::Quit_IOThread));
	Thread->WaitForCompletion();
	UE_LOG(LogLuTCP, Display, TEXT("网络IO线程结束"));
}


