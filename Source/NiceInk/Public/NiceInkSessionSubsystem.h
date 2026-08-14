#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
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

	// 房主語言（NILANG＝主選單語言索引；2026-08-14 user 定案：這遊戲的配對
	// 邊界是語言/文化不是地理——哏要看得懂、語音要能聊才有指認的情報流。
	// 列表同語言優先；INDEX_NONE＝舊房無屬性）
	UPROPERTY(BlueprintReadOnly, Category = "Nice Ink|Session")
	int32 LangIndex = INDEX_NONE;

	// 公開房房名（NINAME＝徵人啟事：讓瀏覽者知道這房在找怎樣的人；可空）
	UPROPERTY(BlueprintReadOnly, Category = "Nice Ink|Session")
	FString RoomName;

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
	// LangIndex=INDEX_NONE＝跟介面語言（公開房的社群邊界；邀請制無作用）；
	// RoomName＝公開房徵人啟事（可空；只在公開房上廣告）
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Session")
	void HostSession(bool bLan = true, bool bPublicListed = true, int32 MaxPlayers = 6,
		int32 LangIndex = -1, const FString& RoomName = TEXT(""));

	// 搜房（結果進 GetFoundSessions；主選單列表用）。LangFilter＝語言過濾
	//（-1=全部；EOS 走查詢端屬性過濾＝規模化正解、LAN 由顯示層過濾——
	// LAN beacon 回應本子網全部房、天然小規模）。bBackground＝背景自動更新：
	// UI 全程靜音（狀態列不講話、按鈕不變灰、舊列表掛著等新結果＝零閃爍）
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Session")
	void SearchSessions(bool bLan = true, int32 LangFilter = -1, bool bBackground = false);

	// 背景自動更新搜尋進行中（UI 靜音判準）
	bool IsBackgroundSearching() const
	{
		return bBackgroundSearch && UiState == ENiSessionUiState::Searching;
	}

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

	// 取消進行中的建房/搜房/加入（2026-08-10 UI 邏輯修：進行中狀態要有出口）。
	// 完成回呼可能已在飛行——旗標讓回呼落地時丟棄結果（建成/加成則拆房、不旅行）；
	// 登入等待中（可能開著瀏覽器）＝清掉待跑動作，登入結果照收但不再補跑。
	void CancelMenuAction();

	// 選單開場的純靜默登入（persistentauth；失敗絕不開 Account Portal、不進
	// Failed UI）——目的＝進房前就把雲端 persona（現金/刺青/偏好/名字）拉下來
	// 給個人檔案頁與舞台力士。每 session 只試一次；真登入仍由 Host/Join 觸發。
	void TrySilentLogin();

	// 目前是否已 EOS 登入（個人檔案頁「尚未登入」提示用；LAN/NULL 恆 false）
	bool IsLoggedIn() const;

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
	bool bCancelRequested = false; // CancelMenuAction 設；各動作起點與完成回呼消費

	// 碼直達流程中待比對的房間碼（OnFindSessionsComplete 消費後清除）
	FString PendingJoinCode;
	// 本次建房是否公開列出（HostSessionInternal 讀）
	bool bPendingPublicListed = true;
	// 本次建房房間人數（4~6；HostSessionInternal 讀）
	int32 PendingMaxPlayers = 6;
	// 本次建房語言（INDEX_NONE=跟介面語言）
	int32 PendingLangIndex = INDEX_NONE;
	// 本次建房房名（公開房徵人啟事；已消毒）
	FString PendingRoomName;
	// 本次搜尋的語言過濾（INDEX_NONE=全部；碼路恆全部——房號可能是任何語言的房）
	int32 PendingSearchLang = INDEX_NONE;
	// 上一輪搜尋的過濾狀態＋模式（碼路搭便車撞上過濾搜尋漏接時重搜保底用）
	int32 LastSearchLang = INDEX_NONE;
	bool bLastSearchLan = true;
	// 本輪搜尋是否背景自動更新（UI 靜音；完成/取消即清）
	bool bBackgroundSearch = false;

	// 即時串流輪詢（2026-08-14 搜尋 5s 窗根治）：LAN 回應毫秒級進
	// SearchResults、完成回呼卻死等 LAN_QUERY_TIMEOUT=5（引擎 #define 不可
	// 配置）——搜尋中每 0.2s 把現況倒進 FoundSummaries（房即到即上桌）＋
	// 碼路命中即早退加入（不等窗）
	FTSTicker::FDelegateHandle SearchPollTicker;
	bool TickSearchPoll(float DeltaSeconds);
	// SearchResults→FoundSummaries 映射＋三鍵排序（串流輪詢與完成回呼共用）
	void RebuildSummariesFromSearch();

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
	bool bSilentLoginAttempt = false; // 選單開場的純靜默登入：失敗不開 portal、不進 Failed UI
	bool bSilentLoginTried = false;
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
