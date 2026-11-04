#include "MuduoNetComponent.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "MuduoNetSubsystem.h"

UMuduoNetComponent::UMuduoNetComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UMuduoNetSubsystem* UMuduoNetComponent::GetNetSubsystem() const
{
	if (const UWorld* World = GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			return GameInstance->GetSubsystem<UMuduoNetSubsystem>();
		}
	}
	return nullptr;
}

void UMuduoNetComponent::BeginPlay()
{
	Super::BeginPlay();

	if (UMuduoNetSubsystem* Subsystem = GetNetSubsystem())
	{
		Subsystem->OnConnectionEvent.AddDynamic(this, &UMuduoNetComponent::HandleConnectionEvent);
		Subsystem->OnMessageEvent.AddDynamic(this, &UMuduoNetComponent::HandleMessageEvent);
	}

	if (bAutoConnect)
	{
		Connect();
	}
}

void UMuduoNetComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Disconnect();

	if (UMuduoNetSubsystem* Subsystem = GetNetSubsystem())
	{
		Subsystem->OnConnectionEvent.RemoveDynamic(this, &UMuduoNetComponent::HandleConnectionEvent);
		Subsystem->OnMessageEvent.RemoveDynamic(this, &UMuduoNetComponent::HandleMessageEvent);
	}

	Super::EndPlay(EndPlayReason);
}

void UMuduoNetComponent::Connect()
{
	if (ClientId != 0)
	{
		return;
	}

	if (UMuduoNetSubsystem* Subsystem = GetNetSubsystem())
	{
		ClientId = Subsystem->ConnectToServer(ServerHost, ServerPort);
	}
}

void UMuduoNetComponent::Disconnect()
{
	if (ClientId == 0)
	{
		return;
	}

	if (UMuduoNetSubsystem* Subsystem = GetNetSubsystem())
	{
		Subsystem->DisconnectClient(ClientId);
	}
	ClientId = 0;
}

bool UMuduoNetComponent::Send(const TArray<uint8>& Data)
{
	UMuduoNetSubsystem* Subsystem = GetNetSubsystem();
	return ClientId != 0 && Subsystem != nullptr && Subsystem->SendFromClient(ClientId, Data);
}

bool UMuduoNetComponent::IsConnected() const
{
	UMuduoNetSubsystem* Subsystem = GetNetSubsystem();
	return ClientId != 0 && Subsystem != nullptr && Subsystem->IsClientConnected(ClientId);
}

void UMuduoNetComponent::HandleConnectionEvent(int64 EndpointId, bool bConnected)
{
	if (EndpointId != ClientId)
	{
		return;
	}

	if (bConnected)
	{
		OnConnected.Broadcast();
	}
	else
	{
		OnDisconnected.Broadcast();
	}
}

void UMuduoNetComponent::HandleMessageEvent(int64 EndpointId, const TArray<uint8>& Message)
{
	if (EndpointId != ClientId)
	{
		return;
	}

	OnMessage.Broadcast(Message);
}
