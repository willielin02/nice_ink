#include "InkTestPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "InkBodyActor.h"
#include "InkCanvasComponent.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"

AInkTestPawn::AInkTestPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	AutoPossessPlayer = EAutoReceiveInput::Player0;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);

	// 小畫家式基本盤：黑、白、紅、橙、黃、綠、藍、紫、粉、棕
	Palette = {
		FLinearColor(0.02f, 0.02f, 0.02f),
		FLinearColor(0.95f, 0.95f, 0.95f),
		FLinearColor(0.78f, 0.05f, 0.05f),
		FLinearColor(0.9f, 0.4f, 0.05f),
		FLinearColor(0.92f, 0.85f, 0.05f),
		FLinearColor(0.06f, 0.55f, 0.1f),
		FLinearColor(0.05f, 0.2f, 0.8f),
		FLinearColor(0.4f, 0.08f, 0.65f),
		FLinearColor(0.95f, 0.4f, 0.65f),
		FLinearColor(0.4f, 0.22f, 0.08f)
	};
}

void AInkTestPawn::BeginPlay()
{
	Super::BeginPlay();

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->bShowMouseCursor = false;
		PC->SetInputMode(FInputModeGameOnly());
	}
}

void AInkTestPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		return;
	}

	PollMovement(PC, DeltaSeconds);
	PollPalette(PC);
	PollDebugOps(PC);
	PollPainting(PC);
}

void AInkTestPawn::PollMovement(APlayerController* PC, float DeltaSeconds)
{
	float MouseX = 0.0f;
	float MouseY = 0.0f;
	PC->GetInputMouseDelta(MouseX, MouseY);

	FRotator ActorRotation = GetActorRotation();
	ActorRotation.Yaw += MouseX * LookSensitivity;
	ActorRotation.Pitch = 0.0f;
	ActorRotation.Roll = 0.0f;
	SetActorRotation(ActorRotation);

	CameraPitch = FMath::Clamp(CameraPitch + MouseY * LookSensitivity, -89.0f, 89.0f);
	Camera->SetRelativeRotation(FRotator(CameraPitch, 0.0f, 0.0f));

	FVector MoveInput = FVector::ZeroVector;
	if (PC->IsInputKeyDown(EKeys::W)) { MoveInput += FVector::ForwardVector; }
	if (PC->IsInputKeyDown(EKeys::S)) { MoveInput -= FVector::ForwardVector; }
	if (PC->IsInputKeyDown(EKeys::D)) { MoveInput += FVector::RightVector; }
	if (PC->IsInputKeyDown(EKeys::A)) { MoveInput -= FVector::RightVector; }
	if (PC->IsInputKeyDown(EKeys::E)) { MoveInput += FVector::UpVector; }
	if (PC->IsInputKeyDown(EKeys::Q)) { MoveInput -= FVector::UpVector; }

	if (!MoveInput.IsNearlyZero())
	{
		const float Speed = PC->IsInputKeyDown(EKeys::LeftShift) ? MoveSpeed * 3.0f : MoveSpeed;
		const FVector CameraSpaceInput =
			Camera->GetForwardVector() * MoveInput.X +
			Camera->GetRightVector() * MoveInput.Y +
			FVector::UpVector * MoveInput.Z;
		AddActorWorldOffset(CameraSpaceInput.GetSafeNormal() * Speed * DeltaSeconds, true);
	}
}

void AInkTestPawn::PollPalette(APlayerController* PC)
{
	static const FKey DigitKeys[10] = {
		EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
		EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine, EKeys::Zero
	};

	for (int32 Index = 0; Index < 10; ++Index)
	{
		if (PC->WasInputKeyJustPressed(DigitKeys[Index]) && Palette.IsValidIndex(Index))
		{
			SelectedColorIndex = Index;
			break;
		}
	}
}

void AInkTestPawn::PollDebugOps(APlayerController* PC)
{
	AInkBodyActor* Body = FindAnyBodyActor();
	UInkCanvasComponent* Canvas = Body ? Body->InkCanvas.Get() : nullptr;
	if (!Canvas)
	{
		return;
	}

	if (PC->WasInputKeyJustPressed(EKeys::X))
	{
		Canvas->WashAllMarker();
	}
	else if (PC->WasInputKeyJustPressed(EKeys::C))
	{
		const int32 MyWorkId = Canvas->GetActiveWorkId(TestAuthorId);
		if (MyWorkId != INDEX_NONE)
		{
			Canvas->ConvertWorkToCarbon(MyWorkId);
		}
	}
	else if (PC->WasInputKeyJustPressed(EKeys::L))
	{
		const TArray<int32> CarbonWorks = Canvas->GetWorkIdsByState(EInkWorkState::Carbon);
		if (CarbonWorks.Num() > 0)
		{
			Canvas->ApplyLaserToWork(CarbonWorks[0]);
		}
	}
	else if (PC->WasInputKeyJustPressed(EKeys::P))
	{
		const TArray<int32> CarbonWorks = Canvas->GetWorkIdsByState(EInkWorkState::Carbon);
		if (CarbonWorks.Num() > 0)
		{
			Canvas->LockWorkPermanent(CarbonWorks[0]);
		}
	}
	else if (PC->WasInputKeyJustPressed(EKeys::R))
	{
		Canvas->SetRoundIndex(Canvas->GetRoundIndex() + 1);
	}
	else if (PC->WasInputKeyJustPressed(EKeys::F10))
	{
		const FString Prefix = FPaths::ProjectSavedDir() / TEXT("InkQA") / FString::Printf(TEXT("ink_%.0f"), FPlatformTime::Seconds());
		Canvas->ExportLayersToPng(FPaths::ConvertRelativePathToFull(Prefix));
	}
}

void AInkTestPawn::PollPainting(APlayerController* PC)
{
	const bool bWantsPaint = PC->IsInputKeyDown(EKeys::LeftMouseButton);
	if (!bWantsPaint)
	{
		StopPainting();
		return;
	}

	FVector2D UV = FVector2D::ZeroVector;
	UInkCanvasComponent* Canvas = TraceForCanvas(UV);
	if (!Canvas)
	{
		StopPainting();
		return;
	}

	if (!bPainting || ActiveCanvas.Get() != Canvas)
	{
		StopPainting();
		Canvas->BeginStroke(TestAuthorId, GetCurrentColor(), UV);
		ActiveCanvas = Canvas;
		bPainting = true;
	}
	else
	{
		Canvas->AddStrokePoint(TestAuthorId, UV);
	}
}

UInkCanvasComponent* AInkTestPawn::TraceForCanvas(FVector2D& OutUV) const
{
	UWorld* World = GetWorld();
	if (!World || !Camera)
	{
		return nullptr;
	}

	const FVector Start = Camera->GetComponentLocation();
	const FVector End = Start + Camera->GetForwardVector() * PaintReach;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(InkPaintTrace), /*bInTraceComplex=*/true);
	QueryParams.bReturnFaceIndex = true; // FindCollisionUV 必需
	QueryParams.AddIgnoredActor(this);

	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, QueryParams))
	{
		return nullptr;
	}

	AInkBodyActor* Body = Cast<AInkBodyActor>(Hit.GetActor());
	if (!Body || !Body->InkCanvas)
	{
		return nullptr;
	}

	if (!Body->ResolveBodyUV(Hit.ImpactPoint, OutUV))
	{
		return nullptr;
	}

	return Body->InkCanvas.Get();
}

void AInkTestPawn::StopPainting()
{
	if (bPainting)
	{
		if (UInkCanvasComponent* Canvas = ActiveCanvas.Get())
		{
			Canvas->EndStroke(TestAuthorId);
		}
		ActiveCanvas = nullptr;
		bPainting = false;
	}
}

AInkBodyActor* AInkTestPawn::FindAnyBodyActor() const
{
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AInkBodyActor> It(World); It; ++It)
		{
			return *It;
		}
	}
	return nullptr;
}

FLinearColor AInkTestPawn::GetCurrentColor() const
{
	return Palette.IsValidIndex(SelectedColorIndex) ? Palette[SelectedColorIndex] : FLinearColor::Black;
}
