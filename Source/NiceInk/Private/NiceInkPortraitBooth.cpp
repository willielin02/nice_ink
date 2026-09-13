#include "NiceInkPortraitBooth.h"

#include "ImageUtils.h"
#include "TextureResource.h"
#include "Components/BoxComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/TextureCube.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/ScopeExit.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UObjectIterator.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InkBodyComponent.h"
#include "InkCanvasComponent.h"
#include "NiceInkCharacter.h"
#include "NiceInkTypes.h"
#include "NeckStretchComponent.h"
#include "NiceInkFaceShare.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "ImageCore.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

// A/B 診斷：0＝不收身體（舊 v4 畫面）
static TAutoConsoleVariable<int32> CVarNiPortraitCollapseBody(
	TEXT("ni.PortraitCollapseBody"), 1, TEXT("Portrait booth: collapse non-head bones before capture (1) or not (0)."));

// A/B 診斷：0＝不做深度遮罩（身體已收掉時，遮罩只會在頭島的脖子上再切一刀）
static TAutoConsoleVariable<int32> CVarNiPortraitDepthMask(
	TEXT("ni.PortraitDepthMask"), 1, TEXT("Portrait booth: apply the SceneDepth shoulder mask (1) or not (0)."));

// A/B 診斷：投影方式。Fov<=0＝正交（出貨值）；>0＝透視，配 CamDist（相機到瞄準點的距離，cm）
static TAutoConsoleVariable<float> CVarNiPortraitFov(
	TEXT("ni.PortraitFov"), -1.0f, TEXT("Portrait booth override: <0 = use FovDeg property; 0 = orthographic; >0 = perspective horizontal FOV (deg)."));
static TAutoConsoleVariable<float> CVarNiPortraitCamDist(
	TEXT("ni.PortraitCamDist"), -1.0f, TEXT("Portrait booth override: <=0 = use CamDistanceCm property; else camera distance to aim point (cm)."));

ANiceInkPortraitBooth::ANiceInkPortraitBooth()
{
	PrimaryActorTick.bCanEverTick = true;

	// 替身落腳台（亭在世界下方＝沒有現成地板；頂面＝亭原點）
	Floor = CreateDefaultSubobject<UBoxComponent>(TEXT("Floor"));
	SetRootComponent(Floor);
	Floor->SetBoxExtent(FVector(300.0f, 300.0f, 50.0f));
	Floor->SetRelativeLocation(FVector(0, 0, -50.0f));
	Floor->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Floor->SetCollisionResponseToAllChannels(ECR_Block);
	Floor->SetHiddenInGame(true);

	// 均勻環境光＝真天光（與道場同制）。天光是全世界級光源、無 LightingChannels
	//（USkyLightComponent 是 ULightComponentBase 旁系）且 shader 不理通道——
	// 通道隔離不可用，改「時間隔離」：平時隱藏，只在 CaptureNow 的同步捕捉
	// 瞬間亮起、拍完立即隱回＝主視口永不渲染到開著的一幀（見 CaptureNow）。
	// 光源＝指定灰 cubemap（全方向恆定；亭在地下、SLS_CapturedScene 捕到虛空）
	Sky = CreateDefaultSubobject<USkyLightComponent>(TEXT("Sky"));
	Sky->SetupAttachment(Floor);
	Sky->Mobility = EComponentMobility::Movable;
	Sky->SourceType = SLS_SpecifiedCubemap;
	Sky->bLowerHemisphereIsBlack = false; // 下半球也要光（下巴底/髷底）
	static ConstructorHelpers::FObjectFinder<UTextureCube> GrayCube(
		TEXT("/Engine/EngineResources/GrayLightTextureCube.GrayLightTextureCube"));
	if (GrayCube.Succeeded())
	{
		Sky->Cubemap = GrayCube.Object;
	}
	Sky->SetVisibility(false);

	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
	Capture->SetupAttachment(Floor);
	// 正交（user 定案：人物正交的樣子、頭部精準裁切）＋SceneColorHDR
	//（文件保證：RGB=場景色、A=inverse opacity＝透明背景的唯一可靠來源；
	// FinalColorLDR 的 alpha 是垃圾——方形黑底版的教訓）
	Capture->ProjectionType = ECameraProjectionMode::Orthographic;
	Capture->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
}

ANiceInkPortraitBooth* ANiceInkPortraitBooth::Get(const UObject* Ctx)
{
	UWorld* World = Ctx ? Ctx->GetWorld() : nullptr;
	if (!World || World->WorldType != EWorldType::Game)
	{
		// 只在正式遊戲行程開亭（FaceShare 同款範圍閘）：PIE robo 全套零干擾
		//（HUD/選單在亭缺席時自動走舊裁切路墊檔）
		return nullptr;
	}
	for (TActorIterator<ANiceInkPortraitBooth> It(World); It; ++It)
	{
		return *It;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// 藏在世界正下方（席位/道場/選單舞台都碰不到的深度）
	return World->SpawnActor<ANiceInkPortraitBooth>(FVector(0, 0, -8000.0f), FRotator::ZeroRotator, Params);
}

void ANiceInkPortraitBooth::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	++TicksAlive; // 替身的程序化姿勢（步態/垂手）要跑幾拍才穩——之前不出片
}

void ANiceInkPortraitBooth::EnsureDummy()
{
	if (Dummy)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// 替身：站台上、面向 -X 的相機（診斷實測：yaw 0＝臉朝 +X＝拍到背；轉 180）
	const FTransform SpawnT(FRotator(0, 180.0f, 0), GetActorLocation() + FVector(0, 0, 120.0f));
	Dummy = World->SpawnActorDeferred<ANiceInkCharacter>(ANiceInkCharacter::StaticClass(), SpawnT,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Dummy)
	{
		return;
	}
	if (UInkCanvasComponent* Canvas = Dummy->FindComponentByClass<UInkCanvasComponent>())
	{
		// 肖像不畫墨：RT 壓到最小省 VRAM
		Canvas->RenderTargetResolution = 256;
		Canvas->MistRenderTargetResolution = 256;
	}
	Dummy->FinishSpawning(SpawnT);
	Dummy->SetupAsMenuDummy(0);
	DummyTick0 = TicksAlive; // 替身要先 tick 幾拍：站立替身（BowBody 骨骼身體）在它的第一個 UpdateWalkAnim 才接管顯示
	// 頭像＝頭本人：程序化脖（頭身切縫之間的銜接曲面）不入鏡（2026-09-11，見 CaptureNow 的收身段）
	if (UNeckStretchComponent* Neck = Dummy->GetNeckStretch())
	{
		Neck->SetVisibility(false);
	}

	// 光照全隔離：替身所有面件只吃通道 2（世界的平行光/天光照不到＝
	// 肖像亮度跨關卡恆定；亭燈也只打通道 2＝不漏進世界）
	TInlineComponentArray<UPrimitiveComponent*> Prims(Dummy);
	for (UPrimitiveComponent* Prim : Prims)
	{
		Prim->SetLightingChannels(false, false, true);
	}
}

UTexture2D* ANiceInkPortraitBooth::CaptureNow(const FLinearColor& Tone)
{
	if (!Dummy)
	{
		return nullptr;
	}
	// 取景：明確旋鈕瞄準（actor 中心+AimZ＝頭高；骨骼查詢在診斷輪證實不穩）
	const FVector Aim = Dummy->GetActorLocation() + FVector(0, 0, AimZCm);
	const float FovCVar = CVarNiPortraitFov.GetValueOnGameThread();
	const float Fov = (FovCVar >= 0.0f) ? FovCVar : FovDeg;                 // 出貨值＝FovDeg（90mm 等效）；cvar 只做 A/B
	const float DistCVar = CVarNiPortraitCamDist.GetValueOnGameThread();
	const float Dist = (Fov > 0.0f) ? ((DistCVar > 0.0f) ? DistCVar : CamDistanceCm) : 200.0f; // 正交＝距離不影響構圖
	const FVector CamPos = Aim + FVector(-Dist, 0, 0);
	Capture->SetWorldLocationAndRotation(CamPos, (Aim - CamPos).Rotation());
	Capture->ProjectionType = (Fov > 0.0f) ? ECameraProjectionMode::Perspective : ECameraProjectionMode::Orthographic;
	Capture->FOVAngle = (Fov > 0.0f) ? Fov : 90.0f;
	Capture->OrthoWidth = OrthoWidthCm;

	// 時間隔離開燈（本函式全同步、主視口在幀尾才渲染＝世界看不到）：
	// ①世界自己的天光先藏（道場 SaunaSkyLight 會污染肖像——天光不理通道）
	// ②亭天光亮起；ON_SCOPE_EXIT 保證任何 return 路徑都還原
	TArray<USkyLightComponent*> HiddenWorldSkies;
	for (TObjectIterator<USkyLightComponent> It; It; ++It)
	{
		if (*It != Sky && It->GetWorld() == GetWorld() && It->IsVisible())
		{
			It->SetVisibility(false);
			HiddenWorldSkies.Add(*It);
		}
	}
	Sky->SetIntensity(AmbientIntensity);
	Sky->SetVisibility(true);
	ON_SCOPE_EXIT
	{
		Sky->SetVisibility(false);
		for (USkyLightComponent* S : HiddenWorldSkies)
		{
			S->SetVisibility(true);
		}
	};
	// 指定 cubemap 的濾波處理是排隊制——捕捉前強制出隊（首烘不吃黑片）
	USkyLightComponent::UpdateSkyCaptureContents(GetWorld());

	// **頭以外的身體部位隱形**（2026-09-11 user：「在原來的基礎上只動一件事：把頭以外的身體部位標記上去並讓他們
	// 隱形，讓下巴輪廓自然地呈現，不要硬用直線截出頭部區域」）。做法＝可擺骨網格上把 Hips 以下整條鏈縮到 0.001
	//（軀幹、四肢、褌、彈跳骨全部塌成骨盆位置的一點＝在頭下方一公尺、框外），再把 Head 的元件空間變換原樣寫回
	//（Poseable 的 SetBoneTransformByName(ComponentSpace) 會把 local 反推成相對於縮小後父骨的巨大值 ⇒ 頭留在原位）。
	// 頭島的邊界就是手標切縫（NeckSeamData 的 84 環）＝下巴到頸的弧線由網格自己給，沒有任何一條人畫的線。
	// 拍完立刻還原（替身的姿勢系統下一 tick 還要在這具骨架上算）。
	UPoseableMeshComponent* PM = Dummy->GetBowBody();
	const bool bCollapse = PM && CVarNiPortraitCollapseBody.GetValueOnGameThread() != 0;
	FTransform HipsCS0, HeadCS0;
	if (bCollapse)
	{
		HipsCS0 = PM->GetBoneTransformByName(TEXT("Hips"), EBoneSpaces::ComponentSpace);
		HeadCS0 = PM->GetBoneTransformByName(TEXT("Head"), EBoneSpaces::ComponentSpace);
		FTransform HipsTiny = HipsCS0;
		HipsTiny.SetScale3D(FVector(0.001f));
		PM->SetBoneTransformByName(TEXT("Hips"), HipsTiny, EBoneSpaces::ComponentSpace);
		PM->RefreshBoneTransforms();   // 寫→讀之間必須 refresh（PoseableMesh 讀到上一幀＝陷阱年鑑）
		PM->SetBoneTransformByName(TEXT("Head"), HeadCS0, EBoneSpaces::ComponentSpace);
		PM->RefreshBoneTransforms();
	}
	ON_SCOPE_EXIT
	{
		if (bCollapse)
		{
			// 兩根都寫回（Head 的 local 在收身時被反推成巨大值，不還原會在下一次烘焙時把頭炸飛）
			PM->SetBoneTransformByName(TEXT("Hips"), HipsCS0, EBoneSpaces::ComponentSpace);
			PM->RefreshBoneTransforms();
			PM->SetBoneTransformByName(TEXT("Head"), HeadCS0, EBoneSpaces::ComponentSpace);
			PM->RefreshBoneTransforms();
		}
	};

	// **只拍 BowBody 這一個元件**（2026-09-11 定罪：ShowOnlyActors 會把替身身上所有面件都拍進去——靜態 Body
	// 網格在某些 tick 仍可見、與骨骼身體同姿重疊 ⇒ 骨骼上做的任何收身都被靜態網格原樣蓋回去，看起來像沒生效）。
	Capture->ShowOnlyActors.Reset();
	Capture->ShowOnlyComponents.Reset();
	if (PM)
	{
		Capture->ShowOnlyComponents.Add(PM);
	}
	else
	{
		Capture->ShowOnlyActors.Add(Dummy);
	}
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;

	if (!ScratchRT)
	{
		ScratchRT = NewObject<UTextureRenderTarget2D>(this);
		ScratchRT->RenderTargetFormat = RTF_RGBA16f;
		ScratchRT->ClearColor = FLinearColor(0, 0, 0, 1); // A=1＝空（inverse opacity 語義）
		ScratchRT->InitAutoFormat(PortraitSize, PortraitSize);
	}
	Capture->TextureTarget = ScratchRT;
	Capture->CaptureScene();

	// CPU 端：讀回→alpha 反轉→找頭的邊界→方形精準裁切（含小邊距）→
	// 透明背景貼圖（icon＝頭的形狀，user 定案）
	FRenderTarget* Res = ScratchRT->GameThread_GetRenderTargetResource();
	if (!Res)
	{
		return nullptr;
	}
	TArray<FLinearColor> Pixels;
	if (!Res->ReadLinearColorPixels(Pixels) || Pixels.Num() != PortraitSize * PortraitSize)
	{
		return nullptr;
	}

	// 深度遮罩（肩膀切除的物理正解——輪廓法全滅的血價：肩丘與頭同高「寬度
	// 判」誤觸腮、「碰框判」誤觸肩丘、「中央段判」敗於肩顎相連）：
	// 肩/軀幹在臉後方——第二趟拍 SceneDepth，比最近點（鼻尖）深過
	// DepthKeepCm 的像素一律清透明＝只剩頭
	Capture->CaptureSource = ESceneCaptureSource::SCS_SceneDepth;
	Capture->CaptureScene();
	TArray<FLinearColor> DepthPx;
	// 身體已在骨骼上收掉時不做深度遮罩：它只會在頭島自己的脖子上再切一刀（2026-09-11）
	const bool bDepthOk = !bCollapse && CVarNiPortraitDepthMask.GetValueOnGameThread() != 0
		&& Res->ReadLinearColorPixels(DepthPx) && DepthPx.Num() == Pixels.Num();
	Capture->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR; // 還原
	if (bDepthOk)
	{
		float NearDepth = FLT_MAX;
		int32 PreMinY = PortraitSize, PreMaxY = -1;
		for (int32 i = 0; i < Pixels.Num(); ++i)
		{
			if (1.0f - Pixels[i].A > 0.1f)
			{
				NearDepth = FMath::Min(NearDepth, DepthPx[i].R);
				const int32 Y = i / PortraitSize;
				PreMinY = FMath::Min(PreMinY, Y);
				PreMaxY = FMath::Max(PreMaxY, Y);
			}
		}
		if (NearDepth < FLT_MAX && PreMaxY > PreMinY)
		{
			const int32 SplitRow = PreMinY + FMath::RoundToInt((PreMaxY - PreMinY) * DepthSplitFrac);
			for (int32 i = 0; i < Pixels.Num(); ++i)
			{
				const float Keep = (i / PortraitSize) < SplitRow ? DepthKeepUpperCm : DepthKeepLowerCm;
				if (DepthPx[i].R > NearDepth + Keep)
				{
					Pixels[i].A = 1.0f; // inverse opacity＝清透明
				}
			}
		}
	}

	// 第一趟：逐列寬度＋垂直界（A=inverse opacity＝文件語義）
	TArray<int32> RowWidth;
	RowWidth.SetNumZeroed(PortraitSize);
	int32 MinY = PortraitSize, MaxY = -1;
	for (int32 Y = 0; Y < PortraitSize; ++Y)
	{
		for (int32 X = 0; X < PortraitSize; ++X)
		{
			if (1.0f - Pixels[Y * PortraitSize + X].A > 0.1f)
			{
				++RowWidth[Y];
			}
		}
		if (RowWidth[Y] > 0)
		{
			MinY = FMath::Min(MinY, Y);
			MaxY = FMath::Max(MaxY, Y);
		}
	}
	if (MaxY < MinY)
	{
		return nullptr; // 空片（替身還沒就位？）——不記快取、下次再烘
	}

	// 肩膀切除三代（中央帶／頸窄點／中線連續段）**整套退役**（2026-09-11 user：「不要硬用直線截出頭部區域」）。
	// 現制＝拍之前就把頭以外的身體收掉（CaptureNow 的收身段），畫面裡只剩頭，下巴輪廓是網格自己的。

	// 第二趟：只在保留列段內取水平界（肩膀列可能撐寬過 X 界）
	int32 MinX = PortraitSize, MaxX = -1;
	for (int32 Y = MinY; Y <= MaxY; ++Y)
	{
		for (int32 X = 0; X < PortraitSize; ++X)
		{
			if (1.0f - Pixels[Y * PortraitSize + X].A > 0.1f)
			{
				MinX = FMath::Min(MinX, X);
				MaxX = FMath::Max(MaxX, X);
			}
		}
	}
	if (MaxX < MinX)
	{
		return nullptr;
	}

	// 膚色錨定自動曝光（過曝根治）：SceneColorHDR＝無 tonemapper 無曝光的
	// 原始輻射，亮度=光強×cubemap×材質裸乘積——手動 gain 每換光就得重校
	//（天光 5.0 直接撞頂實錘）。均勻光下 輻射≈albedo×K（K=光場常數）：
	// 頭部像素中位亮度÷已知膚色亮度=K，除回＝還原 albedo＝亮度與光強徹底
	// 解耦；每張臉各自對自己的膚色錨＝膚色深淺（身分特徵）原樣保留
	float Gain = ColorGain; // 量測失敗＝退回手動保底
	{
		TArray<float> Lums;
		for (int32 Y = MinY; Y <= MaxY; ++Y)
		{
			for (int32 X = MinX; X <= MaxX; ++X)
			{
				const FLinearColor& C = Pixels[Y * PortraitSize + X];
				if (1.0f - C.A > 0.1f)
				{
					Lums.Add(C.GetLuminance());
				}
			}
		}
		const float ToneLum = Tone.GetLuminance();
		if (Lums.Num() > 16 && ToneLum > 0.005f)
		{
			Lums.Sort();
			// 中位＝膚（相撲頭膚占多數；髷/眉是少數派拉不動 p50）
			const float MedianLum = Lums[Lums.Num() / 2];
			if (MedianLum > KINDA_SMALL_NUMBER)
			{
				Gain = ToneLum / MedianLum;
			}
		}
	}

	// 方形裁切：以頭界的長邊為邊、置中、加 4% 邊距（成品仍方形貼圖＝
	// 消費端零改動；頭以外全透明＝畫出來就是頭形）
	const int32 BW = MaxX - MinX + 1, BH = MaxY - MinY + 1;
	const int32 Side = FMath::Min(PortraitSize, FMath::Max(BW, BH) + FMath::Max(BW, BH) / 12);
	int32 CX = (MinX + MaxX) / 2, CY = (MinY + MaxY) / 2;
	int32 X0 = FMath::Clamp(CX - Side / 2, 0, PortraitSize - Side);
	int32 Y0 = FMath::Clamp(CY - Side / 2, 0, PortraitSize - Side);

	TArray<FLinearColor> Lin;
	Lin.SetNumUninitialized(Side * Side);
	for (int32 Y = 0; Y < Side; ++Y)
	{
		const int32 SrcY = Y0 + Y;
		// 方形窗可能蓋到肩線以下——界外列 alpha 歸零（肩膀不得復活）
		const bool bRowKept = SrcY >= MinY && SrcY <= MaxY;
		for (int32 X = 0; X < Side; ++X)
		{
			FLinearColor C = Pixels[SrcY * PortraitSize + (X0 + X)];
			const float A = bRowKept ? FMath::Clamp(1.0f - C.A, 0.0f, 1.0f) : 0.0f;
			C *= Gain; // 膚色錨定自動曝光（見上）
			// 色調映射（偏灰根治）：主畫面的「不灰」來自引擎 ACES filmic
			// tonemapper（暗部收 toe、中段提對比）——SceneColorHDR 繞過整條
			// 後處理鏈，裸線性→sRGB 天生平灰。補 ACES 擬合曲線（Narkowicz）
			// ＋飽和度旋鈕＝與局內讀感對齊；曲線自帶高光滾降＝軟膝蓋退役
			auto Aces = [](float V)
			{
				return FMath::Clamp(
					V * (2.51f * V + 0.03f) / (V * (2.43f * V + 0.59f) + 0.14f), 0.0f, 1.0f);
			};
			C.R = Aces(C.R); C.G = Aces(C.G); C.B = Aces(C.B);
			const float Lum = C.GetLuminance();
			C.R = FMath::Clamp(Lum + (C.R - Lum) * Saturation, 0.0f, 1.0f);
			C.G = FMath::Clamp(Lum + (C.G - Lum) * Saturation, 0.0f, 1.0f);
			C.B = FMath::Clamp(Lum + (C.B - Lum) * Saturation, 0.0f, 1.0f);
			C.A = A;
			Lin[Y * Side + X] = C;
		}
	}

	// 五官增顯 unsharp（見標頭旋鈕註）：亮度通道箱式可分離模糊、alpha 預乘
	// 加權（透明區不參與＝輪廓邊不吃黑底暈）；只縮放 RGB 不動色相
	if (DetailAmount > 0.0f)
	{
		const int32 N = Side * Side;
		const int32 R = FMath::Clamp(FMath::RoundToInt(DetailRadiusPx), 1, 32);
		TArray<float> PLum, W, Tmp;
		PLum.SetNumUninitialized(N);
		W.SetNumUninitialized(N);
		Tmp.SetNumUninitialized(N);
		for (int32 i = 0; i < N; ++i)
		{
			W[i] = Lin[i].A;
			PLum[i] = Lin[i].GetLuminance() * W[i];
		}
		auto BlurPass = [Side, R](const TArray<float>& Src, TArray<float>& Dst, bool bHoriz)
		{
			const float InvW = 1.0f / static_cast<float>(2 * R + 1);
			for (int32 A0 = 0; A0 < Side; ++A0)
			{
				for (int32 B0 = 0; B0 < Side; ++B0)
				{
					float Sum = 0.0f;
					for (int32 K = -R; K <= R; ++K)
					{
						const int32 B1 = FMath::Clamp(B0 + K, 0, Side - 1);
						Sum += bHoriz ? Src[A0 * Side + B1] : Src[B1 * Side + A0];
					}
					(bHoriz ? Dst[A0 * Side + B0] : Dst[B0 * Side + A0]) = Sum * InvW;
				}
			}
		};
		BlurPass(PLum, Tmp, true);
		BlurPass(Tmp, PLum, false);
		BlurPass(W, Tmp, true);
		BlurPass(Tmp, W, false);
		for (int32 i = 0; i < N; ++i)
		{
			FLinearColor& C = Lin[i];
			if (C.A > 0.05f)
			{
				const float Blur = PLum[i] / FMath::Max(W[i], KINDA_SMALL_NUMBER);
				const float Lum = C.GetLuminance();
				const float S = FMath::Clamp(
					1.0f + DetailAmount * (Lum - Blur) / FMath::Max(Lum, 0.02f), 0.4f, 2.5f);
				C.R = FMath::Clamp(C.R * S, 0.0f, 1.0f);
				C.G = FMath::Clamp(C.G * S, 0.0f, 1.0f);
				C.B = FMath::Clamp(C.B * S, 0.0f, 1.0f);
			}
		}
	}

	TArray<FColor> Out;
	Out.SetNumUninitialized(Side * Side);
	for (int32 i = 0; i < Side * Side; ++i)
	{
		Out[i] = Lin[i].ToFColor(/*bSRGB=*/true);
	}

	LastPixels = Out;
	LastSide = Side;
	// 成品用 CreateTransient 直填 mip0（2026-09-11）：此前走 FImageUtils::CreateTexture2D＝帶 Source 的貼圖，
	// 在編輯器二進位（-game）下要**非同步編譯**平台資料，編好之前資源是佔位＝畫面上一格灰棋盤
	//（進房時間軸 t=32s 實拍抓到）。Transient 貼圖沒有 Source、UpdateResource 當幀就有 RHI 資源。
	UTexture2D* Tex = UTexture2D::CreateTransient(Side, Side, PF_B8G8R8A8);
	if (!Tex)
	{
		return nullptr;
	}
	Tex->SRGB = true;
	Tex->NeverStream = true;
	Tex->Filter = TF_Trilinear;
	if (void* Mip0 = Tex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE))
	{
		FMemory::Memcpy(Mip0, Out.GetData(), Out.Num() * sizeof(FColor));
	}
	Tex->GetPlatformData()->Mips[0].BulkData.Unlock();
	Tex->UpdateResource();
	return Tex;
}

UTexture* ANiceInkPortraitBooth::GetPortraitKeyed(const FString& Key, UTexture2D* Open,
	UTexture2D* Closed, UTexture2D* Mask, const FLinearColor& Tone)
{
	if (const TObjectPtr<UTexture2D>* Found = Cache.Find(Key))
	{
		// 資源還是引擎的佔位（編譯中／初始化中）＝對呼叫端而言「還沒有」——寧可空一幀，不畫棋盤
		UTexture2D* T = *Found;
		return (T && !T->IsDefaultTexture() && T->GetResource()) ? T : nullptr;
	}
	if (!Open || TicksAlive < 3)
	{
		return nullptr; // 未就緒＝呼叫端先用舊路墊檔、下次再來
	}
	EnsureDummy();
	if (!Dummy)
	{
		return nullptr;
	}
	// **替身生成當幀不出片**（2026-09-11 定罪）：舊制第一張永遠在替身還沒 tick 過就拍＝拍到的是靜態 Body
	// 網格（無骨骼），之後的才是 BowBody——同一房的肖像來源不一致。等它 tick 過、BowBody 接管顯示再拍。
	if (TicksAlive - DummyTick0 < 3 || !Dummy->GetBowBody() || !Dummy->GetBowBody()->IsVisible())
	{
		return nullptr; // 呼叫端下次再來
	}
	if (UInkBodyComponent* Body = Dummy->FindComponentByClass<UInkBodyComponent>())
	{
		// 肖像睜眼、無墨＝Closed/Mask 純補位（缺席以 Open 頂）
		Body->ApplyCustomAvatar(Open, Closed ? Closed : Open, Mask ? Mask : Open, Tone);
	}
	UTexture2D* Portrait = CaptureNow(Tone);
	if (Portrait)
	{
		Cache.Add(Key, Portrait);
		CachePixels.Add(Key, TPair<int32, TArray<FColor>>(LastSide, LastPixels));
	}
	// 生成當幀同樣過「資源就緒」閘（與快取命中同一條）：呼叫端下一幀再拿到的就是成品
	return (Portrait && !Portrait->IsDefaultTexture() && Portrait->GetResource()) ? Portrait : nullptr;
}

UTexture* ANiceInkPortraitBooth::GetPortraitRoster(int32 AvatarIdx)
{
	if (AvatarIdx < 0 || AvatarIdx >= FNiceInkAvatars::Num())
	{
		return nullptr;
	}
	const FString Key = FString::Printf(TEXT("roster%d"), AvatarIdx);
	if (const TObjectPtr<UTexture2D>* Found = Cache.Find(Key))
	{
		return *Found;
	}
	const FNiceInkAvatarDef& Def = FNiceInkAvatars::Get(AvatarIdx);
	UTexture2D* Open = nullptr;
	if (const TObjectPtr<UTexture2D>* Cached = RosterOpen.Find(AvatarIdx))
	{
		Open = *Cached;
	}
	else
	{
		Open = LoadObject<UTexture2D>(nullptr, *Def.FaceOpenPath);
		RosterOpen.Add(AvatarIdx, Open);
	}
	// 名冊臉是串流資產：載入當幀只有最小 mip 在顯存，拍下去＝糊的肖像進快取、永遠不會變清楚
	//（09-11 之前「名冊肖像糊」的真因）。烘之前把整套 mip 拉進來；還沒到＝這幀不出片、呼叫端空著等。
	if (Open)
	{
		Open->SetForceMipLevelsToBeResident(30.0f);
		if (Open->IsStreamable() && !Open->IsFullyStreamedIn())
		{
			return nullptr;
		}
	}
	return GetPortraitKeyed(Key, Open, nullptr, nullptr, Def.SkinTone);
}

// ============================================================================
// 校準儀器（不進出貨路徑）：主控台 `NiPortraitDump [子資料夾]` 把快取裡的肖像寫成 PNG
//（Saved/Portraits/<sub>/<key>.png），配 `NiDelayExec 35 NiPortraitDump x` 在 -ExecCmds 裡延遲執行。
// ============================================================================
int32 ANiceInkPortraitBooth::DumpCache(const FString& SubDir) const
{
	const FString Dir = FPaths::ProjectSavedDir() / TEXT("Portraits") / SubDir;
	IFileManager::Get().MakeDirectory(*Dir, true);
	int32 Written = 0;
	for (const TPair<FString, TPair<int32, TArray<FColor>>>& It : CachePixels)
	{
		const int32 Side = It.Value.Key;
		const TArray<FColor>& Px = It.Value.Value;
		if (Side <= 0 || Px.Num() != Side * Side) { continue; }
		FImageView View(Px.GetData(), Side, Side, EGammaSpace::sRGB);
		if (FImageUtils::SaveImageByExtension(*(Dir / (It.Key + TEXT(".png"))), View)) { ++Written; }
	}
	UE_LOG(LogTemp, Warning, TEXT("NiPortrait: dumped %d portraits to %s"), Written, *Dir);
	return Written;
}

static FAutoConsoleCommandWithWorldAndArgs GNiPortraitDumpCmd(
	TEXT("NiPortraitDump"),
	TEXT("Dump cached portraits to Saved/Portraits/<sub>/"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (ANiceInkPortraitBooth* Booth = ANiceInkPortraitBooth::Get(World))
		{
			Booth->DumpCache(Args.Num() > 0 ? Args[0] : TEXT("dump"));
		}
	}));

// 診斷：同一顆頭、只換投影（其餘＝出貨流程原樣），逐檔烘本機臉並寫 PNG（Saved/Portraits/proj_<名>/）。
// 焦距等效：ortho／135mm（距 220）／90mm（距 130）／50mm（距 75）／35mm（距 55）；FOV 依「臉面離相機 ≈ 距 −15」
// 把框寬固定在 ~60cm，讓 bbox 裁切後每一檔的頭大小可比。
static FAutoConsoleCommandWithWorld GNiPortraitProjSweepCmd(
	TEXT("NiPortraitProjSweep"),
	TEXT("Bake the local face under ortho + 4 perspective focal lengths and dump PNGs."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		ANiceInkPortraitBooth* Booth = ANiceInkPortraitBooth::Get(World);
		UNiceInkFaceShare* Share = UNiceInkFaceShare::Get(World);
		if (!Booth || !Share) { UE_LOG(LogTemp, Warning, TEXT("NiPortraitProjSweep: no booth/share")); return; }
		struct FPreset { const TCHAR* Name; float Fov; float Dist; };
		const FPreset Presets[] = {
			{ TEXT("proj_ortho"),  0.0f,  200.0f },
			{ TEXT("proj_135mm"), 16.7f,  220.0f },
			{ TEXT("proj_90mm"),  29.2f,  130.0f },
			{ TEXT("proj_50mm"),  53.0f,   75.0f },
			{ TEXT("proj_35mm"),  73.7f,   55.0f },
		};
		int32 Done = 0;
		for (const FPreset& P : Presets)
		{
			CVarNiPortraitFov->Set(P.Fov, ECVF_SetByConsole);
			CVarNiPortraitCamDist->Set(P.Dist, ECVF_SetByConsole);
			Booth->ClearCache();
			for (int32 Seat = 0; Seat < 6; ++Seat)
			{
				if (UTexture2D* Open = Share->GetOpen(Seat))
				{
					Booth->GetPortraitKeyed(FString::Printf(TEXT("seat%d"), Seat), Open,
						Share->GetClosed(Seat), Share->GetMask(Seat), Share->GetTone(Seat));
				}
			}
			Done += Booth->DumpCache(P.Name);
		}
		CVarNiPortraitFov->Set(-1.0f, ECVF_SetByConsole);      // 還給 UPROPERTY 出貨值
		CVarNiPortraitCamDist->Set(-1.0f, ECVF_SetByConsole);
		Booth->ClearCache();
		UE_LOG(LogTemp, Warning, TEXT("NiPortraitProjSweep: DONE %d portraits"), Done);
	}));
