// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FactoryNetSubsystem.h"
#include "FactoryTwinActor.generated.h"

UCLASS()
class FACTORY_API AFactoryTwinActor : public AActor
{
	GENERATED_BODY()
	
public:
	UPROPERTY(EditAnywhere)
	FName RobotName;

public:	
	// Sets default values for this actor's properties
	AFactoryTwinActor();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

};
