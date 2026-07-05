#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameState.h"
#include "NiceInkTypes.h"
#include "NiceInkGameState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNiceInkPhaseChanged, ENiceInkPhase, NewPhase);

UCLASS()
class NICEINK_API ANiceInkGameState : public AGameState
{
	GENERATED_BODY()

public:
	ANiceInkGameState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(BlueprintAssignable, Category = "Nice Ink")
	FOnNiceInkPhaseChanged OnPhaseChanged;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Phase, Category = "Nice Ink")
	ENiceInkPhase CurrentPhase = ENiceInkPhase::Lobby;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	int32 CurrentRound = 0;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	int32 VictimPlayerId = INDEX_NONE;

	// 巡禮中的傑作（受害者畫布上的 WorkId）與其作者；非巡禮階段為 INDEX_NONE
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	int32 TourWorkId = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	int32 TourWorkNumber = 0; // 第幾幅（1 起算，給 HUD）

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	int32 TourWorkCount = 0;

	// 上一次指認的結果（Resolution 演出用）
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	ENiceInkAccusationResult LastAccusationResult = ENiceInkAccusationResult::None;

	// 猜錯時揭曉的真作者（此刻全員都知道那幅是誰畫的）
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	int32 RevealedAuthorId = INDEX_NONE;

	// 終局輸家
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	int32 LoserPlayerId = INDEX_NONE;

	// 計時相位的結束時間（server world time）；非計時相位為 0
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	float PhaseEndServerTime = 0.0f;

	UFUNCTION(BlueprintCallable, Category = "Nice Ink")
	void SetPhase(ENiceInkPhase NewPhase, float DurationSeconds);

	UFUNCTION(BlueprintPure, Category = "Nice Ink")
	float GetPhaseTimeRemaining() const;

	UFUNCTION(BlueprintPure, Category = "Nice Ink")
	APlayerState* FindPlayerStateById(int32 PlayerId) const;

private:
	UFUNCTION()
	void OnRep_Phase();
};
