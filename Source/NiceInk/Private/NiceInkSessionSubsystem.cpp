#include "NiceInkSessionSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Misc/ConfigCacheIni.h"
#include "NiceInkGameInstance.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"

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

void UNiceInkSessionSubsystem::SetFailed(const FString& Why)
{
	UiState = ENiSessionUiState::Failed;
	LastError = Why;
	UE_LOG(LogTemp, Warning, TEXT("Session: %s"), *Why);
}

FString UNiceInkSessionSubsystem::BuildTravelOptions() const
{
	FString Options;
	if (const UNiceInkGameInstance* GI = Cast<UNiceInkGameInstance>(GetGameInstance()))
	{
		const FString Name = UNiceInkGameInstance::SanitizePlayerName(GI->PlayerDisplayName);
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

void UNiceInkSessionSubsystem::EnsureLoggedInThen(TFunction<void()> Then)
{
	IOnlineSubsystem* OSS = GetWorld() ? Online::GetSubsystem(GetWorld()) : nullptr;
	IOnlineIdentityPtr Identity = OSS ? OSS->GetIdentityInterface() : nullptr;
	if (!IsOnlineServiceConfigured() || !Identity.IsValid() ||
		Identity->GetLoginStatus(0) == ELoginStatus::LoggedIn)
	{
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
		SetFailed(TEXT("Epic sign-in failed — try again"));
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("NiSession: EOS login OK (%s)"), *UserId.ToString());
	if (Run)
	{
		Run();
	}
}

void UNiceInkSessionSubsystem::HostSession(bool bLan)
{
	if (UiState == ENiSessionUiState::Hosting || UiState == ENiSessionUiState::Joining)
	{
		return; // 重入護欄（實測：同一擊在連續兩幀被讀成 just-pressed → 雙重建房）
	}
	// 先佔狀態再登入：登入期間（可能開瀏覽器）選單顯示進行中、殘留點擊被護欄擋
	UiState = ENiSessionUiState::Hosting;
	LastError.Reset();
	EnsureLoggedInThen([this, bLan]() { HostSessionInternal(bLan); });
}

void UNiceInkSessionSubsystem::HostSessionInternal(bool bLan)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		SetFailed(TEXT("no session interface — online services unavailable"));
		return;
	}

	if (Sessions->GetNamedSession(NAME_GameSession))
	{
		Sessions->DestroySession(NAME_GameSession);
	}

	FOnlineSessionSettings Settings;
	Settings.NumPublicConnections = 6;
	Settings.bIsLANMatch = bLan;
	Settings.bShouldAdvertise = true;
	Settings.bAllowJoinInProgress = false; // 開賽中不收新客（回合狀態機不支援中途加入）
	Settings.bUsesPresence = !bLan;      // EOS 走 presence session
	Settings.bUseLobbiesIfAvailable = !bLan; // EOS lobby（語音掛在 lobby RTC 上）
	Settings.bUseLobbiesVoiceChatIfAvailable = !bLan; // lobby 建立即開 RTC 語音房，成員進房自動入語音（SPEC：無方位全房恆開）
	Settings.bAllowJoinViaPresence = true;
	Settings.Set(FName(TEXT("NICEINK")), FString(TEXT("dojo")), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

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
	if (!bWasSuccessful)
	{
		SetFailed(TEXT("could not create the room"));
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

void UNiceInkSessionSubsystem::SearchSessions(bool bLan)
{
	if (UiState == ENiSessionUiState::Searching || UiState == ENiSessionUiState::Joining ||
		UiState == ENiSessionUiState::Hosting)
	{
		return; // 重入護欄
	}
	UiState = ENiSessionUiState::Searching; // 先佔狀態再登入（同 HostSession）
	LastError.Reset();
	EnsureLoggedInThen([this, bLan]() { SearchSessionsInternal(bLan); });
}

void UNiceInkSessionSubsystem::SearchSessionsInternal(bool bLan)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		SetFailed(TEXT("no session interface — online services unavailable"));
		return;
	}

	SessionSearch = MakeShared<FOnlineSessionSearch>();
	SessionSearch->MaxSearchResults = 32;
	SessionSearch->bIsLanQuery = bLan;
	if (!bLan)
	{
		// EOS presence session 搜尋鍵（SEARCH_PRESENCE 常數在 5.7 移進 FName 字面值）
		SessionSearch->QuerySettings.Set(FName(TEXT("PRESENCESEARCH")), true, EOnlineComparisonOp::Equals);
	}

	UiState = ENiSessionUiState::Searching;
	LastError.Reset();
	FoundSummaries.Reset();
	FindHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateUObject(this, &UNiceInkSessionSubsystem::OnFindSessionsComplete));
	Sessions->FindSessions(0, SessionSearch.ToSharedRef());
}

void UNiceInkSessionSubsystem::JoinFirstFoundSession(bool bLan)
{
	bAutoJoinFirst = true;
	SearchSessions(bLan);
}

void UNiceInkSessionSubsystem::OnFindSessionsComplete(bool bWasSuccessful)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
	}

	const bool bWantedAutoJoin = bAutoJoinFirst;
	bAutoJoinFirst = false;

	if (!bWasSuccessful || !SessionSearch.IsValid())
	{
		SetFailed(TEXT("search failed"));
		return;
	}

	FoundSummaries.Reset();
	for (const FOnlineSessionSearchResult& R : SessionSearch->SearchResults)
	{
		FNiFoundSession S;
		S.OwnerName = R.Session.OwningUserName.IsEmpty() ? TEXT("unknown host") : R.Session.OwningUserName;
		S.PingMs = R.PingInMs;
		S.MaxSlots = R.Session.SessionSettings.NumPublicConnections;
		S.OpenSlots = R.Session.NumOpenPublicConnections;
		FoundSummaries.Add(S);
	}
	UE_LOG(LogTemp, Log, TEXT("FindSessions: %d found"), FoundSummaries.Num());

	if (FoundSummaries.IsEmpty())
	{
		SetFailed(TEXT("no rooms found on this network"));
		return;
	}

	UiState = ENiSessionUiState::Idle;
	if (bWantedAutoJoin)
	{
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
		SetFailed(TEXT("that room is no longer available"));
		return;
	}

	UiState = ENiSessionUiState::Joining;
	LastError.Reset();
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

	if (Result != EOnJoinSessionCompleteResult::Success || !Sessions.IsValid())
	{
		SetFailed(Result == EOnJoinSessionCompleteResult::SessionIsFull
			? TEXT("the room is full")
			: TEXT("could not join the room"));
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
	SetFailed(TEXT("could not resolve the room address"));
}

void UNiceInkSessionSubsystem::DestroySession()
{
	if (IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		Sessions->DestroySession(NAME_GameSession);
	}
	UiState = ENiSessionUiState::Idle;
}
