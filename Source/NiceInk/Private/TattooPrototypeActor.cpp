#include "TattooPrototypeActor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TattooComponent.h"
#include "UObject/ConstructorHelpers.h"

ATattooPrototypeActor::ATattooPrototypeActor()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	PlaneMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaneMesh"));
	SetRootComponent(PlaneMesh);
	PlaneMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneAsset(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneAsset.Succeeded())
	{
		PlaneMesh->SetStaticMesh(PlaneAsset.Object);
		PlaneMesh->SetWorldScale3D(FVector(5.0f, 5.0f, 1.0f));
	}

	TattooComponent = CreateDefaultSubobject<UTattooComponent>(TEXT("TattooComponent"));
	TattooComponent->bAutoDemoDrawing = false;
}

void ATattooPrototypeActor::BeginPlay()
{
	Super::BeginPlay();

	if (TattooComponent)
	{
		TattooComponent->InitializeRenderTarget(TattooComponent->RenderTargetResolution);
		TattooComponent->bAutoDemoDrawing = bStartAutoDemoOnBeginPlay;
	}

	if (PlaneMaterial && PlaneMesh && TattooComponent)
	{
		DynamicPlaneMaterial = UMaterialInstanceDynamic::Create(PlaneMaterial, this);
		if (DynamicPlaneMaterial)
		{
			DynamicPlaneMaterial->SetTextureParameterValue(TattooTextureParameter, TattooComponent->GetRenderTarget());
			PlaneMesh->SetMaterial(0, DynamicPlaneMaterial);
		}
	}
}

void ATattooPrototypeActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bEnableMousePaintingInPIE || !TattooComponent || !GetWorld() || !GetWorld()->IsPlayInEditor())
	{
		return;
	}

	APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
	const bool bWantsPaint = PlayerController && PlayerController->IsInputKeyDown(EKeys::LeftMouseButton);
	if (!bWantsPaint)
	{
		if (bMousePainting)
		{
			TattooComponent->StopTattooing();
			bMousePainting = false;
		}
		return;
	}

	FVector2D PaintUV = FVector2D::ZeroVector;
	if (!ResolveMousePaintUV(PaintUV))
	{
		if (bMousePainting)
		{
			TattooComponent->StopTattooing();
			bMousePainting = false;
		}
		return;
	}

	if (!bMousePainting)
	{
		TattooComponent->BeginTattooingAtUV(PaintUV, 1.0f);
		bMousePainting = true;
	}
	else
	{
		TattooComponent->SubmitStrokeAtUV(PaintUV, 1.0f);
	}
}

bool ATattooPrototypeActor::ResolveMousePaintUV(FVector2D& OutUV) const
{
	if (!PlaneMesh || !GetWorld())
	{
		return false;
	}

	APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
	if (!PlayerController)
	{
		return false;
	}

	FVector WorldOrigin = FVector::ZeroVector;
	FVector WorldDirection = FVector::ForwardVector;
	if (!PlayerController->DeprojectMousePositionToWorld(WorldOrigin, WorldDirection))
	{
		return false;
	}

	FHitResult HitResult;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(NiceInkMousePaint), true);
	const FVector TraceEnd = WorldOrigin + WorldDirection * 20000.0f;
	if (!GetWorld()->LineTraceSingleByChannel(HitResult, WorldOrigin, TraceEnd, ECC_Visibility, QueryParams))
	{
		return false;
	}

	if (HitResult.GetActor() != this)
	{
		return false;
	}

	if (UGameplayStatics::FindCollisionUV(HitResult, 0, OutUV))
	{
		OutUV.X = FMath::Clamp(OutUV.X, 0.0f, 1.0f);
		OutUV.Y = FMath::Clamp(OutUV.Y, 0.0f, 1.0f);
		return true;
	}

	const FVector LocalPoint = PlaneMesh->GetComponentTransform().InverseTransformPosition(HitResult.ImpactPoint);
	OutUV.X = FMath::Clamp(LocalPoint.X / 100.0f + 0.5f, 0.0f, 1.0f);
	OutUV.Y = FMath::Clamp(1.0f - (LocalPoint.Y / 100.0f + 0.5f), 0.0f, 1.0f);
	return true;
}
