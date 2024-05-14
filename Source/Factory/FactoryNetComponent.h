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
	TSharedPtr<FInternetAddr> TwinServerAddr;    //���������IP��ַ
	FUniqueSocket UDPSocket; 
	uint32 NetPackageId = 0;
	TUniquePtr<LuTCP::FClientIOThreadRunnable> ClientIOThread;   //TCP����IO�߳�
	TWeakPtr<LuTCP::FNetConnection> ServerConnection;     //�������������������
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
	//�������˵����ӺͶϿ�
	void OnNetConnection(TSharedPtr<LuTCP::FNetConnection> InNetConn);
	//�յ��������Ϣ
	void OnMessage(TSharedPtr<LuTCP::FNetConnection> InNetConn, LuTCP::FBuffer InBuffer);
	//���ͻ�������
	UFUNCTION(BlueprintCallable)
	void SendTCPMessage(FFactoryRobotData& InData);
public:	
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

		
};
