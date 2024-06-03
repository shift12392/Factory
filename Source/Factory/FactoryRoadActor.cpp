// Fill out your copyright notice in the Description page of Project Settings.


#include "FactoryRoadActor.h"


const float AFactoryRoadActor::LaneLineHalfWidth = 10.0f;         //实线的宽度的一半
const float AFactoryRoadActor::LaneDashLineLength = 600.0f;        //虚线的长度
const float AFactoryRoadActor::LaneDashLineSapceLength = 900.0f;   //虚线的间隔长度
const float AFactoryRoadActor::RoadSideWidth = 50.0f;
const float AFactoryRoadActor::TwoCenterLaneLineWidth = 20.0f;
// Sets default values
AFactoryRoadActor::AFactoryRoadActor()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;

	PMC = CreateDefaultSubobject<UProceduralMeshComponent>(FName(TEXT("ProceduralMeshComponent")));
	PMC->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);

}

// Called when the game starts or when spawned
void AFactoryRoadActor::BeginPlay()
{
	Super::BeginPlay();
	GenMesh();
}

static void ReverPointList(TArray<FVector2D>& PointList)
{
	FVector2D Temp;
	int32 i, c;

	c = PointList.Num() / 2;
	for (i = 0; i < c; i++)
	{
		Temp = PointList[i];

		PointList[i] = PointList[(PointList.Num() - 1) - i];
		PointList[(PointList.Num() - 1) - i] = Temp;
	}
}


void AFactoryRoadActor::UpdateRoad()
{
	CheckData();
	float RoadWidth = GetRoadWidth();
	TArray<FVector> RoadPolygons;
	GenRectPoints(FVector2D(0, 0), RoadWidth, GetRoadLength(), RoadPolygons);

	TArray<TArray<FVector>> Holes;
	TArray<FVector2D> OutResults;
	TArray<FVector2D> OutUVs;
	TriangleAreaWithHole(RoadPolygons, Holes, OutResults, OutUVs);

	TArray<int32> OutIndexs;
	TArray<FVector> MeshVertices;
	TArray<FColor>  MeshColors;
	TArray<FVector> Normals;
	TArray<FProcMeshTangent> Tangents;
	for (int32 i = 0; i < OutResults.Num(); ++i)
	{
		MeshVertices.Add(FVector(OutResults[i], 0.0f));
		MeshColors.Add(FColor(255, 0, 0, 0));
		Normals.Add(FVector::UpVector);

		Tangents.Add(FProcMeshTangent(FVector::ForwardVector, false));
		OutIndexs.Add(i);
	}

	PMC->CreateMeshSection(0, MeshVertices, OutIndexs, Normals, OutUVs, MeshColors, Tangents, false);

	if (RoadMaterial != nullptr)
		PMC->SetMaterial(0, UMaterialInstanceDynamic::Create(RoadMaterial, this));
}


void AFactoryRoadActor::UpdateLaneLine()
{
	CheckData();
	float RoadHalfWidth = GetRoadWidth() * 0.5f;
	TArray<TArray<FVector> > AllLaneLinePolygons;
	TArray<FVector> LaneLinePolygons;

	float TwoCenterLaneLineHalfWidth = TwoCenterLaneLineWidth * 0.5f;

	//右边
	{
		float StartY = (LaneLineHalfWidth + TwoCenterLaneLineHalfWidth);
		float EndY = (RoadHalfWidth - GetRoadSideWidth());
		GenRectPoints(FVector2D(0, StartY), LaneLineHalfWidth * 2.0f, GetRoadLength(), LaneLinePolygons);
		AllLaneLinePolygons.Add(LaneLinePolygons);

		if (LaneNum > 1)
		{
			float Temp = (EndY - StartY) / LaneNum;
			for (int32 i = 0; i < LaneNum - 1; ++i)
			{
				for (float XPos = 0; XPos < GetRoadLength(); XPos += (LaneDashLineLength + LaneDashLineSapceLength))
				{
					float LaneLength = LaneDashLineLength;
					if (XPos + LaneDashLineLength > GetRoadLength())
						LaneLength = GetRoadLength() - XPos;

					LaneLinePolygons.Empty();
					GenRectPoints(FVector2D(XPos, StartY + Temp * (i + 1)), LaneLineHalfWidth * 2.0f, LaneLength, LaneLinePolygons);
					AllLaneLinePolygons.Add(LaneLinePolygons);
				}
			}
		}

		LaneLinePolygons.Empty();
		GenRectPoints(FVector2D(0, EndY), LaneLineHalfWidth * 2.0f, GetRoadLength(), LaneLinePolygons);
		AllLaneLinePolygons.Add(LaneLinePolygons);
	}

	//左边
	{
		float StartY = -(LaneLineHalfWidth + TwoCenterLaneLineHalfWidth);
		float EndY = -(RoadHalfWidth - GetRoadSideWidth());

		LaneLinePolygons.Empty();
		GenRectPoints(FVector2D(0, StartY), LaneLineHalfWidth * 2.0f, GetRoadLength(), LaneLinePolygons);
		AllLaneLinePolygons.Add(LaneLinePolygons);

		if (LaneNum > 1)
		{
			float Temp = (EndY - StartY) / LaneNum;
			for (int32 i = 0; i < LaneNum - 1; ++i)
			{
				for (float XPos = GetRoadLength(); XPos > 0; XPos -= (LaneDashLineLength + LaneDashLineSapceLength))
				{
					float LaneLength = LaneDashLineLength;
					if (XPos - LaneDashLineLength < 0)
						LaneLength = XPos;

					LaneLinePolygons.Empty();
					GenRectPointsSubtractiveX(FVector2D(XPos, StartY + Temp * (i + 1)), LaneLineHalfWidth * 2.0f, LaneLength, LaneLinePolygons);
					AllLaneLinePolygons.Add(LaneLinePolygons);
				}
			}
		}

		LaneLinePolygons.Empty();
		GenRectPoints(FVector2D(0, EndY), LaneLineHalfWidth * 2.0f, GetRoadLength(), LaneLinePolygons);
		AllLaneLinePolygons.Add(LaneLinePolygons);

	}

	TArray<int32> OutIndexs;
	TArray<FVector> MeshVertices;
	TArray<FColor>  MeshColors;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FProcMeshTangent> Tangents;
	int Index = 0;
	for (auto &LaneLineIte : AllLaneLinePolygons)
	{
		TArray<FVector2D> OutResults;
		TArray<FVector2D> OutUVs;
		TriangleAreaWithHole(LaneLineIte, TArray<TArray<FVector>>(), OutResults, OutUVs);
	    
		UVs.Append(OutUVs);
		for (int32 i = 0; i < OutResults.Num(); ++i,++Index)
		{
			MeshVertices.Add(FVector(OutResults[i], 0.1f));
			MeshColors.Add(FColor(255, 0, 0, 0));
			Normals.Add(FVector::UpVector);

			Tangents.Add(FProcMeshTangent(FVector::ForwardVector, false));
			OutIndexs.Add(Index);
		}
	}

	PMC->CreateMeshSection(1, MeshVertices, OutIndexs, Normals, UVs, MeshColors, Tangents, false);

	if (LaneLineMaterial != nullptr)
		PMC->SetMaterial(1, UMaterialInstanceDynamic::Create(LaneLineMaterial, this));
}

void AFactoryRoadActor::CheckData()
{
	if(RoadLength <= 1.0f)
	   RoadLength = 1000.0f;    //路长，单位m

	if (LaneNum < 1)
	   LaneNum = 2;             //单向车道数

	if (LaneWidth <= 0.5f)
	   LaneWidth = 3.75;        //车道宽度，单位m
}

#if WITH_EDITOR
void AFactoryRoadActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FProperty* MemberPropertyThatChanged = PropertyChangedEvent.MemberProperty;
	const FName MemberPropertyName = MemberPropertyThatChanged != NULL ? MemberPropertyThatChanged->GetFName() : NAME_None;
	if (MemberPropertyName == FName("RoadLength")
		|| MemberPropertyName == FName("LaneNum")
		|| MemberPropertyName == FName("LaneWidth")
		|| MemberPropertyName == FName("bEmergencyLane")
		|| MemberPropertyName == FName("RoadMaterial")
		|| MemberPropertyName == FName("LaneLineMaterial"))
	{
		GenMesh();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void AFactoryRoadActor::TriangleAreaWithHole(const TArray<FVector>& OuterVertices, const TArray<TArray<FVector>>& InnearHoles,
	TArray<FVector2D>& OutResults, TArray<FVector2D>& OutUVs)
{
	XRPolygon<std::pair<float, float>> tempVertices;
	std::vector<std::pair<float, float>> OutConvertexs;
	for (FVector iter : OuterVertices)
	{
		std::pair<float, float> converVert{ iter.X,iter.Y };
		OutConvertexs.push_back(converVert);
	}
	TArray<FVector> totalVetices;
	totalVetices.Append(OuterVertices);
	tempVertices.push_back(OutConvertexs);
	for (TArray<FVector> iter : InnearHoles)
	{
		std::vector<std::pair<float, float>> innerHole;
		for (FVector& iter0 : iter)
		{
			std::pair<float, float> converVert{ iter0.X,iter0.Y };
			innerHole.push_back(converVert);
		}
		tempVertices.push_back(innerHole);
		totalVetices.Append(iter);
	}
	FXREarcutTesselator<float, decltype(tempVertices)> tesslator(tempVertices);
	tesslator.Run();
	std::vector<uint32_t> indexs = tesslator.Indices();
	for (uint32_t index : indexs)
	{
		OutResults.Push(FVector2D(totalVetices[index]));
	}

	int TraianglePoints = OutResults.Num();
	ReverPointList(OutResults);

	FBox box(OuterVertices);
	FVector BoxSize = box.GetSize();

	for (int32 index = 0; index < TraianglePoints; ++index)
	{
		float VCoord, UCoord;
		//UCoord = (OutResults[index].X - box.Min.X) / BoxSize.X;
		//VCoord = (OutResults[index].Y - box.Min.Y) / BoxSize.Y;
		UCoord = (OutResults[index].X - box.Min.X) * 10.0f / 1024;
		VCoord = (OutResults[index].Y - box.Min.Y) * 10.0f / 1024;
		OutUVs.Add(FVector2D(UCoord, VCoord));
	}
}


void AFactoryRoadActor::GenRectPoints(const FVector2D& InPos, float InWidth, float InXHeight, TArray<FVector>& OuterVertices)
{
	OuterVertices.Add(FVector(InPos.X, InPos.Y - InWidth * 0.5f, 0));
	OuterVertices.Add(FVector(InPos.X + InXHeight, InPos.Y - InWidth * 0.5f, 0));
	OuterVertices.Add(FVector(InPos.X + InXHeight, InPos.Y + InWidth * 0.5f, 0));
	OuterVertices.Add(FVector(InPos.X, InPos.Y + InWidth * 0.5f, 0));
}

void AFactoryRoadActor::GenRectPointsSubtractiveX(const FVector2D& InPos, float InWidth, float InXHeight, TArray<FVector>& OuterVertices)
{
	OuterVertices.Add(FVector(InPos.X, InPos.Y - InWidth * 0.5f, 0));
	OuterVertices.Add(FVector(InPos.X - InXHeight, InPos.Y - InWidth * 0.5f, 0));
	OuterVertices.Add(FVector(InPos.X - InXHeight, InPos.Y + InWidth * 0.5f, 0));
	OuterVertices.Add(FVector(InPos.X, InPos.Y + InWidth * 0.5f, 0));
}


