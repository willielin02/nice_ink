#include "NiceInkGameMode.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/PlayerController.h"
#include "InkCanvasComponent.h"
#include "InkTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Crc.h"
#include "NiceInkCharacter.h"
#include "NiceInkFaceShare.h"
#include "NiceInkGameInstance.h"
#include "NiceInkGameSession.h"
#include "NiceInkGameState.h"
#include "NiceInkHUD.h"
#include "NiceInkPersonaSubsystem.h"
#include "NiceInkPlayerState.h"
#include "NiceInkSaveGame.h"
#include "NiceInkSessionSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "TimerManager.h"

ANiceInkGameMode::ANiceInkGameMode()
{
	GameStateClass = ANiceInkGameState::StaticClass();
	PlayerStateClass = ANiceInkPlayerState::StaticClass();
	HUDClass = ANiceInkHUD::StaticClass();
	DefaultPawnClass = ANiceInkCharacter::StaticClass();
	// 引擎 match≠我們的局：預設 GameSession 開場即 StartSession＝LAN beacon
	// 無聲拒答（2026-08-14 定罪）——換 no-op 版、session 狀態走 SetSessionInProgress
	GameSessionClass = ANiceInkGameSession::StaticClass();

	// 醉夢迷宮難度檔：每杯一組（ini 有覆寫時 config 載入會蓋掉這裡）
	MazeParamsPerCup.Add(FDreamMazeGen::DefaultParamsForCup(0));
	MazeParamsPerCup.Add(FDreamMazeGen::DefaultParamsForCup(1));
	MazeParamsPerCup.Add(FDreamMazeGen::DefaultParamsForCup(2));
}

FString ANiceInkGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId,
	const FString& Options, const FString& Portal)
{
	// 引擎在 Super 裡消化 ?Name=（客戶端旅行 URL 帶來的玩家名）
	const FString Error = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);

	if (ANiceInkPlayerState* PS = NewPlayerController ? NewPlayerController->GetPlayerState<ANiceInkPlayerState>() : nullptr)
	{
		if (UGameplayStatics::HasOption(Options, TEXT("Avatar")))
		{
			PS->DesiredAvatarIndex = UGameplayStatics::GetIntOption(Options, TEXT("Avatar"), INDEX_NONE);
		}
	}
	return Error;
}

void ANiceInkGameMode::PostLogin(APlayerController* NewPlayer)
{
	// 房間碼上牆：建房時 SessionSubsystem 存進 GameInstance，這裡轉進 GameState
	// 複製全員（大廳顯示給朋友唸）。無 session 流程（PIE/robo/直連）＝空＝不顯示。
	if (ANiceInkGameState* GS = GetGameState<ANiceInkGameState>())
	{
		if (GS->RoomCode.IsEmpty())
		{
			if (const UNiceInkGameInstance* GI = Cast<UNiceInkGameInstance>(GetGameInstance()))
			{
				GS->RoomCode = GI->HostRoomCode;
				// 房間人數同批播種（2026-08-14 定案：房主直接決定這房幾個人）：
				// 建房頁選的值經 GameInstance 轉進 GameState 複製全員；直連/PIE＝6
				GS->MaxPlayers = FMath::Clamp(GI->HostMaxPlayers, 4, 6);
			}
		}
	}

	// 席位＝入場順序；avatar 先看玩家意向、被佔用則輪派。要在 Super 之前指定，
	// SpawnDefaultPawnFor 讀 SeatIndex 決定出生位置。
	if (ANiceInkPlayerState* PS = NewPlayer ? NewPlayer->GetPlayerState<ANiceInkPlayerState>() : nullptr)
	{
		if (PS->SeatIndex == INDEX_NONE)
		{
			PS->SeatIndex = NextSeatIndex++;

			// 房主標示（大廳名冊＋ESC 踢人 UI 的依據）：listen server 本人
			if (NewPlayer->IsLocalController())
			{
				PS->bIsRoomHost = true;
			}

			// listen 主機本人不經 ?Name=（沒有重登入）：從 GameInstance 讀主選單設定。
			// 只在 standalone/packaged（Game world）生效——PIE 維持引擎派名，robo 不受擾。
			if (NewPlayer->IsLocalController() && GetWorld() && GetWorld()->WorldType == EWorldType::Game)
			{
				if (UNiceInkGameInstance* GI = Cast<UNiceInkGameInstance>(GetGameInstance()))
				{
					// 有效名＝自訂 > 平台 > session 保底（2026-08-10 平台名優先制）
					const FString Wanted = UNiceInkGameInstance::SanitizePlayerName(GI->GetEffectiveDisplayName());
					if (!Wanted.IsEmpty())
					{
						ChangeName(NewPlayer, Wanted, false);
					}
					PS->DesiredAvatarIndex = GI->PreferredAvatar;
				}
			}

			PS->AvatarIndex = PickAvatarFor(PS);
		}
	}

	Super::PostLogin(NewPlayer);

	// 跨場資產還原（延遲讓新客戶端的 actor channel 就緒，multicast 才到得了它）。
	// B3 起＝編排制：LAN/PIE 首 tick 即走本機槽（時序與舊制同＝+2s）；
	// EOS 玩家等雲端（主機本人）或上行列車（遠端），逾時 fallback 本機槽。
	if (APawn* Pawn = NewPlayer ? NewPlayer->GetPawn() : nullptr)
	{
		TWeakObjectPtr<ANiceInkCharacter> WeakChar = Cast<ANiceInkCharacter>(Pawn);
		FTimerHandle Unused;
		GetWorldTimerManager().SetTimer(Unused, FTimerDelegate::CreateWeakLambda(this, [this, WeakChar]()
		{
			TryRestoreTick(WeakChar, 12);
		}), 2.0f, false);
	}

	MaybeScheduleAutoStart();
}

void ANiceInkGameMode::PreLogin(const FString& Options, const FString& Address,
	const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	if (!ErrorMessage.IsEmpty())
	{
		return;
	}

	// 被踢名單（2026-08-13 踢人制）：本場拒再入——踢出後拿房號重連直接擋門
	if (UniqueId.IsValid() && KickedNetIds.Contains(UniqueId->ToString()))
	{
		ErrorMessage = TEXT("you were removed from this room");
		return;
	}

	// 開賽中不收新客（session 層 bAllowJoinInProgress=false 已擋；
	// 這裡防直連 IP 繞過 session 的路徑）
	const ANiceInkGameState* GS = NIState();
	if (GS && GS->CurrentPhase != ENiceInkPhase::Lobby && GS->CurrentPhase != ENiceInkPhase::PostGame)
	{
		ErrorMessage = TEXT("match in progress");
		return;
	}
	// 房間人數＝房主建房時選的值（坐滿關門；直連/PIE 預設 6）
	if (GS && GS->PlayerArray.Num() >= FMath::Clamp(GS->MaxPlayers, 4, 6))
	{
		ErrorMessage = TEXT("room is full");
		return;
	}

	// 同機 loopback 保底路（-nilanloopback 直連）帶碼＝必須驗房號：
	// 誤碼不得靜默連進本機恰好開著的別的房。無 NiCode 的直連（play_ingame/
	// robo）不受此檢查影響。
	if (UGameplayStatics::HasOption(Options, TEXT("NiCode")))
	{
		const FString Wanted = UGameplayStatics::ParseOption(Options, TEXT("NiCode"));
		const FString Have = GS ? GS->RoomCode : FString();
		if (!Wanted.Equals(Have, ESearchCase::IgnoreCase))
		{
			ErrorMessage = TEXT("no room with that code on this host");
		}
	}
}

void ANiceInkGameMode::SetSessionInProgress(bool bInProgress)
{
	if (bSessionInProgress == bInProgress)
	{
		return; // 冪等（回大廳的多個路徑都會呼叫）
	}
	bSessionInProgress = bInProgress;

	IOnlineSessionPtr Sessions = Online::GetSessionInterface(GetWorld());
	if (!Sessions.IsValid() || !Sessions->GetNamedSession(NAME_GameSession))
	{
		return; // 無 session 流程（PIE/robo/直連）＝無事可做
	}
	// 遠端 client 的本地 session 記錄同步（引擎 AGameSession 同款通知）
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (PC && !PC->IsLocalController())
		{
			if (bInProgress) { PC->ClientStartOnlineSession(); }
			else { PC->ClientEndOnlineSession(); }
		}
	}
	if (bInProgress)
	{
		Sessions->StartSession(NAME_GameSession);
	}
	else
	{
		Sessions->EndSession(NAME_GameSession);
	}
	UE_LOG(LogTemp, Log, TEXT("NiSession: match session -> %s"),
		bInProgress ? TEXT("InProgress") : TEXT("Ended (joinable)"));
}

void ANiceInkGameMode::HostKickPlayer(ANiceInkPlayerState* PS)
{
	APlayerController* PC = PS ? Cast<APlayerController>(PS->GetOwner()) : nullptr;
	if (!PC || PC->IsLocalController())
	{
		return; // 主機本人不可踢（也擋 null）
	}
	// 記入本場拒再入名單（LAN NULL id 可能無效＝只斷線不記名，重連可回——
	// 正式 EOS 路 id 恆有效；記帳於 header）
	if (PS->GetUniqueId().IsValid())
	{
		KickedNetIds.Add(PS->GetUniqueId()->ToString());
	}
	UE_LOG(LogTemp, Log, TEXT("NiKick: host kicked %s (seat %d)"), *PS->GetPlayerName(), PS->SeatIndex);
	if (GameSession)
	{
		// 引擎標準踢流程：ClientWasKicked＋關連線→客戶端走 NetworkFailure 回選單；
		// 我方 Logout 既有離場處理（持久化/AbortRound 防護）原樣接手
		GameSession->KickPlayer(PC, FText::FromString(TEXT("removed by the host")));
	}
}

void ANiceInkGameMode::Logout(AController* Exiting)
{
	const ANiceInkPlayerState* PS = Exiting ? Exiting->GetPlayerState<ANiceInkPlayerState>() : nullptr;
	const int32 LeavingId = PS ? PS->GetPlayerId() : INDEX_NONE;

	// 恩怨博物館不因斷線蒸發：離場前持久化（麥克筆本來就不入檔）
	if (ANiceInkCharacter* Char = Exiting ? Cast<ANiceInkCharacter>(Exiting->GetPawn()) : nullptr)
	{
		PersistCharacter(Char);
	}

	Super::Logout(Exiting);

	ANiceInkGameState* GS = NIState();
	if (!GS || LeavingId == INDEX_NONE)
	{
		return;
	}

	// 剩餘人數（PlayerArray 移除時序不可靠，顯式排除離開者）
	int32 Remaining = 0;
	for (const APlayerState* Other : GS->PlayerArray)
	{
		if (Other && Other->GetPlayerId() != LeavingId)
		{
			++Remaining;
		}
	}

	const ENiceInkPhase Phase = GS->CurrentPhase;
	const bool bInRound = Phase == ENiceInkPhase::Seating || Phase == ENiceInkPhase::Drawing ||
		Phase == ENiceInkPhase::Tour || Phase == ENiceInkPhase::Accusation;
	// Resolution/Finale 有計時器自走且對缺席者 null-safe（EnterSeating 會重轉）；
	// 卡死風險只在無計時器的 Drawing/Accusation 與受害者鏈上——一律作廢本回合。
	if (bInRound && (Remaining < 2 || GS->VictimPlayerId == LeavingId))
	{
		UE_LOG(LogTemp, Warning, TEXT("Logout: player %d left mid-round (victim=%d, remaining=%d) — aborting round"),
			LeavingId, GS->VictimPlayerId, Remaining);
		AbortRound(Remaining >= 2);
	}
}

void ANiceInkGameMode::AbortRound(bool bEnoughPlayers)
{
	ANiceInkGameState* GS = NIState();
	if (!GS)
	{
		return;
	}

	SetPhaseTimer(0.0f, nullptr);
	GetWorldTimerManager().ClearTimer(DialFailsafeHandle);
	PendingDialKillerId = INDEX_NONE;
	PendingDialVictim = nullptr;
	ForceExitAllLeans();
	ClearFlipProposal();
	RoundCleanupAllCharacters();

	// 殘留的沉睡者拉起來（受害者中離時不會有；防禦寫法）
	for (TActorIterator<ANiceInkCharacter> It(GetWorld()); It; ++It)
	{
		if (It->bAsleep)
		{
			It->ServerSetAsleep(false, FTransform::Identity);
		}
	}

	GS->VictimPlayerId = INDEX_NONE;
	GS->TourWorkId = INDEX_NONE;
	GS->TourWorkIdList.Reset();
	GS->TourWorkCount = 0;
	GS->ResolutionWorkId = INDEX_NONE;

	if (bEnoughPlayers)
	{
		GS->CurrentRound++;
		EnterBottleSpin(); // 人夠：重新轉瓶續攤
	}
	else
	{
		GS->SetPhase(ENiceInkPhase::Lobby, 0.0f); // 人不夠：回大廳等人
		SetSessionInProgress(false); // 回大廳＝重新可搜可加入
	}
}

// --- 跨場持久化 ---

FString ANiceInkGameMode::SaveSlotFor(const ANiceInkPlayerState* PS) const
{
	if (!PS)
	{
		return TEXT("NiceInk_Unknown");
	}

	// B3：EOS 玩家＝ProductUserId 鍵（跨房/改名/席位恆定；Steam 票證登入
	// 同為 Connect 層 PUID＝同一條路）。此槽是主機側熱備——雲端才是正本。
	const FString Puid = UNiceInkPersonaSubsystem::PuidFromNetIdString(PS->GetUniqueId().ToString());
	if (!Puid.IsEmpty())
	{
		return FString::Printf(TEXT("NiceInk_P_%s"), *Puid);
	}

	// 舊制 fallback（LAN/PIE/robo）：PIE 的玩家名帶隨機尾碼（Willie_desktop-7461A）
	// ——剝掉，改用席位穩定鍵
	FString Name = PS->GetPlayerName();
	int32 DashIdx;
	if (Name.FindLastChar(TEXT('-'), DashIdx) && Name.Len() - DashIdx == 6)
	{
		Name = Name.Left(DashIdx);
	}
	Name = Name.Replace(TEXT(" "), TEXT("_"));
	return FString::Printf(TEXT("NiceInk_%s_Seat%d"), *Name, PS->SeatIndex);
}

void ANiceInkGameMode::PersistCharacter(ANiceInkCharacter* Character)
{
	ANiceInkPlayerState* PS = Character ? Character->GetPlayerState<ANiceInkPlayerState>() : nullptr;
	if (!PS || !Character->InkCanvas)
	{
		return;
	}

	UNiceInkSaveGame* Save = Cast<UNiceInkSaveGame>(UGameplayStatics::CreateSaveGameObject(UNiceInkSaveGame::StaticClass()));
	Save->Cash = PS->Cash;
	for (const FInkWork& Work : Character->InkCanvas->GetWorks())
	{
		if (Work.State != EInkWorkState::Marker)
		{
			Save->Tattoos.Add(Work); // 麥克筆永不跨場
		}
	}
	UGameplayStatics::SaveGameToSlot(Save, SaveSlotFor(PS), 0);

	// B3 雲端下行：EOS 玩家把最新資產送回本人寫自己的雲端保險箱
	//（PlayerDataStorage 私人不可代寫＝必經本人；LAN/PIE 無 PUID＝跳過）。
	// 遠端斷線瞬間的 Client RPC 送不到＝無妨，上一個結算點已寫過雲端。
	const FString Puid = UNiceInkPersonaSubsystem::PuidFromNetIdString(PS->GetUniqueId().ToString());
	if (!Puid.IsEmpty() && UNiceInkSessionSubsystem::IsOnlineServiceConfigured())
	{
		TArray<uint8> Bytes;
		if (UGameplayStatics::SaveGameToMemory(Save, Bytes) && Bytes.Num() > 0)
		{
			const APlayerController* PC = Cast<APlayerController>(Character->GetController());
			if (PC && PC->IsLocalController())
			{
				// listen 主機本人：不過網，直寫雲端
				if (UNiceInkPersonaSubsystem* Persona = UNiceInkPersonaSubsystem::Get(this))
				{
					Persona->StoreAssets(Bytes);
				}
			}
			else
			{
				Character->SendPersonaToOwner(Bytes);
			}
		}
	}
}

void ANiceInkGameMode::RestoreCharacter(ANiceInkCharacter* Character)
{
	ANiceInkPlayerState* PS = Character ? Character->GetPlayerState<ANiceInkPlayerState>() : nullptr;
	if (!PS || PS->bAssetsRestored)
	{
		return;
	}
	PS->bAssetsRestored = true; // 嘗試過即封口（含「無存檔＝乾淨新身」）——防雙重還原

	const FString Slot = SaveSlotFor(PS);
	if (!UGameplayStatics::DoesSaveGameExist(Slot, 0))
	{
		return;
	}
	const UNiceInkSaveGame* Save = Cast<UNiceInkSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
	if (!Save)
	{
		return;
	}

	PS->Cash = Save->Cash;
	for (const FInkWork& Work : Save->Tattoos)
	{
		Character->MulticastRestoreWork(Work); // 恩怨博物館：刺青跟著角色走
	}
}

// --- 自訂臉房內分發（2026-08-10）---

void ANiceInkGameMode::OnFaceBlobReceived(ANiceInkCharacter* From, const TArray<uint8>& Blob)
{
	const ANiceInkPlayerState* PS = From ? From->GetPlayerState<ANiceInkPlayerState>() : nullptr;
	if (!PS || PS->SeatIndex < 0 || Blob.Num() <= 0)
	{
		return;
	}
	const int32 Seat = PS->SeatIndex;
	TSharedPtr<TArray<uint8>> Shared = MakeShared<TArray<uint8>>(Blob);
	FaceBlobs.Add(Seat, Shared);

	// 主機自己也是 viewer：直接入本機登記簿（不走 RPC）
	if (UNiceInkFaceShare* Share = UNiceInkFaceShare::Get(this))
	{
		Share->StoreBlob(Seat, Blob);
	}

	// 廣播給已報到的遠端 viewer（本人席位跳過——上傳前已入自己的簿）
	for (const TWeakObjectPtr<ANiceInkCharacter>& V : FaceViewers)
	{
		ANiceInkCharacter* C = V.Get();
		const ANiceInkPlayerState* VPS = C ? C->GetPlayerState<ANiceInkPlayerState>() : nullptr;
		if (C && (!VPS || VPS->SeatIndex != Seat))
		{
			EnqueueFaceJob(C, Seat, Shared);
		}
	}
	UE_LOG(LogTemp, Log, TEXT("NiFaceShare: host received face for seat %d (%d bytes, %d viewers)"),
		Seat, Blob.Num(), FaceViewers.Num());
}

void ANiceInkGameMode::RegisterFaceViewer(ANiceInkCharacter* Viewer)
{
	if (!Viewer)
	{
		return;
	}
	FaceViewers.AddUnique(Viewer);
	const ANiceInkPlayerState* VPS = Viewer->GetPlayerState<ANiceInkPlayerState>();
	const int32 OwnSeat = VPS ? VPS->SeatIndex : INDEX_NONE;
	for (const TPair<int32, TSharedPtr<TArray<uint8>>>& Pair : FaceBlobs)
	{
		if (Pair.Key != OwnSeat)
		{
			EnqueueFaceJob(Viewer, Pair.Key, Pair.Value);
		}
	}
}

void ANiceInkGameMode::EnqueueFaceJob(ANiceInkCharacter* Target, int32 Seat,
	const TSharedPtr<TArray<uint8>>& Blob)
{
	if (!Target || Seat < 0 || !Blob.IsValid() || Blob->Num() <= 0)
	{
		return;
	}
	FNiFaceSendJob& Job = FaceSendQueue.AddDefaulted_GetRef();
	Job.Target = Target;
	Job.Seat = Seat;
	Job.Blob = Blob;
	if (!GetWorldTimerManager().IsTimerActive(FaceSendTimer))
	{
		GetWorldTimerManager().SetTimer(FaceSendTimer, this,
			&ANiceInkGameMode::TickFaceSend, 0.1f, /*bLoop=*/true);
	}
}

void ANiceInkGameMode::TickFaceSend()
{
	constexpr int32 ChunkSize = 16 * 1024;
	int32 Budget = 8; // 8×16KB / 0.1s ≈ 1.3MB/s——與上行同節奏，防 reliable 緩衝溢位
	while (FaceSendQueue.Num() > 0 && Budget > 0)
	{
		FNiFaceSendJob& Job = FaceSendQueue[0];
		ANiceInkCharacter* C = Job.Target.Get();
		if (!C || !Job.Blob.IsValid())
		{
			FaceSendQueue.RemoveAt(0); // 收件者離場：job 作廢
			continue;
		}
		const TArray<uint8>& B = *Job.Blob;
		if (!Job.bBegun)
		{
			C->ClientFaceBegin(Job.Seat, B.Num());
			Job.bBegun = true;
		}
		while (Budget > 0 && Job.NextOff < B.Num())
		{
			TArray<uint8> Chunk(B.GetData() + Job.NextOff, FMath::Min(ChunkSize, B.Num() - Job.NextOff));
			C->ClientFaceChunk(Job.Seat, Job.NextOff, Chunk);
			Job.NextOff += Chunk.Num();
			--Budget;
		}
		if (Job.NextOff >= B.Num())
		{
			C->ClientFaceEnd(Job.Seat, FCrc::MemCrc32(B.GetData(), B.Num()));
			FaceSendQueue.RemoveAt(0);
		}
	}
	if (FaceSendQueue.Num() == 0)
	{
		GetWorldTimerManager().ClearTimer(FaceSendTimer);
	}
}

void ANiceInkGameMode::ApplyUploadedPersona(ANiceInkCharacter* Character, const TArray<uint8>& Bytes)
{
	ANiceInkPlayerState* PS = Character ? Character->GetPlayerState<ANiceInkPlayerState>() : nullptr;
	if (!PS || PS->bAssetsRestored)
	{
		return;
	}

	UNiceInkSaveGame* Save = Cast<UNiceInkSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
	if (!Save)
	{
		UE_LOG(LogTemp, Warning, TEXT("NiPersona: uploaded persona failed to deserialize — ignoring"));
		return;
	}
	PS->bAssetsRestored = true;

	// 信任界線（listen server 派對遊戲＝無絕對防竄改，記帳接受）：
	// 只做格式與量級的理智檢查，語意照單全收
	PS->Cash = FMath::Clamp(Save->Cash, 0, 100000000);
	int32 Applied = 0;
	for (const FInkWork& Work : Save->Tattoos)
	{
		if (Work.State == EInkWorkState::Marker || Applied >= 1024)
		{
			continue; // 麥克筆永不跨場；1024 幅＝瘋值上限
		}
		Character->MulticastRestoreWork(Work);
		++Applied;
	}

	// 主機本機槽同步熱備（該玩家下次在斷網/雲端故障時仍有得撈）
	UGameplayStatics::SaveGameToSlot(Save, SaveSlotFor(PS), 0);
	UE_LOG(LogTemp, Log, TEXT("NiPersona: applied uploaded persona for %s (cash=%d, tattoos=%d)"),
		*PS->GetPlayerName(), PS->Cash, Applied);
}

void ANiceInkGameMode::TryRestoreTick(TWeakObjectPtr<ANiceInkCharacter> WeakChar, int32 TicksLeft)
{
	ANiceInkCharacter* Character = WeakChar.Get();
	ANiceInkPlayerState* PS = Character ? Character->GetPlayerState<ANiceInkPlayerState>() : nullptr;
	if (!Character || !PS || PS->bAssetsRestored)
	{
		return; // 離場／已還原（上行列車先到）＝收工
	}

	const FString Puid = UNiceInkPersonaSubsystem::PuidFromNetIdString(PS->GetUniqueId().ToString());
	if (Puid.IsEmpty() || !UNiceInkSessionSubsystem::IsOnlineServiceConfigured())
	{
		RestoreCharacter(Character); // LAN/PIE/robo：原路本機槽，時序與舊制同
		return;
	}

	const APlayerController* PC = Cast<APlayerController>(Character->GetController());
	if (PC && PC->IsLocalController())
	{
		// listen 主機本人：資產不過網，直讀本機雲端快取
		if (UNiceInkPersonaSubsystem* Persona = UNiceInkPersonaSubsystem::Get(this))
		{
			if (Persona->GetAssetsPullState() == UNiceInkPersonaSubsystem::EPullState::Done)
			{
				if (Persona->HasCloudAssets())
				{
					ApplyUploadedPersona(Character, Persona->GetCachedAssets());
				}
				else
				{
					RestoreCharacter(Character); // 雲端無檔：本機 PUID 槽 fallback
				}
				return;
			}
		}
		else
		{
			RestoreCharacter(Character);
			return;
		}
	}
	// EOS 遠端：等 Character 的上行列車（ServerPersonaEnd → ApplyUploadedPersona）

	if (TicksLeft <= 0)
	{
		UE_LOG(LogTemp, Log, TEXT("NiPersona: no cloud persona for %s within window — host-local fallback"),
			*PS->GetPlayerName());
		RestoreCharacter(Character); // 逾時：主機本機 PUID 槽（同機重連有得撈；多半＝乾淨新身）
		return;
	}
	FTimerHandle Unused;
	GetWorldTimerManager().SetTimer(Unused, FTimerDelegate::CreateWeakLambda(this, [this, WeakChar, TicksLeft]()
	{
		TryRestoreTick(WeakChar, TicksLeft - 1);
	}), 1.0f, false);
}

void ANiceInkGameMode::PersistAllCharacters()
{
	for (TActorIterator<ANiceInkCharacter> It(GetWorld()); It; ++It)
	{
		PersistCharacter(*It);
	}
}

int32 ANiceInkGameMode::PickAvatarFor(const ANiceInkPlayerState* PS) const
{
	const int32 N = FNiceInkAvatars::Num();
	if (!PS || N <= 0)
	{
		return 0;
	}

	TSet<int32> Taken;
	if (GameState)
	{
		for (APlayerState* Other : GameState->PlayerArray)
		{
			const ANiceInkPlayerState* O = Cast<ANiceInkPlayerState>(Other);
			if (O && O != PS && O->SeatIndex != INDEX_NONE)
			{
				Taken.Add(O->AvatarIndex);
			}
		}
	}

	if (PS->DesiredAvatarIndex >= 0 && PS->DesiredAvatarIndex < N && !Taken.Contains(PS->DesiredAvatarIndex))
	{
		return PS->DesiredAvatarIndex;
	}
	for (int32 k = 0; k < N; ++k)
	{
		const int32 Candidate = (PS->SeatIndex + k) % N;
		if (!Taken.Contains(Candidate))
		{
			return Candidate;
		}
	}
	return PS->SeatIndex % N; // 七人以上理論值：名冊耗盡時允許重臉
}

APawn* ANiceInkGameMode::SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot)
{
	const ANiceInkPlayerState* PS = NewPlayer ? NewPlayer->GetPlayerState<ANiceInkPlayerState>() : nullptr;
	const int32 Seat = PS ? PS->SeatIndex : 0;
	return SpawnDefaultPawnAtTransform(NewPlayer, GetSeatTransform(FMath::Max(0, Seat)));
}

// --- 場地 ---

float ANiceInkGameMode::ProbeFloorZ(const FVector& At) const
{
	if (UWorld* World = GetWorld())
	{
		FHitResult Hit;
		// 起點要在室內（桑拿房高 3m）——從 +500 起測會打到屋頂外側，人全站上屋頂
		const FVector Start = At + FVector(0, 0, 150.0f);
		const FVector End = At - FVector(0, 0, 1000.0f);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(NiceInkFloorProbe), true);
		if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
		{
			return Hit.ImpactPoint.Z;
		}
	}
	return 0.0f;
}

FTransform ANiceInkGameMode::GetSeatTransform(int32 SeatIndex) const
{
	const FVector2D Spot = SeatSpots.Num() > 0 ? SeatSpots[SeatIndex % SeatSpots.Num()] : FVector2D::ZeroVector;
	FVector Location(Spot.X, Spot.Y, 0.0f);
	Location.Z = ProbeFloorZ(Location) + 94.0f;

	// 面向躺位（房間的舞台中心）
	const FVector Focus(VictimLieSpot.X, VictimLieSpot.Y, Location.Z);
	const FVector ToFocus = (Focus - Location).GetSafeNormal2D();
	return FTransform(ToFocus.Rotation(), Location);
}

FTransform ANiceInkGameMode::GetVictimLieTransform() const
{
	FVector Location(VictimLieSpot.X, VictimLieSpot.Y, 0.0f);
	Location.Z = ProbeFloorZ(Location) + 94.0f;
	return FTransform(FRotator(0.0f, VictimLieYaw, 0.0f), Location);
}

// --- 流程 ---

ANiceInkGameState* ANiceInkGameMode::NIState() const
{
	return GetGameState<ANiceInkGameState>();
}

ANiceInkCharacter* ANiceInkGameMode::GetVictimCharacter() const
{
	const ANiceInkGameState* GS = NIState();
	return GS ? ANiceInkCharacter::FindByPlayerId(GetWorld(), GS->VictimPlayerId) : nullptr;
}

ANiceInkPlayerState* ANiceInkGameMode::FindNIPlayerState(int32 PlayerId) const
{
	const ANiceInkGameState* GS = NIState();
	return GS ? Cast<ANiceInkPlayerState>(GS->FindPlayerStateById(PlayerId)) : nullptr;
}

void ANiceInkGameMode::SetPhaseTimer(float Seconds, void (ANiceInkGameMode::*Handler)())
{
	GetWorldTimerManager().ClearTimer(PhaseTimerHandle);
	if (Seconds > 0.0f && Handler)
	{
		GetWorldTimerManager().SetTimer(PhaseTimerHandle, this, Handler, Seconds, false);
	}
}

void ANiceInkGameMode::MaybeScheduleAutoStart()
{
	// 自動開局只服務 PIE（robo 測試流程靠它）；正式流程＝大廳主機手動開始
	//（HUD start 按鈕／NiStart）——派對房不該在湊滿兩人時把後到的朋友關在門外。
	const bool bPieWorld = GetWorld() && GetWorld()->WorldType == EWorldType::PIE;
	ANiceInkGameState* GS = NIState();
	if (!bAutoStart || !bPieWorld || !GS || GS->CurrentPhase != ENiceInkPhase::Lobby)
	{
		return;
	}
	if (GS->PlayerArray.Num() >= MinPlayersToStart && !GetWorldTimerManager().IsTimerActive(AutoStartTimerHandle))
	{
		GetWorldTimerManager().SetTimer(AutoStartTimerHandle, this, &ANiceInkGameMode::RequestStartMatch, AutoStartDelay, false);
	}
}

void ANiceInkGameMode::RequestStartMatch()
{
	// 開局門檻＝遊戲規則 4 人（設計人數 4~6；「下限」不是房間設定、藏在開始
	// 鈕裡——2026-08-14 房間人數制）。PIE 維持 2＝robo 少人探針不受擾
	const int32 MinStart = (GetWorld() && GetWorld()->WorldType == EWorldType::PIE) ? 2 : 4;
	ANiceInkGameState* GS = NIState();
	if (!GS || GS->PlayerArray.Num() < MinStart)
	{
		return;
	}
	// Lobby 開場；PostGame＝場間大廳再開一場（刺青與錢包跟著走）
	if (GS->CurrentPhase != ENiceInkPhase::Lobby && GS->CurrentPhase != ENiceInkPhase::PostGame)
	{
		return;
	}

	GS->CurrentRound = 0;
	GS->VictimPlayerId = INDEX_NONE;
	GS->LoserPlayerId = INDEX_NONE;
	GS->LastAccusationResult = ENiceInkAccusationResult::None;
	GS->RevealedAuthorId = INDEX_NONE;
	for (APlayerState* PS : GS->PlayerArray)
	{
		if (ANiceInkPlayerState* NIPS = Cast<ANiceInkPlayerState>(PS))
		{
			NIPS->PenaltyCups = 0;
		}
	}

	SetSessionInProgress(true); // 真開局＝session 才進 InProgress（擋中途加入）
	EnterBottleSpin();
}

void ANiceInkGameMode::HandleLaserRequest(ANiceInkCharacter* Requester)
{
	ANiceInkGameState* GS = NIState();
	ANiceInkPlayerState* PS = Requester ? Requester->GetPlayerState<ANiceInkPlayerState>() : nullptr;
	if (!GS || GS->CurrentPhase != ENiceInkPhase::PostGame || !PS || !Requester->InkCanvas)
	{
		return;
	}
	if (PS->Cash < LaserCostPerPass)
	{
		return; // 沒錢雷射＝皮膚負債帶進下一場（SPEC 經濟）
	}

	const TArray<int32> CarbonWorks = Requester->InkCanvas->GetWorkIdsByState(EInkWorkState::Carbon);
	if (CarbonWorks.IsEmpty())
	{
		return; // 永久刺青無法雷射
	}

	PS->Cash -= LaserCostPerPass;
	Requester->MulticastApplyLaser(CarbonWorks[0]);
	PersistCharacter(Requester);
}

void ANiceInkGameMode::EnterBottleSpin()
{
	ANiceInkGameState* GS = NIState();
	GS->SetPhase(ENiceInkPhase::BottleSpin, BottleSpinSeconds);
	SetPhaseTimer(BottleSpinSeconds, &ANiceInkGameMode::OnBottleSpinDone);
}

void ANiceInkGameMode::OnBottleSpinDone()
{
	ANiceInkGameState* GS = NIState();
	if (GS->PlayerArray.Num() == 0)
	{
		GS->SetPhase(ENiceInkPhase::Lobby, 0.0f);
		SetSessionInProgress(false);
		return;
	}

	// robo 測試可強制指定席位
	if (DebugForcedVictimSeat >= 0)
	{
		for (APlayerState* PS : GS->PlayerArray)
		{
			const ANiceInkPlayerState* NIPS = Cast<ANiceInkPlayerState>(PS);
			if (NIPS && NIPS->SeatIndex == DebugForcedVictimSeat)
			{
				EnterSeating(NIPS->GetPlayerId());
				return;
			}
		}
	}

	// 轉酒瓶：純儀式，只在開場使用；此後受害者一律由猜對指認產生
	const int32 Pick = FMath::RandRange(0, GS->PlayerArray.Num() - 1);
	EnterSeating(GS->PlayerArray[Pick]->GetPlayerId());
}

void ANiceInkGameMode::EnterSeating(int32 VictimPlayerId)
{
	ANiceInkGameState* GS = NIState();

	// 受害者必須真實在場（斷線／異常 ID 防護）：找不到就隨機重選
	if (!ANiceInkCharacter::FindByPlayerId(GetWorld(), VictimPlayerId))
	{
		UE_LOG(LogTemp, Warning, TEXT("EnterSeating: victim %d missing, re-spinning"), VictimPlayerId);
		if (GS->PlayerArray.Num() == 0)
		{
			GS->SetPhase(ENiceInkPhase::Lobby, 0.0f);
			SetSessionInProgress(false);
			return;
		}
		VictimPlayerId = GS->PlayerArray[FMath::RandRange(0, GS->PlayerArray.Num() - 1)]->GetPlayerId();
	}

	GS->VictimPlayerId = VictimPlayerId;
	GS->LastAccusationResult = ENiceInkAccusationResult::None;
	GS->RevealedAuthorId = INDEX_NONE;
	GS->TourWorkId = INDEX_NONE;
	GS->TourWorkNumber = 0;
	GS->TourWorkCount = 0;
	GS->TourWorkIdList.Reset();
	GS->ResolutionWorkId = INDEX_NONE;

	ForceExitAllLeans();
	ClearFlipProposal(); // 翻身提案不跨回合

	// 掛著的轉盤跨回合作廢（防禦：正常流程死亡序列內不會切相位）
	GetWorldTimerManager().ClearTimer(DialFailsafeHandle);
	PendingDialKillerId = INDEX_NONE;
	PendingDialVictim = nullptr;

	// 搖晃攻擊冷卻不跨回合
	LastShakeTimeByPlayer.Reset();

	if (ANiceInkCharacter* Victim = GetVictimCharacter())
	{
		Victim->MulticastSetRoundIndex(GS->CurrentRound);
		Victim->ServerSetAsleep(true, GetVictimLieTransform());

		// 醉夢描圖（v4.0 定案 #49；迷宮退役）：難度檔＝罰酒杯數（酒越深夢越深＝
		// 時間更長帶更窄、圖案池更複雜）；種子每回合新開。只發受害者。
		const ANiceInkPlayerState* VictimPS = FindNIPlayerState(VictimPlayerId);
		const int32 Cups = VictimPS ? VictimPS->PenaltyCups : 0;
		FDreamTraceParams TraceParams = TraceParamsPerCup.Num() > 0
			? TraceParamsPerCup[FMath::Clamp(Cups, 0, TraceParamsPerCup.Num() - 1)]
			: FDreamTraceGen::DefaultParamsForCup(Cups);

		// user 定案設計程序（08-02 二段）：先定「不受干擾平均完成時間」→依針速
		// 導出線長。發夢當下用受害者實際 v_max 換算＝改割線速度旋鈕時夢自動跟
		if (TraceParams.TargetTraceSeconds > 0.0f)
		{
			TraceParams.PerimeterCm = TraceParams.TargetTraceSeconds * Victim->TattooMaxSpeedCmPerSec();
		}
		// 帶全寬＝筆寬×倍數（user 定案 08-02：2.0/1.8/1.6 隨杯數）——筆寬旋鈕改動夢自動跟
		TraceParams.BandHalfWidthCm = Victim->TattooNibDiameterCm * TraceParams.BandWidthNibMult * 0.5f;

		const int32 TraceSeed = DebugForcedTraceSeed > 0
			? DebugForcedTraceSeed
			: FMath::RandRange(1, MAX_int32 - 1);
		Victim->ClientStartTrace(TraceSeed, TraceParams);
	}

	GS->SetPhase(ENiceInkPhase::Seating, SeatingSeconds);
	SetPhaseTimer(SeatingSeconds, &ANiceInkGameMode::OnSeatingDone);
}

void ANiceInkGameMode::OnSeatingDone()
{
	// 作畫階段：無計時器——收束時機在受害者手上（WASD 現身）
	NIState()->SetPhase(ENiceInkPhase::Drawing, 0.0f);
	SetPhaseTimer(0.0f, nullptr);
}

void ANiceInkGameMode::HandleEmergeRequest(ANiceInkCharacter* Requester, bool bForce)
{
	ANiceInkGameState* GS = NIState();
	if (!GS || GS->CurrentPhase != ENiceInkPhase::Drawing || !Requester)
	{
		return;
	}

	const APlayerState* PS = Requester->GetPlayerState();
	if (!PS || PS->GetPlayerId() != GS->VictimPlayerId)
	{
		return;
	}

	// 現身的前提＝無聲甦醒（已走出迷宮出口睜眼）；robo 測試可強制
	if (!bForce && !Requester->bEyesOpen)
	{
		return;
	}

	Requester->ServerSetAsleep(false, FTransform::Identity);
	EnterTour();
}

void ANiceInkGameMode::EnterTour()
{
	ANiceInkGameState* GS = NIState();
	ANiceInkCharacter* Victim = GetVictimCharacter();

	ForceExitAllLeans(); // 他醒了——所有埋著的頭都得抬起來
	ClearFlipProposal(); // 作畫收束＝未決的翻身提案作廢

	// 打稿制（07-25）：甦醒收束＝稿線全洗——沒上墨的稿從未存在過；洗在收集
	// 傑作之前（稿線拔掉後空掉的作品不進巡禮=不會出現空白傑作）
	if (Victim)
	{
		Victim->MulticastWashStencil();
	}

	TourWorkIds.Reset();
	if (Victim && Victim->InkCanvas)
	{
		for (const FInkWork& Work : Victim->InkCanvas->GetWorks())
		{
			// AuthorId < 0 ＝證據標記（噴漬／瘀青）：全房可見但不是傑作，不進巡禮
			if (Work.State == EInkWorkState::Marker && Work.Strokes.Num() > 0 && Work.AuthorId >= 0)
			{
				TourWorkIds.Add(Work.WorkId);
			}
		}
		TourWorkIds.Sort();
	}

	// 沒有任何作品（沒人動筆）：跳過巡禮與指認，同一位受害者再睡一輪
	if (TourWorkIds.IsEmpty())
	{
		GS->CurrentRound++;
		EnterSeating(GS->VictimPlayerId);
		return;
	}

	TourCursor = 0;
	GS->TourWorkCount = TourWorkIds.Num();
	GS->TourWorkIdList = TourWorkIds;
	GS->SetPhase(ENiceInkPhase::Tour, TourSecondsPerWork * TourWorkIds.Num());
	AdvanceTour();
}

void ANiceInkGameMode::AdvanceTour()
{
	ANiceInkGameState* GS = NIState();
	if (TourCursor >= TourWorkIds.Num())
	{
		EnterAccusation();
		return;
	}

	GS->TourWorkId = TourWorkIds[TourCursor];
	GS->TourWorkNumber = TourCursor + 1;
	++TourCursor;
	SetPhaseTimer(TourSecondsPerWork, &ANiceInkGameMode::AdvanceTour);
}

void ANiceInkGameMode::EnterAccusation()
{
	ANiceInkGameState* GS = NIState();
	GS->TourWorkId = INDEX_NONE;
	GS->SetPhase(ENiceInkPhase::Accusation, 0.0f);
	SetPhaseTimer(0.0f, nullptr);
}

void ANiceInkGameMode::HandleAccusation(ANiceInkCharacter* Accuser, int32 WorkId, int32 AccusedPlayerId)
{
	ANiceInkGameState* GS = NIState();
	if (!GS || GS->CurrentPhase != ENiceInkPhase::Accusation || !Accuser)
	{
		return;
	}

	const APlayerState* AccuserPS = Accuser->GetPlayerState();
	if (!AccuserPS || AccuserPS->GetPlayerId() != GS->VictimPlayerId)
	{
		return; // 只有受害者能指認
	}
	if (!TourWorkIds.Contains(WorkId) || AccusedPlayerId == GS->VictimPlayerId)
	{
		return; // 只能指認巡禮過的傑作、不能指認自己
	}
	if (!FindNIPlayerState(AccusedPlayerId))
	{
		return; // 被指認者必須是在場玩家
	}

	ANiceInkCharacter* Victim = GetVictimCharacter();
	ANiceInkPlayerState* VictimPS = FindNIPlayerState(GS->VictimPlayerId);
	if (!Victim || !Victim->InkCanvas || !VictimPS)
	{
		return;
	}

	FInkWork PickedWork;
	if (!Victim->InkCanvas->GetWork(WorkId, PickedWork))
	{
		return;
	}

	const bool bCorrect = PickedWork.AuthorId == AccusedPlayerId;
	GS->RevealedAuthorId = PickedWork.AuthorId; // 猜對＝證實；猜錯＝真作者現身
	GS->ResolutionWorkId = WorkId;              // 鏡頭聚焦被選那幅
	bPendingFinale = false;

	if (bCorrect)
	{
		GS->LastAccusationResult = ENiceInkAccusationResult::Correct;
		VictimPS->PenaltyCups = 0; // 猜對離座，罰酒計數歸零
		PendingNextVictimId = AccusedPlayerId;
	}
	else
	{
		GS->LastAccusationResult = ENiceInkAccusationResult::Wrong;
		VictimPS->PenaltyCups++;
		PendingNextVictimId = GS->VictimPlayerId; // 繼續畫他
		bPendingFinale = VictimPS->PenaltyCups >= PenaltyCupsToFinale;
	}

	GS->SetPhase(ENiceInkPhase::Resolution, ResolutionSeconds);
	SetPhaseTimer(ResolutionSeconds, &ANiceInkGameMode::OnResolutionDone);

	// 上墨儀式：鏡頭就位後（+1.2s）當眾轉碳黑（猜錯）＋全場洗掉麥克筆與證據
	const bool bWrongGuess = !bCorrect;
	const int32 CeremonyWorkId = WorkId;
	FTimerHandle CeremonyHandle;
	GetWorldTimerManager().SetTimer(CeremonyHandle, FTimerDelegate::CreateWeakLambda(this,
		[this, bWrongGuess, CeremonyWorkId]()
	{
		if (ANiceInkCharacter* V = GetVictimCharacter())
		{
			if (bWrongGuess)
			{
				V->MulticastConvertWorkToCarbon(CeremonyWorkId);
			}
		}
		RoundCleanupAllCharacters();
		// B3：碳黑誕生＝資產即刻落盤＋上雲（不等終局——中途斷線/主機跑路不丟碳黑）
		if (bWrongGuess)
		{
			PersistCharacter(GetVictimCharacter());
		}
	}), FMath::Min(1.2f, ResolutionSeconds * 0.4f), false);
}

void ANiceInkGameMode::OnResolutionDone()
{
	ANiceInkGameState* GS = NIState();
	if (bPendingFinale)
	{
		EnterFinale();
		return;
	}

	GS->CurrentRound++;
	EnterSeating(PendingNextVictimId);
}

void ANiceInkGameMode::EnterFinale()
{
	ANiceInkGameState* GS = NIState();
	GS->LoserPlayerId = GS->VictimPlayerId;

	// 昏睡不醒
	if (ANiceInkCharacter* Loser = GetVictimCharacter())
	{
		Loser->ServerSetAsleep(true, GetVictimLieTransform());
	}

	// 瓜分：輸家的現金被其餘玩家平分
	if (ANiceInkPlayerState* LoserPS = FindNIPlayerState(GS->LoserPlayerId))
	{
		TArray<ANiceInkPlayerState*> Others;
		for (APlayerState* PS : GS->PlayerArray)
		{
			if (ANiceInkPlayerState* NIPS = Cast<ANiceInkPlayerState>(PS))
			{
				if (NIPS != LoserPS)
				{
					Others.Add(NIPS);
				}
			}
		}
		if (Others.Num() > 0)
		{
			const int32 Share = LoserPS->Cash / Others.Num();
			for (ANiceInkPlayerState* Other : Others)
			{
				Other->Cash += Share;
			}
			LoserPS->Cash = 0;
		}
	}

	// 羞辱時間：全員可在輸家身上塗鴉（CanPaintOn 開放）；計時結束收場
	GS->SetPhase(ENiceInkPhase::Finale, FinaleSeconds);
	SetPhaseTimer(FinaleSeconds, &ANiceInkGameMode::OnFinaleDone);
}

void ANiceInkGameMode::OnFinaleDone()
{
	ANiceInkGameState* GS = NIState();

	ForceExitAllLeans();

	if (ANiceInkCharacter* Loser = GetVictimCharacter())
	{
		if (Loser->InkCanvas)
		{
			// 鈦白鎖定：輸家身上所有碳黑（含歷史）鎖成永久。
			// M4 改為玩家手持鈦白刷親自執行；規則結果先行。
			for (const int32 CarbonId : Loser->InkCanvas->GetWorkIdsByState(EInkWorkState::Carbon))
			{
				Loser->MulticastLockWorkPermanent(CarbonId);
			}
		}
		// 場間大廳：輸家醒來（昏睡不醒只到終局結束）
		Loser->ServerSetAsleep(false, FTransform::Identity);
	}

	// 遊戲結束：所有麥克筆塗鴉（含羞辱塗鴉）與殘留證據洗掉
	RoundCleanupAllCharacters();

	// 跨場資產落盤（錢包＋碳黑／永久刺青）
	PersistAllCharacters();

	GS->SetPhase(ENiceInkPhase::PostGame, 0.0f);
	SetSessionInProgress(false); // 場間大廳＝重新可搜可加入
	SetPhaseTimer(0.0f, nullptr);
}

// --- Robo-test 鉤子 ---

void ANiceInkGameMode::DebugRoboStroke(FVector2D FromUV, FVector2D ToUV, int32 ColorIndex)
{
	FTimerHandle Unused;
	GetWorldTimerManager().SetTimer(Unused, FTimerDelegate::CreateWeakLambda(this, [this, FromUV, ToUV, ColorIndex]()
	{
		ANiceInkGameState* GS = NIState();
		ANiceInkCharacter* Victim = GetVictimCharacter();
		if (!GS || !Victim)
		{
			return;
		}
		ANiceInkCharacter* Artist = nullptr;
		for (APlayerState* PS : GS->PlayerArray)
		{
			if (PS && PS->GetPlayerId() != GS->VictimPlayerId)
			{
				Artist = ANiceInkCharacter::FindByPlayerId(GetWorld(), PS->GetPlayerId());
				break;
			}
		}
		if (!Artist)
		{
			return;
		}
		const FLinearColor Color = FNiceInkPalette::Get(ColorIndex);
		const int32 AuthorId = Artist->GetInkAuthorId();
		// robo 線畫維持折線語義（bDotStroke=false）＋液線針＋滿流量（空陣列）；
		// StrokeSeq=0＝server 發起、無任何端預畫＝回播對消永不觸發
		Victim->MulticastPaintBegin(AuthorId, Color, FromUV, /*bDotStroke=*/false,
			EInkNeedle::Liner, /*Flow=*/255, /*StrokeSeq=*/0);
		TArray<FVector2D> Points;
		for (int32 Step = 1; Step <= 10; ++Step)
		{
			Points.Add(FMath::Lerp(FromUV, ToUV, Step / 10.0f));
		}
		Victim->MulticastPaintPoints(AuthorId, Points, TArray<uint8>());
		Victim->MulticastPaintEnd(AuthorId);
	}), 0.1f, false);
}

void ANiceInkGameMode::DebugRoboEmerge()
{
	FTimerHandle Unused;
	GetWorldTimerManager().SetTimer(Unused, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		HandleEmergeRequest(GetVictimCharacter(), /*bForce=*/true);
	}), 0.1f, false);
}

void ANiceInkGameMode::DebugRoboAccuse(bool bCorrect)
{
	FTimerHandle Unused;
	GetWorldTimerManager().SetTimer(Unused, FTimerDelegate::CreateWeakLambda(this, [this, bCorrect]()
	{
		ANiceInkGameState* GS = NIState();
		ANiceInkCharacter* Victim = GetVictimCharacter();
		if (!GS || !Victim || !Victim->InkCanvas || TourWorkIds.IsEmpty())
		{
			return;
		}
		const int32 WorkId = TourWorkIds[0];
		FInkWork Work;
		if (!Victim->InkCanvas->GetWork(WorkId, Work))
		{
			return;
		}
		int32 AccusedId = Work.AuthorId;
		if (!bCorrect)
		{
			AccusedId = INDEX_NONE;
			for (APlayerState* PS : GS->PlayerArray)
			{
				const int32 Id = PS ? PS->GetPlayerId() : INDEX_NONE;
				if (Id != INDEX_NONE && Id != GS->VictimPlayerId && Id != Work.AuthorId)
				{
					AccusedId = Id;
					break;
				}
			}
			if (AccusedId == INDEX_NONE)
			{
				// 兩人房猜錯測試：沒有第三人可誣指——用不存在的 ID 會被駁回，
				// 所以指認真作者以外唯一的選擇是自己（會被駁回）；直接放棄。
				UE_LOG(LogTemp, Warning, TEXT("DebugRoboAccuse(wrong) needs a third player; skipped"));
				return;
			}
		}
		HandleAccusation(Victim, WorkId, AccusedId);
	}), 0.1f, false);
}

void ANiceInkGameMode::RoundCleanupAllCharacters()
{
	for (TActorIterator<ANiceInkCharacter> It(GetWorld()); It; ++It)
	{
		It->MulticastRoundCleanup();
	}
}

void ANiceInkGameMode::ForceExitAllLeans()
{
	for (TActorIterator<ANiceInkCharacter> It(GetWorld()); It; ++It)
	{
		It->ForceExitLean();
	}
}

void ANiceInkGameMode::DebugRoboSpray(float AimYawWorld, uint8 OriginType)
{
	FTimerHandle Unused;
	GetWorldTimerManager().SetTimer(Unused, FTimerDelegate::CreateWeakLambda(this, [this, AimYawWorld, OriginType]()
	{
		if (ANiceInkCharacter* Victim = GetVictimCharacter())
		{
			Victim->ServerSpray(static_cast<EInkEvidenceType>(OriginType), AimYawWorld);
		}
	}), 0.1f, false);
}

// --- 醉夢描圖：搖晃攻擊路由（v4.0 定案 #50；attacker↔server↔victim 三點、零第三方資訊） ---

void ANiceInkGameMode::HandleShakeAttack(ANiceInkCharacter* Attacker)
{
	const ANiceInkGameState* GS = NIState();
	ANiceInkPlayerState* PS = Attacker ? Attacker->GetPlayerState<ANiceInkPlayerState>() : nullptr;
	ANiceInkCharacter* Victim = GetVictimCharacter();
	if (!GS || !PS || !Victim || GS->CurrentPhase != ENiceInkPhase::Drawing ||
		PS->GetPlayerId() == GS->VictimPlayerId)
	{
		return; // 非法請求靜默丟棄（相位外/受害者自搖）
	}
	// 注意：受害者「已無聲睜眼」不拒收——拒收＝告訴攻擊者他醒了（有錢又過冷卻
	// 卻被拒＝唯一解釋），無聲甦醒零提示會被打穿。照收照扣、夢端自然無效
	//（描圖元件睜眼即停）＝砸空是攻擊者自擔的賭；bAsleep 為假只在相位錯亂時
	// 出現（Drawing 中受害者恆沉睡旗標），當防禦拒收即可。
	if (!Victim->bAsleep)
	{
		Attacker->ClientShakeAck(false);
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	if (const float* Last = LastShakeTimeByPlayer.Find(PS->GetPlayerId()))
	{
		if (Now - *Last < ShakeAttackCooldownSec)
		{
			Attacker->ClientShakeAck(false);
			return;
		}
	}
	if (PS->Cash < ShakeAttackCost)
	{
		Attacker->ClientShakeAck(false);
		return;
	}

	PS->Cash -= ShakeAttackCost;
	LastShakeTimeByPlayer.Add(PS->GetPlayerId(), Now);
	Victim->ClientApplyShake(PS->GetPlayerName(), ShakeAttackSeconds, ShakeAttackAmpCm);
	Attacker->ClientShakeAck(true);
	PersistCharacter(Attacker); // 錢包立即入檔（雷射同款語意）
}

// --- 醉夢迷宮：轉盤路由（victim↔server↔killer 三點；零 multicast、零第三方資訊） ---

void ANiceInkGameMode::HandleMazeTrapHit(ANiceInkCharacter* Victim, int32 KillerPlayerId)
{
	const ANiceInkGameState* GS = NIState();
	if (!GS || !Victim || KillerPlayerId == GS->VictimPlayerId || !FindNIPlayerState(KillerPlayerId))
	{
		return; // 兇手必須是在場的非受害者玩家（相位/身分驗證在 Character RPC 端）
	}
	if (PendingDialKillerId != INDEX_NONE)
	{
		return; // 一次一件：受害者死亡序列中不會再踩，重複＝異常訊息，忽略
	}

	ANiceInkCharacter* Killer = ANiceInkCharacter::FindByPlayerId(GetWorld(), KillerPlayerId);
	if (!Killer)
	{
		return;
	}

	PendingDialKillerId = KillerPlayerId;
	PendingDialVictim = Victim;
	Killer->ClientOpenTrapDial(TrapDialSeconds);

	// 失效保險：兇手掉線／沒回 → 逾時預設 0 度（SPEC 定案 #31）
	GetWorldTimerManager().SetTimer(DialFailsafeHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		ResolveTrapDial(0.0f);
	}), TrapDialSeconds + 0.6f, false);
}

void ANiceInkGameMode::HandleTrapDialSubmit(ANiceInkCharacter* Killer, float AngleDeg)
{
	if (!Killer || Killer->GetInkAuthorId() != PendingDialKillerId)
	{
		return; // 非 pending 兇手的度數（含轉盤已被失效保險結案的遲到訊息）
	}
	ResolveTrapDial(AngleDeg);
}

void ANiceInkGameMode::ResolveTrapDial(float AngleDeg)
{
	if (PendingDialKillerId == INDEX_NONE)
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(DialFailsafeHandle);
	PendingDialKillerId = INDEX_NONE;

	if (ANiceInkCharacter* Victim = PendingDialVictim.Get())
	{
		// 度數只回受害者 client 執行旋轉——永不顯示、不進任何共享狀態
		Victim->ClientApplyMazeRotation(FMath::Clamp(AngleDeg, -360.0f, 360.0f));
	}
	PendingDialVictim = nullptr;
}

void ANiceInkGameMode::DebugRoboMazeDial(float AngleDeg)
{
	FTimerHandle Unused;
	GetWorldTimerManager().SetTimer(Unused, FTimerDelegate::CreateWeakLambda(this, [this, AngleDeg]()
	{
		ResolveTrapDial(AngleDeg);
	}), 0.1f, false);
}

FString ANiceInkGameMode::DebugMazeStats(int32 NumSeeds, int32 Cup)
{
	const FDreamMazeParams Params = MazeParamsPerCup.IsValidIndex(FMath::Clamp(Cup, 0, MazeParamsPerCup.Num() - 1))
		? MazeParamsPerCup[FMath::Clamp(Cup, 0, MazeParamsPerCup.Num() - 1)]
		: FDreamMazeGen::DefaultParamsForCup(Cup);
	const FString Report = FDreamMazeGen::RunStats(Params, NumSeeds, /*TrapCount=*/4);
	UE_LOG(LogTemp, Display, TEXT("%s"), *Report);
	return Report;
}

FString ANiceInkGameMode::DebugTraceStats(int32 NumSeeds, int32 Cup)
{
	const FDreamTraceParams Params = TraceParamsPerCup.IsValidIndex(FMath::Clamp(Cup, 0, TraceParamsPerCup.Num() - 1))
		? TraceParamsPerCup[FMath::Clamp(Cup, 0, TraceParamsPerCup.Num() - 1)]
		: FDreamTraceGen::DefaultParamsForCup(Cup);
	const FString Report = FDreamTraceGen::RunStats(Params, NumSeeds);
	UE_LOG(LogTemp, Display, TEXT("%s"), *Report);
	return Report;
}

void ANiceInkGameMode::DebugRoboShake()
{
	// timer-deferred（RPC 逃出 python 執行 guard）：第一位非受害者玩家＝攻擊者，
	// 走真實 HandleShakeAttack 路徑（扣款/冷卻/受害者 Client RPC 全真）
	FTimerHandle Unused;
	GetWorldTimerManager().SetTimer(Unused, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		const ANiceInkGameState* GS = NIState();
		if (!GS)
		{
			return;
		}
		for (TActorIterator<ANiceInkCharacter> It(GetWorld()); It; ++It)
		{
			const APlayerState* PS = It->GetPlayerState();
			if (PS && PS->GetPlayerId() != GS->VictimPlayerId)
			{
				HandleShakeAttack(*It);
				return;
			}
		}
	}), 0.1f, false);
}

// --- 翻身提案（2026-07-15 user 定案）---

void ANiceInkGameMode::HandleFlipPropose(ANiceInkCharacter* Proposer)
{
	ANiceInkGameState* GS = NIState();
	const APlayerState* PS = Proposer ? Proposer->GetPlayerState() : nullptr;
	if (!GS || !PS || GS->CurrentPhase != ENiceInkPhase::Drawing ||
		PS->GetPlayerId() == GS->VictimPlayerId || Proposer->bAsleep ||
		GS->FlipProposerId != INDEX_NONE) // 一次一案
	{
		return;
	}

	GS->FlipProposerId = PS->GetPlayerId();
	GS->FlipProposalSerial++;
	FlipAgreedIds.Reset();
	FlipAgreedIds.Add(PS->GetPlayerId()); // 提案人＝自動同意
	GS->FlipAgreeNeeded = GS->PlayerArray.Num() - 1; // 「其餘的人」＝全體非受害者
	GS->FlipAgreeCount = FlipAgreedIds.Num();

	// 逾時作廢（有人不表態＝否決；不設反對鍵，沉默即否）
	GetWorldTimerManager().SetTimer(FlipTimeoutHandle, this, &ANiceInkGameMode::ClearFlipProposal, 10.0f, false);
	MaybeExecuteFlip(); // 兩人房：提案人＝唯一作畫者，當場過票
}

void ANiceInkGameMode::HandleFlipAgree(ANiceInkCharacter* Agreer)
{
	ANiceInkGameState* GS = NIState();
	const APlayerState* PS = Agreer ? Agreer->GetPlayerState() : nullptr;
	if (!GS || !PS || GS->CurrentPhase != ENiceInkPhase::Drawing ||
		GS->FlipProposerId == INDEX_NONE || PS->GetPlayerId() == GS->VictimPlayerId || Agreer->bAsleep)
	{
		return;
	}
	FlipAgreedIds.Add(PS->GetPlayerId());
	GS->FlipAgreeCount = FlipAgreedIds.Num();
	MaybeExecuteFlip();
}

void ANiceInkGameMode::MaybeExecuteFlip()
{
	ANiceInkGameState* GS = NIState();
	if (!GS || GS->FlipProposerId == INDEX_NONE || GS->FlipAgreeCount < GS->FlipAgreeNeeded)
	{
		return;
	}
	if (ANiceInkCharacter* Victim = GetVictimCharacter())
	{
		ForceExitAllLeans(); // 畫布翻面＝所有鎖定作廢（表面法線全變）
		Victim->ServerSetFaceDown(!Victim->bBodyFaceDown);
	}
	ClearFlipProposal();
}

void ANiceInkGameMode::ClearFlipProposal()
{
	GetWorldTimerManager().ClearTimer(FlipTimeoutHandle);
	FlipAgreedIds.Reset();
	if (ANiceInkGameState* GS = NIState())
	{
		GS->FlipProposerId = INDEX_NONE;
		GS->FlipAgreeCount = 0;
		GS->FlipAgreeNeeded = 0;
	}
}

void ANiceInkGameMode::DebugRoboFlip()
{
	FTimerHandle Unused;
	GetWorldTimerManager().SetTimer(Unused, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (ANiceInkCharacter* Victim = GetVictimCharacter())
		{
			ForceExitAllLeans();
			Victim->ServerSetFaceDown(!Victim->bBodyFaceDown);
		}
	}), 0.1f, false);
}

void ANiceInkGameMode::DebugRoboKick(float AimYawWorld)
{
	FTimerHandle Unused;
	GetWorldTimerManager().SetTimer(Unused, FTimerDelegate::CreateWeakLambda(this, [this, AimYawWorld]()
	{
		if (ANiceInkCharacter* Victim = GetVictimCharacter())
		{
			Victim->ServerKick(AimYawWorld);
		}
	}), 0.1f, false);
}

// --- 作畫許可 ---

bool ANiceInkGameMode::CanPaintOn(const ANiceInkCharacter* Painter, const ANiceInkCharacter* Target) const
{
	const ANiceInkGameState* GS = NIState();
	if (!GS || !Painter || !Target || Painter == Target || Painter->bAsleep)
	{
		return false;
	}

	const APlayerState* PainterPS = Painter->GetPlayerState();
	const APlayerState* TargetPS = Target->GetPlayerState();
	if (!PainterPS || !TargetPS)
	{
		return false;
	}

	if (GS->CurrentPhase == ENiceInkPhase::Drawing)
	{
		// 作畫階段：畫沉睡的受害者。誤傷已廢止（v3.1 定案 #20）——
		// 貼臉鎖定只落在受害者身上；致盲的代價改為毀自己的畫。
		return TargetPS->GetPlayerId() == GS->VictimPlayerId && PainterPS->GetPlayerId() != GS->VictimPlayerId;
	}
	if (GS->CurrentPhase == ENiceInkPhase::Finale)
	{
		// 羞辱時間：全員畫輸家
		return TargetPS->GetPlayerId() == GS->LoserPlayerId && PainterPS->GetPlayerId() != GS->LoserPlayerId;
	}
	return false;
}
