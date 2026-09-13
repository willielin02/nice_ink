#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "NiceInkMenuHUD.generated.h"

class SNiMenu;
class UFont;
class UTexture2D;

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
	// 內容欄的「地」（2026-09-05 中性制）：左側一道**沒有邊界的**暗漸層，畫在 Slate 之下。
	// 道場是亮的（障子牆 sRGB ~208），白字直接壓上去等於隱形；面板有邊界所以是一個東西，
	// 漸層沒有所以只是光線——與局內 DrawTopScrim 同一條規則。
	virtual void DrawHUD() override;

	// 漸層的水平覆蓋（螢幕寬度比例）與峰值濃度
	UPROPERTY(EditAnywhere, Category = "Nice Ink|Menu")
	float ScrimWidthFrac = 0.58f;
	UPROPERTY(EditAnywhere, Category = "Nice Ink|Menu")
	float ScrimAlpha = 0.72f;

	// robo 鉤子（NiMenuShowJoin/NiMenuJoinCode exec 用）：切到加入頁＋預填房間碼
	void RoboOpenJoinPage(const FString& PrefillCode);
	void RoboGoBack();   // robo：等同按 ESC（NiMenuBack）

	// robo 鉤子（NiMenuFontSample）：多文字系統取樣行上牆
	void RoboShowFontSample(const FString& Sample);

	// robo 鉤子（NiMenuShowLang）：開語言全列頁
	void RoboOpenLanguagePage();

	// robo 鉤子（NiMenuShowSettings）：開設定頁
	void RoboOpenSettingsPage();

	// robo 鉤子（NiMenuShowHost）：開開房設定頁
	void RoboOpenHostPage();

	// robo 鉤子（NiMenuShowProfile）：開個人檔案頁
	void RoboOpenProfilePage();

	// 舞台讀：個人檔案頁開著？（SNiMenu 是私有成員，這裡代答）
	bool IsProfileShowcase() const;

	// 換語言後重建選單（靜態文字全在 Construct 定死＝重建刷新；
	// 延後一 tick——不在 widget 自己的點擊回呼裡拆它）
	void RecreateMenu(bool bOpenSettings);

private:
	TSharedPtr<SNiMenu> Menu;

	// Slate 要複合 UFont（裸 FontFace 資產＝豆腐字實錘）；UPROPERTY 保 GC
	UPROPERTY() TObjectPtr<UFont> MenuFont;
	// 標誌字貼圖（Slate brush 不保 GC；UPROPERTY 錨住）
	UPROPERTY() TObjectPtr<UTexture2D> LogoTex;
	// 鍵帽（2026-09-10：局內與選單同一顆；Slate brush 不保 GC ⇒ 錨在這裡）
	UPROPERTY() TObjectPtr<UTexture2D> KeycapTex;
	// 一顆鍵一張（2026-09-11 十修；與局內 ANiceInkHUD::KeyTexCache 同一套）
	UPROPERTY() TMap<FName, TObjectPtr<UTexture2D>> KeyTexCache;
public:
	UTexture2D* GetKeyTex(const FString& Key);
private:
	UFont* BuildMenuFont();

	// 64×1 水平漸層（alpha：前 45% 維持峰值，其後 smoothstep 收到 0）；runtime 生成
	UPROPERTY() TObjectPtr<UTexture2D> ScrimTex;
	// 1×64 垂直版（上／下帶）；同一條曲線
	UPROPERTY() TObjectPtr<UTexture2D> VScrimTex;
	void EnsureScrimTex();
};
