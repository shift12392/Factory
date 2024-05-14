// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SocketSubsystem.h"
#include "FactoryNetSubsystem.h"
#include "FactoryNetComponent.generated.h"


UCLASS( ClassGroup=(Custom), Abstract, meta=(BlueprintSpawnableComponent) )
class FACTORY_API UFactoryNetComponent : public UActorComponent
{
	GENERATED_BODY()

protected:
	TSharedPtr<FInternetAddr> TwinServerAddr;    //孪生世界的IP地址
	FUniqueSocket UDPSocket; 
	uint32 NetPackageId = 0;
	TUniquePtr<LuTCP::FClientIOThreadRunnable> ClientIOThread;   //TCP网络IO线程
	TWeakPtr<LuTCP::FNetConnection> ServerConnection;     //与孪生世界的网络连接
public:
	UPROPERTY(BlueprintReadOnly)
	ETwinWorldType TwinWorldType = ETwinWorldType::None;

public:	
	// Sets default values for this component's properties
	UFactoryNetComponent();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//处理服务端的连接和断开
	void OnNetConnection(TSharedPtr<LuTCP::FNetConnection> InNetConn);
	//收到服务端消息
	void OnMessage(TSharedPtr<LuTCP::FNetConnection> InNetConn, LuTCP::FBuffer InBuffer);
	//发送机器数据
	UFUNCTION(BlueprintCallable)
	void SendTCPMessage(FFactoryRobotData& InData);
public:	
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

		
};
