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
};

// 連線房間管理：建房（listen server）／搜房／加入。
// 走 Online Subsystem 抽象層——DefaultPlatformService=NULL 時是 LAN 房，
// 填好 EOS 憑證並切換後（見 Docs/EOS_SETUP.md）同一套程式碼變成網路房。
UCLASS()
class NICEINK_API UNiceInkSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// 建房並以 listen server 載入道場
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Session")
	void HostSession(bool bLan = true);

	// 搜房（結果進 GetFoundSessions；主選單列表用）
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Session")
	void SearchSessions(bool bLan = true);

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
	bool bAutoJoinFirst = false;

	FDelegateHandle CreateHandle;
	FDelegateHandle FindHandle;
	FDelegateHandle JoinHandle;

	IOnlineSessionPtr GetSessionInterface() const;
	void SetFailed(const FString& Why);

	// 加入成功後的旅行 URL 帶上本機偏好（?Name=&Avatar=——GameMode 端解析）
	FString BuildTravelOptions() const;

	void OnCreateSessionComplete(FName SessionName, bool bWasSuccessful);
	void OnFindSessionsComplete(bool bWasSuccessful);
	void OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
};
