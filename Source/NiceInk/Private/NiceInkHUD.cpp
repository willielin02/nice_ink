#include "NiceInkHUD.h"

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
	const ANiceInkCharacter* MyChar = PlayerOwner ? Cast<ANiceInkCharacter>(PlayerOwner->GetPawn()) : nullptr;
	const ANiceInkPlayerState* MyPS = PlayerOwner ? PlayerOwner->GetPlayerState<ANiceInkPlayerState>() : nullptr;

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
	}

	if (MyPS)
	{
		DrawText(FString::Printf(TEXT("Me: %s    Seat: %d    Cash: %d"), *MyPS->GetPlayerName(), MyPS->SeatIndex, MyPS->Cash),
			FLinearColor(0.8f, 0.9f, 1.0f, 1.0f), Padding, Y, nullptr, 0.85f, false);
		Y += LineHeight;
	}

	if (MyChar && MyChar->bAsleep)
	{
		DrawText(TEXT("You are ASLEEP. Press WASD to wake & emerge (minigame comes in M2)."),
			FLinearColor(1.0f, 1.0f, 0.5f, 1.0f), Padding, Y, nullptr, 0.85f, false);
		Y += LineHeight;
	}

	DrawText(TEXT("Move: WASD  Look: mouse  Draw: hold LMB  Palette: 1-9,0"),
		FLinearColor(0.88f, 0.88f, 0.88f, 1.0f), Padding, Y, nullptr, 0.8f, false);
	Y += LineHeight;
	DrawText(TEXT("Console: NiStart | NiEmerge | NiAccuse <workNo> <seat>"),
		FLinearColor(0.88f, 0.88f, 0.88f, 1.0f), Padding, Y, nullptr, 0.8f, false);

	DrawInkCrosshair();
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
