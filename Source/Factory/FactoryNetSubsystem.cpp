// Fill out your copyright notice in the Description page of Project Settings.


#include "FactoryNetSubsystem.h"
#include "Sockets.h"
#include "Networking.h"

/*
bool FFactoryNetPoseData::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	Ar << BoueName;
	Pos.NetSerialize(Ar, Map, bOutSuccess);
	Dir.NetSerialize(Ar, Map, bOutSuccess);
}
*/
void UFactoryNetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FString WorldName;
	GetWorldRef().GetName(WorldName);

	TwinWorldType = ETwinWorldType::None;
	if (WorldName == TEXT("RealWorld"))
		TwinWorldType = ETwinWorldType::Real;
	else if (WorldName == TEXT("TwinWorld"))
		TwinWorldType = ETwinWorldType::Twin;

	//bTwinScene = FParse::Param(FCommandLine::Get(), TEXT("TwinScene"));

	if (TwinWorldType == ETwinWorldType::None)
		return;

	//创建Socket
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get();
	if (SocketSubsystem != nullptr)
	{
		if (TwinWorldType == ETwinWorldType::Twin)
		{
			UDPSocket = CreateSocket(TEXT(""), TwinPort, TEXT("TwinSocket"));

			//创建TCPServer
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

			//GetWorldRef().GetTimerManager().SetTimer(TimerHandle_MyTest, this, &UFactoryNetSubsystem::MyTest, 10.0f, false, 10.0f);
		}
		
		/*
		if (UDPSocket.IsValid())
		{
			FAddressInfoResult TwinAddrResult = SocketSubsystem->GetAddressInfo(TEXT("127.0.0.1"), *FString::Printf(TEXT("%d"), TwinPort),
				EAddressInfoFlags::AllResultsWithMapping | EAddressInfoFlags::OnlyUsableAddresses, NAME_None, ESocketType::SOCKTYPE_Datagram);

			if (TwinAddrResult.ReturnCode == SE_NO_ERROR)
			{
				for (auto& Result : TwinAddrResult.Results)
				{
					TwinAddr = Result.Address;
					break;
				}
			}
		}
		*/

		else if (TwinWorldType == ETwinWorldType::Real)
		{
			TwinAddr = SocketSubsystem->CreateInternetAddr(FNetworkProtocolTypes::IPv4);
			TwinAddr->SetLoopbackAddress();
			TwinAddr->SetPort(TwinPort);
		}
		
	}

	/*
	FBitWriter BitW;
	BitW.SetAllowResize(true);
	FString TemStr(TEXT("中国"));
	BitW << TemStr;

	FBitReader BitR(BitW.GetData(), BitW.GetMaxBits());
	TemStr.Empty();
	BitR << TemStr;

	TArray<uint8> StructBuffer;
	FMemoryWriter NewBuffer(StructBuffer);
	//NewBuffer.SetAllowResize(true);
	NewBuffer.SetWantBinaryPropertySerialization(true);
	FFactoryNetPoseData TempData;
	TempData.BoueName = TEXT("sssssssss");
	TempData.Pos = FVector(100, 0, 0);
	TempData.Dir = FVector::One();
	UScriptStruct* MyStructStruct = FFactoryNetPoseData::StaticStruct();
	bool bUseBinary = MyStructStruct->UseBinarySerialization(NewBuffer);
	MyStructStruct->SerializeBin(NewBuffer, &TempData);


	FMemoryReader NewReader(StructBuffer);
	//FBitReader NewReader(NewBuffer.GetData(), NewBuffer.GetNumBits());
	NewReader.SetWantBinaryPropertySerialization(true);
	FFactoryNetPoseData TempData02;
	MyStructStruct->SerializeBin(NewReader, &TempData02);
	*/
}

void UFactoryNetSubsystem::Deinitialize()
{
	if (UDPSocket.IsValid())
	{
		UDPSocket->Close();
		UDPSocket.Reset();
	}

	//GetWorldRef().GetTimerManager().ClearTimer(TimerHandle_MyTest);

	if (ServerIOThread.IsValid())
	{
		//等待网络IO线程关闭
		//同时关闭TCPServer。
		ServerIOThread->WaitStopThread();    
	}

	TwinAddr.Reset();

	Super::Deinitialize();
}

void UFactoryNetSubsystem::Tick(float DeltaTime)
{

}

TStatId UFactoryNetSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFactoryNetSubsystem, STATGROUP_Tickables);
}

void UFactoryNetSubsystem::NetTick(float DeltaTime)
{
	if (UDPSocket.IsValid())
	{
		uint8 BufferData[8129] = {};
		int32 RecvSize = 0;
		TSharedRef<FInternetAddr> Sender = ISocketSubsystem::Get()->CreateInternetAddr();
		bool bResult = false;
		int32 RecvPacketNum = 0;
		do
		{
			bResult = UDPSocket->RecvFrom(BufferData, 8129, RecvSize, *Sender);
			if (RecvSize > sizeof(uint32))
			{
				FFactoryUDPPackageLoad PackageLoad;
				PackageLoad.LoadDatas.Append(BufferData, RecvSize);
				uint32 LoadBits = 0;
				FMemoryReader MemReader(PackageLoad.LoadDatas);
				MemReader << LoadBits;
				if ((RecvSize - sizeof(uint32)) == LoadBits)
				{
					FName RobotName;
					MemReader << RobotName;
					MemReader << PackageLoad.PackageId;
					PackageLoad.Offset = MemReader.Tell();
					TArray<FFactoryUDPPackageLoad>* FindReader = AllUDPPackages.Find(RobotName);
					if (FindReader == nullptr)
					{
						TArray<FFactoryUDPPackageLoad> NewReaderArr;
						NewReaderArr.Add(MoveTemp(PackageLoad));
						AllUDPPackages.Add(RobotName, (MoveTemp(NewReaderArr)));
					}
					else
					{
						AllUDPPackages[RobotName].Add(MoveTemp(PackageLoad));
					}

					//UE_LOG(LogTemp, Log, TEXT("Net: UDP recv package, id = %d"), PackageLoad.PackageId);

					RecvPacketNum++;
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("Net: UDP recv half package, package size = %d"), RecvSize);
				}
			}
		} while (bResult && RecvSize > 0);

		//UE_LOG(LogTemp, Log, TEXT("Net: UDP RecvFrom Finish, Recv %d packages"), RecvPacketNum);
	}
}

FUniqueSocket UFactoryNetSubsystem::CreateSocket(const FString& InIP, int32 InPort, const FString& InSocketDescription, bool bIsUDP)
{
	FUniqueSocket NewSocket;
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get();
	if (SocketSubsystem != nullptr)
	{
		TSharedRef<FInternetAddr> LocalAddr = SocketSubsystem->CreateInternetAddr(FNetworkProtocolTypes::IPv4);
		NewSocket = SocketSubsystem->CreateUniqueSocket(bIsUDP ? NAME_DGram : NAME_Stream, InSocketDescription, LocalAddr->GetProtocolType());
		if (NewSocket.IsValid())
		{
			NewSocket->SetNonBlocking(true);
			if(InIP.IsEmpty())
			    LocalAddr->SetLoopbackAddress();
			else
			{
				FIPv4Address IP;
				FIPv4Address::Parse(InIP, IP);
				LocalAddr->SetIp(IP.Value);
			}
			LocalAddr->SetPort(InPort);
			int32 UsedPort = SocketSubsystem->BindNextPort(NewSocket.Get(), *LocalAddr, 1000, 1);
			if (UsedPort != InPort)
			{
				UE_LOG(LogTemp, Log, TEXT("网络：绑定 %d 端口失败"), UsedPort);	
				NewSocket->Close();
				NewSocket.Reset();
			}
		}
	}

	return NewSocket;
}

void UFactoryNetSubsystem::WriteUDPPackageHeader(FMemoryWriter& InBuffer, FFactoryUDPPackageHeader& InHeader)
{
	InBuffer << InHeader.RobotName;   //FBitWriter不能序列化FName
	InBuffer << InHeader.PackageId;
}

void UFactoryNetSubsystem::MyTest()
{

}

bool UFactoryNetSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}


void UFactoryNetSubsystem::OnTCPNetConnection(TSharedPtr<LuTCP::FNetConnection> InNetConn)
{
	if (InNetConn->IsConnected())
	{
		ClientConnections.Add(InNetConn->GetName(), InNetConn);
	}
	else
	{
		ClientConnections.Remove(InNetConn->GetName());
	}
}

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