#include "NiceInkCharacter.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "InkBodyComponent.h"
#include "InkCanvasComponent.h"
#include "InkSprayProjectile.h"
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
	// char17 網格：腳底在原點、臉朝本地 +Y（PIE 實測）；yaw -90 → 臉對齊角色前方 +X
	const FVector BodyStandRelLoc(0.0f, 0.0f, -92.0f);
	const FRotator BodyStandRelRot(0.0f, -90.0f, 0.0f);
	// 仰躺大字（定案 #18）：PIE 實測 (pitch 0, yaw +90, roll -90) ＝臉朝上、頭朝 +X；
	// pivot（腳底）偏 -87 讓身體置中，z -60 貼地
	const FVector BodyLieRelLoc(-87.0f, 0.0f, -60.0f);
	const FRotator BodyLieRelRot(0.0f, 90.0f, -90.0f);

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
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Body->SetCollisionResponseToAllChannels(ECR_Ignore);
	Body->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	// 噴射投射物直接打在身體網格上（精準命中點→UV）；膠囊放行
	Body->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> BodyMeshAsset(TEXT("/Game/Characters/SM_Char17.SM_Char17"));
	if (BodyMeshAsset.Succeeded())
	{
		StandMesh = BodyMeshAsset.Object;
		Body->SetStaticMesh(StandMesh);
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
	DOREPLIFETIME(ANiceInkCharacter, bEyesOpen);
	DOREPLIFETIME(ANiceInkCharacter, MinigameHits);
	DOREPLIFETIME(ANiceInkCharacter, SprayCharges);
	DOREPLIFETIME(ANiceInkCharacter, KickCharges);
	DOREPLIFETIME(ANiceInkCharacter, bBlinded);
	DOREPLIFETIME(ANiceInkCharacter, BlindType);
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
	PollMinigame(PC);
	PollCounterplay(PC);
	PollAccusation(PC);
	PollPalette(PC);
	PollPaint(PC, DeltaSeconds);
	UpdateCinematicCamera(PC);
}

// --- 系統鏡頭（巡禮＝爆點：全員同一時段看同一幅） ---

void ANiceInkCharacter::UpdateCinematicCamera(APlayerController* PC)
{
	const ANiceInkGameState* GS = GetWorld() ? GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
	if (!GS)
	{
		return;
	}

	const bool bIsVictim = GetInkAuthorId() == GS->VictimPlayerId;
	int32 FocusWork = INDEX_NONE;
	bool bWide = false;

	switch (GS->CurrentPhase)
	{
	case ENiceInkPhase::Tour:
		FocusWork = GS->TourWorkId;
		break;
	case ENiceInkPhase::Resolution:
		FocusWork = GS->ResolutionWorkId;
		break;
	case ENiceInkPhase::Accusation:
		// 受害者：數字鍵預覽哪幅、鏡頭就聚焦哪幅；其他人看全景
		if (bIsVictim && GS->TourWorkIdList.IsValidIndex(AccusePickNumber - 1))
		{
			FocusWork = GS->TourWorkIdList[AccusePickNumber - 1];
		}
		else
		{
			bWide = true;
		}
		break;
	default:
		break;
	}

	if (FocusWork != INDEX_NONE)
	{
		ViewWork(PC, FocusWork);
	}
	else if (bWide)
	{
		ViewWide(PC);
	}
	else
	{
		RestoreView(PC);
	}
}

namespace
{
	// 桑拿房內部界限（實測 8.6×6.2×3m；含安全邊距）——鏡頭不出牆、不進天花板
	FVector ClampToRoom(const FVector& P)
	{
		return FVector(
			FMath::Clamp(P.X, -270.0f, 170.0f),
			FMath::Clamp(P.Y, -220.0f, 170.0f),
			FMath::Clamp(P.Z, 40.0f, 225.0f));
	}
}

ACameraActor* ANiceInkCharacter::GetOrSpawnCinematicCamera()
{
	if (!CinematicCamera && GetWorld())
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		CinematicCamera = GetWorld()->SpawnActor<ACameraActor>(FVector(0, 0, 300), FRotator::ZeroRotator, Params);
	}
	return CinematicCamera;
}

void ANiceInkCharacter::ViewWork(APlayerController* PC, int32 WorkId)
{
	if (bViewOverridden && !bWideViewActive && LastViewWorkId == WorkId)
	{
		return;
	}

	const ANiceInkGameState* GS = GetWorld()->GetGameState<ANiceInkGameState>();
	ANiceInkCharacter* Victim = FindByPlayerId(GetWorld(), GS->VictimPlayerId);
	if (!Victim || !Victim->InkCanvas || !Victim->Body)
	{
		return;
	}

	FInkWork Work;
	if (!Victim->InkCanvas->GetWork(WorkId, Work))
	{
		return;
	}

	// 傑作錨點＝可反解筆劃點的平均世界位置
	FVector Sum = FVector::ZeroVector;
	int32 Count = 0;
	for (const FInkStroke& Stroke : Work.Strokes)
	{
		for (int32 PtIdx = 0; PtIdx < Stroke.Points.Num() && Count < 24; PtIdx += FMath::Max(1, Stroke.Points.Num() / 4))
		{
			FVector WorldPos;
			if (Victim->Body->ResolveUVToWorld(Stroke.Points[PtIdx], WorldPos))
			{
				Sum += WorldPos;
				++Count;
			}
		}
	}

	const FVector BodyCenter = Victim->Body->GetComponentTransform().TransformPosition(FVector(0, 0, 88.0f));
	const FVector Anchor = Count > 0 ? Sum / Count : BodyCenter;

	FVector Outward = (Anchor - BodyCenter).GetSafeNormal2D();
	if (Outward.IsNearlyZero())
	{
		Outward = FVector(0, 1, 0);
	}
	const FVector CamPos = ClampToRoom(Anchor + Outward * 135.0f + FVector(0, 0, 45.0f));

	if (ACameraActor* Cam = GetOrSpawnCinematicCamera())
	{
		Cam->SetActorLocationAndRotation(CamPos, (Anchor - CamPos).Rotation());
		PC->SetViewTargetWithBlend(Cam, 0.45f, VTBlend_Cubic);
		bViewOverridden = true;
		bWideViewActive = false;
		LastViewWorkId = WorkId;
	}
}

void ANiceInkCharacter::ViewWide(APlayerController* PC)
{
	if (bViewOverridden && bWideViewActive)
	{
		return;
	}

	const ANiceInkGameState* GS = GetWorld()->GetGameState<ANiceInkGameState>();
	ANiceInkCharacter* Victim = FindByPlayerId(GetWorld(), GS->VictimPlayerId);
	if (!Victim || !Victim->Body)
	{
		return;
	}

	const FVector BodyCenter = Victim->Body->GetComponentTransform().TransformPosition(FVector(0, 0, 88.0f));
	const FVector CamPos = ClampToRoom(BodyCenter + FVector(-50.0f, -190.0f, 165.0f));

	if (ACameraActor* Cam = GetOrSpawnCinematicCamera())
	{
		Cam->SetActorLocationAndRotation(CamPos, (BodyCenter - CamPos).Rotation());
		PC->SetViewTargetWithBlend(Cam, 0.5f, VTBlend_Cubic);
		bViewOverridden = true;
		bWideViewActive = true;
		LastViewWorkId = INDEX_NONE;
	}
}

void ANiceInkCharacter::RestoreView(APlayerController* PC)
{
	if (!bViewOverridden)
	{
		return;
	}
	PC->SetViewTargetWithBlend(this, 0.35f, VTBlend_Cubic);
	bViewOverridden = false;
	bWideViewActive = false;
	LastViewWorkId = INDEX_NONE;
}

// --- 指認輸入（受害者；每回合恰好一次） ---

void ANiceInkCharacter::PollAccusation(APlayerController* PC)
{
	const ANiceInkGameState* GS = GetWorld() ? GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
	if (!GS || GS->CurrentPhase != ENiceInkPhase::Accusation || GetInkAuthorId() != GS->VictimPlayerId)
	{
		AccusePickNumber = 1;
		AccuseSuspectCursor = 0;
		return;
	}

	static const FKey DigitKeys[9] = {
		EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
		EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine
	};
	for (int32 Index = 0; Index < 9; ++Index)
	{
		if (PC->WasInputKeyJustPressed(DigitKeys[Index]) && GS->TourWorkIdList.IsValidIndex(Index))
		{
			AccusePickNumber = Index + 1;
			break;
		}
	}

	if (PC->WasInputKeyJustPressed(EKeys::Tab))
	{
		++AccuseSuspectCursor;
	}

	if (PC->WasInputKeyJustPressed(EKeys::Enter))
	{
		const APlayerState* Suspect = GetAccuseSuspect();
		if (Suspect && GS->TourWorkIdList.IsValidIndex(AccusePickNumber - 1))
		{
			ServerSubmitAccusation(GS->TourWorkIdList[AccusePickNumber - 1], Suspect->GetPlayerId());
		}
	}
}

APlayerState* ANiceInkCharacter::GetAccuseSuspect() const
{
	const ANiceInkGameState* GS = GetWorld() ? GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
	if (!GS)
	{
		return nullptr;
	}

	TArray<ANiceInkPlayerState*> Suspects;
	for (APlayerState* PS : GS->PlayerArray)
	{
		ANiceInkPlayerState* NIPS = Cast<ANiceInkPlayerState>(PS);
		if (NIPS && NIPS->GetPlayerId() != GS->VictimPlayerId)
		{
			Suspects.Add(NIPS);
		}
	}
	if (Suspects.IsEmpty())
	{
		return nullptr;
	}
	Suspects.Sort([](const ANiceInkPlayerState& A, const ANiceInkPlayerState& B) { return A.SeatIndex < B.SeatIndex; });
	return Suspects[AccuseSuspectCursor % Suspects.Num()];
}

void ANiceInkCharacter::PollCounterplay(APlayerController* PC)
{
	// 沉睡者限定：噴射／拳腳。瞄準＝頭部視野方向（聽聲推理、盲瞄）。
	if (!bAsleep)
	{
		return;
	}

	// 出發點選擇：1 鼻／2 陰部／3 肛門
	if (PC->WasInputKeyJustPressed(EKeys::One)) { SelectedSprayOrigin = EInkEvidenceType::Sneeze; }
	if (PC->WasInputKeyJustPressed(EKeys::Two)) { SelectedSprayOrigin = EInkEvidenceType::Piss; }
	if (PC->WasInputKeyJustPressed(EKeys::Three)) { SelectedSprayOrigin = EInkEvidenceType::Shit; }

	const float AimYawWorld = FirstPersonCamera->GetComponentRotation().Yaw;
	if (PC->WasInputKeyJustPressed(EKeys::Q) && SprayCharges > 0)
	{
		ServerSpray(SelectedSprayOrigin, AimYawWorld);
	}
	if (PC->WasInputKeyJustPressed(EKeys::E) && KickCharges > 0)
	{
		ServerKick(AimYawWorld);
	}
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

	if (bAsleep)
	{
		// 沉睡：滑鼠僅控制頭部視野（單純轉頭，SPEC 定案 #8）；身體不動
		SleepCameraYaw = FMath::Clamp(SleepCameraYaw + MouseX * LookSensitivity, -110.0f, 110.0f);
		CameraPitch = FMath::Clamp(CameraPitch + MouseY * LookSensitivity, -89.0f, 89.0f);
		FirstPersonCamera->SetRelativeRotation(FRotator(CameraPitch, SleepCameraYaw, 0.0f));
		return;
	}

	// 俯仰也走 control rotation：滑鼠改的是控制器姿態，
	// 相機每 tick 對齊 pitch（yaw 由 bUseControllerRotationYaw 轉動膠囊）
	FRotator Ctrl = PC->GetControlRotation();
	Ctrl.Yaw += MouseX * LookSensitivity;
	Ctrl.Pitch = FMath::ClampAngle(Ctrl.Pitch + MouseY * LookSensitivity, -89.0f, 89.0f);
	Ctrl.Roll = 0.0f;
	PC->SetControlRotation(Ctrl);

	CameraPitch = Ctrl.Pitch;
	FirstPersonCamera->SetRelativeRotation(FRotator(CameraPitch, 0.0f, 0.0f));
}

void ANiceInkCharacter::PollMove(APlayerController* PC)
{
	const bool bAnyMoveKey =
		PC->IsInputKeyDown(EKeys::W) || PC->IsInputKeyDown(EKeys::A) ||
		PC->IsInputKeyDown(EKeys::S) || PC->IsInputKeyDown(EKeys::D);

	if (bAsleep)
	{
		// 按 WASD＝請求現身（定案 #17）；只有睜眼後才有意義（server 亦驗證）
		if (bAnyMoveKey && bEyesOpen && !bEmergeRequested)
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

float ANiceInkCharacter::MinigameIndicatorPos(float ServerTime, float Period)
{
	// 三角波往復：0 → 1 → 0，一趟 Period 秒
	const float Cycle = FMath::Fmod(ServerTime, Period * 2.0f) / Period; // 0..2
	return Cycle <= 1.0f ? Cycle : 2.0f - Cycle;
}

void ANiceInkCharacter::PollMinigame(APlayerController* PC)
{
	// 只有沉睡且尚未睜眼的受害者在玩小遊戲
	if (!bAsleep || bEyesOpen || MinigameHits >= 3)
	{
		return;
	}
	if (!PC->WasInputKeyJustPressed(EKeys::SpaceBar))
	{
		return;
	}

	const ANiceInkGameState* GS = GetWorld() ? GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
	if (!GS)
	{
		return;
	}

	const float Now = GS->GetServerWorldTimeSeconds();
	if (Now < MinigameCooldownUntil)
	{
		return; // 冷卻中按下無效（不重置冷卻）
	}

	const float Pos = MinigameIndicatorPos(Now, GS->MinigamePeriod);
	const float HalfZone = GS->MinigameZoneWidth * 0.5f;
	if (FMath::Abs(Pos - 0.5f) <= HalfZone)
	{
		ServerMinigameHit();
	}
	else
	{
		// 失手：十秒冷卻（只鎖按鍵，不清成功數——SPEC 定案 #5）
		MinigameCooldownUntil = Now + GS->MinigameMissCooldown;
	}
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
		MinigameHits = 0;
		bEyesOpen = false;
		SprayCharges = 0;
		KickCharges = 0;
		SeatTransform = GetActorTransform();
		SetActorTransform(LieTransform, false, nullptr, ETeleportType::TeleportPhysics);
		GetCharacterMovement()->StopMovementImmediately();
		GetCharacterMovement()->DisableMovement();
	}
	else
	{
		// 睜眼即過期：未用完的噴射／拳腳作廢（SPEC 定案 #6/#7）
		SprayCharges = 0;
		KickCharges = 0;
		SetActorTransform(SeatTransform, false, nullptr, ETeleportType::TeleportPhysics);
		GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	}

	ApplySleepVisual();
}

void ANiceInkCharacter::ServerMinigameHit_Implementation()
{
	const ANiceInkGameState* GS = GetWorld() ? GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
	const APlayerState* PS = GetPlayerState();
	if (!GS || !PS || GS->CurrentPhase != ENiceInkPhase::Drawing ||
		PS->GetPlayerId() != GS->VictimPlayerId || !bAsleep || bEyesOpen || MinigameHits >= 3)
	{
		return;
	}

	++MinigameHits;
	switch (MinigameHits)
	{
	case 1: ++SprayCharges; break;   // 第 1 次＝噴射 ×1
	case 2: ++KickCharges; break;    // 第 2 次＝拳腳 ×1
	case 3:
		bEyesOpen = true;            // 第 3 次＝無聲甦醒：睜眼、零提示
		ApplySleepVisual();
		break;
	default: break;
	}
}

void ANiceInkCharacter::ServerSpray_Implementation(EInkEvidenceType Origin, float AimYawWorld)
{
	const ANiceInkGameState* GS = GetWorld() ? GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
	const APlayerState* PS = GetPlayerState();
	if (!GS || !PS || GS->CurrentPhase != ENiceInkPhase::Drawing ||
		PS->GetPlayerId() != GS->VictimPlayerId || !bAsleep || bEyesOpen ||
		SprayCharges <= 0 || Origin == EInkEvidenceType::Bruise)
	{
		return;
	}
	--SprayCharges;

	// 出發點（身體本地座標；躺姿下由元件變換帶到世界）：鼻／陰部／肛門
	FVector LocalOrigin;
	switch (Origin)
	{
	case EInkEvidenceType::Sneeze: LocalOrigin = FVector(0.0f, 20.0f, 158.0f); break;
	case EInkEvidenceType::Piss:   LocalOrigin = FVector(0.0f, 14.0f, 88.0f); break;
	default:                       LocalOrigin = FVector(0.0f, -16.0f, 88.0f); break;
	}
	const FVector WorldOrigin = Body->GetComponentTransform().TransformPosition(LocalOrigin) + FVector(0, 0, 6.0f);

	// 拋物線：自選 yaw、固定仰角
	const FRotator AimRot(38.0f, AimYawWorld, 0.0f);

	FActorSpawnParameters Params;
	Params.Instigator = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AInkSprayProjectile* Proj = GetWorld()->SpawnActor<AInkSprayProjectile>(
		AInkSprayProjectile::StaticClass(), WorldOrigin, AimRot, Params))
	{
		Proj->SprayType = Origin;
		Proj->Movement->Velocity = AimRot.Vector() * Proj->Movement->InitialSpeed;
	}
}

void ANiceInkCharacter::ServerKick_Implementation(float AimYawWorld)
{
	const ANiceInkGameState* GS = GetWorld() ? GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
	const APlayerState* PS = GetPlayerState();
	if (!GS || !PS || GS->CurrentPhase != ENiceInkPhase::Drawing ||
		PS->GetPlayerId() != GS->VictimPlayerId || !bAsleep || bEyesOpen || KickCharges <= 0)
	{
		return;
	}
	--KickCharges;

	// 從身體中心朝瞄準方向掃掠 170cm
	const FVector Start = Body->GetComponentTransform().TransformPosition(FVector(0.0f, 0.0f, 88.0f));
	const FVector Dir = FRotator(0.0f, AimYawWorld, 0.0f).Vector();
	const FVector End = Start + Dir * 170.0f;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(NiceInkKick), false);
	Params.AddIgnoredActor(this);
	// 只掃 Pawn 物件（地板／長凳不會擋掉這一腳）
	FHitResult Hit;
	const bool bHit = GetWorld()->SweepSingleByObjectType(Hit, Start, End, FQuat::Identity,
		FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(45.0f), Params);
	if (!bHit)
	{
		return;
	}

	ANiceInkCharacter* Target = Cast<ANiceInkCharacter>(Hit.GetActor());
	if (!Target || Target == this)
	{
		return;
	}

	// 瘀青標記（命中部位）＋彈飛。無命中回饋給沉睡者。
	// 膠囊命中點離網格有段距離——容差放寬
	FVector2D UV;
	if (Target->Body && Target->Body->ResolveBodyUV(Hit.ImpactPoint, UV, 45.0f))
	{
		Target->MulticastAddEvidence(EInkEvidenceType::Bruise, UV, FMath::Rand());
	}
	Target->LaunchCharacter(Dir * 900.0f + FVector(0, 0, 380.0f), true, true);
}

void ANiceInkCharacter::MulticastAddEvidence_Implementation(EInkEvidenceType Type, FVector2D UV, int32 Seed)
{
	if (InkCanvas)
	{
		InkCanvas->AddEvidenceMark(Type, UV, Seed);
	}
}

void ANiceInkCharacter::ServerApplyBlind(EInkEvidenceType Type)
{
	if (!HasAuthority())
	{
		return;
	}
	bBlinded = true;
	BlindType = Type;
	OnRep_Blinded();
}

void ANiceInkCharacter::MulticastRoundCleanup_Implementation()
{
	if (InkCanvas)
	{
		InkCanvas->WashAllMarker();
	}
	if (HasAuthority())
	{
		bBlinded = false;
		OnRep_Blinded();
	}
}

void ANiceInkCharacter::OnRep_Blinded()
{
	// HUD 直接讀 bBlinded 畫致盲遮罩；這裡不需額外處理（保留鉤子）
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

void ANiceInkCharacter::OnRep_EyesOpen()
{
	// 睜眼貼圖切換＝其他玩家能觀察到的唯一破綻（不播音效、不通知）
	ApplySleepVisual();
}

void ANiceInkCharacter::ApplySleepVisual()
{
	if (!Body)
	{
		return;
	}

	Body->SetEyesClosed(bAsleep && !bEyesOpen);

	// 睡姿網格惰性載入（資產尚未匯入時退回站姿網格翻轉）
	if (bAsleep && !SleepMesh)
	{
		SleepMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Characters/SM_Char17_Sleep.SM_Char17_Sleep"));
	}
	if (UStaticMesh* WantedMesh = bAsleep && SleepMesh ? SleepMesh.Get() : StandMesh.Get())
	{
		Body->SwapBodyMesh(WantedMesh);
	}

	Body->SetRelativeLocation(bAsleep ? BodyLieRelLoc : BodyStandRelLoc);
	Body->SetRelativeRotation(bAsleep ? BodyLieRelRot : BodyStandRelRot);

	// 沉睡時膠囊不再跟隨控制器 yaw——躺姿朝向由 LieTransform 決定，
	// 不受受害者入睡前的視角污染
	bUseControllerRotationYaw = !bAsleep;

	if (IsLocallyControlled())
	{
		if (bAsleep)
		{
			bEmergeRequested = false;
			// 相機移到躺姿頭部、預設仰望天花板；睜眼後就是實景視野
			SleepCameraYaw = 0.0f;
			CameraPitch = 55.0f;
			FirstPersonCamera->SetRelativeLocation(BodyLieRelLoc + FVector(172.0f, 0.0f, 22.0f));
			FirstPersonCamera->SetRelativeRotation(FRotator(CameraPitch, 0.0f, 0.0f));
		}
		else
		{
			SleepCameraYaw = 0.0f;
			CameraPitch = 0.0f;
			FirstPersonCamera->SetRelativeLocation(FVector(0.0f, 0.0f, 62.0f));
			FirstPersonCamera->SetRelativeRotation(FRotator::ZeroRotator);
		}
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
