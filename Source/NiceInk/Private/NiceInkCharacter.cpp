#include "NiceInkCharacter.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PoseableMeshComponent.h"
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
#include "NiceInkSessionSubsystem.h"
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

	// 貼臉鎖定參數
	constexpr float LeanEnterMaxDistance = 300.0f;  // 起手距離（湊上去的觸發範圍）
	constexpr float LeanStandoff = 55.0f;           // 鎖定時腳站在落筆點外多遠
	constexpr float LeanCameraHeight = 38.0f;       // 鎖定鏡頭離皮膚
	constexpr float LeanPaintDelay = 0.25f;         // 鏡頭到位前不落筆
	constexpr float LeanCursorSpeed = 1.0f;         // 游標像素/滑鼠單位

	// 桑拿房內部界限（實測 8.6×6.2×3m；含安全邊距）——鏡頭不出牆、不進天花板
	FVector ClampToRoom(const FVector& P)
	{
		return FVector(
			FMath::Clamp(P.X, -270.0f, 170.0f),
			FMath::Clamp(P.Y, -220.0f, 170.0f),
			FMath::Clamp(P.Z, 40.0f, 225.0f));
	}
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

	// 彎腰用可擺骨身體：鎖定時亮、平時藏。只擋噴射（PhysicsBody），
	// 不擋 Visibility——別人的游標射線要穿過你打到受害者皮膚。
	BowBody = CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("BowBody"));
	BowBody->SetupAttachment(GetCapsuleComponent());
	BowBody->SetRelativeLocation(BodyStandRelLoc);
	BowBody->SetRelativeRotation(BodyStandRelRot);
	BowBody->SetOwnerNoSee(true);
	BowBody->SetVisibility(false);
	BowBody->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	BowBody->SetCollisionResponseToAllChannels(ECR_Ignore);
	BowBody->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);

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
	DOREPLIFETIME(ANiceInkCharacter, bLeanLocked);
	DOREPLIFETIME(ANiceInkCharacter, LeanPoint);
	DOREPLIFETIME(ANiceInkCharacter, LeanNormal);
	DOREPLIFETIME(ANiceInkCharacter, LeanTarget);
	DOREPLIFETIME(ANiceInkCharacter, bPeeking);
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
	PollLobby(PC);
	PollPalette(PC);
	PollLeanEnter(PC);
	PollLockedDraw(PC, DeltaSeconds);
	UpdateCinematicCamera(PC);
}

void ANiceInkCharacter::PollLobby(APlayerController* PC)
{
	const ANiceInkGameState* GS = GetWorld() ? GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
	if (!GS || GS->CurrentPhase != ENiceInkPhase::PostGame)
	{
		return;
	}
	if (PC->WasInputKeyJustPressed(EKeys::L))
	{
		ServerRequestLaser();
	}
}

// --- 系統鏡頭（巡禮＝爆點：全員同一時段看同一幅） ---

void ANiceInkCharacter::UpdateCinematicCamera(APlayerController* PC)
{
	const ANiceInkGameState* GS = GetWorld() ? GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
	if (!GS)
	{
		return;
	}

	// 貼臉鎖定中：鎖定畫布／偷瞄鏡頭優先
	if (bLeanLocked && (GS->CurrentPhase == ENiceInkPhase::Drawing || GS->CurrentPhase == ENiceInkPhase::Finale))
	{
		UpdateLeanCamera(PC);
		return;
	}
	bLeanCamActive = false;
	bPeekCamApplied = false;

	const bool bIsVictim = GetInkAuthorId() == GS->VictimPlayerId;
	int32 FocusWork = INDEX_NONE;
	bool bWide = false;
	bool bThirdPerson = false;

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
	case ENiceInkPhase::BottleSpin:
		bWide = true; // 轉瓶儀式全景
		break;
	case ENiceInkPhase::PostGame:
		bThirdPerson = true; // 場間大廳：第三人稱端詳自己的刺青（SPEC 視角規則）
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
	else if (bThirdPerson)
	{
		ViewSelfThirdPerson(PC);
	}
	else
	{
		RestoreView(PC);
	}
}

void ANiceInkCharacter::ViewSelfThirdPerson(APlayerController* PC)
{
	// 跟隨式第三人稱：轉身（A/D＋滑鼠）就能看到身體各面
	const FVector Fwd = GetActorForwardVector();
	const FVector CamPos = ClampToRoom(GetActorLocation() - Fwd * 190.0f + FVector(0, 0, 70.0f));
	if (ACameraActor* Cam = GetOrSpawnCinematicCamera())
	{
		Cam->SetActorLocationAndRotation(CamPos, ((GetActorLocation() + FVector(0, 0, 10.0f)) - CamPos).Rotation());
		if (!bThirdPersonActive)
		{
			PC->SetViewTargetWithBlend(Cam, 0.4f, VTBlend_Cubic);
			bViewOverridden = true;
			bThirdPersonActive = true;
			bWideViewActive = false;
			LastViewWorkId = INDEX_NONE;
		}
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
		bThirdPersonActive = false;
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

	// 沒有受害者（轉瓶儀式）就看房間舞台中心
	const FVector BodyCenter = (Victim && Victim->Body)
		? Victim->Body->GetComponentTransform().TransformPosition(FVector(0, 0, 88.0f))
		: FVector(0.0f, 75.0f, 60.0f);
	const FVector CamPos = ClampToRoom(BodyCenter + FVector(-50.0f, -190.0f, 165.0f));

	if (ACameraActor* Cam = GetOrSpawnCinematicCamera())
	{
		Cam->SetActorLocationAndRotation(CamPos, (BodyCenter - CamPos).Rotation());
		PC->SetViewTargetWithBlend(Cam, 0.5f, VTBlend_Cubic);
		bViewOverridden = true;
		bWideViewActive = true;
		bThirdPersonActive = false;
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
	bThirdPersonActive = false;
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
	if (bLeanLocked)
	{
		return; // 鎖定中滑鼠＝麥克筆游標（PollLockedDraw），不轉視角
	}

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
	if (bLeanLocked)
	{
		return; // WASD 在鎖定中＝起身（PollLockedDraw 處理）
	}

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

// --- 貼臉鎖定：湊上去 ---

void ANiceInkCharacter::PollLeanEnter(APlayerController* PC)
{
	if (bLeanLocked || bAsleep || !PC->WasInputKeyJustPressed(EKeys::RightMouseButton))
	{
		return;
	}

	// 準星指著想畫的地方按右鍵＝湊上去
	UWorld* World = GetWorld();
	if (!World || !FirstPersonCamera)
	{
		return;
	}

	const FVector Start = FirstPersonCamera->GetComponentLocation();
	const FVector End = Start + FirstPersonCamera->GetForwardVector() * LeanEnterMaxDistance;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(NiceInkLeanTrace), /*bInTraceComplex=*/true);
	QueryParams.AddIgnoredActor(this);

	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, QueryParams))
	{
		return;
	}

	ANiceInkCharacter* Target = Cast<ANiceInkCharacter>(Hit.GetActor());
	if (!Target || Target == this || !Target->Body)
	{
		return;
	}

	FVector2D UnusedUV;
	if (!Target->Body->ResolveBodyUV(Hit.ImpactPoint, UnusedUV))
	{
		return; // 不在皮膚上
	}

	ServerEnterLean(Target, Hit.ImpactPoint, Hit.ImpactNormal);
}

// --- 貼臉鎖定：鎖定中的游標作畫／偷瞄／起身 ---

void ANiceInkCharacter::PollLockedDraw(APlayerController* PC, float DeltaSeconds)
{
	if (!bLeanLocked)
	{
		StopPaintingLocal();
		return;
	}

	// 起身：右鍵或 WASD
	const bool bWantsExit = PC->WasInputKeyJustPressed(EKeys::RightMouseButton) ||
		PC->IsInputKeyDown(EKeys::W) || PC->IsInputKeyDown(EKeys::A) ||
		PC->IsInputKeyDown(EKeys::S) || PC->IsInputKeyDown(EKeys::D);
	if (bWantsExit)
	{
		StopPaintingLocal();
		ServerExitLean();
		return;
	}

	// 偷瞄：按住 Shift，鬆開彈回（頭頸硬轉，全房可見）
	const bool bWantsPeek = PC->IsInputKeyDown(EKeys::LeftShift);
	if (bWantsPeek != bPeeking)
	{
		StopPaintingLocal();
		ServerSetPeeking(bWantsPeek);
		// 本地即時反應（複寫回來會再套一次，冪等）
		bPeeking = bWantsPeek;
		OnRep_Peeking();
	}

	// 虛擬麥克筆游標：滑鼠位移驅動、夾在視窗內
	float MouseX = 0.0f;
	float MouseY = 0.0f;
	PC->GetInputMouseDelta(MouseX, MouseY);
	int32 ViewX = 0;
	int32 ViewY = 0;
	PC->GetViewportSize(ViewX, ViewY);
	LeanCursorPx.X = FMath::Clamp(LeanCursorPx.X + MouseX * LeanCursorSpeed * 6.0f, 0.0f, static_cast<float>(ViewX));
	LeanCursorPx.Y = FMath::Clamp(LeanCursorPx.Y - MouseY * LeanCursorSpeed * 6.0f, 0.0f, static_cast<float>(ViewY));

	// 偷瞄中或鏡頭未到位不落筆
	const float Now = GetWorld()->GetTimeSeconds();
	const bool bCanPaint = !bPeeking && (Now - LeanLockTime) > LeanPaintDelay;
	const bool bWantsPaint = bCanPaint && PC->IsInputKeyDown(EKeys::LeftMouseButton);
	if (!bWantsPaint)
	{
		StopPaintingLocal();
		return;
	}

	// 游標 → 射線 → 受害者皮膚 UV
	ANiceInkCharacter* Target = LeanTarget.Get();
	if (!Target || !Target->Body)
	{
		StopPaintingLocal();
		return;
	}

	FVector RayOrigin, RayDir;
	if (!PC->DeprojectScreenPositionToWorld(LeanCursorPx.X, LeanCursorPx.Y, RayOrigin, RayDir))
	{
		StopPaintingLocal();
		return;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(NiceInkCursorTrace), /*bInTraceComplex=*/true);
	QueryParams.AddIgnoredActor(this);

	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, RayOrigin, RayOrigin + RayDir * 220.0f, ECC_Visibility, QueryParams) ||
		Hit.GetActor() != Target)
	{
		StopPaintingLocal();
		return;
	}

	FVector2D UV;
	if (!Target->Body->ResolveBodyUV(Hit.ImpactPoint, UV))
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
	// 膠囊命中點離網格有段距離——容差放寬；彎腰中被踹＝從跪伏裡被打飛（強制起身）
	FVector2D UV;
	if (Target->Body && Target->Body->ResolveBodyUV(Hit.ImpactPoint, UV, 45.0f))
	{
		Target->MulticastAddEvidence(EInkEvidenceType::Bruise, UV, FMath::Rand());
	}
	Target->ForceExitLean();
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

// --- 場間大廳 ---

void ANiceInkCharacter::ServerRequestLaser_Implementation()
{
	ANiceInkGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ANiceInkGameMode>() : nullptr;
	if (GM)
	{
		GM->HandleLaserRequest(this);
	}
}

void ANiceInkCharacter::MulticastApplyLaser_Implementation(int32 WorkId)
{
	if (InkCanvas)
	{
		InkCanvas->ApplyLaserToWork(WorkId);
	}
}

void ANiceInkCharacter::MulticastRestoreWork_Implementation(FInkWork Work)
{
	if (InkCanvas)
	{
		InkCanvas->RestoreWork(Work);
	}
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

// --- 貼臉鎖定 ---

void ANiceInkCharacter::ServerEnterLean_Implementation(ANiceInkCharacter* Target, FVector_NetQuantize Point, FVector_NetQuantizeNormal Normal)
{
	ANiceInkGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ANiceInkGameMode>() : nullptr;
	if (!GM || !Target || bLeanLocked || bAsleep || !GM->CanPaintOn(this, Target))
	{
		return;
	}
	if (FVector::Dist(GetActorLocation(), Point) > LeanEnterMaxDistance + 120.0f)
	{
		return;
	}

	// 擠位錯開：與已鎖定的別人距離太近就沿切線推開一點（擠但不穿模）
	FVector AdjustedPoint = Point;
	for (TActorIterator<ANiceInkCharacter> It(GetWorld()); It; ++It)
	{
		if (*It != this && It->bLeanLocked && FVector::Dist(It->LeanPoint, AdjustedPoint) < 30.0f)
		{
			const FVector Away = (AdjustedPoint - It->LeanPoint).GetSafeNormal2D();
			AdjustedPoint += (Away.IsNearlyZero() ? FVector(0, 1, 0) : Away) * 30.0f;
		}
	}

	// 站位：沿目前接近方向退到 standoff，腳保持地面高度，面向落筆點
	const FVector Approach = (GetActorLocation() - AdjustedPoint).GetSafeNormal2D();
	FVector StandLoc = AdjustedPoint + (Approach.IsNearlyZero() ? FVector(0, -1, 0) : Approach) * LeanStandoff;
	StandLoc.Z = GetActorLocation().Z;
	const float FaceYaw = (-Approach).Rotation().Yaw;

	bLeanLocked = true;
	LeanPoint = AdjustedPoint;
	LeanNormal = Normal;
	LeanTarget = Target;
	bPeeking = false;

	SetActorLocation(StandLoc, false, nullptr, ETeleportType::TeleportPhysics);
	SetActorRotation(FRotator(0.0f, FaceYaw, 0.0f));
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();

	OnRep_Lean();
}

void ANiceInkCharacter::ServerExitLean_Implementation()
{
	ForceExitLean();
}

void ANiceInkCharacter::ForceExitLean()
{
	if (!HasAuthority() || !bLeanLocked)
	{
		return;
	}
	bLeanLocked = false;
	bPeeking = false;
	LeanTarget = nullptr;
	if (!bAsleep)
	{
		GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	}
	OnRep_Lean();
}

void ANiceInkCharacter::ServerSetPeeking_Implementation(bool bNewPeeking)
{
	if (!bLeanLocked)
	{
		return;
	}
	bPeeking = bNewPeeking;
	OnRep_Peeking();
}

void ANiceInkCharacter::OnRep_Lean()
{
	// 沉睡時膠囊不跟控制器 yaw 的同一招：鎖定時朝向由伺服器定
	bUseControllerRotationYaw = !bLeanLocked && !bAsleep;

	// 鎖定中：靜態身體讓位（藏＋無碰撞），噴射改打膠囊（骨骼 physics asset 未備前的粗命中）
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_PhysicsBody, bLeanLocked ? ECR_Block : ECR_Ignore);

	if (bLeanLocked)
	{
		ApplyBowPose();
		if (IsLocallyControlled())
		{
			LeanLockTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
			if (APlayerController* PC = Cast<APlayerController>(GetController()))
			{
				int32 ViewX = 0, ViewY = 0;
				PC->GetViewportSize(ViewX, ViewY);
				LeanCursorPx = FVector2D(ViewX * 0.5f, ViewY * 0.5f);
			}
		}
	}
	else
	{
		ResetBowPose();
		StopPaintingLocal();
	}
}

void ANiceInkCharacter::OnRep_Peeking()
{
	if (bLeanLocked)
	{
		ApplyBowPose(); // 頭頸重擺（含偷瞄轉頭）
	}
}

FVector ANiceInkCharacter::GetLeanFaceTargetWorld() const
{
	// 受害者的頭（偷瞄注視點）：睡姿網格腳底原點、頭在本地 +Z ~158
	const ANiceInkCharacter* Target = LeanTarget.Get();
	if (Target && Target->Body)
	{
		return Target->Body->GetComponentTransform().TransformPosition(FVector(0.0f, 15.0f, 158.0f));
	}
	return LeanPoint;
}

namespace
{
	// 對 poseable 骨頭在元件空間左乘一個增量旋轉（子骨自動跟隨）
	void RotateBoneCS(UPoseableMeshComponent* Poseable, FName Bone, const FQuat& Delta)
	{
		FTransform T = Poseable->GetBoneTransformByName(Bone, EBoneSpaces::ComponentSpace);
		T.SetRotation(Delta * T.GetRotation());
		Poseable->SetBoneTransformByName(Bone, T, EBoneSpaces::ComponentSpace);
	}
}

void ANiceInkCharacter::ApplyBowPose()
{
	if (!BowBody)
	{
		return;
	}

	// 骨骼資產惰性載入＋共用身體 MID（缺席時機制照跑、姿勢不演）
	if (!BowBody->GetSkinnedAsset())
	{
		if (!BowMesh)
		{
			BowMesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/SK_Char17.SK_Char17"));
		}
		if (BowMesh)
		{
			BowBody->SetSkinnedAssetAndUpdate(BowMesh);
			if (Body && Body->GetDynamicMaterial())
			{
				BowBody->SetMaterial(0, Body->GetDynamicMaterial());
			}
		}
	}
	if (!BowBody->GetSkinnedAsset())
	{
		return;
	}

	// 換上可擺骨身體；靜態身體藏起來、連碰撞一起讓位（噴射打彎腰的身體）
	if (Body)
	{
		Body->SetVisibility(false);
		Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	BowBody->SetVisibility(true);

	// 重置再擺（冪等）
	BowBody->ResetBoneTransformByName(TEXT("Spine"));
	BowBody->ResetBoneTransformByName(TEXT("Neck"));
	BowBody->ResetBoneTransformByName(TEXT("Head"));

	// 元件空間：網格臉朝本地 +Y。前彎＝繞本地 X 轉負角（PIE 實測校正符號）。
	// Spine 一根直直前彎：落筆點越低彎越深（硬生生，粗映射就是要的效果）
	const float TargetZ = FMath::Clamp(LeanPoint.Z - (GetActorLocation().Z - 92.0f), 0.0f, 120.0f);
	const float SpineDeg = FMath::Clamp(95.0f - TargetZ * 0.45f, 45.0f, 95.0f);
	RotateBoneCS(BowBody, TEXT("Spine"), FQuat(FVector(1, 0, 0), FMath::DegreesToRadians(-SpineDeg)));

	if (bPeeking)
	{
		// 偷瞄：Neck＋Head 一起硬轉向受害者的臉（抬頭＋側轉）
		const FVector FaceTarget = GetLeanFaceTargetWorld();
		const FTransform CompT = BowBody->GetComponentTransform();
		const FVector HeadPos = BowBody->GetBoneTransformByName(TEXT("Head"), EBoneSpaces::WorldSpace).GetLocation();
		const FVector LocalDir = CompT.InverseTransformVector((FaceTarget - HeadPos).GetSafeNormal());
		const float YawDeg = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(LocalDir.X, LocalDir.Y)), -80.0f, 80.0f);
		const FQuat PeekUp(FVector(1, 0, 0), FMath::DegreesToRadians(SpineDeg * 0.5f + 15.0f));
		const FQuat PeekYaw(FVector(0, 0, 1), FMath::DegreesToRadians(YawDeg * 0.5f));
		RotateBoneCS(BowBody, TEXT("Neck"), PeekYaw * PeekUp);
		RotateBoneCS(BowBody, TEXT("Head"), PeekYaw * PeekUp);
	}
	else
	{
		// 作畫：Neck＋Head 一起直直往前上方抬——臉對著落筆點（重度近視式）
		const float HeadDeg = SpineDeg * 0.4f;
		const FQuat HeadUp(FVector(1, 0, 0), FMath::DegreesToRadians(HeadDeg));
		RotateBoneCS(BowBody, TEXT("Neck"), HeadUp);
		RotateBoneCS(BowBody, TEXT("Head"), HeadUp);
	}
}

bool ANiceInkCharacter::GetEvidenceUVForHit(FName BoneName, const FVector& ImpactPoint, FVector2D& OutUV)
{
	if (!Body)
	{
		return false;
	}

	// 骨頭 → 站姿本地座標的粗錨點（濺漬不需要毫米精度；彎腰姿勢下的命中夠用）
	FVector Local(0.0f, 20.0f, 122.0f); // 預設：胸口
	if (BoneName != NAME_None)
	{
		const FString Bone = BoneName.ToString();
		if (Bone.Contains(TEXT("Head")) || Bone.Contains(TEXT("Neck")))
		{
			Local = FVector(0.0f, 18.0f, 155.0f);
		}
		else if (Bone.Contains(TEXT("Hips")) || Bone == TEXT("Spine"))
		{
			Local = FVector(0.0f, 20.0f, 95.0f);
		}
		else if (Bone.Contains(TEXT("Arm")) || Bone.Contains(TEXT("Hand")))
		{
			Local = FVector(Bone.StartsWith(TEXT("Left")) ? 48.0f : -48.0f, -4.0f, 120.0f);
		}
		else if (Bone.Contains(TEXT("UpLeg")))
		{
			Local = FVector(Bone.StartsWith(TEXT("Left")) ? 11.0f : -11.0f, 2.0f, 62.0f);
		}
		else if (Bone.Contains(TEXT("Leg")) || Bone.Contains(TEXT("Foot")) || Bone.Contains(TEXT("Toe")))
		{
			Local = FVector(Bone.StartsWith(TEXT("Left")) ? 10.0f : -10.0f, -3.0f, 28.0f);
		}
	}
	else
	{
		// 膠囊命中（無骨名）：彎腰中臉在前傾低處——命中高度粗分部位
		const float RelZ = ImpactPoint.Z - (GetActorLocation().Z - 92.0f); // 相對腳底
		if (bLeanLocked ? RelZ > 55.0f : RelZ > 130.0f)
		{
			Local = FVector(0.0f, 18.0f, 155.0f); // 頭臉（彎腰時頭在半高）
		}
		else if (RelZ < 45.0f)
		{
			Local = FVector(0.0f, -3.0f, 40.0f); // 腿
		}
	}

	const FVector World = Body->GetComponentTransform().TransformPosition(Local);
	return Body->ResolveBodyUV(World, OutUV, 30.0f);
}

void ANiceInkCharacter::ResetBowPose()
{
	if (BowBody)
	{
		BowBody->SetVisibility(false);
	}
	if (Body && !bAsleep)
	{
		Body->SetVisibility(true);
		Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
}

void ANiceInkCharacter::UpdateLeanCamera(APlayerController* PC)
{
	ACameraActor* Cam = GetOrSpawnCinematicCamera();
	if (!Cam)
	{
		return;
	}

	if (bPeeking)
	{
		if (!bPeekCamApplied)
		{
			// 偷瞄鏡頭：從鎖定點上方硬轉向受害者的臉
			const FVector FaceTarget = GetLeanFaceTargetWorld();
			const FVector CamPos = FVector(LeanPoint) + FVector(LeanNormal) * (LeanCameraHeight + 14.0f);
			Cam->SetActorLocationAndRotation(CamPos, (FaceTarget - CamPos).Rotation());
			PC->SetViewTargetWithBlend(Cam, 0.12f, VTBlend_Linear);
			bPeekCamApplied = true;
			bLeanCamActive = false;
		}
		return;
	}

	if (!bLeanCamActive)
	{
		// 鎖定畫布鏡頭：貼皮膚上方、垂直往下看；「上」偏向作畫者自己（像趴在桌前的紙）
		const FVector N = FVector(LeanNormal).GetSafeNormal();
		const FVector CamPos = FVector(LeanPoint) + N * LeanCameraHeight;
		FVector UpHint = (FVector(LeanPoint) - GetActorLocation()).GetSafeNormal2D();
		if (UpHint.IsNearlyZero())
		{
			UpHint = FVector(0, 1, 0);
		}
		const FRotator CamRot = FRotationMatrix::MakeFromXZ(-N, UpHint).Rotator();
		Cam->SetActorLocationAndRotation(CamPos, CamRot);
		PC->SetViewTargetWithBlend(Cam, 0.18f, VTBlend_Linear);
		bLeanCamActive = true;
		bPeekCamApplied = false;
		bViewOverridden = true;
		bWideViewActive = false;
		bThirdPersonActive = false;
		LastViewWorkId = INDEX_NONE;
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

void ANiceInkCharacter::NiHost()
{
	if (UNiceInkSessionSubsystem* Sessions = GetGameInstance() ? GetGameInstance()->GetSubsystem<UNiceInkSessionSubsystem>() : nullptr)
	{
		Sessions->HostSession(/*bLan=*/true);
	}
}

void ANiceInkCharacter::NiJoin()
{
	if (UNiceInkSessionSubsystem* Sessions = GetGameInstance() ? GetGameInstance()->GetSubsystem<UNiceInkSessionSubsystem>() : nullptr)
	{
		Sessions->JoinFirstFoundSession(/*bLan=*/true);
	}
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
