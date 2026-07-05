#include "InkBodyActor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "InkCanvasComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "StaticMeshResources.h"
#include "UObject/ConstructorHelpers.h"

AInkBodyActor::AInkBodyActor()
{
	PrimaryActorTick.bCanEverTick = false;

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	SetRootComponent(BodyMesh);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	BodyMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	// 預設給一顆球，確保 actor 放進關卡就能畫；正式身體網格在編輯器指定。
	// 注意：不設縮放——身體網格以真實尺寸（174cm）匯入
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereAsset(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereAsset.Succeeded())
	{
		BodyMesh->SetStaticMesh(SphereAsset.Object);
	}

	InkCanvas = CreateDefaultSubobject<UInkCanvasComponent>(TEXT("InkCanvas"));
}

void AInkBodyActor::BeginPlay()
{
	Super::BeginPlay();

	if (BodyMaterial && BodyMesh && InkCanvas)
	{
		DynamicBodyMaterial = UMaterialInstanceDynamic::Create(BodyMaterial, this);
		if (DynamicBodyMaterial)
		{
			DynamicBodyMaterial->SetTextureParameterValue(MarkerRTParam, InkCanvas->GetMarkerRenderTarget());
			DynamicBodyMaterial->SetTextureParameterValue(TattooRTParam, InkCanvas->GetTattooRenderTarget());
			if (FaceTexture)
			{
				DynamicBodyMaterial->SetTextureParameterValue(FaceTexParam, FaceTexture);
			}
			DynamicBodyMaterial->SetVectorParameterValue(SkinToneParam, SkinTone);
			BodyMesh->SetMaterial(0, DynamicBodyMaterial);
		}
	}

	BuildTriCache();
}

bool AInkBodyActor::BuildTriCache()
{
	CachedTris.Reset();
	bTriCacheBuilt = false;

	UStaticMesh* SM = BodyMesh ? BodyMesh->GetStaticMesh() : nullptr;
	if (!SM || !SM->GetRenderData() || SM->GetRenderData()->LODResources.Num() == 0)
	{
		return false;
	}

	const FStaticMeshLODResources& LOD = SM->GetRenderData()->LODResources[0];
	const FPositionVertexBuffer& Positions = LOD.VertexBuffers.PositionVertexBuffer;
	const FStaticMeshVertexBuffer& Vertices = LOD.VertexBuffers.StaticMeshVertexBuffer;
	const FRawStaticIndexBuffer& Indices = LOD.IndexBuffer;

	if (Positions.GetNumVertices() == 0 || Indices.GetNumIndices() == 0)
	{
		return false;
	}

	const uint32 SafeUvChannel = FMath::Clamp<uint32>(UvChannel, 0, Vertices.GetNumTexCoords() - 1);
	const int32 NumTris = Indices.GetNumIndices() / 3;
	CachedTris.Reserve(NumTris);
	for (int32 Tri = 0; Tri < NumTris; ++Tri)
	{
		const uint32 I0 = Indices.GetIndex(Tri * 3 + 0);
		const uint32 I1 = Indices.GetIndex(Tri * 3 + 1);
		const uint32 I2 = Indices.GetIndex(Tri * 3 + 2);

		FCachedTri Cached;
		Cached.A = FVector(Positions.VertexPosition(I0));
		Cached.B = FVector(Positions.VertexPosition(I1));
		Cached.C = FVector(Positions.VertexPosition(I2));
		Cached.UVA = FVector2D(Vertices.GetVertexUV(I0, SafeUvChannel));
		Cached.UVB = FVector2D(Vertices.GetVertexUV(I1, SafeUvChannel));
		Cached.UVC = FVector2D(Vertices.GetVertexUV(I2, SafeUvChannel));
		CachedTris.Add(Cached);
	}

	bTriCacheBuilt = CachedTris.Num() > 0;
	return bTriCacheBuilt;
}

bool AInkBodyActor::ResolveBodyUV(const FVector& WorldPosition, FVector2D& OutUV)
{
	if (!bTriCacheBuilt && !BuildTriCache())
	{
		return false;
	}

	const FVector Local = BodyMesh->GetComponentTransform().InverseTransformPosition(WorldPosition);

	float BestDistSq = TNumericLimits<float>::Max();
	FVector2D BestUV = FVector2D::ZeroVector;
	for (const FCachedTri& Tri : CachedTris)
	{
		const FVector Closest = FMath::ClosestPointOnTriangleToPoint(Local, Tri.A, Tri.B, Tri.C);
		const float DistSq = FVector::DistSquared(Closest, Local);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			const FVector Bary = FMath::ComputeBaryCentric2D(Closest, Tri.A, Tri.B, Tri.C);
			BestUV = Tri.UVA * Bary.X + Tri.UVB * Bary.Y + Tri.UVC * Bary.Z;
		}
	}

	// 命中點離網格太遠（>10cm）視為無效——通常代表打到別的東西
	if (BestDistSq > FMath::Square(10.0f))
	{
		return false;
	}

	OutUV = BestUV;
	return true;
}
