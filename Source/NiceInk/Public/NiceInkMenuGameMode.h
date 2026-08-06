#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "NiceInkMenuGameMode.generated.h"

// L_MainMenu 的 GameMode：沒有遊戲——Slate 選單（ANiceInkMenuHUD 掛 SNiMenu）
// ＋選單舞台（ANiceInkMenuStage：力士跟 BGM 跳舞、相機/燈全程式生成）。
UCLASS()
class NICEINK_API ANiceInkMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ANiceInkMenuGameMode();

	virtual void BeginPlay() override;
};
