// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "earcut.hpp"
#include <array>
#include "FactoryRoadActor.generated.h"

template<typename Coord, typename Polygon>
class FXREarcutTesselator
{
public:
	using Vertex = std::array<Coord, 2>;
	using Vertices = std::vector<Vertex>;
	FXREarcutTesselator(const Polygon& polygon_) :polygon(polygon_)
	{
		for (const auto& ring : polygon_)
		{
			for (auto& vertex : ring)
			{
				vertices_.emplace_back(Vertex{ { Coord(std::get<0>(vertex)),
					Coord(std::get<1>(vertex)) } });
			}
		}
	}
	FXREarcutTesselator& operator=(const FXREarcutTesselator&) = delete;
	void Run()
	{
		indices_ = mapbox::earcut(polygon);
	}
	std::vector<uint32_t> const& Indices()const
	{
		return indices_;
	}
	Vertices const& GetVertices()
	{
		return vertices_;
	}

private:
	const Polygon& polygon;
	Vertices vertices_;
	std::vector<uint32_t> indices_;
};



UCLASS()
class FACTORY_API AFactoryRoadActor : public AActor
{
	GENERATED_BODY()
	
	template<typename T> using XRPolygon = std::vector<std::vector<T>>;

	UPROPERTY()
	UProceduralMeshComponent* PMC;

	static const float LaneLineHalfWidth;         //实线的宽度的一半
	static const float LaneDashLineLength;        //虚线的长度
	static const float LaneDashLineSapceLength;   //虚线的间隔长度
	static const float RoadSideWidth;             //路边的距离
	static const float TwoCenterLaneLineWidth;    //路中间双实线之间的距离
public:	
	// Sets default values for this actor's properties
	AFactoryRoadActor();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere)
	float RoadLength = 1000.0f;    //路长，单位m

	UPROPERTY(EditAnywhere)
	uint8 LaneNum = 2;             //单向车道数

	UPROPERTY(EditAnywhere)
	float LaneWidth = 3.75;        //车道宽度，单位m

	UPROPERTY(EditAnywhere)
	bool bEmergencyLane = true;   //是否有应急车道

	UPROPERTY(EditAnywhere)
	UMaterial* RoadMaterial;      //路的材质

	UPROPERTY(EditAnywhere)
	UMaterial* LaneLineMaterial;      //车道线的材质

	//得到总路宽
	float GetRoadWidth() const
	{
		float BaseWidth = LaneNum * 2 * LaneWidth * 100.0f;
		BaseWidth += GetRoadSideWidth() * 2;
		return BaseWidth + (LaneLineHalfWidth * 2 + TwoCenterLaneLineWidth);
	}

	float GetRoadSideWidth() const {
		return (bEmergencyLane ?  LaneWidth * 100 : RoadSideWidth );
	}

	inline float GetRoadLength() const { return RoadLength * 100.0f; }

	void UpdateRoad();       //生成路面

	void UpdateLaneLine();   //生成标线

	void CheckData();

	UFUNCTION(CallInEditor,Category = "FactoryRoadActor")
	void GenMesh() { UpdateRoad(); UpdateLaneLine(); }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	//三角化
	static void TriangleAreaWithHole(const TArray<FVector>& OuterVertices, const TArray<TArray<FVector>>& InnearHoles,
		TArray<FVector2D>& OutResults, TArray<FVector2D>& OutUVs);

	//生成一个矩形，沿着X正方形
	void GenRectPoints(const FVector2D& InPos, float InWidth, float InXHeight, TArray<FVector>& OuterVertices);
	
	//生成一个矩形，沿着X反方形
	void GenRectPointsSubtractiveX(const FVector2D& InPos, float InWidth, float InXHeight, TArray<FVector>& OuterVertices);
};
