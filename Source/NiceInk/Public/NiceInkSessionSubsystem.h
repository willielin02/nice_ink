#pragma once

#include "CoreMinimal.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "NiceInkSessionSubsystem.generated.h"

class FOnlineSessionSearch;

// 主選單輪詢用的連線狀態機（HUD 每幀讀；本專案 UI 一律輪詢不掛委派）
UENUM(BlueprintType)
enum class ENiSessionUiState : uint8
{
	Idle,        // 無事；FoundSessions 為上次搜尋結果
	Hosting,     // 建房中（成功→ServerTravel 進道場）
	Searching,   // 搜房中
	Joining,     // 加入中（成功→ClientTravel）
	Failed       // 上一動失敗；LastError 有人話說明
};

// 搜到的一個房（給主選單列表畫）
USTRUCT(BlueprintType)
struct FNiFoundSession
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Nice Ink|Session")
	FString OwnerName;

	UPROPERTY(BlueprintReadOnly, Category = "Nice Ink|Session")
	int32 PingMs = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Nice Ink|Session")
	int32 OpenSlots = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Nice Ink|Session")
	int32 MaxSlots = 0;

	// 房間碼（session 廣告屬性 NICODE；比對用，UI 永不顯示他房的碼）
	UPROPERTY(BlueprintReadOnly, Category = "Nice Ink|Session")
	FString Code;

	// 公開房（NIPUB=1）＝出現在瀏覽列表；私房只有碼能進
	UPROPERTY(BlueprintReadOnly, Category = "Nice Ink|Session")
	bool bPublic = true;

	// 對應 SearchResults 的原始索引（列表過濾後 JoinFoundSession 要用它）
	UPROPERTY(BlueprintReadOnly, Category = "Nice Ink|Session")
	int32 SearchIndex = INDEX_NONE;
};

// 連線房間管理：建房（listen server）／搜房／加入。
// 走 Online Subsystem 抽象層——DefaultPlatformService=NULL 時是 LAN 房，
// 填好 EOS 憑證並切換後（見 Docs/EOS_SETUP.md）同一套程式碼變成網路房。
UCLASS()
class NICEINK_API UNiceInkSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// 建房並以 listen server 載入道場。每房生成一個 4 字母房間碼（進 session
	// 廣告屬性＋GameInstance→GameState 複製給大廳顯示）；bPublicListed=false＝
	// 私房：不進瀏覽列表、只有碼能進。
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Session")
	void HostSession(bool bLan = true, bool bPublicListed = true);

	// 搜房（結果進 GetFoundSessions；主選單列表用）
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Session")
	void SearchSessions(bool bLan = true);

	// 按房間碼直達（朋友局主通道）：搜房→比對 NICODE→加入；公開私房都吃
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Session")
	void JoinRoomByCode(const FString& RawCode, bool bLan = true);

	// 加入搜尋結果中的第 Index 個房
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Session")
	void JoinFoundSession(int32 Index);

	// 搜到的第一個房直接加入（NiJoin console 捷徑；選單走 Search＋JoinFound）
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Session")
	void JoinFirstFoundSession(bool bLan = true);

	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Session")
	void DestroySession();

	// --- 選單輪詢面 ---

	UFUNCTION(BlueprintPure, Category = "Nice Ink|Session")
	ENiSessionUiState GetUiState() const { return UiState; }

	UFUNCTION(BlueprintPure, Category = "Nice Ink|Session")
	FString GetLastError() const { return LastError; }

	// 本地化用錯誤鍵＋參數（LastError 保留英文供 log；選單用鍵查 NiLoc 表翻譯）。
	// 鍵值=ENiLocKey 的 int（避免標頭互相依賴）；-1=無
	int32 GetLastErrorKey() const { return LastErrorKey; }
	FString GetLastErrorParam() const { return LastErrorParam; }

	UFUNCTION(BlueprintPure, Category = "Nice Ink|Session")
	const TArray<FNiFoundSession>& GetFoundSessions() const { return FoundSummaries; }

	// 線上模式（EOS）是否已設定（DefaultPlatformService=EOS）——否則選單只開 LAN
	UFUNCTION(BlueprintPure, Category = "Nice Ink|Session")
	static bool IsOnlineServiceConfigured();

private:
	TSharedPtr<FOnlineSessionSearch> SessionSearch;
	TArray<FNiFoundSession> FoundSummaries;

	ENiSessionUiState UiState = ENiSessionUiState::Idle;
	FString LastError;
	int32 LastErrorKey = -1;
	FString LastErrorParam;
	bool bAutoJoinFirst = false;

	// 碼直達流程中待比對的房間碼（OnFindSessionsComplete 消費後清除）
	FString PendingJoinCode;
	// 本次建房是否公開列出（HostSessionInternal 讀）
	bool bPendingPublicListed = true;

	// 4 字母房間碼；字元集剔除易混形（I/L/O）
	static FString MakeRoomCode();

	FDelegateHandle CreateHandle;
	FDelegateHandle FindHandle;
	FDelegateHandle JoinHandle;

	// --- EOS 登入閂（2026-08-05）---
	// EOS 下建房/搜房前要先有 Connect 登入；NULL/LAN 下 EnsureLoggedInThen 直通零行為。
	// 登入制＝persistentauth（快取 token 靜默；首次/過期→引擎內建 fallback 開
	// Account Portal 瀏覽器登入）。動作以 TFunction 暫存、登入成功後補跑。
	FDelegateHandle LoginHandle;
	bool bLoginInFlight = false;
	bool bPortalRetryUsed = false; // 靜默失敗→開瀏覽器重試一次（引擎 fallback 只掛 AutoLogin 路徑，這裡自己接）
	TFunction<void()> PendingAfterLogin;

	void EnsureLoggedInThen(TFunction<void()> Then);
	void OnLoginComplete(int32 LocalUserNum, bool bWasSuccessful, const FUniqueNetId& UserId, const FString& Error);
	void HostSessionInternal(bool bLan);
	void SearchSessionsInternal(bool bLan);

	IOnlineSessionPtr GetSessionInterface() const;
	void SetFailed(const FString& Why, int32 LocKey = -1, const FString& Param = FString());

	// 加入成功後的旅行 URL 帶上本機偏好（?Name=&Avatar=——GameMode 端解析）
	FString BuildTravelOptions() const;

	void OnCreateSessionComplete(FName SessionName, bool bWasSuccessful);
	void OnFindSessionsComplete(bool bWasSuccessful);
	void OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
};
