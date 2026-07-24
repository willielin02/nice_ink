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
#include "NiceInkAudio.h"
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
		MarkerRT = CreateLayerRT(TEXT("InkMarkerRT"), RenderTargetResolution);
	}
	if (!TattooRT)
	{
		TattooRT = CreateLayerRT(TEXT("InkTattooRT"), RenderTargetResolution);
	}
	if (!MistRT)
	{
		MistRT = CreateLayerRT(TEXT("InkMistRT"), MistRenderTargetResolution);
	}
	RebuildRenderTargets();
}

UTextureRenderTarget2D* UInkCanvasComponent::CreateLayerRT(const TCHAR* DebugName, int32 Resolution)
{
	UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(this, DebugName);
	RT->RenderTargetFormat = RTF_RGBA8;
	RT->ClearColor = FLinearColor::Transparent;
	RT->AddressX = TA_Clamp;
	RT->AddressY = TA_Clamp;
	RT->bAutoGenerateMips = false;
	RT->InitAutoFormat(Resolution, Resolution);
	RT->UpdateResourceImmediate(true);
	UKismetRenderingLibrary::ClearRenderTarget2D(this, RT, FLinearColor::Transparent);
	return RT;
}

UTexture2D* UInkCanvasComponent::GetOrCreateSoftNibTexture()
{
	if (SoftNibTexture)
	{
		return SoftNibTexture;
	}

	constexpr int32 Size = 128;
	SoftNibTexture = UTexture2D::CreateTransient(Size, Size, PF_B8G8R8A8, TEXT("InkSoftNib"));
	SoftNibTexture->SRGB = false;
	SoftNibTexture->Filter = TF_Bilinear;
	SoftNibTexture->NeverStream = true;

	FTexture2DMipMap& Mip = SoftNibTexture->GetPlatformData()->Mips[0];
	FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	const float Center = (Size - 1) * 0.5f;
	const float Radius = Size * 0.5f - 1.0f;
	for (int32 Y = 0; Y < Size; ++Y)
	{
		for (int32 X = 0; X < Size; ++X)
		{
			const float R01 = FMath::Min(FMath::Sqrt(
				FMath::Square(X - Center) + FMath::Square(Y - Center)) / Radius, 1.0f);
			// 余弦鐘形衰減到零＝airbrush 軟霧；^1.2 微收核心防中心過亮
			const float Shape = FMath::Pow(0.5f + 0.5f * FMath::Cos(PI * R01), 1.2f);
			const uint8 A = static_cast<uint8>(FMath::RoundToInt(Shape * 255.0f));
			Pixels[Y * Size + X] = FColor(A, A, A, A); // 預乘（白×A）
		}
	}
	Mip.BulkData.Unlock();
	SoftNibTexture->UpdateResource();
	return SoftNibTexture;
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

UTexture2D* UInkCanvasComponent::GetOrCreateStippleRowTexture()
{
	if (StippleRowTexture)
	{
		return StippleRowTexture;
	}

	// 條帶：X=橫越排帶（256px=帶寬 3cm）、Y=沿行進方向；8 個抖動變體垂直堆疊
	//（每變體 32px 高）。每變體=20 顆軟點、單點透明度沿排弧形 30%→5%（接觸壓力
	// 剖面=羽化）、槽內抖動防網格。premult 白×A；stamp 時 tile color=墨色。
	// 旋鈕（ShaderRowDotCount/StippleAlpha*）改了要重啟 session 才重烘（Transient）。
	constexpr int32 W = 256;
	constexpr int32 VariantH = 32;
	constexpr int32 Variants = 8;
	constexpr int32 H = VariantH * Variants;
	StippleRowTexture = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8, TEXT("InkStippleRow"));
	StippleRowTexture->SRGB = false;
	StippleRowTexture->Filter = TF_Bilinear;
	StippleRowTexture->NeverStream = true;

	FTexture2DMipMap& Mip = StippleRowTexture->GetPlatformData()->Mips[0];
	FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	FMemory::Memzero(Pixels, W * H * sizeof(FColor));

	TArray<float> Accum; // 每變體的浮點 alpha 累積（premult over）
	Accum.SetNumZeroed(W * VariantH);
	const int32 K = FMath::Clamp(ShaderRowDotCount, 4, 40);
	// 點半徑：條帶 X 全寬=帶寬 ⇒ px/UV 比 = W/(2*HalfWidth)
	const float DotRadPx = FMath::Max(2.0f, ShaderStippleUvRadius * (W / (2.0f * ShaderRowHalfWidthUv)));
	const float SlotWPx = static_cast<float>(W) / K;

	for (int32 V = 0; V < Variants; ++V)
	{
		FMemory::Memzero(Accum.GetData(), Accum.Num() * sizeof(float));
		uint32 Seed = 0x9E3779B9u * (V + 1);
		auto NextRand = [&Seed]() -> float
		{
			Seed = Seed * 1664525u + 1013904223u;
			return static_cast<float>(Seed >> 8) / 16777216.0f;
		};
		for (int32 i = 0; i < K; ++i)
		{
			const float T = ((i + 0.5f) / K) * 2.0f - 1.0f;
			const float Arc = FMath::Sqrt(FMath::Max(1.0f - T * T, 0.0f));
			const float DotA = FMath::Lerp(ShaderStippleAlphaEdge, ShaderStippleAlphaCenter, Arc);
			const float Cx = (T * 0.5f + 0.5f) * W + (NextRand() - 0.5f) * SlotWPx;
			const float Cy = VariantH * 0.5f + (NextRand() - 0.5f) * SlotWPx; // 縱向抖動同幅
			const int32 X0 = FMath::Max(0, FMath::FloorToInt(Cx - DotRadPx - 1));
			const int32 X1 = FMath::Min(W - 1, FMath::CeilToInt(Cx + DotRadPx + 1));
			const int32 Y0 = FMath::Max(0, FMath::FloorToInt(Cy - DotRadPx - 1));
			const int32 Y1 = FMath::Min(VariantH - 1, FMath::CeilToInt(Cy + DotRadPx + 1));
			for (int32 Y = Y0; Y <= Y1; ++Y)
			{
				for (int32 X = X0; X <= X1; ++X)
				{
					const float R01 = FMath::Sqrt(FMath::Square(X - Cx) + FMath::Square(Y - Cy)) / DotRadPx;
					if (R01 >= 1.0f)
					{
						continue;
					}
					const float Shape = 0.5f + 0.5f * FMath::Cos(PI * R01); // 軟點（皮下暈開）
					float& Dst = Accum[Y * W + X];
					const float Src = DotA * Shape;
					Dst = Src + Dst * (1.0f - Src); // premult over（點相疊自然變深）
				}
			}
		}
		for (int32 Y = 0; Y < VariantH; ++Y)
		{
			for (int32 X = 0; X < W; ++X)
			{
				const uint8 A = static_cast<uint8>(FMath::RoundToInt(
					FMath::Clamp(Accum[Y * W + X], 0.0f, 1.0f) * 255.0f));
				Pixels[(V * VariantH + Y) * W + X] = FColor(A, A, A, A); // 預乘白×A
			}
		}
	}

	Mip.BulkData.Unlock();
	StippleRowTexture->UpdateResource();
	return StippleRowTexture;
}

// --- 作畫 ---

void UInkCanvasComponent::BeginStroke(int32 AuthorId, FLinearColor Color, FVector2D UV, bool bDotStroke,
	EInkNeedle Needle)
{
	if (AuthorId == INDEX_NONE)
	{
		return;
	}

	EndStroke(AuthorId);

	// 落筆聲（每客戶端本地重放時各自播；閉眼沉睡者在音效層被全域靜音）
	NiAudio::Play(this, ENiSound::StrokeStart, 0.7f);

	UV = ClampUV(UV);
	Color.A = 1.0f; // 麥克筆＝不透明墨水

	FInkWork& Work = GetOrCreateActiveWork(AuthorId);

	FInkStroke Stroke;
	Stroke.Color = Color;
	Stroke.Points.Add(UV);
	Stroke.StartTimestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	Stroke.bDotStroke = bDotStroke;
	Stroke.NeedleType = Needle;
	Work.Strokes.Add(Stroke);

	OpenStrokeWorkByAuthor.Add(AuthorId, Work.WorkId);
	LastPointByAuthor.Add(AuthorId, UV);
	LastRowDirByAuthor.Remove(AuthorId); // 筆劃首點=尚無排向

	StampIntoLayerRT(UV, UV, Color, /*bDotOnly=*/true, Needle, nullptr);
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

	if (Stroke.bDotStroke)
	{
		// 點刺：每點一針、點間永不內插。排向=相鄰兩點走向（跨縫大跳沿用上一個
		// 有效方向）——live 增量與重放從同一份點序列得到同一排。
		const FVector2D Delta = UV - LastUV;
		const float DeltaLen = Delta.Size();
		if (DeltaLen > KINDA_SMALL_NUMBER && DeltaLen <= MaxUvSegmentLength)
		{
			LastRowDirByAuthor.Add(AuthorId, Delta / DeltaLen);
		}
		const FVector2D* RowDir = LastRowDirByAuthor.Find(AuthorId);
		StampIntoLayerRT(UV, UV, Stroke.Color, /*bDotOnly=*/true, Stroke.NeedleType, RowDir);
		if (Stroke.NeedleType == EInkNeedle::Liner)
		{
			// 節拍聲只給液線針（霧針高頻沉積的逐針聲=機關槍噪音）
			NiAudio::Play(this, ENiSound::StrokeStart, 0.22f);
		}
	}
	else
	{
		StampIntoLayerRT(LastUV, UV, Stroke.Color, /*bDotOnly=*/false, Stroke.NeedleType);
	}
}

void UInkCanvasComponent::EndStroke(int32 AuthorId)
{
	OpenStrokeWorkByAuthor.Remove(AuthorId);
	LastPointByAuthor.Remove(AuthorId);
	LastRowDirByAuthor.Remove(AuthorId);
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

bool UInkCanvasComponent::GetLastPointForAuthor(int32 AuthorId, FVector2D& OutUV) const
{
	if (const FVector2D* Last = LastPointByAuthor.Find(AuthorId))
	{
		OutUV = *Last;
		return true;
	}
	return false;
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

	// 刺青上身的重量（Resolution 演出全員在看；各端本地播）
	NiAudio::Play(this, ENiSound::CarbonStamp);

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

void UInkCanvasComponent::RestoreWork(const FInkWork& Work)
{
	if (Work.WorkId == INDEX_NONE || FindWork(Work.WorkId))
	{
		return;
	}
	Works.Add(Work);
	NextWorkId = FMath::Max(NextWorkId, Work.WorkId + 1);
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
		StampIntoLayerRT(Stroke.Points[0], Stroke.Points[0], Color, /*bDotOnly=*/true,
			EInkNeedle::Liner); // 證據標記＝液線針寬（雙針制不影響證據語義）
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
	if (MistRT)
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, MistRT, FLinearColor::Transparent);
	}

	// 麥克筆線層：玩家原色、全不透明，直接 stamp（只收 Liner——銳化只咬線）
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
					DrawWorkStrokes(Canvas, CanvasSize, Work, FLinearColor::White, /*bUseOverrideColor=*/false, ENeedleFilter::LinerOnly);
				}
			}
		}
		UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
	}

	// 霧層（07-23 十版）：Shader 筆劃的軟霧重播——獨立層、銳化不咬
	if (MistRT)
	{
		UCanvas* Canvas = nullptr;
		FVector2D CanvasSize = FVector2D::ZeroVector;
		FDrawToRenderTargetContext Context;
		UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, MistRT, Canvas, CanvasSize, Context);
		if (Canvas)
		{
			for (const FInkWork& Work : Works)
			{
				if (Work.State == EInkWorkState::Marker)
				{
					DrawWorkStrokes(Canvas, CanvasSize, Work, FLinearColor::White, /*bUseOverrideColor=*/false, ENeedleFilter::ShaderOnly);
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
					// 刺青層一趟全畫：碳黑的霧筆劃=軟黑填色（刺青層無銳化、軟霧成立）
					DrawWorkStrokes(Canvas, CanvasSize, Work, CarbonInkColor, /*bUseOverrideColor=*/true, ENeedleFilter::All);
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
			ScratchRT = CreateLayerRT(TEXT("InkScratchRT"), RenderTargetResolution);
		}
		UKismetRenderingLibrary::ClearRenderTarget2D(this, ScratchRT, FLinearColor::Transparent);

		{
			UCanvas* Canvas = nullptr;
			FVector2D CanvasSize = FVector2D::ZeroVector;
			FDrawToRenderTargetContext Context;
			UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, ScratchRT, Canvas, CanvasSize, Context);
			if (Canvas)
			{
				DrawWorkStrokes(Canvas, CanvasSize, Work, CarbonInkColor, /*bUseOverrideColor=*/true, ENeedleFilter::All);
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

void UInkCanvasComponent::DrawWorkStrokes(UCanvas* Canvas, const FVector2D& CanvasSize, const FInkWork& Work, const FLinearColor& OverrideColor, bool bUseOverrideColor, ENeedleFilter Filter) const
{
	for (const FInkStroke& Stroke : Work.Strokes)
	{
		if ((Filter == ENeedleFilter::LinerOnly && Stroke.NeedleType == EInkNeedle::Shader) ||
			(Filter == ENeedleFilter::ShaderOnly && Stroke.NeedleType != EInkNeedle::Shader))
		{
			continue;
		}
		FLinearColor Color = bUseOverrideColor ? OverrideColor : Stroke.Color;
		Color.A = 1.0f;
		StampPolyline(Canvas, CanvasSize, Stroke.Points, Color, Stroke.bDotStroke,
			Stroke.NeedleType);
	}
}

void UInkCanvasComponent::StampPolyline(UCanvas* Canvas, const FVector2D& CanvasSize, const TArray<FVector2D>& Points, const FLinearColor& Color, bool bDots, EInkNeedle Needle) const
{
	if (Points.IsEmpty())
	{
		return;
	}

	StampNeedleDot(Canvas, CanvasSize, Points[0], Color, Needle, nullptr);
	if (bDots)
	{
		// 點刺重播：逐點蓋章；排向由相鄰兩點推導（與 live 增量同構＝同一份
		// 點序列撒出同一排；跨縫大跳沿用上一個有效方向）
		FVector2D RowDir = FVector2D::ZeroVector;
		bool bHasDir = false;
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			const FVector2D Delta = Points[Index] - Points[Index - 1];
			const float DeltaLen = Delta.Size();
			if (DeltaLen > KINDA_SMALL_NUMBER && DeltaLen <= MaxUvSegmentLength)
			{
				RowDir = Delta / DeltaLen;
				bHasDir = true;
			}
			StampNeedleDot(Canvas, CanvasSize, Points[Index], Color, Needle,
				bHasDir ? &RowDir : nullptr);
		}
		return;
	}
	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		StampSegment(Canvas, CanvasSize, Points[Index - 1], Points[Index], Color,
			NeedleUvRadius(Needle));
	}
}

void UInkCanvasComponent::StampNeedleDot(UCanvas* Canvas, const FVector2D& CanvasSize, const FVector2D& UV, const FLinearColor& Color, EInkNeedle Needle, const FVector2D* RowDirUv) const
{
	if (Needle != EInkNeedle::Shader)
	{
		StampDot(Canvas, CanvasSize, UV, Color, MarkerUvRadius);
		return;
	}
	StampMistRow(Canvas, CanvasSize, UV, Color, RowDirUv);
}

void UInkCanvasComponent::StampMistRow(UCanvas* Canvas, const FVector2D& CanvasSize, const FVector2D& UV, const FLinearColor& Color, const FVector2D* RowDirUv) const
{
	// 細針點排章（07-23 十一版 user 規格）：一排超小「半透明」墨點垂直於行進
	// 方向、單點不透明度沿排弧形衰減（中央 30%→邊緣 5%=接觸壓力剖面=羽化）。
	// 半透明點在畫布上真實疊加融合（30%+30%=51%）——密度夠=平滑灰面＋細緻
	// 針點紋理；胡椒點版的不透明點只能靠肉眼混色=本作視距（貼膚30cm+FOV36）死路。
	// 決定性：亂數種子=UV 量化雜湊（LCG 推進、恆消費）——各端/重放/碳黑同 pattern。
	UTexture2D* Nib = const_cast<UInkCanvasComponent*>(this)->GetOrCreateSoftNibTexture();
	if (!Nib || !Nib->GetResource())
	{
		return;
	}
	// 冷墨底色：半透明純黑疊暖膚=棕（髒讀感真兇之一）；皮下墨散射偏藍
	const FLinearColor Ink(
		FMath::Max(Color.R, ShaderMistCoolFloor.R),
		FMath::Max(Color.G, ShaderMistCoolFloor.G),
		FMath::Max(Color.B, ShaderMistCoolFloor.B));

	// 縫區閘（07-24 跨縫制）：距 UV 縫 <2.5cm 的排改走表面補丁逐點落墨——
	// 平面條帶在縫上會被裁出直線界線＋汙染圖集隔壁島（viewport 實錘）
	if (RowDirUv)
	{
		if (UInkBodyComponent* Body = ResolveBody())
		{
			if (Body->IsUVNearSeam(UV) &&
				StampMistRowOnSurface(Canvas, CanvasSize, UV, Ink, *RowDirUv))
			{
				return;
			}
		}
	}
	if (!RowDirUv)
	{
		// 筆劃首點=尚無排向：單顆軟點（中央濃度）
		const float A = ShaderStippleAlphaCenter;
		const FLinearColor Premult(Ink.R * A, Ink.G * A, Ink.B * A, A);
		const float Diameter = FMath::Max(1.5f, ShaderStippleUvRadius * 2.0f * CanvasSize.X);
		const FVector2D TopLeft(UV.X * CanvasSize.X - Diameter * 0.5f, UV.Y * CanvasSize.Y - Diameter * 0.5f);
		FCanvasTileItem TileItem(TopLeft, Nib->GetResource(), FVector2D(Diameter, Diameter), Premult);
		TileItem.BlendMode = SE_BLEND_AlphaComposite;
		Canvas->DrawItem(TileItem);
		return;
	}

	// 整排=一張預烘條帶（robo superfast 實錘：每排 20 個 canvas item 的提交成本
	// 拖垮幀率）：兩個三角形擺出旋轉貼片（角點顯式算=無旋轉符號歧義）、
	// 變體由 UV 量化雜湊選=各端/重放/碳黑同 pattern
	UTexture2D* Strip = const_cast<UInkCanvasComponent*>(this)->GetOrCreateStippleRowTexture();
	if (!Strip || !Strip->GetResource())
	{
		return;
	}
	constexpr int32 Variants = 8;
	const uint32 Hash = (static_cast<uint32>(FMath::RoundToInt(UV.X * 65536.0f)) * 73856093u)
		^ (static_cast<uint32>(FMath::RoundToInt(UV.Y * 65536.0f)) * 19349663u);
	const int32 V = static_cast<int32>((Hash >> 4) % Variants);
	const float V0 = static_cast<float>(V) / Variants;
	const float V1 = static_cast<float>(V + 1) / Variants;
	// 反成軌（斷格修同輪）：24 個槽位是固定的——只靠 8 變體的槽內抖動，
	// 沿行進方向點會排成縱向軌。每排加①橫向整體平移 ±1 槽寬②鏡像翻轉，
	// 皆由雜湊決定＝各端/重放/碳黑同 pattern。
	const float SlotShift = ((static_cast<float>((Hash >> 12) & 0xFF) / 255.0f) - 0.5f) * 2.0f
		* (2.0f * ShaderRowHalfWidthUv / FMath::Max(ShaderRowDotCount, 1));
	const bool bFlipU = (Hash & 1u) != 0;
	const float U0 = bFlipU ? 1.0f : 0.0f;
	const float U1 = bFlipU ? 0.0f : 1.0f;

	const FVector2D Perp(-RowDirUv->Y, RowDirUv->X);
	const FVector2D ShiftedUV = ClampUV(UV + Perp * SlotShift);
	const FVector2D CenterPx(ShiftedUV.X * CanvasSize.X, ShiftedUV.Y * CanvasSize.Y);
	const float HalfWPx = ShaderRowHalfWidthUv * CanvasSize.X;
	const FVector2D AcrossPx = Perp * HalfWPx;                  // 條帶 X=橫越排帶
	const FVector2D AlongPx = (*RowDirUv) * (HalfWPx / 8.0f);   // 條帶 Y=沿行進（高=寬/8）
	const FLinearColor VtxColor(Ink.R, Ink.G, Ink.B, 1.0f);     // alpha 剖面已烘進條帶

	FCanvasUVTri Tri1, Tri2;
	const FVector2D PA = CenterPx - AcrossPx - AlongPx; // (U0,V0)
	const FVector2D PB = CenterPx + AcrossPx - AlongPx; // (U1,V0)
	const FVector2D PC = CenterPx + AcrossPx + AlongPx; // (U1,V1)
	const FVector2D PD = CenterPx - AcrossPx + AlongPx; // (U0,V1)
	Tri1.V0_Pos = PA; Tri1.V0_UV = FVector2D(U0, V0); Tri1.V0_Color = VtxColor;
	Tri1.V1_Pos = PB; Tri1.V1_UV = FVector2D(U1, V0); Tri1.V1_Color = VtxColor;
	Tri1.V2_Pos = PC; Tri1.V2_UV = FVector2D(U1, V1); Tri1.V2_Color = VtxColor;
	Tri2.V0_Pos = PA; Tri2.V0_UV = FVector2D(U0, V0); Tri2.V0_Color = VtxColor;
	Tri2.V1_Pos = PC; Tri2.V1_UV = FVector2D(U1, V1); Tri2.V1_Color = VtxColor;
	Tri2.V2_Pos = PD; Tri2.V2_UV = FVector2D(U0, V1); Tri2.V2_Color = VtxColor;

	TArray<FCanvasUVTri> Tris;
	Tris.Add(Tri1);
	Tris.Add(Tri2);
	FCanvasTriangleItem TriItem(Tris, Strip->GetResource());
	TriItem.BlendMode = SE_BLEND_AlphaComposite; // 預乘 over：排/趟自然疊深
	Canvas->DrawItem(TriItem);
}

UInkBodyComponent* UInkCanvasComponent::ResolveBody() const
{
	if (CachedBody.IsValid())
	{
		return CachedBody.Get();
	}
	if (AActor* Owner = GetOwner())
	{
		CachedBody = Owner->FindComponentByClass<UInkBodyComponent>();
	}
	return CachedBody.Get();
}

bool UInkCanvasComponent::StampMistRowOnSurface(UCanvas* Canvas, const FVector2D& CanvasSize, const FVector2D& UV, const FLinearColor& Ink, const FVector2D& RowDirUv) const
{
	// 縫區排章：以排心為種子沿皮膚 BFS 攤平（補丁=cm 平面、跨縫連續），每顆針點
	// 在攤平面上定位再映回 UV0——點落在縫哪一側由「真實表面」決定，兩側自動接續、
	// 圖集上排在隔壁的無關島構造上碰不到。補丁在相鄰排之間快取（中心移 >0.5cm 重建）。
	UInkBodyComponent* Body = ResolveBody();
	if (!Body)
	{
		return false;
	}
	const int32 SeedTri = Body->FindTriAtUV(UV);
	FVector World;
	if (SeedTri == INDEX_NONE || !Body->UVToWorldOnTri(SeedTri, UV, World))
	{
		return false;
	}
	constexpr float PatchRadiusCm = 2.2f; // 帶半寬 1.5＋抖動 ~0.15＋點半徑 ~0.1＋餘裕
	if (!bSeamPatchValid ||
		FVector::DistSquared(World, SeamPatchCenterWorld) > FMath::Square(0.5f))
	{
		bSeamPatchValid = Body->BuildSurfacePatchFromTri(SeedTri, World, PatchRadiusCm, SeamPatch);
		SeamPatchCenterWorld = World;
	}
	if (!bSeamPatchValid)
	{
		return false;
	}

	// 排心的攤平座標＋UV→chart 線性映射（用補丁裡種子 tri 的雙座標角點解 2×2）
	const FInkSurfacePatch::FPatchTri* PT = nullptr;
	for (const FInkSurfacePatch::FPatchTri& Cand : SeamPatch.Tris)
	{
		if (Cand.CacheTri == SeedTri)
		{
			PT = &Cand;
			break;
		}
	}
	if (!PT)
	{
		return false; // 快取補丁沒蓋到本排的 tri（掃過縫走遠了）——重建下輪自然命中
	}
	const FVector2D EU1 = PT->C[1].UV0 - PT->C[0].UV0;
	const FVector2D EU2 = PT->C[2].UV0 - PT->C[0].UV0;
	const FVector2D EC1 = PT->C[1].Chart - PT->C[0].Chart;
	const FVector2D EC2 = PT->C[2].Chart - PT->C[0].Chart;
	const float Det = EU1.X * EU2.Y - EU2.X * EU1.Y;
	if (FMath::Abs(Det) < 1e-12f)
	{
		return false;
	}
	// M×dUV＝dChart：先把 dUV 解成 (EU1,EU2) 的組合、再以 (EC1,EC2) 重組
	auto UvDirToChart = [&](const FVector2D& D)
	{
		const float A = (D.X * EU2.Y - EU2.X * D.Y) / Det;
		const float B = (EU1.X * D.Y - D.X * EU1.Y) / Det;
		return EC1 * A + EC2 * B;
	};
	const FVector2D B1 = UV - PT->C[0].UV0;
	const FVector2D CenterChart = PT->C[0].Chart + UvDirToChart(B1);
	const FVector2D DirCRaw = UvDirToChart(RowDirUv);
	const float CmPerUv = DirCRaw.Size(); // 均勻紋素密度＝近共形，方向長度即比例
	if (CmPerUv < 1e-6f)
	{
		return false;
	}
	const FVector2D DirC = DirCRaw / CmPerUv;
	const FVector2D PerpC(-DirC.Y, DirC.X);

	UTexture2D* Nib = const_cast<UInkCanvasComponent*>(this)->GetOrCreateSoftNibTexture();
	if (!Nib || !Nib->GetResource())
	{
		return false;
	}
	const float HalfWCm = ShaderRowHalfWidthUv * CmPerUv;
	const int32 K = FMath::Clamp(ShaderRowDotCount, 4, 40);
	const float SlotWCm = 2.0f * HalfWCm / K;
	const float DotDiaPx = FMath::Max(1.5f, ShaderStippleUvRadius * 2.0f * CanvasSize.X);
	uint32 Seed = (static_cast<uint32>(FMath::RoundToInt(UV.X * 65536.0f)) * 73856093u)
		^ (static_cast<uint32>(FMath::RoundToInt(UV.Y * 65536.0f)) * 19349663u);
	auto NextRand = [&Seed]() -> float
	{
		Seed = Seed * 1664525u + 1013904223u;
		return static_cast<float>(Seed >> 8) / 16777216.0f;
	};
	for (int32 i = 0; i < K; ++i)
	{
		const float T = ((i + 0.5f) / K) * 2.0f - 1.0f;
		const float Arc = FMath::Sqrt(FMath::Max(1.0f - T * T, 0.0f));
		const float A = FMath::Lerp(ShaderStippleAlphaEdge, ShaderStippleAlphaCenter, Arc);
		const float JitterAcross = (NextRand() - 0.5f) * SlotWCm;
		const float JitterAlong = (NextRand() - 0.5f) * SlotWCm;
		const FVector2D ChartPt = CenterChart
			+ PerpC * (T * HalfWCm + JitterAcross)
			+ DirC * JitterAlong;
		FVector2D DotUV;
		if (!SeamPatch.ChartToUV0(ChartPt, DotUV))
		{
			continue; // 補丁外（剪影邊/褌洞）＝誠實不落墨
		}
		const FLinearColor Premult(Ink.R * A, Ink.G * A, Ink.B * A, A);
		const FVector2D TopLeft(DotUV.X * CanvasSize.X - DotDiaPx * 0.5f,
			DotUV.Y * CanvasSize.Y - DotDiaPx * 0.5f);
		FCanvasTileItem TileItem(TopLeft, Nib->GetResource(), FVector2D(DotDiaPx, DotDiaPx), Premult);
		TileItem.BlendMode = SE_BLEND_AlphaComposite;
		Canvas->DrawItem(TileItem);
	}
	return true;
}

void UInkCanvasComponent::StampSegment(UCanvas* Canvas, const FVector2D& CanvasSize, const FVector2D& From, const FVector2D& To, const FLinearColor& Color, float UvRadius) const
{
	const float Distance = FVector2D::Distance(From, To);

	// 跨 UV 島跳躍：不內插（內插會在圖集上拉出垃圾長線），只在落點蓋章
	if (Distance > MaxUvSegmentLength)
	{
		StampDot(Canvas, CanvasSize, To, Color, UvRadius);
		return;
	}

	// 下限 0.00025：sumo 圖集密度較低（0.617px/mm）→ 半徑 0.000584，舊下限 0.0005 會吃掉間距係數
	const float StepSize = FMath::Max(UvRadius * StampSpacingFactor, 0.00025f);
	const int32 StepCount = FMath::Clamp(FMath::CeilToInt(Distance / StepSize), 1, 256);
	for (int32 Step = 1; Step <= StepCount; ++Step)
	{
		const float Alpha = static_cast<float>(Step) / static_cast<float>(StepCount);
		StampDot(Canvas, CanvasSize, FMath::Lerp(From, To, Alpha), Color, UvRadius);
	}
}

void UInkCanvasComponent::StampDot(UCanvas* Canvas, const FVector2D& CanvasSize, const FVector2D& UV, const FLinearColor& Color, float UvRadius) const
{
	UTexture2D* Nib = const_cast<UInkCanvasComponent*>(this)->GetOrCreateNibTexture();
	if (!Nib || !Nib->GetResource())
	{
		return;
	}

	const float Diameter = FMath::Max(2.0f, UvRadius * 2.0f * CanvasSize.X);
	const FVector2D TopLeft(UV.X * CanvasSize.X - Diameter * 0.5f, UV.Y * CanvasSize.Y - Diameter * 0.5f);

	FCanvasTileItem TileItem(TopLeft, Nib->GetResource(), FVector2D(Diameter, Diameter), Color);
	TileItem.BlendMode = SE_BLEND_AlphaComposite; // premultiplied over：RGB 與 alpha 皆累積
	Canvas->DrawItem(TileItem);
}

void UInkCanvasComponent::StampIntoLayerRT(const FVector2D& From, const FVector2D& To, const FLinearColor& Color, bool bDotOnly, EInkNeedle Needle, const FVector2D* RowDirUv)
{
	// 針型路由：Liner→線層（銳化咬=脆的實心墨）；Shader→霧層
	//（獨立層=銳化不咬=半透明針點成立——兩種材質語義分層的結構保證，三版鐵則）
	UTextureRenderTarget2D* LayerRT = (Needle == EInkNeedle::Shader) ? ToRawPtr(MistRT) : ToRawPtr(MarkerRT);
	if (!LayerRT)
	{
		return;
	}

	// 批次快路徑：同層已開 context → 直畫（每點開關 4096 context=幀率殺手）
	if (BatchCanvas && BatchRT == LayerRT)
	{
		if (bDotOnly)
		{
			StampNeedleDot(BatchCanvas, BatchCanvasSize, To, Color, Needle, RowDirUv);
		}
		else
		{
			StampSegment(BatchCanvas, BatchCanvasSize, From, To, Color, NeedleUvRadius(Needle));
		}
		return;
	}

	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize = FVector2D::ZeroVector;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, LayerRT, Canvas, CanvasSize, Context);
	if (Canvas)
	{
		if (bDotOnly)
		{
			StampNeedleDot(Canvas, CanvasSize, To, Color, Needle, RowDirUv);
		}
		else
		{
			StampSegment(Canvas, CanvasSize, From, To, Color, NeedleUvRadius(Needle));
		}
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
}

void UInkCanvasComponent::BeginStampBatchFor(int32 AuthorId)
{
	if (BatchCanvas)
	{
		return; // 已開（防重入）
	}
	const int32* WorkId = OpenStrokeWorkByAuthor.Find(AuthorId);
	if (!WorkId)
	{
		return; // 無開筆＝無事可批（StampIntoLayerRT 走逐點路徑，冪等安全）
	}
	FInkWork* Work = FindWork(*WorkId);
	if (!Work || Work->Strokes.IsEmpty())
	{
		return;
	}
	const EInkNeedle Needle = Work->Strokes.Last().NeedleType;
	UTextureRenderTarget2D* LayerRT = (Needle == EInkNeedle::Shader) ? ToRawPtr(MistRT) : ToRawPtr(MarkerRT);
	if (!LayerRT)
	{
		return;
	}
	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize = FVector2D::ZeroVector;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, LayerRT, Canvas, CanvasSize, BatchContext);
	if (!Canvas)
	{
		return;
	}
	BatchCanvas = Canvas;
	BatchCanvasSize = CanvasSize;
	BatchRT = LayerRT;
}

void UInkCanvasComponent::EndStampBatch()
{
	if (!BatchCanvas)
	{
		return;
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, BatchContext);
	BatchCanvas = nullptr;
	BatchRT = nullptr;
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
	const bool bMistSaved = SaveRTToPng(MistRT, AbsolutePathPrefix + TEXT("_mist.png"));
	return bMarkerSaved && bTattooSaved && bMistSaved;
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
