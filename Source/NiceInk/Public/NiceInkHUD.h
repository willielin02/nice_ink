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
	void DrawInkCrosshair();

	// 沉睡端全套：視覺全遮蔽黑屏 + 甦醒小遊戲 + 姿勢面板（SPEC 定案 #3/#5）
	void DrawVictimSleepUI(const class ANiceInkCharacter* MyChar, const class ANiceInkGameState* GS);
};
