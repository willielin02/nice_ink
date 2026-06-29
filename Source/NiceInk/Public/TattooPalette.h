#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TattooPalette.generated.h"

UCLASS(BlueprintType)
class NICEINK_API UTattooPalette : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tattoo")
	TArray<FLinearColor> Colors = {
		FLinearColor::Black,
		FLinearColor(0.03f, 0.04f, 0.08f, 1.0f),
		FLinearColor(0.45f, 0.02f, 0.025f, 1.0f),
		FLinearColor(0.02f, 0.18f, 0.08f, 1.0f),
		FLinearColor(0.02f, 0.1f, 0.42f, 1.0f)
	};

	UFUNCTION(BlueprintPure, Category = "Tattoo")
	FLinearColor GetColor(int32 Index) const;
};
