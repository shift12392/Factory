// Fill out your copyright notice in the Description page of Project Settings.


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


LuTCP::FNetEventPublisher::FNetEventPublisher()
{
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get();
	TSharedRef<FInternetAddr> LocalAddr = SocketSubsystem->CreateInternetAddr(FNetworkProtocolTypes::IPv4);
	LocalAddr->SetAnyAddress();
	LocalAddr->SetPort(10002);

	WeakupServerSocket = SocketSubsystem->CreateUniqueSocket(/*NAME_Stream*/NAME_DGram, TEXT("Weakup Server Socket"), LocalAddr->GetProtocolType());
	if (WeakupServerSocket.IsValid())
	{
		WeakupServerSocket->SetNonBlocking(true);

		int32 NewPort = SocketSubsystem->BindNextPort(WeakupServerSocket.Get(), *LocalAddr, 1000, 1);
		if (NewPort > 0)
		{
			//WeakupServerSocket->Listen(2);

			ServerAddr = SocketSubsystem->CreateInternetAddr(FNetworkProtocolTypes::IPv4);
			ServerAddr->SetLoopbackAddress();
			ServerAddr->SetPort(NewPort);
		}
		else
		{
			UE_LOG(LogLuTCP, Fatal, TEXT("绑定唤醒Server Socket失败"));
		}
	}
	else
	{
		UE_LOG(LogLuTCP, Fatal, TEXT("创建唤醒Server Socket失败"));
	}

	WeakupClientSocket = SocketSubsystem->CreateUniqueSocket(/*NAME_Stream*/ NAME_DGram, TEXT("Weakup Client Socket"), ServerAddr->GetProtocolType());
	if (WeakupClientSocket.IsValid())
	{
		WeakupClientSocket->SetNonBlocking(true);
	}
	else
	{
		UE_LOG(LogLuTCP, Fatal, TEXT("创建唤醒Client Socket失败"));
	}

	WeakupChannel = MakeUnique<FChannel>(this, GetNativeSocket(WeakupServerSocket.Get()));
	WeakupChannel->ReadCallback.BindLambda([this]() {
		uint32 RecvBuf = 0;
		int32 Bytes = 0;
		TSharedPtr<FInternetAddr> ClientAddr;
		WeakupServerSocket->Recv((uint8*)&RecvBuf, sizeof(RecvBuf), Bytes);
		if (Bytes != sizeof(uint8))
		{
			UE_LOG(LogLuTCP, Error, TEXT("唤醒收到的字节数不对, Bytes = %d"),Bytes);
		}
	});
	WeakupChannel->EnableReading();
}

LuTCP::FNetEventPublisher::~FNetEventPublisher()
{
	WeakupChannel->DisableAll();
	WeakupChannel->RemoveFromPublisher();
	WeakupServerSocket->Close();
	WeakupClientSocket->Close();
}

void LuTCP::FNetEventPublisher::WeakUp()
{
	//WeakupClientSocket->Connect(*ServerAddr);
	//WeakupClientSocket->Close();
	uint8 Buf = 1;
	int32 Bytes = 0;
	WeakupClientSocket->SendTo(&Buf, 1, Bytes, *ServerAddr);
}

void LuTCP::FNetEventPublisher::QueueTask(const FIOThreadTask_Delegate& InTask)
{
	{
		FScopeLock Lock(&Mutex);
		PendingTasks.Add(InTask);
	}

	WeakUp();
}

void LuTCP::FNetEventPublisher::QueueTask_IOThread(const FIOThreadTask_Delegate& InTask)
{
	{
		FScopeLock Lock(&Mutex);
		PendingTasks.Add(InTask);
	}

	if (bPendingCallTask)
		WeakUp();
}

void LuTCP::FNetEventPublisher::Poll_IOThread(int32 InTimeout, TArray<FChannel*>& OutActiveChannels)
{
	//将 fd 成员设置为负值的 fdarray 成员将被忽略，并且在返回时它们的 revents 将被设置为 POLLNVAL
	int32 NumEvents = ::WSAPoll(AllPollfds.GetData(), AllPollfds.Num(), InTimeout);
	int32 LastError = ::WSAGetLastError();
	if (NumEvents > 0)
	{
		UE_LOG(LogLuTCP, Display, TEXT("%d个网络事件发生"), NumEvents);
		FillActiveChannels_IOThread(NumEvents, OutActiveChannels);
	}
	else if (NumEvents == 0)
	{
		//UE_LOG(LogLuTCP, Display, TEXT("没有任何事情发生"));
	}
	else
	{
		UE_LOG(LogLuTCP, Fatal, TEXT("网络错误，关闭程序，LastError = %d"), LastError);
	}
}

void LuTCP::FNetEventPublisher::UpdateChannel_IOThread(FChannel* InChannel)
{
	if (InChannel->GetIndex() < 0)
	{
		check(!InChannel->IsNoEvent());

		check(SocketToChannels.Find(InChannel->GetSocket()) == nullptr);
		int32 NewItem = AllPollfds.AddUninitialized();
		WSAPOLLFD& FD = AllPollfds[NewItem];
		FD.fd = InChannel->GetSocket();
		FD.events = InChannel->GetEvents();
		FD.revents = 0;
		InChannel->SetIndex(NewItem);
		SocketToChannels.Add(InChannel->GetSocket(),InChannel);
	}
	else
	{
		check(SocketToChannels.Find(InChannel->GetSocket()) != nullptr);
		check(SocketToChannels[InChannel->GetSocket()] == InChannel);
		int32 ChannelIndex = InChannel->GetIndex();
		check(0 <= ChannelIndex && ChannelIndex < AllPollfds.Num());
		WSAPOLLFD& FD = AllPollfds[ChannelIndex];
		check(FD.fd == InChannel->GetSocket());
		FD.fd = InChannel->GetSocket();
		FD.events = InChannel->GetEvents();
		FD.revents = 0;
	}
}

void LuTCP::FNetEventPublisher::FillActiveChannels_IOThread(int32 InNumEvents, TArray<FChannel*>& OutActiveChannels)
{
	for (int32 i = 0; i < AllPollfds.Num() && InNumEvents > 0; ++i)
	{
		WSAPOLLFD& FD = AllPollfds[i];
		if (FD.revents > 0)
		{
			check(SocketToChannels.Find(FD.fd) != nullptr);
			FChannel* FindChannel = *SocketToChannels.Find(FD.fd);
			check(FindChannel->GetSocket() == FD.fd);
			FindChannel->SetREvents(FD.revents);
			OutActiveChannels.Add(FindChannel);
			--InNumEvents;
		}
	}
}

void LuTCP::FNetEventPublisher::NetRun_IOThread()
{
	check(!bNetRuning);
	bNetRuning = true;
	bNetQuit = false;

	UE_LOG(LogLuTCP, Display, TEXT("网络循环开始"));

	while (!bNetQuit.load())
	{
		ActiveChannels.Empty();
		//等待10毫秒
		//获得网络事件
		Poll_IOThread(10, ActiveChannels);

		//处理网络事件
		bHandleChannels = true;
		for (FChannel* Channel : ActiveChannels)
		{
			CurrentActiveChannel = Channel;
			CurrentActiveChannel->HandleEvent();
		}
		CurrentActiveChannel = nullptr;
		bHandleChannels = false;

		//执行挂起的任务
		CallPendingTasks_IOThread();
	}

	UE_LOG(LogLuTCP, Display, TEXT("网络循环结束"));
	bNetRuning = false;
}

void LuTCP::FNetEventPublisher::CallPendingTasks_IOThread()
{
	bPendingCallTask = true;

	TArray<FIOThreadTask_Delegate> CallTasks;
	{
		//需要加锁
		FScopeLock Lock(&Mutex);
		Swap(PendingTasks, CallTasks);
	}

	for (auto &TaskIte : CallTasks)
	{
		TaskIte.ExecuteIfBound();
	}

	bPendingCallTask = false;
}

void LuTCP::FNetEventPublisher::RemoveChannel_IOThread(FChannel* InChannel)
{
	check(SocketToChannels.Find(InChannel->GetSocket()) != nullptr);
	check(SocketToChannels[InChannel->GetSocket()] == InChannel);
	int32 ChannelIndex = InChannel->GetIndex();
	check(0 <= ChannelIndex && ChannelIndex < AllPollfds.Num());
	WSAPOLLFD& FD = AllPollfds[ChannelIndex];
	check(FD.fd == InChannel->GetSocket());
	SocketToChannels.Remove(InChannel->GetSocket());
	if (InChannel->GetIndex() == AllPollfds.Num() - 1)
	{
		AllPollfds.Pop();
	}
	else
	{
		//和最后一个元素交换，然后再移除
		SOCKET EndSocket = AllPollfds.Last().fd;
		AllPollfds.Swap(ChannelIndex, AllPollfds.Num() - 1);
		SocketToChannels[EndSocket]->SetIndex(ChannelIndex);
		AllPollfds.Pop();
	}

}



#endif	//PLATFORM_HAS_BSD_SOCKET_FEATURE_WINSOCKETS && (PLATFORM_HAS_BSD_SOCKETS || PLATFORM_HAS_BSD_IPV6_SOCKETS)

