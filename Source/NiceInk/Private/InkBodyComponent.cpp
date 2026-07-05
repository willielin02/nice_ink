#include "InkBodyComponent.h"

#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "InkCanvasComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiceInkTypes.h"
#include "StaticMeshResources.h"

UInkBodyComponent::UInkBodyComponent()
{
	SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}

void UInkBodyComponent::ApplyAvatar(const FNiceInkAvatarDef& Avatar)
{
	FaceOpenTexture = LoadObject<UTexture2D>(nullptr, *Avatar.FaceOpenPath);
	FaceClosedTexture = LoadObject<UTexture2D>(nullptr, *Avatar.FaceClosedPath);
	EyeMaskTexture = LoadObject<UTexture2D>(nullptr, *Avatar.EyeMaskPath);
	SkinTone = Avatar.SkinTone;

	if (DynamicBodyMaterial)
	{
		DynamicBodyMaterial->SetVectorParameterValue(SkinToneParam, SkinTone);
		if (EyeMaskTexture)
		{
			DynamicBodyMaterial->SetTextureParameterValue(EyeMaskParam, EyeMaskTexture);
		}
		ApplyFaceTexture();
	}
}

void UInkBodyComponent::BindCanvas(UInkCanvasComponent* Canvas)
{
	UMaterialInterface* BaseMaterial = BodyMaterial ? BodyMaterial.Get() : GetMaterial(0);
	if (!BaseMaterial || !Canvas)
	{
		return;
	}

	if (!DynamicBodyMaterial || DynamicBodyMaterial->Parent != BaseMaterial)
	{
		DynamicBodyMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		SetMaterial(0, DynamicBodyMaterial);
	}

	DynamicBodyMaterial->SetTextureParameterValue(MarkerRTParam, Canvas->GetMarkerRenderTarget());
	DynamicBodyMaterial->SetTextureParameterValue(TattooRTParam, Canvas->GetTattooRenderTarget());
	DynamicBodyMaterial->SetVectorParameterValue(SkinToneParam, SkinTone);
	if (EyeMaskTexture)
	{
		DynamicBodyMaterial->SetTextureParameterValue(EyeMaskParam, EyeMaskTexture);
	}
	ApplyFaceTexture();
}

void UInkBodyComponent::SwapBodyMesh(UStaticMesh* NewMesh)
{
	if (!NewMesh || GetStaticMesh() == NewMesh)
	{
		return;
	}
	SetStaticMesh(NewMesh);
	bTriCacheBuilt = false;
	CachedTris.Reset();
	// SetStaticMesh 會重設材質槽為新網格預設——把 MID 綁回去
	if (DynamicBodyMaterial)
	{
		SetMaterial(0, DynamicBodyMaterial);
	}
}

void UInkBodyComponent::SetEyesClosed(bool bClosed)
{
	bEyesClosed = bClosed;
	ApplyFaceTexture();
}

void UInkBodyComponent::ApplyFaceTexture()
{
	if (!DynamicBodyMaterial)
	{
		return;
	}

	UTexture2D* Face = bEyesClosed && FaceClosedTexture ? FaceClosedTexture.Get() : FaceOpenTexture.Get();
	if (Face)
	{
		DynamicBodyMaterial->SetTextureParameterValue(FaceTexParam, Face);
	}
}

bool UInkBodyComponent::BuildTriCache()
{
	CachedTris.Reset();
	bTriCacheBuilt = false;

	UStaticMesh* SM = GetStaticMesh();
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

FString UInkBodyComponent::DebugResolveBodyUV(const FVector& WorldPosition)
{
	const bool bCache = bTriCacheBuilt || BuildTriCache();
	const FVector Local = GetComponentTransform().InverseTransformPosition(WorldPosition);
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
	UStaticMesh* SM = GetStaticMesh();
	const int32 NumLods = (SM && SM->GetRenderData()) ? SM->GetRenderData()->LODResources.Num() : -1;
	return FString::Printf(TEXT("cache=%d tris=%d lods=%d local=(%.1f,%.1f,%.1f) bestDist=%.2f uv=(%.4f,%.4f)"),
		bCache ? 1 : 0, CachedTris.Num(), NumLods, Local.X, Local.Y, Local.Z,
		CachedTris.Num() > 0 ? FMath::Sqrt(BestDistSq) : -1.0f, BestUV.X, BestUV.Y);
}

bool UInkBodyComponent::ResolveBodyUV(const FVector& WorldPosition, FVector2D& OutUV, float MaxDistance)
{
	if (!bTriCacheBuilt && !BuildTriCache())
	{
		return false;
	}

	const FVector Local = GetComponentTransform().InverseTransformPosition(WorldPosition);

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

	// 命中點離網格太遠視為無效——通常代表打到別的東西
	if (BestDistSq > FMath::Square(MaxDistance))
	{
		return false;
	}

	OutUV = BestUV;
	return true;
}
