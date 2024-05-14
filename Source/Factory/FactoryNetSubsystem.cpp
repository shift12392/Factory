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
				TUniquePtr<LuTCP::FNetEventPublisher> NetEventPublisher = MakeUnique<LuTCP::FNetEventPublisher>();

				TSharedRef<FInternetAddr> LocalAddr = SocketSubsystem->CreateInternetAddr(FNetworkProtocolTypes::IPv4);
				LocalAddr->SetAnyAddress();
				LocalAddr->SetPort(TwinPort);
				TUniquePtr<LuTCP::FTCPServer> TCPServer = MakeUnique<LuTCP::FTCPServer>(NetEventPublisher.Get(), LocalAddr);
				TCPServer->MessageCallback.BindUObject(this, &UFactoryNetSubsystem::OnTCPMessage);
				TCPServer->NetConnectionCallback.BindUObject(this, &UFactoryNetSubsystem::OnTCPNetConnection);

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

	GetWorldRef().GetTimerManager().ClearTimer(TimerHandle_MyTest);

	if (ServerIOThread.IsValid())
	{
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

					UE_LOG(LogTemp, Log, TEXT("Net: UDP recv package, id = %d"), PackageLoad.PackageId);

					RecvPacketNum++;
				}
				else
				{
					UE_LOG(LogTemp, Log, TEXT("Net: UDP recv half package, package size = %d"), RecvSize);
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
	TArray<uint8> MessageData(InBuffer.Peek(), InBuffer.ReadableBytes());

	FMemoryReader MReader(MessageData);
	MReader.SetWantBinaryPropertySerialization(true);
	UScriptStruct* MyStructStruct = FFactoryRobotData::StaticStruct();

	FFactoryRobotData Data = {};
	MyStructStruct->SerializeBin(MReader, &Data);

	LuTCP::FBuffer SendBuffer;
	SendBuffer.Append(MessageData.GetData(), MessageData.Num());
	InNetConn->Send(MoveTemp(SendBuffer));
}

#if 0



void UFactoryNetSubsystem::Connect(const FString& ipAddress, int32 port, const FTcpSocketDisconnectDelegate& OnDisconnected, const FTcpSocketConnectDelegate& OnConnected,
	const FTcpSocketReceivedMessageDelegate& OnMessageReceived, int32& ConnectionId)
{
	DisconnectedDelegate = OnDisconnected;
	ConnectedDelegate = OnConnected;
	MessageReceivedDelegate = OnMessageReceived;

	ConnectionId = NextConnectionId;
	NextConnectionId++;

	TWeakObjectPtr<UFactoryNetSubsystem> thisWeakObjPtr = TWeakObjectPtr<UFactoryNetSubsystem>(this);
	TSharedRef<FTcpSocketWorker> worker(new FTcpSocketWorker(ipAddress, port, thisWeakObjPtr, ConnectionId, ReceiveBufferSize, SendBufferSize, TimeBetweenTicks));
	TcpWorkers.Add(ConnectionId, worker);
	worker->Start();
}

void UFactoryNetSubsystem::Disconnect(int32 ConnectionId)
{
	auto worker = TcpWorkers.Find(ConnectionId);
	if (worker)
	{
		UE_LOG(LogTemp, Log, TEXT("Tcp Socket: Disconnected from server."));
		worker->Get().Stop();
		TcpWorkers.Remove(ConnectionId);
	}
}

bool UFactoryNetSubsystem::SendData(int32 ConnectionId /*= 0*/, TArray<uint8> DataToSend)
{
	if (TcpWorkers.Contains(ConnectionId))
	{
		if (TcpWorkers[ConnectionId]->isConnected())
		{
			TcpWorkers[ConnectionId]->AddToOutbox(DataToSend);
			return true;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Log: Socket %d isn't connected"), ConnectionId);
		}
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("Log: SocketId %d doesn't exist"), ConnectionId);
	}
	return false;
}

void UFactoryNetSubsystem::ExecuteOnMessageReceived(int32 ConnectionId, TWeakObjectPtr<ATcpSocketConnection> thisObj)
{
	// the second check is for when we quit PIE, we may get a message about a disconnect, but it's too late to act on it, because the thread has already been killed
	if (!thisObj.IsValid())
		return;

	// how to crash:
	// 1 connect with both clients
	// 2 stop PIE
	// 3 close editor
	if (!TcpWorkers.Contains(ConnectionId)) {
		return;
	}

	TArray<uint8> msg = TcpWorkers[ConnectionId]->ReadFromInbox();
	MessageReceivedDelegate.ExecuteIfBound(ConnectionId, msg);
}

TArray<uint8> UFactoryNetSubsystem::Concat_BytesBytes(TArray<uint8> A, TArray<uint8> B)
{
	TArray<uint8> ArrayResult;
	ArrayResult.SetNumUninitialized(A.Num() + B.Num());
	FMemory::Memcpy(ArrayResult.GetData(), A.GetData(), A.Num());
	FMemory::Memcpy(ArrayResult.GetData() + A.Num(), B.GetData(), B.Num());
	return ArrayResult;
}

TArray<uint8> UFactoryNetSubsystem::Conv_ByteToBytes(uint8 InByte)
{
	return TArray{ InByte };
}

TArray<uint8> UFactoryNetSubsystem::Conv_ShortToBytes(int16 InShort)
{
	TArray<uint8> result;
	result.SetNumUninitialized(2);
	FMemory::Memcpy(result.GetData(), &InShort, 2);
	return result;
}

TArray<uint8> UFactoryNetSubsystem::Conv_IntToBytes(int32 InInt)
{
	TArray<uint8> result;
	result.SetNumUninitialized(4);
	FMemory::Memcpy(result.GetData(), &InInt, 4);
	return result;
}

TArray<uint8> UFactoryNetSubsystem::Conv_FloatToBytes(float InFloat)
{
	TArray<uint8> result;
	result.SetNumUninitialized(4);
	FMemory::Memcpy(result.GetData(), &InFloat, 4);
	return result;
}

TArray<uint8> UFactoryNetSubsystem::Conv_LongToBytes(int64 InLong)
{
	TArray<uint8> result;
	result.SetNumUninitialized(8);
	FMemory::Memcpy(result.GetData(), &InLong, 8);
	return result;
}

TArray<uint8> UFactoryNetSubsystem::Conv_DoubleToBytes(double InDouble)
{
	TArray<uint8> result;
	result.SetNumUninitialized(8);
	FMemory::Memcpy(result.GetData(), &InDouble, 8);
	return result;
}

TArray<uint8> UFactoryNetSubsystem::Conv_StringToBytes(const FString& InStr)
{
	FTCHARToUTF8 Convert(*InStr);
	int bytesLength = Convert.Length(); //length of the utf-8 string in bytes (when non-latin letters are used, it's longer than just the number of characters)
	TArray<uint8> result;
	result.SetNumUninitialized(bytesLength);
	FMemory::Memcpy(result.GetData(), TCHAR_TO_UTF8(InStr.GetCharArray().GetData()), bytesLength);
	return result;
}

uint8 UFactoryNetSubsystem::Message_ReadByte(TArray<uint8>& Message)
{
	if (Message.Num() < 1)
	{
		PrintToConsole("Error in the ReadByte node. Not enough bytes in the Message.", true);
		return 255;
	}

	uint8 result = Message[0];
	Message.RemoveAt(0);
	return result;
}

bool UFactoryNetSubsystem::Message_ReadBytes(int32 NumBytes, TArray<uint8>& Message, TArray<uint8>& returnArray)
{
	if (Message.Num() < NumBytes)
	{
		PrintToConsole("Error in the ReadBytes node. Not enough bytes in the Message. Reading aborted.", true);
		return false;
	}

	returnArray.SetNumUninitialized(NumBytes);
	FMemory::Memcpy(returnArray.GetData(), Message.GetData(), NumBytes);
	Message.RemoveAt(0, NumBytes);
	return true;
}

int16 UFactoryNetSubsystem::Message_ReadShort(TArray<uint8>& Message)
{
	if (Message.Num() < 2)
	{
		PrintToConsole("Error in the ReadShort function. Not enough bytes in the Message.", true);
		return -1;
	}

	int16 result;
	FMemory::Memcpy(&result, Message.GetData(), 2);
	Message.RemoveAt(0, 2);

	return result;
}

int32 UFactoryNetSubsystem::Message_ReadInt(TArray<uint8>& Message)
{
	if (Message.Num() < 4)
	{
		PrintToConsole("Error in the ReadInt node. Not enough bytes in the Message.", true);
		return -1;
	}

	int32 result;
	FMemory::Memcpy(&result, Message.GetData(), 4);
	Message.RemoveAt(0, 4);

	return result;
}

float UFactoryNetSubsystem::Message_ReadFloat(TArray<uint8>& Message)
{
	if (Message.Num() < 4)
	{
		PrintToConsole("Error in the ReadFloat node. Not enough bytes in the Message.", true);
		return -1.f;
	}

	float result;
	FMemory::Memcpy(&result, Message.GetData(), 4);
	Message.RemoveAt(0, 4);

	return result;
}

int64 UFactoryNetSubsystem::Message_ReadLong(TArray<uint8>& Message)
{
	if (Message.Num() < 8)
	{
		PrintToConsole("Error in the ReadLong node. Not enough bytes in the Message.", true);
		return -1;
	}

	int64 result = 0;
	FMemory::Memcpy(&result, Message.GetData(), 8);
	Message.RemoveAt(0, 8);

	return result;
}

double UFactoryNetSubsystem::Message_ReadDouble(TArray<uint8>& Message)
{
	if (Message.Num() < 8)
	{
		PrintToConsole("Error in the ReadDouble node. Not enough bytes in the Message.", true);
		return -1.0;
	}

	double result;
	FMemory::Memcpy(&result, Message.GetData(), 8);
	Message.RemoveAt(0, 8);

	return result;
}

FString UFactoryNetSubsystem::Message_ReadString(TArray<uint8>& Message, int32 BytesLength)
{
	if (BytesLength <= 0)
	{
		if (BytesLength < 0)
			PrintToConsole("Error in the ReadString node. BytesLength isn't a positive number.", true);
		return FString("");
	}
	if (Message.Num() < BytesLength)
	{
		PrintToConsole("Error in the ReadString node. Message isn't as long as BytesLength.", true);
		return FString("");
	}

	TArray<uint8> StringAsBytes;
	Message_ReadBytes(BytesLength, Message, StringAsBytes);
	std::string cstr(reinterpret_cast<const char*>(StringAsBytes.GetData()), StringAsBytes.Num());
	return FString(UTF8_TO_TCHAR(cstr.c_str()));
}

bool UFactoryNetSubsystem::isConnected(int32 ConnectionId)
{
	if (TcpWorkers.Contains(ConnectionId))
		return TcpWorkers[ConnectionId]->isConnected();
	return false;
}

void UFactoryNetSubsystem::PrintToConsole(FString Str, bool Error)
{
	if (auto tcpSocketSettings = GetDefault<UTcpSocketSettings>())
	{
		if (Error && tcpSocketSettings->bPostErrorsToMessageLog)
		{
			auto messageLog = FMessageLog("Tcp Socket Plugin");
			messageLog.Open(EMessageSeverity::Error, true);
			messageLog.Message(EMessageSeverity::Error, FText::AsCultureInvariant(Str));
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("Log: %s"), *Str);
		}
	}
}

void UFactoryNetSubsystem::ExecuteOnConnected(int32 WorkerId, TWeakObjectPtr<ATcpSocketConnection> thisObj)
{
	if (!thisObj.IsValid())
		return;

	ConnectedDelegate.ExecuteIfBound(WorkerId);
}

void UFactoryNetSubsystem::ExecuteOnDisconnected(int32 WorkerId, TWeakObjectPtr<ATcpSocketConnection> thisObj)
{
	if (!thisObj.IsValid())
		return;

	if (TcpWorkers.Contains(WorkerId))
	{
		TcpWorkers.Remove(WorkerId);
	}
	DisconnectedDelegate.ExecuteIfBound(WorkerId);
}

bool FTcpSocketWorker::isConnected()
{
	///FScopeLock ScopeLock(&SendCriticalSection);
	return bConnected;
}

FTcpSocketWorker::FTcpSocketWorker(FString inIp, const int32 inPort, TWeakObjectPtr<ATcpSocketConnection> InOwner, int32 inId, int32 inRecvBufferSize, int32 inSendBufferSize, float inTimeBetweenTicks)
	: ipAddress(inIp)
	, port(inPort)
	, ThreadSpawnerActor(InOwner)
	, id(inId)
	, RecvBufferSize(inRecvBufferSize)
	, SendBufferSize(inSendBufferSize)
	, TimeBetweenTicks(inTimeBetweenTicks)
{

}

FTcpSocketWorker::~FTcpSocketWorker()
{
	AsyncTask(ENamedThreads::GameThread, []() {	ATcpSocketConnection::PrintToConsole("Tcp socket thread was destroyed.", false); });
	Stop();
	if (Thread)
	{
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}
}

void FTcpSocketWorker::Start()
{
	check(!Thread && "Thread wasn't null at the start!");
	check(FPlatformProcess::SupportsMultithreading() && "This platform doesn't support multithreading!");
	if (Thread)
	{
		UE_LOG(LogTemp, Log, TEXT("Log: Thread isn't null. It's: %s"), *Thread->GetThreadName());
	}
	Thread = FRunnableThread::Create(this, *FString::Printf(TEXT("FTcpSocketWorker %s:%d"), *ipAddress, port), 128 * 1024, TPri_Normal);
	UE_LOG(LogTemp, Log, TEXT("Log: Created thread"));
}

void FTcpSocketWorker::AddToOutbox(TArray<uint8> Message)
{
	Outbox.Enqueue(Message);
}

TArray<uint8> FTcpSocketWorker::ReadFromInbox()
{
	TArray<uint8> msg;
	Inbox.Dequeue(msg);
	return msg;
}

bool FTcpSocketWorker::Init()
{
	bRun = true;
	bConnected = false;
	return true;
}

uint32 FTcpSocketWorker::Run()
{
	AsyncTask(ENamedThreads::GameThread, []() {	ATcpSocketConnection::PrintToConsole("Starting Tcp socket thread.", false); });

	while (bRun)
	{
		FDateTime timeBeginningOfTick = FDateTime::UtcNow();

		// Connect
		if (!bConnected)
		{
			Socket = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateSocket(NAME_Stream, TEXT("default"), false);
			if (!Socket)
			{
				return 0;
			}

			Socket->SetReceiveBufferSize(RecvBufferSize, ActualRecvBufferSize);
			Socket->SetSendBufferSize(SendBufferSize, ActualSendBufferSize);

			FIPv4Address ip;
			FIPv4Address::Parse(ipAddress, ip);

			TSharedRef<FInternetAddr> internetAddr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
			internetAddr->SetIp(ip.Value);
			internetAddr->SetPort(port);

			bConnected = Socket->Connect(*internetAddr);
			if (bConnected)
			{
				AsyncTask(ENamedThreads::GameThread, [this]() {
					ThreadSpawnerActor.Get()->ExecuteOnConnected(id, ThreadSpawnerActor);
					});
			}
			else
			{
				AsyncTask(ENamedThreads::GameThread, []() { ATcpSocketConnection::PrintToConsole(FString::Printf(TEXT("Couldn't connect to server. TcpSocketConnection.cpp: line %d"), __LINE__), true); });
				bRun = false;
			}
			continue;
		}

		if (!Socket)
		{
			AsyncTask(ENamedThreads::GameThread, []() { ATcpSocketConnection::PrintToConsole(FString::Printf(TEXT("Socket is null. TcpSocketConnection.cpp: line %d"), __LINE__), true); });
			bRun = false;
			continue;
		}

		// check if we weren't disconnected from the socket
		Socket->SetNonBlocking(true); // set to NonBlocking, because Blocking can't check for a disconnect for some reason
		int32 t_BytesRead;
		uint8 t_Dummy;
		if (!Socket->Recv(&t_Dummy, 1, t_BytesRead, ESocketReceiveFlags::Peek))
		{
			bRun = false;
			continue;
		}
		Socket->SetNonBlocking(false);	// set back to Blocking

		// if Outbox has something to send, send it
		while (!Outbox.IsEmpty())
		{
			TArray<uint8> toSend;
			Outbox.Dequeue(toSend);

			if (!BlockingSend(toSend.GetData(), toSend.Num()))
			{
				// if sending failed, stop running the thread
				bRun = false;
				UE_LOG(LogTemp, Log, TEXT("TCP send data failed !"));
				continue;
			}
		}

		// if we can read something		
		uint32 PendingDataSize = 0;
		TArray<uint8> receivedData;

		int32 BytesReadTotal = 0;
		// keep going until we have no data.
		while (bRun)
		{
			if (!Socket->HasPendingData(PendingDataSize))
			{
				// no messages
				break;
			}

			AsyncTask(ENamedThreads::GameThread, []() { ATcpSocketConnection::PrintToConsole("Pending data", false); });

			receivedData.SetNumUninitialized(BytesReadTotal + PendingDataSize);

			int32 BytesRead = 0;
			if (!Socket->Recv(receivedData.GetData() + BytesReadTotal, PendingDataSize, BytesRead))
			{
				// ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
				// error code: (int32)SocketSubsystem->GetLastErrorCode()
				AsyncTask(ENamedThreads::GameThread, []() {
					ATcpSocketConnection::PrintToConsole(FString::Printf(TEXT("In progress read failed. TcpSocketConnection.cpp: line %d"), __LINE__), true);
					});
				break;
			}
			BytesReadTotal += BytesRead;

			/* TODO: if we have more PendingData than we could read, continue the while loop so that we can send messages if we have any, and then keep recving*/
		}

		// if we received data, inform the main thread about it, so it can read TQueue
		if (bRun && receivedData.Num() != 0)
		{
			Inbox.Enqueue(receivedData);
			AsyncTask(ENamedThreads::GameThread, [this]() {
				ThreadSpawnerActor.Get()->ExecuteOnMessageReceived(id, ThreadSpawnerActor);
				});
		}

		/* In order to sleep, we will account for how much this tick took due to sending and receiving */
		FDateTime timeEndOfTick = FDateTime::UtcNow();
		FTimespan tickDuration = timeEndOfTick - timeBeginningOfTick;
		float secondsThisTickTook = tickDuration.GetTotalSeconds();
		float timeToSleep = TimeBetweenTicks - secondsThisTickTook;
		if (timeToSleep > 0.f)
		{
			//AsyncTask(ENamedThreads::GameThread, [timeToSleep]() { ATcpSocketConnection::PrintToConsole(FString::Printf(TEXT("Sleeping: %f seconds"), timeToSleep), false); });
			FPlatformProcess::Sleep(timeToSleep);
		}
	}

	bConnected = false;

	AsyncTask(ENamedThreads::GameThread, [this]() {
		ThreadSpawnerActor.Get()->ExecuteOnDisconnected(id, ThreadSpawnerActor);
		});

	SocketShutdown();
	if (Socket)
	{
		delete Socket;
		Socket = nullptr;
	}

	return 0;
}

void FTcpSocketWorker::Stop()
{
	bRun = false;
}

void FTcpSocketWorker::Exit()
{

}

bool FTcpSocketWorker::BlockingSend(const uint8* Data, int32 BytesToSend)
{
	if (BytesToSend > 0)
	{
		int32 BytesSent = 0;
		if (!Socket->Send(Data, BytesToSend, BytesSent))
		{
			return false;
		}
	}
	return true;
}

void FTcpSocketWorker::SocketShutdown()
{
	// if there is still a socket, close it so our peer will get a quick disconnect notification
	if (Socket)
	{
		Socket->Close();
	}
}

#endif