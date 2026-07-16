#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "NiceInkMenuPlayerController.generated.h"

// 主選單控制器：顯示滑鼠游標、不鎖定不隱藏；互動全部由 MenuHUD 輪詢。
// NiMenu* exec＝打包版自動化測試的鍵盤免依賴入口（-ExecCmds 可直呼）。
UCLASS()
class NICEINK_API ANiceInkMenuPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ANiceInkMenuPlayerController();

	virtual void BeginPlay() override;

	// 打包版自動化：直接建 LAN 房／搜第一房加入（等價於選單按鈕）
	UFUNCTION(Exec)
	void NiMenuHost();

	UFUNCTION(Exec)
	void NiMenuJoin();
};
