#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "NiceInkTypes.h"
#include "TattooNeedle.generated.h"

UCLASS(BlueprintType)
class NICEINK_API UTattooNeedle : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tattoo")
	FNeedleConfig Config;

	UFUNCTION(BlueprintPure, Category = "Tattoo")
	FNeedleConfig GetConfig() const { return Config; }

	UFUNCTION(BlueprintPure, Category = "Tattoo")
	static FNeedleConfig MakeDefaultConfig(ENiceInkNeedleType Type);
};
