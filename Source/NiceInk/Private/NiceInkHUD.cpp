#include "NiceInkHUD.h"

#include "Engine/Canvas.h"
#include "NiceInkGameState.h"

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
	const float PanelHeight = 156.0f;

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

		const FString RoleText = FString::Printf(TEXT("Victim: %d    Hidden Artist: %d"), NIState->VictimPlayerId, NIState->ArtistPlayerId);
		DrawText(RoleText, FLinearColor(1.0f, 0.83f, 0.62f, 1.0f), Padding, Padding + LineHeight * 2.45f, nullptr, 0.9f, false);
	}
	else
	{
		DrawText(TEXT("Phase: waiting for GameState"), FLinearColor(0.86f, 0.94f, 1.0f, 1.0f), Padding, Padding + LineHeight * 1.35f, nullptr, 0.95f, false);
	}

	DrawText(TEXT("Needles: 1 RL  2 RS  3 Magnum  4 Curved"), FLinearColor(0.88f, 0.88f, 0.88f, 1.0f), Padding, Padding + LineHeight * 3.75f, nullptr, 0.82f, false);
	DrawText(TEXT("Paint: hold Left Mouse on the tattoo surface"), FLinearColor(0.88f, 0.88f, 0.88f, 1.0f), Padding, Padding + LineHeight * 4.75f, nullptr, 0.82f, false);
	DrawText(TEXT("Guess panel placeholder: players 1-6"), FLinearColor(0.88f, 0.88f, 0.88f, 1.0f), Padding, Padding + LineHeight * 5.75f, nullptr, 0.82f, false);
}

FString ANiceInkHUD::GetPhaseLabel(ENiceInkPhase Phase) const
{
	switch (Phase)
	{
	case ENiceInkPhase::Lobby:
		return TEXT("Lobby");
	case ENiceInkPhase::SelectingVictim:
		return TEXT("Selecting Victim");
	case ENiceInkPhase::Binding:
		return TEXT("Binding");
	case ENiceInkPhase::Tattooing:
		return TEXT("Tattooing");
	case ENiceInkPhase::SoulGuessing:
		return TEXT("Soul Guessing");
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
