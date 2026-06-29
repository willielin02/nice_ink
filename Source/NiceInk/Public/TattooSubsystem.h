#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "NiceInkTypes.h"
#include "TattooSubsystem.generated.h"

UCLASS()
class NICEINK_API UTattooSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Tattoo")
	void RecordStroke(AActor* TargetActor, const FTattooStroke& Stroke);

	UFUNCTION(BlueprintCallable, Category = "Tattoo")
	TArray<FTattooStroke> GetStrokeHistory(AActor* TargetActor) const;

	UFUNCTION(BlueprintCallable, Category = "Tattoo")
	void ClearStrokeHistory(AActor* TargetActor);

private:
	TMap<TWeakObjectPtr<AActor>, TArray<FTattooStroke>> StrokeHistoryByActor;
};
