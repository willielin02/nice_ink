#include "NiceInkSessionSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
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

void UNiceInkSessionSubsystem::HostSession(bool bLan)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("HostSession: no session interface"));
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
	Settings.bAllowJoinInProgress = true;
	Settings.bUsesPresence = !bLan;      // EOS 走 presence session
	Settings.bUseLobbiesIfAvailable = !bLan; // EOS lobby（語音掛在 lobby RTC 上）
	Settings.bAllowJoinViaPresence = true;
	Settings.Set(FName(TEXT("NICEINK")), FString(TEXT("sauna")), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

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
	if (bWasSuccessful && GetWorld())
	{
		GetWorld()->ServerTravel(TEXT("/Game/Maps/L_Sauna?listen"));
	}
}

void UNiceInkSessionSubsystem::JoinFirstFoundSession(bool bLan)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
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

	FindHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateUObject(this, &UNiceInkSessionSubsystem::OnFindSessionsComplete));
	Sessions->FindSessions(0, SessionSearch.ToSharedRef());
}

void UNiceInkSessionSubsystem::OnFindSessionsComplete(bool bWasSuccessful)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
	}

	if (!bWasSuccessful || !SessionSearch.IsValid() || SessionSearch->SearchResults.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("FindSessions: none found"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("FindSessions: %d found, joining first"), SessionSearch->SearchResults.Num());
	JoinHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateUObject(this, &UNiceInkSessionSubsystem::OnJoinSessionComplete));
	Sessions->JoinSession(0, NAME_GameSession, SessionSearch->SearchResults[0]);
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
		UE_LOG(LogTemp, Warning, TEXT("JoinSession failed (%d)"), static_cast<int32>(Result));
		return;
	}

	FString Connect;
	if (Sessions->GetResolvedConnectString(SessionName, Connect))
	{
		if (APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController())
		{
			PC->ClientTravel(Connect, TRAVEL_Absolute);
		}
	}
}

void UNiceInkSessionSubsystem::DestroySession()
{
	if (IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		Sessions->DestroySession(NAME_GameSession);
	}
}
