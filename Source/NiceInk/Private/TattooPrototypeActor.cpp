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

	PrototypePalette = {
		FLinearColor::Black,
		FLinearColor(0.03f, 0.04f, 0.08f, 1.0f),
		FLinearColor(0.45f, 0.02f, 0.025f, 1.0f),
		FLinearColor(0.02f, 0.18f, 0.08f, 1.0f),
		FLinearColor(0.02f, 0.1f, 0.42f, 1.0f)
	};
}

void ATattooPrototypeActor::BeginPlay()
{
	Super::BeginPlay();

	if (TattooComponent)
	{
		TattooComponent->InitializeRenderTarget(TattooComponent->RenderTargetResolution);
		TattooComponent->bAutoDemoDrawing = bStartAutoDemoOnBeginPlay;
		SelectPaletteColor(SelectedPaletteIndex);
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

	if (!TattooComponent || !GetWorld() || !GetWorld()->IsPlayInEditor())
	{
		return;
	}

	APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
	if (bEnablePrototypeHotkeys)
	{
		HandlePrototypeHotkeys(PlayerController);
	}

	if (!bEnableMousePaintingInPIE)
	{
		return;
	}

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

bool ATattooPrototypeActor::SelectPaletteColor(int32 PaletteIndex)
{
	if (!PrototypePalette.IsValidIndex(PaletteIndex) || !TattooComponent)
	{
		return false;
	}

	SelectedPaletteIndex = PaletteIndex;
	TattooComponent->SelectColor(PrototypePalette[SelectedPaletteIndex]);
	return true;
}

FLinearColor ATattooPrototypeActor::GetSelectedPaletteColor() const
{
	return PrototypePalette.IsValidIndex(SelectedPaletteIndex) ? PrototypePalette[SelectedPaletteIndex] : FLinearColor::Black;
}

void ATattooPrototypeActor::HandlePrototypeHotkeys(APlayerController* PlayerController)
{
	if (!PlayerController || !TattooComponent)
	{
		return;
	}

	if (PlayerController->WasInputKeyJustPressed(EKeys::One))
	{
		TattooComponent->SelectMarker(ENiceInkMarkerType::FineMarker);
	}
	else if (PlayerController->WasInputKeyJustPressed(EKeys::Two))
	{
		TattooComponent->SelectMarker(ENiceInkMarkerType::ThickMarker);
	}
	else if (PlayerController->WasInputKeyJustPressed(EKeys::Three))
	{
		TattooComponent->SelectMarker(ENiceInkMarkerType::BrushTip);
	}

	if (PlayerController->WasInputKeyJustPressed(EKeys::Five))
	{
		SelectPaletteColor(0);
	}
	else if (PlayerController->WasInputKeyJustPressed(EKeys::Six))
	{
		SelectPaletteColor(1);
	}
	else if (PlayerController->WasInputKeyJustPressed(EKeys::Seven))
	{
		SelectPaletteColor(2);
	}
	else if (PlayerController->WasInputKeyJustPressed(EKeys::Eight))
	{
		SelectPaletteColor(3);
	}
	else if (PlayerController->WasInputKeyJustPressed(EKeys::Nine))
	{
		SelectPaletteColor(4);
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
