#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"

#include "MuduoNetComponent.generated.h"

class UMuduoNetSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMuduoNetComponentEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMuduoNetComponentMessage, const TArray<uint8>&, Message);

/**
 * 挂在 Actor 上的 TCP 客户端组件：自动连接、自动断线、收消息。
 *
 * 内部通过 UMuduoNetSubsystem 复用同一条 IO 线程，
 * 蓝图里只需要填服务器地址/端口并绑定 OnMessage 即可。
 */
UCLASS(ClassGroup = (MuduoNet), meta = (BlueprintSpawnableComponent))
class MUDUOUE_API UMuduoNetComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMuduoNetComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MuduoNet")
	FString ServerHost = TEXT("127.0.0.1");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MuduoNet", meta = (ClampMin = "1", ClampMax = "65535"))
	int32 ServerPort = 7779;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MuduoNet")
	bool bAutoConnect = true;

	UPROPERTY(BlueprintAssignable, Category = "MuduoNet")
	FMuduoNetComponentEvent OnConnected;

	UPROPERTY(BlueprintAssignable, Category = "MuduoNet")
	FMuduoNetComponentEvent OnDisconnected;

	UPROPERTY(BlueprintAssignable, Category = "MuduoNet")
	FMuduoNetComponentMessage OnMessage;

	UFUNCTION(BlueprintCallable, Category = "MuduoNet")
	void Connect();

	UFUNCTION(BlueprintCallable, Category = "MuduoNet")
	void Disconnect();

	UFUNCTION(BlueprintCallable, Category = "MuduoNet")
	bool Send(const TArray<uint8>& Data);

	UFUNCTION(BlueprintPure, Category = "MuduoNet")
	bool IsConnected() const;

	UFUNCTION(BlueprintPure, Category = "MuduoNet")
	int64 GetClientId() const { return ClientId; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void HandleConnectionEvent(int64 EndpointId, bool bConnected);

	UFUNCTION()
	void HandleMessageEvent(int64 EndpointId, const TArray<uint8>& Message);

	UMuduoNetSubsystem* GetNetSubsystem() const;

	int64 ClientId = 0;
};
