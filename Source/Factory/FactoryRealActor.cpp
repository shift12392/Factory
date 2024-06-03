// Fill out your copyright notice in the Description page of Project Settings.


#include "FactoryRealActor.h"

// Sets default values
AFactoryRealActor::AFactoryRealActor()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

// Called when the game starts or when spawned
void AFactoryRealActor::BeginPlay()
{
	Super::BeginPlay();
}

// Called every frame
void AFactoryRealActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

/*
static void RecomputeTangents(TArray<FVector>& InVertices, TArray<uint32>& InTriangles,TArray<FVector> &OutTangents)
{
	for (int32 TriIdx = 0; TriIdx < InTriangles.Num(); TriIdx += 3)
	{
		uint32 TriangleVertex0 = InTriangles[TriIdx];
		uint32 TriangleVertex1 = InTriangles[TriIdx + 1];
		uint32 TriangleVertex2 = InTriangles[TriIdx + 2];
		const FVector& v0 = InVertices[TriangleVertex0];
		const FVector& v1 = InVertices[TriangleVertex1];
		const FVector& v2 = InVertices[TriangleVertex2];

		const FVector Edge01 = v1 - v0;
		const FVector Edge02 = v2 - v0;

		FVector TangentX = Edge01.GetSafeNormal();
		//FVector TangentZ = (Edge02 ^ Edge01).GetSafeNormal();
		//FVector TangentY = (TangentX ^ TangentZ).GetSafeNormal();

		TangentX.Normalize();
		//TangentZ.Normalize();
		//TangentY.Normalize();


		InVertices[TriangleVertex0].Tangent.TangentX = TangentX;
		InVertices[TriangleVertex1].Tangent.TangentX = TangentX;
		InVertices[TriangleVertex2].Tangent.TangentX = TangentX;
	}
}
*/

