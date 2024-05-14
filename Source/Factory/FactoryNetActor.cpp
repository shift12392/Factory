// Fill out your copyright notice in the Description page of Project Settings.


#include "FactoryNetActor.h"
#include "FactoryNetSubsystem.h"

// Sets default values
AFactoryNetActor::AFactoryNetActor()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
}

// Called when the game starts or when spawned
void AFactoryNetActor::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void AFactoryNetActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	GetWorld()->GetSubsystem<UFactoryNetSubsystem>()->NetTick(DeltaTime);
}

