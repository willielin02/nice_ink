#include "NiceInkGameInstance.h"

#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "IOnlineSubsystemEOS.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Kismet/GameplayStatics.h"
#include "NiceInkLocText.h"
#include "NiceInkPersonaSubsystem.h"
#include "NiceInkSessionSubsystem.h"
#include "NiceInkSettingsSave.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "Sound/SoundBase.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Misc/FileHelper.h"
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
		RenderScalePct = FMath::Clamp(Save->RenderScalePct, 50.0f, 100.0f);
		SavedLang = Save->LanguageIndex;
		SettingsRevision = Save->Revision;
	}
	ApplyRenderScale();

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

void UNiceInkGameInstance::NiDumpSK(const FString& AssetPath, const FString& OutPath)
{
	USkeletalMesh* SK = LoadObject<USkeletalMesh>(nullptr, *AssetPath);
	if (!SK || !SK->GetResourceForRendering() || SK->GetResourceForRendering()->LODRenderData.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("NiDumpSK: no render data for %s"), *AssetPath); return;
	}
	const FSkeletalMeshLODRenderData& LOD = SK->GetResourceForRendering()->LODRenderData[0];
	const uint32 N = LOD.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices();
	const bool bHasColor = LOD.StaticVertexBuffers.ColorVertexBuffer.GetNumVertices() == N;
	const uint32 NumUV = LOD.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();
	FString Out; Out.Reserve(N * 160);
	Out += TEXT("sec,i,px,py,pz,nx,ny,nz,tx,ty,tz,u0,v0,u1,v1,r,g,b,a,b0,w0,b1,w1,b2,w2,b3,w3,u2,v2,by0,by1,by2\n");
	for (int32 S = 0; S < LOD.RenderSections.Num(); ++S)
	{
		const FSkelMeshRenderSection& Sec = LOD.RenderSections[S];
		for (uint32 k = 0; k < Sec.NumVertices; ++k)
		{
			const uint32 i = Sec.BaseVertexIndex + k;
			const FVector3f P = LOD.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(i);
			const FVector3f Nn = LOD.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(i);
			const FVector3f T = LOD.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentX(i);
			const FVector2f UV0 = NumUV > 0 ? LOD.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(i, 0) : FVector2f::ZeroVector;
			const FVector2f UV1 = NumUV > 1 ? LOD.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(i, 1) : FVector2f::ZeroVector;
			const FVector3f Bn = LOD.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentY(i);
			const FVector2f UV2 = NumUV > 2 ? LOD.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(i, 2) : FVector2f::ZeroVector;
			const FColor C = bHasColor ? LOD.StaticVertexBuffers.ColorVertexBuffer.VertexColor(i) : FColor::Black;
			int32 B[4] = {0,0,0,0}; float W[4] = {0,0,0,0};
			for (int32 j = 0; j < 4; ++j)
			{
				const int32 Local = LOD.SkinWeightVertexBuffer.GetBoneIndex(i, j);
				B[j] = Sec.BoneMap.IsValidIndex(Local) ? (int32)Sec.BoneMap[Local] : -1;
				W[j] = LOD.SkinWeightVertexBuffer.GetBoneWeight(i, j) / 65535.0f;
			}
			Out += FString::Printf(TEXT("%d,%u,%.4f,%.4f,%.4f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.6f,%.6f,%.6f,%.6f,%d,%d,%d,%d,%d,%.4f,%d,%.4f,%d,%.4f,%d,%.4f,%.6f,%.6f,%.4f,%.4f,%.4f\n"),
				S, i, P.X, P.Y, P.Z, Nn.X, Nn.Y, Nn.Z, T.X, T.Y, T.Z, UV0.X, UV0.Y, UV1.X, UV1.Y, C.R, C.G, C.B, C.A,
				B[0], W[0], B[1], W[1], B[2], W[2], B[3], W[3], UV2.X, UV2.Y, Bn.X, Bn.Y, Bn.Z);
		}
	}
	FFileHelper::SaveStringToFile(Out, *OutPath);
	UE_LOG(LogTemp, Warning, TEXT("NiDumpSK fmt: hiPrecTangent=%d fullPrecUV=%d maxInfl=%d use16BitBoneIdx=%d hasCloth=%d hasMorph=%d ver=%d numBones=%d requiredBones=%d"),
		LOD.StaticVertexBuffers.StaticMeshVertexBuffer.GetUseHighPrecisionTangentBasis() ? 1 : 0,
		LOD.StaticVertexBuffers.StaticMeshVertexBuffer.GetUseFullPrecisionUVs() ? 1 : 0,
		(int32)LOD.SkinWeightVertexBuffer.GetMaxBoneInfluences(), LOD.SkinWeightVertexBuffer.Use16BitBoneIndex() ? 1 : 0,
		LOD.HasClothData() ? 1 : 0, SK->GetMorphTargets().Num(), (int32)LOD.BuffersSize, SK->GetRefSkeleton().GetNum(), LOD.RequiredBones.Num());
	for (int32 S = 0; S < LOD.RenderSections.Num(); ++S)
	{
		const FSkelMeshRenderSection& Sec = LOD.RenderSections[S];
		UE_LOG(LogTemp, Warning, TEXT("NiDumpSK sec%d: mat=%d base=%u tris=%u verts=%u maxInfl=%d recomputeTangent=%d recomputeVtxMask=%d castShadow=%d disabled=%d bones=%d"),
			S, Sec.MaterialIndex, Sec.BaseIndex, Sec.NumTriangles, Sec.NumVertices, Sec.MaxBoneInfluences, Sec.bRecomputeTangent ? 1 : 0, (int32)Sec.RecomputeTangentsVertexMaskChannel, Sec.bCastShadow ? 1 : 0, Sec.bDisabled ? 1 : 0, Sec.BoneMap.Num());
	}
	// ref pose：每骨 CS 位置（bind pose 差異＝同骨轉換下蒙皮結果不同）
	{
		const FReferenceSkeleton& RS = SK->GetRefSkeleton();
		FString Bones;
		for (int32 b = 0; b < RS.GetNum(); ++b)
		{
			const FTransform& T = RS.GetRefBonePose()[b];
			Bones += FString::Printf(TEXT("%s|%.3f,%.3f,%.3f|%.4f,%.4f,%.4f,%.4f;"), *RS.GetBoneName(b).ToString(), T.GetLocation().X, T.GetLocation().Y, T.GetLocation().Z, T.GetRotation().X, T.GetRotation().Y, T.GetRotation().Z, T.GetRotation().W);
		}
		FFileHelper::SaveStringToFile(Bones, *(OutPath + TEXT(".bones.txt")));
	}
	UE_LOG(LogTemp, Warning, TEXT("NiDumpSK: %s -> %s (%u verts, %d sections, uv=%u, color=%d)"), *AssetPath, *OutPath, N, LOD.RenderSections.Num(), NumUV, bHasColor ? 1 : 0);
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
	return Save;
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
