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

	// 沉睡端全套：視覺全遮蔽黑屏＋醉夢圓形迷宮＋姿勢面板（SPEC 定案 #3、#30/#31）
	void DrawVictimSleepUI(class ANiceInkCharacter* MyChar, const class ANiceInkGameState* GS);

	// 兇手轉盤（SPEC 定案 #31）：滾輪選度數、5 秒自動送出——只有兇手本人看得到
	void DrawTrapDial(const class ANiceInkCharacter* MyChar);
};
