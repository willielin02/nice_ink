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

	// --- 選單語言（NiLoc 索引；13 語=照抄 Meccha 清單，2026-08-06 user 定案）---
	int32 GetMenuLanguage() const { return MenuLanguage; }
	// 設語言＋同步引擎文化（字體矩陣的繁簡分流靠 culture）＋存檔
	void ApplyLanguage(int32 LangIndex);

	// --- BGM（全遊戲唯一一首、恆定循環）---
	// 恆定不變＝零洩漏的構造保證：BGM 永不對任何遊戲狀態反應（相位／沉睡／甦醒
	// 一律不理）。沉睡者的全域靜音規格滅的是「情報音」（筆劃聲＝情報）；恆定 BGM
	// 無情報身分，照播。音量＝MasterVolume × BgmScale，混在語音之下（底噪級）。

	// HUD BeginPlay 喚起（冪等）；跨關卡由 bPersistAcrossLevelTransition 存活
	void EnsureBgmPlaying(UWorld* World);

	// MasterVolume 改動後即時生效（音量列每次調整時呼叫）
	void UpdateBgmVolume();

	// 底噪級混音比（使用者口味域旋鈕）
	UPROPERTY(BlueprintReadWrite, Category = "Nice Ink|Settings")
	float BgmScale = 0.30f;

	// BGM 起播的 audio clock 時刻（選單編舞對拍用；<0＝尚未播）——
	// 單曲恆定循環＝拍相位從起播時刻線性推算即可，不需讀音訊
	double GetBgmStartAudioTime() const { return BgmStartAudioTimeS; }

	// 名字合法域＝[A-Za-z0-9_-] 1..16 字——直接進 ?Name= travel option，不做 URL 編碼
	static FString SanitizePlayerName(const FString& Raw);

	// 本機建房生成的房間碼（transient 不入存檔）：ServerTravel 後 GameMode 讀走
	// 轉進 GameState 複製；離房／拆房清除
	UPROPERTY(Transient)
	FString HostRoomCode;

	// 各輸入輪詢點共用的靈敏度倍率（鉗 0.2–3.0）
	float GetMouseScale() const;

	// --- 斷線／失敗回主選單 ---

	// 回主選單並記下原因（主選單 HUD 顯示一次後清除）；Reason 空＝主動離開
	void ReturnToMainMenu(const FString& Reason);

	// 主選單 HUD 取用：讀出上次斷線原因並清除
	FString ConsumeDisconnectReason();

private:
	// --- EOS 語音探針（PostLoadMap 掛起；NULL/LAN 下零行為）---
	// lobby 的 bUseLobbiesVoiceChatIfAvailable 契約＝成員進房自動入 RTC 語音房。
	// 這裡不接管、只驗收：入房後每 3 秒讀登入/頻道狀態寫 log，入到頻道即收工；
	// 30 秒沒入＝大聲警告（查 Dev Portal Client Policy 的 Voice 權限）。
	void HandlePostLoadMapForVoice(class UWorld* World);
	void ProbeVoiceChatOnce(class UWorld* World);
	FTimerHandle VoiceProbeTimer;
	int32 VoiceProbeTicksLeft = 0;

	UPROPERTY(Transient)
	TObjectPtr<class UAudioComponent> BgmComponent;

	double BgmStartAudioTimeS = -1.0;

	int32 MenuLanguage = 0;

	float GetBgmVolume() const;

	FString PendingDisconnectReason;

	void HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);

	void LoadSettings();
};
