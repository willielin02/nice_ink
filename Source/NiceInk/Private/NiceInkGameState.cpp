#include "NiceInkGameState.h"

#include "GameFramework/PlayerState.h"
#include "NiceInkPlayerState.h"
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
	DOREPLIFETIME(ANiceInkGameState, RoomCode);
	DOREPLIFETIME(ANiceInkGameState, MaxPlayers);
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
	DOREPLIFETIME(ANiceInkGameState, CeremonyStep);
	DOREPLIFETIME(ANiceInkGameState, CeremonyStepStartTime);
	DOREPLIFETIME(ANiceInkGameState, CeremonyStepDuration);
	DOREPLIFETIME(ANiceInkGameState, CeremonyCenter);
	DOREPLIFETIME(ANiceInkGameState, CeremonyRadiusCm);
	DOREPLIFETIME(ANiceInkGameState, CeremonyLieLocation);
	DOREPLIFETIME(ANiceInkGameState, CeremonyLieYaw);
	DOREPLIFETIME(ANiceInkGameState, CeremonySlotOffsetDeg);
	DOREPLIFETIME(ANiceInkGameState, BottleStartYaw);
	DOREPLIFETIME(ANiceInkGameState, BottleEndYaw);
}

float ANiceInkGameState::GetCeremonyAlpha() const
{
	if (CeremonyStep == ENiCeremonyStep::None || CeremonyStepDuration <= 0.0f)
	{
		return 0.0f;
	}
	return FMath::Clamp((GetServerWorldTimeSeconds() - CeremonyStepStartTime) / CeremonyStepDuration,
		0.0f, 1.0f);
}

int32 ANiceInkGameState::GetCeremonySlotRank(int32 SeatIndex) const
{
	// 在場席位排序後取名次＝保序指派（席位序≡道場角向序）
	TArray<int32> Seats;
	for (const APlayerState* PS : PlayerArray)
	{
		if (const ANiceInkPlayerState* NIPS = Cast<ANiceInkPlayerState>(PS))
		{
			if (NIPS->SeatIndex >= 0)
			{
				Seats.AddUnique(NIPS->SeatIndex);
			}
		}
	}
	Seats.Sort();
	return Seats.IndexOfByKey(SeatIndex);
}

float ANiceInkGameState::GetCeremonySlotAngleDeg(int32 SeatIndex) const
{
	const int32 Rank = GetCeremonySlotRank(SeatIndex);
	if (Rank == INDEX_NONE)
	{
		return CeremonySlotOffsetDeg;
	}
	int32 Count = 0;
	for (const APlayerState* PS : PlayerArray)
	{
		if (const ANiceInkPlayerState* NIPS = Cast<ANiceInkPlayerState>(PS))
		{
			if (NIPS->SeatIndex >= 0)
			{
				++Count;
			}
		}
	}
	return CeremonySlotOffsetDeg + 360.0f * Rank / FMath::Max(Count, 1);
}

FVector ANiceInkGameState::GetCeremonySlotLocation(int32 SeatIndex) const
{
	const float Rad = FMath::DegreesToRadians(GetCeremonySlotAngleDeg(SeatIndex));
	return CeremonyCenter +
		FVector(FMath::Cos(Rad), FMath::Sin(Rad), 0.0f) * CeremonyRadiusCm;
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
