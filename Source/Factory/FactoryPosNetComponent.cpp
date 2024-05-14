// Fill out your copyright notice in the Description page of Project Settings.


#include "FactoryPosNetComponent.h"
#include "Sockets.h"

// Sets default values for this component's properties
UFactoryPosNetComponent::UFactoryPosNetComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	// ...
}


// Called when the game starts
void UFactoryPosNetComponent::BeginPlay()
{
	Super::BeginPlay();

	// ...

	for (UActorComponent* Comp : GetOwner()->GetComponents())
	{
		if (Comp->GetFName() == SceneComponentName)
		{
			SceneComponent = Cast<USceneComponent>(Comp);
			break;
		}
	}

	if (SceneComponent == nullptr)
	{
		SceneComponent = GetOwner()->GetRootComponent();
	}

}


void UFactoryPosNetComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UDPSocket.IsValid())
	{
		UDPSocket->Close();
	}

	Super::EndPlay(EndPlayReason);
}

// Called every frame
void UFactoryPosNetComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ...

	if (TwinWorldType == ETwinWorldType::Real)
	{
		if (!UDPSocket.IsValid())
			return;

		AActor* OwnerActor = GetOwner();

		FFactoryUDPPackageHeader Header;
		//Header.LoadBits = LoadSize;
		Header.RobotName = OwnerActor->GetFName();
		Header.PackageId = NetPackageId;
		NetPackageId++;

		FFactoryNetSinglePosePackage Data;
		Data.RobotName = Header.RobotName;
		Data.Pos = SceneComponent->GetComponentLocation();
		Data.Dir = SceneComponent->GetComponentRotation();

		TArray<uint8> LoadBuffer;
		FMemoryWriter MemWriter(LoadBuffer);
		MemWriter.SetWantBinaryPropertySerialization(true);

		//Ð´°üÍ·
		UFactoryNetSubsystem::WriteUDPPackageHeader(MemWriter, Header);
		//Ð´¸ºÔØ
		UScriptStruct* MyStructStruct = FFactoryNetSinglePosePackage::StaticStruct();
		//bool bUseBinary = MyStructStruct->UseBinarySerialization(MemWriter);
		MyStructStruct->SerializeBin(MemWriter, &Data);

		uint32 LoadSize = LoadBuffer.Num();

		TArray<uint8> PackageBuffer;
		FMemoryWriter PackageWriter(PackageBuffer);
		PackageWriter << LoadSize;
		PackageWriter.Serialize(LoadBuffer.GetData(), LoadSize);

		int32 SendBits = 0;
		UDPSocket->SendTo(PackageBuffer.GetData(), PackageBuffer.Num(), SendBits, *TwinServerAddr);
	}
	else if (TwinWorldType == ETwinWorldType::Twin)
	{
		if (SceneComponent == nullptr)
			return;

		UFactoryNetSubsystem* NetSubSystem = GetWorld()->GetSubsystem<UFactoryNetSubsystem>();
		TArray<FFactoryUDPPackageLoad>* FindReader = NetSubSystem->AllUDPPackages.Find(GetOwner()->GetFName());
		if (FindReader != nullptr && FindReader->Num() > 0)
		{
			FindReader->Sort([](const FFactoryUDPPackageLoad& A, const FFactoryUDPPackageLoad& B) -> bool {
				return A.PackageId > B.PackageId;
				});

			FMemoryReader MemR((*FindReader)[0].LoadDatas);
			MemR.Seek((*FindReader)[0].Offset);
			MemR.SetWantBinaryPropertySerialization(true);
			FFactoryNetSinglePosePackage Data;
			UScriptStruct* MyStructStruct = FFactoryNetSinglePosePackage::StaticStruct();
			MyStructStruct->SerializeBin(MemR, &Data);

			SceneComponent->SetWorldLocation(Data.Pos);    
			SceneComponent->SetWorldRotation(Data.Dir);

			NetSubSystem->AllUDPPackages.Remove(GetOwner()->GetFName());
		}
	}
}

