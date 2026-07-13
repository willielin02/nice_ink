#include "NiceInkHUD.h"

#include "DreamMazeComponent.h"
#include "Engine/Canvas.h"
#include "GameFramework/PlayerState.h"
#include "InkCanvasComponent.h"
#include "NiceInkCharacter.h"
#include "NiceInkGameState.h"
#include "NiceInkPlayerState.h"
#include "NiceInkTypes.h"

// M1 除錯 HUD：相位／回合／受害者／罰酒／巡禮進度＋準星與選色。
// 正式 UI（甦醒小遊戲、姿勢面板、指認介面）是 UMG，M2/M4 實作。
void ANiceInkHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas || !GetWorld())
	{
		return;
	}

	const ANiceInkGameState* GS = GetWorld()->GetGameState<ANiceInkGameState>();
	ANiceInkCharacter* MyChar = PlayerOwner ? Cast<ANiceInkCharacter>(PlayerOwner->GetPawn()) : nullptr;
	const ANiceInkPlayerState* MyPS = PlayerOwner ? PlayerOwner->GetPlayerState<ANiceInkPlayerState>() : nullptr;

	// 沉睡端：視覺全遮蔽——黑屏＋醉夢迷宮＋姿勢面板，其他 HUD 一概不畫
	if (MyChar && MyChar->bAsleep && !MyChar->bEyesOpen)
	{
		DrawVictimSleepUI(MyChar, GS);
		return;
	}

	// 兇手轉盤覆蓋（不打斷 lean-lock 作畫；只有被踩中陷阱的兇手本人看得到）
	DrawTrapDial(MyChar);

	// 被噴致盲（該回合內）：大面積色漬遮擋視線，指認結算時解除
	if (MyChar && MyChar->bBlinded)
	{
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
		DrawText(TEXT("SPLAT! You can barely see. Washes off at the accusation."),
			FLinearColor::White, 40.0f, H * 0.5f, nullptr, 1.1f, false);
	}

	const float Padding = 18.0f;
	const float LineHeight = 21.0f;
	float Y = Padding;

	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.42f), Padding - 8.0f, Padding - 8.0f, 470.0f, 210.0f);
	DrawText(TEXT("Nice Ink"), FLinearColor::White, Padding, Y, nullptr, 1.15f, false);
	Y += LineHeight * 1.35f;

	if (GS)
	{
		FString PhaseText = FString::Printf(TEXT("Phase: %s    Round: %d"), *GetPhaseLabel(GS->CurrentPhase), GS->CurrentRound + 1);
		const float Remaining = GS->GetPhaseTimeRemaining();
		if (Remaining > 0.0f)
		{
			PhaseText += FString::Printf(TEXT("    %.0fs"), Remaining);
		}
		DrawText(PhaseText, FLinearColor(0.86f, 0.94f, 1.0f, 1.0f), Padding, Y, nullptr, 0.95f, false);
		Y += LineHeight;

		if (const APlayerState* VictimPS = GS->FindPlayerStateById(GS->VictimPlayerId))
		{
			const ANiceInkPlayerState* VictimNIPS = Cast<ANiceInkPlayerState>(VictimPS);
			const FString VictimText = FString::Printf(TEXT("Victim: %s    Cups: %d / 3"),
				*VictimPS->GetPlayerName(), VictimNIPS ? VictimNIPS->PenaltyCups : 0);
			DrawText(VictimText, FLinearColor(1.0f, 0.83f, 0.62f, 1.0f), Padding, Y, nullptr, 0.9f, false);
			Y += LineHeight;
		}

		if (GS->CurrentPhase == ENiceInkPhase::Tour && GS->TourWorkId != INDEX_NONE)
		{
			DrawText(FString::Printf(TEXT("Tour: work %d / %d"), GS->TourWorkNumber, GS->TourWorkCount),
				FLinearColor(0.9f, 1.0f, 0.8f, 1.0f), Padding, Y, nullptr, 0.9f, false);
			Y += LineHeight;
		}

		if (GS->CurrentPhase == ENiceInkPhase::Accusation && MyPS && MyChar &&
			GS->VictimPlayerId == MyPS->GetPlayerId())
		{
			DrawText(FString::Printf(TEXT("ACCUSE — previewing work %d / %d (keys 1-9)"),
				MyChar->AccusePickNumber, GS->TourWorkCount),
				FLinearColor(1.0f, 0.95f, 0.6f, 1.0f), Padding, Y, nullptr, 0.95f, false);
			Y += LineHeight;
			const APlayerState* Suspect = MyChar->GetAccuseSuspect();
			DrawText(FString::Printf(TEXT("Suspect: %s  (TAB cycle)    ENTER = accuse!"),
				Suspect ? *Suspect->GetPlayerName() : TEXT("?")),
				FLinearColor(1.0f, 0.8f, 0.5f, 1.0f), Padding, Y, nullptr, 0.95f, false);
			Y += LineHeight;
		}

		if (GS->CurrentPhase == ENiceInkPhase::Resolution)
		{
			FString ResultText = GS->LastAccusationResult == ENiceInkAccusationResult::Correct
				? TEXT("Correct! Author takes the seat.")
				: TEXT("Wrong! True author inks the picked work. +1 cup.");
			if (const APlayerState* AuthorPS = GS->FindPlayerStateById(GS->RevealedAuthorId))
			{
				ResultText += FString::Printf(TEXT("  (Author: %s)"), *AuthorPS->GetPlayerName());
			}
			DrawText(ResultText, FLinearColor(1.0f, 0.6f, 0.6f, 1.0f), Padding, Y, nullptr, 0.9f, false);
			Y += LineHeight;
		}

		if (GS->CurrentPhase == ENiceInkPhase::Finale || GS->CurrentPhase == ENiceInkPhase::PostGame)
		{
			if (const APlayerState* LoserPS = GS->FindPlayerStateById(GS->LoserPlayerId))
			{
				DrawText(FString::Printf(TEXT("FINALE: %s is out cold. Cash split, ink locked."), *LoserPS->GetPlayerName()),
					FLinearColor(1.0f, 0.4f, 0.4f, 1.0f), Padding, Y, nullptr, 0.9f, false);
				Y += LineHeight;
			}
		}

		// 場間大廳：端詳刺青、自費雷射、開下一場
		if (GS->CurrentPhase == ENiceInkPhase::PostGame && MyChar && MyChar->InkCanvas)
		{
			const int32 CarbonCount = MyChar->InkCanvas->GetWorkIdsByState(EInkWorkState::Carbon).Num();
			const int32 PermanentCount = MyChar->InkCanvas->GetWorkIdsByState(EInkWorkState::Permanent).Num();
			DrawText(FString::Printf(TEXT("LOBBY — your ink: %d carbon (laserable), %d permanent (forever)"), CarbonCount, PermanentCount),
				FLinearColor(0.7f, 0.95f, 1.0f, 1.0f), Padding, Y, nullptr, 0.9f, false);
			Y += LineHeight;
			DrawText(TEXT("L = laser oldest carbon (fades, 3rd pass removes; costs 2000)   Console: NiStart = next match"),
				FLinearColor(0.7f, 0.85f, 0.9f, 1.0f), Padding, Y, nullptr, 0.85f, false);
			Y += LineHeight;
		}
	}

	if (MyPS)
	{
		DrawText(FString::Printf(TEXT("Me: %s    Seat: %d    Cash: %d"), *MyPS->GetPlayerName(), MyPS->SeatIndex, MyPS->Cash),
			FLinearColor(0.8f, 0.9f, 1.0f, 1.0f), Padding, Y, nullptr, 0.85f, false);
		Y += LineHeight;
	}

	if (MyChar && MyChar->bAsleep && MyChar->bEyesOpen)
	{
		// 無聲甦醒中：實景視野；提示只給受害者本人
		DrawText(TEXT("Eyes open. Look around (mouse). WASD = stand up & end the drawing phase."),
			FLinearColor(1.0f, 1.0f, 0.5f, 1.0f), Padding, Y, nullptr, 0.85f, false);
		Y += LineHeight;
	}

	DrawText(TEXT("Move: WASD  Look: mouse  RMB: lean in / stand up  LMB: draw  SHIFT: peek  Palette: 1-9,0"),
		FLinearColor(0.88f, 0.88f, 0.88f, 1.0f), Padding, Y, nullptr, 0.8f, false);
	Y += LineHeight;
	DrawText(TEXT("Console: NiStart | NiEmerge | NiAccuse <workNo> <seat>"),
		FLinearColor(0.88f, 0.88f, 0.88f, 1.0f), Padding, Y, nullptr, 0.8f, false);

	DrawInkCrosshair();
}

void ANiceInkHUD::DrawVictimSleepUI(ANiceInkCharacter* MyChar, const ANiceInkGameState* GS)
{
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;

	// 視覺全遮蔽：看不到任何人、任何筆跡、自己身上任何刺青
	DrawRect(FLinearColor(0.01f, 0.01f, 0.015f, 1.0f), 0.0f, 0.0f, W, H);

	if (!GS)
	{
		return;
	}

	// 醉夢圓形迷宮（SPEC v3.3）：走出外緣出口＝甦醒
	UDreamMazeComponent* Maze = MyChar->DreamMaze;
	if (Maze && Maze->IsMazeActive())
	{
		const FVector2D PanelCenter(W * 0.5f, H * 0.46f);
		const float PanelRadius = FMath::Min(W, H) * 0.30f;
		Maze->DrawMazePanel(Canvas, PanelCenter, PanelRadius);

		switch (Maze->GetSimState())
		{
		case EDreamMazeSimState::Walking:
			DrawText(TEXT("DRUNK DREAM — mouse: cursor, HOLD LMB: walk out to wake. Others hide unseen in your dream."),
				FLinearColor(0.65f, 0.65f, 0.8f, 1.0f), PanelCenter.X - PanelRadius, PanelCenter.Y - PanelRadius - 30.0f, nullptr, 0.95f, false);
			break;
		case EDreamMazeSimState::DeathScreen:
		{
			// 兇手公開（怒氣要有地址）；度數永不顯示
			const APlayerState* KillerPS = GS->FindPlayerStateById(Maze->GetLastKillerId());
			DrawRect(FLinearColor(0.4f, 0.02f, 0.02f, 0.35f), 0.0f, 0.0f, W, H);
			DrawText(FString::Printf(TEXT("TRAPPED BY %s !"), KillerPS ? *KillerPS->GetPlayerName() : TEXT("???")),
				FLinearColor(1.0f, 0.25f, 0.2f, 1.0f), PanelCenter.X - 120.0f, PanelCenter.Y - 12.0f, nullptr, 1.6f, false);
			break;
		}
		case EDreamMazeSimState::AwaitRotation:
		case EDreamMazeSimState::Rotating:
			// 不給任何關於度數的文字——盯緊圓形自己抓定位點
			DrawText(TEXT("the dream reels..."),
				FLinearColor(0.55f, 0.5f, 0.75f, 0.8f), PanelCenter.X - 60.0f, PanelCenter.Y + PanelRadius + 12.0f, nullptr, 1.0f, false);
			break;
		default:
			break;
		}
	}
	else
	{
		// 沒有迷宮＝終局昏死（server 不發夢）：昏睡不醒
		DrawText(TEXT("OUT COLD. The room helps itself to your cash..."),
			FLinearColor(0.5f, 0.4f, 0.4f, 1.0f), W * 0.5f - 190.0f, H * 0.44f, nullptr, 1.2f, false);
	}

	// 姿勢面板（左下）：自己身體的示意——當前姿勢與朝向。
	// 永不顯示身上墨跡的即時變化（SPEC 護欄）。
	const float PanelX = 40.0f;
	const float PanelY = H - 240.0f;
	DrawRect(FLinearColor(0.06f, 0.06f, 0.08f, 1.0f), PanelX - 12.0f, PanelY - 12.0f, 220.0f, 200.0f);
	DrawText(TEXT("POSE: FACE UP"), FLinearColor(0.7f, 0.8f, 0.9f, 1.0f), PanelX, PanelY, nullptr, 0.9f, false);
	// 極簡人形俯視圖：頭（圓 → 方塊近似）＋軀幹＋四肢大字
	const float BodyCX = PanelX + 98.0f;
	const float BodyCY = PanelY + 106.0f;
	const FLinearColor BodyColor(0.55f, 0.42f, 0.34f, 1.0f);
	DrawRect(BodyColor, BodyCX - 9.0f, BodyCY - 64.0f, 18.0f, 18.0f);              // 頭
	DrawRect(BodyColor, BodyCX - 14.0f, BodyCY - 44.0f, 28.0f, 62.0f);             // 軀幹
	Canvas->K2_DrawLine(FVector2D(BodyCX - 12.0f, BodyCY - 38.0f), FVector2D(BodyCX - 44.0f, BodyCY - 10.0f), 5.0f, BodyColor);  // 左臂
	Canvas->K2_DrawLine(FVector2D(BodyCX + 12.0f, BodyCY - 38.0f), FVector2D(BodyCX + 44.0f, BodyCY - 10.0f), 5.0f, BodyColor);  // 右臂
	Canvas->K2_DrawLine(FVector2D(BodyCX - 8.0f, BodyCY + 18.0f), FVector2D(BodyCX - 30.0f, BodyCY + 58.0f), 5.0f, BodyColor);   // 左腿
	Canvas->K2_DrawLine(FVector2D(BodyCX + 8.0f, BodyCY + 18.0f), FVector2D(BodyCX + 30.0f, BodyCY + 58.0f), 5.0f, BodyColor);   // 右腿

	// 工具庫存與操作（右下）
	const float ToolY = H - 140.0f;
	static const TCHAR* OriginNames[] = { TEXT("NOSE"), TEXT("CROTCH"), TEXT("BUTT") };
	const int32 OriginIdx = FMath::Clamp(static_cast<int32>(MyChar->SelectedSprayOrigin), 0, 2);
	DrawText(FString::Printf(TEXT("SPRAY x%d   KICK x%d   (expire when you wake)"), MyChar->SprayCharges, MyChar->KickCharges),
		FLinearColor(0.85f, 0.8f, 0.6f, 1.0f), W - 520.0f, ToolY, nullptr, 0.9f, false);
	DrawText(FString::Printf(TEXT("Q spray from %s (1/2/3 pick origin)   E kick   HOLD RMB + mouse = aim"), OriginNames[OriginIdx]),
		FLinearColor(0.7f, 0.7f, 0.6f, 1.0f), W - 520.0f, ToolY + 22.0f, nullptr, 0.85f, false);

	// 聽覺開放提示
	DrawText(TEXT("You hear the whole room. Voices have no direction. They may be lying."),
		FLinearColor(0.45f, 0.45f, 0.5f, 1.0f), W * 0.25f, H - 60.0f, nullptr, 0.85f, false);
}

void ANiceInkHUD::DrawTrapDial(const ANiceInkCharacter* MyChar)
{
	if (!MyChar || !MyChar->bTrapDialActive || !GetWorld())
	{
		return;
	}

	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	const FVector2D Center(W - 170.0f, H * 0.38f);
	const float Radius = 64.0f;
	const float Remaining = FMath::Max(0.0f, MyChar->TrapDialEndTime - GetWorld()->GetTimeSeconds());

	// 盤面
	Canvas->K2_DrawPolygon(nullptr, Center, FVector2D(Radius + 10.0f, Radius + 10.0f), 32, FLinearColor(0.05f, 0.03f, 0.08f, 0.85f));
	for (int32 Tick = 0; Tick < 8; ++Tick)
	{
		const float Phi = Tick * PI / 4.0f;
		const FVector2D Dir(FMath::Sin(Phi), -FMath::Cos(Phi));
		Canvas->K2_DrawLine(Center + Dir * (Radius - 8.0f), Center + Dir * Radius, 2.0f, FLinearColor(0.4f, 0.35f, 0.5f, 1.0f));
	}
	// 指針（正度數＝順時針）；CW 橘／CCW 青
	const float NeedlePhi = FMath::DegreesToRadians(MyChar->TrapDialAngleDeg);
	const FVector2D NeedleDir(FMath::Sin(NeedlePhi), -FMath::Cos(NeedlePhi));
	const FLinearColor NeedleColor = MyChar->TrapDialAngleDeg >= 0.0f
		? FLinearColor(1.0f, 0.55f, 0.15f, 1.0f) : FLinearColor(0.2f, 0.8f, 0.9f, 1.0f);
	Canvas->K2_DrawLine(Center, Center + NeedleDir * (Radius - 6.0f), 4.0f, NeedleColor);

	// 倒數條
	const float BarW = (Radius * 2.0f) * FMath::Clamp(Remaining / FMath::Max(0.1f, MyChar->TrapDialDuration), 0.0f, 1.0f);
	DrawRect(FLinearColor(0.9f, 0.2f, 0.15f, 0.9f), Center.X - Radius, Center.Y + Radius + 16.0f, BarW, 6.0f);

	DrawText(TEXT("HE STEPPED ON YOU! SPIN HIS DREAM"),
		FLinearColor(1.0f, 0.8f, 0.4f, 1.0f), Center.X - Radius - 60.0f, Center.Y - Radius - 44.0f, nullptr, 0.95f, false);
	DrawText(FString::Printf(TEXT("%+.0f deg   (wheel; auto-locks %.1fs)"), MyChar->TrapDialAngleDeg, Remaining),
		FLinearColor(0.9f, 0.85f, 0.8f, 1.0f), Center.X - Radius - 20.0f, Center.Y + Radius + 26.0f, nullptr, 0.9f, false);
}

void ANiceInkHUD::DrawInkCrosshair()
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
			return; // 沉睡：無準星（M2 換成黑屏＋小遊戲）
		}
		CrosshairColor = MyChar->GetCurrentColor();
		CrosshairColor.A = 1.0f;

		// 貼臉鎖定：麥克筆游標取代準星（筆尖點＋筆桿斜線＋選色環）
		if (MyChar->bLeanLocked)
		{
			const FVector2D Cur = MyChar->GetLeanCursorPx();
			Canvas->K2_DrawLine(FVector2D(Cur.X + 3.0f, Cur.Y - 3.0f), FVector2D(Cur.X + 16.0f, Cur.Y - 16.0f), 4.0f, FLinearColor(0.15f, 0.15f, 0.18f, 1.0f));
			DrawRect(CrosshairColor, Cur.X - 2.0f, Cur.Y - 2.0f, 4.0f, 4.0f);
			DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f), Cur.X + 14.0f, Cur.Y + 10.0f, 18.0f, 18.0f);
			DrawRect(CrosshairColor, Cur.X + 16.0f, Cur.Y + 12.0f, 14.0f, 14.0f);
			DrawText(TEXT("Hold LMB = draw   Hold SHIFT = peek at his face   RMB/WASD = stand up   1-9,0 = color"),
				FLinearColor(0.85f, 0.85f, 0.8f, 1.0f), 40.0f, Canvas->ClipY - 40.0f, nullptr, 0.9f, false);
			return;
		}

		// 未鎖定：右鍵湊上去的提示準星
		DrawText(TEXT("RMB = lean in to draw"),
			FLinearColor(0.7f, 0.7f, 0.7f, 0.9f), Canvas->ClipX * 0.5f - 70.0f, Canvas->ClipY * 0.5f + 26.0f, nullptr, 0.8f, false);
	}

	const float CenterX = Canvas->ClipX * 0.5f;
	const float CenterY = Canvas->ClipY * 0.5f;
	const float Arm = 7.0f;
	const float Thickness = 2.0f;

	DrawRect(CrosshairColor, CenterX - Arm, CenterY - Thickness * 0.5f, Arm * 2.0f, Thickness);
	DrawRect(CrosshairColor, CenterX - Thickness * 0.5f, CenterY - Arm, Thickness, Arm * 2.0f);

	// 目前選色色塊（準星右下）
	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f), CenterX + 14.0f, CenterY + 14.0f, 26.0f, 26.0f);
	DrawRect(CrosshairColor, CenterX + 17.0f, CenterY + 17.0f, 20.0f, 20.0f);
}

FString ANiceInkHUD::GetPhaseLabel(ENiceInkPhase Phase) const
{
	switch (Phase)
	{
	case ENiceInkPhase::Lobby: return TEXT("Lobby");
	case ENiceInkPhase::BottleSpin: return TEXT("Bottle Spin");
	case ENiceInkPhase::Seating: return TEXT("Seating");
	case ENiceInkPhase::Drawing: return TEXT("Drawing");
	case ENiceInkPhase::Tour: return TEXT("Gallery Tour");
	case ENiceInkPhase::Accusation: return TEXT("Accusation");
	case ENiceInkPhase::Resolution: return TEXT("Resolution");
	case ENiceInkPhase::Finale: return TEXT("Finale");
	case ENiceInkPhase::PostGame: return TEXT("Post Game");
	default: return TEXT("Unknown");
	}
}
