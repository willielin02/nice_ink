#include "NiceInkTvSet.h"

#include "Components/StaticMeshComponent.h"
#include "DreamTraceMotifData.h"
#include "Engine/Canvas.h"
#include "Engine/CanvasRenderTarget2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "NiceInkGameState.h"
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

	// 12Hz 更新（映像管的年代感本來就不是 60fps；黑屏後不再重畫）
	const double Now = GetWorld()->GetTimeSeconds();
	const bool bDue = (Now - LastDrawTime) > (1.0 / 12.0);
	if ((ScreenOn01 > 0.0f && bDue) || bChanged)
	{
		LastDrawTime = Now;
		ScreenRT->UpdateResource(); // 觸發 OnCanvasRenderTargetUpdate → DrawScreen
	}
}

void ANiceInkTvSet::DrawScreen(UCanvas* Canvas, int32 Width, int32 Height)
{
	if (!Canvas)
	{
		return;
	}
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	// 底：熄滅態＝暗灰玻璃（純黑 RT 乘上材質增益還是純黑＝黑洞；
	// 真映像管關機是深灰綠玻璃面，略帶上亮下暗漸層）
	// 熄滅態＝淡灰綠玻璃（對照 Sketchfab 原作：玻璃明顯比深棕圍板亮——
	// 亮度差就是「這裡是玻璃」的判讀依據；08-28 前一版深灰跟圍板同亮度帶，
	// user 抓「分不清螢幕邊界」）。上亮下暗漸層＝玻璃反光讀感。
	for (int32 Band = 0; Band < 8; ++Band)
	{
		const float K = Band / 7.0f;
		const float V = FMath::Lerp(0.165f, 0.095f, K);
		FCanvasTileItem Bg(FVector2D(0, Height * Band / 8.0f),
			FVector2D(Width, Height / 8.0f + 1.0f),
			FLinearColor(V * 0.93f, V, V * 0.88f, 1.0f)); // 偏綠＝映像管螢光粉
		Bg.BlendMode = SE_BLEND_Opaque;
		Canvas->DrawItem(Bg);
	}

	if (ScreenOn01 <= 0.0f)
	{
		DrawTubeVignette(Canvas, Width, Height);
		return;
	}

	if (Collapse01 <= 0.0f)
	{
		DrawBroadcastFrame(Canvas, Width, Height, Now);
		return;
	}

	// 關機動畫（映像管經典）：前 55%＝畫面垂直壓成一條白線；後 45%＝白線橫向縮到點滅掉
	const float T = FMath::Clamp(Collapse01, 0.0f, 1.0f);
	if (T < 0.55f)
	{
		const float K = T / 0.55f;
		const float BandH = FMath::Lerp(static_cast<float>(Height), 3.0f, K);
		const float Bright = FMath::Lerp(1.0f, 2.4f, K); // 壓縮＝能量集中＝變亮
		FCanvasTileItem Band(FVector2D(0, (Height - BandH) * 0.5f),
			FVector2D(Width, BandH), FLinearColor(Bright, Bright, Bright, 1.0f));
		Band.BlendMode = SE_BLEND_Opaque;
		Canvas->DrawItem(Band);
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
}

void ANiceInkTvSet::DrawBroadcastFrame(UCanvas* Canvas, int32 W, int32 H, double Now) const
{
	// 「極道刺青特輯」：深藍演播底、皮膚色的背部剪影、輪播和彫紋樣。
	// 紋樣＝DreamTrace motif 資料原樣（電視上的圖＝玩家夢裡要描的圖＝主題閉環）。
	const float Flicker = 0.92f + 0.08f * FMath::Sin(Now * 37.0);

	// 演播背景（深藍）＋暗角
	FCanvasTileItem Studio(FVector2D(0, 0), FVector2D(W, H),
		FLinearColor(0.085f, 0.115f, 0.30f) * Flicker);
	Studio.BlendMode = SE_BLEND_Opaque;
	Canvas->DrawItem(Studio);

	// 背部剪影：肩寬臀窄的圓角梯形（三角扇；解析輪廓＝零資產）
	const float Cx = W * 0.5f, TopY = H * 0.16f, BotY = H * 0.97f;
	const float HalfTop = W * 0.34f, HalfBot = W * 0.26f, NeckR = W * 0.075f;
	const FLinearColor Skin = FLinearColor(0.66f, 0.47f, 0.36f) * Flicker;
	TArray<FCanvasUVTri> Tris;
	auto AddTri = [&](const FVector2D& A, const FVector2D& B, const FVector2D& C) {
		FCanvasUVTri T;
		T.V0_Pos = A; T.V1_Pos = B; T.V2_Pos = C;
		T.V0_Color = T.V1_Color = T.V2_Color = Skin;
		Tris.Add(T);
	};
	// 軀幹側緣＝弧（8 段折線近似）；中心扇出
	const FVector2D Pivot(Cx, (TopY + BotY) * 0.5f);
	TArray<FVector2D> Outline;
	for (int32 i = 0; i <= 8; ++i)
	{
		const float A = i / 8.0f;
		const float Y = FMath::Lerp(TopY, BotY, A);
		const float Bulge = 1.0f + 0.16f * FMath::Sin(A * PI); // 背闊肌外弧
		const float Half = FMath::Lerp(HalfTop, HalfBot, A) * Bulge;
		Outline.Add(FVector2D(Cx - Half, Y));
	}
	for (int32 i = 8; i >= 0; --i)
	{
		Outline.Add(FVector2D(2.0f * Cx - Outline[i].X, Outline[i].Y));
	}
	for (int32 i = 0; i + 1 < Outline.Num(); ++i)
	{
		AddTri(Pivot, Outline[i], Outline[i + 1]);
	}
	AddTri(Pivot, Outline.Last(), Outline[0]);
	// 頭（圓＝12 段扇）
	const FVector2D HeadC(Cx, TopY - NeckR * 0.9f);
	for (int32 i = 0; i < 12; ++i)
	{
		const float A0 = 2.0f * PI * i / 12.0f, A1 = 2.0f * PI * (i + 1) / 12.0f;
		AddTri(HeadC,
			HeadC + FVector2D(FMath::Cos(A0), FMath::Sin(A0)) * NeckR,
			HeadC + FVector2D(FMath::Cos(A1), FMath::Sin(A1)) * NeckR);
	}
	FCanvasTriangleItem TriItem(Tris, GWhiteTexture);
	TriItem.BlendMode = SE_BLEND_Opaque;
	Canvas->DrawItem(TriItem);

	// 和彫：每 2.4 秒輪播一款 motif（蛇→鶴→龜→櫻）＋一款小紋樣在肩位
	static const int32 ShowIdx[4] = {
		NiceInkTraceMotifs::Idx_Snake, NiceInkTraceMotifs::Idx_Crane,
		NiceInkTraceMotifs::Idx_Turtle, NiceInkTraceMotifs::Idx_Sakura };
	const int32 Pick = static_cast<int32>(Now / 2.4) % 4;
	const FLinearColor InkDark = FLinearColor(0.05f, 0.04f, 0.06f) * Flicker;
	const FLinearColor InkRed = FLinearColor(0.42f, 0.06f, 0.05f) * Flicker;
	// 主紋樣（背中央；粗黑線＋內縮紅線＝二重彫）
	DrawMotif(Canvas, ShowIdx[Pick], Cx, H * 0.56f, H * 0.019f, InkDark, 6.0f);
	DrawMotif(Canvas, ShowIdx[Pick], Cx, H * 0.56f, H * 0.0162f, InkRed, 2.5f);
	// 肩位小紋樣（櫻恆駐＝畫面不空）
	DrawMotif(Canvas, NiceInkTraceMotifs::Idx_Sakura, Cx - W * 0.19f, H * 0.30f,
		H * 0.0042f, InkDark, 2.5f);
	DrawMotif(Canvas, NiceInkTraceMotifs::Idx_Sakura, Cx + W * 0.19f, H * 0.30f,
		H * 0.0042f, InkDark, 2.5f);

	// 掃描線（每 4px 一條半透明暗線＝映像管）
	for (int32 Y = 0; Y < H; Y += 4)
	{
		FCanvasTileItem Line(FVector2D(0, Y), FVector2D(W, 1.0f),
			FLinearColor(0.0f, 0.0f, 0.0f, 0.28f));
		Line.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Line);
	}
	DrawTubeVignette(Canvas, W, H);
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

void ANiceInkTvSet::DrawMotif(UCanvas* Canvas, int32 MotifIdx, float Cx, float Cy,
	float Scale, const FLinearColor& Color, float Thickness) const
{
	if (MotifIdx < 0 || MotifIdx >= NiceInkTraceMotifs::Num)
	{
		return;
	}
	const FDreamTraceBakedMotif& M = NiceInkTraceMotifs::Table[MotifIdx];
	// 座標域＝cm@108cm 線長、原點在形心附近；y 朝上 ⇒ 畫布 y 反向
	for (int32 i = 0; i + 1 < M.NumPoints; ++i)
	{
		const FVector2D A(Cx + M.Points[i * 2] * Scale, Cy - M.Points[i * 2 + 1] * Scale);
		const FVector2D B(Cx + M.Points[i * 2 + 2] * Scale, Cy - M.Points[i * 2 + 3] * Scale);
		FCanvasLineItem L(A, B);
		L.SetColor(Color);
		L.LineThickness = Thickness;
		Canvas->DrawItem(L);
	}
	if (M.bClosed && M.NumPoints > 1)
	{
		const FVector2D A(Cx + M.Points[(M.NumPoints - 1) * 2] * Scale,
			Cy - M.Points[(M.NumPoints - 1) * 2 + 1] * Scale);
		const FVector2D B(Cx + M.Points[0] * Scale, Cy - M.Points[1] * Scale);
		FCanvasLineItem L(A, B);
		L.SetColor(Color);
		L.LineThickness = Thickness;
		Canvas->DrawItem(L);
	}
}
