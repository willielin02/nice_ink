#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "NiceInkHUD.generated.h"

enum class ENiceInkPhase : uint8;

UCLASS()
class NICEINK_API ANiceInkHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	FString GetPhaseLabel(ENiceInkPhase Phase) const;
};
