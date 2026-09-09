#include "NiceInkSessionSubsystem.h"

#include "NiceInkLocText.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "NiceInkGameInstance.h"
#include "NiceInkPersonaSubsystem.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"

namespace
{
	// session 廣告屬性鍵：房間碼／公開列出
	const FName NiCodeKey(TEXT("NICODE"));
	const FName NiPublicKey(TEXT("NIPUB"));
	const FName NiLangKey(TEXT("NILANG")); // 房主語言索引（配對邊界=語言/文化）
	const FName NiNameKey(TEXT("NINAME")); // 公開房房名（徵人啟事）
}

FString UNiceInkSessionSubsystem::MakeRoomCode()
{
	// 只用子音（2026-09-06：user 真局抽到 FCUM）——四個隨機字母含母音就會拼出髒字；
	// 子音串幾乎拼不出詞，再加一張短黑名單擋剩下的縮寫。仍剔除 I/L/O（與 1/0 混形）。
	// 20 個字母 4 位＝16 萬組合，撞碼機率可忽略。
	static const TCHAR Charset[] = TEXT("BCDFGHKMNPRSTVWXYZ");   // 2026-09-06 定案剔 J／Q（下伸尾巴）；09-07 才真的落地——此前只改了文件
	constexpr int32 N = UE_ARRAY_COUNT(Charset) - 1;
	static const TCHAR* Blocked[] = {
		TEXT("FCK"), TEXT("FKK"), TEXT("CNT"), TEXT("KNT"), TEXT("DCK"), TEXT("CCK"), TEXT("SHT"),
		TEXT("TWT"), TEXT("FGT"), TEXT("FGG"), TEXT("NGR"), TEXT("NGG"), TEXT("KKK"), TEXT("XXX"),
		TEXT("WTF"), TEXT("STD"), TEXT("SXY"), TEXT("SS"), TEXT("CM"), TEXT("JZ"), TEXT("PNS"),
		TEXT("VGN"), TEXT("BTCH"), TEXT("DMN"), TEXT("HLL"), TEXT("NZ"),
	};
	for (int32 Attempt = 0; Attempt < 64; ++Attempt)
	{
		FString Code;
		for (int32 i = 0; i < 4; ++i)
		{
			Code.AppendChar(Charset[FMath::RandRange(0, N - 1)]);
		}
		bool bBad = false;
		for (const TCHAR* B : Blocked)
		{
			if (Code.Contains(B)) { bBad = true; break; }
		}
		if (!bBad)
		{
			return Code;
		}
	}
	return TEXT("BRTK");   // 64 次都撞黑名單（機率 ~0）＝退回一個已知乾淨的碼
}

IOnlineSessionPtr UNiceInkSessionSubsystem::GetSessionInterface() const
{
	if (const UWorld* World = GetWorld())
	{
		if (IOnlineSubsystem* OSS = Online::GetSubsystem(World))
		{
			return OSS->GetSessionInterface();
		}
	}
	return nullptr;
}

bool UNiceInkSessionSubsystem::IsOnlineServiceConfigured()
{
	FString Service;
	GConfig->GetString(TEXT("OnlineSubsystem"), TEXT("DefaultPlatformService"), Service, GEngineIni);
	return Service.Equals(TEXT("EOS"), ESearchCase::IgnoreCase);
}

void UNiceInkSessionSubsystem::SetFailed(const FString& Why, int32 LocKey, const FString& Param)
{
	UiState = ENiSessionUiState::Failed;
	LastError = Why;          // 英文＝log 用
	LastErrorKey = LocKey;    // 鍵＝選單翻譯用（ENiLocKey 的 int）
	LastErrorParam = Param;
	UE_LOG(LogTemp, Warning, TEXT("Session: %s"), *Why);
}

FString UNiceInkSessionSubsystem::BuildTravelOptions() const
{
	FString Options;
	if (const UNiceInkGameInstance* GI = Cast<UNiceInkGameInstance>(GetGameInstance()))
	{
		// 有效名＝自訂 > 平台 > session 保底（2026-08-10 平台名優先制）
		const FString Name = UNiceInkGameInstance::SanitizePlayerName(GI->GetEffectiveDisplayName());
		if (!Name.IsEmpty())
		{
			Options += FString::Printf(TEXT("?Name=%s"), *Name);
		}
		if (GI->PreferredAvatar != INDEX_NONE)
		{
			Options += FString::Printf(TEXT("?Avatar=%d"), GI->PreferredAvatar);
		}
	}
	return Options;
}

bool UNiceInkSessionSubsystem::IsLoggedIn() const
{
	IOnlineSubsystem* OSS = GetWorld() ? Online::GetSubsystem(GetWorld()) : nullptr;
	IOnlineIdentityPtr Identity = OSS ? OSS->GetIdentityInterface() : nullptr;
	return IsOnlineServiceConfigured() && Identity.IsValid() &&
		Identity->GetLoginStatus(0) == ELoginStatus::LoggedIn;
}

void UNiceInkSessionSubsystem::TrySilentLogin()
{
	if (bSilentLoginTried || !IsOnlineServiceConfigured())
	{
		return;
	}
	bSilentLoginTried = true;

	IOnlineSubsystem* OSS = GetWorld() ? Online::GetSubsystem(GetWorld()) : nullptr;
	IOnlineIdentityPtr Identity = OSS ? OSS->GetIdentityInterface() : nullptr;
	if (!Identity.IsValid())
	{
		return;
	}
	if (Identity->GetLoginStatus(0) == ELoginStatus::LoggedIn)
	{
		if (UNiceInkPersonaSubsystem* Persona = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UNiceInkPersonaSubsystem>() : nullptr)
		{
			Persona->HandleLoginSuccess();
		}
		return;
	}
	if (bLoginInFlight)
	{
		return;
	}

	bLoginInFlight = true;
	bSilentLoginAttempt = true; // OnLoginComplete：失敗只記 log，不開 portal、不進 Failed UI
	LoginHandle = Identity->AddOnLoginCompleteDelegate_Handle(0,
		FOnLoginCompleteDelegate::CreateUObject(this, &UNiceInkSessionSubsystem::OnLoginComplete));
	FOnlineAccountCredentials Creds;
	Creds.Type = TEXT("persistentauth");
	Identity->Login(0, Creds);
	UE_LOG(LogTemp, Log, TEXT("NiSession: silent login attempt (menu persona prefetch)"));
}

void UNiceInkSessionSubsystem::EnsureLoggedInThen(TFunction<void()> Then)
{
	IOnlineSubsystem* OSS = GetWorld() ? Online::GetSubsystem(GetWorld()) : nullptr;
	IOnlineIdentityPtr Identity = OSS ? OSS->GetIdentityInterface() : nullptr;
	if (!IsOnlineServiceConfigured() || !Identity.IsValid() ||
		Identity->GetLoginStatus(0) == ELoginStatus::LoggedIn)
	{
		// 已登入＝補拉雲端 persona（冪等；覆蓋引擎 AutoLogin 等非本閂路徑）
		if (IsOnlineServiceConfigured() && Identity.IsValid() &&
			Identity->GetLoginStatus(0) == ELoginStatus::LoggedIn)
		{
			if (UNiceInkPersonaSubsystem* Persona = GetGameInstance()
				? GetGameInstance()->GetSubsystem<UNiceInkPersonaSubsystem>() : nullptr)
			{
				Persona->HandleLoginSuccess();
			}
		}
		Then(); // NULL/LAN 或已登入＝直通
		return;
	}

	PendingAfterLogin = MoveTemp(Then); // 疊按=最後一個動作贏（前一個尚未登入完成即被替換）
	if (bLoginInFlight)
	{
		return;
	}
	bLoginInFlight = true;
	bPortalRetryUsed = false;
	LoginHandle = Identity->AddOnLoginCompleteDelegate_Handle(0,
		FOnLoginCompleteDelegate::CreateUObject(this, &UNiceInkSessionSubsystem::OnLoginComplete));
	FOnlineAccountCredentials Creds;
	Creds.Type = TEXT("persistentauth"); // 快取靜默；首次/過期→引擎自動轉 Account Portal
	Identity->Login(0, Creds);
	UE_LOG(LogTemp, Log, TEXT("NiSession: EOS login started (persistentauth, portal fallback)"));
}

void UNiceInkSessionSubsystem::OnLoginComplete(int32 LocalUserNum, bool bWasSuccessful,
	const FUniqueNetId& UserId, const FString& Error)
{
	IOnlineSubsystem* OSS = GetWorld() ? Online::GetSubsystem(GetWorld()) : nullptr;
	IOnlineIdentityPtr Identity = OSS ? OSS->GetIdentityInterface() : nullptr;
	if (Identity.IsValid())
	{
		Identity->ClearOnLoginCompleteDelegate_Handle(0, LoginHandle);
	}

	// 選單靜默登入：失敗且沒有待跑動作＝安靜收場（不開 portal、不進 Failed UI）；
	// 靜默期間玩家已按 Host/Join（PendingAfterLogin 有值）＝併回正常路走 portal
	if (bSilentLoginAttempt && !bWasSuccessful && !PendingAfterLogin)
	{
		bSilentLoginAttempt = false;
		bLoginInFlight = false;
		UE_LOG(LogTemp, Log, TEXT("NiSession: silent login declined (%s) — persona sync waits for host/join"), *Error);
		return;
	}
	bSilentLoginAttempt = false;

	// 玩家已取消且無待跑動作：不開 portal、不報錯；登入若成功則照走成功路
	//（persona 照拉——取消的是「動作」不是登入本身）
	if (bCancelRequested && !PendingAfterLogin && !bWasSuccessful)
	{
		bCancelRequested = false;
		bLoginInFlight = false;
		UE_LOG(LogTemp, Log, TEXT("NiSession: login result discarded (user cancelled)"));
		return;
	}

	// 首次/快取過期＝persistentauth 必 EOS_InvalidAuth → 開 Account Portal 瀏覽器登入重試一次
	if (!bWasSuccessful && !bPortalRetryUsed && Identity.IsValid())
	{
		bPortalRetryUsed = true;
		UE_LOG(LogTemp, Log, TEXT("NiSession: silent login failed (%s) -> opening Epic account portal"), *Error);
		LoginHandle = Identity->AddOnLoginCompleteDelegate_Handle(0,
			FOnLoginCompleteDelegate::CreateUObject(this, &UNiceInkSessionSubsystem::OnLoginComplete));
		FOnlineAccountCredentials Creds;
		Creds.Type = TEXT("accountportal");
		Identity->Login(0, Creds);
		return; // bLoginInFlight 維持、PendingAfterLogin 保留——portal 成功後補跑
	}

	bLoginInFlight = false;
	TFunction<void()> Run = MoveTemp(PendingAfterLogin);
	PendingAfterLogin = nullptr;

	if (!bWasSuccessful)
	{
		UE_LOG(LogTemp, Warning, TEXT("NiSession: EOS login FAILED: %s"), *Error);
		SetFailed(TEXT("Epic sign-in failed — try again"), static_cast<int32>(ENiLocKey::ErrSignIn));
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("NiSession: EOS login OK (%s)"), *UserId.ToString());

	// 雲端隨身層開拉（偏好＋跨場資產；B3）——在旅行前啟動，資產在 Dojo 端等它
	if (UNiceInkPersonaSubsystem* Persona = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UNiceInkPersonaSubsystem>() : nullptr)
	{
		Persona->HandleLoginSuccess();
	}

	if (Run)
	{
		Run();
	}
}

void UNiceInkSessionSubsystem::HostSession(bool bLan, bool bPublicListed, int32 MaxPlayers,
	int32 LangIndex, const FString& RoomName)
{
	if (UiState == ENiSessionUiState::Hosting || UiState == ENiSessionUiState::Joining)
	{
		return; // 重入護欄（實測：同一擊在連續兩幀被讀成 just-pressed → 雙重建房）
	}
	// 先佔狀態再登入：登入期間（可能開瀏覽器）選單顯示進行中、殘留點擊被護欄擋
	UiState = ENiSessionUiState::Hosting;
	LastError.Reset();
	bCancelRequested = false;
	bPendingPublicListed = bPublicListed;
	PendingMaxPlayers = FMath::Clamp(MaxPlayers, 4, 6); // 房間人數＝設計人數 4~6
	PendingLangIndex = LangIndex >= 0 && LangIndex < NiLoc::NumLangs ? LangIndex : INDEX_NONE;
	// 房名消毒：控制字元剔除（空白保留——房名要能寫句子）＋截 24 碼元
	PendingRoomName.Reset();
	for (const TCHAR C : RoomName.TrimStartAndEnd())
	{
		if (C >= 0x20 && C != 0x7F)
		{
			PendingRoomName.AppendChar(C);
		}
		if (PendingRoomName.Len() >= 24)
		{
			break;
		}
	}
	EnsureLoggedInThen([this, bLan]() { HostSessionInternal(bLan); });
}

void UNiceInkSessionSubsystem::HostSessionInternal(bool bLan)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		SetFailed(TEXT("no session interface — online services unavailable"), static_cast<int32>(ENiLocKey::ErrNoOnline));
		return;
	}

	if (Sessions->GetNamedSession(NAME_GameSession))
	{
		Sessions->DestroySession(NAME_GameSession);
	}

	FOnlineSessionSettings Settings;
	Settings.NumPublicConnections = PendingMaxPlayers; // 房主人數上限（session 層滿房擋）
	Settings.bIsLANMatch = bLan;
	Settings.bShouldAdvertise = true;
	Settings.bAllowJoinInProgress = false; // 開賽中不收新客（回合狀態機不支援中途加入）
	Settings.bUsesPresence = !bLan;      // EOS 走 presence session
	Settings.bUseLobbiesIfAvailable = !bLan; // EOS lobby（語音掛在 lobby RTC 上）
	Settings.bUseLobbiesVoiceChatIfAvailable = !bLan; // lobby 建立即開 RTC 語音房，成員進房自動入語音（SPEC：無方位全房恆開）
	Settings.bAllowJoinViaPresence = true;
	Settings.Set(FName(TEXT("NICEINK")), FString(TEXT("dojo")), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	// 房間碼＋公開旗標進廣告屬性；碼同時存 GameInstance（ServerTravel 後
	// GameMode 轉進 GameState 複製給大廳顯示——私房也一樣，碼就是門）
	const FString RoomCode = MakeRoomCode();
	Settings.Set(NiCodeKey, RoomCode, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings.Set(NiPublicKey, bPendingPublicListed ? 1 : 0, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	if (UNiceInkGameInstance* GI = Cast<UNiceInkGameInstance>(GetGameInstance()))
	{
		GI->HostRoomCode = RoomCode;
		GI->HostMaxPlayers = PendingMaxPlayers; // GameMode PostLogin 轉進 GameState
		// 房主語言上廣告（社群邊界）：建房頁可改、預設跟介面語言
		const int32 Lang = PendingLangIndex != INDEX_NONE ? PendingLangIndex : GI->GetMenuLanguage();
		Settings.Set(NiLangKey, Lang, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
		// 公開房房名（徵人啟事；私房不上——列表永不顯示私房）
		if (bPendingPublicListed && !PendingRoomName.IsEmpty())
		{
			Settings.Set(NiNameKey, PendingRoomName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
		}
	}
	UE_LOG(LogTemp, Log, TEXT("NiSession: room code %s (%s)"), *RoomCode,
		bPendingPublicListed ? TEXT("public") : TEXT("invite only"));

	UiState = ENiSessionUiState::Hosting;
	LastError.Reset();
	CreateHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(
		FOnCreateSessionCompleteDelegate::CreateUObject(this, &UNiceInkSessionSubsystem::OnCreateSessionComplete));
	Sessions->CreateSession(0, NAME_GameSession, Settings);
}

void UNiceInkSessionSubsystem::OnCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	if (IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
	}
	UE_LOG(LogTemp, Log, TEXT("CreateSession %s: %s"), *SessionName.ToString(), bWasSuccessful ? TEXT("OK") : TEXT("FAILED"));
	if (bCancelRequested)
	{
		// 玩家已取消：建成的房當場拆掉、不旅行；失敗則安靜歸位
		bCancelRequested = false;
		if (bWasSuccessful)
		{
			DestroySession();
		}
		UiState = ENiSessionUiState::Idle;
		UE_LOG(LogTemp, Log, TEXT("NiSession: host cancelled by user — result discarded"));
		return;
	}
	if (!bWasSuccessful)
	{
		SetFailed(TEXT("could not create the room"), static_cast<int32>(ENiLocKey::ErrCreateFailed));
		return;
	}
	if (GetWorld())
	{
		// 刻意「維持」Hosting 直到 DestroySession/離房重設：NULL OSS 的完成是
		// 同幀/次幀同步——成功就歸 Idle 會讓下一幀的殘留點擊再建一次房
		//（第二次 HostSession 開頭的 DestroySession 會把剛建好的房拆掉＝搜不到房）。
		// 主機本人的名字/avatar 不走 ?Name=（listen server 不重登入）：
		// GameMode::PostLogin 直接從 GameInstance 讀（僅 standalone/packaged）。
		GetWorld()->ServerTravel(TEXT("/Game/Maps/L_Dojo?listen"));
	}
}

void UNiceInkSessionSubsystem::SearchSessions(bool bLan, int32 LangFilter, bool bBackground)
{
	if (UiState == ENiSessionUiState::Searching || UiState == ENiSessionUiState::Joining ||
		UiState == ENiSessionUiState::Hosting)
	{
		return; // 重入護欄
	}
	PendingJoinCode.Reset(); // 瀏覽搜尋不帶碼
	PendingSearchLang = LangFilter >= 0 && LangFilter < NiLoc::NumLangs ? LangFilter : INDEX_NONE;
	bBackgroundSearch = bBackground; // 背景=UI 靜音（狀態列/按鈕/列表全不動）
	UiState = ENiSessionUiState::Searching; // 先佔狀態再登入（同 HostSession）
	if (!bBackground)
	{
		LastError.Reset();
	}
	bCancelRequested = false;
	EnsureLoggedInThen([this, bLan]() { SearchSessionsInternal(bLan); });
}

void UNiceInkSessionSubsystem::JoinRoomByCode(const FString& RawCode, bool bLan)
{
	if (UiState == ENiSessionUiState::Searching)
	{
		// 列表自動更新的搜尋在飛（2026-08-14）：碼路搭便車——掛上待比對碼，
		// 完成回呼吃同一批結果照樣配對加入（不取消不重搜=零競態）。
		// 玩家在等了＝背景搜尋轉前景（狀態列開講）
		const FString Ride = RawCode.TrimStartAndEnd().ToUpper();
		if (Ride.Len() == 4)
		{
			PendingJoinCode = Ride;
			bBackgroundSearch = false;
		}
		return;
	}
	if (UiState == ENiSessionUiState::Joining || UiState == ENiSessionUiState::Hosting)
	{
		return; // 重入護欄
	}
	const FString Code = RawCode.TrimStartAndEnd().ToUpper();
	if (Code.Len() != 4)
	{
		SetFailed(TEXT("enter the 4-letter room code"), static_cast<int32>(ENiLocKey::ErrEnterCode));
		return;
	}
	PendingJoinCode = Code;
	PendingSearchLang = INDEX_NONE; // 碼路恆不過濾——朋友的房可能是任何語言
	bBackgroundSearch = false;      // 玩家在等＝前景
	UiState = ENiSessionUiState::Searching;
	LastError.Reset();
	bCancelRequested = false;
	EnsureLoggedInThen([this, bLan]() { SearchSessionsInternal(bLan); });
}

void UNiceInkSessionSubsystem::SearchSessionsInternal(bool bLan)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		SetFailed(TEXT("no session interface — online services unavailable"), static_cast<int32>(ENiLocKey::ErrNoOnline));
		return;
	}

	SessionSearch = MakeShared<FOnlineSessionSearch>();
	SessionSearch->MaxSearchResults = 100; // 規模版（32 的隨機子集病拆除）
	SessionSearch->bIsLanQuery = bLan;
	if (!bLan)
	{
		// EOS presence session 搜尋鍵（SEARCH_PRESENCE 常數在 5.7 移進 FName 字面值）
		SessionSearch->QuerySettings.Set(FName(TEXT("PRESENCESEARCH")), true, EOnlineComparisonOp::Equals);
		// 語言過濾＝查詢端（規模化正解：撈回來的就已經是要的語言，不在
		// 隨機子集上做客戶端過濾）；LAN 無查詢過濾＝顯示層處理（子網天然小）
		if (PendingSearchLang != INDEX_NONE)
		{
			SessionSearch->QuerySettings.Set(NiLangKey, PendingSearchLang, EOnlineComparisonOp::Equals);
		}
	}
	LastSearchLang = PendingSearchLang;
	bLastSearchLan = bLan;

	UiState = ENiSessionUiState::Searching;
	// 雙緩衝（2026-08-14 列表閃爍根治）：搜尋開始「不」清 FoundSummaries——
	// 舊列表掛著等新結果到貨整批替換（OnFindSessionsComplete 重建），
	// 清單不再每 12s 消失兩秒
	FindHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateUObject(this, &UNiceInkSessionSubsystem::OnFindSessionsComplete));
	Sessions->FindSessions(0, SessionSearch.ToSharedRef());

	// 即時串流輪詢開跑（結束/早退/取消時自拆）
	if (!SearchPollTicker.IsValid())
	{
		SearchPollTicker = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateWeakLambda(this, [this](float Dt) { return TickSearchPoll(Dt); }), 0.2f);
	}
}

void UNiceInkSessionSubsystem::JoinFirstFoundSession(bool bLan)
{
	bAutoJoinFirst = true;
	SearchSessions(bLan);
}

void UNiceInkSessionSubsystem::RebuildSummariesFromSearch()
{
	if (!SessionSearch.IsValid())
	{
		return;
	}
	FoundSummaries.Reset();
	for (int32 i = 0; i < SessionSearch->SearchResults.Num(); ++i)
	{
		const FOnlineSessionSearchResult& R = SessionSearch->SearchResults[i];
		FNiFoundSession S;
		S.OwnerName = R.Session.OwningUserName.IsEmpty() ? TEXT("unknown host") : R.Session.OwningUserName;
		S.PingMs = R.PingInMs;
		S.MaxSlots = R.Session.SessionSettings.NumPublicConnections;
		S.OpenSlots = R.Session.NumOpenPublicConnections;
		S.SearchIndex = i;
		R.Session.SessionSettings.Get(NiCodeKey, S.Code);
		int32 Pub = 1; // 舊版房（無旗標）當公開
		R.Session.SessionSettings.Get(NiPublicKey, Pub);
		S.bPublic = Pub != 0;
		S.LangIndex = INDEX_NONE; // 舊房無屬性＝未知
		R.Session.SessionSettings.Get(NiLangKey, S.LangIndex);
		R.Session.SessionSettings.Get(NiNameKey, S.RoomName);
		FoundSummaries.Add(S);
	}
	// 同語言優先（配對邊界=語言/文化）→人多優先（快成局）→ping 低。
	// SearchIndex 指回原始 SearchResults＝排序不影響加入
	int32 MyLang = 0;
	if (const UNiceInkGameInstance* GI = Cast<UNiceInkGameInstance>(GetGameInstance()))
	{
		MyLang = GI->GetMenuLanguage();
	}
	FoundSummaries.StableSort([MyLang](const FNiFoundSession& A, const FNiFoundSession& B)
	{
		const bool bAMine = A.LangIndex == MyLang;
		const bool bBMine = B.LangIndex == MyLang;
		if (bAMine != bBMine)
		{
			return bAMine;
		}
		const int32 TakenA = A.MaxSlots - A.OpenSlots;
		const int32 TakenB = B.MaxSlots - B.OpenSlots;
		if (TakenA != TakenB)
		{
			return TakenA > TakenB;
		}
		const int32 PA = A.PingMs > 0 ? A.PingMs : MAX_int32;
		const int32 PB = B.PingMs > 0 ? B.PingMs : MAX_int32;
		return PA < PB;
	});
}

bool UNiceInkSessionSubsystem::TickSearchPoll(float /*DeltaSeconds*/)
{
	if (UiState != ENiSessionUiState::Searching || !SessionSearch.IsValid())
	{
		SearchPollTicker.Reset();
		return false; // 搜尋結束＝自拆
	}
	// 即時串流：已到貨結果直接上桌（LAN 回應毫秒級、完成回呼死等 5s 窗）
	RebuildSummariesFromSearch();

	// 碼路早退：命中即加入、不等窗。退訂完成回呼＝被棄搜尋在引擎內跑完
	// 剩餘窗（期間新 FindSessions 會被 OSS 拒一次——早退後在旅行、無感；
	// 極端失敗回選單也有 12s 自動更新自癒）
	if (!PendingJoinCode.IsEmpty())
	{
		for (const FNiFoundSession& F : FoundSummaries)
		{
			if (F.Code.Equals(PendingJoinCode, ESearchCase::IgnoreCase))
			{
				const int32 Index = F.SearchIndex;
				PendingJoinCode.Reset();
				if (IOnlineSessionPtr Sessions = GetSessionInterface())
				{
					Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
				}
				UE_LOG(LogTemp, Log, TEXT("NiSession: code matched mid-search — early join"));
				UiState = ENiSessionUiState::Idle;
				SearchPollTicker.Reset();
				JoinFoundSession(Index);
				return false;
			}
		}
	}
	return true;
}

void UNiceInkSessionSubsystem::OnFindSessionsComplete(bool bWasSuccessful)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
	}
	bBackgroundSearch = false; // 本輪結束（碼路漏接重搜=玩家在等=前景）

	if (bCancelRequested)
	{
		// 玩家已取消：結果丟棄（列表不更新也無妨——下次搜尋整組重來）
		bCancelRequested = false;
		bAutoJoinFirst = false;
		PendingJoinCode.Reset();
		UiState = ENiSessionUiState::Idle;
		return;
	}

	if (UiState == ENiSessionUiState::Joining)
	{
		// 搜尋窗內玩家已點列加入（串流列表可點制）：本輪結果只更新列表、
		// 不動狀態機——Joining 的殘留點擊防護不得被踩回 Idle
		RebuildSummariesFromSearch();
		bAutoJoinFirst = false;
		PendingJoinCode.Reset();
		return;
	}

	const bool bWantedAutoJoin = bAutoJoinFirst;
	bAutoJoinFirst = false;
	const FString WantedCode = PendingJoinCode;
	PendingJoinCode.Reset();

	if (!bWasSuccessful || !SessionSearch.IsValid())
	{
		SetFailed(TEXT("search failed"), static_cast<int32>(ENiLocKey::ErrSearchFailed));
		return;
	}

	RebuildSummariesFromSearch();
	UE_LOG(LogTemp, Log, TEXT("FindSessions: %d found"), FoundSummaries.Num());

	// 碼直達：比對 NICODE（公開私房都吃），命中即加入
	if (!WantedCode.IsEmpty())
	{
		for (const FNiFoundSession& S : FoundSummaries)
		{
			if (S.Code.Equals(WantedCode, ESearchCase::IgnoreCase))
			{
				UiState = ENiSessionUiState::Idle;
				JoinFoundSession(S.SearchIndex);
				return;
			}
		}
		// 搭便車搭上了「被語言過濾的搜尋」（EOS 查詢端過濾＝結果可能根本
		// 不含朋友的房）：重搜一次不過濾、碼續掛——第二輪 LastSearchLang
		// 必為 INDEX_NONE＝不會迴圈
		if (LastSearchLang != INDEX_NONE)
		{
			UE_LOG(LogTemp, Log, TEXT("NiSession: code %s missed filtered search — retrying unfiltered"), *WantedCode);
			PendingJoinCode = WantedCode;
			PendingSearchLang = INDEX_NONE;
			UiState = ENiSessionUiState::Searching;
			SearchSessionsInternal(bLastSearchLan);
			return;
		}
		// 同機開發保底（-nilanloopback，play_full_flow*.bat 帶旗標）：Windows 上
		// 主機與搜房端同綁 UDP 14001（LANBeacon 設計）、主機回覆的單播只送達
		// 「先綁的」socket（2026-08-13 本機量測實錘）＝同一台電腦房號搜尋結構性
		// 收不到回應；兩台真機各綁各的不受影響。保底＝直連 127.0.0.1、房號帶上
		// URL 由主機 PreLogin 驗證（誤碼不得靜默連進本機別的房）。
		if (SessionSearch->bIsLanQuery && FParse::Param(FCommandLine::Get(), TEXT("nilanloopback")))
		{
			if (APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController())
			{
				UE_LOG(LogTemp, Log, TEXT("NiSession: LAN search dry — loopback fallback with code %s"), *WantedCode);
				UiState = ENiSessionUiState::Joining; // 失敗＝TravelFailure→ReturnToMainMenu→DestroySession 歸位
				PC->ClientTravel(TEXT("127.0.0.1") + BuildTravelOptions() +
					FString::Printf(TEXT("?NiCode=%s"), *WantedCode), TRAVEL_Absolute);
				return;
			}
		}
		SetFailed(FString::Printf(TEXT("no room with code %s — check the code with your host"), *WantedCode),
			static_cast<int32>(ENiLocKey::ErrNoRoomWithCode), WantedCode);
		return;
	}

	// 瀏覽搜尋零結果不是錯誤——列表空狀態由 HUD 呈現
	UiState = ENiSessionUiState::Idle;
	if (bWantedAutoJoin)
	{
		if (FoundSummaries.IsEmpty())
		{
			SetFailed(TEXT("no rooms found on this network"), static_cast<int32>(ENiLocKey::ErrNoRoomsLan));
			return;
		}
		JoinFoundSession(0);
	}
}

void UNiceInkSessionSubsystem::JoinFoundSession(int32 Index)
{
	if (UiState == ENiSessionUiState::Joining || UiState == ENiSessionUiState::Hosting)
	{
		return; // 重入護欄
	}
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid() || !SessionSearch.IsValid() || !SessionSearch->SearchResults.IsValidIndex(Index))
	{
		SetFailed(TEXT("that room is no longer available"), static_cast<int32>(ENiLocKey::ErrRoomGone));
		return;
	}

	UiState = ENiSessionUiState::Joining;
	LastError.Reset();
	bCancelRequested = false;
	JoinHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateUObject(this, &UNiceInkSessionSubsystem::OnJoinSessionComplete));
	Sessions->JoinSession(0, NAME_GameSession, SessionSearch->SearchResults[Index]);
}

void UNiceInkSessionSubsystem::OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
	}

	if (bCancelRequested)
	{
		// 玩家已取消：加成了也不旅行，退掉 session 安靜歸位
		bCancelRequested = false;
		if (Result == EOnJoinSessionCompleteResult::Success)
		{
			DestroySession();
		}
		UiState = ENiSessionUiState::Idle;
		UE_LOG(LogTemp, Log, TEXT("NiSession: join cancelled by user — result discarded"));
		return;
	}

	if (Result != EOnJoinSessionCompleteResult::Success || !Sessions.IsValid())
	{
		SetFailed(Result == EOnJoinSessionCompleteResult::SessionIsFull
			? TEXT("the room is full")
			: TEXT("could not join the room"),
			Result == EOnJoinSessionCompleteResult::SessionIsFull
			? static_cast<int32>(ENiLocKey::ErrRoomFull) : static_cast<int32>(ENiLocKey::ErrJoinFailed));
		return;
	}

	FString Connect;
	if (Sessions->GetResolvedConnectString(SessionName, Connect))
	{
		if (APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController())
		{
			// 維持 Joining 直到離房/失敗重設（同 Hosting 的殘留點擊防護）
			PC->ClientTravel(Connect + BuildTravelOptions(), TRAVEL_Absolute);
			return;
		}
	}
	SetFailed(TEXT("could not resolve the room address"), static_cast<int32>(ENiLocKey::ErrResolve));
}

void UNiceInkSessionSubsystem::DestroySession()
{
	if (IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		Sessions->DestroySession(NAME_GameSession);
	}
	if (UNiceInkGameInstance* GI = Cast<UNiceInkGameInstance>(GetGameInstance()))
	{
		GI->HostRoomCode.Reset();
		GI->HostMaxPlayers = 6;
	}
	UiState = ENiSessionUiState::Idle;
}

void UNiceInkSessionSubsystem::CancelMenuAction()
{
	if (UiState != ENiSessionUiState::Hosting && UiState != ENiSessionUiState::Searching &&
		UiState != ENiSessionUiState::Joining)
	{
		return;
	}
	// 完成回呼可能已在飛行：旗標留給回呼消費（各動作起點會重置）；
	// 登入等待中（可能開著瀏覽器）＝清待跑動作，登入結果落地時不再補跑
	bCancelRequested = true;
	PendingAfterLogin = nullptr;
	PendingJoinCode.Reset();
	bAutoJoinFirst = false;
	UiState = ENiSessionUiState::Idle;
	LastError.Reset();
	UE_LOG(LogTemp, Log, TEXT("NiSession: menu action cancelled by user"));
}
