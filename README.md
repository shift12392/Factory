TCP网络通信插件在Plugins/LuTCP。

使用示例如下。

服务端初始化（见UFactoryNetSubsystem::Initialize函数）：
```cpp

//创建服务端TCP
{
  //创建网络事件发布者
	TUniquePtr<LuTCP::FNetEventPublisher> NetEventPublisher = MakeUnique<LuTCP::FNetEventPublisher>();
  //创建IP地址
	TSharedRef<FInternetAddr> LocalAddr = SocketSubsystem->CreateInternetAddr(FNetworkProtocolTypes::IPv4);
	LocalAddr->SetAnyAddress();
	LocalAddr->SetPort(TwinPort);
  //创建FTCPServer
	TUniquePtr<LuTCP::FTCPServer> TCPServer = MakeUnique<LuTCP::FTCPServer>(NetEventPublisher.Get(), LocalAddr);
	TCPServer->MessageCallback.BindUObject(this, &UFactoryNetSubsystem::OnTCPMessage);
	TCPServer->NetConnectionCallback.BindUObject(this, &UFactoryNetSubsystem::OnTCPNetConnection);

	//由FServerIOThreadRunnable管理FTCPServer对象和FNetEventPublisher对象
	ServerIOThread.Reset(new LuTCP::FServerIOThreadRunnable(MoveTemp(TCPServer), MoveTemp(NetEventPublisher)));
	ServerIOThread->StartIOThread();   //开启网络IO线程
}

```

服务端关闭（见UFactoryNetSubsystem::Deinitialize函数）：
```cpp
	if (ServerIOThread.IsValid())
	{
		//等待网络IO线程关闭
		//同时关闭TCPServer。
		ServerIOThread->WaitStopThread();    
	}
```

客户端初始化（见UFactoryNetComponent::BeginPlay函数）：
```CPP
//创建客户端TCP
{
	//创建网络事件发布者
	TUniquePtr<LuTCP::FNetEventPublisher> NetEventPublisher = MakeUnique<LuTCP::FNetEventPublisher>();

	//服务端的地址
	TSharedRef<FInternetAddr> ServerAddr = ISocketSubsystem::Get()->CreateInternetAddr(FNetworkProtocolTypes::IPv4);
	ServerAddr->SetLoopbackAddress();
	ServerAddr->SetPort(NetSubSystem->TwinPort);

	//创建FTCPClient
	TUniquePtr<LuTCP::FTCPClient> TCPClient = MakeUnique<LuTCP::FTCPClient>(NetEventPublisher.Get(), ServerAddr);
	TCPClient->MessageCallback.BindUObject(this, &UFactoryNetComponent::OnMessage);
	TCPClient->NetConnectionCallback.BindUObject(this, &UFactoryNetComponent::OnNetConnection);

	//由FClientIOThreadRunnable管理FTCPClient对象和FNetEventPublisher对象
	ClientIOThread.Reset(new LuTCP::FClientIOThreadRunnable(MoveTemp(TCPClient), MoveTemp(NetEventPublisher)));
	ClientIOThread->StartIOThread();   //开启网络IO线程
}
```

客户端关闭（见UFactoryNetComponent::EndPlay函数）
```CPP
	if (ClientIOThread.IsValid())
	{
		ClientIOThread->WaitStopThread();
	}
```

发送数据（见UFactoryNetComponent::SendTCPMessage函数）
```CPP

void UFactoryNetComponent::SendTCPMessage(FFactoryRobotData& InData)
{
	if (ServerConnection.IsValid())
	{
		LuTCP::FBuffer Message;

		//序列化InData得到一个字节数组
		TArray<uint8> LoadBuffer;
		FMemoryWriter MemWriter(LoadBuffer);
		MemWriter.SetWantBinaryPropertySerialization(true);
		UScriptStruct* MyStructStruct = FFactoryRobotData::StaticStruct();
		MyStructStruct->SerializeBin(MemWriter, &InData);

		//把字节数组放到Buffer中。
		LuTCP::FBuffer MessageBuffer;
		MessageBuffer.Append(LoadBuffer.GetData(), LoadBuffer.Num());
		//在Buffer的头部预留字节中写入字节数组的大小
		MessageBuffer.AddInt32ToReserve(LoadBuffer.Num());   

		//发送
		ServerConnection.Pin()->Send(MoveTemp(MessageBuffer));
	}
}

```

接受数据
```CPP

void UFactoryNetSubsystem::OnTCPMessage(TSharedPtr<LuTCP::FNetConnection> InNetConn, LuTCP::FBuffer InBuffer)
{
	if (InBuffer.ReadableBytes() > 4)   
	{
		//得到消息最前面4个字节，这个4个字节是一个完成的消息的大小
		const void* MessageSizePtr = InBuffer.Peek();
		int32 MessageSize = *reinterpret_cast<const int32*>(MessageSizePtr);
		if (MessageSize < 0)
		{
			InNetConn->ForceClose();
			return;
		}
		
		if (InBuffer.ReadableBytes() >= MessageSize + 4)
		{
			//已经收到了一个完整的消息，处理。
			InBuffer.AddToReaderIndex(4);

			TArray<uint8> MessageData(InBuffer.Peek(), InBuffer.ReadableBytes());
			InBuffer.AddToReaderIndex(MessageSize);

			FMemoryReader MReader(MessageData);
			MReader.SetWantBinaryPropertySerialization(true);
			UScriptStruct* MyStructStruct = FFactoryRobotData::StaticStruct();
			FFactoryRobotData Data = {};
			MyStructStruct->SerializeBin(MReader, &Data);

			//通知蓝图
			OnRecvMessage(Data);
		}

	}
}
```



