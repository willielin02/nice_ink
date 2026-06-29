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
	ChooseVictimAndArtist();
	ApplyPrototypeCamera();
}

void ANiceInkGameMode::StartPrototypeRound()
{
	ChooseVictimAndArtist();
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
		SetPhase(ENiceInkPhase::Binding, BindingDuration);
		break;
	case ENiceInkPhase::Binding:
		SetPhase(ENiceInkPhase::Tattooing, TattooDuration);
		break;
	case ENiceInkPhase::Tattooing:
		SetPhase(ENiceInkPhase::SoulGuessing, SoulGuessDuration);
		break;
	case ENiceInkPhase::SoulGuessing:
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
		ChooseVictimAndArtist();
		SetPhase(ENiceInkPhase::Binding, BindingDuration);
		break;
	}
}

bool ANiceInkGameMode::SubmitGuess(APlayerController* GuessingPlayer, int32 GuessedArtistId)
{
	ANiceInkGameState* NIState = GetNiceInkGameState();
	if (!NIState || NIState->CurrentPhase != ENiceInkPhase::SoulGuessing)
	{
		return false;
	}

	const bool bCorrect = GuessedArtistId == NIState->ArtistPlayerId;
	if (bCorrect)
	{
		if (ANiceInkPlayerState* GuessingState = GuessingPlayer ? GuessingPlayer->GetPlayerState<ANiceInkPlayerState>() : nullptr)
		{
			GuessingState->AddCorrectGuess();
		}

		NIState->VictimPlayerId = NIState->ArtistPlayerId;
		ChooseVictimAndArtist();
	}
	else
	{
		ChooseVictimAndArtist();
	}

	SetPhase(ENiceInkPhase::Reveal, RevealDuration);
	return bCorrect;
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

void ANiceInkGameMode::ChooseVictimAndArtist()
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
		NIState->ArtistPlayerId = 1;
		return;
	}

	const int32 Round = FMath::Max(0, NIState->CurrentRound);
	const int32 VictimIndex = Round % Players.Num();
	const int32 ArtistIndex = Players.Num() > 1 ? (VictimIndex + 1) % Players.Num() : VictimIndex;

	NIState->VictimPlayerId = Players[VictimIndex] ? Players[VictimIndex]->GetPlayerId() : INDEX_NONE;
	NIState->ArtistPlayerId = Players[ArtistIndex] ? Players[ArtistIndex]->GetPlayerId() : INDEX_NONE;
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
