// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SocketSubsystem.h"
#include "FactoryNetComponent.h"
#include "FactoryBoneNetComponent.generated.h"


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class FACTORY_API UFactoryBoneNetComponent : public UFactoryNetComponent
{
	GENERATED_BODY()

private:
	UPROPERTY()
	USkeletalMeshComponent* SkeletalMeshComponent;

	UPROPERTY()
	class UPoseableMeshComponent* PoseableMeshComponent;
	/*
public:
	UPROPERTY(EditDefaultsOnly)
	FName SkeletalMeshComponentName;
	UPROPERTY(EditDefaultsOnly)
	FName PoseableMeshComponentName;
	*/
public:	
	// Sets default values for this component's properties
	UFactoryBoneNetComponent();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
public:	
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

		
};
