// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SocketSubsystem.h"
#include "Engine/NetSerialization.h"
#include "TCPServerClient.h"
#include "FactoryNetSubsystem.generated.h"

USTRUCT()
struct FFactoryNetBonePoseData
{

	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	FName BoneName;

	UPROPERTY()
	FVector Pos;

	UPROPERTY()
	FQuat Dir;

	//bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

/*
template<>
struct TStructOpsTypeTraits<FFactoryNetPoseData> : public TStructOpsTypeTraitsBase2<FFactoryNetPoseData>
{
	enum
	{
		WithNetSerializer = true,
	};
};
*/



USTRUCT()
struct FFactoryNetTwinActorPoseData
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	FName RobotName;

	UPROPERTY()
	TArray<FFactoryNetBonePoseData> PoseDatas;
};

USTRUCT()
struct FFactoryNetSinglePosePackage
{
	GENERATED_USTRUCT_BODY()

	FName RobotName;

	UPROPERTY()
	FVector Pos;

	UPROPERTY()
	FRotator Dir;
};

UENUM(BlueprintType)
enum class ETwinWorldType : uint8 
{
	None,
	Twin,
	Real
};

struct FFactoryUDPPackageHeader
{
	//uint32 LoadBits = 0;
	FName RobotName;
	uint32 PackageId = 0;
};

struct FFactoryUDPPackageLoad
{
	uint32 PackageId = 0;
	uint32 Offset = 0;
	TArray<uint8> LoadDatas;
};

//机器手臂的在线状态
UENUM(BlueprintType)
enum class ERobotState : uint8
{
	ERobotState_Stop,     //机器停止
	ERobotState_Wait,     //机器正在等待
	ERobotState_Working   //机器正在工作
};

USTRUCT(BlueprintType)
struct FFactoryRobotData
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	FName RobotName;

	UPROPERTY()
	ERobotState RobotState;

	UPROPERTY()
	int32 ProductNum = 0;     //机器已经处理的产品数量
};

DECLARE_DYNAMIC_DELEGATE_OneParam(FTcpSocketDisconnectDelegate, int32, ConnectionId);
DECLARE_DYNAMIC_DELEGATE_OneParam(FTcpSocketConnectDelegate, int32, ConnectionId);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FTcpSocketReceivedMessageDelegate, int32, ConnectionId, UPARAM(ref) TArray<uint8>&, Message);


/**
 *
 */
UCLASS()
class FACTORY_API UFactoryNetSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

private:
	FUniqueSocket UDPSocket;
	ETwinWorldType TwinWorldType = ETwinWorldType::None;
	TUniquePtr<LuTCP::FServerIOThreadRunnable> ServerIOThread;
	TMap<FName, TSharedPtr<LuTCP::FNetConnection> > ClientConnections;
	FTimerHandle TimerHandle_MyTest;

	TUniquePtr<LuTCP::FClientIOThreadRunnable> ClientIOThread;
	TSharedPtr<LuTCP::FNetConnection> ServerConnection;

public:
	TSharedPtr<FInternetAddr> TwinAddr;
	const int32 TwinPort = 7779;
	TMap<FName, TArray<FFactoryUDPPackageLoad>> AllUDPPackages;
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	void NetTick(float DeltaTime);

	static FUniqueSocket CreateSocket(const FString& InIP, int32 InPort, const FString& InSocketDescription,bool bIsUDP = true);
	static void WriteUDPPackageHeader(FMemoryWriter& InBuffer, FFactoryUDPPackageHeader& InHeader);

	UFUNCTION(BlueprintCallable)
	ETwinWorldType GetTwinWorldType() const { return TwinWorldType; }

	UFUNCTION(BlueprintCallable)
	void MyTest();

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const;

	//处理新的客户端连接和客户端端口连接
	void OnTCPNetConnection(TSharedPtr<LuTCP::FNetConnection> InNetConn);
	//收到客户端消息
	void OnTCPMessage(TSharedPtr<LuTCP::FNetConnection> InNetConn, LuTCP::FBuffer InBuffer);

#if 0
public:


	/* Returns the ID of the new connection. */
	UFUNCTION(BlueprintCallable, Category = "Socket")
	void Connect(const FString& ipAddress, int32 port,
		const FTcpSocketDisconnectDelegate& OnDisconnected, const FTcpSocketConnectDelegate& OnConnected,
		const FTcpSocketReceivedMessageDelegate& OnMessageReceived, int32& ConnectionId);

	/* Disconnect from connection ID. */
	UFUNCTION(BlueprintCallable, Category = "Socket")
	void Disconnect(int32 ConnectionId);

	/* False means we're not connected to socket and the data wasn't sent. "True" doesn't guarantee that it was successfully sent,
	only that we were still connected when we initiating the sending process. */
	UFUNCTION(BlueprintCallable, Category = "Socket") // use meta to set first default param to 0
		bool SendData(int32 ConnectionId, TArray<uint8> DataToSend);

	/*
	When hitting Stop in PIE while a connection is being established (it's a blocking operation that takes a while to timeout),
	our ATcpSocketConnection actor will be destroyed, an then the thread will send a message through AsyncTask to call ExecuteOnConnected,
	ExecuteOnDisconnected, or ExecuteOnMessageReceived.

	When we enter their code, "this" will point to random memory.
	So to avoid that problem, we also send back a weak pointer as well. If the pointer is valid, we're ok.
	This is why the three methods below have a TWeakObjectPtr.
	*/

	//UFUNCTION(Category = "Socket")	
	void ExecuteOnConnected(int32 WorkerId, TWeakObjectPtr<ATcpSocketConnection> thisObj);

	//UFUNCTION(Category = "Socket")
	void ExecuteOnDisconnected(int32 WorkerId, TWeakObjectPtr<ATcpSocketConnection> thisObj);

	//UFUNCTION(Category = "Socket")
	void ExecuteOnMessageReceived(int32 ConnectionId, TWeakObjectPtr<ATcpSocketConnection> thisObj);

	UFUNCTION(BlueprintPure, meta = (DisplayName = "Append Bytes", CommutativeAssociativeBinaryOperator = "true"), Category = "Socket")
	static TArray<uint8> Concat_BytesBytes(TArray<uint8> A, TArray<uint8> B);

	/** Converts a byte to an array of bytes */
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Byte To Bytes", CompactNodeTitle = "->", Keywords = "cast convert", BlueprintAutocast), Category = "Socket")
	static TArray<uint8> Conv_ByteToBytes(uint8 InByte);

	/** Converts an int16 to an array of bytes */
	UFUNCTION()
	static TArray<uint8> Conv_ShortToBytes(int16 InShort);

	/** Converts an integer to an array of bytes */
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Int To Bytes", CompactNodeTitle = "->", Keywords = "cast convert", BlueprintAutocast), Category = "Socket")
	static TArray<uint8> Conv_IntToBytes(int32 InInt);

	/** Converts a float (single-precision) to an array of bytes */
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Float (single-precision) To Bytes", CompactNodeTitle = "4->", Keywords = "cast convert", BlueprintAutocast), Category = "Socket")
	static TArray<uint8> Conv_FloatToBytes(float InFloat);

	/** Converts an int64 to an array of bytes */
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Int64 To Bytes", CompactNodeTitle = "->", Keywords = "cast convert", BlueprintAutocast), Category = "Socket")
	static TArray<uint8> Conv_LongToBytes(int64 InLong);

	/** Converts a float (double-precision) to an array of bytes */
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Float (double-precision) To Bytes", CompactNodeTitle = "8->", Keywords = "cast convert", BlueprintAutocast), Category = "Socket")
	static TArray<uint8> Conv_DoubleToBytes(double InDouble);

	/** Converts a string to an array of bytes */
	UFUNCTION(BlueprintPure, meta = (DisplayName = "String To Bytes", CompactNodeTitle = "->", Keywords = "cast convert", BlueprintAutocast), Category = "Socket")
	static TArray<uint8> Conv_StringToBytes(const FString& InStr);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Read Bytes", Keywords = "read bytes"), Category = "Socket")
	static bool Message_ReadBytes(int32 NumBytes, UPARAM(ref) TArray<uint8>& Message, TArray<uint8>& ReturnArray);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Read Byte", Keywords = "read byte int8 uint8"), Category = "Socket")
	static uint8 Message_ReadByte(UPARAM(ref) TArray<uint8>& Message);

	UFUNCTION()
	static int16 Message_ReadShort(TArray<uint8>& Message);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Read Int", Keywords = "read int"), Category = "Socket")
	static int32 Message_ReadInt(UPARAM(ref) TArray<uint8>& Message);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Read Int64", Keywords = "read long int64"), Category = "Socket")
	static int64 Message_ReadLong(UPARAM(ref) TArray<uint8>& Message);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Read Float (single-precision)", Keywords = "read float"), Category = "Socket")
	static float Message_ReadFloat(UPARAM(ref) TArray<uint8>& Message);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Read Float (double-precision)", Keywords = "read double"), Category = "Socket")
	static double Message_ReadDouble(UPARAM(ref) TArray<uint8>& Message);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Read String", Keywords = "read string"), Category = "Socket")
	static FString Message_ReadString(UPARAM(ref) TArray<uint8>& Message, int32 StringLength);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Socket")
	bool isConnected(int32 ConnectionId);

	/* Used by the separate threads to print to console on the main thread. */
	static void PrintToConsole(FString Str, bool Error);

	/* Buffer size in bytes. Currently not used. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Socket")
	int32 SendBufferSize = 16384;

	/* Buffer size in bytes. It's set only when creating a socket, never afterwards. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Socket")
	int32 ReceiveBufferSize = 16384;

	/* Time between ticks. Please account for the fact that it takes 1ms to wake up on a modern PC, so 0.01f would effectively be 0.011f */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Socket")
	float TimeBetweenTicks = 0.008f;

private:
	TMap<int32, TSharedRef<class FTcpSocketWorker>> TcpWorkers;

	FTcpSocketDisconnectDelegate DisconnectedDelegate;
	FTcpSocketConnectDelegate ConnectedDelegate;
	FTcpSocketReceivedMessageDelegate MessageReceivedDelegate;

	int32 NextConnectionId = 0;
#endif
};

#if 0

class FTcpSocketWorker : public FRunnable, public TSharedFromThis<FTcpSocketWorker>
{

	FRunnableThread* Thread = nullptr;

private:
	class FSocket* Socket = nullptr;
	FString ipAddress;
	int port;
	TWeakObjectPtr<ATcpSocketConnection> ThreadSpawnerActor;
	int32 id;
	int32 RecvBufferSize;
	int32 ActualRecvBufferSize;
	int32 SendBufferSize;
	int32 ActualSendBufferSize;
	float TimeBetweenTicks;
	FThreadSafeBool bConnected = false;

	// SPSC = single producer, single consumer.
	TQueue<TArray<uint8>, EQueueMode::Spsc> Inbox; // Messages we read from socket and send to main thread. Runner thread is producer, main thread is consumer.
	TQueue<TArray<uint8>, EQueueMode::Spsc> Outbox; // Messages to send to socket from main thread. Main thread is producer, runner thread is consumer.

public:

	//Constructor / Destructor
	FTcpSocketWorker(FString inIp, const int32 inPort, TWeakObjectPtr<ATcpSocketConnection> InOwner, int32 inId, int32 inRecvBufferSize, int32 inSendBufferSize, float inTimeBetweenTicks);
	virtual ~FTcpSocketWorker();

	/*  Starts processing of the connection. Needs to be called immediately after construction	 */
	void Start();

	/* Adds a message to the outgoing message queue */
	void AddToOutbox(TArray<uint8> Message);

	/* Reads a message from the inbox queue */
	TArray<uint8> ReadFromInbox();

	// Begin FRunnable interface.
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;
	// End FRunnable interface	

	/** Shuts down the thread */
	void SocketShutdown();

	/* Getter for bConnected */
	bool isConnected();

private:
	/* Blocking send */
	bool BlockingSend(const uint8* Data, int32 BytesToSend);

	/** thread should continue running */
	FThreadSafeBool bRun = false;

	/** Critical section preventing multiple threads from sending simultaneously */
	//FCriticalSection SendCriticalSection;
};

#endif