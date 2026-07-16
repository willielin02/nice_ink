#include "NiceInkCharacter.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "DreamMazeComponent.h"
#include "Engine/Engine.h"
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
#include "NeckStretchComponent.h"
#include "Net/UnrealNetwork.h"
#include "NiceInkAudio.h"
#include "NiceInkGameInstance.h"
#include "NiceInkGameMode.h"
#include "NiceInkGameState.h"
#include "NiceInkPlayerState.h"
#include "NiceInkSessionSubsystem.h"
#include "NiceInkTypes.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// sumo 網格：腳底在原點。Blender 源臉朝 -Y，FBX→UE 匯入含 (x,-y,z) 鏡射 →
	// UE 本地臉朝 +Y（與 char17 完全相同——char17 的 Blender 源同樣朝 -Y，PIE 實證 +Y）。
	// 因此站/躺旋轉與 char17 逐字沿用；本地錨點 = Blender 量測值 y 取負。
	const FVector BodyStandRelLoc(0.0f, 0.0f, -92.0f);
	const FRotator BodyStandRelRot(0.0f, -90.0f, 0.0f);
	// 仰躺（char17 PIE 實測值沿用）；pivot 偏移待 ragdoll 睡姿（我-12）落地後重調
	const FVector BodyLieRelLoc(-87.0f, 0.0f, -60.0f);
	const FRotator BodyLieRelRot(0.0f, 90.0f, -90.0f);

	constexpr float PointFlushInterval = 0.05f;
	constexpr int32 PointFlushMaxBatch = 10;

	// 脖底切盤關節（2026-07-16 Blender 手術燒進網格的常數——不是調參旋鈕）：
	// 切面＝法線前傾 20°（朝臉側 +Y）、過站姿本地 (0,0,141)cm；上殼（頭+垂肉+髮髻）
	// Head 骨硬權重 1.0、雙斷面實心蓋。合法相對運動只有兩種：繞此軸旋轉、沿切面內平移
	// ——其餘任何 Neck/Head 相對姿勢都會張開切口（楔形縫/穿插），一律禁止。
	// Blender 世界 (0,-sin20,cos20) 過 (0,0,1.41m) → UE 本地 y 取負、m→cm。
	// （2026-07-16 三改版：一維軌道 OrbitAzTheta 退役——「簡單但難用」：同軸換擋、
	// 沿軌開車、俯仰不可控。臉指向制＝滑鼠 X/Y 直接給 (az,tilt)，姿勢/抬升仍為純函數，
	// 域鉗於量測表——見 SleepAimMaxTiltDeg / SleepAimLiftCm。）

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

	static ConstructorHelpers::FObjectFinder<UStaticMesh> BodyMeshAsset(TEXT("/Game/Characters/SM_Sumo.SM_Sumo"));
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
	// 止血（2026-07-15 頭糊成一團）：poseable 手擺骨的前一幀緩衝不可靠，
	// per-bone motion blur 把姿勢寫入當成高速移動塗抹——整件關閉
	BowBody->bPerBoneMotionBlur = false;

	// 轆轤首伸縮脖：頭身切開後的銜接曲面（每幀生成；掛 BowBody 下＝CS 座標直出）
	NeckStretch = CreateDefaultSubobject<UNeckStretchComponent>(TEXT("NeckStretch"));
	NeckStretch->SetupAttachment(BowBody);


	// 實體麥克筆（細圓柱＋深色 MID）：筆尖對著墨點、筆身指向作畫者頭部
	PenMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PenMesh"));
	PenMesh->SetupAttachment(GetCapsuleComponent());
	PenMesh->SetAbsolute(true, true, true);
	PenMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PenMesh->SetVisibility(false);
	PenMesh->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PenCylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (PenCylinder.Succeeded())
	{
		PenMesh->SetStaticMesh(PenCylinder.Object);
	}

	InkCanvas = CreateDefaultSubobject<UInkCanvasComponent>(TEXT("InkCanvas"));

	// 醉夢迷宮：受害者 client 本地模擬（不複製——其餘玩家一無所知）
	DreamMaze = CreateDefaultSubobject<UDreamMazeComponent>(TEXT("DreamMaze"));

	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
	FirstPersonCamera->SetRelativeLocation(FVector(0.0f, 0.0f, 64.0f)); // sumo 眼高 z156（量測）− 半膠囊 92
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

	// 實體筆外觀：細黑桿（引擎圓柱 100×100×100 → 1.2cm 粗、15cm 長）
	if (PenMesh)
	{
		PenMesh->SetWorldScale3D(FVector(0.012f, 0.012f, 0.15f));
		if (UMaterialInstanceDynamic* PenMID = PenMesh->CreateAndSetMaterialInstanceDynamic(0))
		{
			PenMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.03f, 0.03f, 0.04f));
		}
	}
}

void ANiceInkCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ANiceInkCharacter, bAsleep);
	DOREPLIFETIME(ANiceInkCharacter, bEyesOpen);
	// 頭部轉動破綻：臉指向只發給他端（本人端用本地連續值零延遲；抬升＝純函數不複製）
	DOREPLIFETIME_CONDITION(ANiceInkCharacter, SleepAimAzDeg, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(ANiceInkCharacter, SleepAimTiltDeg, COND_SkipOwner);
	// 技能庫存只給本人：作畫者不該從網路層讀到「受害者拿到技能／進度」
	DOREPLIFETIME_CONDITION(ANiceInkCharacter, SprayCharges, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ANiceInkCharacter, KickCharges, COND_OwnerOnly);
	DOREPLIFETIME(ANiceInkCharacter, bBlinded);
	DOREPLIFETIME(ANiceInkCharacter, BlindType);
	DOREPLIFETIME(ANiceInkCharacter, bLeanLocked);
	DOREPLIFETIME(ANiceInkCharacter, LeanPoint);
	DOREPLIFETIME(ANiceInkCharacter, LeanNormal);
	DOREPLIFETIME(ANiceInkCharacter, LeanTarget);
	DOREPLIFETIME(ANiceInkCharacter, bPeeking);
	DOREPLIFETIME(ANiceInkCharacter, bBodyFaceDown);
}

void ANiceInkCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	EnsureAvatarApplied();
	UpdatePenVisual(); // 所有端：筆尖跟著墨點走

	APlayerController* PC = Cast<APlayerController>(GetController());
	const bool bLocal = PC && IsLocallyControlled();

	if (bLocal)
	{
		// robo 頭控鉤子：只有本地受害者消化——睜眼＝(Yaw,Pitch)=臉指向(az,tilt)；
		// 閉眼＝盲瞄(扭轉,低頭)
		if (bHasPendingDebugSleepLook)
		{
			bHasPendingDebugSleepLook = false;
			if (bAsleep && bEyesOpen)
			{
				SleepAimAzLocal = FMath::Fmod(FMath::Fmod(
					static_cast<float>(PendingDebugSleepLook.X), 360.0f) + 360.0f, 360.0f);
				SleepAimTiltLocal = FMath::Clamp(static_cast<float>(PendingDebugSleepLook.Y),
					0.0f, SleepAimMaxTiltDeg(SleepAimAzLocal));
			}
			else if (bAsleep)
			{
				SleepTwistLocal = PendingDebugSleepLook.X; // 360° 自由
				SleepBendLocal = FMath::Clamp(PendingDebugSleepLook.Y, 0.0f, SleepBendMaxDeg);
			}
		}
		if (!bSystemMenuOpen)
		{
			PollSleepHead(PC, DeltaSeconds); // 視線輸入先於替身更新＝相機零延遲
		}
	}

	UpdateSleepBodyDouble(DeltaSeconds); // 所有端：睡姿替身＋頭部轉動破綻
	UpdateWalkAnim(DeltaSeconds);        // 所有端：站立移動的程式化步伐
	UpdateLeanArm();                     // 所有端：貼臉鎖定的握筆右臂（跟著實體筆）

	// 伸縮脖：所有擺骨完成後解銜接曲面（顯式順序；lean-lock 事件驅動的擺骨
	// 由下一 tick 的變化偵測接住，一幀延遲不可感）
	if (NeckStretch)
	{
		NeckStretch->UpdateNeck();
	}

	if (!bLocal)
	{
		return;
	}

	PollSystemMenu(PC);
	if (bSystemMenuOpen)
	{
		// 選單開著＝遊戲輸入全停（滑鼠屬於選單按鈕）；系統演出鏡頭照常
		UpdateCinematicCamera(PC);
		return;
	}

	PollLook(PC, DeltaSeconds);
	PollMove(PC);
	PollTrapDial(PC);
	PollCounterplay(PC);
	PollFlip(PC);
	PollAccusation(PC);
	PollLobby(PC);
	PollPalette(PC);
	PollLeanEnter(PC);
	PollLockedDraw(PC, DeltaSeconds);
	UpdateCinematicCamera(PC);
}

void ANiceInkCharacter::PollSystemMenu(APlayerController* PC)
{
	if (PC->WasInputKeyJustPressed(EKeys::Escape))
	{
		SetSystemMenuOpen(!bSystemMenuOpen);
	}
}

void ANiceInkCharacter::SetSystemMenuOpen(bool bOpen)
{
	bSystemMenuOpen = bOpen;
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		return;
	}
	PC->bShowMouseCursor = bOpen;
	if (bOpen)
	{
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
		int32 VX = 0, VY = 0;
		PC->GetViewportSize(VX, VY);
		PC->SetMouseLocation(VX / 2, VY / 2);
	}
	else
	{
		PC->SetInputMode(FInputModeGameOnly());
	}
}

void ANiceInkCharacter::PollFlip(APlayerController* PC)
{
	// 翻身提案（user 定案 2026-07-15）：作畫階段、作畫者限定；F＝提出／同意。
	// 鎖定作畫中也可表態（F 不與 lean 輸入衝突）。
	const ANiceInkGameState* GS = GetWorld() ? GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
	if (!GS || GS->CurrentPhase != ENiceInkPhase::Drawing || bAsleep ||
		GetInkAuthorId() == GS->VictimPlayerId || !PC->WasInputKeyJustPressed(EKeys::F))
	{
		return;
	}
	if (GS->FlipProposerId == INDEX_NONE)
	{
		FlipAgreedProposalSerial = GS->FlipProposalSerial + 1; // 我提的＝自動同意
		ServerProposeFlip();
	}
	else if (FlipAgreedProposalSerial != GS->FlipProposalSerial)
	{
		FlipAgreedProposalSerial = GS->FlipProposalSerial;
		ServerAgreeFlip();
	}
}

void ANiceInkCharacter::PollLobby(APlayerController* PC)
{
	const ANiceInkGameState* GS = GetWorld() ? GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
	if (!GS)
	{
		return;
	}

	// 主機手動開局（大廳與場間大廳）；伺服器端再驗一次主機身分
	if ((GS->CurrentPhase == ENiceInkPhase::Lobby || GS->CurrentPhase == ENiceInkPhase::PostGame) &&
		GetWorld()->GetNetMode() != NM_Client && PC->WasInputKeyJustPressed(EKeys::Enter))
	{
		ServerRequestStartMatch();
	}

	if (GS->CurrentPhase != ENiceInkPhase::PostGame)
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
		if (CinematicCamera && CinematicCamera->GetCameraComponent())
		{
			// CameraActor 預設鎖 16:9——視窗比例不同時產生黑邊，
			// 會讓「螢幕像素→世界射線」與 HUD 座標系錯開（游標 offset 的元凶）
			CinematicCamera->GetCameraComponent()->bConstrainAspectRatio = false;
		}
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

	const FVector BodyCenter = Victim->Body->GetComponentTransform().TransformPosition(FVector(0, 0, 103.0f));
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
		? Victim->Body->GetComponentTransform().TransformPosition(FVector(0, 0, 103.0f))
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
	if (GNiceInkKickEnabled && PC->WasInputKeyJustPressed(EKeys::E) && KickCharges > 0)
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

void ANiceInkCharacter::PollSleepHead(APlayerController* PC, float DeltaSeconds)
{
	if (!bAsleep)
	{
		SleepKeyHeldTime[0] = SleepKeyHeldTime[1] = SleepKeyHeldTime[2] = SleepKeyHeldTime[3] = 0.0f;
		return;
	}

	if (bEyesOpen)
	{
		// 睜眼＝滑鼠 X/Y 直接操縱臉指向（2026-07-16 三改版：滑鼠往哪撥臉朝哪；
		// 俯仰鉗量測表、姿勢與抬升＝指向的純函數）。迷宮在睜眼即停止吃滑鼠
		//（DreamMazeComponent tick 閘）＝滑鼠此刻是空的。
		SleepKeyHeldTime[0] = SleepKeyHeldTime[1] = SleepKeyHeldTime[2] = SleepKeyHeldTime[3] = 0.0f;
		if (bWakeGazeActive)
		{
			float MouseX = 0.0f;
			float MouseY = 0.0f;
			PC->GetInputMouseDelta(MouseX, MouseY);
			// 號誌待驗：右滑＝＋az；上撥＝抬（tilt 減，FPS 慣例）——viewport 驗手感後可反轉
			SleepAimAzLocal = FMath::Fmod(FMath::Fmod(
				SleepAimAzLocal + MouseX * EffectiveLookSensitivity(), 360.0f) + 360.0f, 360.0f);
			SleepAimTiltLocal = FMath::Clamp(SleepAimTiltLocal - MouseY * EffectiveLookSensitivity(),
				0.0f, SleepAimMaxTiltDeg(SleepAimAzLocal));
		}

		// 頭部轉動破綻：臉指向節流上報（閉眼不送＝盲瞄不洩漏）
		SleepLookSendAccum += DeltaSeconds;
		if (SleepLookSendAccum >= 0.05f &&
			(FMath::Abs(FMath::FindDeltaAngleDegrees(LastSentAimAz, SleepAimAzLocal)) > 0.5f ||
			 FMath::Abs(LastSentAimTilt - SleepAimTiltLocal) > 0.5f))
		{
			SleepLookSendAccum = 0.0f;
			LastSentAimAz = SleepAimAzLocal;
			LastSentAimTilt = SleepAimTiltLocal;
			ServerUpdateSleepAim(SleepAimAzLocal, SleepAimTiltLocal);
		}
		return;
	}

	// 閉眼＝方向鍵分軸盲瞄（滑鼠屬迷宮；狀態照改、骨頭不動＝瞄準不成為破綻）：
	// 左右＝扭轉、下＝低頭、上＝撤回。單擊 1°、按住 0.25s 後連發、斜向可同按。
	static const FKey Keys[4] = { EKeys::Left, EKeys::Right, EKeys::Up, EKeys::Down };
	float Step[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	for (int32 i = 0; i < 4; ++i)
	{
		if (PC->IsInputKeyDown(Keys[i]))
		{
			if (SleepKeyHeldTime[i] == 0.0f)
			{
				Step[i] = 1.0f; // 單擊＝精準 1°
			}
			else if (SleepKeyHeldTime[i] > 0.25f)
			{
				Step[i] = SleepHeadTurnRate * DeltaSeconds; // 按住連發
			}
			SleepKeyHeldTime[i] += DeltaSeconds;
		}
		else
		{
			SleepKeyHeldTime[i] = 0.0f;
		}
	}

	SleepTwistLocal = FRotator::NormalizeAxis(SleepTwistLocal - Step[0] + Step[1]); // 360° 自由
	SleepBendLocal = FMath::Clamp(SleepBendLocal + Step[3] - Step[2], 0.0f, SleepBendMaxDeg);
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
		// 沉睡中滑鼠永久屬於迷宮游標（2026-07-15 終版：頭改方向鍵分軸控制，
		// RMB 瞄準模式退役——待定 #15 一併解決）；相機由睡姿替身統一放置
		return;
	}

	// 俯仰也走 control rotation：滑鼠改的是控制器姿態，
	// 相機每 tick 對齊 pitch（yaw 由 bUseControllerRotationYaw 轉動膠囊）
	FRotator Ctrl = PC->GetControlRotation();
	Ctrl.Yaw += MouseX * EffectiveLookSensitivity();
	Ctrl.Pitch = FMath::ClampAngle(Ctrl.Pitch + MouseY * EffectiveLookSensitivity(), -89.0f, 89.0f);
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

void ANiceInkCharacter::PollTrapDial(APlayerController* PC)
{
	// 兇手轉盤（SPEC 定案 #31）：滾輪選 −360~360——兇手很可能正 lean-lock 作畫，
	// 游標是他的筆，不徵用；5 秒到自動送出當前值（沒動＝0 度）。
	if (!bTrapDialActive)
	{
		return;
	}
	if (PC->WasInputKeyJustPressed(EKeys::MouseScrollUp))
	{
		TrapDialAngleDeg = FMath::Clamp(TrapDialAngleDeg + 15.0f, -360.0f, 360.0f);
	}
	if (PC->WasInputKeyJustPressed(EKeys::MouseScrollDown))
	{
		TrapDialAngleDeg = FMath::Clamp(TrapDialAngleDeg - 15.0f, -360.0f, 360.0f);
	}
	if (GetWorld()->GetTimeSeconds() >= TrapDialEndTime)
	{
		bTrapDialActive = false;
		ServerSubmitTrapDial(TrapDialAngleDeg);
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

	// 起身：右鍵或 WASD。進鎖後 0.25s 內不受理——
	// (1) 主機的 Server RPC 同幀直接執行，進鎖的那次 RMB 在同一 tick 仍是 just-pressed，
	//     會被誤讀成起身（鎖定只活一幀、畫面永遠不切）；
	// (2) 按著 W 走近時按 RMB，殘留的 W 也會讓所有端秒退。
	const bool bWantsExit = PC->WasInputKeyJustPressed(EKeys::RightMouseButton) ||
		PC->IsInputKeyDown(EKeys::W) || PC->IsInputKeyDown(EKeys::A) ||
		PC->IsInputKeyDown(EKeys::S) || PC->IsInputKeyDown(EKeys::D);
	if (bWantsExit && GetWorld()->GetTimeSeconds() - LeanLockTime > 0.25f)
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
	const float CursorScale = LeanCursorSpeed * 6.0f * (EffectiveLookSensitivity() / FMath::Max(0.1f, LookSensitivity));
	LeanCursorPx.X = FMath::Clamp(LeanCursorPx.X + MouseX * CursorScale, 0.0f, static_cast<float>(ViewX));
	LeanCursorPx.Y = FMath::Clamp(LeanCursorPx.Y - MouseY * CursorScale, 0.0f, static_cast<float>(ViewY));

	// 偷瞄中或鏡頭未到位不落筆
	const float Now = GetWorld()->GetTimeSeconds();
	const bool bCanPaint = !bPeeking && (Now - LeanLockTime) > LeanPaintDelay;
	const bool bWantsPaint = bCanPaint && PC->IsInputKeyDown(EKeys::LeftMouseButton);
	if (!bWantsPaint)
	{
		StopPaintingLocal();
		LastCursorPx = LeanCursorPx;
		return;
	}

	ANiceInkCharacter* Target = LeanTarget.Get();
	if (!Target || !Target->Body)
	{
		StopPaintingLocal();
		return;
	}

	// 螢幕空間細分（每 4px 一個取樣）：筆快滑不掉點，
	// 跨 UV 接縫的線在 3D 上連續取樣、兩側各自落墨＝縫合（縫在筆下隱形）
	TArray<FVector2D> SampleUVs;
	const FVector2D From = bPainting ? LastCursorPx : LeanCursorPx;
	const float PixelDist = FVector2D::Distance(From, LeanCursorPx);
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(PixelDist / 4.0f), 1, 24);
	for (int32 Step = 1; Step <= Steps; ++Step)
	{
		const FVector2D Px = FMath::Lerp(From, LeanCursorPx, static_cast<float>(Step) / Steps);
		FVector2D UV;
		if (ResolveCursorToTargetUV(PC, Px, UV))
		{
			SampleUVs.Add(UV);
		}
	}
	LastCursorPx = LeanCursorPx;

	if (SampleUVs.IsEmpty())
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
		ServerPaintBegin(Target, SelectedColorIndex, SampleUVs[0]);
		SampleUVs.RemoveAt(0);
	}

	PendingPoints.Append(SampleUVs);
	PointFlushTimer += DeltaSeconds;
	if (PendingPoints.Num() >= PointFlushMaxBatch || PointFlushTimer >= PointFlushInterval)
	{
		ServerPaintPoints(PendingPoints);
		PendingPoints.Reset();
		PointFlushTimer = 0.0f;
	}
}

bool ANiceInkCharacter::ResolveCursorToTargetUV(APlayerController* PC, const FVector2D& ScreenPx, FVector2D& OutUV) const
{
	ANiceInkCharacter* Target = LeanTarget.Get();
	if (!Target || !Target->Body)
	{
		return false;
	}

	FVector RayOrigin, RayDir;
	if (!PC->DeprojectScreenPositionToWorld(ScreenPx.X, ScreenPx.Y, RayOrigin, RayDir))
	{
		return false;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(NiceInkCursorTrace), /*bInTraceComplex=*/true);
	QueryParams.AddIgnoredActor(this);

	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, RayOrigin, RayOrigin + RayDir * 220.0f, ECC_Visibility, QueryParams) ||
		Hit.GetActor() != Target)
	{
		return false;
	}

	// 容差 1.5mm：褌外表面離皮膚至少一個布厚（2mm）——打在布上的命中解算不到皮膚，
	// 筆劃被拒＝SPEC「褌下的皮膚不可畫（物理遮擋）」。皮膚直擊的解算距離 ≈ 0。
	return Target->Body->ResolveBodyUV(Hit.ImpactPoint, OutUV, /*MaxDistance=*/0.15f);
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

void ANiceInkCharacter::ClientSyncPoseTransform_Implementation(const FTransform& NewTransform)
{
	if (HasAuthority())
	{
		return; // 主機本人＝server 路徑已權威落地
	}
	SetActorTransform(NewTransform, false, nullptr, ETeleportType::TeleportPhysics);
	GetCharacterMovement()->StopMovementImmediately();
}

void ANiceInkCharacter::ServerSetAsleep(bool bNewAsleep, const FTransform& LieTransform)
{
	if (!HasAuthority() || bAsleep == bNewAsleep)
	{
		return;
	}

	bAsleep = bNewAsleep;

	if (bNewAsleep)
	{
		bEyesOpen = false;
		SleepAimAzDeg = 180.0f;  // 頭部轉動破綻：新回合姿態歸零（180=腳側、朝天）
		SleepAimTiltDeg = 0.0f;
		SprayCharges = 0;
		KickCharges = 0;
		bMazeSprayGranted = false; // 技能授予去重：每回合每存檔點一次
		bMazeKickGranted = false;
		SeatTransform = GetActorTransform();
		SetActorTransform(LieTransform, false, nullptr, ETeleportType::TeleportPhysics);
		GetCharacterMovement()->StopMovementImmediately();
		GetCharacterMovement()->DisableMovement();
		ClientSyncPoseTransform(LieTransform); // 本端 yaw 落地（客戶端權威、修正不覆蓋）
	}
	else
	{
		// 睜眼即過期：未用完的噴射／拳腳作廢（SPEC 定案 #6/#7）
		SprayCharges = 0;
		KickCharges = 0;
		bBodyFaceDown = false; // 現身站起＝翻身狀態自然結束
		SetActorTransform(SeatTransform, false, nullptr, ETeleportType::TeleportPhysics);
		GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		ClientSyncPoseTransform(SeatTransform); // 回座同樣要本端落地
	}

	ApplySleepVisual();
}

// --- 翻身提案（2026-07-15 user 定案：非 ragdoll、提案＋全員同意、兩固定姿勢）---

void ANiceInkCharacter::ServerProposeFlip_Implementation()
{
	if (ANiceInkGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ANiceInkGameMode>() : nullptr)
	{
		GM->HandleFlipPropose(this);
	}
}

void ANiceInkCharacter::ServerAgreeFlip_Implementation()
{
	if (ANiceInkGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ANiceInkGameMode>() : nullptr)
	{
		GM->HandleFlipAgree(this);
	}
}

void ANiceInkCharacter::ServerSetFaceDown(bool bNewFaceDown)
{
	if (!HasAuthority() || bBodyFaceDown == bNewFaceDown)
	{
		return;
	}
	bBodyFaceDown = bNewFaceDown;
	ApplySleepVisual();
}

void ANiceInkCharacter::OnRep_FaceDown()
{
	ApplySleepVisual();
}

// --- 醉夢圓形迷宮 RPC（SPEC v3.3）---

namespace
{
	// 迷宮事件的共通驗證：發話者＝當前受害者、沉睡未睜眼、相位在睡眠期
	bool ValidateMazeSender(const ANiceInkCharacter* Sender, bool bAllowSeating)
	{
		const ANiceInkGameState* GS = Sender->GetWorld() ? Sender->GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
		const APlayerState* PS = Sender->GetPlayerState();
		if (!GS || !PS || PS->GetPlayerId() != GS->VictimPlayerId || !Sender->bAsleep || Sender->bEyesOpen)
		{
			return false;
		}
		return GS->CurrentPhase == ENiceInkPhase::Drawing ||
			(bAllowSeating && GS->CurrentPhase == ENiceInkPhase::Seating);
	}
}

void ANiceInkCharacter::ClientStartMaze_Implementation(int32 Seed, const FDreamMazeParams& Params, const TArray<int32>& TrapOwnerIds)
{
	if (DreamMaze)
	{
		DreamMaze->StartMaze(Seed, Params, TrapOwnerIds);
	}
}

void ANiceInkCharacter::ServerMazeTrapHit_Implementation(int32 KillerPlayerId)
{
	if (!ValidateMazeSender(this, /*bAllowSeating=*/true))
	{
		return;
	}
	if (ANiceInkGameMode* GM = GetWorld()->GetAuthGameMode<ANiceInkGameMode>())
	{
		GM->HandleMazeTrapHit(this, KillerPlayerId);
	}
}

void ANiceInkCharacter::ClientOpenTrapDial_Implementation(float DialSeconds)
{
	// 只有兇手本人收到這通（SPEC：系統只通知 A 本人）
	bTrapDialActive = true;
	TrapDialAngleDeg = 0.0f;
	TrapDialDuration = DialSeconds;
	TrapDialEndTime = GetWorld()->GetTimeSeconds() + DialSeconds;
}

void ANiceInkCharacter::ServerSubmitTrapDial_Implementation(float AngleDeg)
{
	if (ANiceInkGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ANiceInkGameMode>() : nullptr)
	{
		GM->HandleTrapDialSubmit(this, AngleDeg);
	}
}

void ANiceInkCharacter::ClientApplyMazeRotation_Implementation(float AngleDeg)
{
	if (DreamMaze)
	{
		DreamMaze->ApplyRotation(AngleDeg);
	}
}

void ANiceInkCharacter::ServerMazeCheckpointReached_Implementation(uint8 CheckpointType)
{
	if (!ValidateMazeSender(this, /*bAllowSeating=*/true))
	{
		return;
	}
	if (CheckpointType == 0 && !bMazeSprayGranted)
	{
		bMazeSprayGranted = true;
		++SprayCharges;
	}
	else if (GNiceInkKickEnabled && CheckpointType == 1 && !bMazeKickGranted)
	{
		bMazeKickGranted = true;
		++KickCharges;
	}
}

void ANiceInkCharacter::ServerMazeExited_Implementation()
{
	// 出口只在作畫階段有效；睜眼＝無聲甦醒（貼圖切換是唯一破綻，零提示）
	if (!ValidateMazeSender(this, /*bAllowSeating=*/false))
	{
		return;
	}
	bEyesOpen = true;
	ApplySleepVisual();
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
	case EInkEvidenceType::Sneeze: LocalOrigin = FVector(0.0f, 22.0f, 151.0f); break;  // sumo 鼻尖（Blender 量測 y 取負）
	case EInkEvidenceType::Piss:   LocalOrigin = FVector(0.0f, 44.0f, 75.0f); break;   // sumo 胯前
	default:                       LocalOrigin = FVector(0.0f, -52.0f, 66.0f); break;  // sumo 臀後
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

	// 從身體中心朝瞄準方向掃掠 170cm（sumo 軀幹質心 z103，量測）
	const FVector Start = Body->GetComponentTransform().TransformPosition(FVector(0.0f, 0.0f, 103.0f));
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
	// 噴漬命中聲（沉睡者本人在音效層被靜音＝「無命中回饋」照舊成立）
	NiAudio::Play(this, ENiSound::SpraySplat);
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

	bSleepPoseDirty = true; // 睡/醒/翻身任何切換＝替身下次 tick 重擺（含抬頭方向重算）

	Body->SetEyesClosed(bAsleep && !bEyesOpen);

	// 無聲甦醒＝睜眼看得到自己的身體與正在落下的筆跡（2026-07-15 user 定案：
	// 抓現行要有畫面——誰低頭、跪在哪、筆落在哪）。現身站起恢復 owner-no-see
	//（SPEC 視角規則不變；巡禮仍是「全貌」初見——躺姿只看得到自己的局部）
	Body->SetOwnerNoSee(!(bAsleep && bEyesOpen));

	// 醒了（現身或睜眼後回合收束）：迷宮收工。終局昏死沒有迷宮（server 不發），
	// 元件維持 Inactive——HUD 畫「昏死不醒」。
	if (!bAsleep && DreamMaze)
	{
		DreamMaze->StopMaze();
	}

	// 睡姿網格：sumo 無烘焙睡姿（我-12 改 runtime ragdoll 快照）；
	// 過渡期用站姿網格放躺（fallback 分支天然處理：SleepMesh=StandMesh）
	if (bAsleep && !SleepMesh)
	{
		SleepMesh = StandMesh;
	}
	if (UStaticMesh* WantedMesh = bAsleep && SleepMesh ? SleepMesh.Get() : StandMesh.Get())
	{
		Body->SwapBodyMesh(WantedMesh);
	}

	if (bAsleep && bBodyFaceDown)
	{
		// 背面固定姿勢＝同一網格繞身體長軸（capsule 本地 X）翻 180°；
		// pivot 在腳跟線上不在體軸上→翻完會沉地，用包圍盒底差把 Z 補回地板
		const FQuat ProneQ = FQuat(FVector::XAxisVector, PI) * BodyLieRelRot.Quaternion();
		const FTransform CapT = GetCapsuleComponent()->GetComponentTransform();
		const float UpBottom = Body->CalcBounds(FTransform(BodyLieRelRot.Quaternion(), BodyLieRelLoc) * CapT).GetBox().Min.Z;
		const float DnBottom = Body->CalcBounds(FTransform(ProneQ, BodyLieRelLoc) * CapT).GetBox().Min.Z;
		FVector ProneLoc = BodyLieRelLoc;
		ProneLoc.Z += UpBottom - DnBottom; // capsule 直立：世界 Z 差＝相對 Z 差
		Body->SetRelativeLocation(ProneLoc);
		Body->SetRelativeRotation(ProneQ.Rotator());
	}
	else
	{
		Body->SetRelativeLocation(bAsleep ? BodyLieRelLoc : BodyStandRelLoc);
		Body->SetRelativeRotation(bAsleep ? BodyLieRelRot : BodyStandRelRot);
	}

	// 沉睡時膠囊不再跟隨控制器 yaw——躺姿朝向由 LieTransform 決定，
	// 不受受害者入睡前的視角污染
	bUseControllerRotationYaw = !bAsleep;

	if (IsLocallyControlled())
	{
		if (bAsleep)
		{
			bEmergeRequested = false;
			// 頭部姿態歸零（安睡朝向）；相機由睡姿替身函式接管
			SleepAimAzLocal = 180.0f;
			SleepAimTiltLocal = 0.0f;
			SleepTwistLocal = 0.0f;
			SleepBendLocal = 0.0f;
			bSleepPoseDirty = true;
			CameraPitch = bBodyFaceDown ? -55.0f : 55.0f;
			FirstPersonCamera->SetRelativeLocation(BodyLieRelLoc + FVector(172.0f, 0.0f, 22.0f));
			FirstPersonCamera->SetRelativeRotation(FRotator(CameraPitch, 0.0f, 0.0f));
		}
		else
		{
			CameraPitch = 0.0f;
			FirstPersonCamera->SetRelativeLocation(FVector(0.0f, 0.0f, 64.0f)); // 與建構子一致（sumo 眼高）
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
	// 受害者的頭（偷瞄注視點）：sumo 腳底原點、頭在本地 +Z ~152、臉朝 +Y（UE 匯入後）
	const ANiceInkCharacter* Target = LeanTarget.Get();
	if (Target && Target->Body)
	{
		return Target->Body->GetComponentTransform().TransformPosition(FVector(0.0f, 15.0f, 152.0f));
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

bool ANiceInkCharacter::EnsurePoseableAsset(UPoseableMeshComponent* Poseable)
{
	// 骨骼資產惰性載入＋共用身體 MID（缺席時機制照跑、姿勢不演）。
	// lean-lock 彎腰（BowBody）與睡姿替身（BowBody/FpSleepBody）共用。
	if (!Poseable)
	{
		return false;
	}
	if (Poseable->GetSkinnedAsset())
	{
		return true;
	}
	if (!BowMesh)
	{
		BowMesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/SK_Sumo.SK_Sumo"));
	}
	if (BowMesh)
	{
		Poseable->SetSkinnedAssetAndUpdate(BowMesh);
		if (Body && Body->GetDynamicMaterial())
		{
			// 皮膚 MID 按槽名指派：SK 匯入的槽序與 SM 相反（褌在 0）——
			// 寫死 index 0 會把皮膚材質糊到褌上。褌槽保留資產上的 M_Fundoshi。
			const TArray<FSkeletalMaterial>& SkMats = BowMesh->GetMaterials();
			for (int32 i = 0; i < SkMats.Num(); ++i)
			{
				if (!SkMats[i].MaterialSlotName.ToString().Contains(TEXT("Fundoshi")))
				{
					Poseable->SetMaterial(i, Body->GetDynamicMaterial());
				}
			}
		}
	}
	// 伸縮脖綁定資料源（骨骼 ref pose 快取＋端色＋抄身體 MID 膚色參數）
	if (Poseable == BowBody && NeckStretch && Poseable->GetSkinnedAsset())
	{
		NeckStretch->InitFromSource(BowBody, Body ? Body->GetDynamicMaterial() : nullptr);
	}
	return Poseable->GetSkinnedAsset() != nullptr;
}

void ANiceInkCharacter::DebugRoboSleepLook(float Yaw, float Pitch)
{
	bHasPendingDebugSleepLook = true;
	PendingDebugSleepLook = FVector2D(Yaw, Pitch);
}

void ANiceInkCharacter::ApplyBowPose()
{
	if (!BowBody)
	{
		return;
	}

	if (!EnsureBowBodyAsset())
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

	// 重置再擺（冪等）：先還原元件相對位置（上次的補位滑移不可累積），再回參考姿勢。
	// UPoseableMeshComponent 的 CS 快取要等 tick 才重算——每次「寫姿勢→讀骨骼」之間
	// 都必須 RefreshBoneTransforms()，否則讀到上一幀的舊姿勢（65.9cm 誤差的元凶）。
	BowBody->SetRelativeLocationAndRotation(BodyStandRelLoc, BodyStandRelRot);
	BowBody->ResetBoneTransformByName(TEXT("Spine"));
	BowBody->ResetBoneTransformByName(TEXT("Neck"));
	BowBody->ResetBoneTransformByName(TEXT("Head"));
	BowBody->RefreshBoneTransforms();

	const FTransform CompT = BowBody->GetComponentTransform();
	const FVector BendAxisW = CompT.TransformVectorNoScale(FVector(1, 0, 0)).GetSafeNormal(); // 元件 X＝彎折軸

	// --- Spine：解算單一硬彎角，讓「頭骨」真的抵達落筆點上方 ---
	// 目標：頭骨到 LeanPoint + 法線 × 22cm（臉貼著畫，重度近視式）
	const FVector SpinePivot = BowBody->GetBoneTransformByName(TEXT("Spine"), EBoneSpaces::WorldSpace).GetLocation();
	const FVector Head0 = BowBody->GetBoneTransformByName(TEXT("Head"), EBoneSpaces::WorldSpace).GetLocation();
	const FVector HeadTarget = FVector(LeanPoint) + FVector(LeanNormal).GetSafeNormal() * 22.0f;

	auto ProjectOntoBendPlane = [&BendAxisW](const FVector& V) {
		return (V - FVector::DotProduct(V, BendAxisW) * BendAxisW);
	};
	const FVector A = ProjectOntoBendPlane(Head0 - SpinePivot);
	const FVector B = ProjectOntoBendPlane(HeadTarget - SpinePivot);
	float SpineRad = 0.0f;
	if (!A.IsNearlyZero() && !B.IsNearlyZero())
	{
		const FVector An = A.GetSafeNormal();
		const FVector Bn = B.GetSafeNormal();
		SpineRad = FMath::Atan2(FVector::DotProduct(FVector::CrossProduct(An, Bn), BendAxisW), FVector::DotProduct(An, Bn));
	}
	SpineRad = FMath::Clamp(SpineRad, FMath::DegreesToRadians(-115.0f), FMath::DegreesToRadians(115.0f));
	RotateBoneCS(BowBody, TEXT("Spine"), FQuat(FVector(1, 0, 0), SpineRad));
	BowBody->RefreshBoneTransforms();

	// 彎腰半徑不足以抵達 HeadTarget 時，整個 BowBody 補位湊過去（上半身探出去的誇張感）
	{
		const FVector HeadAfterSpine = BowBody->GetBoneTransformByName(TEXT("Head"), EBoneSpaces::WorldSpace).GetLocation();
		const FVector Gap = HeadTarget - HeadAfterSpine;
		BowBody->AddWorldOffset(Gap);
	}

	// --- 偷瞄（Shift）＝頭自由硬轉向受害者的臉（轆轤首制 2026-07-16 二波：頭身間由
	// 每幀生成的脖子銜接，頭部相對運動不再有切口約束——舊「繞切盤軸投影」退役）。
	// 美術語言＝程式化硬轉；上限 95°（貓頭鷹護欄，貼上限硬轉不平滑）。
	// 作畫時頭仍隨脊椎彎折自然朝下（埋頭式；鏡頭由 UpdateLeanCamera 對準墨點）。
	if (bPeeking)
	{
		const FQuat SpineQ(FVector(1, 0, 0), SpineRad);
		const FVector FaceDirCS = SpineQ.RotateVector(FVector(0, 1, 0));
		const FVector HeadPosNow = BowBody->GetBoneTransformByName(TEXT("Head"), EBoneSpaces::WorldSpace).GetLocation();
		const FVector DesiredCS = CompT.InverseTransformVectorNoScale(
			(GetLeanFaceTargetWorld() - HeadPosNow).GetSafeNormal());
		if (!FaceDirCS.IsNearlyZero() && !DesiredCS.IsNearlyZero())
		{
			FQuat PeekQ = FQuat::FindBetweenNormals(FaceDirCS, DesiredCS.GetSafeNormal());
			FVector Axis;
			float Angle;
			PeekQ.ToAxisAndAngle(Axis, Angle);
			PeekQ = FQuat(Axis, FMath::Min(Angle, FMath::DegreesToRadians(95.0f)));
			const FVector PivotCS =
				BowBody->GetBoneTransformByName(TEXT("Head"), EBoneSpaces::ComponentSpace).GetLocation();
			for (const TCHAR* BoneName : { TEXT("Neck"), TEXT("Head") })
			{
				FTransform BoneCS = BowBody->GetBoneTransformByName(BoneName, EBoneSpaces::ComponentSpace);
				BoneCS.SetLocation(PivotCS + PeekQ.RotateVector(BoneCS.GetLocation() - PivotCS));
				BoneCS.SetRotation(PeekQ * BoneCS.GetRotation());
				BowBody->SetBoneTransformByName(BoneName, BoneCS, EBoneSpaces::ComponentSpace);
				BowBody->RefreshBoneTransforms(); // 寫姿勢→讀骨骼之間必須刷新（poseable 快取陷阱）
			}
		}
	}
	BowBody->RefreshBoneTransforms(); // 收尾：鏡頭/實體筆同 tick 讀頭骨要拿到最終姿勢
}

bool ANiceInkCharacter::GetEvidenceUVForHit(FName BoneName, const FVector& ImpactPoint, FVector2D& OutUV)
{
	if (!Body)
	{
		return false;
	}

	// 骨頭 → 站姿本地座標的粗錨點（sumo 半蹲量測，Blender y 取負：UE 本地臉朝 +Y）
	FVector Local(0.0f, 25.0f, 128.0f); // 預設：胸口
	if (BoneName != NAME_None)
	{
		const FString Bone = BoneName.ToString();
		if (Bone.Contains(TEXT("Head")) || Bone.Contains(TEXT("Neck")))
		{
			Local = FVector(0.0f, 18.0f, 152.0f);
		}
		else if (Bone.Contains(TEXT("Hips")) || Bone == TEXT("Spine"))
		{
			Local = FVector(0.0f, 30.0f, 90.0f);
		}
		else if (Bone.Contains(TEXT("Arm")) || Bone.Contains(TEXT("Hand")))
		{
			Local = FVector(Bone.StartsWith(TEXT("Left")) ? 55.0f : -55.0f, 0.0f, 110.0f);
		}
		else if (Bone.Contains(TEXT("UpLeg")))
		{
			Local = FVector(Bone.StartsWith(TEXT("Left")) ? 25.0f : -25.0f, 5.0f, 60.0f);
		}
		else if (Bone.Contains(TEXT("Leg")) || Bone.Contains(TEXT("Foot")) || Bone.Contains(TEXT("Toe")))
		{
			Local = FVector(Bone.StartsWith(TEXT("Left")) ? 30.0f : -30.0f, 0.0f, 25.0f);
		}
	}
	else
	{
		// 膠囊命中（無骨名）：彎腰中臉在前傾低處——命中高度粗分部位
		const float RelZ = ImpactPoint.Z - (GetActorLocation().Z - 92.0f); // 相對腳底
		if (bLeanLocked ? RelZ > 55.0f : RelZ > 130.0f)
		{
			Local = FVector(0.0f, 18.0f, 152.0f); // 頭臉（彎腰時頭在半高）
		}
		else if (RelZ < 45.0f)
		{
			Local = FVector(0.0f, 10.0f, 40.0f); // 腿（半蹲雙腿開，容差抓內側）
		}
	}

	const FVector World = Body->GetComponentTransform().TransformPosition(Local);
	return Body->ResolveBodyUV(World, OutUV, 30.0f);
}

void ANiceInkCharacter::UpdatePenVisual()
{
	if (!PenMesh)
	{
		return;
	}

	bool bShow = false;
	if (bLeanLocked)
	{
		ANiceInkCharacter* Target = LeanTarget.Get();
		FVector2D UV;
		if (Target && Target->InkCanvas && Target->Body &&
			Target->InkCanvas->GetLastPointForAuthor(GetInkAuthorId(), UV))
		{
			FVector InkPos, SkinNormal;
			if (Target->Body->ResolveUVToWorldWithNormal(UV, InkPos, SkinNormal))
			{
				// 筆尖釘在墨點上（畫布真相＝零 offset）；筆身指向自己的頭＝像被握著
				FVector HeadPos = GetActorLocation() + FVector(0, 0, 40.0f);
				if (BowBody && BowBody->GetSkinnedAsset() && BowBody->IsVisible())
				{
					HeadPos = BowBody->GetBoneTransformByName(TEXT("Head"), EBoneSpaces::WorldSpace).GetLocation();
				}
				FVector ShaftDir = (HeadPos - InkPos).GetSafeNormal();
				if (ShaftDir.IsNearlyZero())
				{
					ShaftDir = SkinNormal;
				}
				constexpr float PenHalfLen = 7.5f; // 15cm 筆，圓柱 pivot 在中心
				PenMesh->SetWorldLocationAndRotation(InkPos + ShaftDir * PenHalfLen,
					FRotationMatrix::MakeFromZ(ShaftDir).Rotator());
				bShow = true;
			}
		}
	}

	if (PenMesh->IsVisible() != bShow)
	{
		PenMesh->SetVisibility(bShow);
	}
}

void ANiceInkCharacter::UpdateSleepBodyDouble(float DeltaSeconds)
{
	// 睡姿替身（2026-07-15；環繞軌道制 2026-07-16 user 定案；同日二波＝轆轤首伸縮脖）：
	// - 頭部姿勢＝軌道(φ)：兩段式（縮下巴段＋巡航掃方位段，θ 隨方位調變）＋量測抬頭表
	//   ——網格已沿 user 手標 cut_seam_head 真切開（頭殼 Head=1.0 硬權重），
	//   頭與身之間由 UNeckStretch 每幀生成的脖子區銜接（頭部運動無切口約束）
	// - 相機＝眉心錨點、朝向＝臉的剛體朝向（含 roll）——看得到的 ≡ 臉表達的
	//   ≡ 旁人讀到的破綻（視線制的頭頂盲區洩漏由構造封死）
	// 靜態 Body 碰撞照舊＝畫墨/貼臉鎖定/噴射的 UV 解算不動。
	// 每 tick 冪等重申視形（ForceExitLean 等會經 ResetBowPose 動到替身狀態）。
	if (!Body || !BowBody)
	{
		return;
	}
	if (bAsleep && !bSleepDoubleActive && EnsureBowBodyAsset())
	{
		bSleepDoubleActive = true;
		// 捕捉參考姿勢（分析式擺骨的基底；姿勢快取歸零）
		BowBody->ResetBoneTransformByName(TEXT("Neck"));
		BowBody->ResetBoneTransformByName(TEXT("Head"));
		BowBody->RefreshBoneTransforms();
		SleepNeckRefCS = BowBody->GetBoneTransformByName(TEXT("Neck"), EBoneSpaces::ComponentSpace);
		SleepHeadRefCS = BowBody->GetBoneTransformByName(TEXT("Head"), EBoneSpaces::ComponentSpace);
		bSleepRefCaptured = true;
		bSleepPoseDirty = true;
	}
	else if (!bAsleep && bSleepDoubleActive)
	{
		bSleepDoubleActive = false;
		bRemoteOrbitSnap = true; // 下次入睡他端顯示角重新對齊
		BowBody->SetVisibility(false);
		BowBody->SetOwnerNoSee(true); // 還給 lean-lock 的預設（彎腰不演給自己看）
		BowBody->SetRelativeLocationAndRotation(BodyStandRelLoc, BodyStandRelRot);
		BowBody->ResetBoneTransformByName(TEXT("Neck"));
		BowBody->ResetBoneTransformByName(TEXT("Head"));
		Body->SetVisibility(true);
		Body->SetOnlyOwnerSee(false);
		Body->SetOwnerNoSee(true); // 站姿恢復第一人稱慣例
		if (IsLocallyControlled() && bWakeGazeActive)
		{
			DeactivateWakeGaze(); // FOV 還原 90（現身/相位收束）
		}
	}
	if (!bSleepDoubleActive)
	{
		return;
	}

	// 一具替身、全員可見（含本人）。姿態模型（2026-07-16 環繞軌道定案）：
	// φ＝繞切面法線軸（過切面錨點）的剛轉——切開的上殼沿斷面乾淨滑移；
	// 抬頭＝世界垂直投影進切面的面內平移（面內平移＝合法滑移；沿法線平移會開氣縫）。
	Body->SetVisibility(false); // 碰撞保留：畫墨/lean/噴射 UV 解算照打
	BowBody->SetVisibility(true);
	BowBody->SetOwnerNoSee(false);
	// 跟隨睡姿 transform（含翻身的背面姿勢與 Z 補償——ApplySleepVisual 是唯一真相）
	BowBody->SetRelativeLocationAndRotation(Body->GetRelativeLocation(), Body->GetRelativeRotation());

	// 本人端：睜眼＝軌道啟停（φ 由 PollSleepHead 滑鼠累積）
	const FTransform CompT = BowBody->GetComponentTransform();
	if (IsLocallyControlled())
	{
		if (bEyesOpen)
		{
			if (!bWakeGazeActive)
			{
				ActivateWakeGaze(CompT);
			}
		}
		else if (bWakeGazeActive)
		{
			DeactivateWakeGaze();
		}
	}

	// 姿態來源（本人＝本地狀態零延遲；他端＝複製值的平滑追趕——20Hz 節流無插值
	// ＝每包跳格抽動）；抬升＝指向的純函數各端自導
	float AimAz = SleepAimAzLocal;
	float AimTilt = SleepAimTiltLocal;
	if (!IsLocallyControlled())
	{
		if (bRemoteOrbitSnap || !bEyesOpen)
		{
			RemoteAimAzDeg = SleepAimAzDeg; // 啟用瞬間/閉眼＝直接對齊（無甩入）
			RemoteAimTiltDeg = SleepAimTiltDeg;
			bRemoteOrbitSnap = false;
		}
		else
		{
			const float K = FMath::Clamp(DeltaSeconds * 12.0f, 0.0f, 1.0f);
			const float DAz = FMath::FindDeltaAngleDegrees(RemoteAimAzDeg, SleepAimAzDeg);
			RemoteAimAzDeg = FMath::Fmod(FMath::Fmod(RemoteAimAzDeg + DAz * K, 360.0f) + 360.0f, 360.0f);
			RemoteAimTiltDeg += (SleepAimTiltDeg - RemoteAimTiltDeg) * K;
		}
		AimAz = RemoteAimAzDeg;
		AimTilt = RemoteAimTiltDeg;
	}

	// 指向 frame（CS、世界錨定）：up＝世界垂直、feet＝頭頂方向水平投影的反向；
	// 姿勢＝先縮下巴（臉朝腳側轉 tilt，軸=up×feet）再繞 up 轉到方位 az——
	// 量測掃描的鏡像實作（趴姿共用：up 相對身體自動翻轉，碰撞剖面近似記帳）。
	FQuat OrbitQ = FQuat::Identity; // 閉眼＝頭不動（盲瞄不成為破綻）
	FVector LiftCS = FVector::ZeroVector;
	if (bEyesOpen)
	{
		const FVector UpCS = CompT.InverseTransformVectorNoScale(FVector::UpVector).GetSafeNormal();
		FVector FeetCS = -(FVector::ZAxisVector - FVector::DotProduct(FVector::ZAxisVector, UpCS) * UpCS);
		if (!FeetCS.Normalize())
		{
			FeetCS = -FVector::ZAxisVector; // 退化保底（身體直立時不會走到睡姿路徑）
		}
		const FVector PitchAxis = FVector::CrossProduct(UpCS, FeetCS).GetSafeNormal();
		OrbitQ = FQuat(UpCS, FMath::DegreesToRadians(AimAz - 180.0f)) *
			FQuat(PitchAxis, FMath::DegreesToRadians(AimTilt));
		const float TotalRotDeg = FMath::RadiansToDegrees(OrbitQ.GetAngle());
		LiftCS = UpCS * SleepAimLiftCm(AimTilt, TotalRotDeg);
	}

	// 止血：姿態沒變不寫骨——每 tick 歸零重擺＝渲染器眼中的高速假移動＝動態模糊糊臉
	if (bSleepRefCaptured &&
		(bSleepPoseDirty || AimAz != LastPoseAz || AimTilt != LastPoseTilt ||
		 bEyesOpen != bLastPoseEyes))
	{
		bSleepPoseDirty = false;
		LastPoseAz = AimAz;
		LastPoseTilt = AimTilt;
		bLastPoseEyes = bEyesOpen;

		// 剛體擺骨：Neck/Head 同一 Δ（頭殼硬權重全在 Head；Neck 同步只為骨鏈一致）——
		// 樞軸＝Head 骨 rest 位置（量測掃描的 P0）、骨位繞樞軸剛轉＋世界垂直抬升
		const FVector HeadPivotCS = SleepHeadRefCS.GetLocation();
		auto RigidJointPose = [&](const FTransform& RefCS)
		{
			FTransform T = RefCS;
			T.SetLocation(HeadPivotCS +
				OrbitQ.RotateVector(RefCS.GetLocation() - HeadPivotCS) + LiftCS);
			T.SetRotation(OrbitQ * RefCS.GetRotation());
			return T;
		};
		BowBody->SetBoneTransformByName(TEXT("Neck"), RigidJointPose(SleepNeckRefCS), EBoneSpaces::ComponentSpace);
		BowBody->RefreshBoneTransforms(); // 頭骨 CS 寫入的父鏈快取要先更新（poseable 快取陷阱）
		BowBody->SetBoneTransformByName(TEXT("Head"), RigidJointPose(SleepHeadRefCS), EBoneSpaces::ComponentSpace);
		BowBody->RefreshBoneTransforms();
	}

	// 本人相機＝眉心錨點、朝向＝臉的剛體朝向（含 roll——φ=±90 世界側倒、180 顛倒
	// ＝臉頰貼枕的真實視感；user 定案「相機嚴格放在眉心、與臉部朝向一致」，
	// 任何相機層打折都是對訊號恆等式的背叛）
	if (IsLocallyControlled())
	{
		const FQuat PoseQ = bEyesOpen ? OrbitQ : FQuat::Identity;
		const FVector FaceDirW = CompT.TransformVectorNoScale(PoseQ.RotateVector(FVector::YAxisVector)).GetSafeNormal();
		const FVector CrownDirW = CompT.TransformVectorNoScale(PoseQ.RotateVector(FVector::ZAxisVector)).GetSafeNormal();
		const FVector HeadPos = BowBody->GetBoneTransformByName(TEXT("Head"), EBoneSpaces::WorldSpace).GetLocation();
		const FVector CamPos = HeadPos + FaceDirW * 13.0f + CrownDirW * 8.0f;

		if (bEyesOpen && bWakeGazeActive)
		{
			const FQuat CamQ = FRotationMatrix::MakeFromXZ(FaceDirW, CrownDirW).ToQuat();
			FirstPersonCamera->SetWorldLocationAndRotation(CamPos, CamQ);
		}
		else
		{
			// 閉眼盲瞄：瞄準方向＝盲瞄姿態組合方向（骨頭不動、相機隱形轉、
			// 畫面被迷宮蓋著）——Q 噴射讀相機 yaw
			const float TwistRad = FMath::DegreesToRadians(SleepTwistLocal);
			const FVector XAfterTwist = FQuat(FVector::ZAxisVector, TwistRad).RotateVector(FVector::XAxisVector);
			const FQuat FullQ = FQuat(XAfterTwist, FMath::DegreesToRadians(-SleepBendLocal)) *
				FQuat(FVector::ZAxisVector, TwistRad);
			const FVector AimDirW = CompT.TransformVectorNoScale(FullQ.RotateVector(FVector::YAxisVector)).GetSafeNormal();
			const FVector AimCrownW = CompT.TransformVectorNoScale(FullQ.RotateVector(FVector::ZAxisVector)).GetSafeNormal();
			const FQuat CamQ = FRotationMatrix::MakeFromXZ(AimDirW, AimCrownW).ToQuat();
			FirstPersonCamera->SetWorldLocationAndRotation(CamPos, CamQ);
		}
	}
}

void ANiceInkCharacter::ActivateWakeGaze(const FTransform& CompT)
{
	// 睜眼瞬間：指向從安睡朝向（az=180 腳側、tilt=0 朝天、旋轉恆等）起步——
	// 閉眼期間骨頭沒動過＝零瞬移、零系統代打（無聲甦醒的破綻只能出自玩家自己的滑鼠）
	bWakeGazeActive = true;
	SleepAimAzLocal = 180.0f;
	SleepAimTiltLocal = 0.0f;
	LastSentAimAz = 180.0f;
	LastSentAimTilt = 0.0f;
	if (FirstPersonCamera)
	{
		FirstPersonCamera->SetFieldOfView(SleepWakeFov); // 廣角＝躺地視點的壓迫感（貼近的作畫者變巨大）
	}
	bSleepPoseDirty = true;
}

void ANiceInkCharacter::DeactivateWakeGaze()
{
	bWakeGazeActive = false;
	if (FirstPersonCamera)
	{
		FirstPersonCamera->SetFieldOfView(90.0f); // 站姿預設
	}
}

float ANiceInkCharacter::SleepAimLiftCm(float TiltDeg, float TotalRotDeg) const
{
	// 抬升＝純函數（量測定案 2026-07-16 v8，Blender 全域 (az,tilt) 網格驗證零洞）：
	// - tilt 立即墊高（下巴一低就要離胸：0→12° 內登頂 46）；
	// - 純 yaw 給 2° 寬限後墊高（±5° 微 yaw 在零抬升會讓垂肉擦過胸摺線——刀口實測；
	//   頭在枕上大角度旋轉＝垂肉掠肩，必須先升）；
	// - 之後恆高（平台制＝掃視時脖長恆定，2026-07-16 user 抓「一下長一下短」的修法）。
	auto SmoothStep = [](float X)
	{
		X = FMath::Clamp(X, 0.0f, 1.0f);
		return X * X * (3.0f - 2.0f * X);
	};
	const float Rise = FMath::Max(SmoothStep(TiltDeg / 12.0f),
		SmoothStep((TotalRotDeg - 2.0f) / 14.0f));
	return SleepOrbitLiftScale * 46.0f * Rise;
}

float ANiceInkCharacter::SleepAimMaxTiltDeg(float AzDeg)
{
	// 每方位最大俯角（每 5° 一格、線性內插）——量測定案 2026-07-16 v8：
	// 全網格 BVH（頭殼 1310 頂點×射線奇偶＋地板＋環間淨空）在上面的抬升規則下逐點驗證，
	// 全域零洞。腳側（az≈180）100°＝下巴/胸極限；側與頭頂側 105–130°（掃描上限 130）。
	static const float Table[72] = {
		130.0f, 130.0f, 130.0f, 130.0f, 130.0f, 130.0f, 130.0f, 130.0f, 130.0f, 130.0f,
		130.0f, 130.0f, 130.0f, 130.0f, 130.0f, 130.0f, 130.0f, 130.0f, 130.0f, 130.0f,
		130.0f, 130.0f, 130.0f, 125.0f, 122.5f, 120.0f, 115.0f, 112.5f, 110.0f, 107.5f,
		105.0f, 105.0f, 102.5f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f,
		102.5f, 105.0f, 105.0f, 107.5f, 110.0f, 112.5f, 115.0f, 120.0f, 122.5f, 125.0f,
		130.0f, 130.0f, 130.0f, 130.0f, 130.0f, 130.0f, 130.0f, 130.0f, 130.0f, 130.0f,
		130.0f, 130.0f, 130.0f, 130.0f, 130.0f, 130.0f, 130.0f, 130.0f, 130.0f, 130.0f,
		130.0f, 130.0f
	};
	const float Az = FMath::Fmod(FMath::Fmod(AzDeg, 360.0f) + 360.0f, 360.0f);
	const float F = Az / 5.0f;
	const int32 I0 = FMath::Clamp(FMath::FloorToInt(F), 0, 71);
	const int32 I1 = (I0 + 1) % 72;
	return FMath::Lerp(Table[I0], Table[I1], F - static_cast<float>(I0));
}

void ANiceInkCharacter::ServerUpdateSleepAim_Implementation(float AzDeg, float TiltDeg)
{
	const ANiceInkGameState* GS = GetWorld() ? GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
	const APlayerState* PS = GetPlayerState();
	if (!GS || !PS || PS->GetPlayerId() != GS->VictimPlayerId || !bAsleep || !bEyesOpen)
	{
		return;
	}
	SleepAimAzDeg = FMath::Fmod(FMath::Fmod(AzDeg, 360.0f) + 360.0f, 360.0f);
	SleepAimTiltDeg = FMath::Clamp(TiltDeg, 0.0f, SleepAimMaxTiltDeg(SleepAimAzDeg));
}

void ANiceInkCharacter::ResetBowPose()
{
	bSleepPoseDirty = true; // 替身骨可能被動過——下次 tick 重擺
	if (BowBody)
	{
		BowBody->SetVisibility(false);
		BowBody->SetRelativeLocationAndRotation(BodyStandRelLoc, BodyStandRelRot); // 清掉補位滑移
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

	// 鏡頭長在臉上：姿勢是唯一真相——彎多深＝看得多低、臉對哪＝看向哪。
	// 偷瞄＝頭真的轉過去，第一人稱畫面自然跟著甩向受害者的臉。
	FVector EyePos;
	FVector AimTarget = bPeeking ? GetLeanFaceTargetWorld() : FVector(LeanPoint);
	if (BowBody && BowBody->GetSkinnedAsset())
	{
		const FVector HeadPos = BowBody->GetBoneTransformByName(TEXT("Head"), EBoneSpaces::WorldSpace).GetLocation();
		const FVector Dir = (AimTarget - HeadPos).GetSafeNormal();
		EyePos = HeadPos + Dir * 12.0f; // 眼窩在頭骨往視線方向前移
	}
	else
	{
		// 無骨骼資產的退路：貼皮膚定位（舊法）
		EyePos = FVector(LeanPoint) + FVector(LeanNormal).GetSafeNormal() * (LeanCameraHeight - 14.0f);
	}
	Cam->SetActorLocationAndRotation(EyePos, (AimTarget - EyePos).Rotation());

	if (!bLeanCamActive)
	{
		PC->SetViewTargetWithBlend(Cam, 0.18f, VTBlend_Linear);
		bLeanCamActive = true;
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
	// 只有主機（listen server 本人）能開局——客戶端的 NiStart/ENTER 一律拒絕
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->IsLocalController())
	{
		return;
	}
	if (ANiceInkGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ANiceInkGameMode>() : nullptr)
	{
		GM->RequestStartMatch();
	}
}

void ANiceInkCharacter::UpdateWalkAnim(float DeltaSeconds)
{
	// 站立移動時 Body 疊步伐；睡姿/貼臉鎖定（Body 隱藏或被睡姿接管）一律不碰。
	if (!Body)
	{
		return;
	}
	const bool bEligible = bWalkAnimEnabled && !bAsleep && !bLeanLocked && Body->IsVisible();
	const float Speed2D = bEligible ? GetVelocity().Size2D() : 0.0f;

	if (Speed2D < 20.0f)
	{
		if (bWalkAnimApplied)
		{
			bWalkAnimApplied = false;
			WalkAnimPhase = 0.0f;
			// 停步＝硬還原站姿基準（美術語言：硬切）。
			// 例外：睡姿已接管 Body 變換（lie/prone），還原會把睡姿蓋掉——
			// 甦醒路徑自己會寫回站姿。lean 只藏不動 Body，照樣還原。
			if (!bAsleep)
			{
				Body->SetRelativeLocationAndRotation(BodyStandRelLoc, BodyStandRelRot);
			}
		}
		return;
	}

	const float MaxSpeed = GetCharacterMovement() ? FMath::Max(1.0f, GetCharacterMovement()->MaxWalkSpeed) : 300.0f;
	const float SpeedRatio = FMath::Clamp(Speed2D / MaxSpeed, 0.0f, 1.0f);
	const float StepsPerSecond = 2.0f + 1.4f * SpeedRatio; // 力士碎步
	WalkAnimPhase = FMath::Fmod(WalkAnimPhase + DeltaSeconds * StepsPerSecond, 1.0f);

	// 三角波側傾（線性折返＝硬轉、不做平滑正弦）＋步點彈跳
	const float Tri = 4.0f * FMath::Abs(WalkAnimPhase - 0.5f) - 1.0f; // -1..1..-1
	const float RollDeg = WalkWaddleDeg * Tri * SpeedRatio;
	const float BobZ = WalkBobCm * (1.0f - FMath::Abs(Tri)) * SpeedRatio;

	// 側傾繞角色前軸（父空間左乘）；基準相對變換每幀重組＝零累積
	const FQuat WaddleQ(FRotator(0.0f, 0.0f, RollDeg));
	Body->SetRelativeLocationAndRotation(
		BodyStandRelLoc + FVector(0.0f, 0.0f, BobZ),
		(WaddleQ * FQuat(BodyStandRelRot)).Rotator());
	bWalkAnimApplied = true;
}

void ANiceInkCharacter::UpdateLeanArm()
{
	static const FName ArmBone(TEXT("RightArm"));
	static const FName ForeBone(TEXT("RightForeArm"));
	static const FName HandBone(TEXT("RightHand"));

	const bool bActive = bPenArmIkEnabled && bLeanLocked && BowBody && BowBody->IsVisible();
	if (!bActive)
	{
		if (bLeanArmApplied && BowBody)
		{
			BowBody->ResetBoneTransformByName(ArmBone);
			BowBody->ResetBoneTransformByName(ForeBone);
			BowBody->ResetBoneTransformByName(HandBone);
			BowBody->RefreshBoneTransforms();
			bLeanArmApplied = false;
		}
		return;
	}
	if (BowBody->GetBoneIndex(ArmBone) == INDEX_NONE ||
		BowBody->GetBoneIndex(ForeBone) == INDEX_NONE ||
		BowBody->GetBoneIndex(HandBone) == INDEX_NONE)
	{
		return; // 骨架缺鏈＝安靜跳過（姿勢不演，機制照跑）
	}

	// 每 tick 重置→解 IK（冪等；姿勢寫→讀之間必須 RefreshBoneTransforms——poseable 快取陷阱）
	BowBody->ResetBoneTransformByName(ArmBone);
	BowBody->ResetBoneTransformByName(ForeBone);
	BowBody->ResetBoneTransformByName(HandBone);
	BowBody->RefreshBoneTransforms();

	const FTransform ArmT = BowBody->GetBoneTransformByName(ArmBone, EBoneSpaces::WorldSpace);
	const FTransform ForeT = BowBody->GetBoneTransformByName(ForeBone, EBoneSpaces::WorldSpace);
	const FTransform HandT = BowBody->GetBoneTransformByName(HandBone, EBoneSpaces::WorldSpace);
	const FVector S = ArmT.GetLocation();
	const FVector E = ForeT.GetLocation();
	const FVector H = HandT.GetLocation();

	// 目標＝實體筆（筆尖已釘在墨點；未落筆時退回鎖定點）；握上段＝往筆桿上抬 6cm
	FVector Target = (PenMesh && PenMesh->IsVisible())
		? PenMesh->GetComponentLocation() + FVector(0, 0, 6.0f)
		: FVector(LeanPoint);

	const float UpperLen = FVector::Dist(S, E);
	const float LowerLen = FVector::Dist(E, H);
	FVector ToTarget = Target - S;
	const float Dist = FMath::Clamp(static_cast<float>(ToTarget.Size()),
		FMath::Abs(UpperLen - LowerLen) + 1.0f, (UpperLen + LowerLen) * 0.999f);
	const FVector N = ToTarget.GetSafeNormal();
	if (N.IsNearlyZero() || UpperLen < 1.0f || LowerLen < 1.0f)
	{
		return;
	}

	// 肘極向：右外側（本地 -X＝右，GetEvidenceUVForHit 同一慣例）偏向世界下
	const FTransform CompT = BowBody->GetComponentTransform();
	FVector Pole = CompT.TransformVectorNoScale(FVector(-1, 0, 0)) - FVector(0, 0, 0.6f);
	Pole = (Pole - FVector::DotProduct(Pole, N) * N);
	if (!Pole.Normalize())
	{
		Pole = FVector::CrossProduct(N, FVector::UpVector).GetSafeNormal();
	}

	// 兩骨解析解（餘弦定理）：肘的面內位置
	const float CosShoulder = FMath::Clamp(
		(UpperLen * UpperLen + Dist * Dist - LowerLen * LowerLen) / (2.0f * UpperLen * Dist), -1.0f, 1.0f);
	const float SinShoulder = FMath::Sqrt(FMath::Max(0.0f, 1.0f - CosShoulder * CosShoulder));
	const FVector NewElbow = S + N * (UpperLen * CosShoulder) + Pole * (UpperLen * SinShoulder);
	const FVector NewHand = S + N * Dist;

	// 上臂：把 S→E 轉到 S→NewElbow（世界空間旋轉、位置不動）
	const FQuat Q1 = FQuat::FindBetweenNormals((E - S).GetSafeNormal(), (NewElbow - S).GetSafeNormal());
	FTransform NewArmT = ArmT;
	NewArmT.SetRotation(Q1 * ArmT.GetRotation());
	BowBody->SetBoneTransformByName(ArmBone, NewArmT, EBoneSpaces::WorldSpace);
	BowBody->RefreshBoneTransforms();

	// 前臂：轉完上臂後重讀（子骨已被帶走），再把 E'→H' 轉向 E'→NewHand
	const FTransform ForeT2 = BowBody->GetBoneTransformByName(ForeBone, EBoneSpaces::WorldSpace);
	const FTransform HandT2 = BowBody->GetBoneTransformByName(HandBone, EBoneSpaces::WorldSpace);
	const FVector E2 = ForeT2.GetLocation();
	const FQuat Q2 = FQuat::FindBetweenNormals(
		(HandT2.GetLocation() - E2).GetSafeNormal(), (NewHand - E2).GetSafeNormal());
	FTransform NewForeT = ForeT2;
	NewForeT.SetRotation(Q2 * ForeT2.GetRotation());
	BowBody->SetBoneTransformByName(ForeBone, NewForeT, EBoneSpaces::WorldSpace);
	BowBody->RefreshBoneTransforms();

	bLeanArmApplied = true;
}

float ANiceInkCharacter::EffectiveLookSensitivity() const
{
	const UNiceInkGameInstance* GI = UNiceInkGameInstance::Get(this);
	return LookSensitivity * (GI ? GI->GetMouseScale() : 1.0f);
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

void ANiceInkCharacter::NiMazeStats(int32 NumSeeds, int32 Cup)
{
	// 參數來源：listen host 有 GameMode（含 ini 覆寫）；純 client 用內建預設檔
	FDreamMazeParams Params = FDreamMazeGen::DefaultParamsForCup(Cup);
	if (const ANiceInkGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ANiceInkGameMode>() : nullptr)
	{
		if (GM->MazeParamsPerCup.IsValidIndex(FMath::Clamp(Cup, 0, GM->MazeParamsPerCup.Num() - 1)))
		{
			Params = GM->MazeParamsPerCup[FMath::Clamp(Cup, 0, GM->MazeParamsPerCup.Num() - 1)];
		}
	}
	const FString Report = FDreamMazeGen::RunStats(Params, NumSeeds > 0 ? NumSeeds : 1000, /*TrapCount=*/4);
	UE_LOG(LogTemp, Display, TEXT("%s"), *Report);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(9137, 12.0f, FColor::Cyan, Report);
	}
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
