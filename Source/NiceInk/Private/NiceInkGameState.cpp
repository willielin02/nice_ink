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
	DOREPLIFETIME(ANiceInkGameState, NotaryRoomId);
	DOREPLIFETIME(ANiceInkGameState, LastWakeSeconds);
}

FString ANiceInkGameState::BuildRoundAttestCanon() const
{
	// 正準字串（P2 digest 素材）：全部取自複製屬性＝各端逐位相同的前提是「已複製到」
	// ——呼叫端負責在 Resolution 相位進入後留足複製裕量再算。
	// 浮點（LastWakeSeconds）取分秒整數＝避開字串化格式差異。
	FString Canon = FString::Printf(TEXT("NIAT1|%s|%d|%d|%d|%d|%d|%d"),
		*NotaryRoomId, CurrentRound, VictimPlayerId, ResolutionWorkId, RevealedAuthorId,
		static_cast<int32>(LastAccusationResult),
		FMath::RoundToInt(LastWakeSeconds * 10.0f));
	TArray<const ANiceInkPlayerState*> Sorted;
	for (const APlayerState* PS : PlayerArray)
	{
		if (const ANiceInkPlayerState* NIPS = Cast<ANiceInkPlayerState>(PS))
		{
			Sorted.Add(NIPS);
		}
	}
	Sorted.Sort([](const ANiceInkPlayerState& A, const ANiceInkPlayerState& B)
	{
		return A.GetPlayerId() < B.GetPlayerId();
	});
	for (const ANiceInkPlayerState* NIPS : Sorted)
	{
		Canon += FString::Printf(TEXT("|%d:%d:%d"),
			NIPS->GetPlayerId(), NIPS->Cash, NIPS->PenaltyCups);
	}
	return Canon;
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

FVector ANiceInkGameState::GetIntroTvLocation() const
{
	// 舞台中心的純函式（**不可寫死世界座標**——舞台搬家要跟；ViewWide 同一條鐵則）
	return CeremonyCenter + FVector(0.0f, -310.0f, 0.0f);
}

FVector ANiceInkGameState::GetIntroSitLocation(int32 SeatIndex) const
{
	// 弧上角位：席位 0..5 從左到右各 17°；基準向＝電視朝觀眾群的 +Y。
	// 半徑 260 ⇒ 弧在舞台中心南側一點點（y≈−54），坐在榻榻米區。
	// 24 deg @ r260 = 鄰居間距 109cm（體寬 ~95cm；17°=77cm 實測兩人互穿）。
	// 依**在場名次**置中（同 GetCeremonySlotRank 慣例）：用固定六席樽位會讓
	// 2~5 人局搠在弧的西端（截圖自查實錬），人群要永遠坐在電視正前。
	constexpr float StepDeg = 24.0f;
	const int32 Rank = GetCeremonySlotRank(SeatIndex);
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
	const int32 UseRank = (Rank == INDEX_NONE) ? 0 : Rank;
	const float Fan = (UseRank - (FMath::Max(Count, 1) - 1) * 0.5f) * StepDeg;
	const FVector Dir = FVector(0.0f, 1.0f, 0.0f).RotateAngleAxis(Fan, FVector::UpVector);
	return GetIntroTvLocation() + Dir * 260.0f;
}

float ANiceInkGameState::GetIntroSitYawDeg(int32 SeatIndex) const
{
	const FVector ToTv = GetIntroTvLocation() - GetIntroSitLocation(SeatIndex);
	return FMath::RadiansToDegrees(FMath::Atan2(ToTv.Y, ToTv.X));
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
