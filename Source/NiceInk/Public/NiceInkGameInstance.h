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

	// 玩家自訂名（空＝從未自訂）。畫面與連線一律走 GetEffectiveDisplayName()——
	// 自訂名 > 平台名 > session 保底；這欄只存「玩家親手輸入過的」
	UPROPERTY(BlueprintReadWrite, Category = "Nice Ink|Settings")
	FString PlayerDisplayName;

	// 有效顯示名（2026-08-10 user 定案：預設名直接從平台拿、減少摩擦）：
	// ①玩家自訂名（個人檔案頁輸入）②平台帳號顯示名（現行=Epic 暱稱；B5 切
	// Steam 票證後同一條 OSS Identity 介面自動變 Steam persona，零改動）
	// ③離線/LAN 保底＝session 隨機 rikishiNN（transient 不落檔——別把鷹架名
	// 寫進玩家存檔）。語言同理從 OS 偵測（Steam 語言拉取＝B5 併入）。
	FString GetEffectiveDisplayName() const;

	// INDEX_NONE＝交給席位輪派
	UPROPERTY(BlueprintReadWrite, Category = "Nice Ink|Settings")
	int32 PreferredAvatar = INDEX_NONE;

	UPROPERTY(BlueprintReadWrite, Category = "Nice Ink|Settings")
	float MouseSensitivityScale = 1.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Nice Ink|Settings")
	float MasterVolume = 1.0f;

	// 渲染比例 %（50~100）：只降 3D 內部渲染解析度、UI 恆原生——立即生效零切換
	UPROPERTY(BlueprintReadWrite, Category = "Nice Ink|Settings")
	float RenderScalePct = 100.0f;

	// RenderScalePct 改動後即時生效（r.ScreenPercentage）；載入/雲端套用後也要呼叫
	void ApplyRenderScale();

	// --- 效能預設（2026-08-25 幀率上限制）---
	// 幀率上限／VSync 的正本住引擎 GameUserSettings（與視窗模式同一責任邊界，
	// 不進本作的偏好存檔、也不上雲——那是每台機器自己的事）。這裡只負責
	// 「首次啟動把煞車裝上去」：引擎預設無上限＝顯卡永遠 100% 滿載。
	// v1＝120fps 上限；v2＝VSync 開（120 對 60Hz 是整數 2 倍＋VSync 關 ⇒ 撕裂線
	// 幾乎不動＝user 回報的「水平橫條」）。上限管功耗，VSync 管畫面完整性。
	static constexpr int32 NiPerfDefaultsVersion = 2;
	static constexpr float NiDefaultFrameRateLimit = 120.0f;
	int32 PerfDefaultsVersion = 0;
	bool bPerfTouchedByPlayer = false; // 玩家動過＝任何版本的預設遷移都不准再碰
	void ApplyPerfDefaultsOnce();

	// 設定頁的幀率上限選項（0＝無上限，恆為最後一項）
	static const TArray<float>& GetFrameRateLimitChoices();

	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Settings")
	void SaveSettings();

	// 偏好版本號（雲端同步比帳用；SaveSettings 遞增、套用雲端偏好時直設）
	int32 SettingsRevision = 0;

	// 以當前欄位組一顆 SettingsSave 物件（本機槽與雲端共用同一序列化）
	class UNiceInkSettingsSave* BuildSettingsSaveObject() const;

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

	// 名字合法域（v4.0e 開放 Unicode）＝黑名單制：擋控制字元/空白/URL 語法字/
	// 檔名保留字，上限 16 字——直接進 ?Name= travel option，不做 URL 編碼
	static FString SanitizePlayerName(const FString& Raw);

	// 本機建房生成的房間碼（transient 不入存檔）：ServerTravel 後 GameMode 讀走
	// 轉進 GameState 複製；離房／拆房清除
	UPROPERTY(Transient)
	FString HostRoomCode;

	// 建房頁選的人數上限（2026-08-13；同 HostRoomCode 的搬運路：SessionSubsystem
	// 寫入→GameMode PostLogin 轉進 GameState；拆房重置 6）
	UPROPERTY(Transient)
	int32 HostMaxPlayers = 6;

	// robo 鉤子：延遲截圖（Shot showui＝含 Slate/HUD）。住 GameInstance＝任何
	// 世界/任何 PC 類都路由得到（選單/道場/直連 client 通吃）、core ticker 跨
	// ServerTravel 存活——NiMenuShot 只活在選單 PC 的補位
	UFUNCTION(Exec)
	void NiShot(float DelaySeconds, const FString& Name);

	// 斑普查疊圖（2026-08-24）：把 sumo_spot_census 烘出來的 UV0 圖當成麥克筆墨層
	// 貼到每個人身上——**讓 user 的眼睛和儀器共用同一個座標系**。
	// 用法：主控台 `NiSpotMap 1` 開、`NiSpotMap 0` 還原真墨層。
	// 為什麼要這個：08-24 user 圈的兩點，離線儀器判定「乾淨」而他看得到 ⇒ 尺與眼睛不同步；
	// 褌邊那六天的教訓＝先證明「我量的那條線＝他看的那條線」，再談修。
	UFUNCTION(Exec)
	void NiSpotMap(int32 On);

	// 公證接線自查（防作弊 P1；Docs/ANTICHEAT_PLAN.md §4）：對本地後端跑
	// 信封往返→SHA256 對標準向量→簽發→C++ 驗簽→篡改必敗→無單拒簽→
	// latest-seq→attest→憑單簽發（序號 +1）。用法：起 Backend（NICEINK_DEV=1、
	// port 8787）→ 遊戲以 -notary=http://127.0.0.1:8787 啟動 → 主控台 NiNotaryTest
	// → log 收「NiNotaryTest: DONE」。這支抓的是 C++/python 兩端格式對不齊那一類 bug。
	UFUNCTION(Exec)
	void NiNotaryTest();

	// 各輸入輪詢點共用的靈敏度倍率（鉗 0.2–3.0）
	float GetMouseScale() const;

	// --- 斷線／失敗回主選單 ---

	// 回主選單並記下原因（主選單 HUD 顯示一次後清除）；Reason 空＝主動離開
	void ReturnToMainMenu(const FString& Reason);

	// 主選單 HUD 取用：讀出上次斷線原因並清除
	FString ConsumeDisconnectReason();

	// 語音：這位玩家現在在說話嗎（EOS RTC；NULL／LAN／未入頻道＝恆 false）。
	// 2026-09-07：這是一個靠語音推理的遊戲而全站沒有說話者指示；沉睡者端不畫（感官剝奪是設計）。
	bool IsPlayerTalking(const class APlayerState* PS);
private:
	// 快取（2026-09-07 血價）：一版每幀每張臉都叫 EOS 的 GetVoiceChatUserInterface ⇒ PIE 開始 5 秒
	// D3D12 E_OUTOFMEMORY。只在 Game 世界查、每 0.1s 一次、結果按 PlayerId 快取。
	TMap<int32, bool> TalkingCache;
	double TalkingCacheAt = -1.0;
public:

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

	// 離線/LAN 的 session 保底名（不入存檔；見 GetEffectiveDisplayName）
	FString SessionFallbackName;

	void HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);

	void LoadSettings();
};
