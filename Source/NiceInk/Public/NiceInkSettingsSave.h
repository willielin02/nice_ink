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

	// 選單語言（NiLoc 索引；-1＝未選過→依 OS 文化自動偵測）
	UPROPERTY()
	int32 LanguageIndex = -1;

	// 偏好版本號：每次使用者改動遞增。雲端同步（NiPersona）比帳用——
	// 高者贏、平手本機贏；跨裝置同步的夠用規則
	UPROPERTY()
	int32 Revision = 0;
};
