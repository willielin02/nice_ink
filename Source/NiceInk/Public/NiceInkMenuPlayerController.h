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

	// --- 房間碼自動化（-ExecCmds 開機直呼用，timer-deferred 讓引擎完全就緒）---

	// 建房（跟隨服務自選 LAN/EOS；bPublic 0=私房 invite only）
	UFUNCTION(Exec)
	void NiMenuAutoHost(int32 bPublic = 0);

	// 開加入頁並預填房間碼（只擺畫面不加入——截圖自查用）
	UFUNCTION(Exec)
	void NiMenuShowJoin(const FString& Code);

	// 按碼加入（等價於加入頁輸碼＋join with code）
	UFUNCTION(Exec)
	void NiMenuJoinCode(const FString& Code);

	// 多文字系統取樣行（字體矩陣驗證：日/繁/簡/韓/俄/阿/土 一行上牆）
	UFUNCTION(Exec)
	void NiMenuFontSample();

	// 切語言（NiLoc 索引 0-12；robo 多語截圖用）
	UFUNCTION(Exec)
	void NiMenuLang(int32 LangIndex);

	// 開語言全列頁（robo 截圖用）
	UFUNCTION(Exec)
	void NiMenuShowLang();
};
