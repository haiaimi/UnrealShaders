// Fill out your copyright notice in the Description page of Project Settings.


#include "RainRenderingCone.h"

// Sets default values
ARainRenderingCone::ARainRenderingCone()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	ConeMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ProceduralConeMesh"));
}

// Called when the game starts or when spawned
void ARainRenderingCone::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void ARainRenderingCone::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

void ARainRenderingCone::MakeConeProceduralMesh()
{
	static const uint32 ConeEdgeVertexCount = 10;
	if (ConeMeshComponent)
	{
		 static TArray<int32> Triangles; 
		 static TArray<FVector> Vertices;

		 Vertices.Empty();
		 Vertices.Reserve(ConeEdgeVertexCount * 3);
		 Triangles.Empty();
		 Triangles.Reserve(ConeEdgeVertexCount * 2 * 3);

		 // Add Top Vertices
		 for (int32 i = 0; i < ConeEdgeVertexCount; ++i)
		 {
			 Vertices.Add(FVector(0.f, 0.f, 2.f));
		 }

		 // Add Middle Vertices
		 const float DeltaAngle = 2 * PI / ConeEdgeVertexCount;
		 for (int32 i = 0; i < ConeEdgeVertexCount; ++i)
		 {
			float X = 0, Y = 0;
			FMath::SinCos(&X, &Y, DeltaAngle * i);
			Vertices.Add(FVector(X, Y, 0.f));
		 }

		 // Add Bottom Vertices
		 for (int32 i = 0; i < ConeEdgeVertexCount; ++i)
		 {
			Vertices.Add(FVector(0.f, 0.f, -2.f));
		 }

		 // Add Vertex Indices
		 for (int32 i = 0; i < ConeEdgeVertexCount; ++i)
		 {
			Triangles.Add(i);
			Triangles.Add(i + ConeEdgeVertexCount);
			Triangles.Add((i + 1) % ConeEdgeVertexCount + ConeEdgeVertexCount);

			Triangles.Add(i + ConeEdgeVertexCount);
			Triangles.Add(i + 2 * ConeEdgeVertexCount);
			Triangles.Add((i + 1) % ConeEdgeVertexCount + ConeEdgeVertexCount);
		 }
	}
}

