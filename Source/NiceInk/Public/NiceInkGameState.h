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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_Phase, Category = "Nice Ink")
	ENiceInkPhase CurrentPhase = ENiceInkPhase::Lobby;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Nice Ink")
	int32 CurrentRound = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Nice Ink")
	int32 VictimPlayerId = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Nice Ink")
	float PhaseEndServerTime = 0.0f;

	UFUNCTION(BlueprintCallable, Category = "Nice Ink")
	void SetPhase(ENiceInkPhase NewPhase, float DurationSeconds);

	UFUNCTION(BlueprintPure, Category = "Nice Ink")
	float GetPhaseTimeRemaining() const;

private:
	UFUNCTION()
	void OnRep_Phase();
};
