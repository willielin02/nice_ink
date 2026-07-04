#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "NiceInkTypes.h"
#include "TattooNeedle.generated.h"

UCLASS(BlueprintType)
class NICEINK_API UTattooMarker : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tattoo")
	FMarkerConfig Config;

	UFUNCTION(BlueprintPure, Category = "Tattoo")
	FMarkerConfig GetConfig() const { return Config; }

	UFUNCTION(BlueprintPure, Category = "Tattoo")
	static FMarkerConfig MakeDefaultConfig(ENiceInkMarkerType Type);
};
