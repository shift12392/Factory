#include "MuduoNetSubsystem.h"

#include "MuduoNetBridge.h"

void UMuduoNetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Bridge = MakeUnique<mnet::FMuduoNetBridge>(mnet::EDispatchMode::GameThread);
	Bridge->SetConnectionEvent([this](int64 EndpointId, bool bConnected)
	{
		// Bridge 在 GameThread 模式下已经把回调切到游戏线程，这里可以安全广播。
		OnConnectionEvent.Broadcast(EndpointId, bConnected);
	});
	Bridge->SetMessageEvent([this](int64 EndpointId, const TArray<uint8>& Message)
	{
		OnMessageEvent.Broadcast(EndpointId, Message);
	});
}

void UMuduoNetSubsystem::Deinitialize()
{
	if (Bridge.IsValid())
	{
		// 先解绑，避免析构过程中还往已销毁的委托广播。
		Bridge->SetConnectionEvent(nullptr);
		Bridge->SetMessageEvent(nullptr);
		Bridge.Reset();
	}

	Super::Deinitialize();
}

bool UMuduoNetSubsystem::StartServer(int32 Port, int32 ServerThreadNum)
{
	return Bridge.IsValid() && Bridge->StartServer(static_cast<uint16>(FMath::Clamp(Port, 0, 65535)), ServerThreadNum);
}

void UMuduoNetSubsystem::StopServer()
{
	if (Bridge.IsValid())
	{
		Bridge->StopServer();
	}
}

bool UMuduoNetSubsystem::IsServerRunning() const
{
	return Bridge.IsValid() && Bridge->IsServerRunning();
}

int32 UMuduoNetSubsystem::GetServerPort() const
{
	return Bridge.IsValid() ? static_cast<int32>(Bridge->GetServerPort()) : 0;
}

int32 UMuduoNetSubsystem::GetServerConnectionCount() const
{
	return Bridge.IsValid() ? Bridge->GetServerConnectionCount() : 0;
}

bool UMuduoNetSubsystem::SendToConnection(int64 ConnectionId, const TArray<uint8>& Data)
{
	return Bridge.IsValid() && Bridge->SendToServerConnection(ConnectionId, Data);
}

int64 UMuduoNetSubsystem::ConnectToServer(const FString& Host, int32 Port)
{
	if (!Bridge.IsValid())
	{
		return 0;
	}
	return Bridge->ConnectToServer(Host, static_cast<uint16>(FMath::Clamp(Port, 1, 65535)));
}

void UMuduoNetSubsystem::DisconnectClient(int64 ClientId)
{
	if (Bridge.IsValid())
	{
		Bridge->DisconnectClient(ClientId);
	}
}

bool UMuduoNetSubsystem::SendFromClient(int64 ClientId, const TArray<uint8>& Data)
{
	return Bridge.IsValid() && Bridge->SendFromClient(ClientId, Data);
}

bool UMuduoNetSubsystem::IsClientConnected(int64 ClientId) const
{
	return Bridge.IsValid() && Bridge->IsClientConnected(ClientId);
}

void UMuduoNetSubsystem::SetUseLengthHeaderCodec(bool bEnabled)
{
	if (Bridge.IsValid())
	{
		Bridge->SetUseLengthHeaderCodec(bEnabled);
	}
}

int64 UMuduoNetSubsystem::GetBytesReceived() const
{
	return Bridge.IsValid() ? Bridge->GetBytesReceived() : 0;
}

int64 UMuduoNetSubsystem::GetBytesSent() const
{
	return Bridge.IsValid() ? Bridge->GetBytesSent() : 0;
}

double UMuduoNetSubsystem::GetLastLoopBusyMs() const
{
	return Bridge.IsValid() ? Bridge->GetLastLoopBusyMs() : 0.0;
}

double UMuduoNetSubsystem::GetMaxLoopBusyMs() const
{
	return Bridge.IsValid() ? Bridge->GetMaxLoopBusyMs() : 0.0;
}
