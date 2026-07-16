#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "NiceInkMenuGameMode.generated.h"

// L_MainMenu 的 GameMode：沒有遊戲，只有 canvas 選單。
// 玩家＝旁觀者（空世界、鏡頭不動），一切互動在 ANiceInkMenuHUD。
UCLASS()
class NICEINK_API ANiceInkMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ANiceInkMenuGameMode();
};
