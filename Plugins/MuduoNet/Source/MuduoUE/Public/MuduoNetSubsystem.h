#pragma once

#include "CoreMinimal.h"
#include "MuduoNetBridge.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Templates/UniquePtr.h"

#include "MuduoNetSubsystem.generated.h"

/** 端点（服务端连接或客户端）的连接状态变化。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMuduoNetConnectionEvent, int64, EndpointId, bool, bConnected);
/** 收到一条完整消息（默认已按 4 字节长度头拆好包）。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMuduoNetMessageEvent, int64, EndpointId, const TArray<uint8>&, Message);

/**
 * MuduoNet 的 UE 门面：持有 IO 线程、服务端与所有客户端，并把回调派发到游戏线程。
 *
 * 端点 id 规则（int64）：
 * - 服务端连接：1, 2, 3 ...
 * - 客户端：1,000,000,000 + n
 * 这样可以只用一个 id 空间在蓝图里区分两侧。
 */
UCLASS()
class MUDUOUE_API UMuduoNetSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ---- 服务端 ----
	UFUNCTION(BlueprintCallable, Category = "MuduoNet|Server")
	bool StartServer(int32 Port, int32 ServerThreadNum = 0);

	UFUNCTION(BlueprintCallable, Category = "MuduoNet|Server")
	void StopServer();

	UFUNCTION(BlueprintPure, Category = "MuduoNet|Server")
	bool IsServerRunning() const;

	UFUNCTION(BlueprintPure, Category = "MuduoNet|Server")
	int32 GetServerPort() const;

	UFUNCTION(BlueprintPure, Category = "MuduoNet|Server")
	int32 GetServerConnectionCount() const;

	UFUNCTION(BlueprintCallable, Category = "MuduoNet|Server")
	bool SendToConnection(int64 ConnectionId, const TArray<uint8>& Data);

	// ---- 客户端 ----
	/** 返回客户端端点 id，失败返回 0。 */
	UFUNCTION(BlueprintCallable, Category = "MuduoNet|Client")
	int64 ConnectToServer(const FString& Host, int32 Port);

	UFUNCTION(BlueprintCallable, Category = "MuduoNet|Client")
	void DisconnectClient(int64 ClientId);

	UFUNCTION(BlueprintCallable, Category = "MuduoNet|Client")
	bool SendFromClient(int64 ClientId, const TArray<uint8>& Data);

	UFUNCTION(BlueprintPure, Category = "MuduoNet|Client")
	bool IsClientConnected(int64 ClientId) const;

	// ---- 通用 ----
	/** 关闭后消息按「当前可读的原始字节块」投递；两端必须一致。 */
	UFUNCTION(BlueprintCallable, Category = "MuduoNet")
	void SetUseLengthHeaderCodec(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "MuduoNet")
	int64 GetBytesReceived() const;

	UFUNCTION(BlueprintPure, Category = "MuduoNet")
	int64 GetBytesSent() const;

	/** IO 线程上一次事件处理耗时（毫秒，不含 poll 等待）。 */
	UFUNCTION(BlueprintPure, Category = "MuduoNet|Metrics")
	double GetLastLoopBusyMs() const;

	/** IO 线程历史最大事件处理耗时（毫秒）。 */
	UFUNCTION(BlueprintPure, Category = "MuduoNet|Metrics")
	double GetMaxLoopBusyMs() const;

	UPROPERTY(BlueprintAssignable, Category = "MuduoNet")
	FMuduoNetConnectionEvent OnConnectionEvent;

	UPROPERTY(BlueprintAssignable, Category = "MuduoNet")
	FMuduoNetMessageEvent OnMessageEvent;

private:
	mnet::FMuduoNetBridge* GetBridge() const { return Bridge.Get(); }

	TUniquePtr<mnet::FMuduoNetBridge> Bridge;
};
