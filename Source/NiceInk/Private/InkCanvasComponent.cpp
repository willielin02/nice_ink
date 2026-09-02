#include "InkCanvasComponent.h"

#include "InkMistSurface.h"

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

	// --- 筆頭的兩個歸一化形狀常數（2026-08-29 羽化修）---
	// 病史：舊版把「羽化 2px」寫在**筆頭貼圖域**，而這張 64px 的圖被畫成
	// 3.70 個畫布紋素＝縮小 17.3 倍 ⇒ 羽化帶到了畫布上只剩 **0.116 紋素**＝
	// alpha 實質二值（實測只有 4~6% 的墨紋素帶部分覆蓋，正確面積抗鋸齒是 ~36%）。
	// 而材質的 MarkerSharpen（Valve alpha-tested magnification）**假設來源是距離場**：
	// 餵它二值覆蓋 ⇒ 銳化不修抖動，只是把糊掉的鋸齒變成清晰的鋸齒
	//（量測：銳化把 0.1→0.9 過渡從 11.1px 壓到 2.6px，但邊緣位置偏差 std 前後
	// 完全相同 1.97px）。同型的坑十四版在霧層的排針條帶上修過了（烘在目的地
	// 解析度＋積分下取樣），線層的筆頭當時沒被一起掃到。
	//
	// 現制＝羽化帶在**畫布上**恰好 1 個紋素（＝面積覆蓋的正確過渡寬度）。
	// 羽化要有地方放 ⇒ tile 必須比墨大：α=0.5 等值面只佔紋理半徑的
	// NibHalfAlphaFrac，剩下的半徑放羽化。兩顆常數的比 0.426/0.74 就是
	// 「麥克筆尺寸（α=0.5 半徑 1.736 紋素）下羽化帶＝1.00 紋素」解出來的。
	// 離線受控 A/B（同一條點鏈只換光柵器）：邊緣抖動 0.190 → 0.082 紋素，
	// 而**逐點做精確面積覆蓋的理論值是 0.080**＝這一刀吃滿。
	constexpr float NibHalfAlphaFrac = 0.74f;  // α=0.5 半徑 / 紋理半徑
	constexpr float NibRampFrac = 0.426f;      // 羽化帶寬 / 紋理半徑
	// 舊版的 α=0.5 半徑＝(CoreRadius 29 + Falloff 2 / 2) / 32 = 0.9375 個 tile 半徑。
	// 新版放大 tile 時乘回這個係數 ⇒ **實際線寬逐位不變**（2.81mm）。
	// 註：2.81mm 是墨真正的寬度；`TattooNibDiameterCm=0.30`（v_max 守恆式與 HUD
	// 圈的錨）說的是 3.00mm，兩者差 6.3%——**已知偏差，本刀不動**（改它會動到
	// 線寬與針速，屬 user 口味域）。
	constexpr float NibLegacyHalfAlphaFrac = 0.9375f;
}

UInkCanvasComponent::UInkCanvasComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UInkCanvasComponent::BeginPlay()
{
	Super::BeginPlay();

	// 惰性配置制（2026-08-25）：這裡不再無條件配四張 4096——RebuildRenderTargets
	// 依 Works 內容決定要哪幾層。空畫布＝零 VRAM，材質綁 4×4 全透明替身。
	RebuildRenderTargets();
}

UTexture2D* UInkCanvasComponent::GetEmptyInkTexture()
{
	static UTexture2D* Empty = nullptr;
	if (Empty && IsValid(Empty))
	{
		return Empty;
	}
	constexpr int32 Size = 4;
	Empty = UTexture2D::CreateTransient(Size, Size, PF_B8G8R8A8, TEXT("InkEmptyLayer"));
	Empty->SRGB = false;
	Empty->Filter = TF_Bilinear;
	Empty->NeverStream = true;
	FTexture2DMipMap& Mip = Empty->GetPlatformData()->Mips[0];
	FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	for (int32 I = 0; I < Size * Size; ++I)
	{
		Pixels[I] = FColor(0, 0, 0, 0); // 預乘全透明＝「這裡沒有墨」
	}
	Mip.BulkData.Unlock();
	Empty->UpdateResource();
	Empty->AddToRoot(); // 全域共用、永不回收（64 byte）
	return Empty;
}

UInkCanvasComponent::FLayerNeeds UInkCanvasComponent::ComputeLayerNeeds() const
{
	FLayerNeeds Needs;
	Needs.bMarker = bDrawLayersPinned;
	Needs.bMist = bDrawLayersPinned;
	for (const FInkWork& Work : Works)
	{
		if (Work.State == EInkWorkState::Marker)
		{
			// 稿線／液線→線層、打霧→霧層（渲染分層與 StampIntoLayerRT 的路由同構）
			for (const FInkStroke& Stroke : Work.Strokes)
			{
				if (Stroke.NeedleType == EInkNeedle::Shader)
				{
					Needs.bMist = true;
				}
				else
				{
					Needs.bMarker = true;
				}
			}
		}
		else
		{
			Needs.bTattoo = true;
			if (Work.LaserLevel > 0 && LaserOpacity(Work.LaserLevel) > 0.0f)
			{
				Needs.bScratch = true; // 淡化合成的暫存畫布
			}
		}
	}
	return Needs;
}

void UInkCanvasComponent::ReleaseLayer(TObjectPtr<UTextureRenderTarget2D>& Slot, bool& bOutChanged)
{
	if (Slot)
	{
		Slot = nullptr;
		bOutChanged = true;
	}
}

UTextureRenderTarget2D* UInkCanvasComponent::EnsureMarkerRT()
{
	if (!MarkerRT)
	{
		MarkerRT = CreateLayerRT(TEXT("InkMarkerRT"), RenderTargetResolution);
		OnLayersChanged.Broadcast();
	}
	return MarkerRT;
}

UTextureRenderTarget2D* UInkCanvasComponent::EnsureTattooRT()
{
	if (!TattooRT)
	{
		TattooRT = CreateLayerRT(TEXT("InkTattooRT"), RenderTargetResolution);
		OnLayersChanged.Broadcast();
	}
	return TattooRT;
}

UTextureRenderTarget2D* UInkCanvasComponent::EnsureScratchRT()
{
	if (!ScratchRT)
	{
		// 只是合成暫存、不進材質＝不必廣播
		ScratchRT = CreateLayerRT(TEXT("InkScratchRT"), RenderTargetResolution);
	}
	return ScratchRT;
}

void UInkCanvasComponent::PrewarmDrawLayers()
{
	bDrawLayersPinned = true;
	EnsureMarkerRT();
	EnsureMistLayer();
}

void UInkCanvasComponent::ReleaseDrawLayerPin()
{
	if (!bDrawLayersPinned)
	{
		return;
	}
	bDrawLayersPinned = false;
	// 釘選解除後這兩層還在不在，改由 Works 決定（甦醒時稿線通常還在＝照樣留著）
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
	const float TexHalf = NibTextureSize * 0.5f;
	// α=0.5 等值面（＝玩家看到的墨緣，銳化的閾值就落在這裡）與羽化帶寬，
	// 兩者都以紋理半徑為單位＝與 StampDot 的 tile 尺寸同一組常數（見檔頭）
	const float HalfAlphaRadius = NibHalfAlphaFrac * TexHalf;
	const float FalloffWidth = FMath::Max(NibRampFrac * TexHalf, 1.0f);

	for (int32 Y = 0; Y < NibTextureSize; ++Y)
	{
		for (int32 X = 0; X < NibTextureSize; ++X)
		{
			const float Dist = FMath::Sqrt(FMath::Square(X - Center) + FMath::Square(Y - Center));
			// 線性覆蓋斜坡：α=1 在 HalfAlphaRadius−W/2、α=0 在 +W/2（0 落在紋理
			// 邊界內＝tile 邊緣不會被切出硬方角）
			const float Alpha01 = FMath::Clamp(
				(HalfAlphaRadius + FalloffWidth * 0.5f - Dist) / FalloffWidth, 0.0f, 1.0f);
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

// --- 霧層＝CPU 軟體光柵器（2026-09-02 灰洗分檔戰役；語義全文見 InkMistSurface.h）---

bool UInkCanvasComponent::EnsureMistLayer()
{
	if (MistSurface.IsValid() && MistTex)
	{
		return true;
	}
	if (!MistSurface.IsValid())
	{
		MistSurface = MakeShared<FInkMistSurface>();
		MistSurface->Init(MistRenderTargetResolution);
	}
	if (!MistTex)
	{
		MistTex = CreateMistTexture(this, TEXT("InkMistTex"), MistRenderTargetResolution);
		// 初始內容＝整張從（零的）緩衝上傳一次（貼圖無 bulk＝不上傳就是未定義）
		UploadMistSurface(MistSurface, MistTex, /*bFull=*/true);
		OnLayersChanged.Broadcast();
	}
	return MistSurface.IsValid() && MistTex != nullptr;
}

void UInkCanvasComponent::EnsureMistScratch()
{
	if (!MistScratchSurface.IsValid())
	{
		MistScratchSurface = MakeShared<FInkMistSurface>();
		MistScratchSurface->Init(MistRenderTargetResolution);
	}
	if (!MistScratchTex)
	{
		// 只是合成暫存、不進材質＝不必廣播
		MistScratchTex = CreateMistTexture(this, TEXT("InkMistScratchTex"), MistRenderTargetResolution);
		UploadMistSurface(MistScratchSurface, MistScratchTex, /*bFull=*/true);
	}
}

UTexture2D* UInkCanvasComponent::CreateMistTexture(UObject* Outer, const TCHAR* Name, int32 Res)
{
	UTexture2D* Tex = UTexture2D::CreateTransient(Res, Res, PF_B8G8R8A8, Name);
	// sRGB=true＝與舊 RTF_RGBA8 RT 同語義（RT 預設非線性 gamma；實測其儲存位元組
	// ＝sRGB 編碼：linear 0.0168 的「1 號黑」落盤 35/255）——材質取樣端零改動
	Tex->SRGB = true;
	Tex->Filter = TF_Bilinear;
	Tex->NeverStream = true;
	Tex->AddressX = TA_Clamp;
	Tex->AddressY = TA_Clamp;
	// **不鎖 bulk、不歸零**（09-02 OOM 修）：鎖了就是每張 64MB 的 CPU 側常駐副本，
	// 而內容永遠從光柵緩衝 UpdateTextureRegions 上傳＝bulk 純浪費。robo 單行程
	// 三世界 ×（bulk 64＋緩衝 64）疊在既有記憶體壓力上＝本日 OOM 現場（08-25
	//「Out of video memory 倒的是系統記憶體」同族）。初始內容由呼叫端做一次
	// 全區上傳（緩衝是零＝全透明）來定義。
	Tex->UpdateResource();
	return Tex;
}

void UInkCanvasComponent::UploadMistSurface(const TSharedPtr<FInkMistSurface>& Surface, UTexture2D* Tex, bool bFull)
{
	if (!Tex || !Surface.IsValid() || !Surface->IsInited())
	{
		return;
	}
	FIntRect R;
	if (bFull)
	{
		Surface->MarkAllDirty();
	}
	if (!Surface->TakeDirty(R))
	{
		return;
	}
	const int32 Res = Surface->Resolution();
	if (!Surface->HasGlaze())
	{
		// 零複製快路：直接引用光柵器常駐緩衝（pitch=整列；SrcX/SrcY 指進緩衝）；
		// **清理回呼抓一份 SharedPtr＝緩衝被釘住到渲染執行緒複製完成**（09-02 崩潰修）。
		// 複製期間遊戲執行緒繼續寫緩衝＝良性（讀到更新的位元組、下一筆上傳跟上）。
		FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(
			R.Min.X, R.Min.Y, R.Min.X, R.Min.Y, R.Width(), R.Height());
		Tex->UpdateTextureRegions(0, 1, Region, Res * sizeof(FColor), sizeof(FColor),
			(uint8*)const_cast<FColor*>(Surface->Data()),
			[Surface](uint8*, const FUpdateTextureRegion2D* Regions)
			{
				delete Regions;
			});
		return;
	}
	// 罩染合成路（09-02 glazing）：把「罩染 over 基底」預合成進 rect-local 暫存再
	// 上傳——材質端仍只看到一張貼圖＝零材質改動；暫存自含＝清理回呼釋放、無 UAF。
	const int32 W = R.Width();
	const int32 H = R.Height();
	FColor* Staging = new FColor[static_cast<int64>(W) * H];
	Surface->ComposeInto(Staging, W, R);
	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(
		R.Min.X, R.Min.Y, 0, 0, W, H);
	Tex->UpdateTextureRegions(0, 1, Region, W * sizeof(FColor), sizeof(FColor),
		(uint8*)Staging,
		[](uint8* Data, const FUpdateTextureRegion2D* Regions)
		{
			delete[] reinterpret_cast<FColor*>(Data);
			delete Regions;
		});
}

void UInkCanvasComponent::FlushMistUpload()
{
	if (MistSurface.IsValid() && MistTex)
	{
		UploadMistSurface(MistSurface, MistTex, /*bFull=*/false);
	}
}

void UInkCanvasComponent::ReleaseMistLayer(bool& bOutChanged)
{
	if (MistTex || MistSurface.IsValid())
	{
		MistTex = nullptr; // 只放指標；消費端重綁後由 GC 收（與 RT 同紀律）
		MistSurface.Reset();
		bOutChanged = true;
	}
}

// --- 作畫 ---

void UInkCanvasComponent::BeginStroke(int32 AuthorId, FLinearColor Color, FVector2D UV, bool bDotStroke,
	EInkNeedle Needle, uint8 Flow, uint8 Tier)
{
	if (AuthorId == INDEX_NONE)
	{
		return;
	}

	EndStroke(AuthorId);

	// 落筆聲（每客戶端本地重放時各自播；閉眼沉睡者在音效層被全域靜音）。
	// 稿筆無落筆 one-shot（08-05 user 裁決）：失去接觸閘會讓一次拖曳反覆收筆/
	// 重開筆——per-BeginStroke 的音在稿筆上=急促連響；摩擦 loop 才是稿筆的聲音。
	if (Needle != EInkNeedle::Stencil)
	{
		NiAudio::Play(this, ENiSound::StrokeStart, 0.7f);
	}

	UV = ClampUV(UV);
	Color.A = 1.0f; // 麥克筆＝不透明墨水

	FInkWork& Work = GetOrCreateActiveWork(AuthorId);

	FInkStroke Stroke;
	Stroke.Color = Color;
	// 濃度檔（09-02 分檔）：per-stroke；只有 Shader 消費（其餘針型恆實檔）
	Stroke.TierAlpha = (Needle == EInkNeedle::Shader) ? Tier : 255;
	Stroke.Points.Add(UV);
	Stroke.PointFlow.Add(Flow);
	// 稿線牆（09-02 v2）：**首點也有牆**——v1 首點恆無牆（「首點的牆算不出來」），
	// 等於每一筆的起點都是一個整圓的漏墨口。半平面不需要行進方向；編碼框固定
	// (1,0)，重放端首點同樣以 (1,0) 解碼＝一致。
	uint8 Wall0[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
	if (Needle == EInkNeedle::Shader)
	{
		FNiInkWallPlane Planes[2];
		int32 NumPlanes = 0;
		ComputeStencilWallPlanes(AuthorId, UV, FVector2D(1.0, 0.0), Planes, NumPlanes);
		EncodeWallPlanes(Planes, NumPlanes, FVector2D(1.0, 0.0), ShaderRowHalfWidthUv, Wall0);
	}
	Stroke.PointWall.Append(Wall0, 4);
	Stroke.StartTimestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	Stroke.bDotStroke = bDotStroke;
	Stroke.NeedleType = Needle;
	Work.Strokes.Add(Stroke);

	OpenStrokeWorkByAuthor.Add(AuthorId, Work.WorkId);
	LastPointByAuthor.Add(AuthorId, UV);
	LastRowDirByAuthor.Remove(AuthorId); // 筆劃首點=尚無排向

	// 罩染的筆劃邊界（09-02 三修）：開筆＝新世代——同筆掃過自己恆走 max、
	// 跨筆的淡壓深才罩染（重放端經 MulticastPaintBegin 走同一條＝同構）
	if (Needle == EInkNeedle::Shader && EnsureMistLayer())
	{
		MistSurface->BeginStrokeMark();
	}

	StampIntoLayerRT(UV, UV, Color, /*bDotOnly=*/true, Needle, nullptr, Flow / 255.0f, Wall0,
		Stroke.TierAlpha / 255.0f);
	OnCanvasChanged.Broadcast();
}

void UInkCanvasComponent::AddStrokePoint(int32 AuthorId, FVector2D UV, uint8 Flow)
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
	// PointFlow 與 Points 逐索引對齊；中途缺項（不應發生）補滿濃度墊平
	while (Stroke.PointFlow.Num() < Stroke.Points.Num() - 1)
	{
		Stroke.PointFlow.Add(255);
	}
	Stroke.PointFlow.Add(Flow);
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
		// 稿線牆（09-01 建制、09-02 v2 半平面）：只有填色有牆；落墨當下算一次、存進
		// 筆劃＝重建可重現（稿線 EnterTour 會被洗掉，現算的話已裁好的填色會在巡禮
		// 那刻膨脹回去）。
		uint8 Wall[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
		if (Stroke.NeedleType == EInkNeedle::Shader)
		{
			const FVector2D FrameDir = RowDir ? *RowDir : FVector2D(1.0, 0.0);
			FNiInkWallPlane Planes[2];
			int32 NumPlanes = 0;
			ComputeStencilWallPlanes(AuthorId, UV, FrameDir, Planes, NumPlanes);
			EncodeWallPlanes(Planes, NumPlanes, FrameDir, ShaderRowHalfWidthUv, Wall);
		}
		while (Stroke.PointWall.Num() < (Stroke.Points.Num() - 1) * 4)
		{
			Stroke.PointWall.Add(0xFF); // 缺項補「不截斷」＝與 PointFlow 同款對齊語義（stride 4）
		}
		Stroke.PointWall.Append(Wall, 4);
		StampIntoLayerRT(UV, UV, Stroke.Color, /*bDotOnly=*/true, Stroke.NeedleType, RowDir, Flow / 255.0f, Wall,
			Stroke.TierAlpha / 255.0f);
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

void UInkCanvasComponent::WashStencil()
{
	// 打稿制（07-25）：稿線在受害者甦醒收束時全洗——沒上墨的部分從未存在過。
	// 稿線與真墨同住作者的 Marker 作品裡（同一套筆劃管線）；只拔 Stencil 筆劃、
	// 空掉的 Marker 作品一併移除（空作品進巡禮＝空白傑作）。
	OpenStrokeWorkByAuthor.Empty();
	LastPointByAuthor.Empty();
	for (FInkWork& W : Works)
	{
		if (W.State == EInkWorkState::Marker)
		{
			W.Strokes.RemoveAll([](const FInkStroke& S) { return S.NeedleType == EInkNeedle::Stencil; });
		}
	}
	Works.RemoveAll([](const FInkWork& W)
	{
		return W.State == EInkWorkState::Marker && W.Strokes.Num() == 0;
	});
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
	// 批次 context 若還開著，BatchRT 是裸指標——重播前先收，免得指向剛被放掉的層
	EndStampBatch();

	// 惰性配置：先配齊需要的，再放掉不需要的。順序不可反——消費端要先收到
	// 「新的那張」才安全（釋放只是放開指標，資源由 UObject 生命週期收）。
	const FLayerNeeds Needs = ComputeLayerNeeds();
	if (Needs.bMarker) { EnsureMarkerRT(); }
	if (Needs.bMist)   { EnsureMistLayer(); }
	if (Needs.bTattoo) { EnsureTattooRT(); }

	bool bLayerSetChanged = false;
	if (!Needs.bMarker)  { ReleaseLayer(MarkerRT, bLayerSetChanged); }
	if (!Needs.bMist)    { ReleaseMistLayer(bLayerSetChanged); }
	if (!Needs.bTattoo)  { ReleaseLayer(TattooRT, bLayerSetChanged); }
	if (!Needs.bScratch) { bool bIgnored = false; ReleaseLayer(ScratchRT, bIgnored); }
	if (bLayerSetChanged)
	{
		OnLayersChanged.Broadcast();
	}

	if (MarkerRT)
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, MarkerRT, FLinearColor::Transparent);
	}
	if (TattooRT)
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, TattooRT, FLinearColor::Transparent);
	}
	if (MistSurface.IsValid())
	{
		MistSurface->Clear();
	}

	// 麥克筆線層：玩家原色、全不透明，直接 stamp（只收 Liner——銳化只咬線）
	if (MarkerRT)
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

	// 霧層（07-23 十版建制；09-02 CPU 光柵）：Shader 筆劃重播進 CPU 緩衝、整張上傳
	if (MistSurface.IsValid())
	{
		const FVector2D SurfSize(MistSurface->Resolution(), MistSurface->Resolution());
		for (const FInkWork& Work : Works)
		{
			if (Work.State == EInkWorkState::Marker)
			{
				DrawWorkStrokes(nullptr, SurfSize, Work, FLinearColor::White, /*bUseOverrideColor=*/false, ENeedleFilter::ShaderOnly);
			}
		}
		if (MistTex)
		{
			UploadMistSurface(MistSurface, MistTex, /*bFull=*/true);
		}
	}

	// 刺青層：碳黑墨色。未淡化的：**霧成分先走 CPU 光柵合成一張打底**（與活霧同
	// 一支光柵器＝轉碳黑那一刻外觀零跳變），液線成分 canvas 直畫在上（筋彫壓在
	// 暈し上＝正確層序）。淡化的：同構做進 ScratchRT，再整張以工作透明度合成
	//（半透明 stamp 直接重疊會產生堆疊條紋）。
	if (TattooRT)
	{
		bool bAnyCarbonMist = false;
		for (const FInkWork& Work : Works)
		{
			if (Work.State != EInkWorkState::Marker && Work.LaserLevel == 0)
			{
				for (const FInkStroke& St : Work.Strokes)
				{
					if (St.NeedleType == EInkNeedle::Shader)
					{
						bAnyCarbonMist = true;
						break;
					}
				}
			}
			if (bAnyCarbonMist)
			{
				break;
			}
		}
		if (bAnyCarbonMist)
		{
			EnsureMistScratch();
			MistScratchSurface->Clear();
			MistTargetOverride = MistScratchSurface.Get();
			const FVector2D SurfSize(MistScratchSurface->Resolution(), MistScratchSurface->Resolution());
			for (const FInkWork& Work : Works)
			{
				if (Work.State != EInkWorkState::Marker && Work.LaserLevel == 0)
				{
					DrawWorkStrokes(nullptr, SurfSize, Work, CarbonInkColor, /*bUseOverrideColor=*/true, ENeedleFilter::ShaderOnly);
				}
			}
			MistTargetOverride = nullptr;
			UploadMistSurface(MistScratchSurface, MistScratchTex, /*bFull=*/true);
		}

		UCanvas* Canvas = nullptr;
		FVector2D CanvasSize = FVector2D::ZeroVector;
		FDrawToRenderTargetContext Context;
		UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, TattooRT, Canvas, CanvasSize, Context);
		if (Canvas)
		{
			if (bAnyCarbonMist && MistScratchTex && MistScratchTex->GetResource())
			{
				FCanvasTileItem MistUnderlay(FVector2D::ZeroVector,
					MistScratchTex->GetResource(), CanvasSize, FLinearColor::White);
				MistUnderlay.BlendMode = SE_BLEND_AlphaComposite;
				Canvas->DrawItem(MistUnderlay);
			}
			for (const FInkWork& Work : Works)
			{
				if (Work.State != EInkWorkState::Marker && Work.LaserLevel == 0)
				{
					DrawWorkStrokes(Canvas, CanvasSize, Work, CarbonInkColor, /*bUseOverrideColor=*/true, ENeedleFilter::LinerOnly);
				}
			}
		}
		UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
	}

	for (const FInkWork& Work : Works)
	{
		if (Work.State == EInkWorkState::Marker || Work.LaserLevel == 0 || !TattooRT)
		{
			continue;
		}

		const float Opacity = LaserOpacity(Work.LaserLevel);
		if (Opacity <= 0.0f)
		{
			continue;
		}

		EnsureScratchRT();
		UKismetRenderingLibrary::ClearRenderTarget2D(this, ScratchRT, FLinearColor::Transparent);

		// 霧成分＝CPU 光柵（與未淡化路同構），一張 tile 打底進 ScratchRT
		bool bWorkHasMist = false;
		for (const FInkStroke& St : Work.Strokes)
		{
			if (St.NeedleType == EInkNeedle::Shader)
			{
				bWorkHasMist = true;
				break;
			}
		}
		if (bWorkHasMist)
		{
			EnsureMistScratch();
			MistScratchSurface->Clear();
			MistTargetOverride = MistScratchSurface.Get();
			const FVector2D SurfSize(MistScratchSurface->Resolution(), MistScratchSurface->Resolution());
			DrawWorkStrokes(nullptr, SurfSize, Work, CarbonInkColor, /*bUseOverrideColor=*/true, ENeedleFilter::ShaderOnly);
			MistTargetOverride = nullptr;
			UploadMistSurface(MistScratchSurface, MistScratchTex, /*bFull=*/true);
		}

		{
			UCanvas* Canvas = nullptr;
			FVector2D CanvasSize = FVector2D::ZeroVector;
			FDrawToRenderTargetContext Context;
			UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, ScratchRT, Canvas, CanvasSize, Context);
			if (Canvas)
			{
				if (bWorkHasMist && MistScratchTex && MistScratchTex->GetResource())
				{
					FCanvasTileItem MistUnderlay(FVector2D::ZeroVector,
						MistScratchTex->GetResource(), CanvasSize, FLinearColor::White);
					MistUnderlay.BlendMode = SE_BLEND_AlphaComposite;
					Canvas->DrawItem(MistUnderlay);
				}
				DrawWorkStrokes(Canvas, CanvasSize, Work, CarbonInkColor, /*bUseOverrideColor=*/true, ENeedleFilter::LinerOnly);
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
		// 罩染筆劃邊界（重放側）：每條霧筆劃開新世代——與 live 的 BeginStroke 同構
		//（世代「值」不必跨端相同，只比對同/異筆＝每筆唯一即可 ⇒ 重建逐位等價成立）
		if (Stroke.NeedleType == EInkNeedle::Shader)
		{
			if (FInkMistSurface* Surf = MistTargetOverride ? MistTargetOverride : MistSurface.Get())
			{
				Surf->BeginStrokeMark();
			}
		}
		StampPolyline(Canvas, CanvasSize, Stroke.Points, Color, Stroke.bDotStroke,
			Stroke.NeedleType, &Stroke.PointFlow, &Stroke.PointWall, Stroke.TierAlpha / 255.0f);
	}
}

void UInkCanvasComponent::StampPolyline(UCanvas* Canvas, const FVector2D& CanvasSize, const TArray<FVector2D>& Points, const FLinearColor& Color, bool bDots, EInkNeedle Needle, const TArray<uint8>* PointFlow, const TArray<uint8>* PointWall, float Tier01) const
{
	if (Points.IsEmpty())
	{
		return;
	}

	// 逐點流量（十二版手速→濃淡）：缺項=滿濃度（舊存檔/液線針/robo 折線零遷移）
	auto FlowAt = [PointFlow](int32 Index) -> float
	{
		return (PointFlow && PointFlow->IsValidIndex(Index))
			? (*PointFlow)[Index] / 255.0f : 1.0f;
	};

	// 逐點稿線牆（09-01；09-02 v2＝每點 4 位元組，見 FInkStroke::PointWall）：
	// 缺項=不截斷（舊存檔／Liner／稿筆／robo 折線零遷移）
	auto WallAt = [PointWall](int32 Index) -> const uint8*
	{
		return (PointWall && PointWall->Num() >= (Index + 1) * 4)
			? PointWall->GetData() + Index * 4 : nullptr;
	};

	StampNeedleDot(Canvas, CanvasSize, Points[0], Color, Needle, nullptr, FlowAt(0), WallAt(0), Tier01);
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
				bHasDir ? &RowDir : nullptr, FlowAt(Index), WallAt(Index), Tier01);
		}
		return;
	}
	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		if (Needle == EInkNeedle::Shader)
		{
			// 折線段（robo/舊管線）在霧層＝沿線密排單點（CPU 光柵無 canvas 段畫）
			StampMistSegment(Points[Index - 1], Points[Index], Color, Tier01);
		}
		else if (Canvas)
		{
			StampSegment(Canvas, CanvasSize, Points[Index - 1], Points[Index], Color,
				NeedleUvRadius(Needle));
		}
	}
}

// 霧層折線段（robo/證據等舊管線語義）：以麥克筆半徑沿線每半徑 0.5 落一點——
// max 合成下聯集＝連續線，與舊 StampSegment 進霧層的讀感等價
void UInkCanvasComponent::StampMistSegment(const FVector2D& From, const FVector2D& To, const FLinearColor& Color, float Tier01) const
{
	FInkMistSurface* Surf = MistTargetOverride ? MistTargetOverride : MistSurface.Get();
	if (!Surf)
	{
		return;
	}
	Surf->Configure(MistToneCoolColor, MistToneCoolStrength, MistToneCurveGamma, MistGrainAmp, MistGrainPeriodPx);
	const float R = MarkerUvRadius;
	const float EdgeUv = 1.0f / FMath::Max(Surf->Resolution(), 1); // 1 紋素斜坡＝抗鋸齒
	const float Len = static_cast<float>((To - From).Size());
	const int32 Steps = FMath::Max(1, FMath::CeilToInt(Len / FMath::Max(R * 0.5f, 1e-6f)));
	for (int32 I = 0; I <= Steps; ++I)
	{
		const FVector2D P = From + (To - From) * (static_cast<float>(I) / Steps);
		Surf->StampDot(P, R, EdgeUv, Color, Tier01);
	}
}

// --- 稿線擋墨（2026-09-01；user 定案「稿線變成擋墨的牆」）---
//
// 填色掃到自己畫的稿線就停：手照樣自由亂掃、游標零干擾，邊界由稿線保證乾淨。
// 這不是新的否決機制——被擋的是**墨的落點**，不是游標，與既有的可畫域橢圓同一類
//（橢圓外收筆、游標不受影響）。
//
// 兩條刻意的限制：
//   ①**只擋同一位作者的稿線**——別人的框關不住你的墨（否則「畫個籠子把人圍起來」
//     就是零成本騷擾）；也符合心智模型「你的稿線關住你的墨」。
//   ②**只擋 Shader**——割線已經有沿稿自動走（吸附、不是牆），語義不同不可混。
// **v2＝半平面（09-02；v1 白刺定罪）**：v1 只在行進方向 ±0.3R 的窗內查、且只會把章
// 「橫向夾窄」——正對稿線衝過去時線落在窗外＝查不到，圓章前半直接壓過線 ⇒ 放射白刺
//（長度上限恰為一個半徑、方向垂直輪廓＝user 截圖的簽名）。削章模型只有一個自由度，
// 斜的、正面的牆構造上削不出來——**解不在那個參數空間裡**。v2：對每一枚章收集半徑
// R 內同作者稿線段的半平面（過最近點、法線＝章心→最近點；線段內部＝稿線本身的直線，
// 端點＝圓帽切面＝牆以圓角收尾不無限延伸），聚類取最近至多 2 個（轉角）。
void UInkCanvasComponent::ComputeStencilWallPlanes(int32 AuthorId, const FVector2D& CenterUV,
	const FVector2D& FrameDir, FNiInkWallPlane OutPlanes[2], int32& OutNum) const
{
	OutNum = 0;
	const float R = ShaderRowHalfWidthUv;
	if (AuthorId == INDEX_NONE || R <= 0.0f)
	{
		return;
	}
	const float RSq = R * R;

	// 聚類：法線差 <25° 視為同一面牆（同一條稿線的相鄰線段），每群只留最近者；
	// 8 群上限防退化（半徑 10mm 內塞不進更多真牆）。
	FNiInkWallPlane Cands[8];
	int32 NumCands = 0;
	const float SameWallCos = FMath::Cos(FMath::DegreesToRadians(25.0f));

	for (const FInkWork& W : Works)
	{
		if (W.State != EInkWorkState::Marker || W.AuthorId != AuthorId)
		{
			continue;
		}
		for (const FInkStroke& St : W.Strokes)
		{
			if (St.NeedleType != EInkNeedle::Stencil)
			{
				continue;
			}
			// **逐線段、不逐點**（09-01 首驗修）：逐點查會讓「落在兩個稿點之間」的
			// 章查不到牆＝把 bug 綁在稿點間距的旋鈕上；查線段＝與稿點密度無關。
			for (int32 Pi = 0; Pi + 1 < St.Points.Num(); ++Pi)
			{
				const FVector2D A = St.Points[Pi];
				const FVector2D B = St.Points[Pi + 1];
				if ((A.X - CenterUV.X > R && B.X - CenterUV.X > R) ||
					(CenterUV.X - A.X > R && CenterUV.X - B.X > R) ||
					(A.Y - CenterUV.Y > R && B.Y - CenterUV.Y > R) ||
					(CenterUV.Y - A.Y > R && CenterUV.Y - B.Y > R))
				{
					continue; // 粗篩：兩端在同一側且都離章 >R
				}
				const FVector2D AB = B - A;
				const float LenSq = static_cast<float>(AB.SizeSquared());
				float T = 0.0f;
				if (LenSq > 1e-16f)
				{
					T = FMath::Clamp(static_cast<float>(FVector2D::DotProduct(CenterUV - A, AB)) / LenSq, 0.0f, 1.0f);
				}
				const FVector2D Q = A + AB * T;
				const FVector2D ToQ = Q - CenterUV;
				const float DistSq = static_cast<float>(ToQ.SizeSquared());
				if (DistSq > RSq)
				{
					continue;
				}
				const float Dist = FMath::Sqrt(DistSq);
				FVector2D N;
				if (Dist > R * 0.02f)
				{
					N = ToQ / Dist;
				}
				else
				{
					// 章心正壓在稿線上＝法線不定：取線段法線、朝行進前方
					//（保留「來的那一側」——跨線的那一瞬各章各自守自己那側）
					const FVector2D SegPerp = FVector2D(-AB.Y, AB.X).GetSafeNormal();
					if (SegPerp.IsNearlyZero())
					{
						continue;
					}
					N = (FVector2D::DotProduct(SegPerp, FrameDir) >= 0.0) ? SegPerp : -SegPerp;
				}
				bool bMerged = false;
				for (int32 Ci = 0; Ci < NumCands; ++Ci)
				{
					if (FVector2D::DotProduct(Cands[Ci].NormalUv, N) > SameWallCos)
					{
						if (Dist < Cands[Ci].DistUv)
						{
							Cands[Ci].NormalUv = N;
							Cands[Ci].DistUv = Dist;
						}
						bMerged = true;
						break;
					}
				}
				if (!bMerged && NumCands < 8)
				{
					Cands[NumCands].NormalUv = N;
					Cands[NumCands].DistUv = Dist;
					++NumCands;
				}
			}
		}
	}

	// 依距離插入排序（≤8 元素）、取最近 2 群
	for (int32 I = 1; I < NumCands; ++I)
	{
		const FNiInkWallPlane Key = Cands[I];
		int32 J = I - 1;
		while (J >= 0 && Cands[J].DistUv > Key.DistUv)
		{
			Cands[J + 1] = Cands[J];
			--J;
		}
		Cands[J + 1] = Key;
	}
	OutNum = FMath::Min(NumCands, 2);
	for (int32 I = 0; I < OutNum; ++I)
	{
		OutPlanes[I] = Cands[I];
	}
}

void UInkCanvasComponent::EncodeWallPlanes(const FNiInkWallPlane* Planes, int32 Num,
	const FVector2D& FrameDir, float HalfWUv, uint8 Out[4])
{
	Out[0] = Out[1] = Out[2] = Out[3] = 0xFF;
	if (HalfWUv <= 0.0f)
	{
		return;
	}
	const FVector2D Perp(-FrameDir.Y, FrameDir.X);
	for (int32 I = 0; I < Num && I < 2; ++I)
	{
		const float CosA = static_cast<float>(FVector2D::DotProduct(Planes[I].NormalUv, FrameDir));
		const float SinA = static_cast<float>(FVector2D::DotProduct(Planes[I].NormalUv, Perp));
		float Ang = FMath::Atan2(SinA, CosA);
		if (Ang < 0.0f)
		{
			Ang += 2.0f * PI;
		}
		Out[I * 2 + 0] = static_cast<uint8>(FMath::RoundToInt(Ang / (2.0f * PI) * 256.0f) & 255);
		Out[I * 2 + 1] = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Planes[I].DistUv / HalfWUv * 254.0f), 0, 254));
	}
}

int32 UInkCanvasComponent::DecodeWallPlanes(const uint8* In4, const FVector2D& FrameDir,
	float HalfWUv, FNiInkWallPlane OutPlanes[2])
{
	if (!In4 || HalfWUv <= 0.0f)
	{
		return 0;
	}
	const FVector2D Perp(-FrameDir.Y, FrameDir.X);
	int32 Num = 0;
	for (int32 I = 0; I < 2; ++I)
	{
		if (In4[I * 2 + 1] == 0xFF)
		{
			continue;
		}
		const float Ang = static_cast<float>(In4[I * 2 + 0]) / 256.0f * 2.0f * PI;
		OutPlanes[Num].NormalUv = FrameDir * FMath::Cos(Ang) + Perp * FMath::Sin(Ang);
		OutPlanes[Num].DistUv = static_cast<float>(In4[I * 2 + 1]) / 254.0f * HalfWUv;
		++Num;
	}
	return Num;
}

void UInkCanvasComponent::StampNeedleDot(UCanvas* Canvas, const FVector2D& CanvasSize, const FVector2D& UV, const FLinearColor& Color, EInkNeedle Needle, const FVector2D* RowDirUv, float Flow, const uint8* Wall4, float Tier01) const
{
	if (Needle != EInkNeedle::Shader)
	{
		// Liner 忽略流量：割線=機器擁有速度（巡航恆速），濃度不是手的表達軸
		if (Canvas)
		{
			StampDot(Canvas, CanvasSize, UV, Color, MarkerUvRadius);
		}
		return;
	}
	StampMistRow(UV, Color, RowDirUv, Flow, Wall4, Tier01);
}

void UInkCanvasComponent::StampMistRow(const FVector2D& UV, const FLinearColor& Color, const FVector2D* RowDirUv, float Flow, const uint8* Wall4, float Tier01) const
{
	// 打霧的一枚章＝**圓形硬心軟邊**（2026-09-01 定形；09-02 改 CPU 光柵）。
	//
	// 為什麼是圓：舊排帶的兩端有 10mm 力臂，朝向由 2mm 位移正規化＝手一慢就是雜訊
	// ⇒ 梳齒；圓沒有「兩端」，聯集＝等距偏移曲線（扇貝 s²/8R＝0.05mm 看不見）
	// ＝平滑是構造保證。硬心軟邊＝地與暈し：漸層是**邊界現象**，面的內部深淺叫髒；
	// 反覆工作＝實心區往外長、暈開停在最前緣（真打霧手法）。
	//
	// 為什麼 CPU（09-02 分檔）：淡/中檔的均勻性要求「同檔重疊＝不變深」跨筆劃成立
	// ⇒ 合成必須 max，Canvas 給不了 ⇒ FInkMistSurface（全文見該檔頭）。
	// 圓章剖面直接逐像素解析求值（羽化 2mm≈2.5px 取樣充分）——比縮放貼圖更乾淨，
	// 十四版「烘在目的地解析度」的積分需求由解析求值天然滿足。
	FInkMistSurface* Surf = MistTargetOverride ? MistTargetOverride : MistSurface.Get();
	if (!Surf || !Surf->IsInited())
	{
		return;
	}
	Surf->Configure(MistToneCoolColor, MistToneCoolStrength, MistToneCurveGamma, MistGrainAmp, MistGrainPeriodPx);
	// 冷墨底色（預設 0＝關；見標頭）：半透明純黑疊暖膚=棕
	const FLinearColor Ink(
		FMath::Max(Color.R, ShaderMistCoolFloor.R),
		FMath::Max(Color.G, ShaderMistCoolFloor.G),
		FMath::Max(Color.B, ShaderMistCoolFloor.B));

	// 密度＝檔位 × 流量 × 濃度旋鈕（剖面在光柵器內乘）
	const float Density = FMath::Clamp(Flow, 0.0f, 1.0f)
		* FMath::Clamp(Tier01, 0.0f, 1.0f)
		* FMath::Clamp(ShaderStippleAlphaCenter, 0.01f, 1.0f);
	if (Density <= 0.0f)
	{
		return;
	}

	// 稿線擋墨 v2（09-02）：解碼本點的裁切半平面。Wall4 是落墨當下算好存進筆劃的
	//（見 FInkStroke::PointWall）——重建時讀回同一組數，稿線洗掉也不會膨脹。
	// 解碼框＝行進方向（首點=(1,0)）；live 與重放由同一份點序列推導＝一致。
	const FVector2D FrameDir = RowDirUv ? *RowDirUv : FVector2D(1.0, 0.0);
	FNiInkWallPlane WallPlanes[2];
	const int32 NumWallPlanes = DecodeWallPlanes(Wall4, FrameDir, ShaderRowHalfWidthUv, WallPlanes);

	// 縫區閘（07-24 跨縫制）：距 UV 縫 <2.5cm 的章改走表面補丁逐點落墨——
	// 平面圓章在縫上會被裁出直線界線＋汙染圖集隔壁島（viewport 實錘）
	if (RowDirUv)
	{
		if (UInkBodyComponent* Body = ResolveBody())
		{
			if (Body->IsUVNearSeam(UV) &&
				StampMistRowOnSurface(UV, Ink, *RowDirUv, Density, WallPlanes, NumWallPlanes))
			{
				return;
			}
		}
	}

	// 剖面幾何：羽化寬（mm）→ 半徑比例（與退役貼圖同式）
	const float RadiusMm = FMath::Max(ShaderRowHalfWidthUv, 1e-5f) / 0.000301f;
	const float FeatherFrac = FMath::Clamp(ShaderFillFeatherMm / FMath::Max(RadiusMm, 0.01f), 0.02f, 1.0f);

	// 牆平面轉光柵器型別（同語義、零耦合的型別橋）
	FInkMistSurface::FPlane SurfPlanes[2];
	for (int32 I = 0; I < NumWallPlanes; ++I)
	{
		SurfPlanes[I].NormalUv = WallPlanes[I].NormalUv;
		SurfPlanes[I].DistUv = WallPlanes[I].DistUv;
	}
	Surf->StampDisc(UV, ShaderRowHalfWidthUv, FeatherFrac, Ink, Density,
		NumWallPlanes > 0 ? SurfPlanes : nullptr, NumWallPlanes);
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

bool UInkCanvasComponent::StampMistRowOnSurface(const FVector2D& UV, const FLinearColor& Ink, const FVector2D& RowDirUv, float Density, const FNiInkWallPlane* WallPlanes, int32 NumWallPlanes) const
{
	// 縫區排章：以排心為種子沿皮膚 BFS 攤平（補丁=cm 平面、跨縫連續），每顆針點
	// 在攤平面上定位再映回 UV0——點落在縫哪一側由「真實表面」決定，兩側自動接續、
	// 圖集上排在隔壁的無關島構造上碰不到。補丁在相鄰排之間快取（中心移 >0.5cm 重建）。
	FInkMistSurface* Surf = MistTargetOverride ? MistTargetOverride : MistSurface.Get();
	UInkBodyComponent* Body = ResolveBody();
	if (!Body || !Surf || !Surf->IsInited())
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

	// **縫區也是圓章**（2026-09-01；09-02 改 CPU 光柵）：平面路徑貼一枚圓形硬心軟邊；
	// 這裡因為要跨縫，改成把同一個圓在**攤平面上以同心環取樣**、每個樣本各自映回
	// UV0 落一顆點。點落在縫哪一側由真實表面決定 ⇒ 兩側自動接續、鄰島碰不到。
	//
	// max 合成（09-02）讓兩隻舊病一起消滅：①軟章疊合坑疤（09-01 首驗血價）——
	// 重疊點取最大＝剖面本身，不會堆疊；②over 反解 1−(1−t)^(1/N) 隨之退役——
	// 每顆點直接寫目標濃度。
	const float RadCm = ShaderRowHalfWidthUv * CmPerUv;
	const float RadMm = ShaderRowHalfWidthUv / 0.000301f;
	const float FeatherFrac = FMath::Clamp(ShaderFillFeatherMm / FMath::Max(RadMm, 0.01f), 0.02f, 1.0f);
	const float CoreFrac = 1.0f - FeatherFrac;

	constexpr int32 Rings = 5;                        // 含中心點（環距 = R/4）
	const float RingStepCm = RadCm / FMath::Max(Rings - 1, 1);
	const float RingStepUv = RingStepCm / FMath::Max(CmPerUv, 1e-6f);
	// 腳印 = 2.5×環距 ⇒ 相鄰點重疊充分＝max 聯集融成連續面
	const float DotRadUv = FMath::Max(1.5f / Surf->Resolution(), 1.25f * RingStepUv);
	const float DotEdgeUv = 1.0f / Surf->Resolution(); // 1 紋素斜坡＝抗鋸齒

	for (int32 Ring = 0; Ring < Rings; ++Ring)
	{
		const float R01 = (Rings > 1) ? (static_cast<float>(Ring) / (Rings - 1)) : 0.0f;
		// 徑向剖面＝與平面圓章同式（硬心＋smoothstep 軟邊）
		float Prof;
		if (R01 <= CoreFrac)
		{
			Prof = 1.0f;
		}
		else
		{
			const float T = FMath::Clamp((1.0f - R01) / FMath::Max(FeatherFrac, 1e-4f), 0.0f, 1.0f);
			Prof = T * T * (3.0f - 2.0f * T);
		}
		const float Target = Prof * Density;
		if (Target <= 0.004f)
		{
			continue;
		}
		const int32 Count = (Ring == 0) ? 1
			: FMath::Max(6, FMath::RoundToInt(2.0f * PI * R01 * RadCm / RingStepCm));
		for (int32 i = 0; i < Count; ++i)
		{
			const float Ang = (Count > 1) ? (2.0f * PI * i / Count) : 0.0f;
			// 局部座標：u＝沿行進、v＝橫越
			const float LocalU = FMath::Cos(Ang) * R01 * RadCm;
			const float LocalV = FMath::Sin(Ang) * R01 * RadCm;
			const FVector2D ChartPt = CenterChart + DirC * LocalU + PerpC * LocalV;
			FVector2D DotUV;
			if (!SeamPatch.ChartToUV0(ChartPt, DotUV))
			{
				continue; // 補丁外（剪影邊/褌洞）＝誠實不落墨
			}
			// 稿線牆 v2（09-02 二修）：測**實際落點**（DotUV−章心），不測局部座標的
			// 理論落點——chart→UV 逐 tri 各自仿射，折疊/高扭曲處兩者可以差一整顆章
			// ⇒ 用理論落點測＝漏（c6 首驗縫區漏 26%、深達一個半徑實錘）。跨島跳走
			// 的點（|offset|>3R）牆概念不適用、也落不到牆邊＝不測。點腳印半徑扣進
			// 距離＝腳印也不越線（planar 路是硬裁，點章語義對齊）。
			if (NumWallPlanes > 0)
			{
				const FVector2D OffUv = DotUV - UV;
				const float R3 = 3.0f * ShaderRowHalfWidthUv;
				if (static_cast<float>(OffUv.SizeSquared()) <= R3 * R3)
				{
					bool bBlocked = false;
					for (int32 Wi = 0; Wi < NumWallPlanes; ++Wi)
					{
						if (static_cast<float>(FVector2D::DotProduct(OffUv, WallPlanes[Wi].NormalUv)) > WallPlanes[Wi].DistUv - DotRadUv)
						{
							bBlocked = true;
							break;
						}
					}
					if (bBlocked)
					{
						continue; // 牆外＝不落墨
					}
				}
			}
			Surf->StampDot(DotUV, DotRadUv, DotEdgeUv, Ink, Target);
		}
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

	// 墨的直徑（α=0.5 等值面）＝舊制原樣；tile 再放大到足以裝下羽化帶——
	// 兩者的比值由筆頭的形狀常數決定，所以**線寬不隨這一刀改變**（見檔頭註）
	const float InkDiameter = FMath::Max(2.0f, UvRadius * 2.0f * CanvasSize.X);
	const float Diameter = InkDiameter * (NibLegacyHalfAlphaFrac / NibHalfAlphaFrac);
	const FVector2D TopLeft(UV.X * CanvasSize.X - Diameter * 0.5f, UV.Y * CanvasSize.Y - Diameter * 0.5f);

	FCanvasTileItem TileItem(TopLeft, Nib->GetResource(), FVector2D(Diameter, Diameter), Color);
	TileItem.BlendMode = SE_BLEND_AlphaComposite; // premultiplied over：RGB 與 alpha 皆累積
	Canvas->DrawItem(TileItem);
}

void UInkCanvasComponent::StampIntoLayerRT(const FVector2D& From, const FVector2D& To, const FLinearColor& Color, bool bDotOnly, EInkNeedle Needle, const FVector2D* RowDirUv, float Flow, const uint8* Wall4, float Tier01)
{
	// 針型路由：Liner→線層（GPU canvas；銳化咬=脆的實心墨）；Shader→霧層
	//（09-02 起＝CPU 光柵器；獨立層=銳化不咬——兩種材質語義分層的結構保證）
	// 惰性配置：真的有墨要落才配那一層（受害者入睡已預熱＝這裡通常是命中）
	if (Needle == EInkNeedle::Shader)
	{
		if (!EnsureMistLayer())
		{
			return;
		}
		const float Res = static_cast<float>(MistSurface->Resolution());
		if (bDotOnly)
		{
			StampNeedleDot(nullptr, FVector2D(Res, Res), To, Color, Needle, RowDirUv, Flow, Wall4, Tier01);
		}
		else
		{
			StampMistSegment(From, To, Color, Tier01);
		}
		// 批內只累積髒區、EndStampBatch 一次上傳；非批（robo/證據直呼）即時上傳
		if (!bMistBatchOpen)
		{
			FlushMistUpload();
		}
		return;
	}

	UTextureRenderTarget2D* LayerRT = EnsureMarkerRT();
	if (!LayerRT)
	{
		return;
	}

	// 批次快路徑：同層已開 context → 直畫（每點開關 4096 context=幀率殺手）
	if (BatchCanvas && BatchRT == LayerRT)
	{
		if (bDotOnly)
		{
			StampNeedleDot(BatchCanvas, BatchCanvasSize, To, Color, Needle, RowDirUv, Flow, Wall4, Tier01);
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
			StampNeedleDot(Canvas, CanvasSize, To, Color, Needle, RowDirUv, Flow, Wall4, Tier01);
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
	if (BatchCanvas || bMistBatchOpen)
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
	if (Needle == EInkNeedle::Shader)
	{
		// 霧層批次（CPU 光柵）：批內只寫緩衝累積髒區、EndStampBatch 一次上傳
		if (EnsureMistLayer())
		{
			bMistBatchOpen = true;
		}
		return;
	}
	UTextureRenderTarget2D* LayerRT = EnsureMarkerRT();
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
	if (bMistBatchOpen)
	{
		bMistBatchOpen = false;
		FlushMistUpload();
	}
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

bool UInkCanvasComponent::ExportLayersToPng(const FString& AbsolutePathPrefix)
{
	// 惰性配置制：傾印是 QA/robo 路徑，先把三層補齊＝契約仍恆得到三個檔
	//（新配的層是全透明＝與「舊制配了但沒畫過」的內容逐位相同）
	EnsureMarkerRT();
	EnsureTattooRT();
	EnsureMistLayer();

	const bool bMarkerSaved = SaveRTToPng(MarkerRT, AbsolutePathPrefix + TEXT("_marker.png"));
	const bool bTattooSaved = SaveRTToPng(TattooRT, AbsolutePathPrefix + TEXT("_tattoo.png"));

	// 霧層＝直接吐 CPU 權威緩衝（BGRA 預乘 sRGB＝與舊 RT dump 同語義、零 GPU 往返）；
	// 有罩染時吐「罩染 over 基底」的合成結果＝dump 恆等於顯示內容
	bool bMistSaved = false;
	if (MistSurface.IsValid() && MistSurface->IsInited())
	{
		const int32 Res = MistSurface->Resolution();
		const FColor* PixelData = MistSurface->Data();
		TArray64<FColor> Composed;
		if (MistSurface->HasGlaze())
		{
			Composed.SetNumUninitialized(static_cast<int64>(Res) * Res);
			MistSurface->ComposeInto(Composed.GetData(), Res, FIntRect(0, 0, Res, Res));
			PixelData = Composed.GetData();
		}
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(AbsolutePathPrefix), true);
		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
		if (ImageWrapper.IsValid())
		{
			ImageWrapper->SetRaw(PixelData, static_cast<int64>(Res) * Res * sizeof(FColor), Res, Res, ERGBFormat::BGRA, 8);
			const TArray64<uint8>& CompressedData = ImageWrapper->GetCompressed(90);
			TArray<uint8> SaveData;
			SaveData.Append(CompressedData.GetData(), CompressedData.Num());
			bMistSaved = FFileHelper::SaveArrayToFile(SaveData, *(AbsolutePathPrefix + TEXT("_mist.png")));
		}
	}
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
