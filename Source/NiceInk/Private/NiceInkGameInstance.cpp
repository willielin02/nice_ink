#include "NiceInkGameInstance.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "NiceInkSessionSubsystem.h"
#include "NiceInkSettingsSave.h"

namespace
{
	const TCHAR* SettingsSlotName = TEXT("NiceInk_Settings");
	const TCHAR* MainMenuMapPath = TEXT("/Game/Maps/L_MainMenu");
}

void UNiceInkGameInstance::Init()
{
	Super::Init();

	LoadSettings();

	if (GEngine)
	{
		GEngine->OnNetworkFailure().AddUObject(this, &UNiceInkGameInstance::HandleNetworkFailure);
		GEngine->OnTravelFailure().AddUObject(this, &UNiceInkGameInstance::HandleTravelFailure);
	}
}

UNiceInkGameInstance* UNiceInkGameInstance::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? Cast<UNiceInkGameInstance>(World->GetGameInstance()) : nullptr;
}

FString UNiceInkGameInstance::SanitizePlayerName(const FString& Raw)
{
	FString Out;
	for (const TCHAR C : Raw)
	{
		const bool bOk = (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') ||
			(C >= '0' && C <= '9') || C == '_' || C == '-';
		if (bOk)
		{
			Out.AppendChar(C);
		}
		if (Out.Len() >= 16)
		{
			break;
		}
	}
	return Out;
}

float UNiceInkGameInstance::GetMouseScale() const
{
	return FMath::Clamp(MouseSensitivityScale, 0.2f, 3.0f);
}

void UNiceInkGameInstance::LoadSettings()
{
	if (const UNiceInkSettingsSave* Save = Cast<UNiceInkSettingsSave>(
		UGameplayStatics::LoadGameFromSlot(SettingsSlotName, 0)))
	{
		PlayerDisplayName = SanitizePlayerName(Save->PlayerDisplayName);
		PreferredAvatar = Save->PreferredAvatar;
		MouseSensitivityScale = FMath::Clamp(Save->MouseSensitivityScale, 0.2f, 3.0f);
		MasterVolume = FMath::Clamp(Save->MasterVolume, 0.0f, 1.0f);
	}

	if (PlayerDisplayName.IsEmpty())
	{
		// 首次啟動的預設名：可讀、可直接開玩、也逼著玩家看見改名入口
		PlayerDisplayName = FString::Printf(TEXT("rikishi%02d"), FMath::RandRange(0, 99));
		SaveSettings();
	}
}

void UNiceInkGameInstance::SaveSettings()
{
	UNiceInkSettingsSave* Save = Cast<UNiceInkSettingsSave>(
		UGameplayStatics::CreateSaveGameObject(UNiceInkSettingsSave::StaticClass()));
	Save->PlayerDisplayName = SanitizePlayerName(PlayerDisplayName);
	Save->PreferredAvatar = PreferredAvatar;
	Save->MouseSensitivityScale = MouseSensitivityScale;
	Save->MasterVolume = MasterVolume;
	UGameplayStatics::SaveGameToSlot(Save, SettingsSlotName, 0);
}

void UNiceInkGameInstance::ReturnToMainMenu(const FString& Reason)
{
	PendingDisconnectReason = Reason;

	if (UNiceInkSessionSubsystem* Sessions = GetSubsystem<UNiceInkSessionSubsystem>())
	{
		Sessions->DestroySession();
	}

	// OpenLevel 斷開現有連線並本地載入主選單（host 離開＝房間解散，
	// 客戶端會走 NetworkFailure 路徑各自回選單）
	UGameplayStatics::OpenLevel(this, FName(MainMenuMapPath));
}

FString UNiceInkGameInstance::ConsumeDisconnectReason()
{
	FString Out = PendingDisconnectReason;
	PendingDisconnectReason.Reset();
	return Out;
}

void UNiceInkGameInstance::HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver,
	ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	// 只處理本 GameInstance 的世界（PIE 多世界同進程）
	if (World && World->GetGameInstance() != this)
	{
		return;
	}
	UE_LOG(LogTemp, Warning, TEXT("NetworkFailure %d: %s"), static_cast<int32>(FailureType), *ErrorString);

	FString Reason;
	switch (FailureType)
	{
	case ENetworkFailure::ConnectionLost:
	case ENetworkFailure::ConnectionTimeout:
		Reason = TEXT("connection to the room was lost");
		break;
	case ENetworkFailure::OutdatedClient:
	case ENetworkFailure::OutdatedServer:
		Reason = TEXT("game version mismatch with the host");
		break;
	default:
		Reason = TEXT("network error — returned to the main menu");
		break;
	}
	ReturnToMainMenu(Reason);
}

void UNiceInkGameInstance::HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString)
{
	if (World && World->GetGameInstance() != this)
	{
		return;
	}
	UE_LOG(LogTemp, Warning, TEXT("TravelFailure %d: %s"), static_cast<int32>(FailureType), *ErrorString);
	ReturnToMainMenu(TEXT("failed to reach the room"));
}
