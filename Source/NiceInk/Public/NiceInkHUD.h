#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "HAL/IConsoleManager.h"
#include "NiceInkHUD.generated.h"

enum class ENiceInkPhase : uint8;
class UFont;
class UTexture2D;

// 開發者遙測總開關（0=出貨畫面；1=console 指令、seat、版本戳等全部顯示）
extern TAutoConsoleVariable<int32> CVarNiDebugHud;

UCLASS()
class NICEINK_API ANiceInkHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void DrawHUD() override;

protected:
	// ---- 設計 token：字階（Slate 字級，乘 UiScale）／對齊 ----
	// （protected：主選單 HUD 繼承共用同一套 token helpers——樣式只有一套）
	enum class ETextTier : uint8 { Display, Title, Body, Small };
	enum class EHAlign : uint8 { Left, Center, Right };

	float UiScale = 1.0f; // ClipY / 1080：所有尺寸的唯一縮放來源

	float TierSize(ETextTier Tier) const;

	// ---- UI 資產（runtime 字體＋圖示；BeginPlay 載入，UPROPERTY 保 GC）----
	UPROPERTY() TObjectPtr<UFont> UiFont;
	UPROPERTY() TObjectPtr<UTexture2D> IconCup;
	UPROPERTY() TObjectPtr<UTexture2D> IconSpray;
	UPROPERTY() TObjectPtr<UTexture2D> IconKick;
	UPROPERTY() TObjectPtr<UTexture2D> IconMarker;
	UPROPERTY() TObjectPtr<UTexture2D> IconCash;
	UPROPERTY() TObjectPtr<UTexture2D> IconRotate;
	UPROPERTY() TObjectPtr<UTexture2D> IconEye;
	UPROPERTY() TObjectPtr<UTexture2D> IconTrap;
	UPROPERTY() TObjectPtr<UTexture2D> IconSleep;
	UPROPERTY() TObjectPtr<UTexture2D> IconNose;
	void EnsureUiAssets();

	// ---- 繪製 helper（全 HUD 只准經過這組，樣式不得繞道自畫）----
	FVector2D DrawTok(const FString& Text, float X, float Y, ETextTier Tier,
		const FLinearColor& Color, EHAlign Align = EHAlign::Left, bool bBold = false);
	FVector2D MeasureTok(const FString& Text, ETextTier Tier, bool bBold);
	void DrawPanelBox(float X, float Y, float W, float H, float Alpha = 0.72f);
	void DrawIconTok(UTexture2D* Tex, float X, float Y, float Size, const FLinearColor& Tint);
	void DrawCupsRow(float X, float Y, float CupSize, int32 Filled, EHAlign Align = EHAlign::Left);

	// ---- 即時模式 UI 互動（主選單／ESC 選單共用；每幀 BeginUiFrame 後才可用）----
	FVector2D MousePos = FVector2D::ZeroVector;
	bool bClickThisFrame = false;
	bool bClickConsumed = false;
	double LastUiClickTime = -1.0;

	void BeginUiFrame();

	// 即時模式按鈕：畫＋判定一次完成；回傳「本幀被點下」
	bool Button(const FString& Label, float CenterX, float Y, float W, float H,
		bool bEnabled = true, bool bAccent = false);

	// 左右調整列：回傳 -1／0／+1
	int32 AdjustRow(const FString& Label, const FString& Value, float CenterX, float Y,
		bool bLeftEnabled = true, bool bRightEnabled = true);

	void DrawBigTitle(const FString& Text, float CenterX, float Y, float SizePx, const FLinearColor& Color);

	// ---- 畫面（相位 × 角色）----
	void DrawTopBar(const class ANiceInkGameState* GS, const class ANiceInkPlayerState* MyPS, class ANiceInkCharacter* MyChar);
	void DrawCenterBanners(const ANiceInkGameState* GS);

	// 相位轉換音效（client 端輪詢偵測；無聲甦醒相關轉換刻意無音）
	void TickAudioCues(const ANiceInkGameState* GS);
	ENiceInkPhase LastPhaseSeen = static_cast<ENiceInkPhase>(0); // Lobby
	int32 LastTourWorkSeen = INDEX_NONE;
	bool bPhaseSeeded = false;

	// 大廳（Lobby 相位）：玩家列表＋主機開始提示
	void DrawLobbyPanel(const ANiceInkGameState* GS);

	// ESC 系統選單：繼續／靈敏度／音量／離開房間（任何相位可開）
	void DrawSystemMenu(class ANiceInkCharacter* MyChar);
	void DrawAccusePanel(const ANiceInkGameState* GS, class ANiceInkCharacter* MyChar);
	void DrawPostGamePanel(class ANiceInkCharacter* MyChar);
	void DrawBottomHint(const FString& Text, const FLinearColor& Color);

	// 沉睡端全套：視覺全遮蔽黑屏＋醉夢圓形迷宮＋姿勢面板（SPEC 定案 #3、#30/#31）
	void DrawVictimSleepUI(class ANiceInkCharacter* MyChar, const class ANiceInkGameState* GS, const class ANiceInkPlayerState* MyPS);

	// 兇手轉盤（SPEC 定案 #31）：滾輪選度數、5 秒自動送出——只有兇手本人看得到
	void DrawTrapDial(const class ANiceInkCharacter* MyChar);

	void DrawBlindOverlay(const class ANiceInkCharacter* MyChar);
	void DrawInkCrosshair(const class ANiceInkGameState* GS, const class ANiceInkPlayerState* MyPS);
	void DrawDebugPanel(const class ANiceInkGameState* GS, const class ANiceInkPlayerState* MyPS, class ANiceInkCharacter* MyChar);

	FString GetPhaseLabel(ENiceInkPhase Phase) const;
};
