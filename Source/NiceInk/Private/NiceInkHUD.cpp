#include "NiceInkHUD.h"

#include "CanvasItem.h"
#include "DreamMazeComponent.h"
#include "DreamTraceComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "Fonts/FontMeasure.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerState.h"
#include "InkCanvasComponent.h"
#include "NiceInkAudio.h"
#include "NiceInkCharacter.h"
#include "NiceInkGameInstance.h"
#include "NiceInkGameState.h"
#include "NiceInkPlayerState.h"
#include "NiceInkTypes.h"
#include "NiceInkUiTokens.h"

TAutoConsoleVariable<int32> CVarNiDebugHud(
	TEXT("ni.DebugHud"), 0,
	TEXT("1 = show developer HUD telemetry (console hints, seat/id, version stamps)."));

// 調色盤搬進 NiceInkUiTokens.h（主選單 HUD 共用同一組 token）

void ANiceInkHUD::BeginPlay()
{
	Super::BeginPlay();
	EnsureUiAssets();
}

void ANiceInkHUD::EnsureUiAssets()
{
	if (UiFont)
	{
		return;
	}

	UObject* MediumFace = StaticLoadObject(UObject::StaticClass(), nullptr, TEXT("/Game/UI/Fonts/FF_MPlusRounded_Medium.FF_MPlusRounded_Medium"));
	UObject* XBoldFace = StaticLoadObject(UObject::StaticClass(), nullptr, TEXT("/Game/UI/Fonts/FF_MPlusRounded_XBold.FF_MPlusRounded_XBold"));
	if (MediumFace || XBoldFace)
	{
		UFont* Composite = NewObject<UFont>(this, TEXT("NiUiFont"));
		Composite->FontCacheType = EFontCacheType::Runtime;
		if (MediumFace)
		{
			FTypefaceEntry& Entry = Composite->GetMutableInternalCompositeFont().DefaultTypeface.Fonts.AddDefaulted_GetRef();
			Entry.Name = TEXT("Regular");
			Entry.Font = FFontData(MediumFace);
		}
		if (XBoldFace)
		{
			FTypefaceEntry& Entry = Composite->GetMutableInternalCompositeFont().DefaultTypeface.Fonts.AddDefaulted_GetRef();
			Entry.Name = TEXT("Bold");
			Entry.Font = FFontData(XBoldFace);
		}
		UiFont = Composite;
	}
	else if (GEngine)
	{
		UiFont = GEngine->GetMediumFont(); // 資產缺失的保底：至少不畫空
	}

	IconCup    = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Icons/T_UI_Cup.T_UI_Cup"));
	IconSpray  = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Icons/T_UI_Spray.T_UI_Spray"));
	IconKick   = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Icons/T_UI_Kick.T_UI_Kick"));
	IconMarker = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Icons/T_UI_Marker.T_UI_Marker"));
	IconCash   = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Icons/T_UI_Cash.T_UI_Cash"));
	IconRotate = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Icons/T_UI_Rotate.T_UI_Rotate"));
	IconEye    = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Icons/T_UI_Eye.T_UI_Eye"));
	IconTrap   = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Icons/T_UI_Trap.T_UI_Trap"));
	IconSleep  = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Icons/T_UI_Sleep.T_UI_Sleep"));
	IconNose   = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Icons/T_UI_Nose.T_UI_Nose"));
	PenSprite  = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Icons/T_UI_TattooPen.T_UI_TattooPen"));
	MarkerSprite = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Icons/T_UI_MarkerPen.T_UI_MarkerPen"));
}

float ANiceInkHUD::TierSize(ETextTier Tier) const
{
	switch (Tier)
	{
	case ETextTier::Display: return 34.0f;
	case ETextTier::Title:   return 21.0f;
	case ETextTier::Body:    return 15.0f;
	default:                 return 12.0f;
	}
}

FVector2D ANiceInkHUD::MeasureTok(const FString& Text, ETextTier Tier, bool bBold)
{
	if (!Canvas || !UiFont || !FSlateApplication::IsInitialized())
	{
		return FVector2D::ZeroVector;
	}
	const int32 SizePx = FMath::Max(8, FMath::RoundToInt(TierSize(Tier) * UiScale));
	const FSlateFontInfo Info(UiFont, SizePx, bBold ? FName("Bold") : FName("Regular"));
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	return FVector2D(Measure->Measure(Text, Info, Canvas->GetDPIScale()));
}

FVector2D ANiceInkHUD::DrawTok(const FString& Text, float X, float Y, ETextTier Tier,
	const FLinearColor& Color, EHAlign Align, bool bBold)
{
	if (!Canvas || !UiFont || Text.IsEmpty())
	{
		return FVector2D::ZeroVector;
	}
	const FVector2D Size = MeasureTok(Text, Tier, bBold);
	if (Align == EHAlign::Center)
	{
		X -= Size.X * 0.5f;
	}
	else if (Align == EHAlign::Right)
	{
		X -= Size.X;
	}
	const int32 SizePx = FMath::Max(8, FMath::RoundToInt(TierSize(Tier) * UiScale));
	const FSlateFontInfo Info(UiFont, SizePx, bBold ? FName("Bold") : FName("Regular"));
	FCanvasTextItem Item(FVector2D(X, Y), FText::FromString(Text), Info, Color);
	Item.EnableShadow(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f), FVector2D(1.0f, 1.0f) * FMath::Max(1.0f, UiScale));
	Canvas->DrawItem(Item);
	return Size;
}

void ANiceInkHUD::DrawPanelBox(float X, float Y, float W, float H, float Alpha)
{
	// 方角單矩形：半透面板疊蓋會出接縫，圓角免談；方角＝美術語言的硬切
	FLinearColor C = NiHudColor::Ink;
	C.A = Alpha;
	DrawRect(C, X, Y, W, H);
}

void ANiceInkHUD::DrawIconTok(UTexture2D* Tex, float X, float Y, float Size, const FLinearColor& Tint)
{
	if (!Canvas || !Tex)
	{
		return;
	}
	Canvas->K2_DrawTexture(Tex, FVector2D(X, Y), FVector2D(Size, Size),
		FVector2D::ZeroVector, FVector2D::UnitVector, Tint, BLEND_Translucent);
}

// ---- 即時模式 UI 互動（主選單／ESC 選單共用）----

void ANiceInkHUD::BeginUiFrame()
{
	MousePos = FVector2D::ZeroVector;
	bClickThisFrame = false;
	bClickConsumed = false;
	if (PlayerOwner)
	{
		float MX = 0.0f, MY = 0.0f;
		PlayerOwner->GetMousePosition(MX, MY);
		MousePos = FVector2D(MX, MY);
		bClickThisFrame = PlayerOwner->WasInputKeyJustPressed(EKeys::LeftMouseButton);

		// 去抖：實測同一實體點擊可在連續兩幀都讀成 just-pressed（打包版 60fps，
		// 雙重建房的元凶之一）——150ms 內的第二擊一律吞掉
		if (bClickThisFrame && GetWorld())
		{
			const double Now = GetWorld()->GetRealTimeSeconds();
			if (Now - LastUiClickTime < 0.15)
			{
				bClickThisFrame = false;
			}
			else
			{
				LastUiClickTime = Now;
			}
		}
	}
}

bool ANiceInkHUD::Button(const FString& Label, float CenterX, float Y, float W, float H,
	bool bEnabled, bool bAccent)
{
	const float X = CenterX - W * 0.5f;
	const bool bHover = bEnabled &&
		MousePos.X >= X && MousePos.X <= X + W && MousePos.Y >= Y && MousePos.Y <= Y + H;

	// 面板底＋hover 反白：美術語言＝硬切，不做漸變
	FLinearColor Fill = NiHudColor::Ink;
	Fill.A = bHover ? 0.95f : 0.72f;
	DrawRect(Fill, X, Y, W, H);
	const FLinearColor Edge = !bEnabled ? NiHudColor::PaperDim :
		(bHover ? NiHudColor::Amber : (bAccent ? NiHudColor::Amber : NiHudColor::Paper));
	const float B = FMath::Max(1.0f, 2.0f * UiScale);
	DrawRect(Edge, X, Y, W, B);
	DrawRect(Edge, X, Y + H - B, W, B);
	DrawRect(Edge, X, Y, B, H);
	DrawRect(Edge, X + W - B, Y, B, H);

	const FLinearColor TextColor = bEnabled ? (bHover ? NiHudColor::Amber : NiHudColor::Paper) : NiHudColor::PaperDim;
	const FVector2D TextSize = MeasureTok(Label, ETextTier::Title, bAccent);
	DrawTok(Label, CenterX, Y + (H - TextSize.Y) * 0.5f, ETextTier::Title, TextColor, EHAlign::Center, bAccent);

	if (bHover && bClickThisFrame && !bClickConsumed)
	{
		bClickConsumed = true;
		NiAudio::Play(this, ENiSound::UiClick);
		return true;
	}
	return false;
}

int32 ANiceInkHUD::AdjustRow(const FString& Label, const FString& Value, float CenterX, float Y,
	bool bLeftEnabled, bool bRightEnabled)
{
	const float RowH = 40.0f * UiScale;
	DrawTok(Label, CenterX - 40.0f * UiScale, Y + 8.0f * UiScale, ETextTier::Body, NiHudColor::PaperDim, EHAlign::Right, false);
	DrawTok(Value, CenterX + 170.0f * UiScale, Y + 8.0f * UiScale, ETextTier::Body, NiHudColor::Paper, EHAlign::Center, false);

	int32 Delta = 0;
	if (Button(TEXT("<"), CenterX + 30.0f * UiScale, Y, 36.0f * UiScale, RowH, bLeftEnabled))
	{
		Delta = -1;
	}
	if (Button(TEXT(">"), CenterX + 310.0f * UiScale, Y, 36.0f * UiScale, RowH, bRightEnabled))
	{
		Delta = +1;
	}
	return Delta;
}

void ANiceInkHUD::DrawBigTitle(const FString& Text, float CenterX, float Y, float SizePx, const FLinearColor& Color)
{
	if (!Canvas || !UiFont)
	{
		return;
	}
	const FSlateFontInfo Info(UiFont, FMath::RoundToInt(SizePx * UiScale), FName("Bold"));
	FCanvasTextItem Item(FVector2D(0, 0), FText::FromString(Text), Info, Color);
	Item.bCentreX = true;
	Item.Position = FVector2D(CenterX, Y);
	Item.EnableShadow(FLinearColor(0, 0, 0, 0.6f), FVector2D(2.0f, 2.0f) * UiScale);
	Canvas->DrawItem(Item);
}

void ANiceInkHUD::DrawCupsRow(float X, float Y, float CupSize, int32 Filled, EHAlign Align)
{
	const float Step = CupSize * 1.18f;
	float StartX = X;
	if (Align == EHAlign::Center)
	{
		StartX -= (Step * 2.0f + CupSize) * 0.5f;
	}
	else if (Align == EHAlign::Right)
	{
		StartX -= Step * 2.0f + CupSize;
	}
	for (int32 i = 0; i < 3; ++i)
	{
		FLinearColor Tint;
		if (i < Filled)
		{
			Tint = NiHudColor::Amber;
		}
		else
		{
			// 第二杯起，空杯轉紅：懸崖警示全場一眼可讀
			Tint = (Filled >= 2) ? NiHudColor::Red : NiHudColor::Paper;
			Tint.A = 0.28f;
		}
		DrawIconTok(IconCup, StartX + i * Step, Y, CupSize, Tint);
	}
}

void ANiceInkHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas || !GetWorld())
	{
		return;
	}
	EnsureUiAssets();
	UiScale = Canvas->ClipY / 1080.0f;

	const ANiceInkGameState* GS = GetWorld()->GetGameState<ANiceInkGameState>();
	ANiceInkCharacter* MyChar = PlayerOwner ? Cast<ANiceInkCharacter>(PlayerOwner->GetPawn()) : nullptr;
	const ANiceInkPlayerState* MyPS = PlayerOwner ? PlayerOwner->GetPlayerState<ANiceInkPlayerState>() : nullptr;

	BeginUiFrame(); // ESC 選單的按鈕判定
	TickAudioCues(GS);

	// 沉睡端：視覺全遮蔽——黑屏＋醉夢迷宮＋姿勢面板，其他 HUD 一概不畫
	if (MyChar && MyChar->bAsleep && !MyChar->bEyesOpen)
	{
		DrawVictimSleepUI(MyChar, GS, MyPS);
		DrawTrapDial(MyChar);
		DrawSystemMenu(MyChar);
		DrawDebugPanel(GS, MyPS, MyChar);
		return;
	}

	// 裝睡（Shift 按住）：對旁人＝沉睡姿勢＋閉眼貼圖；對本人＝閉眼就是看不到
	//（定案 #42 恆等式：你看得到的≡臉表達的——裝睡不是免費監視器，
	// 何時敢重新睜眼本身是賭注）。黑屏上只留一行提示。
	if (MyChar && MyChar->IsFeigningSleep())
	{
		DrawRect(FLinearColor(0.01f, 0.01f, 0.015f, 1.0f), 0.0f, 0.0f, Canvas->ClipX, Canvas->ClipY);
		DrawBottomHint(TEXT("feigning sleep — release SHIFT to open your eyes"), NiHudColor::PaperDim);
		DrawTrapDial(MyChar);
		DrawSystemMenu(MyChar);
		DrawDebugPanel(GS, MyPS, MyChar);
		return;
	}

	// ESC 選單開著＝只畫選單（醒著沒有遮蔽義務；底層面板文字互疊會打架）
	if (MyChar && MyChar->IsSystemMenuOpen())
	{
		DrawSystemMenu(MyChar);
		DrawDebugPanel(GS, MyPS, MyChar);
		return;
	}

	DrawBlindOverlay(MyChar);
	DrawTopBar(GS, MyPS, MyChar);
	DrawCenterBanners(GS);

	if (GS && GS->CurrentPhase == ENiceInkPhase::Lobby)
	{
		DrawLobbyPanel(GS);
	}

	const bool bIsVictim = GS && MyPS && GS->VictimPlayerId == MyPS->GetPlayerId();
	if (GS && GS->CurrentPhase == ENiceInkPhase::Accusation && bIsVictim && MyChar)
	{
		DrawAccusePanel(GS, MyChar);
	}
	if (GS && GS->CurrentPhase == ENiceInkPhase::PostGame && MyChar)
	{
		DrawPostGamePanel(MyChar);
	}

	// 兇手轉盤覆蓋（不打斷 lean-lock 作畫；只有被踩中陷阱的兇手本人看得到）
	DrawTrapDial(MyChar);

	// 底部情境提示：每畫面至多一行（優先序：翻身表決 > 鎖定操作 > 睜眼 > 翻身入口）
	// 翻身表決只給作畫者看——偷醒的受害者不該從 HUD 白拿「有人要翻你」的情報
	const bool bDrawingArtist = GS && GS->CurrentPhase == ENiceInkPhase::Drawing &&
		MyChar && !MyChar->bAsleep && !bIsVictim;
	if (bDrawingArtist && GS->FlipProposerId != INDEX_NONE)
	{
		const APlayerState* ProposerPS = GS->FindPlayerStateById(GS->FlipProposerId);
		const bool bIVoted = MyChar->FlipAgreedProposalSerial == GS->FlipProposalSerial ||
			(MyPS && GS->FlipProposerId == MyPS->GetPlayerId());
		DrawBottomHint(bIVoted
			? FString::Printf(TEXT("flip the body — waiting for the others (%d/%d)"), GS->FlipAgreeCount, GS->FlipAgreeNeeded)
			: FString::Printf(TEXT("%s proposes to FLIP the body — F agree (%d/%d)"),
				ProposerPS ? *ProposerPS->GetPlayerName() : TEXT("?"), GS->FlipAgreeCount, GS->FlipAgreeNeeded),
			NiHudColor::Amber);
	}
	else if (MyChar && MyChar->bLeanLocked)
	{
		// 可畫域標記＝皮膚上的 veil 殼（角色端 UpdateReachVeilShell）；HUD 只補提示行
		// 搆不到持續 >1s＝邊界開口說話（無聲失敗鐵則）：筆收起是物理訊號，
		// 這行話補上「該怎麼辦」
		DrawBottomHint(MyChar->GetDrawUnreachableSeconds() > 1.0f
			? TEXT("out of reach — veiled skin needs a closer lean (RMB stand up)")
			: TEXT("LMB draw   ·   SCROLL needle   ·   G shake his dream   ·   RMB stand up"),
			MyChar->GetDrawUnreachableSeconds() > 1.0f ? NiHudColor::Amber : NiHudColor::PaperDim);
		DrawPaletteStrip(MyChar); // 色票列＝「1-9,0 color」提示的可視化本體
	}
	else if (MyChar && MyChar->bAsleep && MyChar->bEyesOpen)
	{
		// 無聲甦醒中：實景視野；提示只給受害者本人
		DrawBottomHint(TEXT("eyes open — mouse aims your face · hold SHIFT feigns sleep · WASD stands you up & ends the drawing"), NiHudColor::Amber);
	}
	else if (bDrawingArtist)
	{
		// 搖晃攻擊（v4.0 定案 #50；-$500＝ShakeAttackCost 佔位價，同步改）
		DrawBottomHint(TEXT("F — propose to flip the body   ·   G — shake his dream (-$500)"), NiHudColor::PaperDim);
	}

	// 搖晃購買回執（只給攻擊者本人；不透漏夢內結果）
	if (MyChar && GetWorld() && GetWorld()->GetTimeSeconds() < MyChar->ShakeAckFlashUntil)
	{
		DrawTok(MyChar->bLastShakeAckBought ? TEXT("DREAM SHAKEN  -$500") : TEXT("SHAKE REFUSED (cash / cooldown)"),
			Canvas->ClipX * 0.5f, Canvas->ClipY * 0.22f, ETextTier::Title,
			MyChar->bLastShakeAckBought ? NiHudColor::Amber : NiHudColor::Red, EHAlign::Center, true);
	}

	DrawInkCrosshair(GS, MyPS);
	DrawDebugPanel(GS, MyPS, MyChar);
}

void ANiceInkHUD::DrawTopBar(const ANiceInkGameState* GS, const ANiceInkPlayerState* MyPS, ANiceInkCharacter* MyChar)
{
	if (!GS)
	{
		return;
	}
	const float W = Canvas->ClipX;
	const float M = 22.0f * UiScale;

	// 相位（頂部置中）＋可選倒數
	FString PhaseText = GetPhaseLabel(GS->CurrentPhase);
	const float Remaining = GS->GetPhaseTimeRemaining();
	if (Remaining > 0.0f)
	{
		PhaseText += FString::Printf(TEXT("  ·  %.0fs"), Remaining);
	}

	// 副行：回合中＝受害者＋罰酒；巡禮＝進度；指認（旁觀）＝等待中
	FString SubText;
	int32 SubCups = -1;
	const APlayerState* VictimPS = GS->FindPlayerStateById(GS->VictimPlayerId);
	const ANiceInkPlayerState* VictimNIPS = Cast<ANiceInkPlayerState>(VictimPS);
	switch (GS->CurrentPhase)
	{
	case ENiceInkPhase::Drawing:
		if (VictimPS)
		{
			SubText = FString::Printf(TEXT("%s is asleep"), *VictimPS->GetPlayerName());
			SubCups = VictimNIPS ? VictimNIPS->PenaltyCups : 0;
		}
		break;
	case ENiceInkPhase::Tour:
		SubText = FString::Printf(TEXT("work %d / %d"), GS->TourWorkNumber, GS->TourWorkCount);
		break;
	case ENiceInkPhase::Accusation:
		if (VictimPS)
		{
			SubText = FString::Printf(TEXT("%s is choosing..."), *VictimPS->GetPlayerName());
			SubCups = VictimNIPS ? VictimNIPS->PenaltyCups : 0;
		}
		break;
	default:
		break;
	}

	const FVector2D PhaseSize = MeasureTok(PhaseText, ETextTier::Title, true);
	const FVector2D SubSize = SubText.IsEmpty() ? FVector2D::ZeroVector : MeasureTok(SubText, ETextTier::Body, false);
	const float CupSize = 22.0f * UiScale;
	const float CupsW = (SubCups >= 0) ? (CupSize * 1.18f * 2.0f + CupSize + 12.0f * UiScale) : 0.0f;
	const float PanelW = FMath::Max(PhaseSize.X, SubSize.X + CupsW) + 60.0f * UiScale;
	const float PanelH = PhaseSize.Y + (SubText.IsEmpty() ? 0.0f : SubSize.Y + 6.0f * UiScale) + 22.0f * UiScale;
	DrawPanelBox(W * 0.5f - PanelW * 0.5f, M * 0.5f, PanelW, PanelH, 0.55f);

	float Y = M * 0.5f + 10.0f * UiScale;
	DrawTok(PhaseText, W * 0.5f, Y, ETextTier::Title, NiHudColor::Paper, EHAlign::Center, true);
	Y += PhaseSize.Y + 6.0f * UiScale;
	if (!SubText.IsEmpty())
	{
		const float RowW = SubSize.X + CupsW;
		const float TextX = W * 0.5f - RowW * 0.5f;
		DrawTok(SubText, TextX, Y, ETextTier::Body, NiHudColor::PaperDim, EHAlign::Left, false);
		if (SubCups >= 0)
		{
			DrawCupsRow(TextX + SubSize.X + 12.0f * UiScale, Y + (SubSize.Y - CupSize) * 0.5f, CupSize, SubCups);
		}
	}

	// 現金（右上）
	if (MyPS)
	{
		const float CashIcon = 22.0f * UiScale;
		const FString CashText = FText::AsNumber(MyPS->Cash).ToString();
		DrawIconTok(IconCash, W - M - CashIcon, M, CashIcon, NiHudColor::Amber);
		DrawTok(CashText, W - M - CashIcon - 8.0f * UiScale, M + 2.0f * UiScale, ETextTier::Body, NiHudColor::Paper, EHAlign::Right, true);
	}
}

void ANiceInkHUD::DrawCenterBanners(const ANiceInkGameState* GS)
{
	if (!GS)
	{
		return;
	}
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;

	if (GS->CurrentPhase == ENiceInkPhase::Resolution)
	{
		const bool bCorrect = GS->LastAccusationResult == ENiceInkAccusationResult::Correct;
		const APlayerState* AuthorPS = GS->FindPlayerStateById(GS->RevealedAuthorId);
		const FString AuthorName = AuthorPS ? AuthorPS->GetPlayerName() : TEXT("?");
		DrawTok(bCorrect ? TEXT("CORRECT!") : TEXT("WRONG!"),
			W * 0.5f, H * 0.26f, ETextTier::Display, bCorrect ? NiHudColor::Green : NiHudColor::Red, EHAlign::Center, true);
		DrawTok(bCorrect
			? FString::Printf(TEXT("%s takes the seat"), *AuthorName)
			: FString::Printf(TEXT("%s inks the picked work  ·  +1 cup"), *AuthorName),
			W * 0.5f, H * 0.26f + 52.0f * UiScale, ETextTier::Body, NiHudColor::Paper, EHAlign::Center, false);
	}

	if (GS->CurrentPhase == ENiceInkPhase::Finale || GS->CurrentPhase == ENiceInkPhase::PostGame)
	{
		if (const APlayerState* LoserPS = GS->FindPlayerStateById(GS->LoserPlayerId))
		{
			DrawTok(TEXT("OUT COLD"), W * 0.5f, H * 0.2f, ETextTier::Display, NiHudColor::Red, EHAlign::Center, true);
			DrawTok(FString::Printf(TEXT("%s's cash is split  ·  ink locked forever"), *LoserPS->GetPlayerName()),
				W * 0.5f, H * 0.2f + 52.0f * UiScale, ETextTier::Body, NiHudColor::Paper, EHAlign::Center, false);
		}
	}
}

void ANiceInkHUD::TickAudioCues(const ANiceInkGameState* GS)
{
	if (!GS)
	{
		return;
	}
	const ENiceInkPhase Phase = GS->CurrentPhase;
	if (!bPhaseSeeded)
	{
		// 首幀（含中途加入）：記狀態不發聲
		bPhaseSeeded = true;
		LastPhaseSeen = Phase;
		LastTourWorkSeen = GS->TourWorkId;
		return;
	}

	if (Phase != LastPhaseSeen)
	{
		switch (Phase)
		{
		case ENiceInkPhase::BottleSpin: NiAudio::Play(this, ENiSound::BottleSpin); break;
		case ENiceInkPhase::Seating:    NiAudio::Play(this, ENiSound::DrinkGulp); break; // 入座酒
		case ENiceInkPhase::Resolution:
			NiAudio::Play(this, GS->LastAccusationResult == ENiceInkAccusationResult::Correct
				? ENiSound::AccuseCorrect : ENiSound::AccuseWrong);
			break;
		case ENiceInkPhase::Finale:     NiAudio::Play(this, ENiSound::FinaleGong); break;
		// Drawing/Tour/Accusation/PostGame 的入場不發聲（Tour 由逐幅 chime 承擔；
		// Drawing 開始＝沉睡開始，甦醒側零提示鐵律的另一半）
		default: break;
		}
		LastPhaseSeen = Phase;
	}

	if (GS->TourWorkId != LastTourWorkSeen)
	{
		if (Phase == ENiceInkPhase::Tour && GS->TourWorkId != INDEX_NONE)
		{
			NiAudio::Play(this, ENiSound::TourChime);
		}
		LastTourWorkSeen = GS->TourWorkId;
	}
}

void ANiceInkHUD::DrawLobbyPanel(const ANiceInkGameState* GS)
{
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	const float CX = W * 0.5f;

	const int32 NumIn = GS->PlayerArray.Num();
	const float PanelW = 480.0f * UiScale;
	const float RowH = 30.0f * UiScale;
	const float PanelH = (110.0f + 30.0f * FMath::Max(1, NumIn)) * UiScale;
	const float X = CX - PanelW * 0.5f;
	const float Y = H * 0.30f;
	DrawPanelBox(X, Y, PanelW, PanelH, 0.72f);

	float LineY = Y + 16.0f * UiScale;
	DrawTok(FString::Printf(TEXT("DOJO LOBBY  ·  %d / 6"), NumIn), CX, LineY, ETextTier::Title, NiHudColor::Paper, EHAlign::Center, true);
	LineY += 44.0f * UiScale;

	// 名單按席位排序（席位＝入場順序）
	TArray<const ANiceInkPlayerState*> Sorted;
	for (const APlayerState* PS : GS->PlayerArray)
	{
		if (const ANiceInkPlayerState* NIPS = Cast<ANiceInkPlayerState>(PS))
		{
			Sorted.Add(NIPS);
		}
	}
	Sorted.Sort([](const ANiceInkPlayerState& A, const ANiceInkPlayerState& B) { return A.SeatIndex < B.SeatIndex; });
	for (const ANiceInkPlayerState* PS : Sorted)
	{
		DrawTok(FString::Printf(TEXT("seat %d"), PS->SeatIndex + 1),
			CX - 150.0f * UiScale, LineY, ETextTier::Body, NiHudColor::PaperDim, EHAlign::Left, false);
		DrawTok(PS->GetPlayerName(), CX - 40.0f * UiScale, LineY, ETextTier::Body, NiHudColor::Paper, EHAlign::Left, false);
		DrawTok(FText::AsNumber(PS->Cash).ToString(), CX + 190.0f * UiScale, LineY, ETextTier::Body, NiHudColor::Amber, EHAlign::Right, false);
		LineY += RowH;
	}

	// 主機（listen server 本人）手動開始；其他人等待——自動開局只活在 PIE（robo）
	const bool bIsHost = GetWorld() && GetWorld()->GetNetMode() != NM_Client;
	if (bIsHost)
	{
		DrawBottomHint(NumIn >= 2
			? TEXT("ENTER — start the match")
			: TEXT("waiting for players — need at least 2 to start"),
			NumIn >= 2 ? NiHudColor::Amber : NiHudColor::PaperDim);
	}
	else
	{
		DrawBottomHint(TEXT("waiting for the host to start the match"), NiHudColor::PaperDim);
	}
}

void ANiceInkHUD::DrawSystemMenu(ANiceInkCharacter* MyChar)
{
	if (!MyChar || !MyChar->IsSystemMenuOpen())
	{
		return;
	}
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	const float CX = W * 0.5f;

	// 壓暗全畫面（menu 在最上層；沉睡黑屏之上也可讀）
	FLinearColor Dim = NiHudColor::Ink;
	Dim.A = 0.62f;
	DrawRect(Dim, 0, 0, W, H);

	DrawBigTitle(TEXT("MENU"), CX, H * 0.22f, 44.0f, NiHudColor::Paper);

	UNiceInkGameInstance* GI = UNiceInkGameInstance::Get(this);
	const float RowX = CX - 40.0f * UiScale;
	float Y = H * 0.22f + 96.0f * UiScale;
	const float Step = 64.0f * UiScale;

	if (GI)
	{
		const int32 SensDelta = AdjustRow(TEXT("mouse sensitivity"),
			FString::Printf(TEXT("%.1f"), GI->MouseSensitivityScale), RowX, Y,
			GI->MouseSensitivityScale > 0.25f, GI->MouseSensitivityScale < 2.95f);
		if (SensDelta != 0)
		{
			GI->MouseSensitivityScale = FMath::Clamp(GI->MouseSensitivityScale + SensDelta * 0.1f, 0.2f, 3.0f);
			GI->SaveSettings();
		}
		Y += Step;

		const int32 VolDelta = AdjustRow(TEXT("master volume"),
			FString::Printf(TEXT("%d%%"), FMath::RoundToInt(GI->MasterVolume * 100.0f)), RowX, Y,
			GI->MasterVolume > 0.01f, GI->MasterVolume < 0.99f);
		if (VolDelta != 0)
		{
			GI->MasterVolume = FMath::Clamp(GI->MasterVolume + VolDelta * 0.05f, 0.0f, 1.0f);
			GI->SaveSettings();
		}
		Y += Step + 24.0f * UiScale;
	}

	const float BtnW = 300.0f * UiScale;
	const float BtnH = 52.0f * UiScale;
	if (Button(TEXT("resume"), CX, Y, BtnW, BtnH, true, true))
	{
		MyChar->SetSystemMenuOpen(false);
	}
	Y += BtnH + 14.0f * UiScale;
	if (Button(TEXT("leave the room"), CX, Y, BtnW, BtnH))
	{
		if (GI)
		{
			GI->ReturnToMainMenu(FString());
		}
	}
	Y += BtnH + 18.0f * UiScale;
	DrawTok(TEXT("esc — resume"), CX, Y, ETextTier::Small, NiHudColor::PaperDim, EHAlign::Center, false);
}

void ANiceInkHUD::DrawAccusePanel(const ANiceInkGameState* GS, ANiceInkCharacter* MyChar)
{
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	const float PanelW = 460.0f * UiScale;
	const float PanelH = 118.0f * UiScale;
	const float X = W * 0.5f - PanelW * 0.5f;
	const float Y = H - PanelH - 70.0f * UiScale;
	DrawPanelBox(X, Y, PanelW, PanelH, 0.8f);

	float LineY = Y + 12.0f * UiScale;
	DrawTok(FString::Printf(TEXT("ACCUSE  ·  work %d / %d"), MyChar->AccusePickNumber, GS->TourWorkCount),
		W * 0.5f, LineY, ETextTier::Title, NiHudColor::Paper, EHAlign::Center, true);
	LineY += 34.0f * UiScale;
	const APlayerState* Suspect = MyChar->GetAccuseSuspect();
	DrawTok(FString::Printf(TEXT("suspect:  %s"), Suspect ? *Suspect->GetPlayerName() : TEXT("?")),
		W * 0.5f, LineY, ETextTier::Body, NiHudColor::Amber, EHAlign::Center, true);
	LineY += 28.0f * UiScale;
	DrawTok(TEXT("1-9 view work   ·   TAB suspect   ·   ENTER accuse"),
		W * 0.5f, LineY, ETextTier::Small, NiHudColor::PaperDim, EHAlign::Center, false);
}

void ANiceInkHUD::DrawPostGamePanel(ANiceInkCharacter* MyChar)
{
	if (!MyChar || !MyChar->InkCanvas)
	{
		return;
	}
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	const int32 CarbonCount = MyChar->InkCanvas->GetWorkIdsByState(EInkWorkState::Carbon).Num();
	const int32 PermanentCount = MyChar->InkCanvas->GetWorkIdsByState(EInkWorkState::Permanent).Num();

	// 場間大廳：端詳刺青、自費雷射、開下一場
	DrawTok(FString::Printf(TEXT("your ink:  %d carbon (laserable)  ·  %d permanent"), CarbonCount, PermanentCount),
		W * 0.5f, H - 96.0f * UiScale, ETextTier::Body, NiHudColor::Paper, EHAlign::Center, false);
	const bool bIsHost = GetWorld() && GetWorld()->GetNetMode() != NM_Client;
	DrawBottomHint(bIsHost
		? TEXT("L — laser oldest carbon (2000, 3rd pass removes)   ·   ENTER — next match")
		: TEXT("L — laser oldest carbon (2000, 3rd pass removes)   ·   host starts the next match"),
		NiHudColor::PaperDim);
}

void ANiceInkHUD::DrawBottomHint(const FString& Text, const FLinearColor& Color)
{
	DrawTok(Text, Canvas->ClipX * 0.5f, Canvas->ClipY - 46.0f * UiScale, ETextTier::Small, Color, EHAlign::Center, false);
}

void ANiceInkHUD::DrawPaletteStrip(const ANiceInkCharacter* MyChar)
{
	// 鎖定中常駐色票列：十色塊＋鍵位數字＋當前色高亮框——固定色盤是承重設計
	//（限時作畫/皮膚可讀策展/墨杯題材/畫風指紋），可視化補完 hotbar 慣例的另一半。
	// 色塊直接用調色盤 linear 值＝與墨水同色（準星/範圍圈同一約定，不過 sRGB）。
	if (!MyChar || !Canvas)
	{
		return;
	}
	const int32 N = FMath::Min(10, FNiceInkPalette::Num());
	const float S = 20.0f * UiScale;            // 色塊邊長
	const float Gap = 7.0f * UiScale;
	const float TotalW = N * S + (N - 1) * Gap;
	const float X0 = (Canvas->ClipX - TotalW) * 0.5f;
	const float SwatchY = Canvas->ClipY - 104.0f * UiScale; // 底部提示行(-46)之上
	for (int32 i = 0; i < N; ++i)
	{
		const float X = X0 + i * (S + Gap);
		const bool bSel = MyChar->SelectedColorIndex == i;
		// 外框：未選=墨色細框（白色/淡色在膚色背景上也讀得出邊界）；
		// 當前色=紙色粗框＋微放大（唯一高亮語彙，不加動畫——美術語言 #24 硬切）
		const float B = (bSel ? 2.5f : 1.0f) * UiScale;
		const float Grow = bSel ? 2.0f * UiScale : 0.0f;
		FLinearColor Frame = bSel ? NiHudColor::Paper : NiHudColor::Ink;
		Frame.A = bSel ? 1.0f : 0.8f;
		DrawRect(Frame, X - B - Grow, SwatchY - B - Grow,
			S + 2.0f * (B + Grow), S + 2.0f * (B + Grow));
		FLinearColor Swatch = FNiceInkPalette::Get(i);
		Swatch.A = 1.0f;
		DrawRect(Swatch, X - Grow, SwatchY - Grow, S + 2.0f * Grow, S + 2.0f * Grow);
		// 鍵位標（1..9,0）：置於色塊下、提示行上
		DrawTok(FString::Printf(TEXT("%d"), (i + 1) % 10), X + S * 0.5f,
			SwatchY + S + 5.0f * UiScale, ETextTier::Small,
			bSel ? NiHudColor::Paper : NiHudColor::PaperDim, EHAlign::Center, bSel);
	}
}

void ANiceInkHUD::DrawBlindOverlay(const ANiceInkCharacter* MyChar)
{
	// 被噴致盲（該回合內）：大面積色漬遮擋視線，指認結算時解除
	if (!MyChar || !MyChar->bBlinded)
	{
		return;
	}
	FLinearColor Splat;
	switch (MyChar->BlindType)
	{
	case EInkEvidenceType::Sneeze: Splat = FLinearColor(0.5f, 0.62f, 0.3f, 0.93f); break;
	case EInkEvidenceType::Piss:   Splat = FLinearColor(0.8f, 0.68f, 0.1f, 0.93f); break;
	default:                       Splat = FLinearColor(0.24f, 0.13f, 0.04f, 0.95f); break;
	}
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	// 不規則遮蔽：幾塊交疊大色塊，留小縫（部分致盲）
	DrawRect(Splat, 0.0f, 0.0f, W * 0.62f, H * 0.75f);
	DrawRect(Splat, W * 0.45f, H * 0.18f, W * 0.55f, H * 0.62f);
	DrawRect(Splat, W * 0.12f, H * 0.55f, W * 0.72f, H * 0.45f);
	DrawRect(Splat, W * 0.3f, 0.0f, W * 0.5f, H * 0.3f);
	DrawTok(TEXT("SPLAT!"), W * 0.5f, H * 0.4f, ETextTier::Display, NiHudColor::Paper, EHAlign::Center, true);
	DrawTok(TEXT("washes off at the accusation"), W * 0.5f, H * 0.4f + 52.0f * UiScale, ETextTier::Small, NiHudColor::Paper, EHAlign::Center, false);
}

void ANiceInkHUD::DrawVictimSleepUI(ANiceInkCharacter* MyChar, const ANiceInkGameState* GS, const ANiceInkPlayerState* MyPS)
{
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;

	// 視覺全遮蔽：看不到任何人、任何筆跡、自己身上任何刺青
	DrawRect(FLinearColor(0.01f, 0.01f, 0.015f, 1.0f), 0.0f, 0.0f, W, H);

	if (!GS)
	{
		return;
	}

	// 標題＋自己的罰酒（第三杯＝生死，沉睡者最需要盯的數字）
	DrawTok(TEXT("DRUNK DREAM"), W * 0.5f, 20.0f * UiScale, ETextTier::Title, NiHudColor::Lavender, EHAlign::Center, true);
	if (const ANiceInkPlayerState* NIPS = MyPS)
	{
		DrawCupsRow(W * 0.5f, 56.0f * UiScale, 24.0f * UiScale, NIPS->PenaltyCups, EHAlign::Center);
	}

	// 醉夢描圖（SPEC v4.0 定案 #49）：沿線描完＝甦醒；描出線＝重來
	UDreamTraceComponent* Trace = MyChar->DreamTrace;
	UDreamMazeComponent* Maze = MyChar->DreamMaze;
	if (Trace && Trace->IsTraceActive())
	{
		// 整圖入鏡（08-02 user 裁決：圖案是夢裡唯一的參考系——速度=完成時間與
		// 針速/帶寬比，皆縮放不變；畫面歸針/像素倍率同源已退役）。
		// 可用矩形＝杯數列之下、底部提示行之上、左右留邊——包圍盒貼合最大化
		//（圓形面板＋MaxAbsR 半徑貼合對非圓圖案縮到三~五成＝「圖太小」病根；
		// 進度%併入提示行＝整條 H*0.86 保留帶還給圖）；姿勢面板＝撞到才讓位。
		const FBox2D PanelAvail(
			FVector2D(50.0f * UiScale, 95.0f * UiScale),
			FVector2D(W - 50.0f * UiScale, H - 70.0f * UiScale));
		const FBox2D PanelAvoid(
			FVector2D(34.0f * UiScale, H - 246.0f * UiScale),
			FVector2D(234.0f * UiScale, H - 34.0f * UiScale));
		Trace->DrawTracePanel(Canvas, PanelAvail, PanelAvoid);

		// 事件行（搖晃顯名＝怒氣要有地址；失敗＝當場明講重來）——蓋在圖上、瞬態
		if (Trace->IsShakeActive())
		{
			DrawTok(FString::Printf(TEXT("%s SHAKES YOUR DREAM !"), *Trace->GetShakeAttackerName().ToUpper()),
				W * 0.5f, H * 0.16f, ETextTier::Display,
				NiHudColor::Red, EHAlign::Center, true);
		}
		else if (Trace->IsFailFlashing())
		{
			DrawTok(TEXT("SLIPPED — BACK TO THE START"),
				W * 0.5f, H * 0.16f, ETextTier::Title,
				NiHudColor::Red, EHAlign::Center, true);
		}
		DrawBottomHint(FString::Printf(TEXT("hold LMB — trace the line to wake · %d%%"),
			FMath::RoundToInt(Trace->GetProgress01() * 100.0f)), NiHudColor::Lavender);
	}
	else if (Maze && Maze->IsMazeActive())
	{
		const FVector2D PanelCenter(W * 0.5f, H * 0.46f);
		const float PanelRadius = FMath::Min(W, H) * 0.30f;
		Maze->DrawMazePanel(Canvas, PanelCenter, PanelRadius);

		switch (Maze->GetSimState())
		{
		case EDreamMazeSimState::Walking:
			DrawBottomHint(TEXT("hold LMB — walk out of the dream to wake"), NiHudColor::Lavender);
			break;
		case EDreamMazeSimState::DeathScreen:
		{
			// 兇手公開（怒氣要有地址）；度數永不顯示
			const APlayerState* KillerPS = GS->FindPlayerStateById(Maze->GetLastKillerId());
			DrawRect(FLinearColor(0.4f, 0.02f, 0.02f, 0.35f), 0.0f, 0.0f, W, H);
			// 整組抬離中央光圈（截圖自查：壓在圓盤上讀感髒）
			const float TextY = PanelCenter.Y - PanelRadius * 0.52f;
			const float IconS = 64.0f * UiScale;
			DrawIconTok(IconTrap, PanelCenter.X - IconS * 0.5f, TextY - IconS - 12.0f * UiScale, IconS, NiHudColor::Red);
			DrawTok(FString::Printf(TEXT("TRAPPED BY %s !"), KillerPS ? *KillerPS->GetPlayerName().ToUpper() : TEXT("???")),
				PanelCenter.X, TextY, ETextTier::Display, NiHudColor::Red, EHAlign::Center, true);
			break;
		}
		case EDreamMazeSimState::AwaitRotation:
		case EDreamMazeSimState::Rotating:
			// 不給任何關於度數的文字——盯緊圓形自己抓定位點
			DrawTok(TEXT("the dream reels..."),
				PanelCenter.X, PanelCenter.Y + PanelRadius + 14.0f * UiScale, ETextTier::Body, NiHudColor::Lavender, EHAlign::Center, false);
			break;
		default:
			break;
		}
	}
	else
	{
		// 沒有迷宮＝終局昏死（server 不發夢）：昏睡不醒
		DrawTok(TEXT("OUT COLD"), W * 0.5f, H * 0.4f, ETextTier::Display, NiHudColor::Red, EHAlign::Center, true);
		DrawTok(TEXT("the room helps itself to your cash..."), W * 0.5f, H * 0.4f + 52.0f * UiScale, ETextTier::Body, NiHudColor::PaperDim, EHAlign::Center, false);
	}

	// 姿勢面板（左下）：自己身體的示意——當前姿勢與朝向。
	// 永不顯示身上墨跡的即時變化（SPEC 護欄）。
	{
		const float PanelW = 200.0f * UiScale;
		const float PanelH = 212.0f * UiScale;
		const float PanelX = 34.0f * UiScale;
		const float PanelY = H - PanelH - 34.0f * UiScale;
		DrawPanelBox(PanelX, PanelY, PanelW, PanelH, 0.85f);
		DrawTok(MyChar->bBodyFaceDown ? TEXT("POSE — FACE DOWN") : TEXT("POSE — FACE UP"),
			PanelX + PanelW * 0.5f, PanelY + 10.0f * UiScale, ETextTier::Small,
			MyChar->bBodyFaceDown ? NiHudColor::Amber : NiHudColor::Paper, EHAlign::Center, true);

		// 極簡人形俯視圖：頭＋軀幹＋四肢大字
		const float BodyCX = PanelX + PanelW * 0.5f;
		const float BodyCY = PanelY + PanelH * 0.56f;
		const float B = UiScale;
		const FLinearColor BodyColor = NiHudColor::Skin;
		DrawRect(BodyColor, BodyCX - 10.0f * B, BodyCY - 68.0f * B, 20.0f * B, 20.0f * B);           // 頭
		DrawRect(BodyColor, BodyCX - 16.0f * B, BodyCY - 46.0f * B, 32.0f * B, 66.0f * B);           // 軀幹
		Canvas->K2_DrawLine(FVector2D(BodyCX - 14.0f * B, BodyCY - 40.0f * B), FVector2D(BodyCX - 48.0f * B, BodyCY - 10.0f * B), 6.0f * B, BodyColor); // 左臂
		Canvas->K2_DrawLine(FVector2D(BodyCX + 14.0f * B, BodyCY - 40.0f * B), FVector2D(BodyCX + 48.0f * B, BodyCY - 10.0f * B), 6.0f * B, BodyColor); // 右臂
		Canvas->K2_DrawLine(FVector2D(BodyCX - 9.0f * B, BodyCY + 20.0f * B), FVector2D(BodyCX - 33.0f * B, BodyCY + 62.0f * B), 6.0f * B, BodyColor);  // 左腿
		Canvas->K2_DrawLine(FVector2D(BodyCX + 9.0f * B, BodyCY + 20.0f * B), FVector2D(BodyCX + 33.0f * B, BodyCY + 62.0f * B), 6.0f * B, BodyColor);  // 右腿
	}

	// 技能庫存（右下）：噴射（v4.0 定案 #51 噴射拳腳移出核心循環——雙閘全關＝
	// 整列不畫；未來更新回歸時原地起用）
	if (GNiceInkSprayEnabled || GNiceInkKickEnabled)
	{
		static const TCHAR* OriginNames[] = { TEXT("NOSE"), TEXT("CROTCH"), TEXT("BUTT") };
		const int32 OriginIdx = FMath::Clamp(static_cast<int32>(MyChar->SelectedSprayOrigin), 0, 2);
		const FString HintText = GNiceInkKickEnabled
			? FString::Printf(TEXT("Q spray from %s  ·  E kick  ·  arrows aim"), OriginNames[OriginIdx])
			: FString::Printf(TEXT("Q spray from %s (1/2/3)  ·  arrow keys aim"), OriginNames[OriginIdx]);

		// 面板寬＝量測提示行自動定寬（固定寬在小視窗溢出——截圖自查教訓）
		const float Pad = 18.0f * UiScale;
		const float PanelW = MeasureTok(HintText, ETextTier::Small, false).X + Pad * 2.0f;
		const float PanelH = 92.0f * UiScale;
		const float PanelX = W - PanelW - 34.0f * UiScale;
		const float PanelY = H - PanelH - 34.0f * UiScale;
		DrawPanelBox(PanelX, PanelY, PanelW, PanelH, 0.85f);

		const float IconS = 30.0f * UiScale;
		float X = PanelX + Pad;
		const float RowY = PanelY + 14.0f * UiScale;
		const FLinearColor SprayTint = MyChar->SprayCharges > 0 ? NiHudColor::Paper : NiHudColor::PaperDim;
		DrawIconTok(IconSpray, X, RowY, IconS, SprayTint);
		X += IconS + 6.0f * UiScale;
		X += DrawTok(FString::Printf(TEXT("x%d"), MyChar->SprayCharges), X, RowY + 3.0f * UiScale, ETextTier::Body, SprayTint, EHAlign::Left, true).X;
		if (GNiceInkKickEnabled)
		{
			X += 26.0f * UiScale;
			const FLinearColor KickTint = MyChar->KickCharges > 0 ? NiHudColor::Paper : NiHudColor::PaperDim;
			DrawIconTok(IconKick, X, RowY, IconS, KickTint);
			X += IconS + 6.0f * UiScale;
			DrawTok(FString::Printf(TEXT("x%d"), MyChar->KickCharges), X, RowY + 3.0f * UiScale, ETextTier::Body, KickTint, EHAlign::Left, true);
		}

		DrawTok(HintText, PanelX + PanelW * 0.5f, PanelY + PanelH - 30.0f * UiScale, ETextTier::Small, NiHudColor::PaperDim, EHAlign::Center, false);
	}
}

void ANiceInkHUD::DrawTrapDial(const ANiceInkCharacter* MyChar)
{
	if (!MyChar || !MyChar->bTrapDialActive || !GetWorld())
	{
		return;
	}

	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	const FVector2D Center(W - 200.0f * UiScale, H * 0.38f);
	const float Radius = 70.0f * UiScale;
	const float Remaining = FMath::Max(0.0f, MyChar->TrapDialEndTime - GetWorld()->GetTimeSeconds());

	DrawTok(TEXT("HE STEPPED ON YOU"), Center.X, Center.Y - Radius - 74.0f * UiScale, ETextTier::Body, NiHudColor::Paper, EHAlign::Center, true);
	DrawTok(TEXT("SPIN HIS DREAM"), Center.X, Center.Y - Radius - 48.0f * UiScale, ETextTier::Title, NiHudColor::Amber, EHAlign::Center, true);

	// 盤面
	Canvas->K2_DrawPolygon(nullptr, Center, FVector2D(Radius + 10.0f * UiScale, Radius + 10.0f * UiScale), 32, FLinearColor(0.05f, 0.03f, 0.08f, 0.85f));
	for (int32 Tick = 0; Tick < 8; ++Tick)
	{
		const float Phi = Tick * PI / 4.0f;
		const FVector2D Dir(FMath::Sin(Phi), -FMath::Cos(Phi));
		Canvas->K2_DrawLine(Center + Dir * (Radius - 8.0f * UiScale), Center + Dir * Radius, 2.0f * UiScale, FLinearColor(0.4f, 0.35f, 0.5f, 1.0f));
	}
	const float RotIcon = 30.0f * UiScale;
	FLinearColor RotTint = NiHudColor::Lavender;
	RotTint.A = 0.45f;
	DrawIconTok(IconRotate, Center.X - RotIcon * 0.5f, Center.Y - RotIcon * 0.5f, RotIcon, RotTint);

	// 指針（正度數＝順時針）；CW 橘／CCW 青
	const float NeedlePhi = FMath::DegreesToRadians(MyChar->TrapDialAngleDeg);
	const FVector2D NeedleDir(FMath::Sin(NeedlePhi), -FMath::Cos(NeedlePhi));
	const FLinearColor NeedleColor = MyChar->TrapDialAngleDeg >= 0.0f
		? FLinearColor(1.0f, 0.55f, 0.15f, 1.0f) : FLinearColor(0.2f, 0.8f, 0.9f, 1.0f);
	Canvas->K2_DrawLine(Center, Center + NeedleDir * (Radius - 6.0f * UiScale), 4.0f * UiScale, NeedleColor);

	// 倒數條
	const float BarW = (Radius * 2.0f) * FMath::Clamp(Remaining / FMath::Max(0.1f, MyChar->TrapDialDuration), 0.0f, 1.0f);
	DrawRect(FLinearColor(0.9f, 0.2f, 0.15f, 0.9f), Center.X - Radius, Center.Y + Radius + 14.0f * UiScale, BarW, 5.0f * UiScale);

	DrawTok(FString::Printf(TEXT("%+.0f°"), MyChar->TrapDialAngleDeg),
		Center.X, Center.Y + Radius + 24.0f * UiScale, ETextTier::Title, NeedleColor, EHAlign::Center, true);
	DrawTok(FString::Printf(TEXT("scroll wheel  ·  locks in %.1fs"), Remaining),
		Center.X, Center.Y + Radius + 56.0f * UiScale, ETextTier::Small, NiHudColor::PaperDim, EHAlign::Center, false);
}

void ANiceInkHUD::DrawInkCrosshair(const ANiceInkGameState* GS, const ANiceInkPlayerState* MyPS)
{
	if (!Canvas)
	{
		return;
	}

	FLinearColor CrosshairColor = FLinearColor::White;
	if (const ANiceInkCharacter* MyChar = PlayerOwner ? Cast<ANiceInkCharacter>(PlayerOwner->GetPawn()) : nullptr)
	{
		if (MyChar->bAsleep)
		{
			return; // 沉睡：無準星（黑屏＋小遊戲）
		}
		CrosshairColor = MyChar->GetCurrentColor();
		CrosshairColor.A = 1.0f;

		// 筆即游標（07-20 定案）：UI 準星退役——世界上只有一支實體筆。
		// 接觸墨點＝筆尖可落墨時，在筆尖的螢幕投影畫一粒選色小點（錨在筆尖、
		// 不在螢幕中心——任何中心標記都會重新製造「兩支筆」的對照）；
		// 搆不到＝無點（筆懸空本身就是訊號），持續 >1s 由底部提示補一句話。
		if (MyChar->bLeanLocked)
		{
			// 準星歸 2D（07-22 user 定案）：落點指示固定畫在螢幕正中心（零抖動、
			// FPS 標準）——舊制「錨在世界筆尖投影」的小點會吃到解算殘差＋濾波相位差
			// 的像素跳動；「兩支筆」禁令針對的矛盾源（實體筆＋UI 準星並存）在 2D 筆制
			// 下已不存在。墨照舊從世界針尖出（偏差=解算容差 ≤1.5cm、感知可忽略）。
			// 可落墨才顯示（搆不到=無點的訊號語義保留）。
			// 07-24 皮繩制二修：畫面屬於針（作畫中相機=針 aim）⇒ 螢幕中心構造上
			// 恆=針尖=墨的出生點——2D 筆/落點/針線錨死中心即與墨重合，viewmodel
			// 恆定（07-22 定案）不破。（一修曾把筆錨到針的投影＝筆離開中心，被
			// user 打回「筆要維持在螢幕中間」——正解是畫面歸針，不是筆追針。）
			// 三工具制（07-25 打稿制）：打稿筆=龍膽紫小方點＋2D 麥克筆 viewmodel；
			// 液線針=選色小方點；霧針=筆刷範圍圈（自由揮掃要知道落在哪圈）
			const bool bShaderNeedle = MyChar->SelectedNeedle == EInkNeedle::Shader;
			const bool bStencilPen = MyChar->SelectedNeedle == EInkNeedle::Stencil;
			// 錨點：機器工具=螢幕中心（畫面歸針＝中心構造上恆=針尖）；稿筆游標制
			//（07-31）=游標的螢幕投影（相機=惰性 gaze、游標在畫面上自由移動——
			// 小畫家式；投影必用 Canvas->Project＝HUD 投影鐵律）
			FVector2D Aim(Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f);
			if (bStencilPen)
			{
				FVector CursorW;
				if (MyChar->GetStencilCursorHudWorld(CursorW))
				{
					const FVector Pr = Project(CursorW);
					if (Pr.Z > 0.0f)
					{
						Aim = FVector2D(Pr.X, Pr.Y);
					}
				}
			}
			// 落墨小點/✕ 讀的是墨閘同一個裁決（07-29 單一裁判）——皮膚紗在稜線
			// 掠射角會被透視壓成看不見的細縫（腳掌實錘），筆尖級提示任何角度都準
			const bool bReach = MyChar->IsCursorDrawable();
			if (!bReach && MyChar->HasDrawTarget())
			{
				// 游標點不可畫＝筆尖紅 ✕（硬切、美術語言 #24）
				const float A = 7.0f * UiScale;
				const FLinearColor XCol = NiHudColor::Red.CopyWithNewOpacity(0.9f);
				DrawLine(Aim.X - A, Aim.Y - A, Aim.X + A, Aim.Y + A, XCol, 2.5f * UiScale);
				DrawLine(Aim.X - A, Aim.Y + A, Aim.X + A, Aim.Y - A, XCol, 2.5f * UiScale);
			}
			if (bReach && !bShaderNeedle)
			{
				const float B = UiScale;
				const FLinearColor DotCol = bStencilPen ? NiceInkStencil::Color() : CrosshairColor;
				DrawRect(DotCol, Aim.X - 2.5f * B, Aim.Y - 2.5f * B, 5.0f * B, 5.0f * B);
			}
			if (bStencilPen)
			{
				// 2D 麥克筆 viewmodel（07-25 二修 user 定案「與機器同構：旁人 3D＋本人
				// 2D」；貼圖版=SM_Marker 染紫 Blender 渲染 T_UI_MarkerPen）：
				// 貼圖以筆尖為樞軸右傾 30°（同機器握姿）、按住 LMB＝筆壓近落點（壓筆感）。
				const bool bInkingPen = MyChar->IsPenTriggerHeldLocal() && bReach;
				constexpr float PenTiltDeg = 30.0f;
				const float TiltRad = FMath::DegreesToRadians(PenTiltDeg);
				const FVector2D AxisUp(FMath::Sin(TiltRad), -FMath::Cos(TiltRad));
				const float GapPx = (bInkingPen ? 2.0f : 14.0f) * UiScale;
				// 拉繩穩定器（08-02）：繪製中 2D 筆錨到拉繩墨尖（筆尖=墨出處；
				// 游標先走、筆沿平滑路徑追=Lazy Mouse 讀感）；小點/✕ 照舊錨生游標
				FVector2D PenAnchor = Aim;
				FVector LazyW;
				if (MyChar->GetStencilLazyTipHudWorld(LazyW))
				{
					const FVector Pr = Project(LazyW);
					if (Pr.Z > 0.0f)
					{
						PenAnchor = FVector2D(Pr.X, Pr.Y);
					}
				}
				const FVector2D TipPt = PenAnchor + AxisUp * GapPx;
				if (MarkerSprite)
				{
					// 筆尖在貼圖內的正規化座標（render_marker_ui.py 印出）
					constexpr float TipU = 0.5000f, TipV = 0.9310f;
					const float SpriteW = Canvas->ClipY * 0.46f; // 方形貼圖邊長（筆長≈0.4×ClipY）
					DrawTexture(MarkerSprite, TipPt.X - TipU * SpriteW, TipPt.Y - TipV * SpriteW,
						SpriteW, SpriteW, 0.0f, 0.0f, 1.0f, 1.0f, FLinearColor::White,
						EBlendMode::BLEND_Translucent, 1.0f, false,
						PenTiltDeg, FVector2D(TipU, TipV));
					DrawTok(TEXT("STENCIL"), TipPt.X + 22.0f * UiScale, TipPt.Y - 8.0f * UiScale,
						ETextTier::Small, NiHudColor::PaperDim, EHAlign::Left, false);
				}
				else
				{
					// 貼圖缺席退路：向量筆（深筆頭＋紫筆桿＋淺尾帽）
					const FVector2D NibEnd = TipPt + AxisUp * 26.0f * UiScale;
					const FVector2D BodyEnd = TipPt + AxisUp * (Canvas->ClipY * 0.34f);
					const FVector2D CapEnd = BodyEnd + AxisUp * 16.0f * UiScale;
					const FLinearColor BodyCol = NiceInkStencil::Color() * FLinearColor(0.6f, 0.6f, 0.6f, 1.0f);
					DrawLine(NibEnd.X, NibEnd.Y, BodyEnd.X, BodyEnd.Y, BodyCol, 40.0f * UiScale);
					DrawLine(BodyEnd.X, BodyEnd.Y, CapEnd.X, CapEnd.Y,
						FLinearColor(0.82f, 0.80f, 0.86f, 1.0f), 40.0f * UiScale);
					DrawLine(TipPt.X, TipPt.Y, NibEnd.X, NibEnd.Y,
						FLinearColor(0.10f, 0.04f, 0.16f, 1.0f), 18.0f * UiScale);
					DrawTok(TEXT("STENCIL"), NibEnd.X + 18.0f * UiScale, NibEnd.Y - 8.0f * UiScale,
						ETextTier::Small, NiHudColor::PaperDim, EHAlign::Left, false);
				}
			}
			if (bShaderNeedle && bReach)
			{
				FVector TipW;
				if (MyChar->GetPenTipWorldForHud(TipW))
				{
					const FVector P1 = Project(TipW);
					const FVector P2 = Project(TipW + FVector(0.0f, 0.0f, MyChar->ShaderBrushRadiusCm));
					if (P1.Z > 0.0f && P2.Z > 0.0f)
					{
						const float Rpx = FMath::Clamp(FVector2D::Distance(
							FVector2D(P1.X, P1.Y), FVector2D(P2.X, P2.Y)), 8.0f, 600.0f);
						FLinearColor RingCol = CrosshairColor;
						RingCol.A = 0.55f;
						constexpr int32 Segs = 28;
						for (int32 i = 0; i < Segs; ++i)
						{
							const float A0 = 2.0f * PI * i / Segs;
							const float A1 = 2.0f * PI * (i + 1) / Segs;
							DrawLine(Aim.X + FMath::Cos(A0) * Rpx, Aim.Y + FMath::Sin(A0) * Rpx,
								Aim.X + FMath::Cos(A1) * Rpx, Aim.Y + FMath::Sin(A1) * Rpx,
								RingCol, 1.5f * UiScale);
						}
					}
				}
			}

			// 巡航導引（07-22 二改「業界式」）：皮膚面預測路徑＋行進蟻虛線——
			// 巡航步進器往前模擬 8cm 的貼膚曲線投影到螢幕，沿弧長畫虛線；
			// 虛線相位隨時間向前滾動（marching ants＝方向零歧義）、遠端漸淡、
			// 線停在剪影邊緣＝「針會在這裡釘住」的預告。指「路徑」不是位置目標點
			//（不重蹈「兩支筆」雙指標矛盾）；放開左鍵/收回死區即消失。
			{
				TArray<FVector> GuidePts;
				if (MyChar->BuildTattooGuidePath(GuidePts) >= 2)
				{
					TArray<FVector2D> Scr;
					Scr.Reserve(GuidePts.Num());
					for (const FVector& W : GuidePts)
					{
						const FVector Pr = Project(W);
						if (Pr.Z <= 0.0f)
						{
							break; // 相機後方（理論上不會發生）
						}
						Scr.Add(FVector2D(Pr.X, Pr.Y));
					}
					// 折線平滑（07-22 三修）：retopo 大三角面上逐步 trace 的落點微鋸齒
					// ＋模擬步長不完全均勻→折線毛邊；3 點滑動平均殺面片鋸齒、
					// 保留真曲率（端點錨定：起點釘針尖、終點釘邊緣截斷）
					if (Scr.Num() >= 3)
					{
						TArray<FVector2D> Smoothed = Scr;
						for (int32 i = 1; i < Scr.Num() - 1; ++i)
						{
							Smoothed[i] = (Scr[i - 1] + Scr[i] + Scr[i + 1]) / 3.0f;
						}
						Scr = MoveTemp(Smoothed);
					}
					float TotalPx = 0.0f;
					for (int32 i = 1; i < Scr.Num(); ++i)
					{
						TotalPx += FVector2D::Distance(Scr[i - 1], Scr[i]);
					}
					if (Scr.Num() >= 2 && TotalPx > 8.0f)
					{
						const float DashPx = 7.0f * UiScale;
						const float GapPx2 = 5.0f * UiScale;
						const float Period = DashPx + GapPx2;
						const float SkipPx = 12.0f * UiScale; // 讓開中心落點
						// 行進蟻：相位隨時間增加＝dash 沿路徑向外滾（前進方向）
						const float MarchPx = FMath::Fmod(static_cast<float>(
							GetWorld()->GetTimeSeconds()) * 36.0f * UiScale, Period);
						float S = 0.0f; // 投影路徑弧長（px）
						for (int32 i = 1; i < Scr.Num(); ++i)
						{
							const FVector2D A = Scr[i - 1];
							const FVector2D Bp = Scr[i];
							const float L = FVector2D::Distance(A, Bp);
							if (L <= KINDA_SMALL_NUMBER)
							{
								continue;
							}
							const FVector2D Dn = (Bp - A) / L;
							const float SEnd = S + L;
							// 段內 on 區間：s ≡ MarchPx (mod Period) 起、長 DashPx
							float Base = MarchPx +
								FMath::FloorToFloat((S - MarchPx) / Period) * Period;
							for (; Base < SEnd; Base += Period)
							{
								const float On0 = FMath::Max(FMath::Max(Base, S), SkipPx);
								const float On1 = FMath::Min(Base + DashPx, SEnd);
								if (On1 <= On0)
								{
									continue;
								}
								// 遠端漸淡（近端 0.9 → 尾端 0.25）
								const float Fade = 1.0f - 0.72f *
									FMath::Clamp(On0 / FMath::Max(TotalPx, 1.0f), 0.0f, 1.0f);
								const FLinearColor C(0.95f, 0.95f, 0.95f, 0.9f * Fade);
								const FVector2D P0 = A + Dn * (On0 - S);
								const FVector2D P1 = A + Dn * (On1 - S);
								DrawLine(P0.X, P0.Y, P1.X, P1.Y, C, 2.0f * UiScale);
							}
							S = SEnd;
						}
					}
				}
			}

			// FP 2D 筆（07-22「要 2D 感」二改：user 定案「筆尖要對齊落筆點＋筆身像
			// 真人握筆右傾」）：貼圖以出針口為樞軸右傾 PEN_TILT、出針口錨在準星
			// 沿筆軸外推一小段——待機=留間隙+針樁（收）、按住 LMB=針線補滿間隙且
			// 機身微壓近（伸/壓筆感）。恆定大小角度=viewmodel 體感不變。
			if (PenSprite && MyChar->IsPenMachineMode() && !bStencilPen)
			{
				const bool bInking = MyChar->IsPenTriggerHeldLocal() && bReach;
				constexpr float PenTiltDeg = 30.0f;                    // 右傾（人握筆攻角）
				constexpr float MuzU = 0.4716f, MuzV = 0.9080f;        // 出針口在貼圖內的正規化座標
				const float SpriteW = Canvas->ClipY * 0.48f;           // 方形貼圖邊長
				const float TiltRad = FMath::DegreesToRadians(PenTiltDeg);
				// 筆軸方向（準星→機身）：右上
				const FVector2D AxisUp(FMath::Sin(TiltRad), -FMath::Cos(TiltRad));
				const float GapPx = (bInking ? 16.0f : 30.0f) * UiScale; // 壓筆=機身微壓近
				const FVector2D Muz = Aim + AxisUp * GapPx;
				const FLinearColor NeedleCol(0.78f, 0.80f, 0.84f, 1.0f); // 針鋼
				// 針線粗細隨針型（shader=粗針視覺；切針的即時回饋之一）
				const float NeedlePx = (bShaderNeedle ? 5.0f : 3.0f) * UiScale;
				if (bInking)
				{
					// 伸：針線補滿出針口→準星（線先畫、貼圖蓋線頭）
					DrawLine(Muz.X, Muz.Y, Aim.X, Aim.Y, NeedleCol, NeedlePx);
				}
				else
				{
					// 收：短樁指向準星（讀出「會往哪扎」）
					const FVector2D Stub = Muz - AxisUp * 12.0f * UiScale;
					DrawLine(Muz.X, Muz.Y, Stub.X, Stub.Y, NeedleCol, NeedlePx);
				}
				// 以出針口為樞軸旋轉（RotPivot=貼圖內正規化座標）
				DrawTexture(PenSprite, Muz.X - MuzU * SpriteW, Muz.Y - MuzV * SpriteW,
					SpriteW, SpriteW, 0.0f, 0.0f, 1.0f, 1.0f, FLinearColor::White,
					EBlendMode::BLEND_Translucent, 1.0f, false,
					PenTiltDeg, FVector2D(MuzU, MuzV));
				// 針型標籤（07-23 雙針制）：常駐在出針口旁——狀態永遠可讀、切針即時回饋
				DrawTok(bShaderNeedle ? TEXT("SHADER") : TEXT("LINER"),
					Muz.X + 22.0f * UiScale, Muz.Y - 8.0f * UiScale,
					ETextTier::Small, NiHudColor::PaperDim, EHAlign::Left, bShaderNeedle);
			}
			return;
		}

		// 未鎖定：作畫階段才給「湊上去」提示（受害者本人無畫可下，不提示）
		const bool bCanLeanPrompt = GS && GS->CurrentPhase == ENiceInkPhase::Drawing &&
			(!MyPS || GS->VictimPlayerId != MyPS->GetPlayerId());
		if (bCanLeanPrompt)
		{
			// 提示放色塊下方（+30 會撞到準星右下的選色色塊）
			DrawTok(TEXT("RMB — lean in"), Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f + 52.0f * UiScale,
				ETextTier::Small, NiHudColor::PaperDim, EHAlign::Center, false);
		}

	}

	const float CenterX = Canvas->ClipX * 0.5f;
	const float CenterY = Canvas->ClipY * 0.5f;
	const float Arm = 7.0f * UiScale;
	const float Thickness = 2.0f * UiScale;

	DrawRect(CrosshairColor, CenterX - Arm, CenterY - Thickness * 0.5f, Arm * 2.0f, Thickness);
	DrawRect(CrosshairColor, CenterX - Thickness * 0.5f, CenterY - Arm, Thickness, Arm * 2.0f);

	// 目前選色色塊（準星右下）
	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f), CenterX + 14.0f * UiScale, CenterY + 14.0f * UiScale, 24.0f * UiScale, 24.0f * UiScale);
	DrawRect(CrosshairColor, CenterX + 17.0f * UiScale, CenterY + 17.0f * UiScale, 18.0f * UiScale, 18.0f * UiScale);
}

void ANiceInkHUD::DrawDebugPanel(const ANiceInkGameState* GS, const ANiceInkPlayerState* MyPS, ANiceInkCharacter* MyChar)
{
	if (CVarNiDebugHud.GetValueOnGameThread() == 0)
	{
		return;
	}
	const float M = 16.0f * UiScale;
	const float LineH = 22.0f * UiScale;
	DrawPanelBox(M, M, 470.0f * UiScale, LineH * 4.0f + 16.0f * UiScale, 0.6f);
	float Y = M + 8.0f * UiScale;
	DrawTok(TEXT("hud-v1  ·  ni.DebugHud 1"), M + 10.0f * UiScale, Y, ETextTier::Small, NiHudColor::Green, EHAlign::Left, true);
	Y += LineH;
	if (MyPS)
	{
		DrawTok(FString::Printf(TEXT("me: %s  seat %d  id %d"), *MyPS->GetPlayerName(), MyPS->SeatIndex, MyPS->GetPlayerId()),
			M + 10.0f * UiScale, Y, ETextTier::Small, NiHudColor::PaperDim, EHAlign::Left, false);
	}
	Y += LineH;
	if (GS)
	{
		DrawTok(FString::Printf(TEXT("phase %d  round %d  victim id %d"), static_cast<int32>(GS->CurrentPhase), GS->CurrentRound, GS->VictimPlayerId),
			M + 10.0f * UiScale, Y, ETextTier::Small, NiHudColor::PaperDim, EHAlign::Left, false);
	}
	Y += LineH;
	DrawTok(TEXT("console: NiStart | NiEmerge | NiAccuse <workNo> <seat>"),
		M + 10.0f * UiScale, Y, ETextTier::Small, NiHudColor::PaperDim, EHAlign::Left, false);

	// 描圖現場（v4.0）：受害者端即時 summary
	if (MyChar && MyChar->DreamTrace && MyChar->DreamTrace->IsTraceActive())
	{
		Y += LineH * 1.2f;
		DrawTok(MyChar->DreamTrace->GetDebugSummary(), M + 10.0f * UiScale, Y,
			ETextTier::Small, NiHudColor::Green, EHAlign::Left, false);
	}

	// 迷宮現場（出口卡死診斷 2026-07-15）：受害者端即時 summary，兩行折顯
	if (MyChar && MyChar->DreamMaze && MyChar->DreamMaze->IsMazeActive())
	{
		const FString Summary = MyChar->DreamMaze->GetDebugSummary();
		FString L1 = Summary;
		FString L2;
		Summary.Split(TEXT(" cpS="), &L1, &L2);
		Y += LineH * 1.2f;
		DrawTok(L1, M + 10.0f * UiScale, Y, ETextTier::Small, NiHudColor::Green, EHAlign::Left, false);
		Y += LineH;
		DrawTok(TEXT("cpS=") + L2, M + 10.0f * UiScale, Y, ETextTier::Small, NiHudColor::Green, EHAlign::Left, false);
	}
}

FString ANiceInkHUD::GetPhaseLabel(ENiceInkPhase Phase) const
{
	switch (Phase)
	{
	case ENiceInkPhase::Lobby: return TEXT("LOBBY");
	case ENiceInkPhase::BottleSpin: return TEXT("BOTTLE SPIN");
	case ENiceInkPhase::Seating: return TEXT("SEATING");
	case ENiceInkPhase::Drawing: return TEXT("DRAWING");
	case ENiceInkPhase::Tour: return TEXT("GALLERY TOUR");
	case ENiceInkPhase::Accusation: return TEXT("ACCUSATION");
	case ENiceInkPhase::Resolution: return TEXT("RESOLUTION");
	case ENiceInkPhase::Finale: return TEXT("FINALE");
	case ENiceInkPhase::PostGame: return TEXT("PARLOR");
	default: return TEXT("?");
	}
}
