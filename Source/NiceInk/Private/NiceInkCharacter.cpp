#include "NiceInkCharacter.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "DreamMazeComponent.h"
#include "DreamTraceComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "SceneView.h"
#include "UnrealClient.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "DrawPoseData.h"
#include "InkBodyComponent.h"
#include "InkCanvasComponent.h"
#include "InkSprayProjectile.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "NeckStretchComponent.h"
#include "Net/UnrealNetwork.h"
#include "NiceInkAudio.h"
#include "NiceInkGameInstance.h"
#include "NiceInkGameMode.h"
#include "NiceInkGameState.h"
#include "NiceInkFaceShare.h"
#include "NiceInkPersonaSubsystem.h"
#include "NiceInkPlayerState.h"
#include "NiceInkSessionSubsystem.h"
#include "NiceInkTypes.h"
#include "Sound/SoundBase.h"
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
	// 入座距離＝目標高度的函數（07-20 三修）：Backup4 剛臂前向模型解析——
	// z(θ)=67.9+116.3sinθ−8.2cosθ、fwd(θ)=−29.7+116.3cosθ+8.2sinθ（θ=髖角）。
	// 「恆定 86」只在 ±12° 髖域成立；貼地目標髖摺 ~−30° 時水平投影縮到 ~70
	// ——常數 standoff＝低位入座永遠搆不到的真兇之一。折線取樣自解析曲線。
	float LeanStandoffForHeight(float TargetHCm)
	{
		const float H = FMath::Clamp(TargetHCm, 0.0f, 120.0f);
		if (H >= 60.0f)
		{
			return 86.0f;
		}
		if (H >= 40.0f)
		{
			return FMath::Lerp(84.0f, 86.0f, (H - 40.0f) / 20.0f);
		}
		if (H >= 20.0f)
		{
			return FMath::Lerp(77.0f, 84.0f, (H - 20.0f) / 20.0f);
		}
		return FMath::Lerp(69.0f, 77.0f, H / 20.0f);
	}
	constexpr float LeanPaintDelay = 0.25f;         // 鏡頭到位前不落筆
	constexpr float LeanCursorSpeed = 1.0f;         // 游標像素/滑鼠單位

	// 直接畫制 aim 域與剛臂姿勢解算（2026-07-20 user 定案「手就一直伸直就好」：
	// 手臂恆 Backup4 手勢，對齊靠 3-DOF（yaw/Hips/雙踝）解「筆尖=落墨點」）
	constexpr float DrawTiltMinDeg = -35.0f;   // 抬頭上限（看他的臉抓睜眼）
	constexpr float DrawTiltMaxDeg = 80.0f;    // 低頭下限（畫低位）
	constexpr float DrawHipDeltaClampDeg = 40.0f;   // Hips 前傾增量鉗位。±22 是我發明的保守值
	                                                // ＝貼地搆不到＋頭被迫折 75° 刺穿胸背的共同
	                                                // 根因（07-20 三輪 viewport）；畫貼地需要髖摺
	                                                // ~-35°＝「近乎趴下」——user 明示接受的姿勢
	constexpr float DrawAnkleDeltaClampDeg = 18.0f; // 雙踝重心搖增量鉗位（腳尖上前後搖）
	constexpr float DrawTipSolveTolCm = 1.5f;  // 筆尖=P 殘差容許（超過=搆不到=不落墨）
	constexpr float PenTipAheadCm = 9.0f;      // 校準：筆尖在握骨前方多遠（沿眉→手延長線）
	constexpr float DrawToePadCm = 4.0f;       // 落地補償的趾墊高

	// 子樹剛轉工具（以樞軸為心、含位置與旋轉；scale 不動）——ApplyBowPose 增量鏈用
	void RotSubtreeAboutPivotCS(const FReferenceSkeleton& Ref, TArray<FTransform>& CS,
		int32 RootIdx, const FQuat& Q, const FVector& PivotCS)
	{
		for (int32 i = 0; i < CS.Num(); ++i)
		{
			bool bIn = false;
			for (int32 A = i; A != INDEX_NONE; A = Ref.GetParentIndex(A))
			{
				if (A == RootIdx)
				{
					bIn = true;
					break;
				}
			}
			if (!bIn)
			{
				continue;
			}
			CS[i].SetLocation(PivotCS + Q.RotateVector(CS[i].GetLocation() - PivotCS));
			CS[i].SetRotation(Q * CS[i].GetRotation());
		}
	}

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

	// 08-14 同步卡頓根治③：FRepMovement 旋轉量化預設 8-bit/軸（1.4° 階梯）——
	// 滑鼠轉身在他端跳格。提到 16-bit（0.005°）；頻寬代價每包數 byte。
	GetReplicatedMovement_Mutable().RotationQuantizationLevel = ERotatorQuantization::ShortComponents;

	GetCapsuleComponent()->SetCapsuleSize(42.0f, 92.0f);
	// 準星描畫要打到身體網格，不是膠囊
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);

	GetCharacterMovement()->MaxWalkSpeed = 250.0f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2048.0f;

	// 08-14 同步卡頓根治②：CMC 的網路平滑（client 看 simulated proxy＋listen
	// server 看 client 的 pawn）只把平滑偏移寫在 ACharacter::Mesh 的相對變換上；
	// 本作可見身體是自訂元件、原掛 capsule 下＝每個網路修正原封硬跳在可見網格上
	//（乾淨 LAN 60Hz 看不見；EOS P2P 抖動/頻寬飽和把包距拉大＝可見瞬移，jiggle
	// 彈簧還把每次 snap 當激勵）。把沒資產的 Mesh 釘在 capsule 原點當平滑載體、
	// 可見身體改掛它下面＝免費繼承引擎 Exponential 平滑；本人端與 server 端偏移
	// 恆零＝畫墨/UV 解算/伺服器判定讀到的位置完全不變。
	GetMesh()->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Body = CreateDefaultSubobject<UInkBodyComponent>(TEXT("Body"));
	Body->SetupAttachment(GetMesh());
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
	BowBody->SetupAttachment(GetMesh()); // 同 Body：掛平滑載體（08-14 根治②）
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
	// 刺青機三件套（2026-07-22 伸縮分帳制、user 提供 Meshy 生成+Text-to-Texture）：
	// 機械體（pivot=握管頂）＋握管（可伸縮）＋針（可伸縮）——任一缺席整套退
	// 真麥克筆（尖端在原點、筆身 +Z、13cm），再退圓柱
	static ConstructorHelpers::FObjectFinder<UStaticMesh> MachineMesh(TEXT("/Game/Characters/SM_TattooMachine.SM_TattooMachine"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> GripAsset(TEXT("/Game/Characters/SM_TattooGrip.SM_TattooGrip"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> NeedleAsset(TEXT("/Game/Characters/SM_TattooNeedle.SM_TattooNeedle"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> MarkerMesh(TEXT("/Game/Characters/SM_Marker.SM_Marker"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PenCylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (MachineMesh.Succeeded() && GripAsset.Succeeded() && NeedleAsset.Succeeded())
	{
		PenMesh->SetStaticMesh(MachineMesh.Object);
		bPenIsMachineAsset = true;
		bNeedleIsAsset = true;
	}
	else if (MarkerMesh.Succeeded())
	{
		PenMesh->SetStaticMesh(MarkerMesh.Object);
		bPenIsMarkerAsset = true;
	}
	else if (PenCylinder.Succeeded())
	{
		PenMesh->SetStaticMesh(PenCylinder.Object);
	}

	// 伸縮件（LMB 伸出到皮膚接觸點、放開收樁；長度每 tick 由 UpdatePenVisual 沿針軸
	// 實測皮膚距重設——各端同構解算＝同長度，無需複製）
	auto MakeStretchPart = [&](const TCHAR* Name) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		C->SetupAttachment(GetCapsuleComponent());
		C->SetAbsolute(true, true, true);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetVisibility(false);
		C->SetCastShadow(false);
		return C;
	};
	GripMesh = MakeStretchPart(TEXT("GripMesh"));
	NeedleMesh = MakeStretchPart(TEXT("NeedleMesh"));
	if (bPenIsMachineAsset)
	{
		GripMesh->SetStaticMesh(GripAsset.Object);
		NeedleMesh->SetStaticMesh(NeedleAsset.Object);
	}
	else if (PenCylinder.Succeeded())
	{
		NeedleMesh->SetStaticMesh(PenCylinder.Object);
	}

	// 打稿麥克筆（07-25）：Stencil 工具的手持模型——與機器三件套互斥顯示；
	// 機器缺席時 PenMesh 本身就是麥克筆，這支不再建（免雙筆）
	if (bPenIsMachineAsset && MarkerMesh.Succeeded())
	{
		MarkerPen = MakeStretchPart(TEXT("MarkerPen"));
		MarkerPen->SetStaticMesh(MarkerMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> PenBaseMat(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (PenBaseMat.Succeeded())
	{
		PenBodyMaterial = PenBaseMat.Object;
	}

	// 直接畫制 ghost 材質（畫畫時除自己與沉睡者外，其餘人半透明；資產由
	// Tools/AssetPrep/ue_make_ghost_material.py 生成，缺席時 ghost 退化為隱藏）
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GhostMat(
		TEXT("/Game/Characters/M_GhostBody.M_GhostBody"));
	if (GhostMat.Succeeded())
	{
		GhostMaterial = GhostMat.Object;
	}

	InkCanvas = CreateDefaultSubobject<UInkCanvasComponent>(TEXT("InkCanvas"));

	// 醉夢迷宮：受害者 client 本地模擬（不複製——其餘玩家一無所知）
	//（v4.0 退役封存：元件保留、永不啟動——描圖取代）
	DreamMaze = CreateDefaultSubobject<UDreamMazeComponent>(TEXT("DreamMaze"));

	// 醉夢描圖（v4.0 定案 #49）：受害者 client 本地模擬（不複製——作畫者看不到夢的進度）
	DreamTrace = CreateDefaultSubobject<UDreamTraceComponent>(TEXT("DreamTrace"));

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

	// 雲端隨身上行（B3）：只有 packaged/-game 的遠端客戶端有戲——等本機雲端
	// 拉取完成後把資產交給主機。PIE/robo（WorldType≠Game）與 listen 主機不啟動。
	if (GetWorld() && GetWorld()->WorldType == EWorldType::Game && !HasAuthority() &&
		UNiceInkSessionSubsystem::IsOnlineServiceConfigured())
	{
		PersonaUploadTicksLeft = 20; // 0.5s × 20 ＝ 10s 內等到就發車
		GetWorldTimerManager().SetTimer(PersonaUploadTimer, this,
			&ANiceInkCharacter::MaybeUploadPersona, 0.5f, /*bLoop=*/true);
	}

	// 自訂臉房內分發（2026-08-10）：-game 世界啟動輪詢（LAN 與 EOS 同路；
	// PIE/robo WorldType≠Game 不啟動＝既有測試零干擾）。等佔有＋席位就緒後：
	// 本人臉入自己登記簿＋上傳 server；遠端 client 另外報到領全房已知臉。
	if (GetWorld() && GetWorld()->WorldType == EWorldType::Game)
	{
		FaceShareTicksLeft = 150; // 0.1s × 150 ＝ 15s 內等到佔有（08-14 輪詢 0.5→0.1s＝進房握手提速）
		GetWorldTimerManager().SetTimer(FaceShareTimer, this,
			&ANiceInkCharacter::MaybeStartFaceShare, 0.1f, /*bLoop=*/true);
	}

	// 實體筆外觀：真資產（刺青機/麥克筆）＝實尺寸（scale 1）；圓柱退路＝縮成 1.2cm 粗 15cm 長。
	// 材質＝引擎基本材質 MID（幾何 only 匯入，預設材質無 Color 參數）：
	// 刺青機依槽名分區上色（黑鐵骨架/銅線圈/鋼握管/黃銅小件；linear 色），其餘單色深灰
	if (PenMesh)
	{
		PenMesh->SetWorldScale3D((bPenIsMachineAsset || bPenIsMarkerAsset)
			? FVector::OneVector : FVector(0.012f, 0.012f, 0.15f));
		if (PenBodyMaterial)
		{
			if (bPenIsMachineAsset && PenMesh->GetStaticMesh())
			{
				const TArray<FStaticMaterial>& Slots = PenMesh->GetStaticMesh()->GetStaticMaterials();
				for (int32 i = 0; i < Slots.Num(); ++i)
				{
					FLinearColor C(0.020f, 0.020f, 0.026f); // TattooFrame 黑鐵（預設）
					const FName Slot = Slots[i].MaterialSlotName;
					if (Slot == TEXT("TattooTex"))
					{
						continue; // 貼圖版（Meshy PBR＋M_TattooMachine 已綁在資產上）：不蓋 MID
					}
					if (Slot == TEXT("TattooCoils"))
					{
						C = FLinearColor(0.430f, 0.150f, 0.045f); // 銅線圈
					}
					else if (Slot == TEXT("TattooGrip"))
					{
						C = FLinearColor(0.300f, 0.320f, 0.350f); // 鋼握管
					}
					else if (Slot == TEXT("TattooBrass"))
					{
						C = FLinearColor(0.400f, 0.250f, 0.060f); // 黃銅銜鐵/接線柱
					}
					if (UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(PenBodyMaterial, this))
					{
						MID->SetVectorParameterValue(TEXT("Color"), C);
						PenMesh->SetMaterial(i, MID);
					}
				}
			}
			else if (UMaterialInstanceDynamic* PenMID = UMaterialInstanceDynamic::Create(PenBodyMaterial, this))
			{
				PenMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.03f, 0.03f, 0.04f));
				PenMesh->SetMaterial(0, PenMID);
			}
		}
	}
	// 貼圖版伸縮件（槽 TattooTex）沿用資產綁定的 M_TattooMachine；其餘退路上鋼色 MID
	for (UStaticMeshComponent* Part : { GripMesh.Get(), NeedleMesh.Get() })
	{
		if (!Part || !PenBodyMaterial)
		{
			continue;
		}
		const UStaticMesh* PartSM = Part->GetStaticMesh();
		const bool bTextured = PartSM && PartSM->GetStaticMaterials().Num() > 0 &&
			PartSM->GetStaticMaterials()[0].MaterialSlotName == TEXT("TattooTex");
		if (!bTextured)
		{
			if (UMaterialInstanceDynamic* PartMID = UMaterialInstanceDynamic::Create(PenBodyMaterial, this))
			{
				PartMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.55f, 0.57f, 0.60f)); // 鋼
				Part->SetMaterial(0, PartMID);
			}
		}
	}
	// 打稿麥克筆＝筆身染龍膽紫（工具識別一眼讀出「這支畫的是稿」；深紫壓暗防搶戲）
	if (MarkerPen && PenBodyMaterial)
	{
		if (UMaterialInstanceDynamic* MarkerMID = UMaterialInstanceDynamic::Create(PenBodyMaterial, this))
		{
			MarkerMID->SetVectorParameterValue(TEXT("Color"),
				NiceInkStencil::Color() * FLinearColor(0.5f, 0.5f, 0.5f, 1.0f));
			MarkerPen->SetMaterial(0, MarkerMID);
		}
	}
}

void ANiceInkCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ANiceInkCharacter, bAsleep);
	DOREPLIFETIME(ANiceInkCharacter, bEyesOpen);
	// 裝睡：本人端用本地鏡像零延遲（pattern 同臉指向），複製只服務他端的姿勢與眼皮
	DOREPLIFETIME_CONDITION(ANiceInkCharacter, bFeignSleep, COND_SkipOwner);
	// 頭部轉動破綻：臉指向只發給他端（本人端用本地連續值零延遲；抬升＝純函數不複製）
	DOREPLIFETIME_CONDITION(ANiceInkCharacter, SleepAimAzDeg, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(ANiceInkCharacter, SleepAimTiltDeg, COND_SkipOwner);
	// 伸縮針觸發：本人端本地鏡像零延遲，複製只服務他端的針視覺
	DOREPLIFETIME_CONDITION(ANiceInkCharacter, bPenTriggerHeld, COND_SkipOwner);
	// 工具狀態（07-25 打稿制）：他端選 3D 手持模型（麥克筆 vs 刺青機）
	DOREPLIFETIME_CONDITION(ANiceInkCharacter, RepNeedle, COND_SkipOwner);
	// 技能庫存只給本人：作畫者不該從網路層讀到「受害者拿到技能／進度」
	DOREPLIFETIME_CONDITION(ANiceInkCharacter, SprayCharges, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ANiceInkCharacter, KickCharges, COND_OwnerOnly);
	DOREPLIFETIME(ANiceInkCharacter, bBlinded);
	DOREPLIFETIME(ANiceInkCharacter, BlindType);
	DOREPLIFETIME(ANiceInkCharacter, bLeanLocked);
	DOREPLIFETIME(ANiceInkCharacter, LeanPoint);
	DOREPLIFETIME(ANiceInkCharacter, LeanNormal);
	DOREPLIFETIME(ANiceInkCharacter, LeanTarget);
	// 作畫臉指向：本人端用本地值零延遲（pattern 同 SleepAim），複製只服務他端擺姿
	DOREPLIFETIME_CONDITION(ANiceInkCharacter, DrawAimAzDeg, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(ANiceInkCharacter, DrawAimTiltDeg, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(ANiceInkCharacter, DrawTargetRepW, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(ANiceInkCharacter, bDrawTargetRepValid, COND_SkipOwner);
	DOREPLIFETIME(ANiceInkCharacter, bBodyFaceDown);
	DOREPLIFETIME(ANiceInkCharacter, SleepLieYawDeg);
	DOREPLIFETIME(ANiceInkCharacter, bSleepLieYawValid);
	DOREPLIFETIME_CONDITION(ANiceInkCharacter, LookPitchDeg, COND_SkipOwner);
}

void ANiceInkCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	EnsureAvatarApplied();

	APlayerController* PC = Cast<APlayerController>(GetController());
	const bool bLocal = PC && IsLocallyControlled();

	// 08-14 根治②補丁：本人端關掉 CMC 網路平滑——平滑載體（GetMesh）只服務
	// 「看別人」（simulated proxy／listen server 看 client）。本人的 client 修正
	// 若也被攤 100ms，入鎖瞬間讀骨會拿到半路位置＝凍結相機錨污染
	//（robo_remotejitter first-lock dCamToPoint 102.5cm 實錘）。關掉＝本人視覺
	// 回到 capsule 硬跳＝改制前的既有行為原樣（他端平滑不受影響）。
	if (bLocal && GetCharacterMovement() &&
		GetCharacterMovement()->NetworkSmoothingMode != ENetworkSmoothingMode::Disabled)
	{
		GetCharacterMovement()->NetworkSmoothingMode = ENetworkSmoothingMode::Disabled;
		GetMesh()->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
	}

	// 睡姿 yaw 每 tick 斷言（08-15 競態封死；owner client 限定——server 端本就權威）：
	// bAsleep 且 SleepLieYaw 已複製到＝actor/控制器 yaw 必須等於它。差 >2° 就扶正並記 log
	//（留證：日後再現一次就有數字）。與 08-04 的一次性 RPC 寫入互補＝順序無關。
	if (bLocal && !HasAuthority() && bAsleep && bSleepLieYawValid)
	{
		const float CurYaw = GetActorRotation().Yaw;
		const float Dev = FMath::Abs(FMath::FindDeltaAngleDegrees(CurYaw, SleepLieYawDeg));
		if (Dev > 2.0f)
		{
			UE_LOG(LogTemp, Warning, TEXT("NiSleep: owner yaw drift %.1f (actor %.1f vs lie %.1f) - corrected"),
				Dev, CurYaw, SleepLieYawDeg);
			SetActorRotation(FRotator(0.0f, SleepLieYawDeg, 0.0f), ETeleportType::TeleportPhysics);
		}
		if (PC)
		{
			const float CtlDev = FMath::Abs(FMath::FindDeltaAngleDegrees(PC->GetControlRotation().Yaw, SleepLieYawDeg));
			if (CtlDev > 2.0f && !bEyesOpen)
			{
				// 閉眼期控制器 yaw 恆=睡姿 yaw（睜眼後臉指向制自己管相機，控制器不再承載朝向）
				PC->SetControlRotation(FRotator(0.0f, SleepLieYawDeg, 0.0f));
			}
		}
	}

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

	// 臉齊現身閘（08-14）：Game 世界裡臉還沒到齊的力士對旁人整體隱形——
	// 房內從頭到尾不存在頂著名冊臉的力士（SPEC #52 身分載體閉環）。
	// server 端裁決（bHidden 複製全房）；client 端對 simulated proxy 補強（複製時序保底）；
	// owner 端不干預（自己第一人稱本來就 OwnerNoSee、veil 蓋著）。
	// 臉齊現身閘（08-14 三修＝單一權威）：只有 server 動 bHidden——真玩家 pawn
	// spawn 即隱形、GameMode 等全房 ack 才 FaceGateShowNow。無 PlayerState（頭像亭
	// 替身/選單舞者）不閘；host 本人首 tick 即放行。client 端零介入＝競態根絕。
	if (HasAuthority() && GetWorld()->WorldType == EWorldType::Game && !bFaceGateShown)
	{
		if (GetPlayerState())
		{
			if (IsLocallyControlled())
			{
				FaceGateShowNow(); // listen 主機本人：臉走本地零延遲路
			}
			else if (!IsHidden())
			{
				SetActorHiddenInGame(true);
				UE_LOG(LogTemp, Log, TEXT("NiFaceShare: gate hide pid=%d"),
					GetPlayerState<APlayerState>()->GetPlayerId());
			}
		}
		// PlayerState 未到（佔有中）＝先不動，下一 tick 再看
	}

	UpdateSleepBodyDouble(DeltaSeconds); // 所有端：睡姿替身＋頭部轉動破綻
	UpdateWalkAnim(DeltaSeconds);        // 所有端：站立移動的程式化步伐
	UpdateMarkerSfx(DeltaSeconds);       // 稿筆摩擦聲（內部只服務本地端、失控時自停）

	// 直接畫制：aim 驅動的作畫姿勢（所有端；姿態沒變不寫骨——ApplyBowPose 內建
	// 髒檢查；他端在這裡做複製值的平滑追趕）
	if (bLeanLocked)
	{
		if (!IsLocallyControlled())
		{
			// 追趕係數 12→20（07-26 遲鈍根治）：τ 83ms→50ms——上報 30Hz＋server
			// tick 60Hz 後包距縮半，較快的追趕不再顯跳格
			const float K = FMath::Clamp(DeltaSeconds * 20.0f, 0.0f, 1.0f);
			if (bRemoteDrawSnap)
			{
				RemoteDrawAzDeg = DrawAimAzDeg;
				RemoteDrawTiltDeg = DrawAimTiltDeg;
				RemoteDrawTargetW = DrawTargetRepW;
				bRemoteDrawSnap = false;
			}
			else
			{
				RemoteDrawAzDeg += FMath::FindDeltaAngleDegrees(RemoteDrawAzDeg, DrawAimAzDeg) * K;
				RemoteDrawTiltDeg += (DrawAimTiltDeg - RemoteDrawTiltDeg) * K;
				RemoteDrawTargetW = FMath::Lerp(RemoteDrawTargetW, FVector(DrawTargetRepW), K);
			}
		}
		ApplyBowPose();
	}

	UpdateJiggleBones(DeltaSeconds); // 所有端：軟肉彈跳（一切擺骨之後、讀骨消費者之前）

	UpdatePenVisual(); // 所有端：筆焊死在右手（讀本 tick 最終骨骼——必在 ApplyBowPose 後）

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
	PollShakeAttack(PC);
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
	if (bLeanCamActive)
	{
		bLeanCamActive = false;
		// 鎖定期間本體相機被世界寫入接管，相對變換已亂——還原站姿掛點（硬切）。
		// 睡姿例外：ApplySleepVisual 擁有沉睡的相機掛點，別蓋。
		if (!bAsleep && FirstPersonCamera)
		{
			FirstPersonCamera->SetRelativeLocation(FVector(0.0f, 0.0f, 64.0f)); // 與建構子一致（sumo 眼高）
			FirstPersonCamera->SetRelativeRotation(FRotator(CameraPitch, 0.0f, 0.0f));
		}
	}

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
	// 沉睡者限定：噴射／拳腳（v4.0 定案 #51 移出核心循環——全域閘封存；
	// 未來更新回歸時改回 true）。瞄準＝頭部視野方向（聽聲推理、盲瞄）。
	if (!bAsleep || (!GNiceInkSprayEnabled && !GNiceInkKickEnabled))
	{
		return;
	}

	// 出發點選擇：1 鼻／2 陰部／3 肛門
	if (PC->WasInputKeyJustPressed(EKeys::One)) { SelectedSprayOrigin = EInkEvidenceType::Sneeze; }
	if (PC->WasInputKeyJustPressed(EKeys::Two)) { SelectedSprayOrigin = EInkEvidenceType::Piss; }
	if (PC->WasInputKeyJustPressed(EKeys::Three)) { SelectedSprayOrigin = EInkEvidenceType::Shit; }

	const float AimYawWorld = FirstPersonCamera->GetComponentRotation().Yaw;
	if (GNiceInkSprayEnabled && PC->WasInputKeyJustPressed(EKeys::Q) && SprayCharges > 0)
	{
		ServerSpray(SelectedSprayOrigin, AimYawWorld);
	}
	if (GNiceInkKickEnabled && PC->WasInputKeyJustPressed(EKeys::E) && KickCharges > 0)
	{
		ServerKick(AimYawWorld);
	}
}

void ANiceInkCharacter::PollShakeAttack(APlayerController* PC)
{
	// 搖晃攻擊（v4.0 定案 #50）：作畫階段、非受害者、G 鍵＝花錢搖他的夢。
	// 鎖定作畫中也可按（G 不與 lean 輸入衝突）；現金/冷卻驗證在 server。
	const ANiceInkGameState* GS = GetWorld() ? GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
	if (!GS || GS->CurrentPhase != ENiceInkPhase::Drawing || bAsleep ||
		GetInkAuthorId() == GS->VictimPlayerId || !PC->WasInputKeyJustPressed(EKeys::G))
	{
		return;
	}
	ServerAttackShake();
}

void ANiceInkCharacter::SetupAsMenuDummy(int32 AvatarIdx)
{
	if (!Body || !InkCanvas || FNiceInkAvatars::Num() <= 0)
	{
		return;
	}
	const int32 Idx = FMath::Clamp(AvatarIdx, 0, FNiceInkAvatars::Num() - 1);
	Body->ApplyAvatar(FNiceInkAvatars::Get(Idx));
	Body->BindCanvas(InkCanvas);
	Body->SetEyesClosed(false);
	AppliedAvatarIndex = Idx; // EnsureAvatarApplied 無 PS 直接 return——不會互搶
}

void ANiceInkCharacter::EnsureAvatarApplied()
{
	const ANiceInkPlayerState* PS = GetPlayerState<ANiceInkPlayerState>();
	if (!PS)
	{
		return;
	}
	if (PS->AvatarIndex != AppliedAvatarIndex)
	{
		Body->ApplyAvatar(FNiceInkAvatars::Get(PS->AvatarIndex));
		Body->BindCanvas(InkCanvas);
		Body->SetEyesClosed(bAsleep);
		AppliedAvatarIndex = PS->AvatarIndex;
		AppliedShareFaceRev = 0; // 名冊重套會蓋臉——強制自訂臉重疊
		AppliedShareToneRev = 0; // tone 先行同理（名冊膚色會蓋掉提前 tone）
	}

	// 房內分發自訂臉（2026-08-10）：登記簿有這席的臉且版本變了＝蓋上名冊臉。
	// 每 tick 輪詢（map find＋int 比對＝廉價），臉晚到/換臉都自然收斂
	if (UNiceInkFaceShare* Share = UNiceInkFaceShare::Get(this))
	{
		// tone 先行（08-14）：FaceBegin 帶到的膚色先上身——臉貼圖還要 ~1 秒列車
		const int32 ToneRev = Share->GetToneRevision(PS->SeatIndex);
		if (ToneRev > 0 && ToneRev != AppliedShareToneRev)
		{
			Body->ApplySkinToneOnly(Share->GetTone(PS->SeatIndex));
			AppliedShareToneRev = ToneRev;
		}
		const int32 Rev = Share->GetRevision(PS->SeatIndex);
		if (Rev > 0 && Rev != AppliedShareFaceRev)
		{
			Body->ApplyCustomAvatar(Share->GetOpen(PS->SeatIndex), Share->GetClosed(PS->SeatIndex),
				Share->GetMask(PS->SeatIndex), Share->GetTone(PS->SeatIndex));
			AppliedShareFaceRev = Rev;
			UE_LOG(LogTemp, Log, TEXT("NiFaceShare: body face applied (seat %d rev %d, %s)"),
				PS->SeatIndex, Rev, HasAuthority() ? TEXT("server view") : TEXT("client view"));
		}
	}
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
			// 裝睡（使用者定案 2026-07-17）：按住 Shift＝姿勢與眼皮回沉睡樣（旁人看＝
			// 還沒醒）；放開＝回到按下前的臉指向。按住期間滑鼠不寫入臉指向＝指向凍結，
			// 放開自然復原——不做存/還原（沒有可以錯的第二份狀態）。
			const bool bWantsFeign = bDebugFeignHeld ||
				PC->IsInputKeyDown(EKeys::LeftShift) ||
				PC->IsInputKeyDown(EKeys::RightShift);
			if (bWantsFeign != bFeignSleepLocal)
			{
				SetFeignSleepLocal(bWantsFeign);
			}
			if (!bFeignSleepLocal)
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
		}

		// 頭部轉動破綻：臉指向節流上報（閉眼不送＝盲瞄不洩漏）；30Hz（07-26 20→30）
		SleepLookSendAccum += DeltaSeconds;
		if (SleepLookSendAccum >= 0.0333f &&
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

	// 站立視野俯仰上報（08-15）：他端據此擺 Neck+Head 俯仰（左右不上報＝全身跟
	// 控制器 yaw、頭身零相對位移=user 定案）；30Hz 節流＋變化 >0.5° 才送
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - LookPitchLastReport >= 1.0f / 30.0f &&
		FMath::Abs(CameraPitch - LookPitchReportedDeg) > 0.5f)
	{
		LookPitchLastReport = Now;
		LookPitchReportedDeg = CameraPitch;
		if (HasAuthority())
		{
			LookPitchDeg = CameraPitch; // listen 主機本人：直寫複製值
		}
		else
		{
			ServerReportLookPitch(CameraPitch);
		}
	}
}

void ANiceInkCharacter::ServerReportLookPitch_Implementation(float PitchDeg)
{
	LookPitchDeg = FMath::Clamp(PitchDeg, -89.0f, 89.0f);
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

	// robo 走路注入（DebugRoboWalk）：模擬按住方向鍵——與真鍵同一入口（輸入所有權慣例）
	if (DebugWalkEndTime > 0.0f)
	{
		if (GetWorld()->GetTimeSeconds() < DebugWalkEndTime)
		{
			const FVector Dir(DebugWalkDirWorld.X, DebugWalkDirWorld.Y, 0.0f);
			if (!Dir.IsNearlyZero())
			{
				AddMovementInput(Dir.GetSafeNormal(), 1.0f);
			}
		}
		else
		{
			DebugWalkEndTime = -1.0f;
		}
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
			if (SelectedColorIndex != Index)
			{
				SelectedColorIndex = Index;
				// 中筆劃換色即時生效：顏色是 per-stroke 屬性（開筆時取樣）——
				// 按住左鍵中換色若不重開筆劃，要抬針才變色=「按了沒反應」讀感
				//（割線/打霧同一條路；與換針 StopPaintingLocal 同款處理）
				StopPaintingLocal();
			}
			break;
		}
	}
}

FString ANiceInkCharacter::DebugRoboLeanEnterFromEye(ANiceInkCharacter* Target, FVector AimPoint, bool bEnter)
{
	// 與 PollLeanEnter 同一條真射線（起點＝當前真實眼位），方向由 AimPoint 給定
	//（robo 不能注入滑鼠）。驗的是「站在外面點不點得到」——DebugRoboEnterLean
	// 會先傳送到點旁＝跳過此段（低位入口從未被驗過的縫）。
	if (!Target || !GetWorld() || !FirstPersonCamera)
	{
		return TEXT("MISS setup");
	}
	if (bLeanLocked)
	{
		return TEXT("MISS locked");
	}
	const FVector Start = FirstPersonCamera->GetComponentLocation();
	const FVector Dir = (AimPoint - Start).GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		return TEXT("MISS dir");
	}
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(NiceInkLeanTrace), /*bInTraceComplex=*/true);
	QueryParams.AddIgnoredActor(this);
	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + Dir * LeanEnterMaxDistance,
		ECC_Visibility, QueryParams))
	{
		return TEXT("MISS trace");
	}
	ANiceInkCharacter* HitChar = Cast<ANiceInkCharacter>(Hit.GetActor());
	if (!HitChar || HitChar != Target || !HitChar->Body)
	{
		return FString::Printf(TEXT("MISS actor z=%.1f"), Hit.ImpactPoint.Z);
	}
	FVector2D UnusedUV;
	if (!Target->Body->ResolveBodyUV(Hit.ImpactPoint, UnusedUV))
	{
		return FString::Printf(TEXT("MISS skin z=%.1f"), Hit.ImpactPoint.Z);
	}
	if (bEnter)
	{
		ServerEnterLean(Target, Hit.ImpactPoint, Hit.ImpactNormal);
	}
	return FString::Printf(TEXT("HIT z=%.1f err=%.1f eyez=%.1f"),
		Hit.ImpactPoint.Z, FVector::Dist(Hit.ImpactPoint, AimPoint), Start.Z);
}

FString ANiceInkCharacter::DebugRoboCoverageScan(ANiceInkCharacter* Target, int32 GridN)
{
	// 覆蓋率量測儀（user 提問「不趴能畫到多大、趴後多大、理論極限在哪」的量測答案）。
	// 對受害者皮膚 UV 網格取樣→UV→世界點＋法線→從眼位集合打「真入座射線」：
	// 眼高 {35,60,98(趴實測),130,156(站實測)} × 繞 P 外向方位扇 {0,±35,±70°} ×
	// 距離 {70,120,190,270}，命中語義同 PollLeanEnter（complex、皮膚 2.5cm、射程≤295）。
	// 路人角色 ignore＝只量受害者本體的自遮擋幾何。
	UWorld* World = GetWorld();
	if (!Target || !Target->Body || !World)
	{
		return TEXT("ERR setup");
	}
	UInkBodyComponent* B = Target->Body;
	const float Ground = GetActorLocation().Z - 92.0f;
	const FVector VictimC = Target->GetActorLocation();

	FCollisionQueryParams QP(SCENE_QUERY_STAT(NiceInkCoverage), /*bInTraceComplex=*/true);
	for (TActorIterator<ANiceInkCharacter> It(World); It; ++It)
	{
		if (*It != Target)
		{
			QP.AddIgnoredActor(*It);
		}
	}

	const float Heights[5] = { 35.0f, 60.0f, 98.0f, 130.0f, 156.0f };
	const float Spreads[5] = { 0.0f, 35.0f, -35.0f, 70.0f, -70.0f };
	const float Dists[4] = { 70.0f, 120.0f, 190.0f, 270.0f };

	int32 Total = 0, Down = 0, LowBand = 0, VisAny = 0, VisStandProne = 0;
	int32 VisAtH[5] = { 0, 0, 0, 0, 0 };

	const int32 N = FMath::Clamp(GridN, 8, 256);
	for (int32 Gy = 0; Gy < N; ++Gy)
	{
		for (int32 Gx = 0; Gx < N; ++Gx)
		{
			const FVector2D UV((Gx + 0.5f) / N, (Gy + 0.5f) / N);
			FVector P, Nrm;
			if (!B->ResolveUVToWorldWithNormal(UV, P, Nrm))
			{
				continue;
			}
			++Total;
			if (Nrm.Z < -0.5f)
			{
				++Down; // 面朝下（貼地側）——結構性看不到＝翻身的領土
			}
			if (P.Z - Ground < 15.0f)
			{
				++LowBand;
			}

			FVector Out = P - VictimC;
			Out.Z = 0.0f;
			Out = Out.GetSafeNormal();
			if (Out.IsNearlyZero())
			{
				Out = FVector(1.0f, 0.0f, 0.0f);
			}

			bool bAnyVis = false;
			bool bVisAtHeight[5] = { false, false, false, false, false };
			for (int32 H = 0; H < 5; ++H)
			{
				bool bVis = false;
				for (int32 A = 0; A < 5 && !bVis; ++A)
				{
					const FVector Dir = Out.RotateAngleAxis(Spreads[A], FVector::UpVector);
					for (int32 D = 0; D < 4 && !bVis; ++D)
					{
						FVector Eye = P + Dir * Dists[D];
						Eye.Z = Ground + Heights[H];
						if (FVector::Dist(Eye, P) > 295.0f)
						{
							continue;
						}
						FHitResult Hit;
						const FVector End = P + (P - Eye).GetSafeNormal() * 3.0f;
						if (World->LineTraceSingleByChannel(Hit, Eye, End, ECC_Visibility, QP) &&
							Hit.GetActor() == Target &&
							FVector::Dist(Hit.ImpactPoint, P) <= 2.5f)
						{
							bVis = true;
						}
					}
				}
				if (bVis)
				{
					++VisAtH[H];
					bVisAtHeight[H] = true;
					bAnyVis = true;
				}
			}
			if (bAnyVis)
			{
				++VisAny;
			}
			if (bVisAtHeight[2] || bVisAtHeight[4])
			{
				++VisStandProne; // 站(156)∪低眼(98) 聯集（07-20 趴姿裁決的量測欄位，儀器保留）
			}
		}
	}
	return FString::Printf(
		TEXT("total=%d down=%d low15=%d any=%d sp=%d h35=%d h60=%d h98=%d h130=%d h156=%d"),
		Total, Down, LowBand, VisAny, VisStandProne,
		VisAtH[0], VisAtH[1], VisAtH[2], VisAtH[3], VisAtH[4]);
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

// --- 直接畫制（2026-07-18 user 定案；07-20 筆即游標：墨從筆尖出、準星退役）---

float ANiceInkCharacter::FAimEuro::Step(float X, float Dt, float MinCutoffHz, float Beta)
{
	// One Euro（Casiez 2012）：截止頻率=MinCutoff+Beta×|速度|——靜止強濾抖（截止低）、
	// 快掃近零滯後（截止高）。固定係數低通把「濾抖」和「延遲」綁在一起（滯後距離=
	// 速度×延遲、掃越快筆落後越遠＝兩支筆分家的 lag 真兇）；自適應把兩者拆開。
	if (!bInit || Dt <= 0.0f)
	{
		Snap(X);
		return X;
	}
	X = XPrev + FMath::FindDeltaAngleDegrees(XPrev, X); // 角度連續化（az 過 ±180 不跳）
	auto Alpha = [Dt](float CutoffHz)
	{
		const float Tau = 1.0f / (2.0f * PI * CutoffHz);
		return 1.0f / (1.0f + Tau / Dt);
	};
	const float Dx = (X - XPrev) / Dt;
	DxPrev += (Dx - DxPrev) * Alpha(1.0f); // 速度估計自己先過 1Hz 低通（教科書配置）
	const float Cutoff = MinCutoffHz + Beta * FMath::Abs(DxPrev);
	XPrev = FMath::UnwindDegrees(XPrev + (X - XPrev) * Alpha(Cutoff));
	return XPrev;
}

void ANiceInkCharacter::PollDrawAim(APlayerController* PC, float DeltaSeconds, bool bCruise)
{
	// 作畫中滑鼠＝臉指向（az=世界 yaw、tilt 正=向下）；robo 直設 aim 由此消化
	//（針 aim 同步 snap＝傳送門不產生追趕位移——backdoor sweep 不落墨契約的載體）
	if (bHasPendingDebugDrawAim)
	{
		bHasPendingDebugDrawAim = false;
		DrawAimAzLocal = FMath::UnwindDegrees(PendingDebugDrawAim.X);
		DrawAimTiltLocal = FMath::Clamp(PendingDebugDrawAim.Y, DrawTiltMinDeg, DrawTiltMaxDeg);
		TattooNeedleAz = DrawAimAzLocal;
		TattooNeedleTilt = DrawAimTiltLocal;
		TattooCursorAz = DrawAimAzLocal;   // 游標同步 snap（傳送門不產生追趕位移）
		TattooCursorTilt = DrawAimTiltLocal;
		// robo 橋接鐵則（07-28 十一輪血價）：角度命令的語義=「射線指哪」——游標制下
		// 游標從命令角的命中點再生（aim 不變＝命中點在射線上＝命令角逐字生效）、
		// gaze 硬切＝robo 下相機/臉零延遲、既有角度契約全數原樣
		bDrawCursorRelatch = true;
	}
	float MouseX = 0.0f;
	float MouseY = 0.0f;
	PC->GetInputMouseDelta(MouseX, MouseY);
	if (bHasPendingDebugMouse)
	{
		bHasPendingDebugMouse = false;
		MouseX += PendingDebugMouse.X;
		MouseY += PendingDebugMouse.Y;
		PendingDebugMouse = FVector2D::ZeroVector; // 鉤子端累加、消化端清帳
	}
	// 皮繩制（07-24）：滑鼠永遠=指哪（模式切換退役——按住左鍵游標照常自由，針負責追）；
	// 增益=FOV 縮放後的鎖定靈敏度（開鏡定律：不縮放=游標三倍速）
	const float Sens = DrawAimSensitivity();
	if (SelectedNeedle == EInkNeedle::Stencil)
	{
		// 稿筆游標制（07-31 user 定案）：手=皮膚游標（恆定公分增益）、臉/相機=惰性
		// 注視——畫布在螢幕上近似不動＝自由手繪的參考系；aim 由游標反算＝下游零改動。
		// StencilCursorGain（08-01）：畫圖手速=瞄準手速再降一級（角度命令 relatch
		// 不經此處=robo 角度契約逐字原樣）
		UpdateStencilCursor(MouseX, MouseY, Sens * StencilCursorGain, DeltaSeconds);
	}
	else if (bCruise)
	{
		bDrawGazeInit = false;
		bDrawCamInit = false;
		// 自由游標（08-04 二輪修）：滑鼠純積分推游標——無皮繩、可放很遠；
		// 「方向」=針→游標連線（位置=積分=穩定；瞬時位移=噪聲——一輪方向舵
		// 的敏感/粗顆粒病根）。aim 恆=針（畫面歸針原樣）、游標=獨立意圖點。
		TattooCursorAz = FMath::UnwindDegrees(TattooCursorAz + MouseX * Sens);
		TattooCursorTilt = FMath::Clamp(TattooCursorTilt - MouseY * Sens,
			DrawTiltMinDeg, DrawTiltMaxDeg);
	}
	else
	{
		bDrawGazeInit = false;    // 機器工具＝畫面歸針原樣；切回稿筆時 gaze/凍結相機重新 snap
		bDrawCamInit = false;
		DrawAimAzLocal = FMath::UnwindDegrees(DrawAimAzLocal + MouseX * Sens);
		// （07-28 顯示制：游標只受全域 tilt 域鉗——可達域邊界不鉗游標、只做 HUD 標記
		// 與收筆；「永遠不要去干擾玩家畫筆的移動」＝user 逐字定案）
		DrawAimTiltLocal = FMath::Clamp(DrawAimTiltLocal - MouseY * Sens,
			DrawTiltMinDeg, DrawTiltMaxDeg);
	}
	if (bCruise)
	{
		// 液線針按住左鍵＝針以 v_max 上限追趕「手的意圖點」（速率所有權歸機器、
		// 路徑所有權歸手——lazy-mouse 結構）。**畫面屬於針**：相機/螢幕中心/2D 筆
		// 恆=針（viewmodel 恆定=07-22 定案不破），手 aim=隱形意圖點（皮繩內）——
		// 慢畫=針貼手=畫面跟手、快甩=針限速=畫面慢移「機器的重量」（拉桿時代讀感）。
		if (!bTattooChaseActive)
		{
			bTattooChaseActive = true;
			TattooNeedleAz = DrawAimAzLocal;   // 落針點=按下瞬間的 aim（起點重合）
			TattooNeedleTilt = DrawAimTiltLocal;
			TattooCursorAz = DrawAimAzLocal;   // 游標同點起手（輕點不動=dotwork）
			TattooCursorTilt = DrawAimTiltLocal;
			bStencilFollowTried = false;       // 沿稿吸附：每次壓針重新嘗試一次
		}
		if (bStencilFollowActive && (FMath::Abs(MouseX) + FMath::Abs(MouseY)) > 6.0f)
		{
			bStencilFollowActive = false; // 玩家出手＝拿回方向（沿稿→自由巡航；游標已被本 tick 滑鼠推走）
		}
		if (bDebugPaintStickActive)
		{
			// robo 方向命令：游標恆掛在針前方（無限走廊巡航——速度/針距契約同構）；
			// (0,0) 由 DebugRoboPaintStick 收游標=停
			const FVector2D Dn = DebugPaintStickPx.GetSafeNormal();
			const float AheadDeg = TattooChaseLeashCm * FMath::Max(TattooAngPerCmEst, 0.02f);
			TattooCursorAz = FMath::UnwindDegrees(TattooNeedleAz + Dn.X * AheadDeg);
			TattooCursorTilt = FMath::Clamp(TattooNeedleTilt + Dn.Y * AheadDeg,
				DrawTiltMinDeg, DrawTiltMaxDeg);
		}
		UpdateTattooCruise(DeltaSeconds);
	}
	else
	{
		if (bTattooChaseActive)
		{
			// 放開左鍵：手 aim 收斂到針位（巡航中 aim 恆=針，保留=防禦）
			DrawAimAzLocal = TattooNeedleAz;
			DrawAimTiltLocal = TattooNeedleTilt;
		}
		bTattooChaseActive = false;
		bTattooCruising = false;
		TattooChaseErrCm = 0.0f;
		bStencilFollowActive = false;
		bStencilFollowTried = false;
	}

	// One Euro：姿勢/筆/墨的驅動源＝濾波 aim（相機用生值——視角零延遲）。
	// 皮繩追趕中濾波源=針 aim（追趕本身已是平滑器；放開左鍵源切回手 aim、
	// ≤皮繩長的差距由濾波 ~100ms 滑過=不硬跳）
	const float SrcAz = bTattooChaseActive ? TattooNeedleAz : DrawAimAzLocal;
	const float SrcTilt = bTattooChaseActive ? TattooNeedleTilt : DrawAimTiltLocal;
	DrawAimAzFilt = AimEuroAz.Step(SrcAz, DeltaSeconds, DrawAimFilterMinCutoffHz, DrawAimFilterBeta);
	DrawAimTiltFilt = FMath::Clamp(
		AimEuroTilt.Step(SrcTilt, DeltaSeconds, DrawAimFilterMinCutoffHz, DrawAimFilterBeta),
		DrawTiltMinDeg, DrawTiltMaxDeg);

	// 上報節流（30Hz、變化 >0.5 度；07-26 20→30）——pattern 同 SleepAim（他端只拿來擺姿，
	// 送姿勢驅動源＝追趕中送針 aim，他端針視覺與本人一致）。
	// 稿筆游標制：複製通道送 gaze（他端的臉/頭語義=惰性注視）；姿勢/筆尖的世界
	// 目標由同包的 P（=游標）承載——臉慢手準的拆分在他端由這兩條載體天然重現
	const bool bRepGaze = SelectedNeedle == EInkNeedle::Stencil && bDrawGazeInit;
	const float RepAz = bRepGaze ? DrawGazeAz : SrcAz;
	const float RepTilt = bRepGaze ? DrawGazeTilt : SrcTilt;
	DrawAimSendAccum += DeltaSeconds;
	if (DrawAimSendAccum >= 0.0333f &&
		(FMath::Abs(FMath::FindDeltaAngleDegrees(LastSentDrawAz, RepAz)) > 0.5f ||
			FMath::Abs(LastSentDrawTilt - RepTilt) > 0.5f ||
			bLastSentTargetValid != bDrawTargetValid))
	{
		DrawAimSendAccum = 0.0f;
		LastSentDrawAz = RepAz;
		LastSentDrawTilt = RepTilt;
		bLastSentTargetValid = bDrawTargetValid;
		if (HasAuthority())
		{
			DrawAimAzDeg = RepAz;
			DrawAimTiltDeg = RepTilt;
			DrawTargetRepW = DrawTargetWorld;
			bDrawTargetRepValid = bDrawTargetValid;
		}
		else
		{
			// P 隨 aim 同包上報（07-26）：他端的筆尖/姿勢與墨同源（見 DrawTargetRepW）
			ServerUpdateDrawAim(RepAz, RepTilt, DrawTargetWorld, bDrawTargetValid);
		}
	}
}

void ANiceInkCharacter::UpdateStencilCursor(float MouseX, float MouseY, float SensDeg, float DeltaSeconds)
{
	// 五版（07-31 user 逐字定案「放滑鼠自由，僅接收資訊而不控制滑鼠的走向」）：
	// 滑鼠→aim 純積分——零投影、零吸附、零否決。二~四版的皮膚游標狀態機
	//（皮膚點狀態/切面步進/摺縫鉗/tilt 滑動）整批退役：每一個「防護」都被 user
	// 抓成一種卡——否決式防護在連續操作域＝結構性卡頓（本役三犯的總結）。
	// 凍結相機下「角度≡螢幕位置」＝小畫家的螢幕恆定增益（開鏡定律 tan 縮放已在
	// SensDeg 內）。皮膚點 P/筆/墨＝每 tick 從「射線打到哪」讀出來的導出量；
	// 打不到/出橢圓＝收筆與 ✕（純回饋面）＝十六輪鐵則「輸入所有權歸玩家、
	// 系統只動回饋面」的字面義。
	const float PrevAimAz = DrawAimAzLocal;
	const float PrevAimTilt = DrawAimTiltLocal;
	DrawAimAzLocal = FMath::UnwindDegrees(DrawAimAzLocal + MouseX * SensDeg);
	DrawAimTiltLocal = FMath::Clamp(DrawAimTiltLocal - MouseY * SensDeg,
		DrawTiltMinDeg, DrawTiltMaxDeg);
	// 本 tick 的實際 aim 位移（tilt 取鉗後實走量——域邊界上推不動就不算外推）
	const float TickAz = FMath::FindDeltaAngleDegrees(PrevAimAz, DrawAimAzLocal);
	const float TickTilt = DrawAimTiltLocal - PrevAimTilt;

	// 硬切事件（入鎖播種/錨點重瞄/robo 角度命令/切工具）＝gaze 與凍結相機重新對準
	if (bDrawCursorRelatch)
	{
		bDrawCursorRelatch = false;
		bDrawGazeInit = false;
		bDrawCamInit = false;
	}

	// 注視（gaze）＝aim 的惰性追隨——只餵臉（本人姿勢與他端複製通道＝
	// 第三人稱「臉追著筆走」讀感）；τ→0=硬跟
	if (!bDrawGazeInit)
	{
		DrawGazeAz = DrawAimAzLocal;
		DrawGazeTilt = DrawAimTiltLocal;
		bDrawGazeInit = true;
	}
	else
	{
		const float A = (DrawGazeTauS <= 0.001f) ? 1.0f
			: 1.0f - FMath::Exp(-DeltaSeconds / DrawGazeTauS);
		DrawGazeAz = FMath::UnwindDegrees(DrawGazeAz +
			FMath::FindDeltaAngleDegrees(DrawGazeAz, DrawAimAzLocal) * A);
		DrawGazeTilt += (DrawAimTiltLocal - DrawGazeTilt) * A;
	}

	// 稿筆相機（07-31 六版 user 定案＝邊緣推擠制）：恆凍結；只有「筆貼在畫面
	// 最邊緣（StencilCamEdgeFrac）＋本 tick 還在往外推＋左鍵沒按住」時，視野才
	// 吃掉這一 tick 的外推量（速度=推的速度、每 tick 上限=溢出量——不是碰邊就把
	// 既有溢出一次吃掉）。手停/筆回畫面內/畫畫中（LMB 按住）＝視野完全靜止。
	//（二三版「遠拉觸發→置中追趕」退役：停手後還會自己滑=user 打回）
	if (!bDrawCamInit)
	{
		DrawCamAz = DrawAimAzLocal;
		DrawCamTilt = DrawAimTiltLocal;
		bDrawCamInit = true;
	}
	else if (!bPenTriggerLocal)
	{
		// 邊緣帶半角＝讀投影矩陣（08-01 三修，儀器=robo_fovaxis_probe）：引擎
		// 實際維持垂直 FOV（halfV 恆 10.36°、halfH 隨視窗長寬比走；方窗實測
		// halfH≈10.3° vs 舊假設 18°）——「LeanLockedFov/2＋16:9」假設只在 16:9
		// 窗成立；讀矩陣=任何視窗恆準。貼邊判定的主詞演化＝生 aim（六版）→
		// 濾波 aim（三修）→筆網格尖（四修）→**游標標記投影**（六修終案，見下）。
		float HalfH = FMath::Clamp(LeanLockedFov * 0.5f, 5.0f, 85.0f);
		float HalfV = FMath::RadiansToDegrees(FMath::Atan(
			FMath::Tan(FMath::DegreesToRadians(HalfH)) * (9.0f / 16.0f))); // 無投影退路
		APlayerController* PC = Cast<APlayerController>(GetController());
		if (PC)
		{
			if (const ULocalPlayer* LP = PC->GetLocalPlayer())
			{
				FSceneViewProjectionData PD;
				if (LP->ViewportClient &&
					LP->GetProjectionData(LP->ViewportClient->Viewport, PD))
				{
					const FMatrix& M = PD.ProjectionMatrix;
					if (M.M[0][0] > KINDA_SMALL_NUMBER && M.M[1][1] > KINDA_SMALL_NUMBER)
					{
						HalfH = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan(1.0 / M.M[0][0])));
						HalfV = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan(1.0 / M.M[1][1])));
					}
				}
			}
		}
		// 六修（08-01 user 抓「左側還有一段就觸發、右側就不會」）：四修錨在紫筆
		// 網格筆尖——但筆只有射線打得到皮膚才跟得動（鎖點皮膚在左半＝往左筆貼
		// 膚跟到邊、往右筆卡在原地永不到帶）＝觸發與否跟著身體幾何走、不跟邊框
		// ＝方向不對稱。終錨＝**游標標記**（✕/小點的世界錨=GetStencilCursorHudWorld
		// ：P 有效=P、出剪影=aim 射線基準深度——永遠跟滑鼠走的那個可見記號）
		// 投影到本視窗像素；邊條寬=四邊統一像素（短邊半長×(1-EdgeFrac)，五修）
		// ——上下左右/往左往右觸發時游標離邊框距離恆等。讓位量=min(本 tick 手的
		// 外推量, 游標超出帶的角度溢出)——速度=推的速度、絕不吃既有溢出。
		FVector CursorW;
		FVector2D CursorPx;
		int32 SX = 0, SY = 0;
		if (PC)
		{
			PC->GetViewportSize(SX, SY);
		}
		if (PC && SX > 0 && SY > 0 && GetStencilCursorHudWorld(CursorW) &&
			PC->ProjectWorldLocationToScreen(CursorW, CursorPx, true))
		{
			const float HalfPxX = SX * 0.5f;
			const float HalfPxY = SY * 0.5f;
			const float BandPx = (1.0f - StencilCamEdgeFrac) * FMath::Min(HalfPxX, HalfPxY);
			const float Fx = (static_cast<float>(CursorPx.X) - HalfPxX) / HalfPxX; // +右
			const float Fy = (static_cast<float>(CursorPx.Y) - HalfPxY) / HalfPxY; // +下
			const float FxEdge = 1.0f - BandPx / HalfPxX;
			const float FyEdge = 1.0f - BandPx / HalfPxY;
			const float TanH = FMath::Tan(FMath::DegreesToRadians(HalfH));
			const float TanV = FMath::Tan(FMath::DegreesToRadians(HalfV));
			if (FMath::Abs(Fx) > FxEdge && TickAz * Fx > 0.0f)
			{
				const float OverDeg = FMath::RadiansToDegrees(
					FMath::Atan(FMath::Abs(Fx) * TanH) - FMath::Atan(FxEdge * TanH));
				const float Give = FMath::Min(FMath::Abs(TickAz), OverDeg);
				DrawCamAz = FMath::UnwindDegrees(DrawCamAz + FMath::Sign(Fx) * Give);
			}
			// 螢幕 +下 = tilt +（AimRot pitch=-tilt：tilt 大=低頭=畫面下方）
			if (FMath::Abs(Fy) > FyEdge && TickTilt * Fy > 0.0f)
			{
				const float OverDeg = FMath::RadiansToDegrees(
					FMath::Atan(FMath::Abs(Fy) * TanV) - FMath::Atan(FyEdge * TanV));
				const float Give = FMath::Min(FMath::Abs(TickTilt), OverDeg);
				DrawCamTilt += FMath::Sign(Fy) * Give;
			}
		}
	}
}

void ANiceInkCharacter::UpdateTattooCruise(float DeltaSeconds)
{
	// 自由游標追趕（08-04 二輪修＝user 擊穿互斥誤診：「滑鼠給方向」的正確實作
	// =「滑鼠給一個可以放很遠的目標點」）：Dir=針→游標、步長=min(v_max·dt·gain+債,
	// 殘距)——手擁有路徑（游標位置）、機器擁有速度；**皮繩退役**＝游標甩遠=
	// 持續走、游標貼針=貼手精描、游標橫移一點=方向細調（解析度隨距離放大）。
	// 速率上限量在「皮膚表面 3D 距離」不在角速度——眼距隨部位變、掠射面上
	// 小角=大皮膚位移；trace 回饋鉗讓掠射面自動變慢。P 跳段（拉出剪影/跨肢
	// 溝壑）＝收斂到零步＝針釘在邊緣持續原地扎——真刺青要跨過去必須抬針，
	// 順帶消滅跨肢誤連線。aim 恆=針（畫面歸針、相機/姿勢/複製全鏈）。
	bTattooCruising = false;
	// 沿稿吸附（07-25 打稿制）：壓針首 tick 找稿——SnapCm 內有稿線＝機器沿稿自動走
	//（手勢歸打稿、慢工歸機器）；沒稿/吸附失敗＝整段自由巡航
	if (!bStencilFollowTried)
	{
		bStencilFollowTried = true;
		FVector P0;
		if (TraceAimToTarget(FRotator(-TattooNeedleTilt, TattooNeedleAz, 0.0f).Vector(), P0))
		{
			bStencilFollowActive = TryAcquireStencilFollow(P0);
		}
	}
	if (bStencilFollowActive)
	{
		// 沿稿模式：手 aim 收攏到針（意圖點不驅動；動滑鼠取消在 PollDrawAim 層）、
		// 相機/畫面照常屬於針；稿走完/失效＝停針原地扎（放開再壓＝從斷點續走）
		DrawAimAzLocal = TattooNeedleAz;
		DrawAimTiltLocal = TattooNeedleTilt;
		TattooCursorAz = TattooNeedleAz;   // 游標收攏到針（沿稿結束不得衝向殘留游標）
		TattooCursorTilt = TattooNeedleTilt;
		TattooChaseErrCm = 0.0f;
		FVector PNow;
		if (!TraceAimToTarget(FRotator(-TattooNeedleTilt, TattooNeedleAz, 0.0f).Vector(), PNow) ||
			!UpdateStencilFollow(DeltaSeconds, PNow))
		{
			bStencilFollowActive = false;
		}
		return;
	}
	// aim 恆=針（畫面歸針）
	DrawAimAzLocal = TattooNeedleAz;
	DrawAimTiltLocal = TattooNeedleTilt;
	const float EstDegPerCm = FMath::Max(TattooAngPerCmEst, 0.02f);
	float DAz = FMath::FindDeltaAngleDegrees(TattooNeedleAz, TattooCursorAz);
	float DTilt = TattooCursorTilt - TattooNeedleTilt;
	float AngDist = FMath::Sqrt(DAz * DAz + DTilt * DTilt);
	TattooChaseErrCm = AngDist / EstDegPerCm;
	if (TattooChaseErrCm <= TattooChaseStopCm)
	{
		TattooSpeedDebtCm = 0.0f; // 游標在針上=停針意圖：清債（原地扎；輕點=單點=dotwork）
		return;
	}
	const float BaseStepCm = TattooMaxSpeedCmPerSec() * FMath::Min(DeltaSeconds, 0.25f);
	if (BaseStepCm <= 0.0f)
	{
		return;
	}
	// 速度債（07-22 三修）：上幀短差本幀補；外環增益再補「步進折損→針的平滑
	// 路徑縮短」的系統性折損（見 TattooSpeedGain 註）。
	const float FullStepCm = BaseStepCm * TattooSpeedGain + TattooSpeedDebtCm;
	if (TattooChaseErrCm < FullStepCm)
	{
		// 貼手域（殘距<一步）：針直接落在游標上——慢工精描=亦步亦趨；band 步進器
		// 強迫走整步會繞點震盪（首驗 TP 實錘）。不記債不進增益視窗。
		TattooNeedleAz = TattooCursorAz;
		TattooNeedleTilt = TattooCursorTilt;
		DrawAimAzLocal = TattooNeedleAz;
		DrawAimTiltLocal = TattooNeedleTilt;
		bTattooCruising = true;
		TattooSpeedDebtCm = 0.0f;
		TattooGainCmdAccum = 0.0f;
		TattooGainActAccum = 0.0f;
		TattooChaseErrCm = 0.0f;
		return;
	}
	const float StepCm = FullStepCm;
	const FVector2D Dir(DAz / AngDist, DTilt / AngDist);

	FVector PNow;
	if (!TraceAimToTarget(FRotator(-TattooNeedleTilt, TattooNeedleAz, 0.0f).Vector(), PNow))
	{
		// 針下皮膚失蹤（身體動了/落針在剪影邊）：無回饋可量，按估計角速朝游標慢移
		const float SlideDeg = FMath::Min(AngDist, EstDegPerCm * BaseStepCm);
		TattooNeedleAz = FMath::UnwindDegrees(TattooNeedleAz + Dir.X * SlideDeg);
		TattooNeedleTilt = FMath::Clamp(TattooNeedleTilt + Dir.Y * SlideDeg,
			DrawTiltMinDeg, DrawTiltMaxDeg);
		TattooSpeedDebtCm = 0.0f; // 無量測=無帳可記
		bTattooCruising = true;
	}
	else
	{
		float MovedCm = 0.0f;
		if (CruiseStepOnSkin(TattooNeedleAz, TattooNeedleTilt, Dir, StepCm, TattooAngPerCmEst, PNow, MovedCm))
		{
			bTattooCruising = true;
			TattooDbgCruiseSecs += DeltaSeconds;
			TattooDbgHopCm += MovedCm;
			if (MovedCm < StepCm * 0.5f)
			{
				// 幾何受限（貼邊小步）：不記債不進增益視窗——受限樣本會把外環帶壞
				TattooSpeedDebtCm = 0.0f;
				TattooGainCmdAccum = 0.0f;
				TattooGainActAccum = 0.0f;
			}
			else
			{
				TattooSpeedDebtCm = FMath::Clamp(StepCm - MovedCm, -0.1f, 0.3f);
				// 外環增益視窗：每累積 1cm 命令距離對帳一次（實走量在出墨段累積）
				TattooGainCmdAccum += BaseStepCm;
				if (TattooGainCmdAccum >= 1.0f)
				{
					const float Ratio = TattooGainCmdAccum / FMath::Max(TattooGainActAccum, 0.05f);
					// 下限 0.7：閉環必須雙向可修——1.0 下限=只補不煞，折損消失的環境
					// 會恆定超速 15% 而增益鎖死在底（robo 兩輪實錘 tipSpd 2.72/gain=1.00）
					TattooSpeedGain = FMath::Clamp(
						FMath::Lerp(TattooSpeedGain, TattooSpeedGain * Ratio, 0.4f), 0.7f, 1.7f);
					TattooGainCmdAccum = 0.0f;
					TattooGainActAccum = 0.0f;
				}
			}
		}
		else
		{
			TattooSpeedDebtCm = 0.0f; // 邊緣釘住：清債（針原地扎，等新方向/抬針）
			TattooGainCmdAccum = 0.0f;
			TattooGainActAccum = 0.0f;
		}
	}

	// 步進動了針＝aim 再同步（相機/姿勢/複製全鏈=針）＋殘距重算（導引/summary）
	DrawAimAzLocal = TattooNeedleAz;
	DrawAimTiltLocal = TattooNeedleTilt;
	DAz = FMath::FindDeltaAngleDegrees(TattooNeedleAz, TattooCursorAz);
	DTilt = TattooCursorTilt - TattooNeedleTilt;
	TattooChaseErrCm = FMath::Sqrt(DAz * DAz + DTilt * DTilt) / EstDegPerCm;
}

namespace
{
	// 沿稿追蹤的稿線筆劃定位（Works 可能因洗墨/新筆劃改動——每 tick 以 WorkId 重解）
	const FInkStroke* ResolveFollowStroke(const ANiceInkCharacter* Target, int32 WorkId, int32 StrokeIdx)
	{
		if (!Target || !Target->InkCanvas)
		{
			return nullptr;
		}
		for (const FInkWork& W : Target->InkCanvas->GetWorks())
		{
			if (W.WorkId == WorkId)
			{
				return W.Strokes.IsValidIndex(StrokeIdx) ? &W.Strokes[StrokeIdx] : nullptr;
			}
		}
		return nullptr;
	}
}

bool ANiceInkCharacter::TryAcquireStencilFollow(const FVector& NeedleWorld)
{
	// 壓針點 SnapCm 內找最近稿線點（07-25 打稿制）。量距在 UV 空間（稿點距 ~0.2cm、
	// 圖集均勻紋素密度＝UV 距 × 常數 = 皮膚距；跨縫稿在縫上斷開＝誠實限制，
	// 放開重壓另一側接續）。方向＝朝點多的一端（點少側幾步就走完；要回頭＝重壓）。
	FollowWorkId = INDEX_NONE;
	ANiceInkCharacter* Target = LeanTarget.Get();
	if (!Target || !Target->Body || !Target->InkCanvas)
	{
		return false;
	}
	FVector2D NeedleUV;
	if (!Target->Body->ResolveBodyUV(NeedleWorld, NeedleUV, /*MaxDistance=*/5.0f))
	{
		return false;
	}
	constexpr float CmPerUv = 332.0f; // 2048px ÷ 0.617px/mm（UV0 均勻紋素密度）
	const float SnapUv = TattooStencilSnapCm / CmPerUv;
	float BestD2 = SnapUv * SnapUv;
	int32 BestStrokePoints = 0;
	for (const FInkWork& W : Target->InkCanvas->GetWorks())
	{
		if (W.State != EInkWorkState::Marker)
		{
			continue;
		}
		for (int32 S = 0; S < W.Strokes.Num(); ++S)
		{
			const FInkStroke& St = W.Strokes[S];
			if (St.NeedleType != EInkNeedle::Stencil)
			{
				continue;
			}
			for (int32 Pi = 0; Pi < St.Points.Num(); ++Pi)
			{
				const float D2 = static_cast<float>((St.Points[Pi] - NeedleUV).SizeSquared());
				if (D2 < BestD2)
				{
					BestD2 = D2;
					FollowWorkId = W.WorkId;
					FollowStrokeIdx = S;
					FollowPointIdx = Pi;
					BestStrokePoints = St.Points.Num();
				}
			}
		}
	}
	if (FollowWorkId == INDEX_NONE)
	{
		return false;
	}
	FollowDir = (BestStrokePoints - 1 - FollowPointIdx >= FollowPointIdx) ? 1 : -1;
	return true;
}

bool ANiceInkCharacter::UpdateStencilFollow(float DeltaSeconds, const FVector& PNow)
{
	// 沿稿步進（07-25）：waypoint＝下一個距現針 ≥ 半步的稿點；步進走 CruiseStepOnSkin
	// ＝速度上限/皮膚面恆速/浮雕跨越/邊緣釘住全部沿用——機器只是把方向盤交給了稿。
	// 回 false＝稿走完/失效（呼叫端收沿稿、停針原地扎）。
	ANiceInkCharacter* Target = LeanTarget.Get();
	if (!Target || !Target->Body)
	{
		return false;
	}
	const FInkStroke* St = ResolveFollowStroke(Target, FollowWorkId, FollowStrokeIdx);
	if (!St || St->NeedleType != EInkNeedle::Stencil || St->Points.Num() == 0)
	{
		return false;
	}
	const float BaseStepCm = TattooMaxSpeedCmPerSec() * FMath::Min(DeltaSeconds, 0.25f);
	if (BaseStepCm <= 0.0f)
	{
		return true;
	}
	FVector Wp = FVector::ZeroVector;
	bool bHaveWp = false;
	for (int32 Guard = 0; Guard < 64; ++Guard)
	{
		if (!St->Points.IsValidIndex(FollowPointIdx))
		{
			return false; // 稿走完
		}
		if (!Target->Body->ResolveUVToWorld(St->Points[FollowPointIdx], Wp))
		{
			FollowPointIdx += FollowDir; // 解不回世界的稿點（拓樸邊角）：跳過
			continue;
		}
		if (FVector::Dist(PNow, Wp) >= FMath::Max(BaseStepCm * 0.5f, 0.05f))
		{
			bHaveWp = true;
			break;
		}
		FollowPointIdx += FollowDir; // 已到這個稿點：前進
	}
	if (!bHaveWp)
	{
		return false;
	}
	const FVector Eye = GetAimRayOrigin();
	const FRotator ToWp = (Wp - Eye).Rotation();
	const float TgtTilt = FMath::Clamp(-ToWp.Pitch, DrawTiltMinDeg, DrawTiltMaxDeg);
	const float DAz = FMath::FindDeltaAngleDegrees(TattooNeedleAz, ToWp.Yaw);
	const float DTilt = TgtTilt - TattooNeedleTilt;
	const float AngDist = FMath::Sqrt(DAz * DAz + DTilt * DTilt);
	if (AngDist <= KINDA_SMALL_NUMBER)
	{
		FollowPointIdx += FollowDir;
		return true;
	}
	const FVector2D Dir(DAz / AngDist, DTilt / AngDist);
	FVector P = PNow;
	float MovedCm = 0.0f;
	if (CruiseStepOnSkin(TattooNeedleAz, TattooNeedleTilt, Dir, BaseStepCm, TattooAngPerCmEst, P, MovedCm))
	{
		bTattooCruising = true;
		TattooDbgCruiseSecs += DeltaSeconds;
		TattooDbgHopCm += MovedCm;
	}
	// 步進失敗＝邊緣釘住：留在沿稿模式原地扎（跨縫/剪影的誠實極限）
	return true;
}

bool ANiceInkCharacter::CruiseStepOnSkin(float& AzDeg, float& TiltDeg, const FVector2D& Dir,
	float StepCm, float& AngPerCmEst, FVector& InOutP, float& OutMovedCm) const
{
	// 巡航與導引預測共用的單步核心（07-22 二改抽出；三修=對稱收斂帶）：
	// 首版只鉗超速＝單向偏差——低於目標的步長無條件放行，量測噪聲下平均步長
	// 系統性偏短（12Hz 實測掉到 70% v_max、robo 數據實錘；單向鉗位必產單向偏差
	// ＝lift 護束老教訓的變體）。對稱帶 [0.9,1.05]×Step 兩側都重試；六輪未進帶
	// 取「帶下最佳候選」而非凍幀（幾何受限的貼邊小步也算誠實前進，殘差交給債務）。
	OutMovedCm = 0.0f;
	float AngDeg = FMath::Clamp(AngPerCmEst * StepCm, 0.0005f, 5.0f);
	float BestAng = -1.0f;
	float BestDs = 0.0f;
	float BestAz = 0.0f;
	float BestTilt = 0.0f;
	FVector BestP = FVector::ZeroVector;
	// 浮雕跳段候選（07-22 五修）：所有超帶候選中跳距最小者——凸起（乳頭/肚臍/下顎）
	// 的自遮盲帶讓任何角度步都跳過去，縮步收斂不了；最小跳＝以最小角度剛好落到
	// 凸起對側的那一點
	float JumpDs = 1e9f;
	float JumpAz = 0.0f;
	float JumpTilt = 0.0f;
	FVector JumpP = FVector::ZeroVector;
	for (int32 It = 0; It < 6; ++It)
	{
		const float CandAz = FMath::UnwindDegrees(AzDeg + Dir.X * AngDeg);
		const float CandTilt = FMath::Clamp(TiltDeg + Dir.Y * AngDeg,
			DrawTiltMinDeg, DrawTiltMaxDeg);
		FVector PNew;
		if (!TraceAimToTarget(FRotator(-CandTilt, CandAz, 0.0f).Vector(), PNew))
		{
			AngDeg *= 0.5f; // 滑出剪影：縮步貼邊
			continue;
		}
		const float Ds = FVector::Dist(InOutP, PNew);
		if (Ds <= StepCm * 1.05f && Ds > BestDs)
		{
			BestAng = AngDeg;
			BestDs = Ds;
			BestAz = CandAz;
			BestTilt = CandTilt;
			BestP = PNew;
		}
		if (Ds > StepCm * 1.05f)
		{
			if (Ds < JumpDs)
			{
				JumpDs = Ds;
				JumpAz = CandAz;
				JumpTilt = CandTilt;
				JumpP = PNew;
			}
			// 超速：比例縮回再試（跳段時 Ds 巨大＝縮到近零；帶上緣=速率契約）
			AngDeg *= StepCm / FMath::Max(Ds, KINDA_SMALL_NUMBER);
			continue;
		}
		if (Ds < StepCm * 0.9f)
		{
			// 低於帶：放大重試（縮放比鉗 ×4——近零 Ds 的比例放大會爆衝）
			AngDeg = FMath::Min(AngDeg * FMath::Clamp(
				StepCm / FMath::Max(Ds, KINDA_SMALL_NUMBER), 1.0f, 4.0f), 5.0f);
			continue;
		}
		AzDeg = CandAz;
		TiltDeg = CandTilt;
		InOutP = PNew;
		OutMovedCm = Ds;
		// 角/公分換算的回饋更新（半衰混合；下一發首試即近命中，迭代通常 1 輪收斂）
		AngPerCmEst = FMath::Clamp(
			FMath::Lerp(AngPerCmEst, AngDeg / Ds, 0.5f), 0.02f, 20.0f);
		return true;
	}
	if (BestDs > StepCm * 0.02f)
	{
		// 未進帶但有效：取最接近目標的帶下候選（貼邊/掠射的誠實小步）
		AzDeg = BestAz;
		TiltDeg = BestTilt;
		InOutP = BestP;
		OutMovedCm = BestDs;
		if (BestDs > 0.02f)
		{
			AngPerCmEst = FMath::Clamp(
				FMath::Lerp(AngPerCmEst, BestAng / BestDs, 0.5f), 0.02f, 20.0f);
		}
		return true;
	}
	if (JumpDs <= TattooReliefJumpCm)
	{
		// 浮雕跨越（07-22 五修）：小跳=凸起自遮盲帶，針騎過去落對側——盲帶誠實
		// 留白（看到哪畫到哪的固有邊界）。速度記帳只算一步（跳距非真皮膚路程、
		// 不給免費里程）；est 不吃跳距（那不是角/公分換算資料）。
		// >容差或射線離體＝真邊緣照舊釘住（跨肢誤連線防護保留）。
		AzDeg = JumpAz;
		TiltDeg = JumpTilt;
		InOutP = JumpP;
		OutMovedCm = StepCm;
		return true;
	}
	return false; // 連小步都無效且跳距超容差＝真邊緣（釘住）
}

int32 ANiceInkCharacter::BuildTattooGuidePath(TArray<FVector>& OutPoints) const
{
	// 導引預測路徑（08-04 二輪修）：從針位沿「針→游標」方向模擬步進器——
	// 待走路徑、長度=追趕殘距（虛線盡頭=你指的目標點；皮繩退役後殘距可以很長
	// =誠實預告整段）；本地複本（az/tilt/est/P）模擬、巡航狀態零污染；
	// 線=貼膚曲線、停在剪影邊緣＝「針會在這裡釘住」在拉過去之前就預告。
	OutPoints.Reset();
	if (!bLeanLocked || !bPenTriggerLocal || SelectedNeedle != EInkNeedle::Liner ||
		!bTattooChaseActive || bStencilFollowActive || TattooChaseErrCm <= TattooGuideShowCm)
	{
		return 0;
	}
	float Az = TattooNeedleAz;
	float Tilt = TattooNeedleTilt;
	float Est = TattooAngPerCmEst;
	FVector P;
	if (!TraceAimToTarget(FRotator(-Tilt, Az, 0.0f).Vector(), P))
	{
		return 0;
	}
	OutPoints.Add(P);
	const float Step = FMath::Max(TattooGuideStepCm, 0.1f);
	// 逐步朝游標追（08-04 三修，與針的真實追法同構）：每步重算方向、剩餘角距
	// 不足一步即停——一次性方向＋估計長度的舊版在斜面/曲面上估計失準＝虛線
	// 多走一節、末端脫離游標十字（user 抓）。64 步上限=48cm 防極端長線吃 trace。
	bool bReachedCursor = false;
	for (int32 i = 0; i < 64; ++i)
	{
		const float DAz = FMath::FindDeltaAngleDegrees(Az, TattooCursorAz);
		const float DTilt = TattooCursorTilt - Tilt;
		const float AngD = FMath::Sqrt(DAz * DAz + DTilt * DTilt);
		if (AngD <= Step * FMath::Max(Est, 0.02f))
		{
			bReachedCursor = true; // 剩不足一步＝到游標
			break;
		}
		const FVector2D Dir(DAz / AngD, DTilt / AngD);
		float MovedCm = 0.0f;
		if (!CruiseStepOnSkin(Az, Tilt, Dir, Step, Est, P, MovedCm))
		{
			break; // 邊緣＝導引線誠實截斷（針會在這裡釘住；游標可指在剪影外）
		}
		OutPoints.Add(P);
	}
	// 終點釘在游標的真實命中點上＝虛線末端與十字重合（構造保證）
	if (bReachedCursor)
	{
		FVector CurP;
		if (TraceAimToTarget(FRotator(-TattooCursorTilt, TattooCursorAz, 0.0f).Vector(), CurP))
		{
			OutPoints.Add(CurP);
		}
	}
	return OutPoints.Num();
}

void ANiceInkCharacter::ServerUpdateDrawAim_Implementation(float AzDeg, float TiltDeg, FVector_NetQuantize TargetW, bool bTargetValid)
{
	if (!bLeanLocked)
	{
		return;
	}
	DrawAimAzDeg = FMath::UnwindDegrees(AzDeg);
	DrawAimTiltDeg = FMath::Clamp(TiltDeg, DrawTiltMinDeg, DrawTiltMaxDeg);
	DrawTargetRepW = TargetW;
	bDrawTargetRepValid = bTargetValid;
}

float ANiceInkCharacter::EffectiveDrawAz() const
{
	// 本人＝One Euro 濾波值（姿勢/筆/墨的共同驅動源）；他端＝複製追趕值。
	// 相機不走這裡（UpdateLeanCamera 直讀生值＝視角零延遲）。
	// 稿筆＝生 aim（08-02 user 定案「讓滑鼠當純輸入」）：游標/墨/姿勢全鏈同源
	// 零延遲——One Euro 是 07-20「畫面歸針」時代防姿勢抖的層，自由游標制下它
	// 變成游標的直接滯後（慢速 ~0.16s 果凍感）。靜止不抖由構造保證（滑鼠
	// delta=0→aim 靜止→P 走快取）；移動中手抖 ≈0.5mm 對公尺級身體姿勢不可見。
	// 機器工具照舊吃濾波（巡航/導引鏈粒度敏感、且畫面歸針下滯後無感）。
	if (IsLocallyControlled())
	{
		return SelectedNeedle == EInkNeedle::Stencil ? DrawAimAzLocal : DrawAimAzFilt;
	}
	return RemoteDrawAzDeg;
}

float ANiceInkCharacter::EffectiveDrawTilt() const
{
	if (IsLocallyControlled())
	{
		return SelectedNeedle == EInkNeedle::Stencil ? DrawAimTiltLocal : DrawAimTiltFilt;
	}
	return RemoteDrawTiltDeg;
}

void ANiceInkCharacter::DebugRoboDrawAim(float AzDeg, float TiltDeg)
{
	bHasPendingDebugDrawAim = true;
	PendingDebugDrawAim = FVector2D(AzDeg, TiltDeg);
}

void ANiceInkCharacter::DebugRoboPaintHold(bool bHold)
{
	bDebugPaintHeld = bHold;
}

void ANiceInkCharacter::DebugRoboMouse(float DX, float DY)
{
	// 合成滑鼠增量（07-31 稿筆游標制探針）：走真人管線（PollDrawAim→
	// UpdateStencilCursor）——角度命令會 relatch 游標＝量不到增益/漂移，此鉤子量得到
	bHasPendingDebugMouse = true;
	PendingDebugMouse += FVector2D(DX, DY);
}

bool ANiceInkCharacter::GetStencilCursorHudWorld(FVector& Out) const
{
	if (!bLeanLocked || !IsLocallyControlled() || SelectedNeedle != EInkNeedle::Stencil)
	{
		return false;
	}
	if (bDrawTargetValid)
	{
		Out = DrawTargetWorld; // P=墨的落點＝單一真相（游標/筆/墨同點）
		return true;
	}
	// 出剪影（角度制退路）：筆浮在 aim 射線的基準距離上（無 P＝無小點/✕，只有筆）
	Out = GetAimRayOrigin() +
		FRotator(-DrawAimTiltLocal, DrawAimAzLocal, 0.0f).Vector() * DrawCursorRefDistCm;
	return true;
}

bool ANiceInkCharacter::GetTattooCursorHudWorld(FVector& Out) const
{
	// 巡航中的自由游標＝針→游標待走路徑的終點（十字記號；虛線盡頭與它重合）
	if (!bLeanLocked || !IsLocallyControlled() || SelectedNeedle != EInkNeedle::Liner ||
		!bTattooChaseActive || TattooChaseErrCm <= TattooGuideShowCm)
	{
		return false;
	}
	if (!TraceAimToTarget(FRotator(-TattooCursorTilt, TattooCursorAz, 0.0f).Vector(), Out))
	{
		// 游標射線出剪影：浮在射線基準距離上（照樣可指——針會走到剪影邊釘住）
		Out = GetAimRayOrigin() +
			FRotator(-TattooCursorTilt, TattooCursorAz, 0.0f).Vector() * DrawCursorRefDistCm;
	}
	return true;
}

bool ANiceInkCharacter::GetStencilLazyTipHudWorld(FVector& Out) const
{
	// 拉繩墨尖（08-02）：只在本人稿筆繪製中有效——2D 筆錨到這裡=「筆尖在墨
	// 出處」；不繪製/繩關閉時 HUD 落回游標錨（GetStencilCursorHudWorld）
	if (!bLeanLocked || !IsLocallyControlled() || SelectedNeedle != EInkNeedle::Stencil ||
		!bPainting || !bStencilLazyValid || StencilLazyRadiusCm <= 0.01f)
	{
		return false;
	}
	Out = StencilLazyTip;
	return true;
}


void ANiceInkCharacter::DebugRoboNeedle(int32 NeedleIndex)
{
	bHasPendingDebugNeedle = true;
	// 0=Liner 1=Shader 2=Stencil（07-25 打稿制；與 EInkNeedle 值一致）
	PendingDebugNeedle = NeedleIndex == 2 ? EInkNeedle::Stencil
		: (NeedleIndex == 1 ? EInkNeedle::Shader : EInkNeedle::Liner);
}

void ANiceInkCharacter::DebugRoboColor(int32 ColorIndex)
{
	SelectedColorIndex = FMath::Clamp(ColorIndex, 0, FNiceInkPalette::Num() - 1);
	StopPaintingLocal(); // 換色=重開筆劃（顏色是 per-stroke 屬性，與鍵盤路徑同語義）
}

void ANiceInkCharacter::DebugRoboPaintStick(float X, float Y)
{
	DebugPaintStickPx = FVector2D(X, Y);
	bDebugPaintStickActive = DebugPaintStickPx.Size() > KINDA_SMALL_NUMBER;
	if (!bDebugPaintStickActive)
	{
		// 解除命令＝模擬「手停」：游標收回針上（robo 沒有滑鼠可停；不收回=
		// 殘餘游標距讓針多追＝打破停針冪等契約）
		TattooCursorAz = TattooNeedleAz;
		TattooCursorTilt = TattooNeedleTilt;
		DrawAimAzLocal = TattooNeedleAz;
		DrawAimTiltLocal = TattooNeedleTilt;
	}
}

FVector ANiceInkCharacter::GetAimRayOrigin() const
{
	// 眼錨定（07-20）：入畫收斂後＝世界定點（與 UpdateLeanCamera 共用⇒準星=P 精確）。
	// 錨定前（入鎖首幀）＝ActorLoc+60——與 server 算 aim 初值的眼睛同一公式，
	// 首幀 P≈LeanPoint。絕不讀活骨骼：骨骼被解算驅動＝自我參照回饋（滑走實錘）。
	if (bDrawEyeAnchorValid)
	{
		return DrawEyeAnchorWorld;
	}
	return GetActorLocation() + FVector(0.0f, 0.0f, 60.0f);
}

bool ANiceInkCharacter::TraceAimToTarget(const FVector& DirWorld, FVector& OutImpact) const
{
	// 中心射線→受害者皮膚命中點。其他角色一律被射線無視（ghost＝別人的頭脖永遠
	// 擋不住你的筆）。射程只是找 P 的幾何上限——「搆不搆得到」由姿勢解算裁決
	//（舊 PaintReach=140 讓「站高俯瞰低點」入座首幀差 2cm 打不到＝低位永遠無 P，
	// 07-20 robo 實錘：eye 高 152、點高 37、距離 142）。
	ANiceInkCharacter* Target = LeanTarget.Get();
	if (!Target || !Target->Body || !GetWorld())
	{
		return false;
	}
	const FVector Origin = GetAimRayOrigin();
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(NiceInkAimTrace), /*bInTraceComplex=*/true);
	QueryParams.AddIgnoredActor(this);
	for (TActorIterator<ANiceInkCharacter> It(GetWorld()); It; ++It)
	{
		if (*It != Target)
		{
			QueryParams.AddIgnoredActor(*It);
		}
	}
	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin + DirWorld * 300.0f,
			ECC_Visibility, QueryParams) ||
		Hit.GetActor() != Target)
	{
		return false;
	}
	OutImpact = Hit.ImpactPoint;
	return true;
}

bool ANiceInkCharacter::ResolveAimToTargetUV(const FVector& DirWorld, FVector2D& OutUV,
	const FVector2D* PrevUV) const
{
	// 命中點→UV。容差 1.5mm＝打在褌上不落墨（「褌下不可畫＝物理遮擋」）；
	// PrevUV＝縫區連續性偏好（兩島搶點的浮點來回跳＝筆跡毛邊真兇，07-20 實錘）。
	ANiceInkCharacter* Target = LeanTarget.Get();
	FVector Impact;
	if (!Target || !Target->Body || !TraceAimToTarget(DirWorld, Impact))
	{
		return false;
	}
	return Target->Body->ResolveBodyUV(Impact, OutUV, /*MaxDistance=*/0.15f, PrevUV);
}

void ANiceInkCharacter::ApplyGhostView(bool bEnable)
{
	// 畫畫時除自己與沉睡者外，其餘人半透明（本端視覺；碰撞穿透在 OnRep_Lean 全域處理）。
	// 冪等；還原＝「從各自的真相重建」而非材質快照（被換下的 MID 失去引用會被 GC 反殺）。
	if (!bEnable)
	{
		for (const TWeakObjectPtr<ANiceInkCharacter>& Weak : GhostedChars)
		{
			if (ANiceInkCharacter* C = Weak.Get())
			{
				C->SetActorHiddenInGame(false);
				C->ReapplyCanonicalMaterials();
			}
		}
		GhostedChars.Reset();
		return;
	}
	for (TActorIterator<ANiceInkCharacter> It(GetWorld()); It; ++It)
	{
		ANiceInkCharacter* C = *It;
		if (C == this || C == LeanTarget.Get() || GhostedChars.Contains(C))
		{
			continue;
		}
		if (GhostMaterial)
		{
			TInlineComponentArray<UMeshComponent*> Meshes(C);
			for (UMeshComponent* M : Meshes)
			{
				if (!M || M == C->PenMesh || M == C->GripMesh || M == C->NeedleMesh ||
					M == C->MarkerPen || Cast<UNeckStretchComponent>(M))
				{
					continue; // 筆/握管/針/打稿麥克筆微小、伸縮脖收合時本就隱藏——不 ghost
				}
				for (int32 i = 0; i < M->GetNumMaterials(); ++i)
				{
					M->SetMaterial(i, GhostMaterial);
				}
			}
			C->bGhostMaterialApplied = true; // 站姿 MID 晚綁防護讓路（勿覆蓋半透明）
		}
		else
		{
			C->SetActorHiddenInGame(true); // 資產缺席的退化：全隱（比穿模誠實）
		}
		GhostedChars.Add(C);
	}
}

void ANiceInkCharacter::ReapplyCanonicalMaterials()
{
	bGhostMaterialApplied = false; // ghost 解除＝站姿 MID 防護恢復巡邏
	// Body：MID 重建/重綁（臉貼圖、RT、眼罩、膚色全部回真相）
	if (Body && InkCanvas)
	{
		Body->BindCanvas(InkCanvas);
	}
	// BowBody：皮膚槽回身體 MID、褌槽回資產材質（EnsurePoseableAsset 的指派邏輯重申）
	if (BowBody && BowBody->GetSkinnedAsset() && Body && Body->GetDynamicMaterial())
	{
		if (const USkeletalMesh* Sk = Cast<USkeletalMesh>(BowBody->GetSkinnedAsset()))
		{
			const TArray<FSkeletalMaterial>& SkMats = Sk->GetMaterials();
			for (int32 i = 0; i < SkMats.Num(); ++i)
			{
				if (SkMats[i].MaterialSlotName.ToString().Contains(TEXT("Fundoshi")))
				{
					BowBody->SetMaterial(i, SkMats[i].MaterialInterface);
				}
				else
				{
					BowBody->SetMaterial(i, Body->GetDynamicMaterial());
				}
			}
		}
	}
	// 其餘網格件（髮髻等道具）：回資產預設材質
	TInlineComponentArray<UMeshComponent*> Meshes(this);
	for (UMeshComponent* M : Meshes)
	{
		if (!M || M == Body || M == BowBody || M == PenMesh || M == GripMesh ||
			M == NeedleMesh || Cast<UNeckStretchComponent>(M))
		{
			continue;
		}
		if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(M))
		{
			if (const UStaticMesh* SM = SMC->GetStaticMesh())
			{
				const TArray<FStaticMaterial>& Mats = SM->GetStaticMaterials();
				for (int32 i = 0; i < Mats.Num(); ++i)
				{
					SMC->SetMaterial(i, Mats[i].MaterialInterface);
				}
			}
		}
	}
}

// --- 貼臉鎖定：鎖定中的游標作畫／偷瞄／起身 ---

void ANiceInkCharacter::PollLockedDraw(APlayerController* PC, float DeltaSeconds)
{
	if (!bLeanLocked)
	{
		StopPaintingLocal();
		bPenTriggerLocal = false; // 伺服器端由 ForceExitLean 清；這裡只清本地鏡像
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

	// LMB 狀態先讀（07-22 刺青巡航：滑鼠的歸屬依此分流——按住=方向拉桿、放開=自由 aim）
	const float Now = GetWorld()->GetTimeSeconds();
	const bool bCanPaint = (Now - LeanLockTime) > LeanPaintDelay;
	const bool bWantsPaint = bCanPaint && (PC->IsInputKeyDown(EKeys::LeftMouseButton) || bDebugPaintHeld);

	// 伸縮針觸發（2026-07-21）：LMB 邊緣同步——本人端本地鏡像零延遲，
	// RPC 只在變化時發（他端針視覺吃複製值；pattern 同裝睡）
	if (bWantsPaint != bPenTriggerLocal)
	{
		bPenTriggerLocal = bWantsPaint;
		ServerSetPenTrigger(bWantsPaint);
	}

	// 滾輪切工具（07-23 雙針制；07-25 打稿制 user 定案三檔）：Stencil 麥克筆（預設，
	// 打稿）→ Liner（割線）→ Shader（打霧）循環，下滾=下一件、上滾=上一件
	//（08-02 user 定案對齊主流慣例：Minecraft/FPS 換武器皆下滾前進）；
	// 鎖定中滾輪閒置（轉盤=受害者迷宮情境）、數字鍵=調色盤。
	// 換工具=抬針重開筆劃（渲染屬性是 per-stroke）；狀態跟選色同壽命；
	// RepNeedle 上服=他端 3D 手持模型跟著換（麥克筆 vs 刺青機）。
	auto SetNeedleLocal = [&](EInkNeedle NewNeedle)
	{
		if (SelectedNeedle == NewNeedle)
		{
			return;
		}
		// 切工具視野不跳（08-02 user 定案「以切換時視野在哪為準」）：aim 先歸位到
		// 當前相機朝向——稿筆（凍結相機）與機器（畫面歸針）的參考系差全部收進
		// 「游標歸中」（小、恆可預期），視野凍在原地。兩個方向都成立：稿筆→機器
		// =相機續讀 aim（=舊凍結朝向）；機器→稿筆=凍結相機從 aim 重播種（=舊畫面）。
		// robo 角度命令同 tick 在後（PollDrawAim 消化）照舊覆寫＝角度契約原樣。
		const bool bFrozenCamNow = SelectedNeedle == EInkNeedle::Stencil && bDrawCamInit;
		const float CamAzNow = bTattooChaseActive ? TattooNeedleAz
			: (bFrozenCamNow ? DrawCamAz : DrawAimAzLocal);
		const float CamTiltNow = bTattooChaseActive ? TattooNeedleTilt
			: (bFrozenCamNow ? DrawCamTilt : DrawAimTiltLocal);
		DrawAimAzLocal = FMath::UnwindDegrees(CamAzNow);
		DrawAimTiltLocal = FMath::Clamp(CamTiltNow, DrawTiltMinDeg, DrawTiltMaxDeg);
		bMistPrevAimValid = false; // 歸位是傳送不是手速——不進霧針移動閘量測
		SelectedNeedle = NewNeedle;
		StopPaintingLocal();
		bDrawCursorRelatch = true; // 切到稿筆＝游標從當前 aim 命中點再生（機器檔不消化）
		if (HasAuthority())
		{
			RepNeedle = NewNeedle;
		}
		else
		{
			ServerSetNeedle(NewNeedle);
		}
	};
	if (bHasPendingDebugNeedle)
	{
		bHasPendingDebugNeedle = false;
		SetNeedleLocal(PendingDebugNeedle);
	}
	const bool bScrollUp = PC->WasInputKeyJustPressed(EKeys::MouseScrollUp);
	if (bScrollUp || PC->WasInputKeyJustPressed(EKeys::MouseScrollDown))
	{
		constexpr int32 ToolCount = 3; // Stencil→Liner→Shader（EInkNeedle 值 2/0/1）
		auto CycleOrder = [](EInkNeedle N) -> int32
		{
			return N == EInkNeedle::Stencil ? 0 : (N == EInkNeedle::Liner ? 1 : 2);
		};
		static constexpr EInkNeedle Order[ToolCount] =
			{ EInkNeedle::Stencil, EInkNeedle::Liner, EInkNeedle::Shader };
		const int32 Next = (CycleOrder(SelectedNeedle) + (bScrollUp ? ToolCount - 1 : 1)) % ToolCount;
		SetNeedleLocal(Order[Next]);
		NiAudio::Play(this, ENiSound::UiClick, 0.5f);
	}

	// 滑鼠＝臉指向或方向拉桿。巡航=液線針專屬（慢而穩=割線手法）；
	// 霧針按住左鍵仍是自由 aim＝噴槍式自由揮掃（打霧手法=手擁有速度，07-23 四版）。
	// 液線針抬針（放開左鍵）＝瞬間拿回全速 aim。
	PollDrawAim(PC, DeltaSeconds, bWantsPaint && SelectedNeedle == EInkNeedle::Liner);

	// 霧針移動閘量測（每 tick 無條件更新——跨越早退幀的殘差會偽造移動暴衝）：
	// 量在 raw aim 上＝滑鼠靜止時 delta 精確為零，解算抖動偽造不了移動。
	// 短窗 EMA（τ≈0.08s）：高幀率下滑鼠回報率 < 幀率＝沒回報的幀瞬時速度為 0、
	// 閘每隔幾幀閃關一次——瞬時值當閘=排帶斷格真兇（robo 每 tick 餵 aim 測不到；
	// 「手在動」是 0.1s 尺度的事實，不是單幀的事實）
	{
		const float InstSpeed = (bMistPrevAimValid && DeltaSeconds > KINDA_SMALL_NUMBER)
			? (FMath::Abs(FMath::FindDeltaAngleDegrees(DrawAimAzLocal, MistPrevAimAz)) +
				FMath::Abs(DrawAimTiltLocal - MistPrevAimTilt)) / DeltaSeconds
			: 0.0f;
		const float K = FMath::Clamp(DeltaSeconds / 0.08f, 0.0f, 1.0f);
		MistAimSpeedDegS = FMath::Lerp(MistAimSpeedDegS, InstSpeed, K);
	}
	MistPrevAimAz = DrawAimAzLocal;
	MistPrevAimTilt = DrawAimTiltLocal;
	bMistPrevAimValid = true;

	// 收筆裁決＝橢圓公式（07-29 單一裁判制；user 定案「嚴格最大橢圓」）：
	// 擬合完成後「橢圓外⟺不能畫」——灰紗畫到哪、筆就在哪抬起，顯示與行為同一條
	// 公式＝構造保證零縫；未擬合完（入鎖 ~0.5s 內）退回活解算 reach。
	// 巡航（Liner 皮繩）沿用活 reach：機器工具自有導引/浮雕/速度契約機構。
	{
		bCursorDrawable = bDrawTipReachable;
		if (bReachEllipseReady && bDrawTargetValid && !bTattooChaseActive)
		{
			bCursorDrawable = IsInsideReachEllipse(DrawTargetWorld);
		}
	}

	// 搆不到計時（HUD 提示閘）：有目標但裁決不可畫才累積；看向房間（無 P）不算
	if (bDrawTargetValid && !bCursorDrawable)
	{
		DrawUnreachSecs += DeltaSeconds;
	}
	else
	{
		DrawUnreachSecs = 0.0f;
	}

	// 拉繩收筆補完（08-02）：放開左鍵的這一 tick 繩長歸零，墨尖走完最後 ≤L 的
	// 鬆繩段再收筆——不補=每條線恆短一截（「筆跟不上」讀感）。補完段走同一條
	// 出墨機（StopPaintingLocal 會先 flush 再 End，補完點不丟）
	const bool bStencilCatchUp = !bWantsPaint && bPainting && bStencilLazyValid &&
		SelectedNeedle == EInkNeedle::Stencil &&
		PaintTarget.IsValid() && PaintTarget.Get() == LeanTarget.Get();
	if (!bWantsPaint && !bStencilCatchUp)
	{
		StopPaintingLocal();
		bHasLastPaintTip = false;
		bStencilLazyValid = false;
		return;
	}

	ANiceInkCharacter* Target = LeanTarget.Get();
	if (!Target || !Target->Body)
	{
		StopPaintingLocal();
		return;
	}

	// 可畫才落墨（裁決=遮罩，見上）。刺青機再加一閘：針必須實際伸出（UpdatePenVisual
	// 的針軸 trace 命中；上一 tick 值＝首個觸發 tick 針還沒彈出、墨慢一 tick=針彈出即墨）
	// ——針懸空沒碰到就出墨＝視覺謊言（第三者因果：針戳進肉才有墨）。
	// 打稿麥克筆無伸縮針＝此閘不適用（筆尖壓膚即畫）。
	if (!bCursorDrawable || !bPenStateValid ||
		(bPenIsMachineAsset && SelectedNeedle != EInkNeedle::Stencil &&
			PenNeedleLenCm <= PenNeedleStubCm + 0.01f))
	{
		StopPaintingLocal();
		bHasLastPaintTip = false;
		return;
	}

	// 接觸點＝筆軸與皮膚表面的交點：沿筆軸壓入 trace（筆尾側 2.5cm→筆尖前 3cm，
	// 涵蓋解算殘差 1.5＋寫入誤差 0.5）→ 表面命中點→UV（容差 1.5mm＝打在褌上
	// 不落墨的物理遮擋語義原樣保留）；PrevUV 鏈＝縫區兩島搶點的連續性偏好。
	FCollisionQueryParams TipQP(SCENE_QUERY_STAT(NiceInkTipTrace), /*bInTraceComplex=*/true);
	for (TActorIterator<ANiceInkCharacter> It(GetWorld()); It; ++It)
	{
		if (*It != Target)
		{
			TipQP.AddIgnoredActor(*It); // 同 aim trace：只認 LeanTarget、無視其他角色
		}
	}
	auto TipToSurfaceUV = [&](const FVector& TipP, const FVector2D* Prev, FVector2D& OutUV) -> bool
	{
		FHitResult Hit;
		const FVector A = TipP + PenShaftDirWorld * 2.5f;
		const FVector B = TipP - PenShaftDirWorld * 3.0f;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, A, B, ECC_Visibility, TipQP) ||
			Hit.GetActor() != Target)
		{
			return false;
		}
		return Target->Body->ResolveBodyUV(Hit.ImpactPoint, OutUV, /*MaxDistance=*/0.15f, Prev);
	};

	// --- 點狀出墨＝距離節拍（07-22 刺青手感 user 定案；二版）---
	// 墨的釋出＝離散扎針：線不是被拖出來的，是被一針一針組裝出來的。首針在觸發
	// 接觸瞬間（針彈出=墨流出的因果），其後沿筆尖路徑**每 k×筆寬落一針**——針距=
	// 構造保證的實線（首版時間節拍被 robo 抓到：姿勢解算的橫向抖動 ±0.2cm 疊在
	// 前進間距上＝針距尾巴 0.31 破實線界；距離制對抖動免疫）。5Hz 節拍在最高速下
	// 自然湧現（v_max/間距=f）；預算天花板 f×1.5 防異常快移灌針（robo 傳送/hitch）。
	// 幀內多針沿路徑內插（hitch 不留縫——覆蓋保證按最壞幀推，r15 老教訓）。
	// 舊 0.5cm 連續軌跡取樣退役——點與點之間不再內插，縫區內插毛邊整類病失去載體。
	const bool bStrokeOpen = bPainting && PaintTarget.Get() == Target;
	FVector TipNow = PenTipWorld;
	// 稿筆拉繩穩定器（08-02）：墨尖 B 被定長繩拖著追游標 P——d≤L 繩鬆不動、
	// d>L 沿 B→P 前進 (d−L)＝手擁有速度（每 tick 只走手多拉出的距離；鬆繩存量
	// 不丟帳，收筆補完吐出）。路徑=追逐曲線＝以 L 為截止尺度的空間低通（垂直
	// 抖動被繩長按比例壓掉）。只動墨鏈的 TipNow——PenTipWorld/游標 P/✕/小點/
	// 邊緣推擠全不經此（繩子只拉回饋面）；首針即點原樣（繩起點=按下點）。
	if (SelectedNeedle == EInkNeedle::Stencil && IsLocallyControlled() &&
		StencilLazyRadiusCm > 0.01f)
	{
		if (!bStrokeOpen || !bStencilLazyValid)
		{
			StencilLazyTip = TipNow; // 繩起點=按下點（首針即點打在生游標上）
			bStencilLazyValid = true;
		}
		else
		{
			const float L = bStencilCatchUp ? 0.0f : StencilLazyRadiusCm;
			const FVector To = TipNow - StencilLazyTip;
			const float D = static_cast<float>(To.Size());
			if (D > L)
			{
				StencilLazyTip += To * ((D - L) / D);
			}
			TipNow = StencilLazyTip;
		}
	}
	const FVector TipFrom = bHasLastPaintTip ? LastPaintTipWorld : TipNow;
	// 液線針=距離節拍實線；霧針/打稿筆=自由揮掃距離節拍（07-23 四版；07-25 打稿制）
	// ——打稿=手擁有速度（草稿的本質），節拍頻率走 Shader 的寬鬆天花板（純防外掛）
	const bool bFreehandTool = SelectedNeedle == EInkNeedle::Shader ||
		SelectedNeedle == EInkNeedle::Stencil;
	const float SpacingCm = FMath::Max(TattooSpacingK * TattooNibDiameterCm, 0.02f);
	const float DotHzNow = bFreehandTool ? ShaderDotHz : TattooDotHz;
	const FVector2D* PrevUV = bHasLastPaintTip ? &LastPaintUV : nullptr;
	TattooDotBudget = FMath::Min(
		TattooDotBudget + DeltaSeconds * DotHzNow * 1.5f,
		FMath::Max(2.0f, DotHzNow * 0.25f)); // 桶容量隨頻率（hitch 幀要補得起）

	TArray<FVector2D> DotUVs;
	auto EmitDotAt = [&](const FVector& TipP)
	{
		FVector2D UV;
		if (!TipToSurfaceUV(TipP, PrevUV, UV))
		{
			return; // 針此刻不在皮膚上（滑出剪影邊緣）＝這一針空扎
		}
		// 原地扎同一點＝冪等（同 UV 針跳過：資料不灌水、視覺同一顆點）——
		// 不做「停留越久越大/越深」：筆寬通膨的後門不開（細筆=SPEC 承重不變量）
		if (PrevUV && UV.Equals(*PrevUV, 1e-4))
		{
			return;
		}
		if (bHasTattooLastDotTip)
		{
			TattooLastDotGapCm = FVector::Dist(TattooLastDotTipWorld, TipP);
		}
		TattooLastDotTipWorld = TipP;
		bHasTattooLastDotTip = true;
		++TattooDotsEmitted;
		DotUVs.Add(UV);
		LastPaintUV = UV;
		PrevUV = &LastPaintUV;
	};

	// 手速→濃淡（十二版）：本 tick 的流量因子——EMA 手速已算好（τ0.08s），
	// 同 tick 所有針同因子（EMA 時間尺度 >> tick）；量化 byte 隨針進筆劃資料
	const uint8 FlowByte = ComputeMistFlowByte();

	if (bFreehandTool)
	{
		// 霧針（07-23 五修＝距離節拍）／打稿筆（07-25）：自由揮掃、沿筆尖路徑每
		// 節拍距離沉積——**間距夠密=任何手速單趟連續**（時間節拍版被 user 實測抓到：
		// 真人快掃 300~1000°/s 把 stamp 攤到不相鄰=看起來沒畫）。
		// 打稿筆間距=k×筆寬（麥克筆實線、與液線同構）；霧針=ShaderStampSpacingCm。
		// 流量天花板=出針預算（九版：純防外掛——3000/s 真人構不到）。
		// 移動閘：LMB 按住「且」aim 在動才出墨——停針=零沉積；
		// 閘同時擋掉解算噪聲位移的距離灌水（liner 巡航閘的老教訓）。
		// 稿筆豁免移動閘＋首針即點（08-01）：閘要防的噪聲位移在稿筆鏈沒有載體
		//（墨=P、aim 靜止時 P 走快取＝凍結、原地同 UV 針冪等跳過），而慢工細描
		// 正是打稿的操作域——3°/s 閘＝慢畫整段無墨（閘關 tick 走過的路徑不記帳
		// ＝永久丟棄）、點一下＝沒有點、短筆劃 EMA 爬坡期＝斷頭。「拒收式防護在
		// 連續操作域=移動牆」鐵則的出墨層適用；Shader 閘照舊（打霧操作域在快端）。
		const bool bStencilPen = SelectedNeedle == EInkNeedle::Stencil;
		if (bStencilPen && !bStrokeOpen && TattooDotBudget >= 1.0f)
		{
			// 首針即點（與 Liner 同構：接觸即墨、沒接觸下一 tick 重試）
			MistDistAccum = 0.0f;
			TattooDotBudget -= 1.0f;
			EmitDotAt(TipNow);
		}
		if (bStencilPen || MistAimSpeedDegS >= MistMinAimSpeedDegS)
		{
			const float MistSpacing = (SelectedNeedle == EInkNeedle::Stencil)
				? SpacingCm : FMath::Max(ShaderStampSpacingCm, 0.1f);
			const float L = FVector::Dist(TipFrom, TipNow);
			float Walked = 0.0f;
			// Guard 64：真人快掃 @低幀 一 tick 可走 ~6cm=30 排——12 也是隱性丟排閘
			for (int32 Guard = 0; Guard < 64; ++Guard)
			{
				const float Need = MistSpacing - MistDistAccum;
				if (Walked + Need > L)
				{
					MistDistAccum += (L - Walked);
					break;
				}
				if (TattooDotBudget < 1.0f)
				{
					MistDistAccum = MistSpacing; // 天花板擋住：記帳停在「差一枚」
					break;
				}
				Walked += Need;
				MistDistAccum = 0.0f;
				TattooDotBudget = FMath::Max(TattooDotBudget - 1.0f, 0.0f);
				EmitDotAt(FMath::Lerp(TipFrom, TipNow,
					(L > KINDA_SMALL_NUMBER) ? (Walked / L) : 1.0f));
			}
		}
		else
		{
			// 停針：距離帳「保留」不歸零——閘只暫停沉積。歸零版=閘每閃關一幀就
			// 丟掉累積 ⇒ 排距變成「手速×tick」（斷格真兇之二）；閘關期間 aim 不動
			// ⇒ 帳本本來就不會被灌水，保留是安全的。
		}
	}
	else if (!bStrokeOpen)
	{
		// 首針即點（成功接觸才會真正開筆，見下）
		TattooDistSinceDot = 0.0f;
		TattooDotBudget = FMath::Max(TattooDotBudget - 1.0f, 0.0f);
		EmitDotAt(TipNow);
	}
	else if (bTattooCruising)
	{
		// 出墨只在巡航推進時前進：真輸入下 LMB 按住時 aim 只會被巡航移動——
		// 非巡航 tick 的筆尖位移＝解算收斂/濾波尾巴的噪聲，入帳=原地打點灌水
		//（12Hz robo 實錘：停桿後 1.5s 漏 +4 針）；robo aim 傳送門的跳段同樣
		// 不落墨（跳不是線）。死區停針=首針之後零出墨=冪等由構造保證。
		const float L = FVector::Dist(TipFrom, TipNow);
		TattooGainActAccum += L; // 外環增益的實走量測（tip 路徑=玩家感受到的針速）
		TattooDbgTipCm += L;
		float Walked = 0.0f;
		for (int32 Guard = 0; Guard < 8; ++Guard)
		{
			const float Need = SpacingCm - TattooDistSinceDot;
			if (Walked + Need > L)
			{
				TattooDistSinceDot += (L - Walked); // 本幀路徑走完、下一針還沒到
				break;
			}
			if (TattooDotBudget < 1.0f)
			{
				// 速率天花板擋住（異常快移）：距離記帳停在「差一針」，預算回來立刻補
				TattooDistSinceDot = SpacingCm;
				break;
			}
			Walked += Need;
			TattooDistSinceDot = 0.0f;
			TattooDotBudget -= 1.0f;
			EmitDotAt(FMath::Lerp(TipFrom, TipNow,
				(L > KINDA_SMALL_NUMBER) ? (Walked / L) : 1.0f));
		}
	}
	LastPaintTipWorld = TipNow;
	bHasLastPaintTip = true;

	if (!bStrokeOpen && DotUVs.IsEmpty())
	{
		return; // 首針沒接觸到皮膚＝還沒開筆；下一 tick 首針重試（接觸即墨）
	}
	// 本地預測（07-26）：客戶端自己的墨當幀上屏——舊制連本人的墨都等完整來回
	//（批次 50ms＋RTT＋server tick）＝筆到墨不到的遲鈍主因。與回播嚴格同構
	//（同 BeginStroke/AddStrokePoint 鏈），回播端以 StrokeSeq 對消防重複蓋章。
	const bool bLocalEcho = !HasAuthority(); // listen 主機 multicast 同幀本地執行＝已零延遲
	if (!bStrokeOpen)
	{
		StopPaintingLocal();
		bPainting = true;
		PaintTarget = Target;
		PendingPoints.Reset();
		PendingFlows.Reset();
		PointFlushTimer = 0.0f;
		++LocalStrokeSeq;
		if (bLocalEcho && Target->InkCanvas)
		{
			// 開筆預測：Stencil 強制紫與 server 端同式（稿不吃調色盤）
			const FLinearColor EchoColor = (SelectedNeedle == EInkNeedle::Stencil)
				? NiceInkStencil::Color() : FNiceInkPalette::Get(SelectedColorIndex);
			Target->InkCanvas->BeginStroke(GetInkAuthorId(), EchoColor, DotUVs[0],
				/*bDotStroke=*/true, SelectedNeedle, FlowByte);
		}
		ServerPaintBegin(Target, SelectedColorIndex, DotUVs[0], SelectedNeedle, FlowByte, LocalStrokeSeq);
		DotUVs.RemoveAt(0);
	}
	// 筆劃保持開著（節拍未到/原地冪等/空扎都不是抬針）——抬針只由放開左鍵/
	// 失去接觸閘裁決；跨縫跨肢的點間大跳在點刺渲染下＝誠實的兩顆點，無內插垃圾

	PendingPoints.Append(DotUVs);
	if (SelectedNeedle == EInkNeedle::Shader)
	{
		// 流量陣列與點列逐索引對齊；Liner 恆走空陣列（=全滿濃度）省頻寬
		for (int32 i = 0; i < DotUVs.Num(); ++i)
		{
			PendingFlows.Add(FlowByte);
		}
	}
	if (bLocalEcho && DotUVs.Num() > 0 && Target->InkCanvas)
	{
		// 落墨預測：與 MulticastPaintPoints 重播同構（整批一次 RT context）
		Target->InkCanvas->BeginStampBatchFor(GetInkAuthorId());
		for (const FVector2D& EchoUv : DotUVs)
		{
			Target->InkCanvas->AddStrokePoint(GetInkAuthorId(), EchoUv,
				SelectedNeedle == EInkNeedle::Shader ? FlowByte : 255);
		}
		Target->InkCanvas->EndStampBatch();
	}
	PointFlushTimer += DeltaSeconds;
	if (PendingPoints.Num() > 0 &&
		(PendingPoints.Num() >= PointFlushMaxBatch || PointFlushTimer >= PointFlushInterval))
	{
		FlushPendingPoints();
		PointFlushTimer = 0.0f;
	}
	if (bStencilCatchUp)
	{
		// 補完段已入帳——正式收筆（flush 由 StopPaintingLocal 保證）
		StopPaintingLocal();
		bHasLastPaintTip = false;
		bStencilLazyValid = false;
	}
}

void ANiceInkCharacter::FlushPendingPoints()
{
	// 分塊送出（九版）：server 單批上限 256——shader 真人快掃低幀時一 tick 可積
	// 破百針，整包送=被上限整批拒收（靜默丟墨+各端不同步）
	constexpr int32 ChunkMax = 200;
	const bool bHasFlows = PendingFlows.Num() == PendingPoints.Num(); // Liner=空陣列
	int32 Cursor = 0;
	while (Cursor < PendingPoints.Num())
	{
		const int32 Count = FMath::Min(ChunkMax, PendingPoints.Num() - Cursor);
		TArray<FVector2D> Chunk(PendingPoints.GetData() + Cursor, Count);
		TArray<uint8> FlowChunk;
		if (bHasFlows)
		{
			FlowChunk.Append(PendingFlows.GetData() + Cursor, Count);
		}
		ServerPaintPoints(Chunk, FlowChunk);
		Cursor += Count;
	}
	PendingPoints.Reset();
	PendingFlows.Reset();
}

uint8 ANiceInkCharacter::ComputeMistFlowByte() const
{
	// 十六版填色制：流量恆滿——Shader=塗色工具，濃度屬於機器（塗均勻契約）。
	// 手速→濃淡映射（十二版）退役；byte 資料鏈保留（存檔/RPC 格式不動）。
	return 255;
}

bool ANiceInkCharacter::ResolveCursorToTargetUV(APlayerController* PC, const FVector2D& ScreenPx, FVector2D& OutUV) const
{
	// 直接畫制：任意螢幕座標→世界射線→中心落墨鏈（robo/診斷用；遊戲路徑走
	// ResolveAimToTargetUV 的 aim 方向版——螢幕中心與 aim 方向構造恆等）
	FVector RayOrigin, RayDir;
	if (!PC->DeprojectScreenPositionToWorld(ScreenPx.X, ScreenPx.Y, RayOrigin, RayDir))
	{
		return false;
	}
	return ResolveAimToTargetUV(RayDir, OutUV);
}

void ANiceInkCharacter::StopPaintingLocal()
{
	// 抬針：距離節拍歸零（下次落針=首針即點）；針距量測鏈斷開（跨筆劃不量距）
	TattooDistSinceDot = 0.0f;
	MistDistAccum = 0.0f;
	bHasTattooLastDotTip = false;
	if (!bPainting)
	{
		return;
	}
	if (PendingPoints.Num() > 0)
	{
		FlushPendingPoints();
	}
	if (!HasAuthority())
	{
		// 本地預測收筆（回播 End 由對消跳過；server 拒收開筆時這裡收的是
		// 本地預測那條——冪等）
		if (ANiceInkCharacter* EchoTarget = PaintTarget.Get())
		{
			if (EchoTarget->InkCanvas)
			{
				EchoTarget->InkCanvas->EndStroke(GetInkAuthorId());
			}
		}
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
	if (!HasAuthority())
	{
		// 主機本人＝server 路徑已權威落地，僅遠端要補 transform
		SetActorTransform(NewTransform, false, nullptr, ETeleportType::TeleportPhysics);
		GetCharacterMovement()->StopMovementImmediately();
	}
	// 控制器朝向同步（08-04 競態封死；user 抓「第一人稱與第三人稱反向」）：
	// bUseControllerRotationYaw 平時=true，而關閉它的 bAsleep 複製與本 RPC 到達
	// 順序不保證——RPC 先到＝下一個移動 tick 把身體 yaw 轉回「入睡前視角」且
	// 永不修正（本人端身體朝向≠server＝甦醒相機整個世界讀感旋轉）。把同一個
	// yaw 寫進控制器＝就算 yaw-follow 還開著，轉向目標也已是躺姿/座位朝向，
	// 兩條路徑殊途同歸、與封包順序無關；主機受害者的現身回座同治。
	if (AController* C = GetController())
	{
		C->SetControlRotation(FRotator(0.0f, NewTransform.Rotator().Yaw, 0.0f));
	}
}

void ANiceInkCharacter::ServerSetAsleep(bool bNewAsleep, const FTransform& LieTransform)
{
	if (!HasAuthority() || bAsleep == bNewAsleep)
	{
		return;
	}

	bAsleep = bNewAsleep;
	bFeignSleep = false;      // 睡/醒任一方向切換＝裝睡歸零（server 權威）
	bFeignSleepLocal = false; // 主機本人當受害者時 OnRep 不跑，這裡一併清鏡像
	bDebugFeignHeld = false;

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
		SleepLieYawDeg = LieTransform.Rotator().Yaw; // 複製屬性＝owner 每 tick 斷言的權威 yaw
		bSleepLieYawValid = true;
		ClientSyncPoseTransform(LieTransform); // 本端 yaw 落地（客戶端權威、修正不覆蓋）
	}
	else
	{
		// 睜眼即過期：未用完的噴射／拳腳作廢（SPEC 定案 #6/#7）
		SprayCharges = 0;
		KickCharges = 0;
		bBodyFaceDown = false; // 現身站起＝翻身狀態自然結束
		bSleepLieYawValid = false;
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

// --- 醉夢描圖（SPEC v4.0 定案 #49/#50）---

void ANiceInkCharacter::ClientStartTrace_Implementation(int32 Seed, const FDreamTraceParams& Params)
{
	if (DreamTrace)
	{
		DreamTrace->StartTrace(Seed, Params);
	}
}

void ANiceInkCharacter::ServerTraceComplete_Implementation()
{
	// 描完只在作畫階段有效；睜眼＝無聲甦醒（與 ServerMazeExited 同語意同驗證）
	if (!ValidateMazeSender(this, /*bAllowSeating=*/false))
	{
		return;
	}
	bEyesOpen = true;
	ApplySleepVisual();
}

void ANiceInkCharacter::ServerAttackShake_Implementation()
{
	if (ANiceInkGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ANiceInkGameMode>() : nullptr)
	{
		GM->HandleShakeAttack(this);
	}
}

void ANiceInkCharacter::ClientApplyShake_Implementation(const FString& AttackerName, float Seconds, float AmpCm)
{
	if (DreamTrace)
	{
		DreamTrace->ApplyShake(AttackerName, Seconds, AmpCm);
	}
}

void ANiceInkCharacter::ClientShakeAck_Implementation(bool bBought)
{
	ShakeAckFlashUntil = GetWorld() ? GetWorld()->GetTimeSeconds() + 1.2f : 0.0f;
	bLastShakeAckBought = bBought;
	NiAudio::Play(this, bBought ? ENiSound::UiClick : ENiSound::AccuseWrong, bBought ? 0.7f : 0.4f);
}

void ANiceInkCharacter::ServerSpray_Implementation(EInkEvidenceType Origin, float AimYawWorld)
{
	if (!GNiceInkSprayEnabled)
	{
		return; // v4.0 定案 #51：噴射移出核心循環——伺服器拒收（改裝客戶端也進不來）
	}
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

// --- 雲端隨身資產搬運（B3）---

namespace
{
	constexpr int32 PersonaChunkSize = 16 * 1024;
	constexpr int32 PersonaMaxBytes = 4 * 1024 * 1024; // 資產＝向量筆劃，KB 級；4MB＝瘋值上限
}

void ANiceInkCharacter::MaybeUploadPersona()
{
	UWorld* World = GetWorld();
	const bool bEligible = World && World->WorldType == EWorldType::Game && !HasAuthority() &&
		UNiceInkSessionSubsystem::IsOnlineServiceConfigured() && !bPersonaUploadDone;
	if (!bEligible)
	{
		GetWorldTimerManager().ClearTimer(PersonaUploadTimer);
		return;
	}
	if (--PersonaUploadTicksLeft <= 0)
	{
		GetWorldTimerManager().ClearTimer(PersonaUploadTimer); // 放棄：server 逾時走主機本機槽
		return;
	}
	if (!IsLocallyControlled())
	{
		return; // 佔有時序未到（他人角色會在 ticks 耗盡後自然收攤）
	}

	UNiceInkPersonaSubsystem* Persona = UNiceInkPersonaSubsystem::Get(this);
	if (!Persona || Persona->GetAssetsPullState() != UNiceInkPersonaSubsystem::EPullState::Done)
	{
		return; // 雲端拉取未完成：下一 tick 再看
	}

	bPersonaUploadDone = true;
	GetWorldTimerManager().ClearTimer(PersonaUploadTimer);

	const TArray<uint8>& Bytes = Persona->GetCachedAssets();
	if (!Persona->HasCloudAssets() || Bytes.Num() <= 0 || Bytes.Num() > PersonaMaxBytes)
	{
		return; // 新帳號無資產：不上行，server 逾時 fallback（多半也是空）＝乾淨新身
	}

	ServerPersonaBegin(Bytes.Num());
	for (int32 Off = 0; Off < Bytes.Num(); Off += PersonaChunkSize)
	{
		TArray<uint8> Chunk(Bytes.GetData() + Off, FMath::Min(PersonaChunkSize, Bytes.Num() - Off));
		ServerPersonaChunk(Off, Chunk);
	}
	ServerPersonaEnd(FCrc::MemCrc32(Bytes.GetData(), Bytes.Num()));
	UE_LOG(LogTemp, Log, TEXT("NiPersona: uploaded %d bytes to host"), Bytes.Num());
}

void ANiceInkCharacter::ServerPersonaBegin_Implementation(int32 TotalBytes)
{
	const ANiceInkPlayerState* PS = GetPlayerState<ANiceInkPlayerState>();
	if (TotalBytes <= 0 || TotalBytes > PersonaMaxBytes || (PS && PS->bAssetsRestored))
	{
		PersonaUpExpected = -1; // 拒收（已還原過＝重複列車；或瘋值）
		return;
	}
	PersonaUpExpected = TotalBytes;
	PersonaUpReceived = 0;
	PersonaUpBuf.SetNumZeroed(TotalBytes);
}

void ANiceInkCharacter::ServerPersonaChunk_Implementation(int32 Offset, const TArray<uint8>& Bytes)
{
	if (PersonaUpExpected < 0 || Offset < 0 || Bytes.Num() <= 0 ||
		Offset + Bytes.Num() > PersonaUpExpected)
	{
		return;
	}
	FMemory::Memcpy(PersonaUpBuf.GetData() + Offset, Bytes.GetData(), Bytes.Num());
	PersonaUpReceived += Bytes.Num();
}

void ANiceInkCharacter::ServerPersonaEnd_Implementation(uint32 Crc)
{
	if (PersonaUpExpected < 0 || PersonaUpReceived != PersonaUpExpected ||
		FCrc::MemCrc32(PersonaUpBuf.GetData(), PersonaUpBuf.Num()) != Crc)
	{
		UE_LOG(LogTemp, Warning, TEXT("NiPersona: upload rejected (expected=%d received=%d)"),
			PersonaUpExpected, PersonaUpReceived);
	}
	else if (ANiceInkGameMode* GM = GetWorld()->GetAuthGameMode<ANiceInkGameMode>())
	{
		GM->ApplyUploadedPersona(this, PersonaUpBuf);
	}
	PersonaUpBuf.Empty();
	PersonaUpExpected = -1;
	PersonaUpReceived = 0;
}

void ANiceInkCharacter::SendPersonaToOwner(const TArray<uint8>& Bytes)
{
	if (!HasAuthority() || Bytes.Num() <= 0 || Bytes.Num() > PersonaMaxBytes)
	{
		return;
	}
	ClientPersonaBegin(Bytes.Num());
	for (int32 Off = 0; Off < Bytes.Num(); Off += PersonaChunkSize)
	{
		TArray<uint8> Chunk(Bytes.GetData() + Off, FMath::Min(PersonaChunkSize, Bytes.Num() - Off));
		ClientPersonaChunk(Off, Chunk);
	}
	ClientPersonaEnd(FCrc::MemCrc32(Bytes.GetData(), Bytes.Num()));
}

void ANiceInkCharacter::ClientPersonaBegin_Implementation(int32 TotalBytes)
{
	if (TotalBytes <= 0 || TotalBytes > PersonaMaxBytes)
	{
		PersonaDownExpected = -1;
		return;
	}
	PersonaDownExpected = TotalBytes;
	PersonaDownReceived = 0;
	PersonaDownBuf.SetNumZeroed(TotalBytes);
}

void ANiceInkCharacter::ClientPersonaChunk_Implementation(int32 Offset, const TArray<uint8>& Bytes)
{
	if (PersonaDownExpected < 0 || Offset < 0 || Bytes.Num() <= 0 ||
		Offset + Bytes.Num() > PersonaDownExpected)
	{
		return;
	}
	FMemory::Memcpy(PersonaDownBuf.GetData() + Offset, Bytes.GetData(), Bytes.Num());
	PersonaDownReceived += Bytes.Num();
}

void ANiceInkCharacter::ClientPersonaEnd_Implementation(uint32 Crc)
{
	const bool bOk = PersonaDownExpected > 0 && PersonaDownReceived == PersonaDownExpected &&
		FCrc::MemCrc32(PersonaDownBuf.GetData(), PersonaDownBuf.Num()) == Crc;
	if (bOk)
	{
		if (UNiceInkPersonaSubsystem* Persona = UNiceInkPersonaSubsystem::Get(this))
		{
			Persona->StoreAssets(PersonaDownBuf); // 快取＋寫自己的雲端保險箱
		}
	}
	PersonaDownBuf.Empty();
	PersonaDownExpected = -1;
	PersonaDownReceived = 0;
}

// --- 自訂臉房內分發（2026-08-10）---

void ANiceInkCharacter::MaybeStartFaceShare()
{
	if (bFaceShareStarted || --FaceShareTicksLeft <= 0)
	{
		GetWorldTimerManager().ClearTimer(FaceShareTimer);
		return;
	}
	if (!IsLocallyControlled())
	{
		return; // 他人角色：ticks 耗盡自然收攤
	}
	const ANiceInkPlayerState* PS = GetPlayerState<ANiceInkPlayerState>();
	UNiceInkPersonaSubsystem* Persona = UNiceInkPersonaSubsystem::Get(this);
	if (!PS || PS->SeatIndex < 0 || !Persona)
	{
		return; // 席位/子系統未就緒：下一 tick 再看
	}

	bFaceShareStarted = true;
	GetWorldTimerManager().ClearTimer(FaceShareTimer);

	// 本人臉先入自己的登記簿（零延遲：不等網路回聲，名冊臉即刻被蓋掉）
	if (Persona->HasCustomFace())
	{
		if (UNiceInkFaceShare* Share = UNiceInkFaceShare::Get(this))
		{
			Share->StoreTextures(PS->SeatIndex, Persona->GetFaceOpen(),
				Persona->GetFaceClosed(), Persona->GetEyeMaskInk(), Persona->GetCustomSkinTone());
		}
	}

	if (HasAuthority())
	{
		// listen 主機本人：blob 不過網、顯示走本地零延遲路——現身閘即刻 ready；
		// blob 打包失敗/無臉＝標 bFaceNone（觀看端不空等、名冊臉誠實降級）
		bool bBlobShared = false;
		if (Persona->HasCustomFace())
		{
			TArray<uint8> Blob;
			if (UNiceInkFaceShare::BuildBlobFromDir(Persona->GetActiveFaceDir(), Blob))
			{
				if (ANiceInkGameMode* GM = GetWorld()->GetAuthGameMode<ANiceInkGameMode>())
				{
					GM->OnFaceBlobReceived(this, Blob); // 內含 ServerSetFaceReady()
					bBlobShared = true;
				}
			}
		}
		if (!bBlobShared)
		{
			ServerSetFaceReady(/*bNoBlobFallback=*/true);
		}
		return;
	}

	// 遠端客戶端：報到（server 補發所有已知臉＋回 manifest）＋節奏上傳自己的臉；
	// 無臉端（開發沙箱）報到即視同上行完成（veil/現身閘不空等）
	ServerFaceHello(Persona->HasCustomFace());
	if (!Persona->HasCustomFace())
	{
		bFaceUpAcked = true;
	}
	if (Persona->HasCustomFace())
	{
		TSharedPtr<TArray<uint8>> Blob = MakeShared<TArray<uint8>>();
		if (UNiceInkFaceShare::BuildBlobFromDir(Persona->GetActiveFaceDir(), *Blob) &&
			Blob->Num() > 0 && Blob->Num() <= PersonaMaxBytes)
		{
			FaceUpSendBuf = Blob;
			FaceUpSendOff = 0;
			FLinearColor Tone(0.4f, 0.22f, 0.13f);
			UNiceInkFaceShare::PeekTone(*Blob, Tone);
			ServerFaceBegin(Blob->Num(), Tone);
			GetWorldTimerManager().SetTimer(FaceUpSendTimer, this,
				&ANiceInkCharacter::TickFaceUpload, 0.025f, /*bLoop=*/true);
		}
	}
}

void ANiceInkCharacter::TickFaceUpload()
{
	if (!FaceUpSendBuf.IsValid())
	{
		GetWorldTimerManager().ClearTimer(FaceUpSendTimer);
		return;
	}
	const TArray<uint8>& B = *FaceUpSendBuf;
	// 08-14 卡頓根治①＋二修：節奏=1×16KB/0.025s≈640KB/s「抹平」——頻寬帳逐幀記，
	// 單 tick 爆發（舊 4×16KB）會把該連線打進飽和數幀、bFaceReady 等小屬性被餓
	//（現身旗標晚 2.3s 實錘）。單塊 16KB＜每幀預算（帽 2MB/s÷60fps=33KB）＝
	// 任何幀都不飽和；blob ~400KB 仍 0.7 秒送完。
	int32 Budget = 1;
	while (Budget-- > 0 && FaceUpSendOff < B.Num())
	{
		TArray<uint8> Chunk(B.GetData() + FaceUpSendOff, FMath::Min(PersonaChunkSize, B.Num() - FaceUpSendOff));
		ServerFaceChunk(FaceUpSendOff, Chunk);
		FaceUpSendOff += Chunk.Num();
	}
	if (FaceUpSendOff >= B.Num())
	{
		ServerFaceEnd(FCrc::MemCrc32(B.GetData(), B.Num()));
		UE_LOG(LogTemp, Log, TEXT("NiFaceShare: uploaded %d bytes to host"), B.Num());
		FaceUpSendBuf.Reset();
		GetWorldTimerManager().ClearTimer(FaceUpSendTimer);
	}
}

void ANiceInkCharacter::ServerFaceHello_Implementation(bool bHasCustomFace)
{
	if (!bHasCustomFace)
	{
		ServerSetFaceReady(/*bNoBlobFallback=*/true); // 無臉端（開發沙箱）：即刻現身走名冊臉
	}
	if (ANiceInkGameMode* GM = GetWorld()->GetAuthGameMode<ANiceInkGameMode>())
	{
		GM->RegisterFaceViewer(this);
	}
}

void ANiceInkCharacter::ServerSetFaceReady(bool bNoBlobFallback)
{
	if (!HasAuthority())
	{
		return;
	}
	if (bNoBlobFallback && !bFaceReady)
	{
		bFaceNone = true; // 沒有 blob 可等（無臉端/上傳失敗）：名冊臉＝誠實降級
		FaceGateShowNow();
	}
	bFaceReady = true;
}

void ANiceInkCharacter::FaceGateShowNow()
{
	if (!HasAuthority() || bFaceGateShown)
	{
		return;
	}
	bFaceGateShown = true;
	if (IsHidden())
	{
		SetActorHiddenInGame(false);
		UE_LOG(LogTemp, Log, TEXT("NiFaceShare: gate show pid=%d"),
			GetPlayerState() ? GetPlayerState<APlayerState>()->GetPlayerId() : -1);
	}
}

void ANiceInkCharacter::ServerFaceGotSeat_Implementation(int32 Seat)
{
	if (ANiceInkGameMode* GM = GetWorld()->GetAuthGameMode<ANiceInkGameMode>())
	{
		GM->OnViewerGotFace(this, Seat);
	}
}

void ANiceInkCharacter::ClientFaceManifest_Implementation(const TArray<int32>& Seats)
{
	JoinFaceWaitSeats = Seats;
	bFaceManifestRecv = true;
}

void ANiceInkCharacter::ClientFaceUpAck_Implementation()
{
	bFaceUpAcked = true;
}

bool ANiceInkCharacter::IsJoinFaceSyncPending()
{
	if (bJoinFaceSyncDone)
	{
		return false;
	}
	UWorld* World = GetWorld();
	// veil 只服務 Game 世界的遠端 client 本人；host 本人臉零延遲、PIE/robo 不啟動。
	// 閂死只准掛在「恆真」條件上——IsLocallyControlled 在剛進世界時 Controller
	// 複製晚一兩幀＝瞬態假，上閂=veil 永不出現（首輪 E2E 實錘）
	if (!World || World->WorldType != EWorldType::Game || HasAuthority())
	{
		bJoinFaceSyncDone = true;
		return false;
	}
	if (!IsLocallyControlled())
	{
		return false; // 佔有複製未到：不蓋布也不判定，下一幀再看
	}
	const double Now = World->GetTimeSeconds();
	if (JoinFaceSyncStartS < 0.0)
	{
		JoinFaceSyncStartS = Now;
	}
	bool bReady = bFaceManifestRecv && bFaceUpAcked;
	if (bReady)
	{
		if (UNiceInkFaceShare* Share = UNiceInkFaceShare::Get(this))
		{
			for (int32 Seat : JoinFaceWaitSeats)
			{
				if (Share->GetRevision(Seat) <= 0)
				{
					bReady = false;
					break;
				}
			}
		}
	}
	// 8s 保底掀開＝傳輸失敗不卡死（誠實降級：名冊臉墊檔、晚到的臉照舊輪詢蓋上）
	if (bReady || Now - JoinFaceSyncStartS > 8.0)
	{
		bJoinFaceSyncDone = true;
		UE_LOG(LogTemp, Log, TEXT("NiFaceShare: join veil lifted (%s, %.2fs, waited %d faces)"),
			bReady ? TEXT("ready") : TEXT("timeout"), Now - JoinFaceSyncStartS, JoinFaceWaitSeats.Num());
		return false;
	}
	return true;
}

void ANiceInkCharacter::ServerFaceBegin_Implementation(int32 TotalBytes, FLinearColor Tone)
{
	if (TotalBytes <= 0 || TotalBytes > PersonaMaxBytes)
	{
		FaceUpExpected = -1;
		return;
	}
	FaceUpExpected = TotalBytes;
	FaceUpReceived = 0;
	FaceUpBuf.SetNumZeroed(TotalBytes);
	// tone 先行：server 端（=listen 主機畫面）膚色即刻上身，臉貼圖隨列車後到
	const ANiceInkPlayerState* PS = GetPlayerState<ANiceInkPlayerState>();
	if (UNiceInkFaceShare* Share = UNiceInkFaceShare::Get(this); Share && PS)
	{
		Share->StoreToneEarly(PS->SeatIndex, Tone);
	}
}

void ANiceInkCharacter::ServerFaceChunk_Implementation(int32 Offset, const TArray<uint8>& Bytes)
{
	if (FaceUpExpected < 0 || Offset < 0 || Bytes.Num() <= 0 ||
		Offset + Bytes.Num() > FaceUpExpected)
	{
		return;
	}
	FMemory::Memcpy(FaceUpBuf.GetData() + Offset, Bytes.GetData(), Bytes.Num());
	FaceUpReceived += Bytes.Num();
}

void ANiceInkCharacter::ServerFaceEnd_Implementation(uint32 Crc)
{
	if (FaceUpExpected > 0 && FaceUpReceived == FaceUpExpected &&
		FCrc::MemCrc32(FaceUpBuf.GetData(), FaceUpBuf.Num()) == Crc)
	{
		if (ANiceInkGameMode* GM = GetWorld()->GetAuthGameMode<ANiceInkGameMode>())
		{
			GM->OnFaceBlobReceived(this, FaceUpBuf);
		}
		ClientFaceUpAck(); // veil 判準之一：本人臉已被 server 收妥
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("NiFaceShare: upload rejected (expected=%d received=%d)"),
			FaceUpExpected, FaceUpReceived);
	}
	FaceUpBuf.Empty();
	FaceUpExpected = -1;
	FaceUpReceived = 0;
}

void ANiceInkCharacter::ClientFaceBegin_Implementation(int32 Seat, int32 TotalBytes, FLinearColor Tone)
{
	if (Seat < 0 || TotalBytes <= 0 || TotalBytes > PersonaMaxBytes)
	{
		FaceDownExpected = -1;
		FaceDownSeat = -1;
		return;
	}
	FaceDownSeat = Seat;
	FaceDownExpected = TotalBytes;
	FaceDownReceived = 0;
	FaceDownBuf.SetNumZeroed(TotalBytes);
	// tone 先行：該席膚色即刻入簿（EnsureAvatarApplied 輪詢套用），臉貼圖隨後
	if (UNiceInkFaceShare* Share = UNiceInkFaceShare::Get(this))
	{
		Share->StoreToneEarly(Seat, Tone);
	}
}

void ANiceInkCharacter::ClientFaceChunk_Implementation(int32 Seat, int32 Offset, const TArray<uint8>& Bytes)
{
	if (FaceDownExpected < 0 || Seat != FaceDownSeat || Offset < 0 || Bytes.Num() <= 0 ||
		Offset + Bytes.Num() > FaceDownExpected)
	{
		return;
	}
	FMemory::Memcpy(FaceDownBuf.GetData() + Offset, Bytes.GetData(), Bytes.Num());
	FaceDownReceived += Bytes.Num();
}

void ANiceInkCharacter::ClientFaceEnd_Implementation(int32 Seat, uint32 Crc)
{
	const bool bOk = FaceDownExpected > 0 && Seat == FaceDownSeat &&
		FaceDownReceived == FaceDownExpected &&
		FCrc::MemCrc32(FaceDownBuf.GetData(), FaceDownBuf.Num()) == Crc;
	if (bOk)
	{
		if (UNiceInkFaceShare* Share = UNiceInkFaceShare::Get(this))
		{
			if (Share->StoreBlob(Seat, FaceDownBuf))
			{
				ServerFaceGotSeat(Seat); // 回報收妥：server 據此判「全房都有他的臉」才現身
			}
		}
	}
	FaceDownBuf.Empty();
	FaceDownExpected = -1;
	FaceDownSeat = -1;
	FaceDownReceived = 0;
}

void ANiceInkCharacter::OnRep_Asleep()
{
	bFeignSleepLocal = false; // 睡/醒切換＝裝睡鏡像歸零（server 端已同步清 bFeignSleep）
	bDebugFeignHeld = false;
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

	Body->SetEyesClosed(bAsleep && (!bEyesOpen || IsFeigningSleep())); // 裝睡＝閉眼貼圖照舊

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
	if (!bAsleep && DreamTrace)
	{
		DreamTrace->StopTrace();
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

	// 直接畫制（2026-07-18）：獨佔制已廢除——同點多人重疊入座合法（ghost 互穿）；
	// 唯一拒絕理由＝CanPaintOn／距離／非皮膚（上面已驗過）。
	// 站位：沿目前接近方向退到「目標高度自適應」standoff（低點站近＝深摺搆得到），面向落筆點
	const float TargetH = static_cast<float>(Point.Z) - (GetActorLocation().Z - 92.0f);
	const FVector Approach = (GetActorLocation() - FVector(Point)).GetSafeNormal2D();
	FVector StandLoc = FVector(Point) + (Approach.IsNearlyZero() ? FVector(0, -1, 0) : Approach) *
		LeanStandoffForHeight(TargetH);
	StandLoc.Z = GetActorLocation().Z;
	const float FaceYaw = (-Approach).Rotation().Yaw;

	bLeanLocked = true;
	LeanPoint = Point;
	LeanNormal = Normal;
	LeanTarget = Target;

	// aim 初值＝從站位看向落筆點（owner 端用同一公式自算——COND_SkipOwner 收不到）
	{
		const FVector Eye = StandLoc + FVector(0.0f, 0.0f, 60.0f);
		const FRotator R = (FVector(Point) - Eye).GetSafeNormal().Rotation();
		DrawAimAzDeg = R.Yaw;
		DrawAimTiltDeg = FMath::Clamp(-R.Pitch, DrawTiltMinDeg, DrawTiltMaxDeg);
	}

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

void ANiceInkCharacter::ServerSetPenTrigger_Implementation(bool bHeld)
{
	// 伸縮針觸發態（他端針視覺用；本人端走 bPenTriggerLocal 零延遲）——鎖定中才有意義
	bPenTriggerHeld = bHeld && bLeanLocked;
}

void ANiceInkCharacter::ForceExitLean()
{
	if (!HasAuthority() || !bLeanLocked)
	{
		return;
	}
	bLeanLocked = false;
	bPenTriggerHeld = false; // 針收回（任何退出路徑：自願/被踹/相位清場/斷線）
	LeanTarget = nullptr;
	if (!bAsleep)
	{
		GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	}
	OnRep_Lean();
}

void ANiceInkCharacter::OnRep_Lean()
{
	// 沉睡時膠囊不跟控制器 yaw 的同一招：鎖定時朝向由伺服器定
	bUseControllerRotationYaw = !bLeanLocked && !bAsleep;

	// 鎖定中：靜態身體讓位（藏＋無碰撞），噴射改打膠囊（骨骼 physics asset 未備前的粗命中）
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_PhysicsBody, bLeanLocked ? ECR_Block : ECR_Ignore);

	// ghost 穿透（全域碰撞面）：入座者的膠囊對 Pawn 通道 Ignore＝任何人可穿過入座者
	//（互擋問題的碰撞半邊；視覺半邊＝owner 端 ApplyGhostView）
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, bLeanLocked ? ECR_Ignore : ECR_Block);

	if (bLeanLocked)
	{
		bRemoteDrawSnap = true;
		bLeanPoseDirty = true;
		bHasLastPaintTip = false;
		bDrawPoseWarm = false; // 入鎖首解硬切（應用層限速不擋進場）
		DrawTargetMissSecs = 0.0f;
		DrawUnreachSecs = 0.0f;
		// 刺青巡航（07-22；07-24 皮繩制）：追趕/節拍/量測鏈全歸零（robo 方向命令
		// 也解除——測試要在進鎖+PaintHold 後才掛）
		bTattooChaseActive = false;
		TattooChaseErrCm = 0.0f;
		bTattooCruising = false;
		TattooDistSinceDot = 0.0f;
		TattooDotBudget = 0.0f;
		TattooSpeedDebtCm = 0.0f;
		MistDistAccum = 0.0f;
		MistAimSpeedDegS = 0.0f; // EMA 狀態跨鎖清零
		bMistPrevAimValid = false;
		TattooGainCmdAccum = 0.0f;
		TattooGainActAccum = 0.0f; // 增益本身跨鎖保留（系統性折損比率不隨席位變）
		TattooDbgCruiseSecs = 0.0f;
		TattooDbgHopCm = 0.0f;
		TattooDbgTipCm = 0.0f;
		bHasTattooLastDotTip = false;
		TattooLastDotGapCm = -1.0f;
		TattooDotsEmitted = 0;
		bDebugPaintStickActive = false;
		// 剛臂解算暖啟動：yaw 從 aim 起步、增量歸零（Backup4 中性）；P/眼錨快取作廢
		DrawSolveYawDeg = GetActorRotation().Yaw;
		DrawSolveHipDeg = 0.0f;
		DrawSolveAnkleDeg = 0.0f;
		DrawNeedleSolveLenCm = -1.0f; // 伸針族黏著不跨鎖（殘留=首解被上一鎖姿勢污染）
		bDrawTipReachable = false;
		bDrawTargetValid = false;
		bDrawEyeAnchorValid = false;
		ClearReachVeilShell(); // 可畫域遮罩不跨鎖（眼錨定後重烘）
		// 首鎖視野修（07-27 user 抓「第一次右鍵超近超小視野」）：owner 本地 aim 在
		// 入鎖時從鎖點幾何播種——server 的 aim 種子寫在 DrawAimAzDeg（COND_SkipOwner
		// ＝本人收不到），舊制本人沿用上一次的本地 aim（開局首鎖=初始值 az0/tilt45）
		// →首幀 P 沿無關方向 trace 命中意外近點→眼錨定格在皮膚上。與 server 種子
		// 同公式（眼=ActorLoc+60，同 GetAimRayOrigin 錨定前退路）；濾波/針 aim 同步
		// snap（入鎖硬切，不從舊方向慢掃過來）。
		if (IsLocallyControlled())
		{
			const FRotator SeedR = (FVector(LeanPoint) -
				(GetActorLocation() + FVector(0.0f, 0.0f, 60.0f))).Rotation();
			const float SeedTilt = FMath::Clamp(-SeedR.Pitch, DrawTiltMinDeg, DrawTiltMaxDeg);
			DrawAimAzLocal = SeedR.Yaw;
			DrawAimTiltLocal = SeedTilt;
			AimEuroAz.Snap(SeedR.Yaw);
			AimEuroTilt.Snap(SeedTilt);
			DrawAimAzFilt = SeedR.Yaw;
			DrawAimTiltFilt = SeedTilt;
			TattooNeedleAz = SeedR.Yaw;
			TattooNeedleTilt = SeedTilt;
			// 稿筆自由滑鼠制：注視/凍結相機不跨鎖——入鎖硬切重新對準播種 aim
			bDrawGazeInit = false;
			bDrawCursorRelatch = true;
		}
		// 本人視角藏自己的身體（筆除外）：眼錨定後真頭會越過錨點相機＝看到自己
		// 後腦勺/肩膀擋畫布；旁人不受影響（OwnerNoSee 只藏 owner）
		if (IsLocallyControlled())
		{
			TInlineComponentArray<UMeshComponent*> OwnMeshes(this);
			for (UMeshComponent* M : OwnMeshes)
			{
				// veil 殼豁免（07-29 二鎖實錘）：殼是「只演給本人看」的標記——首鎖時
				// 殼尚未誕生逃過此迴圈、二鎖時被掃到＝遮罩消失只剩 ✕ 的真兇
				if (M && M != PenMesh && M != GripMesh && M != NeedleMesh && M != ReachVeilShell)
				{
					M->SetOwnerNoSee(true);
				}
			}
		}
		if (IsLocallyControlled())
		{
			LeanLockTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
			// aim 初值：owner 用與 server 同一公式自算（COND_SkipOwner 收不到複製值）
			const FVector Eye = GetActorLocation() + FVector(0.0f, 0.0f, 60.0f);
			const FRotator R = (FVector(LeanPoint) - Eye).GetSafeNormal().Rotation();
			DrawAimAzLocal = R.Yaw;
			DrawAimTiltLocal = FMath::Clamp(-R.Pitch, DrawTiltMinDeg, DrawTiltMaxDeg);
			LastSentDrawAz = DrawAimAzLocal;
			LastSentDrawTilt = DrawAimTiltLocal;
			DrawAimSendAccum = 0.0f;
			// 濾波器 snap 到入座 aim（進鎖硬切、不從舊值飄過來）；針 aim 同步落點
			AimEuroAz.Snap(DrawAimAzLocal);
			AimEuroTilt.Snap(DrawAimTiltLocal);
			DrawAimAzFilt = DrawAimAzLocal;
			DrawAimTiltFilt = DrawAimTiltLocal;
			TattooNeedleAz = DrawAimAzLocal;
			TattooNeedleTilt = DrawAimTiltLocal;
			if (FirstPersonCamera)
			{
				FirstPersonCamera->SetFieldOfView(LeanLockedFov); // 眼錨定相機＋窄視野（user 二調 36）
			}
			if (APlayerController* PC = Cast<APlayerController>(GetController()))
			{
				int32 ViewX = 0, ViewY = 0;
				PC->GetViewportSize(ViewX, ViewY);
				LeanCursorPx = FVector2D(ViewX * 0.5f, ViewY * 0.5f); // 直接畫制：筆＝螢幕中心
			}
			ApplyGhostView(true);
		}
		ApplyBowPose();
	}
	else
	{
		if (IsLocallyControlled())
		{
			ApplyGhostView(false);
			if (FirstPersonCamera)
			{
				FirstPersonCamera->SetFieldOfView(90.0f); // 站姿預設
			}
			TInlineComponentArray<UMeshComponent*> OwnMeshes(this);
			for (UMeshComponent* M : OwnMeshes)
			{
				if (M && !Cast<UNeckStretchComponent>(M))
				{
					// 還原第一人稱慣例：站姿本人看不見自己身體的全貌（SPEC 視角規則；
					// Body/BowBody 的 ctor 基線＝OwnerNoSee true）。先前這裡全開自身可見
					// ＝畫完一次後低頭永久看得到自己的肚子/褌（07-20 user viewport 抓到；
					// 近裁剪面 4cm 還會把貼臉的自身幾何切開）。「自身可見」只屬於
					// 躺著睜眼的受害者——那條由 ApplySleepVisual 自管，不歸這裡。
					M->SetOwnerNoSee(M == Body || M == BowBody);
				}
			}
		}
		bDrawEyeAnchorValid = false;
		bDrawTargetValid = false;
		ClearReachVeilShell();
		ResetBowPose();
		StopPaintingLocal();
	}
}

FString ANiceInkCharacter::DebugLeanSummary() const
{
	const FVector HeadW = (BowBody && BowBody->GetSkinnedAsset())
		? BowBody->GetBoneTransformByName(TEXT("Head"), EBoneSpaces::WorldSpace).GetLocation()
		: FVector::ZeroVector;
	const FVector HipsW = (BowBody && BowBody->GetSkinnedAsset())
		? BowBody->GetBoneTransformByName(TEXT("Hips"), EBoneSpaces::WorldSpace).GetLocation()
		: FVector::ZeroVector;
	// 筆尖對齊誤差：PenTipWorld 到「相機中心射線」的垂距（對齊是構造保證，這裡是量測）
	float PenRayErr = -1.0f;
	if (bPenStateValid && FirstPersonCamera)
	{
		const FVector O = FirstPersonCamera->GetComponentLocation();
		const FVector D = FRotator(-EffectiveDrawTilt(), EffectiveDrawAz(), 0.0f).Vector();
		const FVector Rel = PenTipWorld - O;
		PenRayErr = FVector::CrossProduct(Rel, D).Size();
	}
	return FString::Printf(
		TEXT("locked=%d az=%.1f tilt=%.1f fov=%.0f ghosts=%d ")
		TEXT("head=(%.1f,%.1f,%.1f) hips=(%.1f,%.1f,%.1f) point=(%.1f,%.1f,%.1f) ")
		TEXT("penRayErr=%.1f penValid=%d ")
		TEXT("solveYaw=%.1f hipDeg=%.1f ankleDeg=%.1f tipErr=%.2f reach=%d unreach=%.2f ")
		TEXT("needle=%.2f trig=%d nSolve=%.1f grip=%.1f ")
		TEXT("cruise=%d stick=%.2f dotN=%d dotGapCm=%.2f vmaxCm=%.2f guideN=%d ")
		TEXT("gain=%.2f hopSpd=%.2f tipSpd=%.2f rawAz=%.1f needleSel=%d mistSpd=%.0f flow=%d follow=%d ")
		TEXT("maskRow=%d maskOn=%d ")
		TEXT("curs=%d cursW=(%.2f,%.2f,%.2f) gazeAz=%.1f gazeTilt=%.1f ")
		TEXT("cursOk=%d camAz=%.2f camTilt=%.2f"),
		bLeanLocked ? 1 : 0, EffectiveDrawAz(), EffectiveDrawTilt(),
		FirstPersonCamera ? FirstPersonCamera->FieldOfView : -1.0f,
		GhostedChars.Num(),
		HeadW.X, HeadW.Y, HeadW.Z, HipsW.X, HipsW.Y, HipsW.Z,
		LeanPoint.X, LeanPoint.Y, LeanPoint.Z,
		PenRayErr, bPenStateValid ? 1 : 0,
		DrawSolveYawDeg, DrawSolveHipDeg, DrawSolveAnkleDeg,
		DrawTipResidualCm, bDrawTipReachable ? 1 : 0, DrawUnreachSecs,
		PenNeedleLenCm, (IsLocallyControlled() ? bPenTriggerLocal : bPenTriggerHeld) ? 1 : 0,
		DrawNeedleSolveLenCm, PenGripLenCm,
		bTattooCruising ? 1 : 0, TattooChaseErrCm, TattooDotsEmitted, TattooLastDotGapCm,
		TattooMaxSpeedCmPerSec(),
		[this]() { TArray<FVector> G; return BuildTattooGuidePath(G); }(),
		TattooSpeedGain,
		TattooDbgCruiseSecs > 0.1f ? TattooDbgHopCm / TattooDbgCruiseSecs : -1.0f,
		TattooDbgCruiseSecs > 0.1f ? TattooDbgTipCm / TattooDbgCruiseSecs : -1.0f,
		DrawAimAzLocal,
		static_cast<int32>(SelectedNeedle), // 0=Liner 1=Shader 2=Stencil（舊斷言語義不變）
		MistAimSpeedDegS,
		static_cast<int32>(ComputeMistFlowByte()),
		bStencilFollowActive ? 1 : 0,
		ReachSampleIdx,
		bReachEllipseReady ? 1 : 0,
		// curs/cursW（五版起）＝P 的別名：自由滑鼠制下游標無獨立狀態、皮膚點=導出量
		bDrawTargetValid ? 1 : 0,
		DrawTargetWorld.X, DrawTargetWorld.Y, DrawTargetWorld.Z,
		DrawGazeAz, DrawGazeTilt,
		bCursorDrawable ? 1 : 0,
		// 相機朝向真值（切工具視野不跳契約的量測端；tilt=-pitch 同 AimRot 慣例）
		FirstPersonCamera ? FirstPersonCamera->GetComponentRotation().Yaw : 0.0f,
		FirstPersonCamera ? -FirstPersonCamera->GetComponentRotation().Pitch : 0.0f);
}

FString ANiceInkCharacter::DebugRoboCanvasResolve(float ScreenFracX, float ScreenFracY) const
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		return TEXT("MISS no-pc");
	}
	if (!bLeanLocked)
	{
		return TEXT("MISS not-locked");
	}
	int32 ViewX = 0, ViewY = 0;
	PC->GetViewportSize(ViewX, ViewY);
	FVector2D UV;
	if (!ResolveCursorToTargetUV(PC, FVector2D(ViewX * ScreenFracX, ViewY * ScreenFracY), UV))
	{
		return TEXT("MISS resolve");
	}
	return FString::Printf(TEXT("HIT %.5f %.5f"), UV.X, UV.Y);
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
	if (!BowMeshWhole)
	{
		BowMeshWhole = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/SK_Sumo_Whole.SK_Sumo_Whole"));
	}
	// BowBody 首載＝縫合版（站立是首個狀態）；縫合版缺席退回切開版（機制照跑）
	USkeletalMesh* First = (Poseable == BowBody && BowMeshWhole) ? BowMeshWhole.Get() : BowMesh.Get();
	if (Poseable == BowBody)
	{
		bBowBodyIsWhole = (First == BowMeshWhole && BowMeshWhole != nullptr);
	}
	if (First)
	{
		Poseable->SetSkinnedAssetAndUpdate(First);
		if (Body && Body->GetDynamicMaterial())
		{
			// 皮膚 MID 按槽名指派：SK 匯入的槽序與 SM 相反（褌在 0）——
			// 寫死 index 0 會把皮膚材質糊到褌上。褌槽保留資產上的 M_Fundoshi。
			const TArray<FSkeletalMaterial>& SkMats = First->GetMaterials();
			for (int32 i = 0; i < SkMats.Num(); ++i)
			{
				if (!SkMats[i].MaterialSlotName.ToString().Contains(TEXT("Fundoshi")))
				{
					Poseable->SetMaterial(i, Body->GetDynamicMaterial());
				}
			}
		}
	}
	// 伸縮脖綁定資料源（骨骼 ref pose 快取＋端色＋抄身體 MID 膚色參數）——
	// 環資料按頂點位置查、兩版網格 seam 頂點同位＝任一版都可 Init；啟用與否見 SetBowBodyVariant
	if (Poseable == BowBody && NeckStretch && Poseable->GetSkinnedAsset())
	{
		NeckStretch->InitFromSource(BowBody, Body ? Body->GetDynamicMaterial() : nullptr);
		NeckStretch->bNeckStretchEnabled = !bBowBodyIsWhole;
	}
	return Poseable->GetSkinnedAsset() != nullptr;
}

void ANiceInkCharacter::SetBowBodyVariant(bool bWhole)
{
	if (!BowBody || !EnsureBowBodyAsset())
	{
		return;
	}
	USkeletalMesh* Want = (bWhole && BowMeshWhole) ? BowMeshWhole.Get() : BowMesh.Get();
	if (!Want || BowBody->GetSkinnedAsset() == Want)
	{
		if (NeckStretch)
		{
			NeckStretch->bNeckStretchEnabled = !bBowBodyIsWhole;
		}
		return;
	}
	BowBody->SetSkinnedAssetAndUpdate(Want);
	bBowBodyIsWhole = (Want == BowMeshWhole);
	if (Body && Body->GetDynamicMaterial())
	{
		const TArray<FSkeletalMaterial>& SkMats = Want->GetMaterials();
		for (int32 i = 0; i < SkMats.Num(); ++i)
		{
			if (!SkMats[i].MaterialSlotName.ToString().Contains(TEXT("Fundoshi")))
			{
				BowBody->SetMaterial(i, Body->GetDynamicMaterial());
			}
		}
	}
	if (NeckStretch)
	{
		NeckStretch->InitFromSource(BowBody, Body ? Body->GetDynamicMaterial() : nullptr);
		NeckStretch->bNeckStretchEnabled = !bBowBodyIsWhole;
	}
	// 換網格＝姿勢層/彈跳層基準全部重來
	ResetBowBodyBones();
	bGaitIdleWritten = false;
	bStandLookWritten = false;
	for (FJiggleBoneState& S : JiggleStates)
	{
		S.bValid = false;
	}
	UE_LOG(LogTemp, Log, TEXT("NiBody: BowBody variant -> %s"), bBowBodyIsWhole ? TEXT("whole") : TEXT("cut"));
}

void ANiceInkCharacter::DebugRoboSleepLook(float Yaw, float Pitch)
{
	bHasPendingDebugSleepLook = true;
	PendingDebugSleepLook = FVector2D(Yaw, Pitch);
}

void ANiceInkCharacter::DebugRoboFeignSleep(bool bFeign)
{
	// 「模擬按住 Shift」輸入源：與真鍵 OR、由 PollSleepHead 的同一條 edge 消化
	//（直設 feign 狀態會被下一 tick 的輸入輪詢反殺——owner 輪詢還原 robo 態的老陷阱）
	bDebugFeignHeld = bFeign;
}

void ANiceInkCharacter::DebugRoboWalk(float WorldDirX, float WorldDirY, float Seconds)
{
	// 走 Seconds 秒（PollMove 每 tick 消化＝與真鍵同一條 AddMovementInput 路徑）
	DebugWalkDirWorld = FVector2D(WorldDirX, WorldDirY);
	DebugWalkEndTime = GetWorld() ? static_cast<float>(GetWorld()->GetTimeSeconds()) + Seconds : -1.0f;
}

FString ANiceInkCharacter::DebugRoboGaitStats() const
{
	// gait 探針機讀摘要。腳高=世界 Z 相對腳底地面（actor Z − 半膠囊 92）；
	// footLx/kneeLx 等=CS 橫向座標（雙軌側帶/膝外開契約）；thighGap=左右大腿
	// 骨段最小間距（穿膜的骨級代理量測）
	float FootLz = -1.0f, FootRz = -1.0f;
	float HipsH = -1.0f;
	float FootLx = 0.0f, FootRx = 0.0f, KneeLx = 0.0f, KneeRx = 0.0f, ThighGap = -1.0f;
	if (BowBody && BowBody->GetSkinnedAsset())
	{
		const float FloorZ = static_cast<float>(GetActorLocation().Z) - 92.0f;
		FootLz = static_cast<float>(
			BowBody->GetBoneTransformByName(TEXT("LeftFoot"), EBoneSpaces::WorldSpace).GetLocation().Z) - FloorZ;
		FootRz = static_cast<float>(
			BowBody->GetBoneTransformByName(TEXT("RightFoot"), EBoneSpaces::WorldSpace).GetLocation().Z) - FloorZ;
		HipsH = static_cast<float>(
			BowBody->GetBoneTransformByName(TEXT("Hips"), EBoneSpaces::WorldSpace).GetLocation().Z) - FloorZ;
		auto CSLoc = [&](const TCHAR* N)
		{
			return BowBody->GetBoneTransformByName(N, EBoneSpaces::ComponentSpace).GetLocation();
		};
		const FVector HL = CSLoc(TEXT("LeftUpLeg")), HR = CSLoc(TEXT("RightUpLeg"));
		const FVector KL = CSLoc(TEXT("LeftLeg")), KR = CSLoc(TEXT("RightLeg"));
		FootLx = static_cast<float>(CSLoc(TEXT("LeftFoot")).X);
		FootRx = static_cast<float>(CSLoc(TEXT("RightFoot")).X);
		KneeLx = static_cast<float>(KL.X);
		KneeRx = static_cast<float>(KR.X);
		FVector P1, P2;
		FMath::SegmentDistToSegmentSafe(HL, KL, HR, KR, P1, P2);
		ThighGap = static_cast<float>(FVector::Dist(P1, P2));
	}
	return FString::Printf(
		TEXT("bodyVis=%d bowVis=%d stand=%d speed=%.1f stance=%.2f phase=%.2f ")
		TEXT("footLz=%.1f footRz=%.1f footLspd=%.1f footRspd=%.1f hipsH=%.1f ")
		TEXT("footLx=%.1f footRx=%.1f kneeLx=%.1f kneeRx=%.1f thighGap=%.1f ")
		TEXT("jBelly=%.2f jChL=%.2f jChR=%.2f jBuL=%.2f jBuR=%.2f"),
		Body && Body->IsVisible() ? 1 : 0,
		BowBody && BowBody->IsVisible() ? 1 : 0,
		bStandDoubleActive ? 1 : 0,
		GetVelocity().Size2D(), GaitStanceAlpha, WalkAnimPhase,
		FootLz, FootRz, GaitFootSpeed[0], GaitFootSpeed[1], HipsH,
		FootLx, FootRx, KneeLx, KneeRx, ThighGap,
		JiggleStates[0].LastSpringCm, JiggleStates[1].LastSpringCm,
		JiggleStates[2].LastSpringCm, JiggleStates[3].LastSpringCm,
		JiggleStates[4].LastSpringCm);
}

void ANiceInkCharacter::DebugRoboSideView(bool bEnable)
{
	// 側視第三人稱相機（截圖矩陣用；固定機位——探針在鏡框內走小段路）
	if (bEnable)
	{
		DebugRoboViewFrom(40.0f, -240.0f, 20.0f);
	}
	else if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->SetViewTargetWithBlend(this, 0.0f);
	}
}

void ANiceInkCharacter::DebugRoboViewFrom(float DX, float DY, float DZ)
{
	// 任意方位觀察相機：actor+世界偏移、看向 actor（正面/側面步態截圖矩陣用）
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !GetWorld())
	{
		return;
	}
	if (!DebugSideCam)
	{
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		DebugSideCam = GetWorld()->SpawnActor<ACameraActor>(SP);
	}
	const FVector Focus = GetActorLocation() + FVector(0.0f, 0.0f, -30.0f);
	const FVector CamLoc = ClampToRoom(GetActorLocation() + FVector(DX, DY, DZ));
	DebugSideCam->SetActorLocationAndRotation(CamLoc, (Focus - CamLoc).Rotation());
	PC->SetViewTargetWithBlend(DebugSideCam, 0.0f);
}

bool ANiceInkCharacter::DebugRoboEnterLean(ANiceInkCharacter* Target, FVector Anchor, FVector Normal)
{
	if (!Target || !GetWorld())
	{
		return false;
	}
	const FVector N = Normal.GetSafeNormal();
	FCollisionQueryParams QP(SCENE_QUERY_STAT(NiceInkRoboLean), /*bInTraceComplex=*/true);
	QP.AddIgnoredActor(this);
	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Anchor + N * 150.0f, Anchor - N * 80.0f, ECC_Visibility, QP) ||
		Hit.GetActor() != Target)
	{
		return false;
	}
	// ServerEnterLean 的 420cm 距離守衛：先站到點旁（robo 專用便利，不影響真流程）
	if (FVector::Dist2D(GetActorLocation(), Hit.ImpactPoint) > 300.0f)
	{
		SetActorLocation(FVector(Hit.ImpactPoint.X + 120.0f, Hit.ImpactPoint.Y, GetActorLocation().Z),
			false, nullptr, ETeleportType::TeleportPhysics);
	}
	ServerEnterLean(Target, Hit.ImpactPoint, Hit.ImpactNormal);
	return true;
}

namespace
{
	// 作畫基底可能寫到的骨集合（重置與姿勢共用一張名單）。
	// 盤腿坐姿（DrawPoseData）寫全身 37 骨——名單必須全蓋，殘留會漏進睡姿替身。
	const TCHAR* GDrawPoseBones[] = {
		TEXT("Root"), TEXT("Hips"), TEXT("Spine"), TEXT("Spine1"), TEXT("Neck"), TEXT("Head"),
		TEXT("LeftShoulder"), TEXT("LeftArm"), TEXT("LeftForeArm"), TEXT("LeftHand"),
		TEXT("LeftHandIndex1"), TEXT("LeftHandIndex2"), TEXT("LeftHandThumb1"), TEXT("LeftHandThumb2"),
		TEXT("LeftHandProp"),
		TEXT("RightShoulder"), TEXT("RightArm"), TEXT("RightForeArm"), TEXT("RightHand"),
		TEXT("RightHandIndex1"), TEXT("RightHandIndex2"), TEXT("RightHandThumb1"), TEXT("RightHandThumb2"),
		TEXT("RightHandProp"),
		TEXT("LeftUpLeg"), TEXT("LeftLeg"), TEXT("LeftFoot"), TEXT("LeftToeBase"),
		TEXT("RightUpLeg"), TEXT("RightLeg"), TEXT("RightFoot"), TEXT("RightToeBase"),
		TEXT("Jiggle_Belly"), TEXT("Jiggle_Chest_L"), TEXT("Jiggle_Chest_R"),
		TEXT("Jiggle_Butt_L"), TEXT("Jiggle_Butt_R"),
	};
}

void ANiceInkCharacter::ResetBowBodyBones()
{
	// 基底骨集合全重置（回 SK rest）。只重置 Spine/Neck/Head 會讓跪姿/手臂殘留
	// 漏進下一個使用者（睡姿替身）——重置不經 CS 換算，無快取依賴，單次 refresh 收尾。
	if (!BowBody || !BowBody->GetSkinnedAsset())
	{
		return;
	}
	for (const TCHAR* Bone : GDrawPoseBones)
	{
		BowBody->ResetBoneTransformByName(FName(Bone));
	}
	BowBody->RefreshBoneTransforms();
}

void ANiceInkCharacter::ComposeLeanBaseCS(const FReferenceSkeleton& Ref, TArray<FTransform>& OutCS) const
{
	// 基準姿 CS 組合（LeanBones=Backup4；單位/scale 鐵坑見 DrawPoseData.h 標頭註解）。
	// ApplyBowPose 的基準（拆出＝07-20 趴姿戰役遺產；趴姿已移除、共用結構保留）。
	const int32 NumBones = Ref.GetNum();
	OutCS.SetNum(NumBones);
	auto FindPose = [](const FName& Bone) -> const DrawPoseData::FBonePose* {
		for (const DrawPoseData::FBonePose& P : DrawPoseData::LeanBones)
		{
			if (Bone == FName(P.Name))
			{
				return &P;
			}
		}
		return nullptr;
	};
	for (int32 i = 0; i < NumBones; ++i)
	{
		FTransform Local = Ref.GetRefBonePose()[i];
		if (const DrawPoseData::FBonePose* P = FindPose(Ref.GetBoneName(i)))
		{
			const FVector RefScale = Local.GetScale3D();
			Local = FTransform(FQuat(P->QX, P->QY, P->QZ, P->QW).GetNormalized(),
				FVector(P->LX, P->LY, P->LZ));
			Local.SetScale3D(RefScale); // 保 scale（節點根骨 100 鏈）
		}
		const int32 Parent = Ref.GetParentIndex(i);
		OutCS[i] = Local * (Parent != INDEX_NONE ? OutCS[Parent] : FTransform::Identity);
	}
}

bool ANiceInkCharacter::WriteBowPoseConverged(const FReferenceSkeleton& Ref, const TArray<FTransform>& CS,
	const TArray<FName>& VerifyBones)
{
	// 寫入：補償換算＋實測收斂迴圈（07-20 robo 實錘：首寫時 poseable 快取=舊姿，
	// 單次補償寫入不收斂）。寫→refresh→讀回驗證骨硬驗證（0.5cm 閘）。
	// **收斂是逐層傳播的（每 pass 修正一層鏈深）⇒ 驗證骨必須含最深的鏈尾（手，
	// 深度 9）**——只驗 Head（深度 6）會在第 6 pass 提早收工，把深度 8~9 的雙臂
	// 留在半收斂＝趴姿一臂朝天/一臂扭曲（07-20 viewport 實錘）。
	const int32 NumBones = Ref.GetNum();
	float WriteErr = TNumericLimits<float>::Max();
	TArray<FTransform> Cached;
	Cached.SetNum(NumBones);
	for (int32 Pass = 0; Pass < 10 && WriteErr > 0.5f; ++Pass)
	{
		for (int32 i = 0; i < NumBones; ++i)
		{
			Cached[i] = BowBody->GetBoneTransformByName(Ref.GetBoneName(i), EBoneSpaces::ComponentSpace);
		}
		for (int32 i = 0; i < NumBones; ++i)
		{
			const int32 Parent = Ref.GetParentIndex(i);
			FTransform X = CS[i];
			if (Parent != INDEX_NONE)
			{
				X = CS[i].GetRelativeTransform(CS[Parent]) * Cached[Parent];
			}
			BowBody->SetBoneTransformByName(Ref.GetBoneName(i), X, EBoneSpaces::ComponentSpace);
		}
		BowBody->RefreshBoneTransforms(); // 鏡頭/實體筆/伸縮脖同 tick 讀骨要拿到最終姿勢
		WriteErr = 0.0f;
		for (const FName& Bone : VerifyBones)
		{
			const int32 Idx = Ref.FindBoneIndex(Bone);
			if (Idx != INDEX_NONE)
			{
				WriteErr = FMath::Max(WriteErr, FVector::Dist(
					BowBody->GetBoneTransformByName(Bone, EBoneSpaces::ComponentSpace).GetLocation(),
					CS[Idx].GetLocation()));
			}
		}
	}
	return WriteErr <= 0.5f;
}

namespace
{
	// 剛臂 3-DOF 前向模型＋Newton 解算核心（07-28 抽出共用）：ApplyBowPose 活解算與
	// 可達域邊界表烘焙必須同一來源——「恆等式要同源不要巧合」（07-27 墨=P 鐵則的
	// 解算版）。欄位＝中性姿（未施增量的基準 CS）推導的剛體參考點；方法全 const、
	// 無副作用（烘焙可任意採樣不污染活解算的暖啟動狀態）。
	struct FLeanSolveCtx
	{
		FVector HipPivot = FVector::ZeroVector;
		FVector AnklePivot = FVector::ZeroVector;
		FVector Foot0[2] = { FVector::ZeroVector, FVector::ZeroVector };
		FVector Toe0[2] = { FVector::ZeroVector, FVector::ZeroVector };
		FVector Tip0 = FVector::ZeroVector;     // 校準虛擬筆尖（出針口+標稱針長）
		FVector ExitCS = FVector::ZeroVector;   // 出針口
		FVector TipDirCS = FVector::ZeroVector; // 單位針軸（離手向）
		FVector ActorLoc = FVector::ZeroVector;
		FVector BodyRelLoc = FVector::ZeroVector;
		FRotator BodyRelRot = FRotator::ZeroRotator;
		float NeedleNominalCm = 4.0f;

		float GroundDzFor(const FQuat& QH, const FQuat& QA) const
		{
			float MinZ = TNumericLimits<float>::Max();
			for (int32 F = 0; F < 2; ++F)
			{
				const FVector Foot1 = HipPivot + QH.RotateVector(Foot0[F] - HipPivot);
				FVector Toe1 = HipPivot + QH.RotateVector(Toe0[F] - HipPivot);
				Toe1 = Foot1 + QH.Inverse().RotateVector(Toe1 - Foot1); // 腳反轉＝趾姿不變
				const FVector Foot2 = AnklePivot + QA.RotateVector(Foot1 - AnklePivot);
				FVector Toe2 = AnklePivot + QA.RotateVector(Toe1 - AnklePivot);
				Toe2 = Foot2 + QA.Inverse().RotateVector(Toe2 - Foot2);
				MinZ = FMath::Min(MinZ,
					FMath::Min(static_cast<float>(Foot2.Z), static_cast<float>(Toe2.Z)));
			}
			return DrawToePadCm - MinZ;
		}
		FVector PointCsFor(const FVector& P0, float H, float A) const
		{
			const FQuat QH(FVector::XAxisVector, FMath::DegreesToRadians(H));
			const FQuat QA(FVector::XAxisVector, FMath::DegreesToRadians(A));
			FVector Pt = HipPivot + QH.RotateVector(P0 - HipPivot);
			Pt = AnklePivot + QA.RotateVector(Pt - AnklePivot);
			Pt.Z += GroundDzFor(QH, QA);
			return Pt;
		}
		FVector WorldFor(const FVector& P0, float Psi, float H, float A) const
		{
			const FTransform CompW = FTransform(BodyRelRot, BodyRelLoc) *
				FTransform(FRotator(0.0f, Psi, 0.0f), ActorLoc);
			return CompW.TransformPosition(PointCsFor(P0, H, A));
		}
		// Newton（數值 Jacobian＋Cramer）——與抽出前逐字同構。MaxIters：活解算恆 6
		//（跨 tick 暖啟動＝迭代預算實際無限）；烘焙一次性判定要給足（冷啟動 6 步
		// 走不到解＝鎖點被誤判不可畫，07-29 VEILMASK 實錘）
		float Solve(const FVector& P, const FVector& SolveTipCS, float PsiIn, float HIn, float AIn,
			float& PsiOut, float& HOut, float& AOut, int32 MaxIters = 6) const
		{
			auto Det3 = [](const FVector& X, const FVector& Y, const FVector& Z)
			{ return static_cast<float>(FVector::DotProduct(X, FVector::CrossProduct(Y, Z))); };
			float Psi = PsiIn, H = HIn, A = AIn;
			for (int32 It = 0; It < MaxIters; ++It)
			{
				const FVector T0 = WorldFor(SolveTipCS, Psi, H, A);
				const FVector R = P - T0;
				if (R.Size() < 0.25f)
				{
					break;
				}
				constexpr float D = 0.75f;
				const FVector J0 = (WorldFor(SolveTipCS, Psi + D, H, A) - T0) / D;
				const FVector J1 = (WorldFor(SolveTipCS, Psi, H + D, A) - T0) / D;
				const FVector J2 = (WorldFor(SolveTipCS, Psi, H, A + D) - T0) / D;
				const float Den = Det3(J0, J1, J2);
				if (FMath::Abs(Den) < KINDA_SMALL_NUMBER)
				{
					break;
				}
				Psi += FMath::Clamp(Det3(R, J1, J2) / Den, -25.0f, 25.0f);
				H = FMath::Clamp(H + FMath::Clamp(Det3(J0, R, J2) / Den, -25.0f, 25.0f),
					-DrawHipDeltaClampDeg, DrawHipDeltaClampDeg);
				A = FMath::Clamp(A + FMath::Clamp(Det3(J0, J1, R) / Den, -25.0f, 25.0f),
					-DrawAnkleDeltaClampDeg, DrawAnkleDeltaClampDeg);
			}
			PsiOut = Psi;
			HOut = H;
			AOut = A;
			return (P - WorldFor(SolveTipCS, Psi, H, A)).Size();
		}
		// 靜態可行性（烘焙用）：暖啟動→乾淨重啟→伸針族，任一族解到即可；
		// 無連續性/限速（那些是活姿勢的抗抖層，不屬於「解不解得到」）。
		// 一次性判定＝迭代預算 16（活解算靠跨 tick 暖啟動摊平、烘焙沒有下一 tick）
		bool Feasible(const FVector& P, float AzHint, float& IoPsi, float& IoHip, float& IoAnkle) const
		{
			constexpr int32 BakeIters = 16;
			float Psi, H, A;
			float Res = Solve(P, Tip0, IoPsi, IoHip, IoAnkle, Psi, H, A, BakeIters);
			if (Res > DrawTipSolveTolCm)
			{
				float Psi2, H2, A2;
				const float Res2 = Solve(P, Tip0, AzHint, 0.0f, 0.0f, Psi2, H2, A2, BakeIters);
				if (Res2 < Res)
				{
					Res = Res2;
					Psi = Psi2;
					H = H2;
					A = A2;
				}
			}
			if (Res <= DrawTipSolveTolCm)
			{
				IoPsi = Psi;
				IoHip = H;
				IoAnkle = A;
				return true;
			}
			float PsiE = Psi, HE = H, AE = A, ResE = Res;
			for (int32 Outer = 0; Outer < 2; ++Outer)
			{
				const float LenE = FMath::Clamp(
					static_cast<float>((P - WorldFor(ExitCS, PsiE, HE, AE)).Size()),
					NeedleNominalCm, 300.0f);
				const FVector SolveTip = ExitCS + TipDirCS * LenE;
				ResE = Solve(P, SolveTip, PsiE, HE, AE, PsiE, HE, AE, BakeIters);
			}
			if (ResE <= DrawTipSolveTolCm)
			{
				IoPsi = PsiE;
				IoHip = HE;
				IoAnkle = AE;
				return true;
			}
			return false;
		}
	};
}

void ANiceInkCharacter::ApplyBowPose()
{
	// 直接畫制作畫姿（2026-07-18 user 定案）：基準＝使用者手擺的站立前傾
	// DrawPose_Backup4（LeanBones；髖 68cm 半蹲、腳跟離地腳尖踩地、右手前伸），
	// 程式只在基準附近施加小幅側面增量——Hips=傾多傾少（全身剛轉、相對位置不變）、
	// 雙腳反轉保持腳尖踩地、Neck=臉向精對；左右=整身 yaw。程式不發明姿勢。
	// 每 tick 由 aim 驅動（髒檢查：aim 沒動不寫骨）。
	if (!BowBody || !EnsureBowBodyAsset())
	{
		return;
	}
	SetBowBodyVariant(/*bWhole=*/true); // 作畫姿＝縫合版（冪等；埋頭/偷瞄=蒙皮頸帶）
	const float Az = EffectiveDrawAz();
	const float Tilt = EffectiveDrawTilt();
	// 門檻 0.05°（原 0.2 的量化微跳已由 One Euro 靜止凍結取代——濾波輸出靜止時
	// 真正收斂、不會在門檻兩側振盪；慢速運筆的姿勢粒度細到 0.5mm 級）。
	// 巡航中收緊到 0.01°：aim 每幀只走 ~0.01°（皮膚面恆速 ~1cm/s），0.05 門檻會把
	// 針的路徑量化成 ~0.7mm 階梯＝針距抖動吃掉實線餘裕（k=0.5 的餘裕只有 0.8mm）
	const float DirtyThresholdDeg = bTattooCruising ? 0.01f : 0.05f;
	const bool bAimMoved =
		FMath::Abs(FMath::FindDeltaAngleDegrees(Az, LastAppliedDrawAz)) >= DirtyThresholdDeg ||
		FMath::Abs(Tilt - LastAppliedDrawTilt) >= DirtyThresholdDeg;
	// 皮膚遮罩未烘完前不早退（烘焙塊住在解算段內、需要 Ctx；aim 靜止的重跑無害）
	const bool bBakePending = IsLocallyControlled() && bDrawEyeAnchorValid && !bReachEllipseReady;
	if (!bLeanPoseDirty && !bAimMoved && !bBakePending)
	{
		return;
	}
	LastAppliedDrawAz = Az;
	LastAppliedDrawTilt = Tilt;
	bLeanPoseDirty = false;

	// 換上可擺骨身體；靜態身體讓位（藏＋無碰撞）
	if (Body)
	{
		Body->SetVisibility(false);
		Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	BowBody->SetVisibility(true);
	BowBody->SetRelativeLocationAndRotation(BodyStandRelLoc, BodyStandRelRot);

	const USkinnedAsset* Asset = BowBody->GetSkinnedAsset();
	const FReferenceSkeleton& Ref = Asset->GetRefSkeleton();
	const int32 NumBones = Ref.GetNum();
	auto IdxOf = [&](const TCHAR* N) { return Ref.FindBoneIndex(FName(N)); };

	// 基準姿 CS 組合＋子樹剛轉：與趴姿共用（ComposeLeanBaseCS / RotSubtreeAboutPivotCS）
	TArray<FTransform> CS;
	ComposeLeanBaseCS(Ref, CS);
	auto RotSubtreeAboutPivot = [&](int32 RootIdx, const FQuat& Q, const FVector& PivotCS) {
		RotSubtreeAboutPivotCS(Ref, CS, RootIdx, Q, PivotCS);
	};

	// --- 一次性靜態校準（user 原話「手部的位置還需要你再調整一下」的正解）：
	// 筆焊死在握骨、shaft＝眉→手延長線 ⇒ 筆尖躺在視線正前方、看起來像握著筆。
	// 校準只定「筆在手裡的長相」；對齊由下方「解筆尖=P」保證，與校準精度無關。---
	const int32 HeadBoneIdx = IdxOf(TEXT("Head"));
	if (!bPenGripCalibrated && HeadBoneIdx != INDEX_NONE)
	{
		int32 GripIdx = IdxOf(TEXT("RightHandProp"));
		if (GripIdx == INDEX_NONE)
		{
			GripIdx = IdxOf(TEXT("RightHand"));
		}
		if (GripIdx != INDEX_NONE)
		{
			PenGripBoneName = Ref.GetBoneName(GripIdx);
			// 站姿元件空間臉朝 +Y、上 +Z（粗眉心對校準足夠）
			const FVector BrowCS = CS[HeadBoneIdx].GetLocation() + FVector(0.0f, 13.0f, 8.0f);
			const FVector HandLoc = CS[GripIdx].GetLocation();
			const FQuat HandQ = CS[GripIdx].GetRotation().GetNormalized();
			const FVector Shaft = (HandLoc - BrowCS).GetSafeNormal();
			PenTipLocalCm = HandQ.UnrotateVector(Shaft * PenTipAheadCm);
			// roll 約束（刺青機非旋轉對稱）：資產 -Y=骨架側（FBX 匯入 Y 翻轉實測）——
			// Y hint 給 -Z_CS ⇒ 骨架/線圈塔朝 +Z_CS（站姿校準的頭上方＝握姿的手背側）。
			// 麥克筆圓對稱，同式無感。-Shaft 與 -Z_CS 夾角遠離平行（shaft≈前下 45°）＝無退化。
			PenRotInHand = HandQ.Inverse() *
				FRotationMatrix::MakeFromZY(-Shaft, FVector(0.0f, 0.0f, -1.0f)).ToQuat();
			bPenGripCalibrated = true;
		}
	}

	// --- 剛臂姿勢解算（2026-07-20 user 定案「手就一直伸直就好」）：手臂＝Backup4 原樣，
	// 筆尖＝身體的固定末端；3-DOF（整身 yaw＋Hips 傾＋雙踝搖——後兩者=四關節白名單的
	// 側面自由度、Backup4 腳尖站態天生的搖桿支點）數值解「筆尖=落墨點 P」。
	// 碰到皮膚是解出來的，不是手追出來的；解不到＝搆不到＝不落墨（走近再畫）。---
	const int32 HipsIdx = IdxOf(TEXT("Hips"));
	const int32 LFootIdx = IdxOf(TEXT("LeftFoot"));
	const int32 RFootIdx = IdxOf(TEXT("RightFoot"));
	const int32 LToeIdx = IdxOf(TEXT("LeftToeBase"));
	const int32 RToeIdx = IdxOf(TEXT("RightToeBase"));
	const int32 GripBoneIdx = (PenGripBoneName != NAME_None) ? Ref.FindBoneIndex(PenGripBoneName) : INDEX_NONE;
	const bool bSolverReady = HipsIdx != INDEX_NONE && LFootIdx != INDEX_NONE &&
		RFootIdx != INDEX_NONE && GripBoneIdx != INDEX_NONE && bPenGripCalibrated;

	// 樞軸與參考點全取中性姿（未施增量的 CS）——前向模型與套用步驟共用同一組
	const FVector HipPivot = HipsIdx != INDEX_NONE ? CS[HipsIdx].GetLocation() : FVector::ZeroVector;
	const FVector AnklePivot = bSolverReady
		? (CS[LFootIdx].GetLocation() + CS[RFootIdx].GetLocation()) * 0.5f : FVector::ZeroVector;

	float HipDeg = DrawSolveHipDeg;
	float AnkleDeg = DrawSolveAnkleDeg;
	if (bSolverReady)
	{
		// 解算核心＝共用 Ctx（07-28 抽出：活解算與可達域烘焙同源，見 FLeanSolveCtx；
		// 前向模型與下方套用步驟逐步同構——Hips 剛轉→腳反轉→踝搖→腳反轉→落地）
		FLeanSolveCtx Ctx;
		Ctx.HipPivot = HipPivot;
		Ctx.AnklePivot = AnklePivot;
		Ctx.Foot0[0] = CS[LFootIdx].GetLocation();
		Ctx.Foot0[1] = CS[RFootIdx].GetLocation();
		Ctx.Toe0[0] = LToeIdx != INDEX_NONE ? CS[LToeIdx].GetLocation() : Ctx.Foot0[0];
		Ctx.Toe0[1] = RToeIdx != INDEX_NONE ? CS[RToeIdx].GetLocation() : Ctx.Foot0[1];
		Ctx.Tip0 = CS[GripBoneIdx].GetLocation() +
			CS[GripBoneIdx].GetRotation().RotateVector(PenTipLocalCm);
		// 解算目標點＝虛擬筆尖（出針口+標稱針長）；伸針解（2026-07-21）會沿針軸外推
		Ctx.TipDirCS = (Ctx.Tip0 - CS[GripBoneIdx].GetLocation()) / FMath::Max(PenTipAheadCm, 1.0f);
		Ctx.ExitCS = Ctx.Tip0 - Ctx.TipDirCS * PenNeedleNominalCm;
		Ctx.ActorLoc = GetActorLocation();
		Ctx.BodyRelLoc = BodyStandRelLoc;
		Ctx.BodyRelRot = BodyStandRelRot;
		Ctx.NeedleNominalCm = PenNeedleNominalCm;
		const FVector Tip0 = Ctx.Tip0;
		const FVector ExitCS = Ctx.ExitCS;
		const FVector TipDirCS = Ctx.TipDirCS;
		FVector SolveTipCS = Tip0;
		auto PointCsFor = [&](const FVector& P0, float H, float A) -> FVector
		{
			return Ctx.PointCsFor(P0, H, A);
		};
		const FVector Head0 = (HeadBoneIdx != INDEX_NONE)
			? CS[HeadBoneIdx].GetLocation() : FVector::ZeroVector;
		const FVector ActorLoc = Ctx.ActorLoc;
		auto TipWorldFor = [&](float Psi, float H, float A) -> FVector
		{
			return Ctx.WorldFor(SolveTipCS, Psi, H, A);
		};

		// P 取得：aim 動了才重新 trace；aim 靜止下的重跑（寫入收斂等）沿用快取 P。
		// 眼睛長在會被解算搬動的頭上——每 tick 重 trace＝「眼→P→姿勢→眼」自我參照
		// 回饋迴圈，aim 不動 P 也會漂移到鉗位角落（07-20 探針：三幀漂 10cm）。
		// 他端（07-26 抖動根治）：P＝擁有端上報值的追趕——本地重推（追趕 aim＋他端
		// 眼位）在剪影邊緣間歇 miss＝整身甩姿；改吃複製值後 miss 這一類在他端不存在，
		// 且筆尖/姿勢與墨同源。
		if (!IsLocallyControlled())
		{
			bDrawTargetValid = bDrawTargetRepValid;
			if (bDrawTargetValid)
			{
				DrawTargetWorld = RemoteDrawTargetW;
			}
		}
		else if (bAimMoved || !bDrawTargetValid)
		{
			FVector Traced;
			if (TraceAimToTarget(FRotator(-Tilt, Az, 0.0f).Vector(), Traced))
			{
				DrawTargetWorld = Traced;
				bDrawTargetValid = true;
			}
			else
			{
				bDrawTargetValid = false;
			}
		}

		if (bDrawTargetValid)
		{
			DrawTargetMissSecs = 0.0f;
			const FVector P = DrawTargetWorld;
			// 連續性紀錄（07-26 抖動根治）：「身體彎到」與「伸針補深度」兩族解都合法時
			// 選離上一 tick 姿勢最近的——族間互換曾以 hip 30° 級瞬跳每秒發生（探針實錘）
			const float PrevPsi = DrawSolveYawDeg;
			const float PrevHip = DrawSolveHipDeg;
			const float PrevAnkle = DrawSolveAnkleDeg;
			const float PrevNeedleLen = DrawNeedleSolveLenCm;
			auto PoseDist = [&](float Ps, float Hh, float Aa)
			{
				return FMath::Abs(FMath::FindDeltaAngleDegrees(Ps, PrevPsi)) +
					FMath::Abs(Hh - PrevHip) + FMath::Abs(Aa - PrevAnkle);
			};
			// Newton 本體在 Ctx.Solve（共用核心）；暖啟動=上次解；失敗再從乾淨初值重解
			//（proxy 首幀的壞暖啟動會把 Newton 掐死在鉗位角落——07-20 探針實錘）。
			// SolveTipCS 由伸針迴圈改寫＝此包裝的唯一活變數。
			auto Solve = [&](float PsiIn, float HIn, float AIn, float& PsiOut, float& HOut, float& AOut) -> float
			{
				return Ctx.Solve(P, SolveTipCS, PsiIn, HIn, AIn, PsiOut, HOut, AOut);
			};
			float Psi, HOut, AOut;
			float Res = Solve(DrawSolveYawDeg, HipDeg, AnkleDeg, Psi, HOut, AOut);
			if (Res > DrawTipSolveTolCm)
			{
				// 重啟動只在「真的解開或大幅改善」時採用（07-26：舊制任何 <Res 就換＝
				// 暖/重啟兩分支逐 tick 互搶＝整身跳姿）。
				// 重啟 16 步（07-29 與遮罩烘焙對齊）：重啟每 tick 從同一起點重來＝
				// 不跨 tick 累積，6 步走不完大轉身＝「遮罩說可畫、筆卻抬起」的縫；
				// 只在失敗 tick 執行、成本可忽略，且順手擴大實際可達域。
				float Psi2, H2, A2;
				const float Res2 = Ctx.Solve(P, SolveTipCS, Az, 0.0f, 0.0f, Psi2, H2, A2, 16);
				if (Res2 <= DrawTipSolveTolCm || Res2 < Res * 0.5f)
				{
					Res = Res2;
					Psi = Psi2;
					HOut = H2;
					AOut = A2;
				}
			}

			// 伸縮針解（2026-07-21 user 定案「指到哪畫哪」）：標稱針長＋身體鉗位仍搆
			// 不到＝深度差交給針——虛擬筆尖沿針軸外推 L（=出針口→P 實距、外迭代 2 輪
			// 與姿勢互相收斂），針軸構造上穿過 P（落墨中心對齊不是湊的）。橫向殘差仍受
			// DrawTipSolveTolCm 裁決＝針只補深度、不歪著扎。身體先扛（前傾讀感保留），
			// 針只補鉗位外的殘餘。
			DrawNeedleSolveLenCm = -1.0f;
			if (Res > DrawTipSolveTolCm || PrevNeedleLen > 0.0f)
			{
				auto ExitWorldFor = [&](float PsiE, float HE, float AE) -> FVector
				{
					return Ctx.WorldFor(ExitCS, PsiE, HE, AE);
				};
				// 上 tick 在伸針族＝從上 tick 姿勢起解（族內連續——從身體解起步會被
				// 剛跳完 30° 的身體解污染）；否則照舊從身體解起
				float PsiE = PrevNeedleLen > 0.0f ? PrevPsi : Psi;
				float HE = PrevNeedleLen > 0.0f ? PrevHip : HOut;
				float AE = PrevNeedleLen > 0.0f ? PrevAnkle : AOut;
				float LenE = PenNeedleNominalCm, ResE = Res;
				for (int32 Outer = 0; Outer < 2; ++Outer)
				{
					LenE = FMath::Clamp(
						static_cast<float>((P - ExitWorldFor(PsiE, HE, AE)).Size()),
						PenNeedleNominalCm, 300.0f);
					SolveTipCS = ExitCS + TipDirCS * LenE;
					ResE = Solve(PsiE, HE, AE, PsiE, HE, AE);
				}
				// 族選擇（07-26）：兩族都合法→取離上一 tick 姿勢近者；LenE 縮回標稱
				// ＝兩族幾何重合、自然併回身體族（nSolve 歸 -1，無縫）
				const bool bBodyOK = Res <= DrawTipSolveTolCm;
				const bool bNeedleOK = ResE <= DrawTipSolveTolCm;
				const bool bTakeNeedle = (bBodyOK && bNeedleOK)
					? PoseDist(PsiE, HE, AE) <= PoseDist(Psi, HOut, AOut)
					: (bNeedleOK || (!bBodyOK && ResE < Res));
				if (bTakeNeedle)
				{
					Res = ResE;
					Psi = PsiE;
					HOut = HE;
					AOut = AE;
					if (bNeedleOK && LenE > PenNeedleNominalCm + 0.1f)
					{
						DrawNeedleSolveLenCm = LenE;
					}
				}
				SolveTipCS = Tip0;
			}
			// 應用層額定速率（07-26 抖動根治二層）：3-DOF 有冗餘（hip↔ankle 同平面
			// 互補＝零空間）——解在等效解流形上偶發滑到別處（hip+8° 配 ankle−12°、
			// 筆尖不動髖擺 14cm＝探針殘餘實錘）。解照收斂、應用限速 120°/s＝瞬跳攤成
			// <100ms 滑移；下一 tick 暖啟動從限速值起解＝限速兼任連續性正則。
			// 入鎖首解硬切（美術語言 #24）；墨鏈走 P 不走姿勢＝零影響。
			const float StepDt = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f;
			const float MaxStepDeg = bDrawPoseWarm ? 120.0f * StepDt : 3600.0f;
			// hip/ankle=有界小角（非圓角域）——FixedTurn 會把 −14.9° 寫成 345.1°
			//（幾何等價、暖啟動/鉗位中毒，robo 實錘）；線性步進才對。yaw=圓角域，
			// 走 delta-angle 形式並 Unwind。
			auto StepToward = [](float From, float To, float MaxStep)
			{ return From + FMath::Clamp(To - From, -MaxStep, MaxStep); };
			HipDeg = StepToward(PrevHip, HOut, MaxStepDeg);
			AnkleDeg = StepToward(PrevAnkle, AOut, MaxStepDeg);
			DrawSolveYawDeg = FMath::UnwindDegrees(PrevPsi + FMath::Clamp(
				FMath::FindDeltaAngleDegrees(PrevPsi, Psi), -MaxStepDeg, MaxStepDeg));
			bDrawPoseWarm = true;
			DrawTipResidualCm = Res;
			bDrawTipReachable = Res <= DrawTipSolveTolCm;

			// （07-28 顯示制 user 定案：回捲牆/輸入鉗位全退役——「永遠不要去干擾玩家
			// 畫筆的移動」。域外＝收筆（墨閘+筆視覺），邊界由 HUD 標記；經歷記錄：
			// 回捲版壓牆必顫（事後偵測型約束）、鉗位版仍在表/活解算邊界縫隙跳針。）

			// 眼錨定＝解析算「入座目標姿勢的眉心」（07-20 三修：舊版等首寫收斂才抓
			// ＝抓到平滑層剛起步的半直立姿＝錨點恆在站直眉心高（~135cm）——
			// 「點低處鏡頭吊在上面、看不到下側」的真兇。前向模型直接給收斂後的頭位，
			// 零等待、與目標深度一致；頭骨位置不受臉向 delta 影響（樞軸=頭底））
			if (!bDrawEyeAnchorValid && HeadBoneIdx != INDEX_NONE)
			{
				const FTransform CompW = FTransform(BodyStandRelRot, BodyStandRelLoc) *
					FTransform(FRotator(0.0f, DrawSolveYawDeg, 0.0f), ActorLoc);
				const FVector HeadW = CompW.TransformPosition(PointCsFor(Head0, HipDeg, AnkleDeg));
				const FRotator AnchorRot(-Tilt, Az, 0.0f);
				DrawEyeAnchorWorld = HeadW + AnchorRot.Vector() * 13.0f +
					FRotationMatrix(AnchorRot).GetUnitAxis(EAxis::Z) * 8.0f;
				bDrawEyeAnchorValid = true;
				bDrawTargetValid = false; // P 改由錨點重取（準星=P 自錨點起構造精確）
				bLeanPoseDirty = true;
				// aim 重瞄：眼睛從高處退路換到深摺錨點＝視差可達數十度，沿用舊方向會
				// 從低眼射進地板＝P 永久丟失（07-20 robo 實錘：模特低位卡 hold pose）。
				// 重算「從錨點看向 P」＝準星停在玩家點的同一個世界點、只有眼睛換位。
				const FRotator ReAim = (P - DrawEyeAnchorWorld).Rotation();
				const float NewAz = ReAim.Yaw;
				const float NewTilt = FMath::Clamp(-ReAim.Pitch, DrawTiltMinDeg, DrawTiltMaxDeg);
				if (IsLocallyControlled())
				{
					DrawAimAzLocal = NewAz;
					DrawAimTiltLocal = NewTilt;
					LastSentDrawAz = NewAz;
					LastSentDrawTilt = NewTilt;
					// 錨點視差重瞄＝瞬時跳變，濾波器必須跟著 snap（不然筆會慢動作
					// 掃過整個視差角＝假演出）；針 aim 同步跳（視差不是追趕距離）
					AimEuroAz.Snap(NewAz);
					AimEuroTilt.Snap(NewTilt);
					DrawAimAzFilt = NewAz;
					DrawAimTiltFilt = NewTilt;
					TattooNeedleAz = NewAz;
					TattooNeedleTilt = NewTilt;
					// 稿筆游標同步重掛：眼睛換位、游標停在同一世界點 P（重瞄射線
					// 命中的就是它）、gaze 硬切到新視差角
					bDrawCursorRelatch = true;
				}
				else
				{
					RemoteDrawAzDeg = NewAz;
					RemoteDrawTiltDeg = NewTilt;
				}
				if (HasAuthority())
				{
					DrawAimAzDeg = NewAz;
					DrawAimTiltDeg = NewTilt;
				}
			}
		}
		else
		{
			// 射線 miss（07-26 改制）：舊制直接 yaw=Az——貼剪影邊緣的「間歇 miss」讓
			// 整身瞬甩 20° 再瞬甩回（探針實錘=位置閃爍主因之一）。寬限 0.35s 內姿勢
			// 凍結（墨已由 bDrawTargetValid=false 擋住）；持續 miss（真看向房間）＝yaw
			// 以額定速率轉向 aim——恆等式讀感保留、瞬移拆除。
			const float MissDt = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f;
			DrawTargetMissSecs += MissDt;
			if (DrawTargetMissSecs > 0.35f)
			{
				DrawSolveYawDeg = FMath::UnwindDegrees(DrawSolveYawDeg + FMath::Clamp(
					FMath::FindDeltaAngleDegrees(DrawSolveYawDeg, Az),
					-240.0f * MissDt, 240.0f * MissDt));
			}
			bDrawTipReachable = false;
			DrawTipResidualCm = -1.0f;
		}
		DrawSolveHipDeg = HipDeg;
		DrawSolveAnkleDeg = AnkleDeg;

		// 可畫域採樣＋嚴格最大橢圓（07-29 user 逐字定案「量測出來的圈太小就是
		// 太小，對方自己調整，我們就是嚴格給他我們能給的最大『橢圓』」）：
		// 對受害者皮膚逐點問「眼錨看得到嗎＋解算解得到嗎」（FLeanSolveCtx 同源），
		// 不可解的可見樣本在鎖點切面座標 (u,v) 上成為約束；採樣完＝擬合「內部無
		// 任何不可解樣本」的最大面積橢圓——無下限、無人工放大，太小=誠實資訊。
		// 域=解析公式 ⇒ 顯示（材質逐像素）與收筆閘同一條式＝單一裁判、零貼圖零斑。
		// 巡航中停採（速度量測窗讓路）；每 tick 3ms 預算。
		if (IsLocallyControlled() && bDrawEyeAnchorValid && !bReachEllipseReady && !bTattooChaseActive)
		{
			ANiceInkCharacter* MaskVictim = LeanTarget.Get();
			UInkBodyComponent* VB = MaskVictim ? ToRawPtr(MaskVictim->Body) : nullptr;
			if (VB && GetWorld())
			{
				constexpr int32 Grid = ReachSampleGrid;
				if (ReachSampleIdx == 0)
				{
					// 切面座標系：V=aim 在切面上的投影（朝深處=懸崖向）、H=N×V（橫向）
					ReachLockW = FVector(LeanPoint);
					const FVector Nrm = FVector(LeanNormal).GetSafeNormal();
					const FVector AimDir = (ReachLockW - DrawEyeAnchorWorld).GetSafeNormal();
					FVector Vax = AimDir - Nrm * FVector::DotProduct(AimDir, Nrm);
					if (!Vax.Normalize())
					{
						Vax = FVector::VectorPlaneProject(FVector::XAxisVector, Nrm).GetSafeNormal();
					}
					ReachAxisV = Vax;
					ReachAxisH = FVector::CrossProduct(Nrm, Vax).GetSafeNormal();
					ReachInfeasUV.Reset();
					ReachMaxExtentCm = 0.0f;
				}
				const FVector Anchor = DrawEyeAnchorWorld;
				FCollisionQueryParams VisQP(SCENE_QUERY_STAT(NiceInkReachMask), /*bInTraceComplex=*/true);
				for (TActorIterator<ANiceInkCharacter> It(GetWorld()); It; ++It)
				{
					if (*It != MaskVictim)
					{
						VisQP.AddIgnoredActor(*It);
					}
				}
				const double BudgetEnd = FPlatformTime::Seconds() + 0.003;
				while (FPlatformTime::Seconds() < BudgetEnd && ReachSampleIdx < Grid * Grid)
				{
					const int32 Idx = ReachSampleIdx++;
					const float Uc = ((Idx % Grid) + 0.5f) / Grid;
					const float Vc = ((Idx / Grid) + 0.5f) / Grid;
					FVector Pw, Nw;
					if (!VB->ResolveUVToWorldWithNormal(FVector2D(Uc, Vc), Pw, Nw))
					{
						continue;
					}
					const FVector Dir = (Pw - Anchor).GetSafeNormal();
					FHitResult Hit;
					if (!GetWorld()->LineTraceSingleByChannel(Hit, Anchor, Pw + Dir * 3.0f,
							ECC_Visibility, VisQP) ||
						Hit.GetActor() != MaskVictim ||
						FVector::Dist(Hit.ImpactPoint, Pw) > 2.5f)
					{
						continue; // 錨點看不到＝游標指不到＝不構成橢圓的約束
					}
					const FVector D3 = Pw - ReachLockW;
					const float Su = FVector::DotProduct(D3, ReachAxisH);
					const float Sv = FVector::DotProduct(D3, ReachAxisV);
					float SPsi = DrawSolveYawDeg, SHip = DrawSolveHipDeg, SAnk = DrawSolveAnkleDeg;
					if (Ctx.Feasible(Pw, Dir.Rotation().Yaw, SPsi, SHip, SAnk))
					{
						ReachMaxExtentCm = FMath::Max(ReachMaxExtentCm,
							FMath::Max(FMath::Abs(Su), FMath::Abs(Sv)));
					}
					else
					{
						ReachInfeasUV.Add(FVector2D(Su, Sv));
					}
				}
				if (ReachSampleIdx >= Grid * Grid && !bReachEllipseReady)
				{
					// 擬合最大嚴格橢圓：max A·B s.t. 每個不可解可見樣本都在橢圓外；
					// 收 1.5cm＝取樣粒度（2.6cm 格）的誠實折讓——格與格之間沒問過的點
					const float MaxR = FMath::Clamp(ReachMaxExtentCm + 3.0f, 6.0f, 60.0f);
					float BestA = 3.0f;
					float BestB = 3.0f;
					float BestArea = 0.0f;
					for (float CandA = 3.0f; CandA <= MaxR; CandA += 0.5f)
					{
						float CandB = MaxR;
						for (const FVector2D& Sm : ReachInfeasUV)
						{
							const float Un = FMath::Abs(Sm.X) / CandA;
							if (Un < 0.999f)
							{
								CandB = FMath::Min(CandB, FMath::Abs(Sm.Y) /
									FMath::Sqrt(1.0f - Un * Un));
							}
						}
						if (CandA * CandB > BestArea)
						{
							BestArea = CandA * CandB;
							BestA = CandA;
							BestB = CandB;
						}
					}
					ReachA = FMath::Max(BestA - 1.5f, 1.0f);
					ReachB = FMath::Max(BestB - 1.5f, 1.0f);
					bReachEllipseReady = true;
					UpdateReachVeilShell(); // 擬合完成＝設材質參數+掛殼
				}
			}
		}
	}
	else
	{
		DrawSolveYawDeg = Az;
		bDrawTipReachable = false;
	}

	// （07-20 筆即游標：固定係數追趕層退役——濾波已在 aim 輸入層做完（本人=1€、
	// 他端=複製追趕），姿勢＝濾波 aim 的直接解、零額外滯後）
	const float ShownHip = DrawSolveHipDeg;
	const float ShownAnkle = DrawSolveAnkleDeg;

	// 整身 yaw（不彎任何關節）：lean 中所有端都套本地解值——simulated proxy 若吃
	// 引擎壓縮複製（~1.4° 量化）＝旁人看到的身體階梯跳（07-20 抖動病因之一）
	SetActorRotation(FRotator(0.0f, DrawSolveYawDeg, 0.0f));

	// --- 套用增量（與前向模型逐步同構；樞軸=中性姿座標）---
	if (HipsIdx != INDEX_NONE && FMath::Abs(ShownHip) > 0.05f)
	{
		const FQuat QLean(FVector::XAxisVector, FMath::DegreesToRadians(ShownHip));
		RotSubtreeAboutPivot(HipsIdx, QLean, HipPivot);
		const FQuat QFoot(FVector::XAxisVector, FMath::DegreesToRadians(-ShownHip));
		for (const int32 FootIdx : { LFootIdx, RFootIdx })
		{
			if (FootIdx != INDEX_NONE)
			{
				RotSubtreeAboutPivot(FootIdx, QFoot, CS[FootIdx].GetLocation());
			}
		}
	}
	if (HipsIdx != INDEX_NONE && bSolverReady && FMath::Abs(ShownAnkle) > 0.05f)
	{
		const FQuat QRock(FVector::XAxisVector, FMath::DegreesToRadians(ShownAnkle));
		RotSubtreeAboutPivot(HipsIdx, QRock, AnklePivot);
		const FQuat QFoot(FVector::XAxisVector, FMath::DegreesToRadians(-ShownAnkle));
		for (const int32 FootIdx : { LFootIdx, RFootIdx })
		{
			if (FootIdx != INDEX_NONE)
			{
				RotSubtreeAboutPivot(FootIdx, QFoot, CS[FootIdx].GetLocation());
			}
		}
	}

	// --- 落地補償：腳/趾最低點貼地（元件 Z ∥ 世界 Z：BodyStandRelRot 只轉 yaw）---
	float MinZ = TNumericLimits<float>::Max();
	for (const int32 I : { LFootIdx, RFootIdx, LToeIdx, RToeIdx })
	{
		if (I != INDEX_NONE)
		{
			MinZ = FMath::Min(MinZ, static_cast<float>(CS[I].GetLocation().Z));
		}
	}
	if (MinZ != TNumericLimits<float>::Max())
	{
		const FVector Dz(0.0f, 0.0f, DrawToePadCm - MinZ);
		for (int32 i = 0; i < NumBones; ++i)
		{
			CS[i].SetLocation(CS[i].GetLocation() + Dz);
		}
	}

	// --- Neck 臉向精對（全框解：+Y=臉、+Z≈頭頂＝零 roll；恆等式的骨骼側閉環）---
	const int32 NeckIdx = IdxOf(TEXT("Neck"));
	const int32 HeadIdx = IdxOf(TEXT("Head"));
	if (NeckIdx != INDEX_NONE && HeadIdx != INDEX_NONE)
	{
		const FTransform CompT = BowBody->GetComponentTransform();
		// 臉向：有 P＝look-at P（從真眉心看向落墨點——相機在錨點、臉在真頭，兩者於 P
		// 匯聚＝「你看的點≡臉看的點≡旁人讀到的點」）；無 P＝平行 aim 自由看。
		// 稿筆游標制（07-31）：臉改看「注視點」＝沿 gaze 方向、P 深度處——手畫手的、
		// 臉帶惰性追著筆走（user 定案：臉與手/身體的相對位移可接受、手與身體不脫鉤）。
		// 本人=DrawGaze*；他端 Az/Tilt 經複製通道收到的本來就是 gaze（同一語義零分支）
		const bool bStencilFace = ActiveNeedle() == EInkNeedle::Stencil;
		float FaceAz = Az;
		float FaceTilt = Tilt;
		if (bStencilFace && IsLocallyControlled() && bDrawGazeInit)
		{
			FaceAz = DrawGazeAz;
			FaceTilt = DrawGazeTilt;
		}
		FRotator AimRot(-FaceTilt, FaceAz, 0.0f);
		if (bDrawTargetValid)
		{
			FVector LookPt = DrawTargetWorld;
			if (bStencilFace)
			{
				const FVector RayO = GetAimRayOrigin();
				LookPt = RayO + AimRot.Vector() * FMath::Max(
					static_cast<float>((DrawTargetWorld - RayO).Size()), 10.0f);
			}
			const FVector BrowEstW = CompT.TransformPosition(CS[HeadIdx].GetLocation()) +
				AimRot.Vector() * 13.0f + FRotationMatrix(AimRot).GetUnitAxis(EAxis::Z) * 8.0f;
			const FVector ToP = LookPt - BrowEstW;
			if (ToP.SizeSquared() > 25.0f)
			{
				AimRot = ToP.Rotation();
			}
		}
		const FVector DesiredCS = CompT.InverseTransformVectorNoScale(AimRot.Vector());
		const FVector CrownCS = CompT.InverseTransformVectorNoScale(
			FRotationMatrix(AimRot).GetUnitAxis(EAxis::Z));
		FQuat HeadRestCSQ = FQuat::Identity;
		for (int32 I = HeadIdx; I != INDEX_NONE; I = Ref.GetParentIndex(I))
		{
			HeadRestCSQ = Ref.GetRefBonePose()[I].GetRotation() * HeadRestCSQ;
		}
		const FQuat TargetHeadQ =
			FRotationMatrix::MakeFromYZ(DesiredCS, CrownCS).ToQuat() * HeadRestCSQ;
		FQuat DeltaQ = TargetHeadQ * CS[HeadIdx].GetRotation().Inverse();
		FVector Axis;
		float Angle;
		DeltaQ.ToAxisAndAngle(Axis, Angle);
		if (Angle > PI)
		{
			Angle -= 2.0f * PI; // 短弧
		}
		// 45°＝生理域（07-20 三修：75° 讓頭殼把身體不肯彎的角度全吃下——陡俯時開放
		// 切緣刺穿胸背=脖子破洞。身體增量域放寬後頭只補殘差；極陡時臉鉗在 45°、
		// 相機仍正對落墨點，臉=「幾乎看著」）
		Angle = FMath::Clamp(Angle, -FMath::DegreesToRadians(45.0f), FMath::DegreesToRadians(45.0f));
		DeltaQ = FQuat(Axis, Angle);
		// 施加在 Head 骨（樞軸=頭底），Neck 保持 Backup4 原值——脖切架構鐵則：
		// 頭殼（Head=1.0 硬權重）自由動、身側脖樁（帶 Neck 權重的後頸肉）不准被程式
		// 甩動、兩者由 NeckStretch 每幀橋接。07-20 viewport 實錘：轉 Neck 子樹＝
		// 後頸隆起＋髮下裂縫（脖樁整圈被甩＋橋接身側錨環被搬離）。
		RotSubtreeAboutPivot(HeadIdx, DeltaQ, CS[HeadIdx].GetLocation());
	}

	// --- 寫入：補償換算＋實測收斂迴圈（WriteBowPoseConverged；07-20 robo 實錘教訓）
	// 驗證骨＝雙臂鏈尾（最深）＋Head——收斂逐層傳播，驗淺骨會留下半收斂手臂 ---
	TArray<FName> VerifyBones;
	VerifyBones.Add((GripBoneIdx != INDEX_NONE) ? PenGripBoneName : FName(TEXT("RightHand")));
	VerifyBones.Add(TEXT("LeftHandProp"));
	VerifyBones.Add(TEXT("LeftHand"));
	VerifyBones.Add(TEXT("Head"));
	if (!WriteBowPoseConverged(Ref, CS, VerifyBones))
	{
		bLeanPoseDirty = true; // 本 tick 沒收斂＝下 tick 續寫（髒檢查不得凍結半收斂姿勢）
	}
}

void ANiceInkCharacter::UpdateReachVeilShell()
{
	// veil 殼（07-29 橢圓制）：材質吃三個向量參數（橢圓心＋預除半軸的兩軸），
	// 每個皮膚像素在材質裡直接算 (u/A)²+(v/B)²——光滑羽化圓弧、零貼圖＝斑在
	// 構造上不存在。殼只存在於作畫者自己的 client（NewObject 本地元件、不複製）。
	ANiceInkCharacter* Victim = LeanTarget.Get();
	if (!Victim || !Victim->Body || !bReachEllipseReady)
	{
		return;
	}
	if (!ReachVeilMaterial)
	{
		ReachVeilMaterial = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/Characters/M_ReachVeil.M_ReachVeil"));
		if (!ReachVeilMaterial)
		{
			return; // 資產缺席＝無標記（收筆閘照常工作）
		}
	}
	if (!ReachVeilMID)
	{
		ReachVeilMID = UMaterialInstanceDynamic::Create(ReachVeilMaterial, this);
	}
	const FVector PH = ReachAxisH / FMath::Max(ReachA, 1.0f);
	const FVector PV = ReachAxisV / FMath::Max(ReachB, 1.0f);
	ReachVeilMID->SetVectorParameterValue(TEXT("EllipseCenter"),
		FLinearColor(ReachLockW.X, ReachLockW.Y, ReachLockW.Z, 0.0f));
	ReachVeilMID->SetVectorParameterValue(TEXT("EllipseAxisH"),
		FLinearColor(PH.X, PH.Y, PH.Z, 0.0f));
	ReachVeilMID->SetVectorParameterValue(TEXT("EllipseAxisV"),
		FLinearColor(PV.X, PV.Y, PV.Z, 0.0f));
	if (!ReachVeilShell)
	{
		ReachVeilShell = NewObject<UStaticMeshComponent>(this, TEXT("ReachVeilShell"));
		ReachVeilShell->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ReachVeilShell->SetCastShadow(false);
		ReachVeilShell->SetIsReplicated(false);
		ReachVeilShell->RegisterComponent();
	}
	ReachVeilShell->AttachToComponent(Victim->Body,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	ReachVeilShell->SetRelativeTransform(FTransform::Identity);
	ReachVeilShell->SetStaticMesh(Victim->Body->GetStaticMesh());
	for (int32 SlotIdx = 0; SlotIdx < ReachVeilShell->GetNumMaterials(); ++SlotIdx)
	{
		ReachVeilShell->SetMaterial(SlotIdx, ReachVeilMID);
	}
	ReachVeilShell->SetOwnerNoSee(false); // 顯式還原（進鎖的 OwnerNoSee 掃射雙保險）
	ReachVeilShell->SetVisibility(true);
}

bool ANiceInkCharacter::IsInsideReachEllipse(const FVector& P) const
{
	// 單一裁判查式（07-29 橢圓制）：未擬合完＝一律可畫（收筆閘用活 reach 兜）
	if (!bReachEllipseReady || ReachA <= 0.0f || ReachB <= 0.0f)
	{
		return true;
	}
	const FVector D = P - ReachLockW;
	const float U = FVector::DotProduct(D, ReachAxisH) / ReachA;
	const float V = FVector::DotProduct(D, ReachAxisV) / ReachB;
	return U * U + V * V <= 1.0f;
}

FString ANiceInkCharacter::DebugRoboReachStats()
{
	// 距離圓半徑量測（07-29；量測不猜）：可見皮膚樣本按「離鎖點距離」排序，
	// 回報嚴格圈 R100（最近不可解點）與穩健圈 R95（圈內可解率 ≥95% 的最大半徑）
	ANiceInkCharacter* Victim = LeanTarget.Get();
	if (!bLeanLocked || !Victim || !Victim->Body || !bDrawEyeAnchorValid ||
		!BowBody || !EnsureBowBodyAsset() || !bPenGripCalibrated || !GetWorld())
	{
		return TEXT("ERR not-locked/anchor/calib");
	}
	const USkinnedAsset* Asset = BowBody->GetSkinnedAsset();
	const FReferenceSkeleton& Ref = Asset->GetRefSkeleton();
	auto IdxOf = [&](const TCHAR* BoneName) { return Ref.FindBoneIndex(FName(BoneName)); };
	TArray<FTransform> CS;
	ComposeLeanBaseCS(Ref, CS);
	const int32 HipsIdx = IdxOf(TEXT("Hips"));
	const int32 LFootIdx = IdxOf(TEXT("LeftFoot"));
	const int32 RFootIdx = IdxOf(TEXT("RightFoot"));
	const int32 LToeIdx = IdxOf(TEXT("LeftToeBase"));
	const int32 RToeIdx = IdxOf(TEXT("RightToeBase"));
	const int32 GripBoneIdx = Ref.FindBoneIndex(PenGripBoneName);
	if (HipsIdx == INDEX_NONE || LFootIdx == INDEX_NONE || RFootIdx == INDEX_NONE ||
		GripBoneIdx == INDEX_NONE)
	{
		return TEXT("ERR bones");
	}
	FLeanSolveCtx Ctx;
	Ctx.HipPivot = CS[HipsIdx].GetLocation();
	Ctx.AnklePivot = (CS[LFootIdx].GetLocation() + CS[RFootIdx].GetLocation()) * 0.5f;
	Ctx.Foot0[0] = CS[LFootIdx].GetLocation();
	Ctx.Foot0[1] = CS[RFootIdx].GetLocation();
	Ctx.Toe0[0] = LToeIdx != INDEX_NONE ? CS[LToeIdx].GetLocation() : Ctx.Foot0[0];
	Ctx.Toe0[1] = RToeIdx != INDEX_NONE ? CS[RToeIdx].GetLocation() : Ctx.Foot0[1];
	Ctx.Tip0 = CS[GripBoneIdx].GetLocation() +
		CS[GripBoneIdx].GetRotation().RotateVector(PenTipLocalCm);
	Ctx.TipDirCS = (Ctx.Tip0 - CS[GripBoneIdx].GetLocation()) / FMath::Max(PenTipAheadCm, 1.0f);
	Ctx.ExitCS = Ctx.Tip0 - Ctx.TipDirCS * PenNeedleNominalCm;
	Ctx.ActorLoc = GetActorLocation();
	Ctx.BodyRelLoc = BodyStandRelLoc;
	Ctx.BodyRelRot = BodyStandRelRot;
	Ctx.NeedleNominalCm = PenNeedleNominalCm;

	const FVector Anchor = DrawEyeAnchorWorld;
	const FVector Lock(LeanPoint);
	FCollisionQueryParams VisQP(SCENE_QUERY_STAT(NiceInkReachStats), /*bInTraceComplex=*/true);
	for (TActorIterator<ANiceInkCharacter> It(GetWorld()); It; ++It)
	{
		if (*It != Victim)
		{
			VisQP.AddIgnoredActor(*It);
		}
	}
	constexpr int32 N = 64;
	struct FReachSample
	{
		float D;
		bool bFeas;
	};
	TArray<FReachSample> Samples;
	Samples.Reserve(N * N / 2);
	for (int32 Gy = 0; Gy < N; ++Gy)
	{
		for (int32 Gx = 0; Gx < N; ++Gx)
		{
			FVector Pw, Nw;
			if (!Victim->Body->ResolveUVToWorldWithNormal(
					FVector2D((Gx + 0.5f) / N, (Gy + 0.5f) / N), Pw, Nw))
			{
				continue;
			}
			const FVector Dir = (Pw - Anchor).GetSafeNormal();
			FHitResult Hit;
			if (!GetWorld()->LineTraceSingleByChannel(Hit, Anchor, Pw + Dir * 3.0f,
					ECC_Visibility, VisQP) ||
				Hit.GetActor() != Victim ||
				FVector::Dist(Hit.ImpactPoint, Pw) > 2.5f)
			{
				continue; // 不可見＝游標永遠指不到＝不屬於圈語義的樣本
			}
			float SPsi = DrawSolveYawDeg, SHip = DrawSolveHipDeg, SAnk = DrawSolveAnkleDeg;
			const bool bFeas = Ctx.Feasible(Pw, Dir.Rotation().Yaw, SPsi, SHip, SAnk);
			Samples.Add({ static_cast<float>(FVector::Dist(Pw, Lock)), bFeas });
		}
	}
	Samples.Sort([](const FReachSample& A, const FReachSample& B) { return A.D < B.D; });
	float R100 = -1.0f;
	float R95 = 0.0f;
	int32 FeasCount = 0;
	FString NearStr;
	int32 NearN = 0;
	for (int32 i = 0; i < Samples.Num(); ++i)
	{
		if (Samples[i].bFeas)
		{
			++FeasCount;
		}
		else
		{
			if (R100 < 0.0f)
			{
				R100 = Samples[i].D;
			}
			if (NearN < 5)
			{
				NearStr += FString::Printf(TEXT("%.1f "), Samples[i].D);
				++NearN;
			}
		}
		if (static_cast<float>(FeasCount) / static_cast<float>(i + 1) >= 0.95f)
		{
			R95 = Samples[i].D;
		}
	}
	return FString::Printf(TEXT("vis=%d feas=%d R100=%.1f R95=%.1f nearInfeas=[%s]"),
		Samples.Num(), FeasCount, R100, R95, *NearStr);
}

void ANiceInkCharacter::ClearReachVeilShell()
{
	bReachEllipseReady = false;
	ReachSampleIdx = 0;
	ReachInfeasUV.Reset();
	ReachA = 0.0f;
	ReachB = 0.0f;
	if (ReachVeilShell)
	{
		ReachVeilShell->SetVisibility(false);
		ReachVeilShell->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	}
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
	// 筆焊死在右手（2026-07-20 剛臂制）：入鎖恆顯示——旁人 100% 的時間看得到筆。
	// 位置/朝向＝握骨現值 ∘ 一次性校準常數（校準在 ApplyBowPose；shaft=眉→手延長線）。
	// 舊制「筆尖跟著最後墨點」退役——那讓筆只在墨水流出的瞬間存在（07-20 診斷病灶一）。
	if (!PenMesh)
	{
		return;
	}

	// 打稿工具（07-25）：Stencil 檔＝手持麥克筆（MarkerPen）、機器三件套整組讓位；
	// 本端視角下的有效工具（本人=SelectedNeedle、他端=RepNeedle 複製值）
	const bool bStencilTool = ActiveNeedle() == EInkNeedle::Stencil &&
		MarkerPen && MarkerPen->GetStaticMesh();
	bool bShow = false;
	if (bLeanLocked && bPenGripCalibrated && BowBody && BowBody->GetSkinnedAsset() && BowBody->IsVisible())
	{
		const FTransform HandT = BowBody->GetBoneTransformByName(PenGripBoneName, EBoneSpaces::WorldSpace);
		const FQuat HandQ = HandT.GetRotation().GetNormalized();
		const FQuat PenQ = HandQ * PenRotInHand;
		const FVector ShaftOut = PenQ.GetAxisZ(); // 資產 +Z＝筆尖→筆尾（離皮膚向）
		// 出針口基準＝校準常數的固定點；虛擬筆尖＝出針口沿針軸前推「本 tick 針長語義」：
		// 標稱針長（身體解到）或伸針解針長（深度交給針）——兩者都由解算保證落在 P
		const FVector ExitBaseW = HandT.GetLocation() + HandQ.RotateVector(
			PenTipLocalCm * ((PenTipAheadCm - PenNeedleNominalCm) / PenTipAheadCm));
		const float TipAhead = (bDrawTipReachable && DrawNeedleSolveLenCm > 0.0f)
			? DrawNeedleSolveLenCm : PenNeedleNominalCm;
		const FVector TipW = ExitBaseW - ShaftOut * TipAhead;
		FVector TipEffective = TipW; // 稿筆落筆時改寫成 trace 真實命中點
		constexpr float PenHalfLen = 7.5f;
		if (bStencilTool)
		{
			// 骨軸制（07-27 user 定案「握持=手心充滿筆+另一側露一小段、筆直直向前
			// 不歪、再粗一點」）：方向＝RightHandProp 骨 +Y——模型定義的持物軸
			//（探針量測：作畫姿下指向皮膚 (0.38,-0.07,-0.92)≈朝下）。兩代舊方向源
			// 全退役：「眉→手校準軸」是 07-20 幾何權宜、「尖→手連線」在筆尖釘偏軸
			// 點時整支歪斜（07-27 user 抓）。roll 跟骨 X＝穩定（筆圓對稱、roll 無感）。
			// 筆尖＝骨軸上的落點：落筆=沿骨軸 trace 命中；他端 miss=複製 P 沿軸投影
			//（深度與墨同步、恆在軸上=不歪）；懸筆=標稱懸距。
			// 筆尾＝骨原點再向拳背側露 MarkerProtrudeCm＝「另一側露出一小段」構造保證。
			constexpr float MarkerNativeLenCm = 13.0f; // SM_Marker 原長、pivot=筆尖、+Z=筆尾
			constexpr float MarkerProtrudeCm = 19.0f;  // 拳背側露頭（07-27 user 定值路徑
			                                           // 8→16→22→19：22 吃到拇指（融筆），
			                                           // 19=露頭明顯且離開拇指段）
			constexpr float MarkerHoverTipCm = 12.0f;  // 懸筆（未觸發）筆尖離骨原點
			const FVector BoneTipDir = HandQ.GetAxisY();
			const FVector HandLoc = HandT.GetLocation();
			const FQuat PenQBone = FRotationMatrix::MakeFromZX(-BoneTipDir, HandQ.GetAxisX()).ToQuat();
			float TipDist = MarkerHoverTipCm;
			bool bTouching = false;
			// 收筆閘（07-28 顯示制；07-29 單一裁判＝遮罩）：游標在紗區＝筆回「沒按
			// 左鍵」的懸筆樣——收筆；回可畫區且左鍵仍按著＝自動壓回（墨鏈電平觸發、
			// 筆劃自動分段續畫）。游標移動本身永不被干擾。他端無遮罩＝沿用活 reach。
			const bool bTrig = IsLocallyControlled()
				? (bPenTriggerLocal && bCursorDrawable)
				: (bPenTriggerHeld && bDrawTipReachable);
			ANiceInkCharacter* Target = LeanTarget.Get();
			if (bTrig && Target && Target->Body && GetWorld())
			{
				FCollisionQueryParams MarkQP(SCENE_QUERY_STAT(NiceInkMarkerTrace), /*bInTraceComplex=*/true);
				for (TActorIterator<ANiceInkCharacter> It(GetWorld()); It; ++It)
				{
					if (*It != Target)
					{
						MarkQP.AddIgnoredActor(*It);
					}
				}
				FHitResult Hit;
				if (GetWorld()->LineTraceSingleByChannel(Hit, HandLoc - BoneTipDir * 2.0f,
						HandLoc + BoneTipDir * 300.0f, ECC_Visibility, MarkQP) &&
					Hit.GetActor() == Target)
				{
					TipDist = static_cast<float>(
						FVector::DotProduct(Hit.ImpactPoint - HandLoc, BoneTipDir));
					bTouching = true;
				}
				else if (!IsLocallyControlled() && bDrawTargetRepValid)
				{
					// 他端顯示 trace miss（姿勢視差/掠射）：深度取複製 P 沿骨軸投影
					// ＝筆尖深度與墨同步、方向恆直（07-27）
					TipDist = static_cast<float>(
						FVector::DotProduct(FVector(RemoteDrawTargetW) - HandLoc, BoneTipDir));
					bTouching = true;
				}
				TipDist = FMath::Clamp(TipDist, 6.0f, 300.0f);
			}
			const float Dt = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f;
			const float A = FMath::Clamp(Dt * 12.0f, 0.0f, 1.0f);
			const FVector TipFinal = HandLoc + BoneTipDir * TipDist;
			if (!bMarkerPenSmoothValid)
			{
				MarkerPenSmoothedQ = PenQBone;
				MarkerPenSmoothedTip = TipFinal;
				bMarkerPenSmoothValid = true;
			}
			else
			{
				MarkerPenSmoothedQ = FQuat::Slerp(MarkerPenSmoothedQ, PenQBone, A).GetNormalized();
				MarkerPenSmoothedTip = bTouching
					? TipFinal // 落筆＝釘死骨軸落點（零平滑零延遲）
					: FMath::Lerp(MarkerPenSmoothedTip, TipFinal, A);
			}
			const FVector TipShown = bTouching ? TipFinal : MarkerPenSmoothedTip;
			// 跨距＝筆尖→骨原點→再露頭一段（手心充滿筆）；scale.Z=跨距/原長
			const float SpanCm = FMath::Clamp(
				static_cast<float>(FVector::Dist(HandLoc, TipShown)) + MarkerProtrudeCm,
				MarkerNativeLenCm, MarkerNativeLenCm * 8.0f);
			MarkerPen->SetWorldLocationAndRotation(TipShown, MarkerPenSmoothedQ.Rotator());
			// 徑向 3.0×（07-27 user 三輪「不夠粗」：2.2→3.0＝直徑約 6.6cm）
			MarkerPen->SetWorldScale3D(FVector(3.0f, 3.0f, SpanCm / MarkerNativeLenCm));
			// 墨鏈真相＝準星命中點 P（07-27 三修 user 抓「凹凸處筆觸亂掉、不落在
			// 螢幕中央點上」）：舊制沿筆軸再 trace 一次——筆軸射線從出針口斜入，
			// 凸起（乳頭/肚臍/皺摺）會先攔截或遮蔽＝墨落在準星以外的凸面（平滑面上
			// 兩射線由解算保證 ≤0.25cm 同點、凹凸面分家）。畫面歸針恆等式的字面義：
			// 螢幕中心點＝墨——P 本來就是「眼錨沿 aim 的首命中」＝準星所指，直接吃。
			// P 無效（看向空處）＝維持 TipW，落墨由接觸閘自然擋。
			if (IsLocallyControlled())
			{
				if (bDrawTargetValid)
				{
					TipEffective = DrawTargetWorld;
				}
			}
			else if (bDrawTargetRepValid)
			{
				TipEffective = RemoteDrawTargetW;
			}
		}
		else if (bPenIsMachineAsset)
		{
			// 機械體 pivot=握管頂接點（焊死在手的固定端；伸長時機械不動、握管+針動）
			PenMesh->SetWorldLocationAndRotation(
				ExitBaseW + ShaftOut * PenGripBaseLenCm, PenQ.Rotator());
		}
		else
		{
			// 真麥克筆資產：pivot=筆尖；圓柱退路：pivot=中心 → 沿筆身前推半長
			PenMesh->SetWorldLocationAndRotation(
				bPenIsMarkerAsset ? TipW : TipW + ShaftOut * PenHalfLen, PenQ.Rotator());
		}
		PenTipWorld = TipEffective;
		PenShaftDirWorld = ShaftOut;
		bShow = true;

		// --- 伸縮件（07-21「按下左鍵才伸長」＋07-22 分帳制 user 定案「伸長量一半給針、
		// 一半給握管」）：觸發＝沿針軸實測出針深度 D（瞬時、無動畫=「啪」的機械讀感）、
		// 伸長 e=max(0, D-標稱) → 握管拉長 e/2（出針口前移 e/2）、針長=D-e/2；
		// 放開＝握管回基準長、針收樁。各端同構解算＝同長度，無需複製；墨的真相＝針尖。---
		PenNeedleLenCm = 0.0f;
		PenGripLenCm = 0.0f;
		if (NeedleMesh && bPenIsMachineAsset && !bStencilTool)
		{
			float NeedleLen = PenNeedleStubCm;
			float GripLen = PenGripBaseLenCm;
			FVector ExitVisW = ExitBaseW;
			// 收筆閘（07-28 顯示制；07-29 單一裁判＝遮罩）：紗區＝針收樁（同稿筆）
			const bool bTrig = IsLocallyControlled()
				? (bPenTriggerLocal && bCursorDrawable)
				: (bPenTriggerHeld && bDrawTipReachable);
			ANiceInkCharacter* Target = LeanTarget.Get();
			if (bTrig && Target && Target->Body && GetWorld())
			{
				FCollisionQueryParams NeedleQP(SCENE_QUERY_STAT(NiceInkNeedleTrace), /*bInTraceComplex=*/true);
				for (TActorIterator<ANiceInkCharacter> It(GetWorld()); It; ++It)
				{
					if (*It != Target)
					{
						NeedleQP.AddIgnoredActor(*It); // 同 aim trace：只認 LeanTarget
					}
				}
				FHitResult Hit;
				float D = -1.0f;
				FVector TipPoint = FVector::ZeroVector;
				if (GetWorld()->LineTraceSingleByChannel(Hit, ExitBaseW + ShaftOut * 1.0f,
						ExitBaseW - ShaftOut * 300.0f, ECC_Visibility, NeedleQP) &&
					Hit.GetActor() == Target)
				{
					D = static_cast<float>(FVector::Dist(ExitBaseW, Hit.ImpactPoint));
					TipPoint = Hit.ImpactPoint;
				}
				else if (bDrawTipReachable && DrawNeedleSolveLenCm > 0.0f)
				{
					// 顯示層 trace 間歇 miss（掠射面/他端姿勢視差）曾讓針瞬縮回樁再瞬
					// 彈出（user 抓「延長與收回不及時」的一半真兇）——解算真相（nSolve）
					// 還在：針長走解、尖點走軸上解點（07-26）
					D = DrawNeedleSolveLenCm;
					TipPoint = ExitBaseW - ShaftOut * D;
				}
				if (D > 0.0f)
				{
					const float Ext = FMath::Max(D - PenNeedleNominalCm, 0.0f);
					GripLen = PenGripBaseLenCm + Ext * 0.5f;
					ExitVisW = ExitBaseW - ShaftOut * (Ext * 0.5f);
					NeedleLen = FMath::Max(D - Ext * 0.5f, 0.5f);
					PenTipWorld = TipPoint; // 墨與針同一真相（落墨鏈/HUD 錨都吃這裡）
				}
			}
			if (GripMesh)
			{
				// 握管：頂錨在機械體接點、沿 -Z 伸縮（scale.Z=長度 cm）
				GripMesh->SetWorldLocationAndRotation(
					ExitBaseW + ShaftOut * PenGripBaseLenCm, PenQ.Rotator());
				GripMesh->SetWorldScale3D(FVector(1.0f, 1.0f, GripLen));
			}
			if (bNeedleIsAsset)
			{
				// 針資產：基座在（前移後的）出針口沿 -Z、單位長 1cm ⇒ scale.Z=針長 cm
				NeedleMesh->SetWorldLocationAndRotation(ExitVisW, PenQ.Rotator());
				NeedleMesh->SetWorldScale3D(FVector(1.0f, 1.0f, NeedleLen));
			}
			else
			{
				// 圓柱退路：100cm 高、pivot=中心
				NeedleMesh->SetWorldLocationAndRotation(
					ExitVisW - ShaftOut * (NeedleLen * 0.5f), PenQ.Rotator());
				NeedleMesh->SetWorldScale3D(FVector(0.006f, 0.006f, NeedleLen / 100.0f));
			}
			PenNeedleLenCm = NeedleLen;
			PenGripLenCm = GripLen;
		}

	}

	bPenStateValid = bShow;
	// FP 2D 筆制（07-22）：本人入鎖時機器三件 OwnerNoSee——本人畫面上的機器由
	// NiceInkHUD 畫 2D 貼圖＋針線（viewmodel 體感）；旁人看 3D 原樣。
	// 打稿麥克筆（07-25）：本人 FP 直接看 3D 筆（細長不遮畫布、07-18 直接畫制原味）
	const bool bOwnerHudPen = bPenIsMachineAsset && IsLocallyControlled();
	for (UStaticMeshComponent* Part : { PenMesh.Get(), GripMesh.Get(), NeedleMesh.Get() })
	{
		if (Part)
		{
			Part->SetOwnerNoSee(bOwnerHudPen);
		}
	}
	if (MarkerPen)
	{
		// 07-25 二修（user 定案「與機器同構：旁人 3D＋本人 2D」）：本人端 3D 隱藏
		//（骨骼解算噪聲經筆桿放大的搖擺整個離開 FP）、本人畫面的筆由 HUD 畫 2D 向量筆
		MarkerPen->SetOwnerNoSee(true);
		const bool bMarkerVis = bShow && bStencilTool;
		if (!bMarkerVis)
		{
			bMarkerPenSmoothValid = false; // 下次現身從當前姿勢起步（不從舊值飄過來）
		}
		if (MarkerPen->IsVisible() != bMarkerVis)
		{
			MarkerPen->SetVisibility(bMarkerVis);
		}
	}
	const bool bMachineVis = bShow && !bStencilTool;
	if (PenMesh->IsVisible() != bMachineVis)
	{
		PenMesh->SetVisibility(bMachineVis);
	}
	const bool bPartsVis = bMachineVis && bPenIsMachineAsset;
	for (UStaticMeshComponent* Part : { GripMesh.Get(), NeedleMesh.Get() })
	{
		if (Part && Part->IsVisible() != bPartsVis)
		{
			Part->SetVisibility(bPartsVis);
		}
	}
}

void ANiceInkCharacter::UpdateSleepBodyDouble(float DeltaSeconds)
{
	// 睡姿替身（2026-07-15；環繞軌道制 2026-07-16 user 定案；同日二波＝轆轤首伸縮脖）：
	// - 頭部姿勢＝軌道(φ)：兩段式（縮下巴段＋巡航掃方位段，θ 隨方位調變）＋量測抬頭表
	//   ——網格已沿 user 手標 cut_seam_head3 真切開（頭殼 Head=1.0 硬權重），
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
		SetBowBodyVariant(/*bWhole=*/false); // 睡姿替身＝切開版（轆轤首伸縮脖的舞台）
		bSleepDoubleActive = true;
		// 捕捉參考姿勢（分析式擺骨的基底；姿勢快取歸零）——全基底骨重置：
		// 只重置 Neck/Head 會把 lean 殘留的蹲姿脊椎/手臂帶進睡姿替身
		ResetBowBodyBones();
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
			// 追趕係數 12→20（07-26）：同 draw-aim——頭部破綻旁人要早 30ms 看到
			const float K = FMath::Clamp(DeltaSeconds * 20.0f, 0.0f, 1.0f);
			const float DAz = FMath::FindDeltaAngleDegrees(RemoteAimAzDeg, SleepAimAzDeg);
			RemoteAimAzDeg = FMath::Fmod(FMath::Fmod(RemoteAimAzDeg + DAz * K, 360.0f) + 360.0f, 360.0f);
			RemoteAimTiltDeg += (SleepAimTiltDeg - RemoteAimTiltDeg) * K;
		}
		AimAz = RemoteAimAzDeg;
		AimTilt = RemoteAimTiltDeg;
	}

	// 裝睡（Shift）：姿勢與抬升回沉睡樣＝與閉眼完全同一恆等姿勢（頭放回枕上、
	// 脖子自動收合）；指向值凍結在按下前，放開硬切回（美術語言：程式化硬轉）。
	// 本人讀本地鏡像（零延遲）、他端讀複製值——IsFeigningSleep 內建這個選路。
	const bool bPoseAwake = bEyesOpen && !IsFeigningSleep();

	// 指向 frame（CS、世界錨定）：up＝世界垂直、feet＝頭頂方向水平投影的反向；
	// 姿勢＝先縮下巴（臉朝腳側轉 tilt，軸=up×feet）再繞 up 轉到方位 az——
	// 量測掃描的鏡像實作（趴姿共用：up 相對身體自動翻轉，碰撞剖面近似記帳）。
	FQuat OrbitQ = FQuat::Identity; // 閉眼/裝睡＝頭不動（盲瞄不成為破綻）
	FVector LiftCS = FVector::ZeroVector;
	if (bPoseAwake)
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
		 bPoseAwake != bLastPoseEyes))
	{
		bSleepPoseDirty = false;
		LastPoseAz = AimAz;
		LastPoseTilt = AimTilt;
		bLastPoseEyes = bPoseAwake;

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
		const FQuat PoseQ = bPoseAwake ? OrbitQ : FQuat::Identity;
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

// --- 裝睡（Shift 按住；使用者定案 2026-07-17）---

bool ANiceInkCharacter::IsFeigningSleep() const
{
	// 域鉗在 沉睡×睜眼 之內：狀態機任何一端翻掉，裝睡自動失義（殘值無法外漏）
	return bAsleep && bEyesOpen && (IsLocallyControlled() ? bFeignSleepLocal : bFeignSleep);
}

void ANiceInkCharacter::SetFeignSleepLocal(bool bNewFeign)
{
	// 本人端切換共用點（poll edge 唯一呼叫者）：本地立即生效（零延遲）、RPC 上服
	if (bFeignSleepLocal == bNewFeign)
	{
		return;
	}
	bFeignSleepLocal = bNewFeign;
	ServerSetFeignSleep(bNewFeign);
	ApplyFeignVisual();
}

void ANiceInkCharacter::ServerSetFeignSleep_Implementation(bool bNewFeign)
{
	const ANiceInkGameState* GS = GetWorld() ? GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
	const APlayerState* PS = GetPlayerState();
	if (!GS || !PS || PS->GetPlayerId() != GS->VictimPlayerId || !bAsleep || !bEyesOpen)
	{
		return;
	}
	if (bFeignSleep != bNewFeign)
	{
		bFeignSleep = bNewFeign;
		ApplyFeignVisual();
	}
}

void ANiceInkCharacter::OnRep_FeignSleep()
{
	// 裝睡切換＝眼皮貼圖＋替身姿勢重擺（與睜眼破綻同一條視覺路徑；零音效零提示）
	ApplyFeignVisual();
}

void ANiceInkCharacter::ApplyFeignVisual()
{
	// 窄路徑：只動眼皮貼圖＋替身重擺。刻意不走 ApplySleepVisual——那條路在
	// 本人端 bAsleep 時會把臉指向歸零（入睡重置語意），而裝睡的契約正是指向凍結、
	// 放開回到按下前（2026-07-17 robo 抓到的真 bug：az90 被重置成 180 回不去）。
	if (Body)
	{
		Body->SetEyesClosed(bAsleep && (!bEyesOpen || IsFeigningSleep()));
	}
	bSleepPoseDirty = true;
}

void ANiceInkCharacter::ResetBowPose()
{
	bSleepPoseDirty = true; // 替身骨可能被動過——下次 tick 重擺
	if (BowBody)
	{
		BowBody->SetVisibility(false);
		BowBody->SetRelativeLocationAndRotation(BodyStandRelLoc, BodyStandRelRot); // 清掉補位滑移
		ResetBowBodyBones(); // 蹲姿/手臂全清——殘留會漏進下一個使用者（睡姿替身）
	}
	if (Body && !bAsleep)
	{
		Body->SetVisibility(true);
		Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
}

void ANiceInkCharacter::UpdateLeanCamera(APlayerController* PC)
{
	if (!FirstPersonCamera)
	{
		return;
	}

	// 平面畫布制（2026-07-18）：作畫視圖＝畫布相機（地下畫室、定格、FOV 72）——
	// 你的螢幕就是那張攤平的紙；偷瞄＝眉心相機（甦醒者同構：頭骨＋臉向 13cm＋
	// 頭頂向 8cm、朝向＝臉朝向、FOV 同 72）——從紙上抬眼、畫面甩向受害者的臉。
	// 承載體＝本體 FirstPersonCamera（OwnerNoSee 生效；外部相機會讓自己的頭殼穿膜）。
	// 進鎖/切換一律硬切（美術語言 #24）。
	if (!bLeanCamActive)
	{
		bLeanCamActive = true;
		if (bViewOverridden)
		{
			// 前一個系統鏡頭（巡禮/羞辱全景等）殘留 view target——硬切回本體
			PC->SetViewTargetWithBlend(this, 0.0f);
			bViewOverridden = false;
			bWideViewActive = false;
			bThirdPersonActive = false;
			LastViewWorkId = INDEX_NONE;
		}
	}

	// 直接畫制（07-20 眼錨定）：相機＝入畫定格的眉心世界定點＋aim 朝向——與 trace
	// 共用同一定點。臉（Neck）look-at P、筆尖追 P（濾波鏈），三者於 P 匯聚。
	// 自由 aim 時相機用「生」手 aim（零延遲）；**皮繩追趕中相機=針 aim**（07-24
	// 二修：畫面屬於針——螢幕中心恆=針尖=墨、2D 筆恆中心、手抖不進畫面；慢畫=
	// 針貼手=畫面跟手、快甩=限速慢移=機器的重量）。進出追趕針手重合=相機零跳。
	// 稿筆游標制（07-31 二版 user 定案）：相機朝向=凍結狀態（DrawCam*）——恆靜止的
	// 紙＝自由手繪的參考系；只有游標被刻意拉出畫面外很遠才硬切置中（見
	// UpdateStencilCursor 的 RecenterRatio 段）。位置照舊=凍結眼錨。
	// 機器工具照舊「畫面歸針」。
	const bool bStencilFrozenCam = SelectedNeedle == EInkNeedle::Stencil && bDrawCamInit;
	const float CamAz = bTattooChaseActive ? TattooNeedleAz
		: (bStencilFrozenCam ? DrawCamAz : DrawAimAzLocal);
	const float CamTilt = bTattooChaseActive ? TattooNeedleTilt
		: (bStencilFrozenCam ? DrawCamTilt : DrawAimTiltLocal);
	const FRotator AimRot(-CamTilt, CamAz, 0.0f);
	FVector EyePos;
	if (bDrawEyeAnchorValid)
	{
		EyePos = DrawEyeAnchorWorld;
	}
	else if (BowBody && BowBody->GetSkinnedAsset())
	{
		const FVector HeadPos = BowBody->GetBoneTransformByName(TEXT("Head"), EBoneSpaces::WorldSpace).GetLocation();
		const FVector FaceDir = AimRot.Vector();
		const FVector Crown = FRotationMatrix(AimRot).GetUnitAxis(EAxis::Z); // 零 roll 頭頂
		EyePos = HeadPos + FaceDir * 13.0f + Crown * 8.0f; // 眉心（錨定前的入畫首幀）
	}
	else
	{
		EyePos = GetActorLocation() + FVector(0.0f, 0.0f, 40.0f); // 無骨骼退路
	}
	FirstPersonCamera->SetWorldLocationAndRotation(EyePos, AimRot);
}

// --- 畫墨 RPC ---

void ANiceInkCharacter::ServerPaintBegin_Implementation(ANiceInkCharacter* Target, int32 ColorIndex, FVector2D UV, EInkNeedle Needle, uint8 Flow, int32 StrokeSeq)
{
	ANiceInkGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ANiceInkGameMode>() : nullptr;
	if (!GM || !Target || !GM->CanPaintOn(this, Target))
	{
		ServerPaintTarget = nullptr;
		return;
	}

	ServerPaintTarget = Target;
	// 出墨驗速（07-22；07-23 桶補充率分針）：點數令牌桶開帳——覆蓋率被時間定價，
	// 改裝客戶端不得偷速（shader 大針慢節拍、不得用 liner 針速灌大點）
	ServerPaintNeedle = Needle;
	ServerPaintDotBudget = 2.0f;
	ServerPaintLastRefill = GetWorld()->GetTimeSeconds();
	// 玩家作畫一律點刺筆劃（工具=刺青機；robo 線畫走 GameMode DebugRoboStroke=false）。
	// 打稿筆＝固定龍膽紫（server 端強制：稿是導引不是作品、不吃調色盤——改裝客戶端
	// 也塞不進彩色稿）
	const FLinearColor InkColor = (Needle == EInkNeedle::Stencil)
		? NiceInkStencil::Color() : FNiceInkPalette::Get(ColorIndex);
	Target->MulticastPaintBegin(GetInkAuthorId(), InkColor, UV,
		/*bDotStroke=*/true, Needle, Flow, StrokeSeq);
}

void ANiceInkCharacter::ServerPaintPoints_Implementation(const TArray<FVector2D>& UVs, const TArray<uint8>& Flows)
{
	ANiceInkCharacter* Target = ServerPaintTarget.Get();
	ANiceInkGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ANiceInkGameMode>() : nullptr;
	// 單批上限 256（九版；舊 64 在 shader 3000 排/s 低幀下一批就破=整批靜默拒收）
	// 流量陣列：空=全滿濃度（Liner）；非空必須與點列等長（改裝客戶端亂餵=整批拒收）
	if (!Target || !GM || !GM->CanPaintOn(this, Target) || UVs.Num() == 0 || UVs.Num() > 256 ||
		(Flows.Num() != 0 && Flows.Num() != UVs.Num()))
	{
		return;
	}

	// 點數令牌桶（07-22；07-23 分針）：針數/秒 ≤ 該針型 Hz×1.5＋小桶突發（與客端
	// 出針預算天花板同率——距離節拍的抖動路徑膨脹合法多出幾針）。按「數量」限流
	//（每針=該針型固定面積=墨的真幣）；UV 距離驗速會被跨縫合法大跳誤傷，不用。
	const float NowS = GetWorld()->GetTimeSeconds();
	const float RefillHz = (ServerPaintNeedle == EInkNeedle::Liner ? TattooDotHz : ShaderDotHz);
	// 桶容量隨針型（九版）：硬編 10 在 shader 合法 3000 排/s 下=server 端第二層
	// 飢餓點（每批 30~100 針、桶只裝 10=靜默丟針+各端墨不同步）
	ServerPaintDotBudget = FMath::Min(
		ServerPaintDotBudget + (NowS - ServerPaintLastRefill) * RefillHz * 1.5f,
		FMath::Max(10.0f, RefillHz * 0.35f));
	ServerPaintLastRefill = NowS;
	const int32 Allowed = FMath::FloorToInt(ServerPaintDotBudget);
	if (Allowed <= 0)
	{
		return;
	}
	if (UVs.Num() <= Allowed)
	{
		ServerPaintDotBudget -= UVs.Num();
		Target->MulticastPaintPoints(GetInkAuthorId(), UVs, Flows);
		return;
	}
	TArray<FVector2D> Accepted(UVs.GetData(), Allowed); // 超額針裁掉（順序保留）
	TArray<uint8> AcceptedFlows;
	if (Flows.Num() > 0)
	{
		AcceptedFlows.Append(Flows.GetData(), Allowed); // 與點列同裁＝索引恆對齊
	}
	ServerPaintDotBudget -= Accepted.Num();
	Target->MulticastPaintPoints(GetInkAuthorId(), Accepted, AcceptedFlows);
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

void ANiceInkCharacter::MulticastPaintBegin_Implementation(int32 AuthorId, FLinearColor Color, FVector2D UV, bool bDotStroke, EInkNeedle Needle, uint8 Flow, int32 StrokeSeq)
{
	// 本地預測對消（07-26）：StrokeSeq≠0＝作畫者客戶端已預畫整條筆劃——該端
	// 跳過自己的 Begin/Points/End（半透明針重播=重複蓋章變深）。server 世界
	//（listen 主機）恆重播；robo/GameMode 直呼恆傳 0＝永不對消（server 發起的
	// 筆劃沒有任何端預畫過）。reliable multicast 同 actor channel 有序＝集合
	// 進出與封包順序一致。
	if (StrokeSeq != 0 && !HasAuthority())
	{
		APlayerController* LocalPC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		ANiceInkCharacter* LocalChar = LocalPC ? Cast<ANiceInkCharacter>(LocalPC->GetPawn()) : nullptr;
		if (LocalChar && LocalChar->IsLocallyControlled() && LocalChar->GetInkAuthorId() == AuthorId)
		{
			ReplaySkipAuthors.Add(AuthorId);
			return;
		}
	}
	ReplaySkipAuthors.Remove(AuthorId);
	if (InkCanvas)
	{
		InkCanvas->BeginStroke(AuthorId, Color, UV, bDotStroke, Needle, Flow);
	}
}

void ANiceInkCharacter::MulticastPaintPoints_Implementation(int32 AuthorId, const TArray<FVector2D>& UVs, const TArray<uint8>& Flows)
{
	if (ReplaySkipAuthors.Contains(AuthorId))
	{
		return; // 作畫者本人：這批已在本地預測時蓋過
	}
	if (InkCanvas)
	{
		// 批次蓋章：整批只開關一次 RT context（細針點排每點 20 tile、逐點開關
		// 4096 霧層 context 會拖垮幀率——robo superfast 實錘）
		const bool bHasFlows = Flows.Num() == UVs.Num();
		InkCanvas->BeginStampBatchFor(AuthorId);
		for (int32 i = 0; i < UVs.Num(); ++i)
		{
			InkCanvas->AddStrokePoint(AuthorId, UVs[i], bHasFlows ? Flows[i] : 255);
		}
		InkCanvas->EndStampBatch();
	}
}

void ANiceInkCharacter::MulticastPaintEnd_Implementation(int32 AuthorId)
{
	if (ReplaySkipAuthors.Remove(AuthorId) > 0)
	{
		return; // 作畫者本人：本地預測已收筆
	}
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

void ANiceInkCharacter::ServerSetNeedle_Implementation(EInkNeedle Needle)
{
	RepNeedle = Needle; // 他端 3D 手持模型的選擇依據（麥克筆 vs 刺青機）
}

void ANiceInkCharacter::MulticastWashStencil_Implementation()
{
	if (InkCanvas)
	{
		InkCanvas->WashStencil();
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

void ANiceInkCharacter::UpdateMarkerSfx(float DeltaSeconds)
{
	// 稿筆摩擦聲＝本地端專屬回饋面。失去本地控制（觀戰/換 pawn）時把殘響停乾淨
	if (!IsLocallyControlled() || DeltaSeconds <= 0.0f)
	{
		if (MarkerLoopComp && MarkerLoopComp->IsPlaying())
		{
			MarkerLoopComp->Stop();
		}
		return;
	}

	// 閘=LMB 按住（bPenTriggerLocal）而非筆劃開著（bPainting）：失去接觸閘會讓
	// 一次拖曳中筆劃反覆收/開——摩擦聲跟手走、不跟出墨狀態走（否則勻速畫被切成段）
	const bool bActive = bLeanLocked && bPenTriggerLocal && SelectedNeedle == EInkNeedle::Stencil;

	// 筆尖真實速度（cm/s）——吃 PenTipWorld＝與墨鏈同源（游標停=P 凍結=零速度）
	float SpeedCmS = 0.0f;
	if (bActive && bMarkerSfxHasLastTip)
	{
		SpeedCmS = static_cast<float>(FVector::Dist(PenTipWorld, MarkerSfxLastTip)) / DeltaSeconds;
	}
	MarkerSfxLastTip = PenTipWorld;
	bMarkerSfxHasLastTip = bActive;

	// EMA τ0.06s：起筆快速爬升、停手自然滑向靜音（停頓＝無聲，不是硬切）
	const float Alpha = FMath::Clamp(DeltaSeconds / 0.06f, 0.0f, 1.0f);
	MarkerSfxSpeedEmaCmS += (SpeedCmS - MarkerSfxSpeedEmaCmS) * Alpha;

	const float Norm = FMath::Clamp(
		MarkerSfxSpeedEmaCmS / FMath::Max(MarkerSfxRefSpeedCmS, 0.1f), 0.0f, 1.0f);
	float Vol = bActive ? FMath::Sqrt(Norm) * MarkerSfxVolume : 0.0f;
	if (const UNiceInkGameInstance* GI = UNiceInkGameInstance::Get(this))
	{
		Vol *= FMath::Clamp(GI->MasterVolume, 0.0f, 1.0f);
	}

	if (Vol <= 0.004f)
	{
		if (MarkerLoopComp && MarkerLoopComp->IsPlaying())
		{
			if (bActive)
			{
				MarkerLoopComp->SetVolumeMultiplier(0.0f); // 筆劃中的停頓：壓音量不停播
			}
			else
			{
				MarkerLoopComp->Stop(); // 收筆即停
			}
		}
		return;
	}

	if (!MarkerLoopComp)
	{
		USoundBase* LoopSound = LoadObject<USoundBase>(nullptr,
			TEXT("/Game/Audio/marker_loop.marker_loop"));
		if (!LoopSound)
		{
			UE_LOG(LogTemp, Warning, TEXT("NiMarkerSfx: asset load FAILED (/Game/Audio/marker_loop)"));
			return;
		}
		MarkerLoopComp = UGameplayStatics::SpawnSound2D(GetWorld(), LoopSound, Vol, 1.0f, 0.0f,
			nullptr, /*bPersistAcrossLevelTransition=*/false, /*bAutoDestroy=*/false);
		if (!MarkerLoopComp)
		{
			return;
		}
	}
	if (!MarkerLoopComp->IsPlaying())
	{
		MarkerLoopComp->Play();
	}
	MarkerLoopComp->SetVolumeMultiplier(Vol);
	// 快掃略尖、慢描略沉——摩擦聲的物理直覺（±10% 內＝不破壞素材質感）
	MarkerLoopComp->SetPitchMultiplier(0.94f + 0.12f * Norm);
}

void ANiceInkCharacter::UpdateWalkAnim(float DeltaSeconds)
{
	// 站立顯示與步態（2026-08-04 骨骼常駐改制）：站立/走路顯示=BowBody 骨骼身體
	//（摺り足＋軟肉彈跳的載體）；靜態 Body 退居真相載體（碰撞/UV 解算照舊、恆隱形）。
	// 睡姿（UpdateSleepBodyDouble）與作畫姿（ApplyBowPose）各自接管顯示——這裡讓位。
	if (!Body || !BowBody)
	{
		return;
	}
	const bool bEligible = !bAsleep && !bLeanLocked;
	if (!bEligible)
	{
		bStandDoubleActive = false; // 睡/鎖接管；回站時重新活化（含殘留重置）
		bGaitIdleWritten = false;
		bGaitPrevFootValid = false;
		return;
	}
	if (!bSkeletalStandEnabled || !EnsureBowBodyAsset())
	{
		UpdateLegacyStatueWalk(DeltaSeconds); // 骨骼資產缺席：舊制雕像搖擺（機制照跑）
		return;
	}

	// 08-15 脖子破洞修：actor 隱形期間寫入的骨姿不進渲染（hidden 元件不上傳骨矩陣），
	// 現身後靜止站姿的「已寫過=不重寫」閘讓頭殼停在初始骨位＝頭浮、脖子開洞
	//（user 截圖：剛進房未動時；一動就好=第一次重寫才上傳）。現身瞬間強制整套
	// 重寫一次（Reset＋姿勢寫入＝骨矩陣上傳）。
	const bool bHiddenNow = IsHidden() || !BowBody->IsVisible();
	if (bWasHiddenForPose && !bHiddenNow)
	{
		bGaitIdleWritten = false;
		bStandLookWritten = false;
		BowBody->MarkRenderDynamicDataDirty(); // 骨矩陣重上傳（雙保險）
		if (NeckStretch)
		{
			NeckStretch->ForceRebuild(); // 伸縮脖跳過快取同步清＝現身後必重建一次
		}
	}
	bWasHiddenForPose = bHiddenNow;

	if (!bStandDoubleActive)
	{
		SetBowBodyVariant(/*bWhole=*/true); // 站立/走路＝縫合版（脖子=蒙皮本體）
		bStandDoubleActive = true;
		bGaitIdleWritten = false;
		bStandLookWritten = false;
		bGaitPrevFootValid = false;
		WalkAnimPhase = 0.0f;
		bGaitPhaseSeedPending = true;
		GaitStanceAlpha = 0.0f;
		// 前一個使用者（睡姿替身/作畫姿）的殘留清乾淨；相對變換回站姿基準
		ResetBowBodyBones();
		BowBody->SetRelativeLocationAndRotation(BodyStandRelLoc, BodyStandRelRot);
		BowBody->SetOwnerNoSee(true); // 第一人稱不見自己身體（SPEC 視角規則）
	}
	// 冪等重申視形（ResetBowPose 等事件路徑會把 Body 還原可見——下一 tick 這裡收回）
	if (Body->IsVisible())
	{
		Body->SetVisibility(false); // 碰撞保留：lean 起手/畫墨/噴射 UV 解算照打
	}
	if (!BowBody->IsVisible())
	{
		BowBody->SetVisibility(true);
	}
	// 皮膚 MID 晚綁防護：BowBody 首次載入時 Body 的 MID 可能尚未建（BeginPlay 順序），
	// EnsurePoseableAsset 只在載入瞬間綁一次——這裡每 tick 廉價指標比對補綁。
	// ghost 佔用中讓路（半透明是 ApplyGhostView 的地盤）。
	if (!bGhostMaterialApplied && Body->GetDynamicMaterial())
	{
		if (const USkeletalMesh* Sk = Cast<USkeletalMesh>(BowBody->GetSkinnedAsset()))
		{
			const TArray<FSkeletalMaterial>& SkMats = Sk->GetMaterials();
			for (int32 i = 0; i < SkMats.Num(); ++i)
			{
				if (!SkMats[i].MaterialSlotName.ToString().Contains(TEXT("Fundoshi")) &&
					BowBody->GetMaterial(i) != Body->GetDynamicMaterial())
				{
					BowBody->SetMaterial(i, Body->GetDynamicMaterial());
				}
			}
		}
	}

	const float Speed2D = bWalkAnimEnabled ? GetVelocity().Size2D() : 0.0f;

	// 屈膝深度＝速度斜坡（0.17s 級進出蹲：快而不瞬跳——瞬跳=彈簧假激勵）
	const float TargetStance = FMath::Clamp(Speed2D / 80.0f, 0.0f, 1.0f);
	GaitStanceAlpha = FMath::FInterpConstantTo(GaitStanceAlpha, TargetStance, DeltaSeconds, 6.0f);

	if (Speed2D < 20.0f && GaitStanceAlpha <= KINDA_SMALL_NUMBER)
	{
		WalkAnimPhase = 0.0f;
		bGaitPhaseSeedPending = true;
		bGaitPrevFootValid = false;
		GaitFootSpeed[0] = GaitFootSpeed[1] = 0.0f;
		if (!bGaitIdleWritten)
		{
			bGaitIdleWritten = true;
			bStandLookWritten = false;
			ResetBowBodyBones(); // 站姿＝ref pose（與雕像版同一剪影；彈跳層隨後疊自己的偏移）
		}
		ApplyStandLookPitch(DeltaSeconds); // 靜止站姿：只擺頭頸俯仰
		return;
	}
	if (bGaitPhaseSeedPending)
	{
		// 開步腳播種（08-15 user 抓「往右移動會先左傾再右傾」）：相位恆從 0
		//（左腳撐地）起步＝往右起步的第一拍重心反向壓左。按初始橫向速度選開步腳：
		// 明確往右（CS +X=角色左側 ⇒ VelCS.X<0）從 0.5（右腳撐地）起＝第一拍重心
		// 即壓向行進側；往左/純前後照舊從 0。守恆式/雙軌制/腳貼地構造全不動。
		bGaitPhaseSeedPending = false;
		const FVector VelCS0 = BowBody->GetComponentTransform().InverseTransformVectorNoScale(GetVelocity());
		WalkAnimPhase = (VelCS0.X < -20.0f) ? 0.5f : 0.0f;
		// 傾斜軸「不」播種（08-15 三刀定案）：殘留的舊行進方向＝前搖的天然起點
		//（起步先朝舊向微傾、等角速擺轉掃到行進側＝anticipation 讀感）——二刀曾
		// 直接播種=前搖全滅、質感沒了（user 打回）。擺轉本體見 ApplyGaitPose。
	}
	bGaitIdleWritten = false;
	ApplyGaitPose(DeltaSeconds, Speed2D);
}

void ANiceInkCharacter::UpdateLegacyStatueWalk(float DeltaSeconds)
{
	// 舊制雕像搖擺（07-17 原樣保留＝骨骼資產缺席的退路）：整具靜態 Body 疊
	// 側傾三角波＋步點彈跳；停步硬還原。
	if (bStandDoubleActive)
	{
		// 骨骼制中途失效（開關被關）：把顯示還給雕像
		bStandDoubleActive = false;
		BowBody->SetVisibility(false);
		ResetBowBodyBones();
	}
	if (!Body->IsVisible())
	{
		Body->SetVisibility(true);
	}
	const float Speed2D = bWalkAnimEnabled ? GetVelocity().Size2D() : 0.0f;

	if (Speed2D < 20.0f)
	{
		if (bWalkAnimApplied)
		{
			bWalkAnimApplied = false;
			WalkAnimPhase = 0.0f;
			// 停步＝硬還原站姿基準（美術語言：硬切）。
			Body->SetRelativeLocationAndRotation(BodyStandRelLoc, BodyStandRelRot);
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

void ANiceInkCharacter::ApplyGaitPose(float DeltaSeconds, float Speed2D)
{
	// 摺り足步態（2026-08-04）：CS 全身組合＋解析二骨腿 IK。
	// 核心守恆式：步幅=速度/步頻 ⇒ 撐地腳的本地後移速率恰=移動速度＝世界釘住；
	// 滑步腳沿地面滑行（Z 恆=ref 地面高——「腳不離地」是構造保證不是動畫技巧）。
	const USkinnedAsset* Asset = BowBody->GetSkinnedAsset();
	const FReferenceSkeleton& Ref = Asset->GetRefSkeleton();
	const int32 NumBones = Ref.GetNum();

	// ref CS 組合（scale 鏈照 DrawPoseData.h 鐵坑：節點根骨 scale=100，不得再手動放大）
	TArray<FTransform> CS;
	CS.SetNum(NumBones);
	for (int32 i = 0; i < NumBones; ++i)
	{
		const int32 Parent = Ref.GetParentIndex(i);
		CS[i] = Ref.GetRefBonePose()[i] * (Parent != INDEX_NONE ? CS[Parent] : FTransform::Identity);
	}
	auto IdxOf = [&](const TCHAR* N) { return Ref.FindBoneIndex(FName(N)); };
	const int32 HipsIdx = IdxOf(TEXT("Hips"));
	const int32 SpineIdx = IdxOf(TEXT("Spine"));
	if (HipsIdx == INDEX_NONE || SpineIdx == INDEX_NONE)
	{
		return;
	}

	// 步頻/步幅
	const float MaxSpeed = GetCharacterMovement() ? FMath::Max(1.0f, GetCharacterMovement()->MaxWalkSpeed) : 250.0f;
	const float SpeedRatio = FMath::Clamp(Speed2D / MaxSpeed, 0.0f, 1.0f);
	const float StepsPerSec = FMath::Lerp(GaitStepsPerSecBase, GaitStepsPerSecMax, SpeedRatio);
	// 一週期=左右各滑一步（相位 0.5 錯開）
	WalkAnimPhase = FMath::Fmod(WalkAnimPhase + DeltaSeconds * StepsPerSec * 0.5f, 1.0f);

	// 速度進元件空間。腿的滑步「不」沿速度向量（08-04 二輪 user 打回實錘：
	// 橫移/轉身暫態時雙腳沿橫向齊掃＝越中線穿膜真兇）——腿走雙軌制（見下）。
	// GaitSlideDirCS（平滑追隨的速度向）只給上身前傾/擺臂用。
	const FTransform CompT = BowBody->GetComponentTransform();
	const FVector VelCS = CompT.InverseTransformVectorNoScale(GetVelocity());
	FVector WantDir = FVector(VelCS.X, VelCS.Y, 0.0f);
	if (WantDir.Normalize())
	{
		// 三刀終案（08-15 三輪 user 逐回打磨）：傾斜軸=**等角速擺轉**掃向行進向。
		// 一輪原版=向量 lerp（反向目標先縮後過零硬跳=卡 80ms 再啪一下=「先左傾
		// 再右傾」）；二輪=播種+硬切（即時正確但前搖/平滑全滅=「質感沒了」）。
		// 擺轉=兩者的正解：殘留舊向自然成為前搖起點、全程連續無卡無跳；小角度
		// 微調近瞬時、180° 反轉=GaitDirSlewDegPerSec 決定的重心轉移弧（600°/s
		// ≈0.3s）。正對 180° 時取道身前（重心經前方轉移的讀感）。
		const float CosA = FVector::DotProduct(GaitSlideDirCS, WantDir);
		const float AngDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(CosA, -1.0f, 1.0f)));
		const float StepDeg = GaitDirSlewDegPerSec * DeltaSeconds;
		if (AngDeg <= StepDeg)
		{
			GaitSlideDirCS = WantDir;
		}
		else
		{
			float Sign = FMath::Sign(FVector::CrossProduct(GaitSlideDirCS, WantDir).Z);
			if (Sign == 0.0f)
			{
				Sign = (GaitSlideDirCS.X > 0.0f) ? 1.0f : -1.0f; // 180° 平手：經 +Y（身前）
			}
			GaitSlideDirCS = GaitSlideDirCS.RotateAngleAxis(Sign * StepDeg, FVector::UpVector);
			GaitSlideDirCS.Z = 0.0f;
			GaitSlideDirCS.Normalize();
		}
	}
	// 雙軌分量步幅（有號；守恆式各自成立）：前後=各腳在自己側軌道上滑、
	// 左右=步幅張開/收攏（側帶鉗位後永不越中線）
	const float InvSteps = 1.0f / FMath::Max(StepsPerSec, 0.5f);
	const float FwdStride = FMath::Clamp(VelCS.Y * InvSteps, -GaitMaxStrideCm, GaitMaxStrideCm);
	const float LatStride = FMath::Clamp(VelCS.X * InvSteps, -GaitMaxStrideCm, GaitMaxStrideCm);

	// 骨盆：屈膝下沉＋重心橫移（壓向撐地腳側）＋微沉浮（單腳承重段微沉）
	// 左腳撐地段=phase[0,0.5)（本地後移＝世界釘住）；CS +X=角色左側
	const float Sin1 = FMath::Sin(2.0f * PI * WalkAnimPhase);      // +1=左撐地中點
	const float Cos2 = FMath::Cos(4.0f * PI * WalkAnimPhase);      // 每步一谷
	const float Drop = GaitStanceDropCm * GaitStanceAlpha;
	const float Shift = GaitWeightShiftCm * GaitStanceAlpha * Sin1;
	const float Bob = GaitBobCm * GaitStanceAlpha * 0.5f * (1.0f - Cos2);
	const FVector PelvisOfs(Shift, 0.0f, -(Drop + Bob));
	for (int32 i = 0; i < NumBones; ++i)
	{
		CS[i].SetLocation(CS[i].GetLocation() + PelvisOfs);
	}

	// 上身前傾（向行進方向；繞髖樞軸）——腿隨後被 IK 重釘，不受此轉影響
	if (GaitTorsoLeanDeg > 0.0f)
	{
		const FVector LeanAxis = FVector::CrossProduct(FVector::UpVector, GaitSlideDirCS).GetSafeNormal();
		if (!LeanAxis.IsNearlyZero())
		{
			// 正角=向 SlideDir 前傾（axis=Up×Dir，Rodrigues 驗算：+θ 把 +Z 轉向 Dir）
			const FQuat LeanQ(LeanAxis, FMath::DegreesToRadians(GaitTorsoLeanDeg * GaitStanceAlpha));
			RotSubtreeAboutPivotCS(Ref, CS, SpineIdx, LeanQ, CS[HipsIdx].GetLocation());
		}
	}

	// 手臂小擺（與同側腳反相：左腳前滑時左臂後擺）
	if (GaitArmSwingDeg > 0.0f)
	{
		const FVector SwingAxis = FVector::CrossProduct(FVector::UpVector, GaitSlideDirCS).GetSafeNormal();
		const float SwingDeg = GaitArmSwingDeg * GaitStanceAlpha * Sin1;
		const int32 ArmL = IdxOf(TEXT("LeftArm"));
		const int32 ArmR = IdxOf(TEXT("RightArm"));
		if (!SwingAxis.IsNearlyZero() && ArmL != INDEX_NONE && ArmR != INDEX_NONE)
		{
			RotSubtreeAboutPivotCS(Ref, CS, ArmL,
				FQuat(SwingAxis, FMath::DegreesToRadians(SwingDeg)), CS[ArmL].GetLocation());
			RotSubtreeAboutPivotCS(Ref, CS, ArmR,
				FQuat(SwingAxis, FMath::DegreesToRadians(-SwingDeg)), CS[ArmR].GetLocation());
		}
	}

	// 腿：解析二骨 IK（腳目標=ref 地面位置+滑步位移；三角波=撐地段線性後移）
	struct FLegDef { const TCHAR* Up; const TCHAR* Low; const TCHAR* Foot; const TCHAR* Toe; float PhaseOfs; };
	const FLegDef Legs[2] = {
		{ TEXT("LeftUpLeg"), TEXT("LeftLeg"), TEXT("LeftFoot"), TEXT("LeftToeBase"), 0.0f },
		{ TEXT("RightUpLeg"), TEXT("RightLeg"), TEXT("RightFoot"), TEXT("RightToeBase"), 0.5f },
	};
	// ref 腳位（未受骨盆位移污染的地面錨——重算一份純 ref CS 太貴，改用「扣回骨盆位移」）
	for (const FLegDef& Leg : Legs)
	{
		const int32 UpIdx = IdxOf(Leg.Up);
		const int32 LowIdx = IdxOf(Leg.Low);
		const int32 FootIdx = IdxOf(Leg.Foot);
		const int32 ToeIdx = IdxOf(Leg.Toe);
		if (UpIdx == INDEX_NONE || LowIdx == INDEX_NONE || FootIdx == INDEX_NONE)
		{
			continue;
		}
		const FVector RefHip = CS[UpIdx].GetLocation();               // 已含骨盆位移（正確：髖跟骨盆走）
		const FVector RefKnee = CS[LowIdx].GetLocation() - PelvisOfs; // 扣回=純 ref
		const FVector RefFoot = CS[FootIdx].GetLocation() - PelvisOfs;
		const FVector RefHipPure = RefHip - PelvisOfs;

		const float P = FMath::Fmod(WalkAnimPhase + Leg.PhaseOfs, 1.0f);
		const float Tri = 4.0f * FMath::Abs(P - 0.5f) - 1.0f;         // p=0→+1、p=0.5→-1（撐地段線性後移）
		// 雙軌制腳目標：前後沿自己側軌道、左右張開/收攏；Z=ref＝貼地（構造保證）
		FVector FootTarget = RefFoot;
		FootTarget.Y += Tri * FwdStride * 0.5f;
		FootTarget.X += Tri * LatStride * 0.5f;
		// 側帶鉗位＝不越中線的構造保證（左腳恆左、右腳恆右；08-04 二輪穿膜終案）
		const bool bLeftLeg = RefFoot.X >= 0.0f;
		FootTarget.X = bLeftLeg
			? FMath::Clamp(FootTarget.X, GaitLatBandMinCm, GaitLatBandMaxCm)
			: FMath::Clamp(FootTarget.X, -GaitLatBandMaxCm, -GaitLatBandMinCm);

		const float L1 = FVector::Dist(RefHipPure, RefKnee);
		const float L2 = FVector::Dist(RefKnee, RefFoot);
		FVector D = FootTarget - RefHip;
		float DLen = D.Size();
		const float MinLen = FMath::Abs(L1 - L2) + 0.5f;
		const float MaxLen = (L1 + L2) - 0.5f;
		DLen = FMath::Clamp(DLen, MinLen, MaxLen);
		const FVector DHat = D.GetSafeNormal();

		// 膝極向＝rest 幾何導出（馬步外弓角全程保留；舊「行進向+外側」混合極向=
		// 橫移時一側膝內塌的穿膜真兇——08-04 二輪定罪拆除）
		const FVector RefAxis = (RefFoot - RefHipPure).GetSafeNormal();
		const FVector KneeOff = RefKnee - RefHipPure;
		const FVector PoleRef = (KneeOff - FVector::DotProduct(KneeOff, RefAxis) * RefAxis).GetSafeNormal();
		FVector Pole = (PoleRef - FVector::DotProduct(PoleRef, DHat) * DHat).GetSafeNormal();
		if (Pole.IsNearlyZero())
		{
			Pole = PoleRef;
		}
		const float A = (L1 * L1 - L2 * L2 + DLen * DLen) / (2.0f * DLen);
		const float H = FMath::Sqrt(FMath::Max(L1 * L1 - A * A, 1.0f));
		const FVector Knee = RefHip + DHat * A + Pole * H;
		const FVector FootSolved = RefHip + DHat * DLen;

		// 大腿/小腿：ref 段向→新段向的最小旋轉疊在 ref 骨向上（保留原扭轉）
		const FQuat ThighDelta = FQuat::FindBetweenNormals(
			(RefKnee - RefHipPure).GetSafeNormal(), (Knee - RefHip).GetSafeNormal());
		const FQuat ShinDelta = FQuat::FindBetweenNormals(
			(RefFoot - RefKnee).GetSafeNormal(), (FootSolved - Knee).GetSafeNormal());
		CS[UpIdx].SetLocation(RefHip);
		CS[UpIdx].SetRotation(ThighDelta * CS[UpIdx].GetRotation());
		CS[LowIdx].SetLocation(Knee);
		CS[LowIdx].SetRotation(ShinDelta * CS[LowIdx].GetRotation());
		// 腳掌：位置=解出點、旋轉=ref（腳底貼平、趾向不變——滑步的「摺」讀感）
		FTransform FootRefT = CS[FootIdx];
		FootRefT.SetLocation(FootSolved);
		CS[FootIdx] = FootRefT;
		if (ToeIdx != INDEX_NONE)
		{
			// 趾骨：跟著腳掌的 ref 相對關係走
			const FTransform ToeLocal = FTransform(Ref.GetRefBonePose()[ToeIdx]);
			CS[ToeIdx] = ToeLocal * CS[FootIdx];
		}
	}

	// 寫入（收斂迴圈：poseable 快取陷阱的既有解法；驗證骨含最深鏈尾）
	// 視野俯仰（08-15）：Neck+Head 繞頸樞軸俯仰疊在步態上（yaw 恆 0）
	ApplyLookPitchToCS(Ref, CS, DeltaSeconds);

	static const TArray<FName> GaitVerifyBones = {
		FName(TEXT("LeftFoot")), FName(TEXT("RightFoot")),
		FName(TEXT("RightHand")), FName(TEXT("Head")) };
	WriteBowPoseConverged(Ref, CS, GaitVerifyBones);

	// 探針用腳世界速度（撐地腳釘住斷言的量測源）
	const FVector FootWL = BowBody->GetBoneTransformByName(TEXT("LeftFoot"), EBoneSpaces::WorldSpace).GetLocation();
	const FVector FootWR = BowBody->GetBoneTransformByName(TEXT("RightFoot"), EBoneSpaces::WorldSpace).GetLocation();
	if (bGaitPrevFootValid && DeltaSeconds > KINDA_SMALL_NUMBER)
	{
		GaitFootSpeed[0] = static_cast<float>(FVector::Dist2D(FootWL, GaitPrevFootW[0])) / DeltaSeconds;
		GaitFootSpeed[1] = static_cast<float>(FVector::Dist2D(FootWR, GaitPrevFootW[1])) / DeltaSeconds;
	}
	GaitPrevFootW[0] = FootWL;
	GaitPrevFootW[1] = FootWR;
	bGaitPrevFootValid = true;
}

float ANiceInkCharacter::CurrentLookPitchForPose(float DeltaSeconds)
{
	// 本人＝相機 pitch 零延遲（旁人相機/第三人稱鏡頭下看自己）；他端＝複製值平滑追趕
	//（上報 30Hz、追趕 K20＝τ50ms，同 aim 同步慣例）
	if (IsLocallyControlled())
	{
		RemoteLookPitchDeg = CameraPitch;
	}
	else
	{
		const float K = FMath::Clamp(DeltaSeconds * 20.0f, 0.0f, 1.0f);
		RemoteLookPitchDeg += (LookPitchDeg - RemoteLookPitchDeg) * K;
	}
	// 指數飽和映射（user 定案）：|p|∈[0,89] → [0,Max]，p=89 恰=Max；符號保留
	const float P = FMath::Clamp(FMath::Abs(RemoteLookPitchDeg), 0.0f, 89.0f) / 89.0f;
	const float K = FMath::Max(LookPitchCurveK, 0.05f);
	const float Norm = (1.0f - FMath::Exp(-K * P)) / (1.0f - FMath::Exp(-K));
	return FMath::Sign(RemoteLookPitchDeg) * LookPitchMaxDeg * Norm;
}

void ANiceInkCharacter::ApplyLookPitchToCS(const FReferenceSkeleton& Ref, TArray<FTransform>& CS, float DeltaSeconds)
{
	// 俯仰＝繞角色左右軸（CS +X=角色左側；+Y 前 +Z 上）：+θ 把 +Y（臉向）轉向 +Z（抬頭）
	// ——與相機 pitch 同號。分攤 Neck（1-share）/Head（share）；yaw 恆 0＝頭身零相對
	// 轉向（user 定案：左右穿膜、全身跟控制器）。
	const float PitchDeg = CurrentLookPitchForPose(DeltaSeconds);
	if (FMath::Abs(PitchDeg) < 0.05f)
	{
		return;
	}
	const int32 NeckIdx = Ref.FindBoneIndex(TEXT("Neck"));
	const int32 HeadIdx = Ref.FindBoneIndex(TEXT("Head"));
	if (NeckIdx == INDEX_NONE || HeadIdx == INDEX_NONE)
	{
		return;
	}
	const float HeadShare = FMath::Clamp(LookPitchHeadShare, 0.0f, 1.0f);
	const FQuat NeckQ(FVector::XAxisVector, FMath::DegreesToRadians(PitchDeg * (1.0f - HeadShare)));
	const FQuat HeadQ(FVector::XAxisVector, FMath::DegreesToRadians(PitchDeg * HeadShare));
	RotSubtreeAboutPivotCS(Ref, CS, NeckIdx, NeckQ, CS[NeckIdx].GetLocation());
	RotSubtreeAboutPivotCS(Ref, CS, HeadIdx, HeadQ, CS[HeadIdx].GetLocation());
}

void ANiceInkCharacter::ApplyStandLookPitch(float DeltaSeconds)
{
	// 靜止站姿（gait 未寫骨）：從 ref pose 組 CS、只疊頭頸俯仰、寫 Neck/Head 兩骨
	if (!BowBody || !BowBody->GetSkinnedAsset())
	{
		return;
	}
	const USkinnedAsset* Asset = BowBody->GetSkinnedAsset();
	const FReferenceSkeleton& Ref = Asset->GetRefSkeleton();
	const int32 NumBones = Ref.GetNum();
	TArray<FTransform> CS;
	CS.SetNum(NumBones);
	for (int32 i = 0; i < NumBones; ++i)
	{
		const int32 Parent = Ref.GetParentIndex(i);
		CS[i] = Ref.GetRefBonePose()[i] * (Parent != INDEX_NONE ? CS[Parent] : FTransform::Identity);
	}
	const float Before = RemoteLookPitchDeg;
	ApplyLookPitchToCS(Ref, CS, DeltaSeconds);
	// 頭頸靜止且已寫過＝不重寫（省 poseable 寫入；jiggle 讀骨照舊）
	if (FMath::Abs(RemoteLookPitchDeg) < 0.05f && FMath::Abs(Before) < 0.05f && bStandLookWritten)
	{
		return;
	}
	bStandLookWritten = true;
	static const TArray<FName> LookVerifyBones = { FName(TEXT("Head")) };
	WriteBowPoseConverged(Ref, CS, LookVerifyBones);
}

void ANiceInkCharacter::UpdateJiggleBones(float DeltaSeconds)
{
	// 軟肉彈跳（2026-08-04 user 委託）：胸×2/肚/臀×2 五骨＝世界空間阻尼彈簧追錨點。
	// 錨點＝姿勢層本 tick 寫完的骨位（Tick 顯式順序：一切擺骨之後）；輸出＝骨位平移
	// 偏移（葉骨、蒙皮權重承載）。走路步點、入鎖硬切、被翻身全是天然激勵源。
	static const TCHAR* JiggleBoneNames[5] = {
		TEXT("Jiggle_Belly"), TEXT("Jiggle_Chest_L"), TEXT("Jiggle_Chest_R"),
		TEXT("Jiggle_Butt_L"), TEXT("Jiggle_Butt_R") };
	if (!BowBody || !BowBody->GetSkinnedAsset() || !BowBody->IsVisible())
	{
		for (FJiggleBoneState& S : JiggleStates)
		{
			S.bValid = false; // 隱藏期不模擬；下一個使用者活化時 ResetBowBodyBones 清基準
		}
		return;
	}
	const FTransform CompT = BowBody->GetComponentTransform();
	if (!bJiggleEnabled)
	{
		// 關閉瞬間把殘留偏移/旋轉還原（不然肚子停在半空/歪著）
		bool bRestored = false;
		for (int32 B = 0; B < 5; ++B)
		{
			FJiggleBoneState& S = JiggleStates[B];
			if (S.bValid && (!S.LastOffsetCS.IsNearlyZero() ||
				S.LastDeltaRotCS.AngularDistance(FQuat::Identity) > 0.003f))
			{
				FTransform T = BowBody->GetBoneTransformByName(FName(JiggleBoneNames[B]), EBoneSpaces::ComponentSpace);
				if (T.GetLocation().Equals(S.LastWrittenCS, 0.01f))
				{
					T.SetLocation(T.GetLocation() - S.LastOffsetCS);
					T.SetRotation(S.LastDeltaRotCS.Inverse() * T.GetRotation());
					BowBody->SetBoneTransformByName(FName(JiggleBoneNames[B]), T, EBoneSpaces::ComponentSpace);
					bRestored = true;
				}
			}
			S.bValid = false;
		}
		if (bRestored)
		{
			BowBody->RefreshBoneTransforms();
		}
		return;
	}
	const float FreqOf[5] = { JiggleBellyHz, JiggleChestHz, JiggleChestHz, JiggleButtHz, JiggleButtHz };
	// 旋轉耦合幾何（2026-08-05 user 抓「晃動時陰影更糟」：純平移不轉法線＝
	// 形狀在動、明暗凍結＝肉在「滑」不在「滾」。修法＝偏移的切向分量換成
	// 繞體內樞軸的旋轉——法線隨骨轉、明暗即時響應；徑向殘餘留平移。
	// 樞軸/力臂按解剖硬編（rest CS 幾何=網格契約）：肚=繞脊椎（骨頭自帶 83cm
	// 長軸、頭在脊椎）力臂 65；胸=繞胸壁內 18cm；臀=繞骨盆內 15cm）
	struct FJiggleLever { FVector PivotOfsCS; float LeverCm; };
	static const FJiggleLever Levers[5] = {
		{ FVector(0.0f, 0.0f, 0.0f), 65.0f },      // Belly：樞軸=骨頭（脊椎）本身
		{ FVector(-6.0f, -17.0f, 0.0f), 18.0f },   // Chest_L：胸壁方向（後偏內）
		{ FVector(6.0f, -17.0f, 0.0f), 18.0f },    // Chest_R
		{ FVector(0.0f, 15.0f, 0.0f), 15.0f },     // Butt_L：骨盆核（前向）
		{ FVector(0.0f, 15.0f, 0.0f), 15.0f },     // Butt_R
	};
	constexpr float MaxRollRad = 0.44f; // 旋轉鉗位 ~25°（防小力臂大偏移翻筋斗）
	bool bWrote = false;
	for (int32 B = 0; B < 5; ++B)
	{
		const FName Bone(JiggleBoneNames[B]);
		FTransform T = BowBody->GetBoneTransformByName(Bone, EBoneSpaces::ComponentSpace);
		FJiggleBoneState& S = JiggleStates[B];
		// 基準骨位/骨旋：姿勢層本 tick 若重寫（讀值≠上次寫值）＝讀值就是新基準；
		// 沒重寫（dirty 檢查跳過）＝上次寫值扣回偏移（旋轉同理：左除回去）
		FVector BaseCS = T.GetLocation();
		FQuat BaseRot = T.GetRotation();
		if (S.bValid && BaseCS.Equals(S.LastWrittenCS, 0.01f))
		{
			BaseCS -= S.LastOffsetCS;
		}
		if (S.bValid && BaseRot.AngularDistance(S.LastWrittenRotCS) < 0.001f)
		{
			BaseRot = S.LastDeltaRotCS.Inverse() * BaseRot;
		}
		const FVector AnchorW = CompT.TransformPosition(BaseCS);
		if (!S.bValid || FVector::DistSquared(AnchorW, S.LastAnchorW) > FMath::Square(100.0f))
		{
			// 初始/傳送尖峰（入座傳送、睡姿擺位）：彈簧直接貼齊，不把瞬移當激勵
			S.PosW = AnchorW;
			S.VelW = FVector::ZeroVector;
			S.LastAnchorW = AnchorW;
			S.bValid = true;
		}
		const float Dt = FMath::Min(DeltaSeconds, 0.1f);
		// 錨點速度＝阻尼的參考系（首輪探針實錘：絕對速度阻尼在等速移動有穩態拖尾
		// 2ζv/ω≈7.6cm＝恆撞鉗位「肚子被風吹住」；相對速度阻尼＝等速零偏移、
		// 只有加速度激勵——步點/硬切/翻身才晃，這才是「跳動」）
		const FVector AnchorVel = Dt > KINDA_SMALL_NUMBER
			? (AnchorW - S.LastAnchorW) / Dt : FVector::ZeroVector;
		S.LastAnchorW = AnchorW;

		// 半隱式歐拉＋子步（ω·h 穩定域；h≤1/90s）
		const float W = 2.0f * PI * FreqOf[B];
		const int32 Steps = FMath::Clamp(FMath::CeilToInt(Dt * 90.0f), 1, 6);
		const float StepH = Dt / Steps;
		for (int32 I = 0; I < Steps; ++I)
		{
			const FVector Acc = (AnchorW - S.PosW) * (W * W) -
				(S.VelW - AnchorVel) * (2.0f * JiggleDamping * W);
			S.VelW += Acc * StepH;
			S.PosW += S.VelW * StepH;
		}

		FVector OffsetW = (S.PosW - AnchorW) * JiggleGain;
		OffsetW = OffsetW.GetClampedToMaxSize(JiggleMaxCm);
		const FVector OffsetCS = CompT.InverseTransformVectorNoScale(OffsetW);
		S.LastSpringCm = static_cast<float>(OffsetCS.Size());

		// 偏移→旋轉耦合：樞軸=基準骨位+解剖偏移、力臂=樞軸→骨頭方向。
		// 切向分量/力臂=轉角（軸=力臂×切向）；徑向分量（呼吸向）留平移。
		const FJiggleLever& Lv = Levers[B];
		const FVector PivotCS = BaseCS + Lv.PivotOfsCS;
		// 肚：骨長軸=rest CS 實測（頭(0,-33,95)→尾(0,49,79)）硬編——不賭 FBX
		// 匯入後的骨局部軸向（陷阱年鑑：每骨軸向重映射不可信）
		const FVector LeverHat = Lv.PivotOfsCS.IsNearlyZero()
			? FVector(0.0f, 0.983f, -0.183f)
			: (-Lv.PivotOfsCS).GetSafeNormal();
		// 徑向不對稱鉗位（08-15 user 抓「頭轉到某些角度乳頭沉進乳房」）：LeverHat 由樞軸
		// 指向骨頭＝朝體外；徑向負值＝整顆骨（連乳頭）往胸壁/骨盆內壓——彈簧 8cm 上限
		// 對「往內」是穿膜距離。往外照舊（彈出來是要的），往內鉗到 JiggleInwardMaxCm。
		float RadialS = FVector::DotProduct(OffsetCS, LeverHat);
		if (RadialS < -JiggleInwardMaxCm)
		{
			RadialS = -JiggleInwardMaxCm;
		}
		const FVector Radial = RadialS * LeverHat;
		const FVector Tangent = OffsetCS - FVector::DotProduct(OffsetCS, LeverHat) * LeverHat;
		FQuat DeltaQ = FQuat::Identity;
		if (!Tangent.IsNearlyZero(0.001f))
		{
			const FVector Axis = FVector::CrossProduct(LeverHat, Tangent.GetSafeNormal()).GetSafeNormal();
			if (!Axis.IsNearlyZero())
			{
				// 胸的轉角上限減半（08-15）：蒙皮繞骨原點轉、乳頭在骨前 ~7cm——25° 讓乳頭
				// 近側掃進胸大肌；15° 上限＝橫向擺幅保留、內掃量減 60%
				const float RollCap = (B == 1 || B == 2) ? FMath::DegreesToRadians(JiggleChestMaxRollDeg) : MaxRollRad;
				const float Ang = FMath::Min(Tangent.Size() / FMath::Max(Lv.LeverCm, 1.0f), RollCap);
				DeltaQ = FQuat(Axis, Ang);
			}
		}
		// 骨頭沿旋轉繞樞軸走＋徑向平移（肚：樞軸=骨頭＝頭不動、只轉）
		const FVector NewLoc = PivotCS + DeltaQ.RotateVector(BaseCS - PivotCS) + Radial;
		const FQuat NewRot = DeltaQ * BaseRot;
		S.LastOffsetCS = NewLoc - BaseCS;
		S.LastWrittenCS = NewLoc;
		S.LastDeltaRotCS = DeltaQ;
		S.LastWrittenRotCS = NewRot;
		if (!NewLoc.Equals(T.GetLocation(), 0.02f) ||
			NewRot.AngularDistance(T.GetRotation()) > 0.003f) // 靜止收斂＝零寫入
		{
			T.SetLocation(NewLoc);
			T.SetRotation(NewRot);
			BowBody->SetBoneTransformByName(Bone, T, EBoneSpaces::ComponentSpace);
			bWrote = true;
		}
	}
	if (bWrote)
	{
		BowBody->RefreshBoneTransforms(); // 同 tick 下游讀骨（筆/伸縮脖）要拿到最終姿勢
	}
}

float ANiceInkCharacter::EffectiveLookSensitivity() const
{
	const UNiceInkGameInstance* GI = UNiceInkGameInstance::Get(this);
	return LookSensitivity * (GI ? GI->GetMouseScale() : 1.0f);
}

float ANiceInkCharacter::DrawAimSensitivity() const
{
	// 開鏡定律（07-24 操作優化）：鎖定 FOV 36 對站姿 90 的螢幕投影放大 ≈3.1×，
	// 角增益不縮=游標三倍速（打霧打出去/割線瞄不準主因——07-24 量測，
	// 站姿與鎖定共用同一 LookSensitivity 是 07-20 直接畫制的沿用疏漏）。
	// 縮放=tan 比（monitor-distance matching 0%，FPS ADS 標準）；DrawSensitivity=口味旋鈕。
	const float Ratio = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(LeanLockedFov, 5.0f, 170.0f) * 0.5f)) /
		FMath::Tan(FMath::DegreesToRadians(45.0f));
	return EffectiveLookSensitivity() * Ratio * DrawSensitivity;
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
		// 跟隨已配置的服務：NULL=LAN 房、EOS=網路房（與主選單 bUseLan 同一條規則）
		Sessions->HostSession(/*bLan=*/!UNiceInkSessionSubsystem::IsOnlineServiceConfigured());
	}
}

void ANiceInkCharacter::NiJoin()
{
	if (UNiceInkSessionSubsystem* Sessions = GetGameInstance() ? GetGameInstance()->GetSubsystem<UNiceInkSessionSubsystem>() : nullptr)
	{
		Sessions->JoinFirstFoundSession(/*bLan=*/!UNiceInkSessionSubsystem::IsOnlineServiceConfigured());
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
