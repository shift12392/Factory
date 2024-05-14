// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SocketSubsystem.h"
#include "FactoryNetSubsystem.h"
#include "FactoryNetComponent.h"
#include "FactoryPosNetComponent.generated.h"


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class FACTORY_API UFactoryPosNetComponent : public UFactoryNetComponent
{
	GENERATED_BODY()

private:
	UPROPERTY()
	USceneComponent* SceneComponent;
public:
	UPROPERTY(EditDefaultsOnly)
	FName SceneComponentName;

public:	
	// Sets default values for this component's properties
	UFactoryPosNetComponent();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
public:	
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

		
};
