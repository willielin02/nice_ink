#include "NiceInkGameInstance.h"

#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "IOnlineSubsystemEOS.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "InkBodyComponent.h"
#include "NiceInkNotary.h"
#include "InkCanvasComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiceInkCharacter.h"
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

bool UNiceInkGameInstance::IsPlayerTalking(const APlayerState* PS)
{
	if (!PS)
	{
		return false;
	}
	UWorld* World = GetWorld();
	// 只在真的遊戲世界查（PIE／robo 零干擾；語音在 PIE 裡本來就不存在）
	if (!World || World->WorldType != EWorldType::Game || World->GetNetMode() == NM_Standalone)
	{
		return false;
	}
	if (!UNiceInkSessionSubsystem::IsOnlineServiceConfigured())
	{
		return false;
	}
	const double Now = World->GetRealTimeSeconds();
	if (Now - TalkingCacheAt > 0.1)
	{
		TalkingCacheAt = Now;
		TalkingCache.Reset();
		IOnlineSubsystem* OSS = Online::GetSubsystem(World);
		if (OSS && OSS->GetSubsystemName() == FName(TEXT("EOS")))
		{
			IOnlineSubsystemEOS* EOS = static_cast<IOnlineSubsystemEOS*>(OSS);
			const ULocalPlayer* LP = GetFirstGamePlayer();
			const FUniqueNetIdRepl NetId = LP ? LP->GetPreferredUniqueNetId() : FUniqueNetIdRepl();
			IVoiceChatUser* Voice = NetId.IsValid() ? EOS->GetVoiceChatUserInterface(*NetId) : nullptr;
			if (Voice && Voice->IsLoggedIn())
			{
				if (const AGameStateBase* GSB = World->GetGameState())
				{
					for (const APlayerState* Other : GSB->PlayerArray)
					{
						if (!Other) { continue; }
						FString Name;
						if (LP && LP->PlayerController && LP->PlayerController->PlayerState == Other)
						{
							Name = Voice->GetLoggedInPlayerName();
						}
						else
						{
							Name = Other->GetUniqueId().ToString();
							int32 Bar = INDEX_NONE;
							if (Name.FindChar(TEXT('|'), Bar)) { Name = Name.Mid(Bar + 1); }
						}
						TalkingCache.Add(Other->GetPlayerId(), !Name.IsEmpty() && Voice->IsPlayerTalking(Name));
					}
				}
			}
		}
	}
	const bool* Found = TalkingCache.Find(PS->GetPlayerId());
	return Found && *Found;
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
		RenderScalePct = FMath::Clamp(Save->RenderScalePct, 50.0f, 100.0f);
		SavedLang = Save->LanguageIndex;
		SettingsRevision = Save->Revision;
		PerfDefaultsVersion = Save->PerfDefaultsVersion;
		bPerfTouchedByPlayer = Save->bPerfTouchedByPlayer;
	}
	ApplyRenderScale();
	ApplyPerfDefaultsOnce();

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

	// 保底名只活在這個 session（2026-08-10：預設名從平台拿——鷹架名不落檔，
	// 玩家存檔裡只該有他親手輸入的名字）。三位數＝滿房 6 人撞名 1.5%
	//（兩位數 14%——名字要服社交叫喚，同房同名是實際干擾；2026-08-10 user 抓改）
	SessionFallbackName = FString::Printf(TEXT("rikishi%03d"), FMath::RandRange(0, 999));
}

void UNiceInkGameInstance::NiShot(float DelaySeconds, const FString& Name)
{
	// core ticker＝跨關卡存活；Shot 必走 PC->ConsoleCommand（viewport exec 鏈）
	TWeakObjectPtr<UGameInstance> WeakGI = this;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[WeakGI, Name](float) -> bool
	{
		UGameInstance* GI = WeakGI.Get();
		UWorld* World = GI ? GI->GetWorld() : nullptr;
		if (APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr)
		{
			PC->ConsoleCommand(FString::Printf(TEXT("Shot showui filename=%s"), *Name));
		}
		return false; // 一次性
	}), FMath::Max(0.1f, DelaySeconds));
}

void UNiceInkGameInstance::NiDelayExec(float DelaySeconds, const FString& Command)
{
	TWeakObjectPtr<UGameInstance> WeakGI = this;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[WeakGI, Command](float) -> bool
	{
		UGameInstance* GI = WeakGI.Get();
		UWorld* World = GI ? GI->GetWorld() : nullptr;
		if (APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr)
		{
			PC->ConsoleCommand(Command);
		}
		return false; // 一次性
	}), FMath::Max(0.1f, DelaySeconds));
}

void UNiceInkGameInstance::NiLeaveRoom()
{
	ReturnToMainMenu(FString());
}

void UNiceInkGameInstance::NiNotaryTest()
{
	// P1 公證接線自查（用法見標頭註解）。共享計數器活過整條 HTTP 鏈。
	struct FSt
	{
		int32 Pass = 0;
		int32 Fail = 0;
	};
	TSharedRef<FSt> St = MakeShared<FSt>();
	auto Check = [St](const FString& Name, bool bOk)
	{
		bOk ? ++St->Pass : ++St->Fail;
		UE_LOG(LogTemp, Warning, TEXT("NiNotaryTest: %s %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *Name);
	};
	auto Finish = [St]()
	{
		UE_LOG(LogTemp, Warning, TEXT("NiNotaryTest: %s pass=%d fail=%d"),
			St->Fail == 0 ? TEXT("DONE") : TEXT("RESULT-FAIL"), St->Pass, St->Fail);
	};

	if (!FNiceInkNotary::IsConfigured())
	{
		UE_LOG(LogTemp, Warning, TEXT("NiNotaryTest: 後端未配置（-notary=<url> 或 ini BaseUrl）"));
		return;
	}

	// --- 同步段 ---
	// SHA256 標準向量："abc"
	TArray<uint8> Abc = {'a', 'b', 'c'};
	Check(TEXT("sha256(abc) 標準向量"), FNiceInkNotary::Sha256Hex(Abc) ==
		TEXT("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

	// 信封往返＋舊裸 blob 相容
	TArray<uint8> Payload;
	for (int32 i = 0; i < 64; ++i)
	{
		Payload.Add(static_cast<uint8>(FMath::RandRange(0, 255)));
	}
	const FString FakeSig = FString::ChrN(128, TEXT('a'));
	TArray<uint8> Env;
	FNiceInkNotary::BuildEnvelope(Payload, 7, FakeSig, Env);
	TArray<uint8> OutPayload;
	int32 OutSeq = 0;
	FString OutSig;
	const bool bParse = FNiceInkNotary::ParseEnvelope(Env, OutPayload, OutSeq, OutSig);
	Check(TEXT("信封往返"), bParse && OutPayload == Payload && OutSeq == 7 && OutSig == FakeSig);
	const bool bLegacy = FNiceInkNotary::ParseEnvelope(Payload, OutPayload, OutSeq, OutSig);
	Check(TEXT("舊裸 blob 相容（seq=0）"), bLegacy && OutPayload == Payload && OutSeq == 0);

	// --- HTTP 鏈（新 puid 首簽 → 驗簽 → 篡改敗 → 無單拒簽 → latest-seq → attest → 憑單簽 seq=2）---
	const FString Puid = TEXT("test") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(24).ToLower();
	const FString Sha = FNiceInkNotary::Sha256Hex(Payload);
	FNiceInkNotary::RequestSignPersona(Puid, Sha, FString(),
		[Check, Finish, St, Puid, Sha](bool bOk, int32 Seq, FString Sig)
	{
		Check(TEXT("首簽（無單放行）seq=1"), bOk && Seq == 1);
		if (!bOk)
		{
			Finish();
			return;
		}
		Check(TEXT("C++ 驗簽通過"), FNiceInkNotary::VerifyPersonaSig(Puid, Seq, Sha, Sig));
		FString TamperedSha = Sha;
		TamperedSha[0] = (TamperedSha[0] == TEXT('0')) ? TEXT('1') : TEXT('0');
		Check(TEXT("篡改 blob 驗簽必敗"), !FNiceInkNotary::VerifyPersonaSig(Puid, Seq, TamperedSha, Sig));
		Check(TEXT("錯 seq 驗簽必敗"), !FNiceInkNotary::VerifyPersonaSig(Puid, Seq + 1, Sha, Sig));

		FNiceInkNotary::RequestSignPersona(Puid, Sha, FString(),
			[Check, Finish, Puid, Sha](bool bOk2, int32, FString)
		{
			Check(TEXT("已有帳本＋無結算單＝拒簽"), !bOk2);
			FNiceInkNotary::RequestLatestSeq(Puid,
				[Check, Finish, Puid, Sha](bool bOk3, int32 Latest)
			{
				Check(TEXT("latest-seq=1"), bOk3 && Latest == 1);
				FNiceInkNotary::RequestAttest(Puid, TEXT("testroom_") + Puid, 1, Sha, 1,
					[Check, Finish, Puid, Sha](bool bSettled, FString Token)
				{
					Check(TEXT("attest 單見證結算"), bSettled && !Token.IsEmpty());
					if (!bSettled)
					{
						Finish();
						return;
					}
					FNiceInkNotary::RequestSignPersona(Puid, Sha, Token,
						[Check, Finish, Puid, Sha, Token](bool bOk4, int32 Seq4, FString Sig4)
					{
						Check(TEXT("憑單簽發 seq=2"), bOk4 && Seq4 == 2);
						Check(TEXT("seq=2 簽章驗過"), bOk4 &&
							FNiceInkNotary::VerifyPersonaSig(Puid, Seq4, Sha, Sig4));
						// P2：escrow 登記→（已結算）單 slot 揭示→settlement 查詢
						const FString Room = TEXT("testroom_") + Puid;
						FNiceInkNotary::RequestEscrowRegister(Puid, Room, 1, 77,
							[Check, Finish, Puid, Room, Token](bool bReg)
						{
							Check(TEXT("escrow 登記"), bReg);
							FNiceInkNotary::RequestEscrowReveal(Puid, Room, 1, 77,
								[Check, Finish, Puid, Room, Token](bool bRev, FString Author)
							{
								Check(TEXT("escrow 已結算單 slot 揭示=本人"), bRev && Author == Puid);
								FNiceInkNotary::RequestSettlement(Room, 1,
									[Check, Finish, Token](bool bTok, FString Tok2)
								{
									Check(TEXT("settlement 查詢=token"), bTok && Tok2 == Token);
									Finish();
								});
							});
						});
					});
				});
			});
		});
	});
}

void UNiceInkGameInstance::NiSpotMap(int32 On)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	static UTexture2D* SpotTex = nullptr;
	if (On != 0 && !SpotTex)
	{
		SpotTex = LoadObject<UTexture2D>(nullptr, TEXT("/Game/Characters/Debug/T_SpotMap.T_SpotMap"));
		if (!SpotTex)
		{
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(9141, 8.0f, FColor::Red,
					TEXT("NiSpotMap: T_SpotMap missing (跑 sumo_spot_bake_uv.py + ue_import_spotmap.py)"));
			}
			return;
		}
	}
	int32 Applied = 0;
	for (TActorIterator<ANiceInkCharacter> It(World); It; ++It)
	{
		UInkBodyComponent* Ink = It->Body;
		UMaterialInstanceDynamic* Mid = Ink ? Ink->GetDynamicMaterial() : nullptr;
		if (!Mid)
		{
			continue;
		}
		// 麥克筆層是墨的載體：換掉它＝把普查圖當成「畫在身上的墨」，材質零改動。
		// 惰性圖層制（08-25）：線層可能根本還沒配置——還原成全透明替身，
		// 不能傳 nullptr（材質參數設 null 會退回材質預設貼圖＝整身灰格）
		UTexture* Restore = (It->InkCanvas && It->InkCanvas->GetMarkerRenderTarget())
			? Cast<UTexture>(It->InkCanvas->GetMarkerRenderTarget())
			: Cast<UTexture>(UInkCanvasComponent::GetEmptyInkTexture());
		Mid->SetTextureParameterValue(TEXT("MarkerRT"), (On != 0) ? Cast<UTexture>(SpotTex) : Restore);
		++Applied;
	}
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(9141, 6.0f, FColor::Cyan,
			FString::Printf(TEXT("NiSpotMap %s -> %d characters"), (On != 0) ? TEXT("ON") : TEXT("OFF"), Applied));
	}
}

FString UNiceInkGameInstance::GetEffectiveDisplayName() const
{
	// ① 玩家自訂名
	if (!PlayerDisplayName.IsEmpty())
	{
		return PlayerDisplayName;
	}
	// ② 平台帳號顯示名（Epic 現行；B5 Steam 票證＝同介面同路）。
	// 只認真平台：NULL/LAN 的假登入會把「電腦名-編號」當暱稱回來（實測
	// Willie_desktop-5）＝洩漏主機名，一律落到③保底。
	// Sanitize＝進 ?Name= 的同一道門（空白會被剝掉——URL 語法限制）
	if (IOnlineSubsystem* OSS = UNiceInkSessionSubsystem::IsOnlineServiceConfigured()
		? Online::GetSubsystem(GetWorld()) : nullptr)
	{
		IOnlineIdentityPtr Identity = OSS->GetIdentityInterface();
		if (Identity.IsValid() && Identity->GetLoginStatus(0) == ELoginStatus::LoggedIn)
		{
			const FString Nick = SanitizePlayerName(Identity->GetPlayerNickname(0));
			if (!Nick.IsEmpty())
			{
				return Nick;
			}
		}
	}
	// ③ 離線/LAN 保底
	return SessionFallbackName;
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
	Save->RenderScalePct = RenderScalePct;
	Save->LanguageIndex = MenuLanguage;
	Save->Revision = SettingsRevision;
	Save->PerfDefaultsVersion = PerfDefaultsVersion;
	Save->bPerfTouchedByPlayer = bPerfTouchedByPlayer;
	return Save;
}

const TArray<float>& UNiceInkGameInstance::GetFrameRateLimitChoices()
{
	// 60 起跳＝本作是滑鼠精描遊戲，再低會被感覺到；240 之上對這個場景毫無意義
	//（實測空道場的 GPU 成本 1.05ms/幀，上限之外全是純燒電）。0＝無上限恆在末位。
	static const TArray<float> Choices = { 60.0f, 90.0f, 120.0f, 144.0f, 165.0f, 240.0f, 0.0f };
	return Choices;
}

void UNiceInkGameInstance::ApplyPerfDefaultsOnce()
{
	if (bPerfTouchedByPlayer || PerfDefaultsVersion >= NiPerfDefaultsVersion)
	{
		return; // 玩家動過、或這版預設已套過＝不再介入
	}

	UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!GUS)
	{
		return; // 引擎還沒準備好＝這次跳過，下次啟動再套（版次沒推進＝不會漏）
	}

	bool bChanged = false;

	// v1：幀率上限。只在「還是引擎那個無上限」時介入
	if (PerfDefaultsVersion < 1 && GUS->GetFrameRateLimit() <= 0.0f)
	{
		GUS->SetFrameRateLimit(NiDefaultFrameRateLimit);
		bChanged = true;
		UE_LOG(LogTemp, Log, TEXT("NiPerf: frame rate cap defaulted to %.0f fps (was unlimited)"),
			NiDefaultFrameRateLimit);
	}

	// v2：VSync 開。上限只管功耗，畫面完整性是 VSync 的工作——而且 120 對 60Hz
	// 螢幕是整數 2 倍，撕裂線會幾乎定在同一高度不動＝最顯眼的那種撕裂
	//（2026-08-25 user 回報「遊戲房間內常常出現橫向像素錯位、像水平橫條」）。
	if (PerfDefaultsVersion < 2 && !GUS->IsVSyncEnabled())
	{
		GUS->SetVSyncEnabled(true);
		bChanged = true;
		UE_LOG(LogTemp, Log, TEXT("NiPerf: v-sync defaulted ON (tearing fix)"));
	}

	if (bChanged)
	{
		// ApplyNonResolutionSettings（不是 ApplySettings）：後者會連解析度一起套，
		// 把命令列的 -resx/-resy 蓋掉（實測 1280×720 的測試視窗被縮回存檔裡的 960×540）
		GUS->ApplyNonResolutionSettings();
		GUS->SaveSettings();
	}

	PerfDefaultsVersion = NiPerfDefaultsVersion;
	// 直接寫槽：不走 SaveSettings——那會遞增 Revision 並推雲端，而這只是本機
	// 一次性遷移，不是玩家改了偏好
	UGameplayStatics::SaveGameToSlot(BuildSettingsSaveObject(), SettingsSlotName, 0);
}

void UNiceInkGameInstance::ApplyRenderScale()
{
	// 只動 3D 內部渲染解析度（UI/Slate 不受影響）；GameSetting 優先級＝
	// 蓋過 scalability、讓路 console（robo/除錯手動覆寫照常有效）
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
	{
		CVar->Set(FMath::Clamp(RenderScalePct, 50.0f, 100.0f), ECVF_SetByGameSetting);
	}
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
