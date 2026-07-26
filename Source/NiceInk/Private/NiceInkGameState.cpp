#include "NiceInkGameState.h"

#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"

ANiceInkGameState::ANiceInkGameState()
{
	bReplicates = true;
	// 引擎預設 10Hz＝相位切換/計時 HUD 最多慢 100ms（07-26 遲鈍根治）
	SetNetUpdateFrequency(30.0f);
}

void ANiceInkGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ANiceInkGameState, CurrentPhase);
	DOREPLIFETIME(ANiceInkGameState, CurrentRound);
	DOREPLIFETIME(ANiceInkGameState, VictimPlayerId);
	DOREPLIFETIME(ANiceInkGameState, TourWorkId);
	DOREPLIFETIME(ANiceInkGameState, TourWorkNumber);
	DOREPLIFETIME(ANiceInkGameState, TourWorkCount);
	DOREPLIFETIME(ANiceInkGameState, TourWorkIdList);
	DOREPLIFETIME(ANiceInkGameState, ResolutionWorkId);
	DOREPLIFETIME(ANiceInkGameState, LastAccusationResult);
	DOREPLIFETIME(ANiceInkGameState, RevealedAuthorId);
	DOREPLIFETIME(ANiceInkGameState, LoserPlayerId);
	DOREPLIFETIME(ANiceInkGameState, PhaseEndServerTime);
	DOREPLIFETIME(ANiceInkGameState, FlipProposerId);
	DOREPLIFETIME(ANiceInkGameState, FlipAgreeCount);
	DOREPLIFETIME(ANiceInkGameState, FlipAgreeNeeded);
	DOREPLIFETIME(ANiceInkGameState, FlipProposalSerial);
}

void ANiceInkGameState::SetPhase(ENiceInkPhase NewPhase, float DurationSeconds)
{
	CurrentPhase = NewPhase;
	PhaseEndServerTime = DurationSeconds > 0.0f ? GetServerWorldTimeSeconds() + DurationSeconds : 0.0f;
	OnRep_Phase();
}

float ANiceInkGameState::GetPhaseTimeRemaining() const
{
	return PhaseEndServerTime > 0.0f ? FMath::Max(0.0f, PhaseEndServerTime - GetServerWorldTimeSeconds()) : 0.0f;
}

APlayerState* ANiceInkGameState::FindPlayerStateById(int32 PlayerId) const
{
	for (APlayerState* PS : PlayerArray)
	{
		if (PS && PS->GetPlayerId() == PlayerId)
		{
			return PS;
		}
	}
	return nullptr;
}

void ANiceInkGameState::OnRep_Phase()
{
	OnPhaseChanged.Broadcast(CurrentPhase);
}
