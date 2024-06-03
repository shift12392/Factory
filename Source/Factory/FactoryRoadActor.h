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

	static const float LaneLineHalfWidth;         //ʵ�ߵĿ�ȵ�һ��
	static const float LaneDashLineLength;        //���ߵĳ���
	static const float LaneDashLineSapceLength;   //���ߵļ������
	static const float RoadSideWidth;             //·�ߵľ���
	static const float TwoCenterLaneLineWidth;    //·�м�˫ʵ��֮��ľ���
public:	
	// Sets default values for this actor's properties
	AFactoryRoadActor();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere)
	float RoadLength = 1000.0f;    //·������λm

	UPROPERTY(EditAnywhere)
	uint8 LaneNum = 2;             //���򳵵���

	UPROPERTY(EditAnywhere)
	float LaneWidth = 3.75;        //������ȣ���λm

	UPROPERTY(EditAnywhere)
	bool bEmergencyLane = true;   //�Ƿ���Ӧ������

	UPROPERTY(EditAnywhere)
	UMaterial* RoadMaterial;      //·�Ĳ���

	UPROPERTY(EditAnywhere)
	UMaterial* LaneLineMaterial;      //�����ߵĲ���

	//�õ���·��
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

	void UpdateRoad();       //����·��

	void UpdateLaneLine();   //���ɱ���

	void CheckData();

	UFUNCTION(CallInEditor,Category = "FactoryRoadActor")
	void GenMesh() { UpdateRoad(); UpdateLaneLine(); }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	//���ǻ�
	static void TriangleAreaWithHole(const TArray<FVector>& OuterVertices, const TArray<TArray<FVector>>& InnearHoles,
		TArray<FVector2D>& OutResults, TArray<FVector2D>& OutUVs);

	//����һ�����Σ�����X������
	void GenRectPoints(const FVector2D& InPos, float InWidth, float InXHeight, TArray<FVector>& OuterVertices);
	
	//����һ�����Σ�����X������
	void GenRectPointsSubtractiveX(const FVector2D& InPos, float InWidth, float InXHeight, TArray<FVector>& OuterVertices);
};
