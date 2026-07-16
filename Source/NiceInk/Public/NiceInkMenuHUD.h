#pragma once

#include "CoreMinimal.h"
#include "NiceInkHUD.h"
#include "NiceInkMenuHUD.generated.h"

class UNiceInkGameInstance;
class UNiceInkSessionSubsystem;

// 主選單（canvas 直畫、輪詢互動；無 UMG 鐵律）。
// 繼承 ANiceInkHUD 只為共用 token 繪製 helpers 與字體資產——DrawHUD 完全改寫。
// 即時模式 UI：按鈕＝畫的當下就做點擊判定，沒有 widget 樹。
UCLASS()
class NICEINK_API ANiceInkMenuHUD : public ANiceInkHUD
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void DrawHUD() override;

private:
	enum class EMenuPage : uint8 { Root, Join, Settings, Credits };
	EMenuPage Page = EMenuPage::Root;

	// --- 名字輸入 ---
	FString NameBuffer;
	bool bNameFocused = false;
	double CaretPhase = 0.0;

	// --- avatar 選擇（INDEX_NONE＝auto 席位輪派）---
	int32 AvatarSel = INDEX_NONE;
	UPROPERTY() TArray<TObjectPtr<UTexture2D>> FaceThumbs;

	// --- 連線模式（EOS 憑證未設定時鎖 LAN）---
	bool bUseLan = true;

	// --- 斷線原因橫幅（點擊任意處清除）---
	FString ErrorBanner;

	// join 頁進入時自動搜一次
	bool bSearchKicked = false;

	// settings 頁暫存（按 apply 才動引擎設定）
	int32 PendingWindowMode = 0;
	int32 PendingResIndex = 0;
	bool bSettingsSeeded = false;

	// --- helpers（即時模式按鈕等繪製 helpers 繼承自 ANiceInkHUD）---
	UNiceInkGameInstance* GI() const;
	UNiceInkSessionSubsystem* Sessions() const;

	UTexture2D* GetFaceThumb(int32 Index);
	void PollNameTyping();
	void CommitName();

	void DrawRootPage(float W, float H);
	void DrawJoinPage(float W, float H);
	void DrawSettingsPage(float W, float H);
	void DrawCreditsPage(float W, float H);
	void DrawSessionStatusLine(float CenterX, float Y);
};
