#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "NiceInkMenuHUD.generated.h"

class SNiMenu;
class UFont;

// 主選單宿主（2026-08-06 Slate 白卡制）：畫面本體＝SNiMenu（C++ Slate 直寫、
// 零 UMG 資產）；本類只負責掛/卸 viewport widget 與 robo 鉤子轉接。
// canvas 選單已退役（素色半透明毛玻璃=canvas 做不到的效果）。
UCLASS()
class NICEINK_API ANiceInkMenuHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// robo 鉤子（NiMenuShowJoin/NiMenuJoinCode exec 用）：切到加入頁＋預填房間碼
	void RoboOpenJoinPage(const FString& PrefillCode);

	// robo 鉤子（NiMenuFontSample）：多文字系統取樣行上牆
	void RoboShowFontSample(const FString& Sample);

	// robo 鉤子（NiMenuShowLang）：開語言全列頁
	void RoboOpenLanguagePage();

	// 換語言後重建選單（靜態文字全在 Construct 定死＝重建刷新；
	// 延後一 tick——不在 widget 自己的點擊回呼裡拆它）
	void RecreateMenu(bool bOpenSettings);

private:
	TSharedPtr<SNiMenu> Menu;

	// Slate 要複合 UFont（裸 FontFace 資產＝豆腐字實錘）；UPROPERTY 保 GC
	UPROPERTY() TObjectPtr<UFont> MenuFont;
	UFont* BuildMenuFont();
};
