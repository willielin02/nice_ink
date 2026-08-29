#include "NiceInkTvSet.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/CanvasRenderTarget2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "ImageUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "NiceInkGameMode.h"
#include "NiceInkGameState.h"
#include "NiceInkTvBroadcast.h"
#include "NiceInkTypes.h"
#include "UObject/ConstructorHelpers.h"

ANiceInkTvSet::ANiceInkTvSet()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true; // server spawn → 各端都有；視覺各端自算（零複製欄位）
	SetReplicateMovement(false);

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// 櫃體＝Radiola（ConstructorHelpers 硬引用＝CDO 持有 ⇒ 打包自動 cook）。
	// 螢幕＝網格自己的 'TvGlass' 材質槽（tv_radiola_prep.py 拆島＋UV 歸一化），
	// BeginPlay 對該槽建 MID 餵 ScreenRT。
	static ConstructorHelpers::FObjectFinder<UStaticMesh> RadiolaMesh(
		TEXT("/Game/Props/SM_TvRadiola.SM_TvRadiola"));

	CabinetMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Cabinet"));
	CabinetMesh->SetupAttachment(Root);
	if (RadiolaMesh.Succeeded())
	{
		CabinetMesh->SetStaticMesh(RadiolaMesh.Object);
	}
	CabinetMesh->SetCollisionProfileName(TEXT("BlockAll")); // 家具：場地探針打得到
	CabinetMesh->SetCanEverAffectNavigation(false);
}

void ANiceInkTvSet::BeginPlay()
{
	Super::BeginPlay();
	ScreenRT = UCanvasRenderTarget2D::CreateCanvasRenderTarget2D(
		this, UCanvasRenderTarget2D::StaticClass(), 512, 384);
	if (ScreenRT)
	{
		ScreenRT->ClearColor = FLinearColor::Black;
		ScreenRT->OnCanvasRenderTargetUpdate.AddDynamic(this, &ANiceInkTvSet::DrawScreen);
	}
	EnsureFilmTexture();
	if (CabinetMesh && CabinetMesh->GetStaticMesh())
	{
		const int32 GlassIdx = CabinetMesh->GetMaterialIndex(TEXT("TvGlass"));
		if (GlassIdx != INDEX_NONE)
		{
			ScreenMid = CabinetMesh->CreateDynamicMaterialInstance(GlassIdx);
			if (ScreenMid && ScreenRT)
			{
				ScreenMid->SetTextureParameterValue(TEXT("ScreenTex"), ScreenRT);
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("NiTvSet: TvGlass slot missing on SM_TvRadiola"));
		}
	}
}

void ANiceInkTvSet::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const ANiceInkGameState* GS = GetWorld() ? GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
	if (!GS || !ScreenRT)
	{
		return;
	}

	// 螢幕狀態＝(step, t) 純函式：開場前兩拍播放、TvOff 拍收線、其後永遠黑
	const ENiCeremonyStep Step = GS->CeremonyStep;
	float WantOn = 0.0f, WantCollapse = 1.0f;
	if (Step == ENiCeremonyStep::IntroSit || Step == ENiCeremonyStep::IntroNotice)
	{
		WantOn = 1.0f;
		WantCollapse = 0.0f;
	}
	else if (Step == ENiCeremonyStep::IntroTvOff)
	{
		WantOn = 1.0f;
		WantCollapse = GS->GetCeremonyAlpha();
	}
	const bool bChanged = !FMath::IsNearlyEqual(WantOn, ScreenOn01) ||
		!FMath::IsNearlyEqual(WantCollapse, Collapse01, 0.002f);
	ScreenOn01 = WantOn;
	Collapse01 = WantCollapse;

	// 重畫率＝NiceInkTvFilm::RedrawHz（黑屏後不再重畫）。
	// **12Hz 本身就是一個「膠片」的選擇**（電影 24p 的頓挫）；08-29 改判為新聞畫面之後
	// 提到 24——電視是 60i，動態比膠片滑順，頓挫會把它讀回老電影。成本＝12,288 個
	// 像素的純 CPU 光柵翻倍（微不足道）。
	const double Now = GetWorld()->GetTimeSeconds();
	const bool bDue = (Now - LastDrawTime) > (1.0 / NiceInkTvFilm::RedrawHz);
	if ((ScreenOn01 > 0.0f && bDue) || bChanged)
	{
		LastDrawTime = Now;
		// 節目的分鏡由儀式時間軸決定（不是牆鐘）＝(step, alpha) 的純函式。
		// 08-28 二修：**番組本体は寄ってから頭出しで全部流す**（user 指定「讓玩家可以
		// 在電視前完整看完」）⇒ IntroSit は「点いている」だけ、IntroNotice で五分鏡。
		// StepSeconds を渡すのは物理速度（拍・揺れ）を鏡長から切り離すため。
		NiceInkTvFilm::ResolveShot(Step, GS->GetCeremonyAlpha(), GS->CeremonyStepDuration, Now,
			FilmShot, FilmU, FilmSeconds);
		FilmFrameNo = FMath::FloorToInt(Now * NiceInkTvFilm::RedrawHz);
		ScreenRT->UpdateResource(); // 觸發 OnCanvasRenderTargetUpdate → DrawScreen
	}
}

void ANiceInkTvSet::EnsureFilmTexture()
{
	if (FilmTex)
	{
		return;
	}
	FilmTex = UTexture2D::CreateTransient(NiceInkTvFilm::FilmW, NiceInkTvFilm::FilmH,
		PF_B8G8R8A8, TEXT("NiTvFilm"));
	if (!FilmTex)
	{
		return;
	}
	FilmTex->SRGB = true;          // 影格在 sRGB byte 域作畫（色域校準見 NiceInkTvBroadcast.cpp 檔頭）
	FilmTex->Filter = TF_Nearest;  // ★「低解析度」的承重設定：雙線性會把方塊糊成漸層＝功虧一簣
	FilmTex->AddressX = TA_Clamp;
	FilmTex->AddressY = TA_Clamp;
	FilmTex->NeverStream = true;
	FilmTex->UpdateResource();
}

void ANiceInkTvSet::UploadFilmFrame()
{
	EnsureFilmTexture();
	if (!FilmTex)
	{
		return;
	}
	if (FilmBuf.Num() != NiceInkTvFilm::FilmPx)
	{
		FilmBuf.SetNumUninitialized(NiceInkTvFilm::FilmPx);
	}
	NiceInkTvFilm::RenderFrame(FilmShot, FilmU, FilmSeconds, FilmFrameNo, FilmBuf.GetData());

	// UpdateTextureRegions 是非同步的（render thread 消費）⇒ 來源必須自己配一份、
	// 由 cleanup 回收；本呼叫發生在 canvas 建構期間，因此排在 canvas flush 之前＝
	// 這一幀畫出來的就是這一格（不會慢一幀）。
	const uint32 Pitch = NiceInkTvFilm::FilmW * sizeof(FColor);
	const uint32 Bytes = Pitch * NiceInkTvFilm::FilmH;
	uint8* Copy = static_cast<uint8*>(FMemory::Malloc(Bytes));
	FMemory::Memcpy(Copy, FilmBuf.GetData(), Bytes);
	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(
		0, 0, 0, 0, NiceInkTvFilm::FilmW, NiceInkTvFilm::FilmH);
	FilmTex->UpdateTextureRegions(0, 1, Region, Pitch, 4, Copy,
		[](uint8* Data, const FUpdateTextureRegion2D* Regions)
		{
			FMemory::Free(Data);
			delete Regions;
		});
}

void ANiceInkTvSet::DrawScreen(UCanvas* Canvas, int32 Width, int32 Height)
{
	if (!Canvas)
	{
		return;
	}
	DrawGlassBase(Canvas, Width, Height);

	if (ScreenOn01 <= 0.0f)
	{
		DrawTubeVignette(Canvas, Width, Height);
		return;
	}

	UploadFilmFrame();

	if (Collapse01 <= 0.0f)
	{
		// 水平同期の微揺れ：±1px の横ぶれ（アナログの不安定さ。無いと妙に「デジタル」に見える）
		const float Jitter = static_cast<float>((FilmFrameNo * 7) % 3) - 1.0f;
		DrawFilmTile(Canvas, Jitter, 0.0f, static_cast<float>(Width), static_cast<float>(Height), 1.0f);
		DrawScanlines(Canvas, Width, Height);
		DrawTubeVignette(Canvas, Width, Height);
		return;
	}

	// 關機動畫（映像管經典）：前 55%＝畫面**本身**被垂直壓成一條線（不是拿黑帶蓋掉——
	// 被壓扁的是最後那一格影像，這才是真的映像管讀感）；後 45%＝白線橫向縮到點滅掉。
	const float T = FMath::Clamp(Collapse01, 0.0f, 1.0f);
	if (T < 0.55f)
	{
		const float K = T / 0.55f;
		const float BandH = FMath::Lerp(static_cast<float>(Height), 3.0f, K * K); // 加速潰縮
		const float Bright = FMath::Lerp(1.0f, 2.6f, K);                          // 壓縮＝能量集中＝變亮
		DrawFilmTile(Canvas, 0.0f, (Height - BandH) * 0.5f, static_cast<float>(Width), BandH, Bright);
		if (K > 0.5f) // 最後殘留的白芯
		{
			FCanvasTileItem Core(FVector2D(0, Height * 0.5f - 1.5f), FVector2D(Width, 3.0f),
				FLinearColor(2.4f, 2.4f, 2.4f, (K - 0.5f) / 0.5f));
			Core.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(Core);
		}
	}
	else
	{
		const float K = (T - 0.55f) / 0.45f;
		const float BandW = FMath::Lerp(static_cast<float>(Width), 0.0f, K);
		FCanvasTileItem Band(FVector2D((Width - BandW) * 0.5f, Height * 0.5f - 1.5f),
			FVector2D(BandW, 3.0f), FLinearColor(2.4f, 2.4f, 2.4f, 1.0f));
		Band.BlendMode = SE_BLEND_Opaque;
		Canvas->DrawItem(Band);
	}
	DrawTubeVignette(Canvas, Width, Height);
}

void ANiceInkTvSet::DrawFilmTile(UCanvas* Canvas, float X, float Y, float W, float H, float Bright) const
{
	if (!FilmTex || !FilmTex->GetResource())
	{
		return;
	}
	// 128×96 → 512×384 的 4× 放大，取樣器是 TF_Nearest ⇒ 硬邊方塊像素。
	FCanvasTileItem Tile(FVector2D(X, Y), FilmTex->GetResource(), FVector2D(W, H),
		FVector2D(0.0f, 0.0f), FVector2D(1.0f, 1.0f),
		FLinearColor(Bright, Bright, Bright, 1.0f));
	Tile.BlendMode = SE_BLEND_Opaque;
	Canvas->DrawItem(Tile);
}

void ANiceInkTvSet::DrawScanlines(UCanvas* Canvas, int32 W, int32 H) const
{
	// 走査線は**影格像素と同じ 4px 週期**に合わせる：ずらすと表示側の縮小と干渉して
	// モアレでチラつく（512→畫面 ~290px＝0.57 倍縮小）。合わせておけば
	// 「一つの像素行につき一本の暗線」＝ピクセルの塊と走査線が同じ拍を打つ。
	for (int32 Y = 0; Y < H; Y += 4)
	{
		FCanvasTileItem Line(FVector2D(0, Y), FVector2D(W, 1.0f),
			FLinearColor(0.0f, 0.0f, 0.0f, 0.24f));
		Line.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Line);
	}
	// シャドウマスク（縦の粒）：薄く。強くすると格子に見えて液晶っぽくなる。
	for (int32 X = 0; X < W; X += 4)
	{
		FCanvasTileItem Col(FVector2D(X, 0), FVector2D(1.0f, H),
			FLinearColor(0.0f, 0.0f, 0.0f, 0.07f));
		Col.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Col);
	}
}

void ANiceInkTvSet::DrawGlassBase(UCanvas* Canvas, int32 W, int32 H) const
{
	// 熄滅態＝淡灰綠玻璃（對照 Sketchfab 原作：玻璃明顯比深棕圍板亮——
	// 亮度差就是「這裡是玻璃」的判讀依據；08-28 前一版深灰跟圍板同亮度帶，
	// user 抓「分不清螢幕邊界」）。上亮下暗漸層＝玻璃反光讀感。
	for (int32 Band = 0; Band < 8; ++Band)
	{
		const float K = Band / 7.0f;
		const float V = FMath::Lerp(0.165f, 0.095f, K);
		FCanvasTileItem Bg(FVector2D(0, H * Band / 8.0f),
			FVector2D(W, H / 8.0f + 1.0f),
			FLinearColor(V * 0.93f, V, V * 0.88f, 1.0f)); // 偏綠＝映像管螢光粉
		Bg.BlendMode = SE_BLEND_Opaque;
		Canvas->DrawItem(Bg);
	}
}

void ANiceInkTvSet::DrawTubeVignette(UCanvas* Canvas, int32 W, int32 H) const
{
	// 映像管暗角：玻璃邊緣往內三圈漸暗——在玻璃邊界本身製造亮度落差，
	// 無論播放或熄滅，「螢幕到這裡為止」都讀得出來（順帶＝年代感的角落衰減）。
	const float BandPx[3] = { 14.0f, 8.0f, 4.0f };
	const float BandA[3] = { 0.30f, 0.24f, 0.18f };
	float Inset = 0.0f;
	for (int32 i = 0; i < 3; ++i)
	{
		const FLinearColor C(0.0f, 0.0f, 0.0f, BandA[i]);
		FCanvasTileItem Top(FVector2D(Inset, Inset), FVector2D(W - 2 * Inset, BandPx[i]), C);
		FCanvasTileItem Bot(FVector2D(Inset, H - Inset - BandPx[i]), FVector2D(W - 2 * Inset, BandPx[i]), C);
		FCanvasTileItem Lft(FVector2D(Inset, Inset), FVector2D(BandPx[i], H - 2 * Inset), C);
		FCanvasTileItem Rgt(FVector2D(W - Inset - BandPx[i], Inset), FVector2D(BandPx[i], H - 2 * Inset), C);
		for (FCanvasTileItem* T : { &Top, &Bot, &Lft, &Rgt })
		{
			T->BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(*T);
		}
		Inset += BandPx[i];
	}
}

void ANiceInkTvSet::DumpFilmContactSheet(const FString& OutPath, int32 UpScale)
{
	using namespace NiceInkTvFilm;
	// 橫＝時點、縱＝分鏡。2× 放大 ≈ user 在特寫時看到的真實表觀大小
	//（RT 512 貼到畫面約 290px ⇒ 一個影格像素 ≈ 2.3 螢幕像素），所以這張圖
	// 判讀「讀不讀得出來」是有效的，不是放大好看用的。
	static const float Us[] = { 0.05f, 0.25f, 0.45f, 0.65f, 0.85f, 0.98f };
	const int32 Cols = UE_ARRAY_COUNT(Us), Rows = Shot_Num, Gut = 2;
	UpScale = FMath::Clamp(UpScale, 1, 6);

	const int32 SheetW = (Cols * FilmW + (Cols + 1) * Gut) * UpScale;
	const int32 SheetH = (Rows * FilmH + (Rows + 1) * Gut) * UpScale;
	TArray<FColor> Sheet;
	Sheet.Init(FColor(24, 24, 28, 255), SheetW * SheetH);

	TArray<FColor> Frame;
	Frame.SetNumUninitialized(FilmPx);
	for (int32 R = 0; R < Rows; ++R)
	{
		for (int32 Cn = 0; Cn < Cols; ++Cn)
		{
			RenderFrame(R, Us[Cn], Us[Cn] * 2.3f, R * 7 + Cn * 3, Frame.GetData()); // 公称鏡長 2.3s
			const int32 Ox = (Gut + Cn * (FilmW + Gut)) * UpScale;
			const int32 Oy = (Gut + R * (FilmH + Gut)) * UpScale;
			for (int32 Y = 0; Y < FilmH * UpScale; ++Y)
			{
				for (int32 X = 0; X < FilmW * UpScale; ++X)
				{
					Sheet[(Oy + Y) * SheetW + (Ox + X)] = Frame[(Y / UpScale) * FilmW + (X / UpScale)];
				}
			}
		}
	}

	TArray64<uint8> Png;
	FImageUtils::PNGCompressImageArray(SheetW, SheetH, TArrayView64<const FColor>(Sheet.GetData(), Sheet.Num()), Png);
	const bool bOk = FFileHelper::SaveArrayToFile(Png, *OutPath);
	UE_LOG(LogTemp, Warning, TEXT("NiTvFilmSheet: %s %dx%d -> %s"),
		bOk ? TEXT("WROTE") : TEXT("FAILED"), SheetW, SheetH, *OutPath);
}

void ANiceInkTvSet::DumpFilmFrames(const FString& OutDir, int32 UpScale, int32 PerShot,
	float NoticeSeconds)
{
	using namespace NiceInkTvFilm;
	// 分鏡の実尺＝ResolveShot の比率表と同じ割り（0.14/0.20/0.21/0.24/0.21）。
	// **同じ数字が二箇所にある**＝片方を変えたらもう片方が嘘になる。比率を触るときは
	// ResolveShot の Lo/Hi と一緒に直すこと（傾印は「実際に流れる一格」であることが取り柄）。
	static const float Frac[Shot_Num] = { 0.14f, 0.20f, 0.21f, 0.24f, 0.21f };
	static const TCHAR* Name[Shot_Num] = { TEXT("1_yomatsuri"), TEXT("2_taiko"),
		TEXT("3_mikoshi"), TEXT("4_reveal"), TEXT("5_turn") };

	if (NoticeSeconds <= 0.0f) // 尺は GameMode が持つ。ここで写しを持つと必ず古くなる
	{
		const ANiceInkGameMode* GM = GetDefault<ANiceInkGameMode>();
		NoticeSeconds = GM ? GM->IntroNoticeSeconds : 15.6f;
	}
	UpScale = FMath::Clamp(UpScale, 1, 10);
	PerShot = FMath::Clamp(PerShot, 2, 32);
	IFileManager::Get().MakeDirectory(*OutDir, true);

	TArray<FColor> Frame;
	Frame.SetNumUninitialized(FilmPx);
	const int32 OutW = FilmW * UpScale, OutH = FilmH * UpScale;
	TArray<FColor> Big;
	Big.SetNumUninitialized(OutW * OutH);

	int32 Written = 0;
	for (int32 ShotIdx = 0; ShotIdx < Shot_Num; ++ShotIdx)
	{
		const float ShotSeconds = FMath::Max(Frac[ShotIdx] * NoticeSeconds, 0.01f);
		for (int32 k = 0; k < PerShot; ++k)
		{
			const float U = static_cast<float>(k) / (PerShot - 1);
			const float Seconds = U * ShotSeconds;
			// FrameNo＝実影格番号（RedrawHz）⇒ 粒子・ハムバー・時刻も実演と同じ
			const int32 FrameNo = FMath::RoundToInt(Seconds * NiceInkTvFilm::RedrawHz);
			RenderFrame(ShotIdx, U, Seconds, FrameNo, Frame.GetData());

			for (int32 Y = 0; Y < OutH; ++Y)
			{
				for (int32 X = 0; X < OutW; ++X)
				{
					Big[Y * OutW + X] = Frame[(Y / UpScale) * FilmW + (X / UpScale)];
				}
			}
			TArray64<uint8> Png;
			FImageUtils::PNGCompressImageArray(OutW, OutH,
				TArrayView64<const FColor>(Big.GetData(), Big.Num()), Png);
			const FString File = FString::Printf(TEXT("%s/shot%s_%02d_u%03d_t%04dms.png"),
				*OutDir, Name[ShotIdx], k, FMath::RoundToInt(U * 100.0f),
				FMath::RoundToInt(Seconds * 1000.0f));
			if (FFileHelper::SaveArrayToFile(Png, *File)) { ++Written; }
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("NiTvFilmFrames: wrote %d frames (%dx%d) -> %s"),
		Written, OutW, OutH, *OutDir);
}
