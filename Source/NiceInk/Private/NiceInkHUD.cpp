#include "NiceInkHUD.h"

#include "Engine/Canvas.h"
#include "EngineUtils.h"
#include "NiceInkGameState.h"
#include "TattooComponent.h"
#include "TattooPrototypeActor.h"

namespace
{
FString GetMarkerLabel(ENiceInkMarkerType MarkerType)
{
	switch (MarkerType)
	{
	case ENiceInkMarkerType::FineMarker:
		return TEXT("Fine Marker");
	case ENiceInkMarkerType::ThickMarker:
		return TEXT("Thick Marker");
	case ENiceInkMarkerType::BrushTip:
		return TEXT("Brush Tip");
	default:
		return TEXT("Unknown");
	}
}
}

void ANiceInkHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	const ANiceInkGameState* NIState = GetWorld() ? GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
	const float Padding = 18.0f;
	const float LineHeight = 21.0f;
	const float PanelWidth = 430.0f;
	const float PanelHeight = 184.0f;

	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.42f), Padding - 8.0f, Padding - 8.0f, PanelWidth, PanelHeight);
	DrawText(TEXT("Nice Ink Prototype"), FLinearColor::White, Padding, Padding, nullptr, 1.15f, false);

	if (NIState)
	{
		const FString PhaseText = FString::Printf(
			TEXT("Phase: %s    Time: %.1fs    Round: %d"),
			*GetPhaseLabel(NIState->CurrentPhase),
			NIState->GetPhaseTimeRemaining(),
			NIState->CurrentRound + 1);
		DrawText(PhaseText, FLinearColor(0.86f, 0.94f, 1.0f, 1.0f), Padding, Padding + LineHeight * 1.35f, nullptr, 0.95f, false);

		const FString RoleText = FString::Printf(TEXT("Victim: %d    All others draw"), NIState->VictimPlayerId);
		DrawText(RoleText, FLinearColor(1.0f, 0.83f, 0.62f, 1.0f), Padding, Padding + LineHeight * 2.45f, nullptr, 0.9f, false);
	}
	else
	{
		DrawText(TEXT("Phase: waiting for GameState"), FLinearColor(0.86f, 0.94f, 1.0f, 1.0f), Padding, Padding + LineHeight * 1.35f, nullptr, 0.95f, false);
	}

	const ATattooPrototypeActor* TattooPrototype = nullptr;
	if (GetWorld())
	{
		for (TActorIterator<ATattooPrototypeActor> It(GetWorld()); It; ++It)
		{
			TattooPrototype = *It;
			break;
		}
	}

	if (TattooPrototype && TattooPrototype->TattooComponent)
	{
		const FString ToolText = FString::Printf(
			TEXT("Current: %s    Color Slot: %d"),
			*GetMarkerLabel(TattooPrototype->TattooComponent->CurrentMarkerType),
			TattooPrototype->SelectedPaletteIndex + 1);
		DrawText(ToolText, FLinearColor(0.92f, 0.98f, 1.0f, 1.0f), Padding, Padding + LineHeight * 3.75f, nullptr, 0.82f, false);
	}

	DrawText(TEXT("Markers: 1 Fine  2 Thick  3 Brush"), FLinearColor(0.88f, 0.88f, 0.88f, 1.0f), Padding, Padding + LineHeight * 4.75f, nullptr, 0.82f, false);
	DrawText(TEXT("Colors: 5 Black  6 BlueBlack  7 Red  8 Green  9 Blue"), FLinearColor(0.88f, 0.88f, 0.88f, 1.0f), Padding, Padding + LineHeight * 5.75f, nullptr, 0.78f, false);
	DrawText(TEXT("Paint: hold Left Mouse on the tattoo surface"), FLinearColor(0.88f, 0.88f, 0.88f, 1.0f), Padding, Padding + LineHeight * 6.75f, nullptr, 0.82f, false);
	DrawText(TEXT("Guess panel placeholder: players 1-6"), FLinearColor(0.88f, 0.88f, 0.88f, 1.0f), Padding, Padding + LineHeight * 7.75f, nullptr, 0.82f, false);
}

FString ANiceInkHUD::GetPhaseLabel(ENiceInkPhase Phase) const
{
	switch (Phase)
	{
	case ENiceInkPhase::Lobby:
		return TEXT("Lobby");
	case ENiceInkPhase::SelectingVictim:
		return TEXT("Selecting Victim");
	case ENiceInkPhase::Drinking:
		return TEXT("Drinking");
	case ENiceInkPhase::Tattooing:
		return TEXT("Tattooing");
	case ENiceInkPhase::Accusation:
		return TEXT("Accusation");
	case ENiceInkPhase::Reveal:
		return TEXT("Reveal");
	case ENiceInkPhase::Celebration:
		return TEXT("Celebration");
	case ENiceInkPhase::NextRound:
		return TEXT("Next Round");
	default:
		return TEXT("Unknown");
	}
}
