#include "NiceInkMenuStage.h"

#include "AIController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InkBodyComponent.h"
#include "InkCanvasComponent.h"
#include "InkTypes.h"
#include "NiceInkCharacter.h"
#include "NiceInkGameInstance.h"
#include "NiceInkPersonaSubsystem.h"
#include "NiceInkSaveGame.h"

ANiceInkMenuStage::ANiceInkMenuStage()
{
	PrimaryActorTick.bCanEverTick = true;

	// 隱形地板：角色 CharacterMovement 需要落腳處（頂面＝Z0）
	Floor = CreateDefaultSubobject<UBoxComponent>(TEXT("Floor"));
	SetRootComponent(Floor);
	Floor->SetBoxExtent(FVector(1200.0f, 1200.0f, 50.0f));
	Floor->SetRelativeLocation(FVector(0, 0, -50.0f));
	Floor->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Floor->SetCollisionResponseToAllChannels(ECR_Block);
	Floor->SetHiddenInGame(true);
}

void ANiceInkMenuStage::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// --- 力士替身（墨水 RT 壓 512：選單不畫畫、別佔 800MB）---
	// 出生點=玩家視角最左（screen-left=world +Y）：舞步規格「從最左開始」
	const FTransform SpawnT(FRotator(0, FaceCameraYaw, 0), FVector(0, SwayCm, 120.0f));
	Dancer = World->SpawnActorDeferred<ANiceInkCharacter>(ANiceInkCharacter::StaticClass(), SpawnT,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Dancer)
	{
		if (UInkCanvasComponent* Canvas = Dancer->FindComponentByClass<UInkCanvasComponent>())
		{
			Canvas->RenderTargetResolution = 512;
			Canvas->MistRenderTargetResolution = 512;
		}
		Dancer->FinishSpawning(SpawnT);

		// avatar＝玩家偏好（未選＝0 號）；臉直接指定（無 PlayerState）
		int32 AvatarIdx = 0;
		if (const UNiceInkGameInstance* GI = Cast<UNiceInkGameInstance>(World->GetGameInstance()))
		{
			AvatarIdx = GI->PreferredAvatar != INDEX_NONE ? GI->PreferredAvatar : 0;
		}
		Dancer->SetupAsMenuDummy(AvatarIdx);

		// AI 附身＝取得 MoveToLocation 直驅（無尋路、無 navmesh）。
		// 朝向不能自己每幀寫——AIController::UpdateControlRotation 每 tick 會用
		// focal point 覆寫（跟它打架＝朝向看時序亂跳，user 抓到背對實錘）；
		// 正解＝把 focal point 釘在鏡頭＝AI 原生「邊走邊盯一點」＝恆面對玩家側身滑步
		DancerAI = World->SpawnActor<AAIController>(AAIController::StaticClass(), FVector::ZeroVector,
			FRotator::ZeroRotator, Params);
		if (DancerAI)
		{
			DancerAI->Possess(Dancer);
		}
		if (UCharacterMovementComponent* Move = Dancer->GetCharacterMovement())
		{
			// 速度：一側到另一側（2×SwayCm）要在 BeatsPerSide 拍內走完、留 15% 餘裕
			Move->MaxWalkSpeed = (2.0f * SwayCm) / FMath::Max(0.15f, BeatsPerSide * BeatSec) * 1.15f;
			Move->MaxAcceleration = 1600.0f; // 起步利落＝彈跳激勵乾脆
		}
	}

	// --- 相機（+X 往回看；角色面向 +X＝面向鏡頭）---
	// 俯角 2°：中心射線落在角色平面 88cm 高→腳投影在畫面 ~85%、頭 ~18%
	// ＝地面線入鏡、下方空白=地板；頭離開標題帶（俯角 5° 時腳吊在 70%=下方真空）
	Camera = World->SpawnActor<ACameraActor>(FVector(CamDistCm, 0, CamHeightCm),
		FRotator(-2.0f, 180.0f, 0), Params);
	if (Camera && Camera->GetCameraComponent())
	{
		UCameraComponent* Cam = Camera->GetCameraComponent();
		Cam->bConstrainAspectRatio = false;
		Cam->SetFieldOfView(CamFovDeg);
		// 鎖曝光：空景+人物的亮度不能被自動曝光泵動
		Cam->PostProcessSettings.bOverride_AutoExposureMethod = true;
		Cam->PostProcessSettings.AutoExposureMethod = AEM_Manual;
		Cam->PostProcessSettings.bOverride_AutoExposureBias = true;
		Cam->PostProcessSettings.AutoExposureBias = ExposureBias;
		// 橫移力士＝動態模糊的頭號受害者（首輪截圖糊成一片實錘）——關
		Cam->PostProcessSettings.bOverride_MotionBlurAmount = true;
		Cam->PostProcessSettings.MotionBlurAmount = 0.0f;
	}
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		PC->SetViewTargetWithBlend(Camera, 0.0f);
	}
	if (DancerAI && Camera)
	{
		DancerAI->SetFocalPoint(Camera->GetActorLocation()); // 臉鎖鏡頭（見上）
	}

	// --- 無影平行光×2（fullbright 讀感：正面主光＋反向補光）---
	auto SpawnSun = [&](const FRotator& Rot, float Lux, int32 Priority)
	{
		ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(FVector(0, 0, 400), Rot, Params);
		if (Sun && Sun->GetLightComponent())
		{
			Sun->GetLightComponent()->SetCastShadows(false);
			Sun->GetLightComponent()->SetIntensity(Lux);
			// 雙平行光要分主從，否則引擎每幀洗「competing for forward shading」警告
			if (UDirectionalLightComponent* DL = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
			{
				DL->ForwardShadingPriority = Priority;
			}
		}
	};
	SpawnSun(FRotator(-28.0f, 200.0f, 0), KeyLux, 1);  // 從相機側往下打（主）
	SpawnSun(FRotator(-40.0f, 20.0f, 0), FillLux, 0);  // 反向補（從）
}

void ANiceInkMenuStage::DressDancerFromPersona()
{
	UNiceInkPersonaSubsystem* Persona = UNiceInkPersonaSubsystem::Get(this);
	if (!Persona || !Dancer)
	{
		return;
	}

	// 自訂臉：版本變了就重套（上傳自拍完成的瞬間，舞台力士當場換臉）
	if (Persona->HasCustomFace() && Persona->GetFaceRevision() != AppliedFaceRev)
	{
		if (UInkBodyComponent* Body = Dancer->FindComponentByClass<UInkBodyComponent>())
		{
			Body->ApplyCustomAvatar(Persona->GetFaceOpen(), Persona->GetFaceClosed(),
				Persona->GetEyeMaskInk(), Persona->GetCustomSkinTone());
			AppliedFaceRev = Persona->GetFaceRevision();
		}
	}

	// 雲端刺青：靜默登入拉到資產後，把碳黑/永久重播上替身畫布（一次性）
	if (!bCloudTattoosApplied)
	{
		if (UNiceInkSaveGame* Save = Persona->GetCloudSaveView())
		{
			if (UInkCanvasComponent* Canvas = Dancer->FindComponentByClass<UInkCanvasComponent>())
			{
				int32 Applied = 0;
				for (const FInkWork& Work : Save->Tattoos)
				{
					if (Work.State != EInkWorkState::Marker)
					{
						Canvas->RestoreWork(Work);
						++Applied;
					}
				}
				bCloudTattoosApplied = true;
				UE_LOG(LogTemp, Log, TEXT("NiStage: dancer dressed with %d cloud tattoos"), Applied);
			}
		}
	}
}

double ANiceInkMenuStage::BeatClock(const UWorld* World) const
{
	const UNiceInkGameInstance* GI = World ? Cast<UNiceInkGameInstance>(World->GetGameInstance()) : nullptr;
	const double Start = (GI && GI->GetBgmStartAudioTime() >= 0.0) ? GI->GetBgmStartAudioTime() : 0.0;
	double T = World->GetAudioTimeSeconds() - Start;
	if (LoopDurS > 1.0f)
	{
		T = FMath::Fmod(T, static_cast<double>(LoopDurS)); // loop≠整數拍：跨圈取模防漂
	}
	return T - BeatOffsetS; // 對齊拍網格相位
}

void ANiceInkMenuStage::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UWorld* World = GetWorld();
	if (!World || !Dancer || !DancerAI)
	{
		return;
	}

	// 相機接管保險：PC 晚於舞台出生時 BeginPlay 設不到（冪等）
	if (Camera)
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (PC->GetViewTarget() != Camera)
			{
				PC->SetViewTargetWithBlend(Camera, 0.0f);
			}
		}
	}

	DressDancerFromPersona(); // 自訂臉／雲端刺青到貨即穿上（輪詢冪等）

	const int32 Beat = FMath::Max(0, FMath::FloorToInt32(BeatClock(World) / FMath::Max(0.1f, BeatSec)));
	if (Beat != LastBeat)
	{
		LastBeat = Beat;

		// 變化拍（預設關；user 定案全程面對玩家）
		if (SpinEveryBeats > 0 && Beat > 0 && Beat % SpinEveryBeats == 0)
		{
			SpinYawRemaining = 360.0f;
		}

		// 四拍一循環（user 定案）：前 BeatsPerSide 拍＝由左到右（目標=玩家視角右
		// =world -Y）、後 BeatsPerSide 拍＝由右到左；出生在最左＝循環相位對齊
		const int32 Half = FMath::Max(1, BeatsPerSide);
		const bool bGoingRight = (Beat / Half) % 2 == 0;
		const float TargetY = bGoingRight ? -SwayCm : SwayCm;
		DancerAI->MoveToLocation(FVector(0, TargetY, Dancer->GetActorLocation().Z),
			/*AcceptanceRadius=*/12.0f, /*bStopOnOverlap=*/false, /*bUsePathfinding=*/false,
			/*bProjectDestinationToNavigation=*/false, /*bCanStrafe=*/true);
	}

	// 朝向＝focal point 制（AI 每 tick 自己面向鏡頭）；旋轉拍（預設關）時暫時
	// 收回 focus、手寫 yaw 掃一圈，掃完還 focus
	if (SpinYawRemaining > 0.0f)
	{
		const float Step = 360.0f / FMath::Max(0.2f, 2.0f * BeatSec) * DeltaSeconds;
		const float Used = FMath::Min(SpinYawRemaining, Step);
		CurrentYaw += Used;
		SpinYawRemaining -= Used;
		DancerAI->ClearFocus(EAIFocusPriority::Gameplay);
		DancerAI->SetControlRotation(FRotator(0, CurrentYaw, 0));
		if (SpinYawRemaining <= 0.0f && Camera)
		{
			DancerAI->SetFocalPoint(Camera->GetActorLocation());
		}
	}
}
