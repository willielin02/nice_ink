#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "NiceInkSettingsSave.generated.h"

// 玩家本機偏好（名字／avatar／靈敏度／音量）。
// 視窗模式與解析度不在這裡——那是引擎 GameUserSettings 的責任。
// 存讀由 UNiceInkGameInstance 統一經手，槽位固定 NiceInk_Settings。
UCLASS()
class NICEINK_API UNiceInkSettingsSave : public USaveGame
{
	GENERATED_BODY()

public:
	// 已退役（2026-09-17 user 定案：名字統一匯入平台、不可自訂）。欄位留著只為舊存檔／雲端偏好相容，恆空。
	UPROPERTY()
	FString PlayerDisplayName;

	// INDEX_NONE＝交給席位輪派（auto）
	UPROPERTY()
	int32 PreferredAvatar = INDEX_NONE;

	// 全域滑鼠靈敏度倍率：乘在視角／臉指向／畫筆游標／迷宮游標的既有係數上
	UPROPERTY()
	float MouseSensitivityScale = 1.0f;

	UPROPERTY()
	float MasterVolume = 1.0f;

	// 走路晃動（2026-09-15 user 定案：設定開關、預設開）：站姿第一人稱相機黏在頭骨眉心，
	// 步態的下沉／橫擺／沉浮全進畫面；關＝相機回膠囊固定高（09-15 之前的行為）。
	// 舊存檔沒有這欄＝讀回預設 true。
	UPROPERTY()
	bool bHeadBobEnabled = true;

	// 渲染比例 %（r.ScreenPercentage；50~100）——弱機效能旋鈕，取代解析度選單
	//（2026-08-11 視窗模式簡化制：無邊框/視窗二態、獨占全螢幕與解析度選單退役）
	UPROPERTY()
	float RenderScalePct = 100.0f;

	// 選單語言（NiLoc 索引；-1＝未選過→依 OS 文化自動偵測）
	UPROPERTY()
	int32 LanguageIndex = -1;

	// 效能預設版次（2026-08-25 幀率上限制）：0＝這台機器還沒套過本作的效能預設。
	// 引擎預設 FrameRateLimit=0 且 VSync 關＝顯卡被拉到滿速去畫每秒 500 張
	//（實測空道場 1280×720：Frame 1.95ms／GPU 67%／93W／時脈釘死 1942MHz）。
	// 「無上限」不是設計選擇，是沒人踩煞車。首次啟動（含舊存檔升上來）一次性
	// 寫入預設；此後玩家在設定頁動過就永不再被覆寫（見 bPerfTouchedByPlayer）。
	// 幀率上限與 VSync 的正本住引擎 GameUserSettings（與視窗模式同一個責任邊界），
	// 這裡只記「預設套到第幾版了」。
	// v1＝120fps 上限。v2＝**VSync 開**（同日 user 回報「水平橫條」：120fps 對
	// 60Hz 螢幕是整數 2 倍且 VSync 關 ⇒ 撕裂線幾乎定在同一高度不動＝最顯眼的
	// 那種撕裂。上限只管功耗，不管畫面完整性——那是 VSync 的工作）。
	UPROPERTY()
	int32 PerfDefaultsVersion = 0;

	// 玩家是否親手動過效能列（幀率上限／垂直同步）。動過＝他的選擇是權威，
	// 之後任何版本的預設遷移都不准再碰。**版次不能兼任這個旗標**：自動遷移
	// 也會推進版次，兩者混用就分不出「系統設的」與「玩家選的」。
	UPROPERTY()
	bool bPerfTouchedByPlayer = false;

	// 偏好版本號：每次使用者改動遞增。雲端同步（NiPersona）比帳用——
	// 高者贏、平手本機贏；跨裝置同步的夠用規則
	UPROPERTY()
	int32 Revision = 0;
};
