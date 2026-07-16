#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "NiceInkGameInstance.generated.h"

// 跨關卡常駐層：本機玩家偏好（名字／avatar／靈敏度／音量）＋斷線回主選單。
// 偏好由 UNiceInkSettingsSave 持久化（槽位 NiceInk_Settings），Init 時載入。
UCLASS()
class NICEINK_API UNiceInkGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;

	static UNiceInkGameInstance* Get(const UObject* WorldContext);

	// --- 本機偏好（讀改後呼叫 SaveSettings 持久化）---

	UPROPERTY(BlueprintReadWrite, Category = "Nice Ink|Settings")
	FString PlayerDisplayName;

	// INDEX_NONE＝交給席位輪派
	UPROPERTY(BlueprintReadWrite, Category = "Nice Ink|Settings")
	int32 PreferredAvatar = INDEX_NONE;

	UPROPERTY(BlueprintReadWrite, Category = "Nice Ink|Settings")
	float MouseSensitivityScale = 1.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Nice Ink|Settings")
	float MasterVolume = 1.0f;

	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Settings")
	void SaveSettings();

	// 名字合法域＝[A-Za-z0-9_-] 1..16 字——直接進 ?Name= travel option，不做 URL 編碼
	static FString SanitizePlayerName(const FString& Raw);

	// 各輸入輪詢點共用的靈敏度倍率（鉗 0.2–3.0）
	float GetMouseScale() const;

	// --- 斷線／失敗回主選單 ---

	// 回主選單並記下原因（主選單 HUD 顯示一次後清除）；Reason 空＝主動離開
	void ReturnToMainMenu(const FString& Reason);

	// 主選單 HUD 取用：讀出上次斷線原因並清除
	FString ConsumeDisconnectReason();

private:
	FString PendingDisconnectReason;

	void HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);

	void LoadSettings();
};
