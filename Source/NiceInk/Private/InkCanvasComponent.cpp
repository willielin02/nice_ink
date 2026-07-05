#include "InkCanvasComponent.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "TextureResource.h"

namespace
{
	constexpr int32 NibTextureSize = 64;
	// stamp 間距（相對筆寬半徑）；0.45 在快速揮動時仍是連續實線
	constexpr float StampSpacingFactor = 0.45f;
}

UInkCanvasComponent::UInkCanvasComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UInkCanvasComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!MarkerRT)
	{
		MarkerRT = CreateLayerRT(TEXT("InkMarkerRT"));
	}
	if (!TattooRT)
	{
		TattooRT = CreateLayerRT(TEXT("InkTattooRT"));
	}
	RebuildRenderTargets();
}

UTextureRenderTarget2D* UInkCanvasComponent::CreateLayerRT(const TCHAR* DebugName)
{
	UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(this, DebugName);
	RT->RenderTargetFormat = RTF_RGBA8;
	RT->ClearColor = FLinearColor::Transparent;
	RT->AddressX = TA_Clamp;
	RT->AddressY = TA_Clamp;
	RT->bAutoGenerateMips = false;
	RT->InitAutoFormat(RenderTargetResolution, RenderTargetResolution);
	RT->UpdateResourceImmediate(true);
	UKismetRenderingLibrary::ClearRenderTarget2D(this, RT, FLinearColor::Transparent);
	return RT;
}

UTexture2D* UInkCanvasComponent::GetOrCreateNibTexture()
{
	if (NibTexture)
	{
		return NibTexture;
	}

	NibTexture = UTexture2D::CreateTransient(NibTextureSize, NibTextureSize, PF_B8G8R8A8, TEXT("InkMarkerNib"));
	NibTexture->SRGB = false;
	NibTexture->Filter = TF_Bilinear;
	NibTexture->NeverStream = true;

	FTexture2DMipMap& Mip = NibTexture->GetPlatformData()->Mips[0];
	FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));

	const float Center = (NibTextureSize - 1) * 0.5f;
	const float CoreRadius = NibTextureSize * 0.5f - 3.0f;
	const float FalloffWidth = 2.0f; // 抗鋸齒緣，麥克筆＝硬邊圓頭

	for (int32 Y = 0; Y < NibTextureSize; ++Y)
	{
		for (int32 X = 0; X < NibTextureSize; ++X)
		{
			const float Dist = FMath::Sqrt(FMath::Square(X - Center) + FMath::Square(Y - Center));
			const float Alpha01 = FMath::Clamp((CoreRadius + FalloffWidth - Dist) / FalloffWidth, 0.0f, 1.0f);
			const uint8 Alpha = static_cast<uint8>(FMath::RoundToInt(Alpha01 * 255.0f));
			// 預乘 alpha（白×A）：搭配 SE_BLEND_AlphaComposite，讓 RT 的 alpha 通道
			// 也正確累積（SE_BLEND_Translucent 不寫目的地 alpha，墨水遮罩會全空）
			Pixels[Y * NibTextureSize + X] = FColor(Alpha, Alpha, Alpha, Alpha);
		}
	}

	Mip.BulkData.Unlock();
	NibTexture->UpdateResource();
	return NibTexture;
}

// --- 作畫 ---

void UInkCanvasComponent::BeginStroke(int32 AuthorId, FLinearColor Color, FVector2D UV)
{
	if (AuthorId == INDEX_NONE)
	{
		return;
	}

	EndStroke(AuthorId);

	UV = ClampUV(UV);
	Color.A = 1.0f; // 麥克筆＝不透明墨水

	FInkWork& Work = GetOrCreateActiveWork(AuthorId);

	FInkStroke Stroke;
	Stroke.Color = Color;
	Stroke.Points.Add(UV);
	Stroke.StartTimestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	Work.Strokes.Add(Stroke);

	OpenStrokeWorkByAuthor.Add(AuthorId, Work.WorkId);
	LastPointByAuthor.Add(AuthorId, UV);

	StampSegmentIntoMarkerRT(UV, UV, Color, /*bDotOnly=*/true);
	OnCanvasChanged.Broadcast();
}

void UInkCanvasComponent::AddStrokePoint(int32 AuthorId, FVector2D UV)
{
	const int32* OpenWorkId = OpenStrokeWorkByAuthor.Find(AuthorId);
	if (!OpenWorkId)
	{
		return;
	}

	FInkWork* Work = FindWork(*OpenWorkId);
	if (!Work || Work->Strokes.IsEmpty())
	{
		EndStroke(AuthorId);
		return;
	}

	UV = ClampUV(UV);
	const FVector2D LastUV = LastPointByAuthor.FindChecked(AuthorId);
	if (UV.Equals(LastUV, KINDA_SMALL_NUMBER))
	{
		return;
	}

	FInkStroke& Stroke = Work->Strokes.Last();
	Stroke.Points.Add(UV);
	LastPointByAuthor.Add(AuthorId, UV);

	StampSegmentIntoMarkerRT(LastUV, UV, Stroke.Color, /*bDotOnly=*/false);
}

void UInkCanvasComponent::EndStroke(int32 AuthorId)
{
	OpenStrokeWorkByAuthor.Remove(AuthorId);
	LastPointByAuthor.Remove(AuthorId);
}

// --- 規則操作 ---

bool UInkCanvasComponent::GetWork(int32 WorkId, FInkWork& OutWork) const
{
	for (const FInkWork& Work : Works)
	{
		if (Work.WorkId == WorkId)
		{
			OutWork = Work;
			return true;
		}
	}
	return false;
}

TArray<int32> UInkCanvasComponent::GetWorkIdsByState(EInkWorkState State) const
{
	TArray<int32> Result;
	for (const FInkWork& Work : Works)
	{
		if (Work.State == State)
		{
			Result.Add(Work.WorkId);
		}
	}
	return Result;
}

int32 UInkCanvasComponent::GetActiveWorkId(int32 AuthorId) const
{
	for (const FInkWork& Work : Works)
	{
		if (Work.AuthorId == AuthorId && Work.RoundIndex == RoundIndex && Work.State == EInkWorkState::Marker)
		{
			return Work.WorkId;
		}
	}
	return INDEX_NONE;
}

bool UInkCanvasComponent::ConvertWorkToCarbon(int32 WorkId)
{
	FInkWork* Work = FindWork(WorkId);
	if (!Work || Work->State != EInkWorkState::Marker || Work->Strokes.IsEmpty())
	{
		return false;
	}

	// 關閉這幅上任何進行中的筆劃
	for (auto It = OpenStrokeWorkByAuthor.CreateIterator(); It; ++It)
	{
		if (It.Value() == WorkId)
		{
			LastPointByAuthor.Remove(It.Key());
			It.RemoveCurrent();
		}
	}

	Work->State = EInkWorkState::Carbon;
	Work->LaserLevel = 0;
	RebuildRenderTargets();
	return true;
}

bool UInkCanvasComponent::LockWorkPermanent(int32 WorkId)
{
	FInkWork* Work = FindWork(WorkId);
	if (!Work || Work->State != EInkWorkState::Carbon)
	{
		return false;
	}

	// 淡化級凍結：已淡化的鎖在淡化態
	Work->State = EInkWorkState::Permanent;
	OnCanvasChanged.Broadcast();
	return true;
}

bool UInkCanvasComponent::ApplyLaserToWork(int32 WorkId)
{
	FInkWork* Work = FindWork(WorkId);
	if (!Work || Work->State != EInkWorkState::Carbon)
	{
		return false;
	}

	++Work->LaserLevel;
	if (Work->LaserLevel >= 3)
	{
		Works.RemoveAll([WorkId](const FInkWork& W) { return W.WorkId == WorkId; });
	}
	RebuildRenderTargets();
	return true;
}

void UInkCanvasComponent::WashAllMarker()
{
	OpenStrokeWorkByAuthor.Empty();
	LastPointByAuthor.Empty();
	Works.RemoveAll([](const FInkWork& W) { return W.State == EInkWorkState::Marker; });
	RebuildRenderTargets();
}

void UInkCanvasComponent::AddEvidenceMark(EInkEvidenceType Type, FVector2D UV, int32 Seed)
{
	UV = ClampUV(UV);
	FRandomStream Rand(Seed);

	FLinearColor Color;
	int32 DotCount;
	float ScatterRadius;
	switch (Type)
	{
	case EInkEvidenceType::Sneeze:
		Color = FLinearColor(0.55f, 0.68f, 0.35f); DotCount = 14; ScatterRadius = 0.045f; break;
	case EInkEvidenceType::Piss:
		Color = FLinearColor(0.85f, 0.72f, 0.12f); DotCount = 16; ScatterRadius = 0.05f; break;
	case EInkEvidenceType::Shit:
		Color = FLinearColor(0.27f, 0.15f, 0.05f); DotCount = 18; ScatterRadius = 0.05f; break;
	default: // Bruise
		Color = FLinearColor(0.28f, 0.12f, 0.38f); DotCount = 10; ScatterRadius = 0.02f; break;
	}

	const int32 AuthorId = InkEvidence::AuthorIdFor(Type);
	FInkWork& Work = GetOrCreateActiveWork(AuthorId);

	// 濺射：中心一點＋周圍隨機散點；每點一筆（單點筆劃＝純圓點，不連線）
	for (int32 Dot = 0; Dot < DotCount; ++Dot)
	{
		const float Angle = Rand.FRandRange(0.0f, 2.0f * PI);
		const float Dist = Dot == 0 ? 0.0f : ScatterRadius * FMath::Sqrt(Rand.FRand());
		FInkStroke Stroke;
		Stroke.Color = Color;
		Stroke.Points.Add(ClampUV(UV + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Dist));
		Stroke.StartTimestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
		Work.Strokes.Add(Stroke);
		StampSegmentIntoMarkerRT(Stroke.Points[0], Stroke.Points[0], Color, /*bDotOnly=*/true);
	}

	OnCanvasChanged.Broadcast();
}

void UInkCanvasComponent::SetRoundIndex(int32 NewRoundIndex)
{
	RoundIndex = FMath::Max(0, NewRoundIndex);
}

// --- 渲染 ---

void UInkCanvasComponent::RebuildRenderTargets()
{
	if (!MarkerRT || !TattooRT)
	{
		return;
	}

	UKismetRenderingLibrary::ClearRenderTarget2D(this, MarkerRT, FLinearColor::Transparent);
	UKismetRenderingLibrary::ClearRenderTarget2D(this, TattooRT, FLinearColor::Transparent);

	// 麥克筆層：玩家原色、全不透明，直接 stamp
	{
		UCanvas* Canvas = nullptr;
		FVector2D CanvasSize = FVector2D::ZeroVector;
		FDrawToRenderTargetContext Context;
		UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, MarkerRT, Canvas, CanvasSize, Context);
		if (Canvas)
		{
			for (const FInkWork& Work : Works)
			{
				if (Work.State == EInkWorkState::Marker)
				{
					DrawWorkStrokes(Canvas, CanvasSize, Work, FLinearColor::White, /*bUseOverrideColor=*/false);
				}
			}
		}
		UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
	}

	// 刺青層：碳黑墨色。未淡化的直接 stamp；
	// 淡化的先在 ScratchRT 以滿透明度畫完，再整張以工作透明度合成
	//（半透明 stamp 直接重疊會產生堆疊條紋）。
	{
		UCanvas* Canvas = nullptr;
		FVector2D CanvasSize = FVector2D::ZeroVector;
		FDrawToRenderTargetContext Context;
		UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, TattooRT, Canvas, CanvasSize, Context);
		if (Canvas)
		{
			for (const FInkWork& Work : Works)
			{
				if (Work.State != EInkWorkState::Marker && Work.LaserLevel == 0)
				{
					DrawWorkStrokes(Canvas, CanvasSize, Work, CarbonInkColor, /*bUseOverrideColor=*/true);
				}
			}
		}
		UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
	}

	for (const FInkWork& Work : Works)
	{
		if (Work.State == EInkWorkState::Marker || Work.LaserLevel == 0)
		{
			continue;
		}

		const float Opacity = LaserOpacity(Work.LaserLevel);
		if (Opacity <= 0.0f)
		{
			continue;
		}

		if (!ScratchRT)
		{
			ScratchRT = CreateLayerRT(TEXT("InkScratchRT"));
		}
		UKismetRenderingLibrary::ClearRenderTarget2D(this, ScratchRT, FLinearColor::Transparent);

		{
			UCanvas* Canvas = nullptr;
			FVector2D CanvasSize = FVector2D::ZeroVector;
			FDrawToRenderTargetContext Context;
			UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, ScratchRT, Canvas, CanvasSize, Context);
			if (Canvas)
			{
				DrawWorkStrokes(Canvas, CanvasSize, Work, CarbonInkColor, /*bUseOverrideColor=*/true);
			}
			UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
		}

		{
			UCanvas* Canvas = nullptr;
			FVector2D CanvasSize = FVector2D::ZeroVector;
			FDrawToRenderTargetContext Context;
			UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, TattooRT, Canvas, CanvasSize, Context);
			if (Canvas)
			{
				// Scratch 內容是預乘的：所有通道統一乘上 Opacity 即為均勻淡化，
				// 再以 AlphaComposite 疊進刺青層
				FCanvasTileItem CompositeItem(
					FVector2D::ZeroVector,
					ScratchRT->GetResource(),
					CanvasSize,
					FLinearColor(Opacity, Opacity, Opacity, Opacity));
				CompositeItem.BlendMode = SE_BLEND_AlphaComposite;
				Canvas->DrawItem(CompositeItem);
			}
			UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
		}
	}

	OnCanvasChanged.Broadcast();
}

void UInkCanvasComponent::DrawWorkStrokes(UCanvas* Canvas, const FVector2D& CanvasSize, const FInkWork& Work, const FLinearColor& OverrideColor, bool bUseOverrideColor) const
{
	for (const FInkStroke& Stroke : Work.Strokes)
	{
		FLinearColor Color = bUseOverrideColor ? OverrideColor : Stroke.Color;
		Color.A = 1.0f;
		StampPolyline(Canvas, CanvasSize, Stroke.Points, Color);
	}
}

void UInkCanvasComponent::StampPolyline(UCanvas* Canvas, const FVector2D& CanvasSize, const TArray<FVector2D>& Points, const FLinearColor& Color) const
{
	if (Points.IsEmpty())
	{
		return;
	}

	StampDot(Canvas, CanvasSize, Points[0], Color);
	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		StampSegment(Canvas, CanvasSize, Points[Index - 1], Points[Index], Color);
	}
}

void UInkCanvasComponent::StampSegment(UCanvas* Canvas, const FVector2D& CanvasSize, const FVector2D& From, const FVector2D& To, const FLinearColor& Color) const
{
	const float Distance = FVector2D::Distance(From, To);

	// 跨 UV 島跳躍：不內插（內插會在圖集上拉出垃圾長線），只在落點蓋章
	if (Distance > MaxUvSegmentLength)
	{
		StampDot(Canvas, CanvasSize, To, Color);
		return;
	}

	const float StepSize = FMath::Max(MarkerUvRadius * StampSpacingFactor, 0.0005f);
	const int32 StepCount = FMath::Clamp(FMath::CeilToInt(Distance / StepSize), 1, 256);
	for (int32 Step = 1; Step <= StepCount; ++Step)
	{
		const float Alpha = static_cast<float>(Step) / static_cast<float>(StepCount);
		StampDot(Canvas, CanvasSize, FMath::Lerp(From, To, Alpha), Color);
	}
}

void UInkCanvasComponent::StampDot(UCanvas* Canvas, const FVector2D& CanvasSize, const FVector2D& UV, const FLinearColor& Color) const
{
	UTexture2D* Nib = const_cast<UInkCanvasComponent*>(this)->GetOrCreateNibTexture();
	if (!Nib || !Nib->GetResource())
	{
		return;
	}

	const float Diameter = FMath::Max(2.0f, MarkerUvRadius * 2.0f * CanvasSize.X);
	const FVector2D TopLeft(UV.X * CanvasSize.X - Diameter * 0.5f, UV.Y * CanvasSize.Y - Diameter * 0.5f);

	FCanvasTileItem TileItem(TopLeft, Nib->GetResource(), FVector2D(Diameter, Diameter), Color);
	TileItem.BlendMode = SE_BLEND_AlphaComposite; // premultiplied over：RGB 與 alpha 皆累積
	Canvas->DrawItem(TileItem);
}

void UInkCanvasComponent::StampSegmentIntoMarkerRT(const FVector2D& From, const FVector2D& To, const FLinearColor& Color, bool bDotOnly)
{
	if (!MarkerRT)
	{
		return;
	}

	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize = FVector2D::ZeroVector;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, MarkerRT, Canvas, CanvasSize, Context);
	if (Canvas)
	{
		if (bDotOnly)
		{
			StampDot(Canvas, CanvasSize, To, Color);
		}
		else
		{
			StampSegment(Canvas, CanvasSize, From, To, Color);
		}
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
}

// --- 內部 ---

FInkWork* UInkCanvasComponent::FindWork(int32 WorkId)
{
	for (FInkWork& Work : Works)
	{
		if (Work.WorkId == WorkId)
		{
			return &Work;
		}
	}
	return nullptr;
}

FInkWork& UInkCanvasComponent::GetOrCreateActiveWork(int32 AuthorId)
{
	for (FInkWork& Work : Works)
	{
		if (Work.AuthorId == AuthorId && Work.RoundIndex == RoundIndex && Work.State == EInkWorkState::Marker)
		{
			return Work;
		}
	}

	FInkWork NewWork;
	NewWork.WorkId = NextWorkId++;
	NewWork.AuthorId = AuthorId;
	NewWork.RoundIndex = RoundIndex;
	NewWork.State = EInkWorkState::Marker;
	return Works[Works.Add(NewWork)];
}

float UInkCanvasComponent::LaserOpacity(int32 LaserLevel)
{
	switch (LaserLevel)
	{
	case 0: return 1.0f;
	case 1: return 0.62f;
	case 2: return 0.30f;
	default: return 0.0f;
	}
}

FVector2D UInkCanvasComponent::ClampUV(FVector2D UV)
{
	return FVector2D(FMath::Clamp(UV.X, 0.0f, 1.0f), FMath::Clamp(UV.Y, 0.0f, 1.0f));
}

bool UInkCanvasComponent::ExportLayersToPng(const FString& AbsolutePathPrefix) const
{
	const bool bMarkerSaved = SaveRTToPng(MarkerRT, AbsolutePathPrefix + TEXT("_marker.png"));
	const bool bTattooSaved = SaveRTToPng(TattooRT, AbsolutePathPrefix + TEXT("_tattoo.png"));
	return bMarkerSaved && bTattooSaved;
}

bool UInkCanvasComponent::SaveRTToPng(UTextureRenderTarget2D* RT, const FString& AbsoluteFilePath)
{
	if (!RT || AbsoluteFilePath.IsEmpty())
	{
		return false;
	}

	FTextureRenderTargetResource* Resource = RT->GameThread_GetRenderTargetResource();
	if (!Resource)
	{
		return false;
	}

	TArray<FColor> Pixels;
	if (!Resource->ReadPixels(Pixels) || Pixels.IsEmpty())
	{
		return false;
	}

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(AbsoluteFilePath), true);

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
	if (!ImageWrapper.IsValid())
	{
		return false;
	}

	ImageWrapper->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), RT->SizeX, RT->SizeY, ERGBFormat::BGRA, 8);
	const TArray64<uint8>& CompressedData = ImageWrapper->GetCompressed(90);

	TArray<uint8> SaveData;
	SaveData.Append(CompressedData.GetData(), CompressedData.Num());
	return FFileHelper::SaveArrayToFile(SaveData, *AbsoluteFilePath);
}
