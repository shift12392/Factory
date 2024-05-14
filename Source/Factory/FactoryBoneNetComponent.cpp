// Fill out your copyright notice in the Description page of Project Settings.


#include "FactoryBoneNetComponent.h"
#include "FactoryNetSubsystem.h"
#include "Sockets.h"
#include "Components/PoseableMeshComponent.h"

// Sets default values for this component's properties
UFactoryBoneNetComponent::UFactoryBoneNetComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	// ...
}


// Called when the game starts
void UFactoryBoneNetComponent::BeginPlay()
{
	Super::BeginPlay();

	// ...
	
	if (TwinWorldType == ETwinWorldType::Real)
	{
		TArray<USkeletalMeshComponent*> AllComponents;
		GetOwner()->GetComponents<USkeletalMeshComponent>(AllComponents);
		if (AllComponents.Num() > 0)
			SkeletalMeshComponent = AllComponents[0];
	}
	else if (TwinWorldType == ETwinWorldType::Twin)
	{
		TArray<UPoseableMeshComponent*> AllComponents;
		GetOwner()->GetComponents<UPoseableMeshComponent>(AllComponents);
		if (AllComponents.Num() > 0)
			PoseableMeshComponent = AllComponents[0];
	}

	/*
	for (UActorComponent* Comp : GetOwner()->GetComponents())
	{
		FName CompName = Comp->GetFName();
		if (CompName == SkeletalMeshComponentName)
		{
			SkeletalMeshComponent = Cast<USkeletalMeshComponent>(Comp);
		}
		else if (CompName == PoseableMeshComponentName)
		{
			PoseableMeshComponent = Cast<UPoseableMeshComponent>(Comp);
		}
	}
	*/
}

void UFactoryBoneNetComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UDPSocket.IsValid())
	{
		UDPSocket->Close();
	}

	Super::EndPlay(EndPlayReason);
}


// Called every frame
void UFactoryBoneNetComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ...
	AActor* OwnerActor = GetOwner();
	if (TwinWorldType == ETwinWorldType::Real)
	{
		if (!UDPSocket.IsValid() || SkeletalMeshComponent == nullptr)
			return;

		TArray<FName> AllBones;
		SkeletalMeshComponent->GetBoneNames(AllBones);
		if (AllBones.Num() > 256)
			return;

		FFactoryUDPPackageHeader Header;
		Header.RobotName = OwnerActor->GetFName();
		Header.PackageId = NetPackageId;
		NetPackageId++;


		TArray<uint8> DataBuffer;
		FMemoryWriter MemWriter(DataBuffer);
		MemWriter.SetWantBinaryPropertySerialization(true);
		//Ð´°üÍ·
		UFactoryNetSubsystem::WriteUDPPackageHeader(MemWriter, Header);

		//Ð´¸ºÔØ
		uint8 BoneNum = AllBones.Num();
		MemWriter << BoneNum;
		for (int32 i = 0; i < AllBones.Num(); ++i)
		{
			FFactoryNetBonePoseData Packages;
			Packages.BoneName = AllBones[i];
			Packages.Dir = SkeletalMeshComponent->GetBoneQuaternion(AllBones[i], EBoneSpaces::ComponentSpace);

			UScriptStruct* MyStructStruct = FFactoryNetBonePoseData::StaticStruct();
			//bool bUseBinary = MyStructStruct->UseBinarySerialization(MemWriter);
			MyStructStruct->SerializeBin(MemWriter, &Packages);
		}
		int32 loadSize = DataBuffer.Num();
		TArray<uint8> PackageBuffer;
		FMemoryWriter PackageWriter(PackageBuffer);
		PackageWriter << loadSize;
		PackageWriter.Serialize(DataBuffer.GetData(), DataBuffer.Num());

		int32 SendBits = 0;
		UDPSocket->SendTo(PackageBuffer.GetData(), PackageBuffer.Num(), SendBits, *TwinServerAddr);
	}
	else if (TwinWorldType == ETwinWorldType::Twin)
	{
		if (PoseableMeshComponent == nullptr)
			return;

		UFactoryNetSubsystem* NetSubSystem = GetWorld()->GetSubsystem<UFactoryNetSubsystem>();
		TArray<FFactoryUDPPackageLoad>* FindReader = NetSubSystem->AllUDPPackages.Find(OwnerActor->GetFName());
		if (FindReader != nullptr && FindReader->Num() > 0)
		{
			FindReader->Sort([](const FFactoryUDPPackageLoad& A, const FFactoryUDPPackageLoad& B) -> bool {
				return A.PackageId > B.PackageId;
				});

			uint8 BoneNum = 0;
			FMemoryReader MemR((*FindReader)[0].LoadDatas);
			MemR.SetWantBinaryPropertySerialization(true);
			MemR.Seek((*FindReader)[0].Offset);
			MemR << BoneNum;
			
			for (int32 i = 0; i < BoneNum; ++i)
			{
				FFactoryNetBonePoseData PosData;
				UScriptStruct* ScriptStruct = FFactoryNetBonePoseData::StaticStruct();
				ScriptStruct->SerializeBin(MemR, &PosData);

				PoseableMeshComponent->SetBoneRotationByName(PosData.BoneName, PosData.Dir.Rotator(), EBoneSpaces::Type::ComponentSpace);
			}

		}

		NetSubSystem->AllUDPPackages.Remove(OwnerActor->GetFName());
	}
}

