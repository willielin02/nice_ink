#include "NiceInkGameInstance.h"

#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "IOnlineSubsystemEOS.h"
#include "Kismet/GameplayStatics.h"
#include "NiceInkLocText.h"
#include "NiceInkPersonaSubsystem.h"
#include "NiceInkSessionSubsystem.h"
#include "NiceInkSettingsSave.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "VoiceChat.h"

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

	FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UNiceInkGameInstance::HandlePostLoadMapForVoice);
}

void UNiceInkGameInstance::HandlePostLoadMapForVoice(UWorld* World)
{
	// NULL/LAN（未配置 EOS）或單機＝零行為；語音出聲走 EOS SDK 自己的音訊裝置，
	// 不經 UE 音訊系統——沉睡者全域靜音（NiceInkAudio）天然不會誤殺語音（SPEC：
	// 遮的是視覺與情報音，聽覺開放）。
	VoiceProbeTicksLeft = 0;
	if (!World || World->GetNetMode() == NM_Standalone || IsRunningDedicatedServer())
	{
		return;
	}
	if (World->GetGameInstance() != this || !UNiceInkSessionSubsystem::IsOnlineServiceConfigured())
	{
		return;
	}
	VoiceProbeTicksLeft = 10;
	World->GetTimerManager().SetTimer(VoiceProbeTimer,
		FTimerDelegate::CreateUObject(this, &UNiceInkGameInstance::ProbeVoiceChatOnce, World),
		3.0f, /*bLoop=*/true);
}

void UNiceInkGameInstance::ProbeVoiceChatOnce(UWorld* World)
{
	--VoiceProbeTicksLeft;
	IOnlineSubsystem* OSS = Online::GetSubsystem(World);
	if (!OSS || OSS->GetSubsystemName() != FName(TEXT("EOS")))
	{
		UE_LOG(LogTemp, Warning, TEXT("NiVoice: no EOS subsystem in world (service=%s)"),
			OSS ? *OSS->GetSubsystemName().ToString() : TEXT("none"));
		World->GetTimerManager().ClearTimer(VoiceProbeTimer);
		return;
	}
	IOnlineSubsystemEOS* EOS = static_cast<IOnlineSubsystemEOS*>(OSS);
	const ULocalPlayer* LP = GetFirstGamePlayer();
	const FUniqueNetIdRepl NetId = LP ? LP->GetPreferredUniqueNetId() : FUniqueNetIdRepl();
	IVoiceChatUser* Voice = NetId.IsValid() ? EOS->GetVoiceChatUserInterface(*NetId) : nullptr;
	if (!Voice)
	{
		UE_LOG(LogTemp, Warning, TEXT("NiVoice: no voice user yet (netId %s)"),
			NetId.IsValid() ? TEXT("valid") : TEXT("invalid"));
	}
	else
	{
		const TArray<FString> Channels = Voice->GetChannels();
		UE_LOG(LogTemp, Log, TEXT("NiVoice: loggedIn=%d player=%s channels=%d [%s]"),
			Voice->IsLoggedIn() ? 1 : 0, *Voice->GetLoggedInPlayerName(),
			Channels.Num(), *FString::Join(Channels, TEXT(", ")));
		if (Channels.Num() > 0)
		{
			// 契約兌現：lobby RTC 自動入房成功——探針收工
			World->GetTimerManager().ClearTimer(VoiceProbeTimer);
			return;
		}
	}
	if (VoiceProbeTicksLeft <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("NiVoice: not in any voice channel after 30s — lobby RTC auto-join did NOT happen; check Dev Portal Client Policy has Voice permission, or wire manual JoinChannel"));
		World->GetTimerManager().ClearTimer(VoiceProbeTimer);
	}
}

UNiceInkGameInstance* UNiceInkGameInstance::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? Cast<UNiceInkGameInstance>(World->GetGameInstance()) : nullptr;
}

FString UNiceInkGameInstance::SanitizePlayerName(const FString& Raw)
{
	// v4.0e：名字上畫面＋13 語＝開放 Unicode（中日韓/假名/西里爾…全收）。
	// 黑名單制，只擋三類：控制字元與空白、travel URL 語法字（?=&/\"）、
	// 檔名保留字（<>:*|——LAN 舊制存檔槽名帶玩家名）。上限 16 字元。
	FString Out;
	for (const TCHAR C : Raw)
	{
		const bool bBad = C <= 0x20 || C == 0x7F ||
			C == '?' || C == '=' || C == '&' || C == '/' || C == '\\' || C == '"' ||
			C == '<' || C == '>' || C == ':' || C == '*' || C == '|';
		if (!bBad)
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
	int32 SavedLang = -1;
	if (const UNiceInkSettingsSave* Save = Cast<UNiceInkSettingsSave>(
		UGameplayStatics::LoadGameFromSlot(SettingsSlotName, 0)))
	{
		PlayerDisplayName = SanitizePlayerName(Save->PlayerDisplayName);
		PreferredAvatar = Save->PreferredAvatar;
		MouseSensitivityScale = FMath::Clamp(Save->MouseSensitivityScale, 0.2f, 3.0f);
		MasterVolume = FMath::Clamp(Save->MasterVolume, 0.0f, 1.0f);
		SavedLang = Save->LanguageIndex;
		SettingsRevision = Save->Revision;
	}

	// 語言優先序：-culture= 命令列（robo/測試；引擎已套用、只跟隨不覆蓋）
	// > 存檔 > OS 偵測
	FString ForcedCulture;
	if (FParse::Value(FCommandLine::Get(), TEXT("culture="), ForcedCulture) && !ForcedCulture.IsEmpty())
	{
		MenuLanguage = NiLoc::MatchLangFromCulture(ForcedCulture);
	}
	else
	{
		MenuLanguage = (SavedLang >= 0 && SavedLang < NiLoc::NumLangs)
			? SavedLang : NiLoc::DetectDefaultLang();
		FInternationalization::Get().SetCurrentCulture(NiLoc::LangCultureCode(MenuLanguage));
	}
	UE_LOG(LogTemp, Log, TEXT("NiLang: saved=%d forced='%s' osDefault=%d -> lang=%d (%s)"),
		SavedLang, *ForcedCulture, NiLoc::DetectDefaultLang(), MenuLanguage,
		NiLoc::LangCultureCode(MenuLanguage));

	if (PlayerDisplayName.IsEmpty())
	{
		// 首次啟動的預設名：可讀、可直接開玩、也逼著玩家看見改名入口
		PlayerDisplayName = FString::Printf(TEXT("rikishi%02d"), FMath::RandRange(0, 99));
		SaveSettings();
	}
}

void UNiceInkGameInstance::ApplyLanguage(int32 LangIndex)
{
	MenuLanguage = FMath::Clamp(LangIndex, 0, NiLoc::NumLangs - 1);
	// 文化同步＝字體矩陣的繁簡 Han 分流開關（SubTypeface Cultures 比對目前文化）
	FInternationalization::Get().SetCurrentCulture(NiLoc::LangCultureCode(MenuLanguage));
	SaveSettings();
}

UNiceInkSettingsSave* UNiceInkGameInstance::BuildSettingsSaveObject() const
{
	UNiceInkSettingsSave* Save = Cast<UNiceInkSettingsSave>(
		UGameplayStatics::CreateSaveGameObject(UNiceInkSettingsSave::StaticClass()));
	Save->PlayerDisplayName = SanitizePlayerName(PlayerDisplayName);
	Save->PreferredAvatar = PreferredAvatar;
	Save->MouseSensitivityScale = MouseSensitivityScale;
	Save->MasterVolume = MasterVolume;
	Save->LanguageIndex = MenuLanguage;
	Save->Revision = SettingsRevision;
	return Save;
}

void UNiceInkGameInstance::SaveSettings()
{
	// 雲端偏好套用中＝不遞增（Revision 由雲端直設）也不回推——防乒乓
	UNiceInkPersonaSubsystem* Persona = GetSubsystem<UNiceInkPersonaSubsystem>();
	const bool bApplyingCloud = Persona && Persona->IsApplyingCloudSettings();
	if (!bApplyingCloud)
	{
		++SettingsRevision;
	}

	UGameplayStatics::SaveGameToSlot(BuildSettingsSaveObject(), SettingsSlotName, 0);

	if (!bApplyingCloud && Persona)
	{
		Persona->PushSettings(); // 未登入＝no-op；登入後偏好即時上雲
	}
}

float UNiceInkGameInstance::GetBgmVolume() const
{
	return FMath::Clamp(MasterVolume, 0.0f, 1.0f) * FMath::Clamp(BgmScale, 0.0f, 1.0f);
}

void UNiceInkGameInstance::EnsureBgmPlaying(UWorld* World)
{
	if (!World || IsRunningDedicatedServer())
	{
		return;
	}
	if (BgmComponent && BgmComponent->IsPlaying())
	{
		UpdateBgmVolume();
		return;
	}

	USoundBase* Bgm = LoadObject<USoundBase>(nullptr, TEXT("/Game/Audio/bgm_sneaky_koto.bgm_sneaky_koto"));
	if (!Bgm)
	{
		UE_LOG(LogTemp, Warning, TEXT("NiBgm: asset load FAILED (/Game/Audio/bgm_sneaky_koto)"));
		return;
	}
	// bPersistAcrossLevelTransition：元件掛在 audio device 而非 world，
	// ServerTravel／OpenLevel 不中斷——「所有場景同一首」的載體
	BgmComponent = UGameplayStatics::SpawnSound2D(World, Bgm, GetBgmVolume(), 1.0f, 0.0f, nullptr,
		/*bPersistAcrossLevelTransition=*/true, /*bAutoDestroy=*/false);
	BgmStartAudioTimeS = World->GetAudioTimeSeconds(); // 編舞對拍的時間零點
	UE_LOG(LogTemp, Log, TEXT("NiBgm: playing (vol %.2f, comp %s)"),
		GetBgmVolume(), BgmComponent ? TEXT("ok") : TEXT("NULL"));
}

void UNiceInkGameInstance::UpdateBgmVolume()
{
	if (!BgmComponent)
	{
		return;
	}
	BgmComponent->SetVolumeMultiplier(GetBgmVolume());
	// 音量歸零期間引擎可能把靜音迴圈整個停掉（virtualization 預設 Disabled）
	// ——調回來時要重新起播，不能只設倍率
	if (!BgmComponent->IsPlaying() && GetBgmVolume() > 0.005f)
	{
		BgmComponent->Play();
	}
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
