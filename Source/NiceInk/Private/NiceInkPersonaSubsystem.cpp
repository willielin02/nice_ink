#include "NiceInkPersonaSubsystem.h"

#include "NiceInkNotary.h"
#include "NiceInkPortraitBooth.h"

#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Face/NiceInkFaceBakery.h"
#include "HAL/FileManager.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "ImageUtils.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NiceInkGameInstance.h"
#include "NiceInkSaveGame.h"
#include "NiceInkSettingsSave.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	// 雲端檔名（PlayerDataStorage 每帳號 400MB——這兩檔合計 KB 級）
	const TCHAR* CloudAssetsFile = TEXT("persona_assets_v1.sav");
	const TCHAR* CloudSettingsFile = TEXT("persona_settings_v1.sav");

	// 自拍→臉管線 venv 對照組（正式路＝內建 C++/ONNX FNiFaceBakery，SPEC #52
	// 定案①；此 python 路只在 -facevenv 或模型檔缺席時使用）
	const TCHAR* FacePipelinePython = TEXT("C:/games/Unreal Engine/nice_ink_face_pipeline/venv/Scripts/python.exe");
}

void UNiceInkPersonaSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadCustomFaceFromDisk(); // 上一次上傳的臉＝本機正本，開遊戲即穿上

	// 模型預熱（2026-08-12 user 定案「盡可能縮短等待」）：首啟強制上傳閘
	// ＝無臉玩家必走臉管線，而冷啟大頭（LaMa ORT session ~60s）跟選單待機
	// 完全重疊——開機就背景建好，首次上傳直接走暖路。已有臉＝多半不再
	// 上傳，不預熱（四顆 session 常駐 RAM 不白付）；GIsEditor 閘＝PIE/robo
	// 零干擾（與亭/FaceShare 同精神）
	const bool bForceVenv = FParse::Param(FCommandLine::Get(), TEXT("facevenv"));
	if (!GIsEditor && !HasCustomFace() && !bForceVenv && FNiFaceBakery::IsAvailable())
	{
		Async(EAsyncExecution::Thread, []()
		{
			const double T0 = FPlatformTime::Seconds();
			FString Err;
			const bool bOk = FNiFaceBakery::WarmupModels(Err);
			UE_LOG(LogTemp, Log, TEXT("NiPersona: face model warmup %s in %.1fs%s%s"),
				bOk ? TEXT("done") : TEXT("FAILED"), FPlatformTime::Seconds() - T0,
				bOk ? TEXT("") : TEXT(" — "), bOk ? TEXT("") : *Err);
		});
	}
}

void UNiceInkPersonaSubsystem::Deinitialize()
{
	if (IntakeTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(IntakeTicker);
		IntakeTicker.Reset();
	}
	if (IntakeProc.IsValid())
	{
		FPlatformProcess::TerminateProc(IntakeProc);
		FPlatformProcess::CloseProc(IntakeProc);
	}
	Super::Deinitialize();
}

UNiceInkPersonaSubsystem* UNiceInkPersonaSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UNiceInkPersonaSubsystem>() : nullptr;
}

FString UNiceInkPersonaSubsystem::PuidFromNetIdString(const FString& NetIdStr)
{
	int32 BarIdx;
	if (!NetIdStr.FindChar(TEXT('|'), BarIdx))
	{
		return FString(); // NULL/LAN id 無分隔符＝走舊制
	}
	const FString Tail = NetIdStr.Mid(BarIdx + 1);
	if (Tail.Len() < 16)
	{
		return FString();
	}
	for (const TCHAR C : Tail)
	{
		const bool bHex = (C >= '0' && C <= '9') || (C >= 'a' && C <= 'f') || (C >= 'A' && C <= 'F');
		if (!bHex)
		{
			return FString();
		}
	}
	return Tail;
}

IOnlineUserCloudPtr UNiceInkPersonaSubsystem::GetUserCloud() const
{
	const UGameInstance* GI = GetGameInstance();
	if (IOnlineSubsystem* OSS = GI ? Online::GetSubsystem(GI->GetWorld()) : nullptr)
	{
		return OSS->GetUserCloudInterface();
	}
	return nullptr;
}

FUniqueNetIdPtr UNiceInkPersonaSubsystem::GetLoggedInUserId() const
{
	const UGameInstance* GI = GetGameInstance();
	IOnlineSubsystem* OSS = GI ? Online::GetSubsystem(GI->GetWorld()) : nullptr;
	IOnlineIdentityPtr Identity = OSS ? OSS->GetIdentityInterface() : nullptr;
	if (Identity.IsValid() && Identity->GetLoginStatus(0) == ELoginStatus::LoggedIn)
	{
		return Identity->GetUniquePlayerId(0);
	}
	return nullptr;
}

FString UNiceInkPersonaSubsystem::GetLocalPuid() const
{
	const FUniqueNetIdPtr Id = GetLoggedInUserId();
	return Id.IsValid() ? PuidFromNetIdString(Id->ToString()) : FString();
}

void UNiceInkPersonaSubsystem::HandleLoginSuccess()
{
	if (AssetsPull != EPullState::NotStarted)
	{
		return; // 冪等：一個 session 拉一次
	}

	const FUniqueNetIdPtr Id = GetLoggedInUserId();
	IOnlineUserCloudPtr Cloud = GetUserCloud();
	if (!Id.IsValid() || !Cloud.IsValid())
	{
		AssetsPull = EPullState::Done; // 無雲端介面：直接放行（server 端 12s 後走本機槽）
		UE_LOG(LogTemp, Warning, TEXT("NiPersona: no user cloud interface — cloud persona disabled this session"));
		return;
	}

	if (!bDelegatesBound)
	{
		ReadHandle = Cloud->AddOnReadUserFileCompleteDelegate_Handle(
			FOnReadUserFileCompleteDelegate::CreateUObject(this, &UNiceInkPersonaSubsystem::OnReadUserFileComplete));
		WriteHandle = Cloud->AddOnWriteUserFileCompleteDelegate_Handle(
			FOnWriteUserFileCompleteDelegate::CreateUObject(this, &UNiceInkPersonaSubsystem::OnWriteUserFileComplete));
		bDelegatesBound = true;
	}

	AssetsPull = EPullState::InFlight;
	UE_LOG(LogTemp, Log, TEXT("NiPersona: login OK (puid=%s) — pulling cloud persona"), *GetLocalPuid());
	if (!Cloud->ReadUserFile(*Id, CloudAssetsFile))
	{
		AssetsPull = EPullState::Done; // 同步失敗＝視為無檔
	}
	Cloud->ReadUserFile(*Id, CloudSettingsFile);
}

void UNiceInkPersonaSubsystem::OnReadUserFileComplete(bool bWasSuccessful, const FUniqueNetId& UserId, const FString& FileName)
{
	IOnlineUserCloudPtr Cloud = GetUserCloud();

	if (FileName == CloudAssetsFile)
	{
		TArray<uint8> Raw;
		if (bWasSuccessful && Cloud.IsValid())
		{
			Cloud->GetFileContents(UserId, FileName, Raw);
		}
		// P1 簽章隨身：雲端檔可能是 NIP1 信封（簽過）或舊裸 blob（未簽＝seq 0）。
		// CachedAssets 語意恆＝裸 payload——下游（上行列車/CloudSaveView）零改動。
		CachedAssets.Reset();
		CachedSeq = 0;
		CachedSigHex.Reset();
		if (Raw.Num() > 0 &&
			!FNiceInkNotary::ParseEnvelope(Raw, CachedAssets, CachedSeq, CachedSigHex))
		{
			UE_LOG(LogTemp, Warning, TEXT("NiPersona: 雲端資產信封損壞 — 視為無檔"));
			CachedAssets.Reset();
		}
		bHasCloudAssets = CachedAssets.Num() > 0;
		bCloudViewDirty = true;
		AssetsPull = EPullState::Done;
		UE_LOG(LogTemp, Log, TEXT("NiPersona: assets pull done (%s, %d bytes, seq=%d)"),
			bWasSuccessful ? TEXT("hit") : TEXT("none"), CachedAssets.Num(), CachedSeq);
		return;
	}

	if (FileName == CloudSettingsFile)
	{
		TArray<uint8> Bytes;
		if (bWasSuccessful && Cloud.IsValid() && Cloud->GetFileContents(UserId, FileName, Bytes) && Bytes.Num() > 0)
		{
			ApplyCloudSettings(Bytes);
		}
		else
		{
			PushSettings(); // 雲端沒有偏好檔（新帳號）：以本機播種
		}
	}
}

void UNiceInkPersonaSubsystem::OnWriteUserFileComplete(bool bWasSuccessful, const FUniqueNetId& /*UserId*/, const FString& FileName)
{
	if (!bWasSuccessful)
	{
		UE_LOG(LogTemp, Warning, TEXT("NiPersona: cloud write FAILED (%s) — local copy is authoritative until next write"), *FileName);
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("NiPersona: cloud write OK (%s)"), *FileName);
	}
}

void UNiceInkPersonaSubsystem::ApplyCloudSettings(const TArray<uint8>& Bytes)
{
	UNiceInkSettingsSave* CloudSave = Cast<UNiceInkSettingsSave>(
		UGameplayStatics::LoadGameFromMemory(Bytes));
	UNiceInkGameInstance* GI = Cast<UNiceInkGameInstance>(GetGameInstance());
	if (!CloudSave || !GI)
	{
		return;
	}

	// Revision 比帳：高者贏、平手本機贏（跨裝置同步的夠用規則）
	if (CloudSave->Revision <= GI->SettingsRevision)
	{
		UE_LOG(LogTemp, Log, TEXT("NiPersona: local settings newer/equal (rev %d >= %d) — pushing local to cloud"),
			GI->SettingsRevision, CloudSave->Revision);
		PushSettings();
		return;
	}

	bApplyingCloudSettings = true;
	const FString CloudName = UNiceInkGameInstance::SanitizePlayerName(CloudSave->PlayerDisplayName);
	if (!CloudName.IsEmpty())
	{
		GI->PlayerDisplayName = CloudName;
	}
	GI->PreferredAvatar = CloudSave->PreferredAvatar;
	GI->MouseSensitivityScale = FMath::Clamp(CloudSave->MouseSensitivityScale, 0.2f, 3.0f);
	GI->MasterVolume = FMath::Clamp(CloudSave->MasterVolume, 0.0f, 1.0f);
	GI->RenderScalePct = FMath::Clamp(CloudSave->RenderScalePct, 50.0f, 100.0f);
	GI->ApplyRenderScale();
	GI->SettingsRevision = CloudSave->Revision;

	// 語言：robo/測試的 -culture= 命令列恆優先（與 LoadSettings 同規則）
	FString ForcedCulture;
	const bool bForced = FParse::Value(FCommandLine::Get(), TEXT("culture="), ForcedCulture) && !ForcedCulture.IsEmpty();
	if (!bForced && CloudSave->LanguageIndex >= 0)
	{
		GI->ApplyLanguage(CloudSave->LanguageIndex); // 內含本機 SaveSettings；回推被本旗標抑制
	}
	else
	{
		GI->SaveSettings();
	}
	GI->UpdateBgmVolume();
	bApplyingCloudSettings = false;

	UE_LOG(LogTemp, Log, TEXT("NiPersona: cloud settings applied (rev %d, name=%s)"),
		CloudSave->Revision, *GI->PlayerDisplayName);
}

void UNiceInkPersonaSubsystem::PushSettings()
{
	const FUniqueNetIdPtr Id = GetLoggedInUserId();
	IOnlineUserCloudPtr Cloud = GetUserCloud();
	UNiceInkGameInstance* GI = Cast<UNiceInkGameInstance>(GetGameInstance());
	if (!Id.IsValid() || !Cloud.IsValid() || !GI)
	{
		return; // 未登入＝no-op（下次登入的播種路徑會補推）
	}

	TArray<uint8> Bytes;
	if (UGameplayStatics::SaveGameToMemory(GI->BuildSettingsSaveObject(), Bytes) && Bytes.Num() > 0)
	{
		Cloud->WriteUserFile(*Id, CloudSettingsFile, Bytes);
	}
}

void UNiceInkPersonaSubsystem::StoreAssets(const TArray<uint8>& PayloadBytes, int32 Seq, const FString& SigHex)
{
	if (PayloadBytes.Num() <= 0)
	{
		return;
	}
	CachedAssets = PayloadBytes;
	CachedSeq = Seq;
	CachedSigHex = (Seq > 0) ? SigHex : FString();
	bHasCloudAssets = true;
	bCloudViewDirty = true;

	const FUniqueNetIdPtr Id = GetLoggedInUserId();
	IOnlineUserCloudPtr Cloud = GetUserCloud();
	if (Id.IsValid() && Cloud.IsValid())
	{
		// P1：簽過章＝NIP1 信封落雲；未簽（Seq=0）＝裸 payload（與簽章制之前逐位相同）
		TArray<uint8> Wire;
		if (CachedSeq > 0)
		{
			FNiceInkNotary::BuildEnvelope(PayloadBytes, CachedSeq, CachedSigHex, Wire);
		}
		else
		{
			Wire = PayloadBytes; // WriteUserFile 要非 const
		}
		Cloud->WriteUserFile(*Id, CloudAssetsFile, Wire);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("NiPersona: StoreAssets while not logged in — cached only"));
	}
}

UNiceInkSaveGame* UNiceInkPersonaSubsystem::GetCloudSaveView()
{
	if (bCloudViewDirty)
	{
		bCloudViewDirty = false;
		CloudSaveView = nullptr;
		if (CachedAssets.Num() > 0)
		{
			CloudSaveView = Cast<UNiceInkSaveGame>(UGameplayStatics::LoadGameFromMemory(CachedAssets));
		}
	}
	return CloudSaveView;
}

// --- 自訂臉（SPEC #52 v4.0e）---

FString UNiceInkPersonaSubsystem::PlayerFaceDir() const
{
	return FPaths::ProjectSavedDir() / TEXT("PlayerFace");
}

FString UNiceInkPersonaSubsystem::LibraryDir() const
{
	return PlayerFaceDir() / TEXT("library");
}

void UNiceInkPersonaSubsystem::LoadCustomFaceFromDisk()
{
	const FString Root = PlayerFaceDir();

	// 臉庫前的單臉平鋪檔（08-06 首版）→ 遷移成 library/legacy（一次性）
	if (FPaths::FileExists(Root / TEXT("face_open.png")))
	{
		const FString Legacy = LibraryDir() / TEXT("legacy");
		IFileManager::Get().MakeDirectory(*Legacy, /*Tree=*/true);
		const TCHAR* Names[] = { TEXT("face_open.png"), TEXT("face_closed.png"),
			TEXT("eye_mask_ink.png"), TEXT("skin_color.json"), TEXT("intake_log.txt") };
		for (const TCHAR* Name : Names)
		{
			if (FPaths::FileExists(Root / Name))
			{
				IFileManager::Get().Move(*(Legacy / Name), *(Root / Name));
			}
		}
		FFileHelper::SaveStringToFile(TEXT("legacy"), *(Root / TEXT("active.txt")));
	}

	FString Active;
	FFileHelper::LoadFileToString(Active, *(Root / TEXT("active.txt")));
	Active = Active.TrimStartAndEnd();
	if (Active.IsEmpty())
	{
		const TArray<FString> Ids = ListLibraryFaceIds();
		if (Ids.Num() == 0)
		{
			return; // 沒上傳過臉
		}
		Active = Ids[0];
	}
	if (ActivateFace(Active))
	{
		UE_LOG(LogTemp, Log, TEXT("NiPersona: custom face '%s' loaded (%d in library)"),
			*ActiveFaceId, ListLibraryFaceIds().Num());
	}
}

TArray<FString> UNiceInkPersonaSubsystem::ListLibraryFaceIds() const
{
	TArray<FString> Ids;
	IFileManager::Get().FindFiles(Ids, *(LibraryDir() / TEXT("*")), /*Files=*/false, /*Dirs=*/true);
	Ids.RemoveAll([this](const FString& Id)
	{
		return !FPaths::FileExists(LibraryDir() / Id / TEXT("face_open.png"));
	});
	Ids.Sort([](const FString& A, const FString& B) { return A > B; }); // 時間戳 id：新→舊
	return Ids;
}

FString UNiceInkPersonaSubsystem::GetActiveFaceDir() const
{
	return ActiveFaceId.IsEmpty() ? FString() : LibraryDir() / ActiveFaceId;
}

bool UNiceInkPersonaSubsystem::ActivateFace(const FString& Id)
{
	const FString Dir = LibraryDir() / Id;
	if (!FPaths::FileExists(Dir / TEXT("face_open.png")) || !ImportFaceArtifacts(Dir))
	{
		return false;
	}
	ActiveFaceId = Id;
	FFileHelper::SaveStringToFile(Id, *(PlayerFaceDir() / TEXT("active.txt")));
	return true;
}

UTexture* UNiceInkPersonaSubsystem::GetFaceThumb(const FString& Id)
{
	if (const TObjectPtr<UTexture>* Found = ThumbCache.Find(Id))
	{
		return *Found;
	}
	// 頭像亭 3D 肖像（2026-08-12：取代 thumb.png 的貼圖裁切「膚色豆腐」）。
	// 工件缺席＝記 nullptr 不重試；亭未就緒＝不記快取（下次再來）
	const FString Dir = LibraryDir() / Id;
	if (!FPaths::FileExists(Dir / TEXT("face_open.png")))
	{
		ThumbCache.Add(Id, nullptr);
		return nullptr;
	}
	ANiceInkPortraitBooth* Booth = ANiceInkPortraitBooth::Get(
		GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr);
	if (!Booth)
	{
		return nullptr;
	}
	TObjectPtr<UTexture2D> Open, Closed, Mask;
	FLinearColor Skin = FLinearColor(0.4f, 0.22f, 0.13f);
	if (!ImportFaceDirInto(Dir, Open, Closed, Mask, Skin))
	{
		ThumbCache.Add(Id, nullptr);
		return nullptr;
	}
	UTexture* Portrait = Booth->GetPortraitKeyed(TEXT("lib_") + Id, Open, Closed, Mask, Skin);
	if (Portrait)
	{
		ThumbCache.Add(Id, Portrait);
	}
	return Portrait;
}

double UNiceInkPersonaSubsystem::GetIntakeElapsedS() const
{
	return (IntakeState == EFaceIntakeState::Running)
		? FPlatformTime::Seconds() - IntakeStartTime : 0.0;
}

bool UNiceInkPersonaSubsystem::ImportFaceDirInto(const FString& Dir, TObjectPtr<UTexture2D>& OutOpen,
	TObjectPtr<UTexture2D>& OutClosed, TObjectPtr<UTexture2D>& OutMask, FLinearColor& OutSkin)
{
	UTexture2D* Open = FImageUtils::ImportFileAsTexture2D(Dir / TEXT("face_open.png"));
	UTexture2D* Closed = FImageUtils::ImportFileAsTexture2D(Dir / TEXT("face_closed.png"));
	UTexture2D* Mask = FImageUtils::ImportFileAsTexture2D(Dir / TEXT("eye_mask_ink.png"));
	if (!Open || !Closed || !Mask)
	{
		UE_LOG(LogTemp, Warning, TEXT("NiPersona: face artifact import failed in %s"), *Dir);
		return false;
	}
	// 眼罩＝資料圖（UV0 乘進墨層），不是顏色——關 sRGB
	Mask->SRGB = false;
	Mask->UpdateResource();

	// 膚色（管線量測 linear_rgb；讀不到＝保守預設不擋臉）
	FString Json;
	if (FFileHelper::LoadFileToString(Json, *(Dir / TEXT("skin_color.json"))))
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		const TArray<TSharedPtr<FJsonValue>>* Rgb = nullptr;
		if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid() &&
			Root->TryGetArrayField(TEXT("linear_rgb"), Rgb) && Rgb && Rgb->Num() >= 3)
		{
			OutSkin = FLinearColor(
				(*Rgb)[0]->AsNumber(), (*Rgb)[1]->AsNumber(), (*Rgb)[2]->AsNumber());
		}
	}

	OutOpen = Open;
	OutClosed = Closed;
	OutMask = Mask;
	return true;
}

bool UNiceInkPersonaSubsystem::ImportFaceArtifacts(const FString& Dir)
{
	if (!ImportFaceDirInto(Dir, FaceOpenTex, FaceClosedTex, EyeMaskInkTex, CustomSkinTone))
	{
		return false;
	}
	++FaceRevision;
	return true;
}

bool UNiceInkPersonaSubsystem::GetAuthorFace(UTexture2D*& OutOpen, UTexture2D*& OutClosed,
	UTexture2D*& OutMask, FLinearColor& OutSkin)
{
	if (!bAuthorFaceLoadTried)
	{
		bAuthorFaceLoadTried = true; // 缺檔也只試一次（每 tick 輪詢不重複打磁碟）
		const FString Dir = FPaths::ProjectContentDir() / TEXT("AuthorFace");
		if (FPaths::FileExists(Dir / TEXT("face_open.png")))
		{
			ImportFaceDirInto(Dir, AuthorFaceOpenTex, AuthorFaceClosedTex,
				AuthorEyeMaskInkTex, AuthorSkinTone);
		}
	}
	if (!AuthorFaceOpenTex || !AuthorFaceClosedTex || !AuthorEyeMaskInkTex)
	{
		return false;
	}
	OutOpen = AuthorFaceOpenTex;
	OutClosed = AuthorFaceClosedTex;
	OutMask = AuthorEyeMaskInkTex;
	OutSkin = AuthorSkinTone;
	return true;
}

void UNiceInkPersonaSubsystem::BeginSelfieIntake(const FString& SelfiePath)
{
	if (IntakeState == EFaceIntakeState::Running)
	{
		return; // 管線跑一趟要幾十秒——重入忽略
	}

	// 內建管線優先（SPEC #52 定案①）：模型在 Content/FaceBakery 就緒即走 C++/ONNX；
	// -facevenv＝強制舊 venv python 路（開發對照組）
	const bool bForceVenv = FParse::Param(FCommandLine::Get(), TEXT("facevenv"));
	if (!bForceVenv && FNiFaceBakery::IsAvailable())
	{
		PendingIntakeId = FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"));
		const FString OutDir = FPaths::ConvertRelativePathToFull(LibraryDir() / PendingIntakeId);
		IFileManager::Get().MakeDirectory(*OutDir, /*Tree=*/true);
		IntakeState = EFaceIntakeState::Running;
		IntakeStartTime = FPlatformTime::Seconds();
		NativeIntake = MakeShared<FNativeIntakeState, ESPMode::ThreadSafe>();
		TSharedPtr<FNativeIntakeState, ESPMode::ThreadSafe> State = NativeIntake;
		UE_LOG(LogTemp, Log, TEXT("NiPersona: selfie intake started NATIVE (%s -> %s)"), *SelfiePath, *PendingIntakeId);
		Async(EAsyncExecution::Thread, [SelfiePath, OutDir, State]()
		{
			FString Err;
			State->bSuccess = FNiFaceBakery::RunIntake(SelfiePath, OutDir, Err);
			State->bDone = true;
		});
		if (!IntakeTicker.IsValid())
		{
			IntakeTicker = FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateUObject(this, &UNiceInkPersonaSubsystem::TickIntake), 0.5f);
		}
		return;
	}

	if (!FPaths::FileExists(FacePipelinePython))
	{
		UE_LOG(LogTemp, Warning, TEXT("NiPersona: face pipeline python missing (%s) — selfie intake unavailable on this machine"), FacePipelinePython);
		IntakeState = EFaceIntakeState::Failed;
		return;
	}

	// 全部轉絕對路徑：-game 下 ProjectDir/SavedDir 是相對引擎二進位的相對路徑，
	// 原樣丟給子行程＝cwd 解讀錯位（首驗 5 秒即死 exit=1 的元凶候選）。
	// 輸出進臉庫新格（時間戳 id）——完成後 Activate＝舊臉保留可切回
	PendingIntakeId = FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"));
	const FString Script = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectDir() / TEXT("Tools/FacePipeline/intake_selfie.py"));
	const FString OutDir = FPaths::ConvertRelativePathToFull(LibraryDir() / PendingIntakeId);
	const FString WorkDir = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectDir() / TEXT("Tools/FacePipeline"));
	IFileManager::Get().MakeDirectory(*OutDir, /*Tree=*/true);
	const FString Args = FString::Printf(TEXT("\"%s\" \"%s\" \"%s\""), *Script, *SelfiePath, *OutDir);

	IntakeProc = FPlatformProcess::CreateProc(FacePipelinePython, *Args,
		/*bLaunchDetached=*/false, /*bLaunchHidden=*/true, /*bLaunchReallyHidden=*/true,
		nullptr, /*Priority=*/0, *WorkDir, nullptr);
	if (!IntakeProc.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("NiPersona: failed to launch face pipeline"));
		IntakeState = EFaceIntakeState::Failed;
		return;
	}

	IntakeState = EFaceIntakeState::Running;
	IntakeStartTime = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Log, TEXT("NiPersona: selfie intake started (%s -> %s)"), *SelfiePath, *PendingIntakeId);
	if (!IntakeTicker.IsValid())
	{
		IntakeTicker = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &UNiceInkPersonaSubsystem::TickIntake), 0.5f);
	}
}

bool UNiceInkPersonaSubsystem::TickIntake(float /*DeltaSeconds*/)
{
	// 內建管線路：輪詢背景執行緒完成旗標（成功語義與行程路一致）
	if (NativeIntake.IsValid())
	{
		if (IntakeState != EFaceIntakeState::Running)
		{
			NativeIntake.Reset();
			IntakeTicker.Reset();
			return false;
		}
		if (!NativeIntake->bDone)
		{
			return true;
		}
		const bool bOk = NativeIntake->bSuccess;
		NativeIntake.Reset();
		if (bOk && ActivateFace(PendingIntakeId))
		{
			IntakeState = EFaceIntakeState::Done;
			UE_LOG(LogTemp, Log, TEXT("NiPersona: selfie intake DONE native ('%s', face rev %d)"), *ActiveFaceId, FaceRevision);
		}
		else
		{
			IntakeState = EFaceIntakeState::Failed;
			UE_LOG(LogTemp, Warning, TEXT("NiPersona: selfie intake FAILED (native)"));
		}
		IntakeTicker.Reset();
		return false;
	}

	if (IntakeState != EFaceIntakeState::Running || !IntakeProc.IsValid())
	{
		IntakeTicker.Reset();
		return false; // 停 ticker
	}
	if (FPlatformProcess::IsProcRunning(IntakeProc))
	{
		return true; // 續等
	}

	int32 ReturnCode = -1;
	FPlatformProcess::GetProcReturnCode(IntakeProc, &ReturnCode);
	FPlatformProcess::CloseProc(IntakeProc);
	IntakeProc = FProcHandle();

	if (ReturnCode == 0 && ActivateFace(PendingIntakeId))
	{
		IntakeState = EFaceIntakeState::Done;
		UE_LOG(LogTemp, Log, TEXT("NiPersona: selfie intake DONE ('%s', face rev %d)"), *ActiveFaceId, FaceRevision);
	}
	else
	{
		IntakeState = EFaceIntakeState::Failed;
		UE_LOG(LogTemp, Warning, TEXT("NiPersona: selfie intake FAILED (exit=%d)"), ReturnCode);
	}
	IntakeTicker.Reset();
	return false;
}
