#include "NiceInkGameMode.h"

#include "Camera/CameraActor.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "NiceInkGameState.h"
#include "NiceInkHUD.h"
#include "NiceInkPlayerState.h"

ANiceInkGameMode::ANiceInkGameMode()
{
	GameStateClass = ANiceInkGameState::StaticClass();
	PlayerStateClass = ANiceInkPlayerState::StaticClass();
	HUDClass = ANiceInkHUD::StaticClass();
}

void ANiceInkGameMode::BeginPlay()
{
	Super::BeginPlay();

	SetPhase(ENiceInkPhase::Lobby, 0.0f);
	if (bAutoRunPrototypeFlow)
	{
		GetWorldTimerManager().SetTimerForNextTick(this, &ANiceInkGameMode::StartPrototypeRound);
	}
	if (bUsePrototypeCameraInPIE)
	{
		GetWorldTimerManager().SetTimerForNextTick(this, &ANiceInkGameMode::ApplyPrototypeCamera);
	}
}

void ANiceInkGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	ChooseVictim();
	ApplyPrototypeCamera();
}

void ANiceInkGameMode::StartPrototypeRound()
{
	ChooseVictim();
	SetPhase(ENiceInkPhase::SelectingVictim, 2.0f);
}

void ANiceInkGameMode::AdvancePhase()
{
	ANiceInkGameState* NIState = GetNiceInkGameState();
	if (!NIState)
	{
		return;
	}

	switch (NIState->CurrentPhase)
	{
	case ENiceInkPhase::Lobby:
	case ENiceInkPhase::SelectingVictim:
		SetPhase(ENiceInkPhase::Drinking, DrinkingDuration);
		break;
	case ENiceInkPhase::Drinking:
		SetPhase(ENiceInkPhase::Tattooing, TattooDuration);
		break;
	case ENiceInkPhase::Tattooing:
		SetPhase(ENiceInkPhase::Accusation, AccusationDuration);
		break;
	case ENiceInkPhase::Accusation:
		SetPhase(ENiceInkPhase::Reveal, RevealDuration);
		break;
	case ENiceInkPhase::Reveal:
		SetPhase(ENiceInkPhase::Celebration, 4.0f);
		break;
	case ENiceInkPhase::Celebration:
		SetPhase(ENiceInkPhase::NextRound, 2.0f);
		break;
	case ENiceInkPhase::NextRound:
		++NIState->CurrentRound;
		ChooseVictim();
		SetPhase(ENiceInkPhase::Drinking, DrinkingDuration);
		break;
	}
}

bool ANiceInkGameMode::SubmitGuess(APlayerController* GuessingPlayer, int32 GuessedArtistId)
{
	ANiceInkGameState* NIState = GetNiceInkGameState();
	if (!NIState || NIState->CurrentPhase != ENiceInkPhase::Accusation)
	{
		return false;
	}

	// In the new all-artists model, each player's guess is evaluated per-drawing.
	// GuessedArtistId refers to the accused player for a specific drawing on the victim.
	if (ANiceInkPlayerState* GuessingState = GuessingPlayer ? GuessingPlayer->GetPlayerState<ANiceInkPlayerState>() : nullptr)
	{
		// Scoring is tracked per guess; correctness is validated by the caller
		GuessingState->AddCorrectGuess();
	}

	SetPhase(ENiceInkPhase::Reveal, RevealDuration);
	return true;
}

ANiceInkGameState* ANiceInkGameMode::GetNiceInkGameState() const
{
	return GetGameState<ANiceInkGameState>();
}

void ANiceInkGameMode::SetPhase(ENiceInkPhase NewPhase, float Duration)
{
	if (ANiceInkGameState* NIState = GetNiceInkGameState())
	{
		NIState->SetPhase(NewPhase, Duration);
		UE_LOG(LogTemp, Log, TEXT("NiceInk phase -> %d, duration %.2f"), static_cast<int32>(NewPhase), Duration);
	}

	GetWorldTimerManager().ClearTimer(PhaseTimerHandle);
	if (Duration > 0.0f)
	{
		GetWorldTimerManager().SetTimer(PhaseTimerHandle, this, &ANiceInkGameMode::AdvancePhase, Duration, false);
	}
}

void ANiceInkGameMode::ChooseVictim()
{
	ANiceInkGameState* NIState = GetNiceInkGameState();
	if (!NIState)
	{
		return;
	}

	TArray<APlayerState*> Players = GameState ? GameState->PlayerArray : TArray<APlayerState*>();
	if (Players.Num() == 0)
	{
		NIState->VictimPlayerId = 0;
		return;
	}

	const int32 Round = FMath::Max(0, NIState->CurrentRound);
	const int32 VictimIndex = Round % Players.Num();

	NIState->VictimPlayerId = Players[VictimIndex] ? Players[VictimIndex]->GetPlayerId() : INDEX_NONE;
	// All other players are artists simultaneously
}

void ANiceInkGameMode::ApplyPrototypeCamera()
{
	if (!bUsePrototypeCameraInPIE || !GetWorld())
	{
		return;
	}

	ACameraActor* PrototypeCamera = nullptr;
	for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It)
	{
		ACameraActor* CandidateCamera = *It;
		if (!PrototypeCamera)
		{
			PrototypeCamera = CandidateCamera;
		}
		if (CandidateCamera && CandidateCamera->ActorHasTag(TEXT("NiceInkPrototypeCamera")))
		{
			PrototypeCamera = CandidateCamera;
			break;
		}
	}

	if (!PrototypeCamera)
	{
		return;
	}

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PlayerController = It->Get())
		{
			PlayerController->SetViewTargetWithBlend(PrototypeCamera, 0.15f);
		}
	}
}
