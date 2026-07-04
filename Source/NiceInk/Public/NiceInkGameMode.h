#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "NiceInkTypes.h"
#include "NiceInkGameMode.generated.h"

class ANiceInkGameState;

UCLASS()
class NICEINK_API ANiceInkGameMode : public AGameMode
{
	GENERATED_BODY()

public:
	ANiceInkGameMode();

	virtual void BeginPlay() override;
	virtual void PostLogin(APlayerController* NewPlayer) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Timing")
	float DrinkingDuration = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Timing")
	float TattooDuration = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Timing")
	float AccusationDuration = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Timing")
	float RevealDuration = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Prototype")
	bool bAutoRunPrototypeFlow = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Prototype")
	bool bUsePrototypeCameraInPIE = true;

	UFUNCTION(BlueprintCallable, Category = "Nice Ink")
	void StartPrototypeRound();

	UFUNCTION(BlueprintCallable, Category = "Nice Ink")
	void AdvancePhase();

	UFUNCTION(BlueprintCallable, Category = "Nice Ink")
	bool SubmitGuess(APlayerController* GuessingPlayer, int32 GuessedArtistId);

private:
	FTimerHandle PhaseTimerHandle;

	ANiceInkGameState* GetNiceInkGameState() const;
	void SetPhase(ENiceInkPhase NewPhase, float Duration);
	void ChooseVictim();
	void ApplyPrototypeCamera();
};
