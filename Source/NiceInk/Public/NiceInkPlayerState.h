#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "NiceInkTypes.h"
#include "NiceInkPlayerState.generated.h"

UCLASS()
class NICEINK_API ANiceInkPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	ANiceInkPlayerState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_Appearance, Category = "Nice Ink")
	FCharacterAppearance Appearance;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Nice Ink")
	int32 CorrectGuesses = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Nice Ink")
	int32 TimesTattooed = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Nice Ink")
	int32 TimesArtist = 0;

	UFUNCTION(BlueprintCallable, Category = "Nice Ink")
	void SetAppearance(const FCharacterAppearance& NewAppearance);

	UFUNCTION(BlueprintCallable, Category = "Nice Ink")
	void AddCorrectGuess();

	UFUNCTION(BlueprintCallable, Category = "Nice Ink")
	void AddTattooed();

	UFUNCTION(BlueprintCallable, Category = "Nice Ink")
	void AddArtistRound();

private:
	UFUNCTION()
	void OnRep_Appearance();
};
