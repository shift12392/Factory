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
	Twin,    //孪生世界
	Real     //真实世界
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
	ETwinWorldType TwinWorldType = ETwinWorldType::None;   //世界类型
	TUniquePtr<LuTCP::FServerIOThreadRunnable>      ServerIOThread;       //网络IO线程
	TMap<FName, TSharedPtr<LuTCP::FNetConnection> > ClientConnections;    //客户端连接
	//FTimerHandle TimerHandle_MyTest;

public:
	TSharedPtr<FInternetAddr> TwinAddr;   //孪生世界IP地址
	const int32 TwinPort = 7779;          //孪生世界服务器端口
	TMap<FName, TArray<FFactoryUDPPackageLoad>> AllUDPPackages;   //收到的客户端的UDP数据包
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

	//收到机器数据
	UFUNCTION(BlueprintImplementableEvent)
	void OnRecvMessage(const FFactoryRobotData& InData);

private:

	//处理新的客户端连接和客户端端口连接
	void OnTCPNetConnection(TSharedPtr<LuTCP::FNetConnection> InNetConn);
	//收到客户端消息
	void OnTCPMessage(TSharedPtr<LuTCP::FNetConnection> InNetConn, LuTCP::FBuffer InBuffer);
};