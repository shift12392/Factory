// Fill out your copyright notice in the Description page of Project Settings.


#include "FactoryNetComponent.h"
#include "Sockets.h"

// Sets default values for this component's properties
UFactoryNetComponent::UFactoryNetComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	// ...
}


// Called when the game starts
void UFactoryNetComponent::BeginPlay()
{
	Super::BeginPlay();

	// ...
	UFactoryNetSubsystem* NetSubSystem = GetWorld()->GetSubsystem<UFactoryNetSubsystem>();
	TwinWorldType = NetSubSystem->GetTwinWorldType();

	static int32 RobotPort = NetSubSystem->TwinPort;
	++RobotPort;

	if (NetSubSystem->GetTwinWorldType() == ETwinWorldType::Real)
	{
		UDPSocket = UFactoryNetSubsystem::CreateSocket(TEXT(""), RobotPort, GetOwner()->GetName());
		TwinServerAddr = NetSubSystem->TwinAddr;

		//创建TCP网络IO线程
		{
			TUniquePtr<LuTCP::FNetEventPublisher> NetEventPublisher = MakeUnique<LuTCP::FNetEventPublisher>();

			//服务端的地址
			TSharedRef<FInternetAddr> ServerAddr = ISocketSubsystem::Get()->CreateInternetAddr(FNetworkProtocolTypes::IPv4);
			ServerAddr->SetLoopbackAddress();
			ServerAddr->SetPort(NetSubSystem->TwinPort);

			TUniquePtr<LuTCP::FTCPClient> TCPClient = MakeUnique<LuTCP::FTCPClient>(NetEventPublisher.Get(), ServerAddr);
			TCPClient->MessageCallback.BindUObject(this, &UFactoryNetComponent::OnMessage);
			TCPClient->NetConnectionCallback.BindUObject(this, &UFactoryNetComponent::OnNetConnection);

			ClientIOThread.Reset(new LuTCP::FClientIOThreadRunnable(MoveTemp(TCPClient), MoveTemp(NetEventPublisher)));
			ClientIOThread->StartIOThread();   //开启网络IO线程
		}
	}
}

void UFactoryNetComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ClientIOThread.IsValid())
	{
		ClientIOThread->WaitStopThread();
	}

	Super::EndPlay(EndPlayReason);
}

void UFactoryNetComponent::OnNetConnection(TSharedPtr<LuTCP::FNetConnection> InNetConn)
{
	if (InNetConn->IsConnected())
	{
		ServerConnection = InNetConn;
	}
	else
	{
		ServerConnection.Reset();
	}
}


void UFactoryNetComponent::OnMessage(TSharedPtr<LuTCP::FNetConnection> InNetConn, LuTCP::FBuffer InBuffer)
{

}

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

// Called every frame
void UFactoryNetComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ...
}

