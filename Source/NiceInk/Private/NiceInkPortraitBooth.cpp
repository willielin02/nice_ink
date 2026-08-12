#include "NiceInkPortraitBooth.h"

#include "ImageUtils.h"
#include "TextureResource.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InkBodyComponent.h"
#include "InkCanvasComponent.h"
#include "NiceInkCharacter.h"
#include "NiceInkTypes.h"

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

	// 亭燈：平坦無衰減點光＝fullbright 讀感的局部版；只走通道 2（不漏進世界）
	KeyLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("KeyLight"));
	KeyLight->SetupAttachment(Floor);
	KeyLight->SetCastShadows(false);
	KeyLight->SetAttenuationRadius(1200.0f);
	KeyLight->bUseInverseSquaredFalloff = false;
	KeyLight->LightFalloffExponent = 0.01f;
	KeyLight->SetLightingChannels(false, false, true);

	// 下前補光：下巴底/頸窩朝下的面（key 從上打不到、頭燈假光只顧朝鏡頭面）
	FillLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FillLight"));
	FillLight->SetupAttachment(Floor);
	FillLight->SetCastShadows(false);
	FillLight->SetAttenuationRadius(1200.0f);
	FillLight->bUseInverseSquaredFalloff = false;
	FillLight->LightFalloffExponent = 0.01f;
	FillLight->SetLightingChannels(false, false, true);

	// 背光：從主體後上方打輪廓（黑髮不溶進黑底）；同樣只走通道 2
	RimLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("RimLight"));
	RimLight->SetupAttachment(Floor);
	RimLight->SetCastShadows(false);
	RimLight->SetAttenuationRadius(1200.0f);
	RimLight->bUseInverseSquaredFalloff = false;
	RimLight->LightFalloffExponent = 0.01f;
	RimLight->SetLightingChannels(false, false, true);

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

	// 光照全隔離：替身所有面件只吃通道 2（世界的平行光/天光照不到＝
	// 肖像亮度跨關卡恆定；亭燈也只打通道 2＝不漏進世界）
	TInlineComponentArray<UPrimitiveComponent*> Prims(Dummy);
	for (UPrimitiveComponent* Prim : Prims)
	{
		Prim->SetLightingChannels(false, false, true);
	}
}

UTexture2D* ANiceInkPortraitBooth::CaptureNow()
{
	if (!Dummy)
	{
		return nullptr;
	}
	// 取景：明確旋鈕瞄準（actor 中心+AimZ＝頭高；骨骼查詢在診斷輪證實不穩）
	const FVector Aim = Dummy->GetActorLocation() + FVector(0, 0, AimZCm);
	const FVector CamPos = Aim + FVector(-200.0f, 0, 0); // 正交＝距離不影響構圖
	Capture->SetWorldLocationAndRotation(CamPos, (Aim - CamPos).Rotation());
	Capture->OrthoWidth = OrthoWidthCm;
	KeyLight->SetWorldLocation(Aim + FVector(-80.0f, 0, 40.0f)); // 相機側上方
	KeyLight->SetIntensity(KeyIntensity);
	FillLight->SetWorldLocation(Aim + FVector(-70.0f, 0, -55.0f)); // 相機側下方（打下巴底）
	FillLight->SetIntensity(FillIntensity);
	RimLight->SetWorldLocation(Aim + FVector(90.0f, 0, 70.0f)); // 主體後上方（臉朝 -X＝後方為 +X）
	RimLight->SetIntensity(RimIntensity);
	Capture->ShowOnlyActors.Reset();
	Capture->ShowOnlyActors.Add(Dummy);
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
	const bool bDepthOk = Res->ReadLinearColorPixels(DepthPx) && DepthPx.Num() == Pixels.Num();
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

	// 肩膀切除三代（血價註記：v1「寬過頭寬」被腮幫誤觸、v2「碰框」被臉旁
	// 的肩丘誤觸——相撲的肩與頭同高、列判準原理上分不開）。正解＝兩步：
	// ①中央帶遮罩：頭寬取自「未碰框的上段列」，帶外一律清透明＝臉旁肩丘消失；
	// ②帶內找頸窄點：寬度剖面=腮寬→頸縮→胸寬，谷底=頸＝解剖切點，其下裁掉。
	int32 EdgeY = MaxY + 1; // 第一個碰框列（無碰框＝框內無肩＝不切）
	for (int32 Y = MinY; Y <= MaxY; ++Y)
	{
		if ((1.0f - Pixels[Y * PortraitSize + 0].A > 0.1f) ||
			(1.0f - Pixels[Y * PortraitSize + 1].A > 0.1f) ||
			(1.0f - Pixels[Y * PortraitSize + (PortraitSize - 1)].A > 0.1f) ||
			(1.0f - Pixels[Y * PortraitSize + (PortraitSize - 2)].A > 0.1f))
		{
			EdgeY = Y;
			break;
		}
	}
	if (EdgeY > MinY && EdgeY <= MaxY)
	{
		// ①頭的中央帶（碰框列之前的水平界）＋帶外清透明（A=1＝空，inverse opacity）
		int32 HeadX0 = PortraitSize, HeadX1 = -1;
		for (int32 Y = MinY; Y < EdgeY; ++Y)
		{
			for (int32 X = 0; X < PortraitSize; ++X)
			{
				if (1.0f - Pixels[Y * PortraitSize + X].A > 0.1f)
				{
					HeadX0 = FMath::Min(HeadX0, X);
					HeadX1 = FMath::Max(HeadX1, X);
				}
			}
		}
		if (HeadX1 >= HeadX0)
		{
			HeadX0 = FMath::Max(0, HeadX0 - 2);
			HeadX1 = FMath::Min(PortraitSize - 1, HeadX1 + 2);
			for (int32 Y = 0; Y < PortraitSize; ++Y)
			{
				for (int32 X = 0; X < PortraitSize; ++X)
				{
					if (X < HeadX0 || X > HeadX1)
					{
						Pixels[Y * PortraitSize + X].A = 1.0f;
					}
				}
			}
			// ②帶內寬度剖面：腮峰（上段最寬列）→往下找頸谷（最窄列）→其下裁
			auto BandWidth = [&](int32 Y)
			{
				int32 W = 0;
				for (int32 X = HeadX0; X <= HeadX1; ++X)
				{
					if (1.0f - Pixels[Y * PortraitSize + X].A > 0.1f) { ++W; }
				}
				return W;
			};
			int32 CheekRow = MinY, CheekW = 0;
			const int32 CheekSearchEnd = FMath::Min(EdgeY + 10, MaxY);
			for (int32 Y = MinY; Y <= CheekSearchEnd; ++Y)
			{
				const int32 W = BandWidth(Y);
				if (W > CheekW) { CheekW = W; CheekRow = Y; }
			}
			int32 NeckRow = CheekRow, NeckW = CheekW;
			const int32 NeckSearchEnd = FMath::Min(CheekRow + 60, MaxY);
			for (int32 Y = CheekRow; Y <= NeckSearchEnd; ++Y)
			{
				const int32 W = BandWidth(Y);
				if (W < NeckW) { NeckW = W; NeckRow = Y; }
			}
			if (NeckRow > CheekRow)
			{
				MaxY = NeckRow;
			}

			// ③每列只留「含中線的連續段」：下顎兩側殘存的肩楔與頭輪廓之間
			// 有背景縫＝離散段，清掉（頭每列必是跨中線的單一連續段）
			const int32 MidX = (HeadX0 + HeadX1) / 2;
			for (int32 Y = MinY; Y <= MaxY; ++Y)
			{
				int32 RunL = MidX, RunR = MidX;
				const bool bMidSolid = 1.0f - Pixels[Y * PortraitSize + MidX].A > 0.1f;
				if (bMidSolid)
				{
					while (RunL > HeadX0 && 1.0f - Pixels[Y * PortraitSize + RunL - 1].A > 0.1f) { --RunL; }
					while (RunR < HeadX1 && 1.0f - Pixels[Y * PortraitSize + RunR + 1].A > 0.1f) { ++RunR; }
				}
				for (int32 X = HeadX0; X <= HeadX1; ++X)
				{
					if (!bMidSolid || X < RunL || X > RunR)
					{
						Pixels[Y * PortraitSize + X].A = 1.0f;
					}
				}
			}
		}
	}

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

	// 方形裁切：以頭界的長邊為邊、置中、加 4% 邊距（成品仍方形貼圖＝
	// 消費端零改動；頭以外全透明＝畫出來就是頭形）
	const int32 BW = MaxX - MinX + 1, BH = MaxY - MinY + 1;
	const int32 Side = FMath::Min(PortraitSize, FMath::Max(BW, BH) + FMath::Max(BW, BH) / 12);
	int32 CX = (MinX + MaxX) / 2, CY = (MinY + MaxY) / 2;
	int32 X0 = FMath::Clamp(CX - Side / 2, 0, PortraitSize - Side);
	int32 Y0 = FMath::Clamp(CY - Side / 2, 0, PortraitSize - Side);

	TArray<FColor> Out;
	Out.SetNumUninitialized(Side * Side);
	for (int32 Y = 0; Y < Side; ++Y)
	{
		const int32 SrcY = Y0 + Y;
		// 方形窗可能蓋到肩線以下——界外列 alpha 歸零（肩膀不得復活）
		const bool bRowKept = SrcY >= MinY && SrcY <= MaxY;
		for (int32 X = 0; X < Side; ++X)
		{
			FLinearColor C = Pixels[SrcY * PortraitSize + (X0 + X)];
			const float A = bRowKept ? FMath::Clamp(1.0f - C.A, 0.0f, 1.0f) : 0.0f;
			C *= ColorGain; // HDR→sRGB 的手動曝光
			// 高光軟膝蓋（量測定罪：鼻樑熱斑 R 撞頂 6.3%、p50=189 全臉正常——
			// 再壓 gain=整臉拖暗；只把膝蓋 0.8 以上 Reinhard 壓進 [0.8,1) 零撞頂）
			const float Knee = 0.8f;
			auto SoftKnee = [Knee](float V)
			{
				if (V <= Knee) return V;
				const float E = (V - Knee) / (1.0f - Knee);
				return Knee + (1.0f - Knee) * (E / (1.0f + E));
			};
			C.R = SoftKnee(C.R); C.G = SoftKnee(C.G); C.B = SoftKnee(C.B);
			C.A = A;
			Out[Y * Side + X] = C.ToFColor(/*bSRGB=*/true);
		}
	}

	FCreateTexture2DParameters Params;
	Params.bUseAlpha = true;
	Params.bSRGB = true;
	return FImageUtils::CreateTexture2D(Side, Side, Out, this, FString(), RF_NoFlags, Params);
}

UTexture* ANiceInkPortraitBooth::GetPortraitKeyed(const FString& Key, UTexture2D* Open,
	UTexture2D* Closed, UTexture2D* Mask, const FLinearColor& Tone)
{
	if (const TObjectPtr<UTexture2D>* Found = Cache.Find(Key))
	{
		return *Found;
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
	if (UInkBodyComponent* Body = Dummy->FindComponentByClass<UInkBodyComponent>())
	{
		// 肖像睜眼、無墨＝Closed/Mask 純補位（缺席以 Open 頂）
		Body->ApplyCustomAvatar(Open, Closed ? Closed : Open, Mask ? Mask : Open, Tone);
	}
	UTexture2D* Portrait = CaptureNow();
	if (Portrait)
	{
		Cache.Add(Key, Portrait);
	}
	return Portrait;
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
	return GetPortraitKeyed(Key, Open, nullptr, nullptr, Def.SkinTone);
}
