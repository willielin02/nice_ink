#include "NiceInkCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InkBodyComponent.h"
#include "InkCanvasComponent.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "NiceInkGameMode.h"
#include "NiceInkGameState.h"
#include "NiceInkPlayerState.h"
#include "NiceInkTypes.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// char17 網格：腳底在原點；PIE 實測 yaw +90 時看到後腦勺 → 正確為 -90（臉對齊角色前方 +X）
	const FVector BodyStandRelLoc(0.0f, 0.0f, -92.0f);
	const FRotator BodyStandRelRot(0.0f, -90.0f, 0.0f);
	// 仰躺大字（定案 #18）：臉朝上、頭朝 +X，pivot（腳底）偏移讓身體置中
	const FVector BodyLieRelLoc(-87.0f, 0.0f, -74.0f);
	const FRotator BodyLieRelRot(0.0f, -90.0f, -90.0f);

	constexpr float PointFlushInterval = 0.05f;
	constexpr int32 PointFlushMaxBatch = 10;
}

ANiceInkCharacter::ANiceInkCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bUseControllerRotationYaw = true;

	GetCapsuleComponent()->SetCapsuleSize(42.0f, 92.0f);
	// 準星描畫要打到身體網格，不是膠囊
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);

	GetCharacterMovement()->MaxWalkSpeed = 250.0f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2048.0f;

	Body = CreateDefaultSubobject<UInkBodyComponent>(TEXT("Body"));
	Body->SetupAttachment(GetCapsuleComponent());
	Body->SetRelativeLocation(BodyStandRelLoc);
	Body->SetRelativeRotation(BodyStandRelRot);
	Body->SetOwnerNoSee(true); // 第一人稱看不見自己身體的全貌（SPEC 視角規則）
	Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Body->SetCollisionResponseToAllChannels(ECR_Ignore);
	Body->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> BodyMeshAsset(TEXT("/Game/Characters/SM_Char17.SM_Char17"));
	if (BodyMeshAsset.Succeeded())
	{
		Body->SetStaticMesh(BodyMeshAsset.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BodyMaterialAsset(TEXT("/Game/Characters/M_InkBodyChar.M_InkBodyChar"));
	if (BodyMaterialAsset.Succeeded())
	{
		Body->BodyMaterial = BodyMaterialAsset.Object;
	}

	InkCanvas = CreateDefaultSubobject<UInkCanvasComponent>(TEXT("InkCanvas"));

	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
	FirstPersonCamera->SetRelativeLocation(FVector(0.0f, 0.0f, 62.0f)); // 174cm 的眼高
}

void ANiceInkCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (IsLocallyControlled())
	{
		if (APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			PC->bShowMouseCursor = false;
			PC->SetInputMode(FInputModeGameOnly());
		}
	}
}

void ANiceInkCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ANiceInkCharacter, bAsleep);
}

void ANiceInkCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	EnsureAvatarApplied();

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !IsLocallyControlled())
	{
		return;
	}

	PollLook(PC, DeltaSeconds);
	PollMove(PC);
	PollPalette(PC);
	PollPaint(PC, DeltaSeconds);
}

void ANiceInkCharacter::EnsureAvatarApplied()
{
	const ANiceInkPlayerState* PS = GetPlayerState<ANiceInkPlayerState>();
	if (!PS || PS->AvatarIndex == AppliedAvatarIndex)
	{
		return;
	}

	Body->ApplyAvatar(FNiceInkAvatars::Get(PS->AvatarIndex));
	Body->BindCanvas(InkCanvas);
	Body->SetEyesClosed(bAsleep);
	AppliedAvatarIndex = PS->AvatarIndex;
}

void ANiceInkCharacter::PollLook(APlayerController* PC, float DeltaSeconds)
{
	float MouseX = 0.0f;
	float MouseY = 0.0f;
	PC->GetInputMouseDelta(MouseX, MouseY);

	// 沉睡時滑鼠僅控制頭部視野（M2 接無聲甦醒的轉頭）；不轉身體
	if (!bAsleep)
	{
		FRotator Ctrl = PC->GetControlRotation();
		Ctrl.Yaw += MouseX * LookSensitivity;
		Ctrl.Pitch = 0.0f;
		Ctrl.Roll = 0.0f;
		PC->SetControlRotation(Ctrl);
	}

	CameraPitch = FMath::Clamp(CameraPitch + MouseY * LookSensitivity, -89.0f, 89.0f);
	FirstPersonCamera->SetRelativeRotation(FRotator(CameraPitch, 0.0f, 0.0f));
}

void ANiceInkCharacter::PollMove(APlayerController* PC)
{
	const bool bAnyMoveKey =
		PC->IsInputKeyDown(EKeys::W) || PC->IsInputKeyDown(EKeys::A) ||
		PC->IsInputKeyDown(EKeys::S) || PC->IsInputKeyDown(EKeys::D);

	if (bAsleep)
	{
		// 按 WASD＝請求現身（定案 #17）。M2 起由甦醒小遊戲第三次成功解鎖。
		if (bAnyMoveKey && !bEmergeRequested)
		{
			bEmergeRequested = true;
			ServerRequestEmerge();
		}
		return;
	}

	if (!bAnyMoveKey)
	{
		return;
	}

	const FRotator YawRot(0.0f, GetControlRotation().Yaw, 0.0f);
	const FVector Forward = FRotationMatrix(YawRot).GetUnitAxis(EAxis::X);
	const FVector Right = FRotationMatrix(YawRot).GetUnitAxis(EAxis::Y);

	if (PC->IsInputKeyDown(EKeys::W)) { AddMovementInput(Forward, 1.0f); }
	if (PC->IsInputKeyDown(EKeys::S)) { AddMovementInput(Forward, -1.0f); }
	if (PC->IsInputKeyDown(EKeys::D)) { AddMovementInput(Right, 1.0f); }
	if (PC->IsInputKeyDown(EKeys::A)) { AddMovementInput(Right, -1.0f); }
}

void ANiceInkCharacter::PollPalette(APlayerController* PC)
{
	static const FKey DigitKeys[10] = {
		EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
		EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine, EKeys::Zero
	};

	for (int32 Index = 0; Index < 10 && Index < FNiceInkPalette::Num(); ++Index)
	{
		if (PC->WasInputKeyJustPressed(DigitKeys[Index]))
		{
			SelectedColorIndex = Index;
			break;
		}
	}
}

void ANiceInkCharacter::PollPaint(APlayerController* PC, float DeltaSeconds)
{
	const bool bWantsPaint = !bAsleep && PC->IsInputKeyDown(EKeys::LeftMouseButton);
	if (!bWantsPaint)
	{
		StopPaintingLocal();
		return;
	}

	FVector2D UV = FVector2D::ZeroVector;
	ANiceInkCharacter* Target = TraceForBody(UV);
	if (!Target)
	{
		StopPaintingLocal();
		return;
	}

	if (!bPainting || PaintTarget.Get() != Target)
	{
		StopPaintingLocal();
		bPainting = true;
		PaintTarget = Target;
		PendingPoints.Reset();
		PointFlushTimer = 0.0f;
		ServerPaintBegin(Target, SelectedColorIndex, UV);
		return;
	}

	PendingPoints.Add(UV);
	PointFlushTimer += DeltaSeconds;
	if (PendingPoints.Num() >= PointFlushMaxBatch || PointFlushTimer >= PointFlushInterval)
	{
		ServerPaintPoints(PendingPoints);
		PendingPoints.Reset();
		PointFlushTimer = 0.0f;
	}
}

void ANiceInkCharacter::StopPaintingLocal()
{
	if (!bPainting)
	{
		return;
	}
	if (PendingPoints.Num() > 0)
	{
		ServerPaintPoints(PendingPoints);
		PendingPoints.Reset();
	}
	ServerPaintEnd();
	bPainting = false;
	PaintTarget = nullptr;
}

ANiceInkCharacter* ANiceInkCharacter::TraceForBody(FVector2D& OutUV) const
{
	UWorld* World = GetWorld();
	if (!World || !FirstPersonCamera)
	{
		return nullptr;
	}

	const FVector Start = FirstPersonCamera->GetComponentLocation();
	const FVector End = Start + FirstPersonCamera->GetForwardVector() * PaintReach;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(NiceInkPaintTrace), /*bInTraceComplex=*/true);
	QueryParams.AddIgnoredActor(this);

	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, QueryParams))
	{
		return nullptr;
	}

	ANiceInkCharacter* Target = Cast<ANiceInkCharacter>(Hit.GetActor());
	if (!Target || Target == this || !Target->Body)
	{
		return nullptr;
	}

	if (!Target->Body->ResolveBodyUV(Hit.ImpactPoint, OutUV))
	{
		return nullptr;
	}

	return Target;
}

FLinearColor ANiceInkCharacter::GetCurrentColor() const
{
	return FNiceInkPalette::Get(SelectedColorIndex);
}

int32 ANiceInkCharacter::GetInkAuthorId() const
{
	const APlayerState* PS = GetPlayerState();
	return PS ? PS->GetPlayerId() : INDEX_NONE;
}

// --- 伺服器端流程控制 ---

void ANiceInkCharacter::ServerSetAsleep(bool bNewAsleep, const FTransform& LieTransform)
{
	if (!HasAuthority() || bAsleep == bNewAsleep)
	{
		return;
	}

	bAsleep = bNewAsleep;

	if (bNewAsleep)
	{
		SeatTransform = GetActorTransform();
		SetActorTransform(LieTransform, false, nullptr, ETeleportType::TeleportPhysics);
		GetCharacterMovement()->StopMovementImmediately();
		GetCharacterMovement()->DisableMovement();
	}
	else
	{
		SetActorTransform(SeatTransform, false, nullptr, ETeleportType::TeleportPhysics);
		GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	}

	ApplySleepVisual();
}

void ANiceInkCharacter::OnRep_Asleep()
{
	ApplySleepVisual();
	if (bAsleep && IsLocallyControlled())
	{
		bEmergeRequested = false;
		StopPaintingLocal();
	}
}

void ANiceInkCharacter::ApplySleepVisual()
{
	if (!Body)
	{
		return;
	}

	Body->SetEyesClosed(bAsleep);
	Body->SetRelativeLocation(bAsleep ? BodyLieRelLoc : BodyStandRelLoc);
	Body->SetRelativeRotation(bAsleep ? BodyLieRelRot : BodyStandRelRot);

	// 沉睡時膠囊不再跟隨控制器 yaw——躺姿朝向由 LieTransform 決定，
	// 不受受害者入睡前的視角污染
	bUseControllerRotationYaw = !bAsleep;

	if (bAsleep && IsLocallyControlled())
	{
		bEmergeRequested = false;
	}
}

// --- 畫墨 RPC ---

void ANiceInkCharacter::ServerPaintBegin_Implementation(ANiceInkCharacter* Target, int32 ColorIndex, FVector2D UV)
{
	ANiceInkGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ANiceInkGameMode>() : nullptr;
	if (!GM || !Target || !GM->CanPaintOn(this, Target))
	{
		ServerPaintTarget = nullptr;
		return;
	}

	ServerPaintTarget = Target;
	Target->MulticastPaintBegin(GetInkAuthorId(), FNiceInkPalette::Get(ColorIndex), UV);
}

void ANiceInkCharacter::ServerPaintPoints_Implementation(const TArray<FVector2D>& UVs)
{
	ANiceInkCharacter* Target = ServerPaintTarget.Get();
	ANiceInkGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ANiceInkGameMode>() : nullptr;
	if (!Target || !GM || !GM->CanPaintOn(this, Target) || UVs.Num() == 0 || UVs.Num() > 64)
	{
		return;
	}

	Target->MulticastPaintPoints(GetInkAuthorId(), UVs);
}

void ANiceInkCharacter::ServerPaintEnd_Implementation()
{
	if (ANiceInkCharacter* Target = ServerPaintTarget.Get())
	{
		Target->MulticastPaintEnd(GetInkAuthorId());
	}
	ServerPaintTarget = nullptr;
}

// --- 畫墨重播 ---

void ANiceInkCharacter::MulticastPaintBegin_Implementation(int32 AuthorId, FLinearColor Color, FVector2D UV)
{
	if (InkCanvas)
	{
		InkCanvas->BeginStroke(AuthorId, Color, UV);
	}
}

void ANiceInkCharacter::MulticastPaintPoints_Implementation(int32 AuthorId, const TArray<FVector2D>& UVs)
{
	if (InkCanvas)
	{
		for (const FVector2D& UV : UVs)
		{
			InkCanvas->AddStrokePoint(AuthorId, UV);
		}
	}
}

void ANiceInkCharacter::MulticastPaintEnd_Implementation(int32 AuthorId)
{
	if (InkCanvas)
	{
		InkCanvas->EndStroke(AuthorId);
	}
}

// --- 規則操作重播 ---

void ANiceInkCharacter::MulticastConvertWorkToCarbon_Implementation(int32 WorkId)
{
	if (InkCanvas)
	{
		InkCanvas->ConvertWorkToCarbon(WorkId);
	}
}

void ANiceInkCharacter::MulticastWashAllMarker_Implementation()
{
	if (InkCanvas)
	{
		InkCanvas->WashAllMarker();
	}
}

void ANiceInkCharacter::MulticastLockWorkPermanent_Implementation(int32 WorkId)
{
	if (InkCanvas)
	{
		InkCanvas->LockWorkPermanent(WorkId);
	}
}

void ANiceInkCharacter::MulticastSetRoundIndex_Implementation(int32 NewRoundIndex)
{
	if (InkCanvas)
	{
		InkCanvas->SetRoundIndex(NewRoundIndex);
	}
}

// --- 受害者流程 ---

void ANiceInkCharacter::ServerRequestEmerge_Implementation()
{
	if (ANiceInkGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ANiceInkGameMode>() : nullptr)
	{
		GM->HandleEmergeRequest(this);
	}
}

void ANiceInkCharacter::ServerSubmitAccusation_Implementation(int32 WorkId, int32 AccusedPlayerId)
{
	if (ANiceInkGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ANiceInkGameMode>() : nullptr)
	{
		GM->HandleAccusation(this, WorkId, AccusedPlayerId);
	}
}

void ANiceInkCharacter::ServerRequestStartMatch_Implementation()
{
	if (ANiceInkGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ANiceInkGameMode>() : nullptr)
	{
		GM->RequestStartMatch();
	}
}

// --- 除錯 exec ---

void ANiceInkCharacter::NiStart()
{
	ServerRequestStartMatch();
}

void ANiceInkCharacter::NiEmerge()
{
	ServerRequestEmerge();
}

void ANiceInkCharacter::NiAccuse(int32 WorkNumber, int32 SeatIndex)
{
	UWorld* World = GetWorld();
	const ANiceInkGameState* GS = World ? World->GetGameState<ANiceInkGameState>() : nullptr;
	if (!GS)
	{
		return;
	}

	const ANiceInkCharacter* Victim = FindByPlayerId(World, GS->VictimPlayerId);
	if (!Victim || !Victim->InkCanvas)
	{
		return;
	}

	TArray<int32> WorkIds = Victim->InkCanvas->GetWorkIdsByState(EInkWorkState::Marker);
	WorkIds.Sort();
	if (!WorkIds.IsValidIndex(WorkNumber - 1))
	{
		return;
	}

	int32 AccusedPlayerId = INDEX_NONE;
	for (APlayerState* PS : GS->PlayerArray)
	{
		if (const ANiceInkPlayerState* NIPS = Cast<ANiceInkPlayerState>(PS))
		{
			if (NIPS->SeatIndex == SeatIndex)
			{
				AccusedPlayerId = NIPS->GetPlayerId();
				break;
			}
		}
	}

	if (AccusedPlayerId != INDEX_NONE)
	{
		ServerSubmitAccusation(WorkIds[WorkNumber - 1], AccusedPlayerId);
	}
}

ANiceInkCharacter* ANiceInkCharacter::FindByPlayerId(UWorld* World, int32 PlayerId)
{
	if (!World || PlayerId == INDEX_NONE)
	{
		return nullptr;
	}

	for (TActorIterator<ANiceInkCharacter> It(World); It; ++It)
	{
		if (const APlayerState* PS = It->GetPlayerState())
		{
			if (PS->GetPlayerId() == PlayerId)
			{
				return *It;
			}
		}
	}
	return nullptr;
}
