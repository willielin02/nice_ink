#include "NiceInkGameState.h"

#include "Net/UnrealNetwork.h"

ANiceInkGameState::ANiceInkGameState()
{
	bReplicates = true;
}

void ANiceInkGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ANiceInkGameState, CurrentPhase);
	DOREPLIFETIME(ANiceInkGameState, CurrentRound);
	DOREPLIFETIME(ANiceInkGameState, VictimPlayerId);
	DOREPLIFETIME(ANiceInkGameState, PhaseEndServerTime);
}

void ANiceInkGameState::SetPhase(ENiceInkPhase NewPhase, float DurationSeconds)
{
	CurrentPhase = NewPhase;
	PhaseEndServerTime = GetServerWorldTimeSeconds() + FMath::Max(0.0f, DurationSeconds);
	OnRep_Phase();
}

float ANiceInkGameState::GetPhaseTimeRemaining() const
{
	return FMath::Max(0.0f, PhaseEndServerTime - GetServerWorldTimeSeconds());
}

void ANiceInkGameState::OnRep_Phase()
{
	OnPhaseChanged.Broadcast(CurrentPhase);
}
