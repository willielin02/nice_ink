#include "NiceInkGameMode.h"

#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InkCanvasComponent.h"
#include "InkTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Crc.h"
#include "NiceInkBottle.h"
#include "NiceInkCharacter.h"
#include "NiceInkTvSet.h"
#include "NiceInkFaceShare.h"
#include "NiceInkGameInstance.h"
#include "NiceInkGameSession.h"
#include "NiceInkGameState.h"
#include "NiceInkHUD.h"
#include "NiceInkNotary.h"
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
	EnsureStageGeometry(); // 舞台幾何要在任何鏡頭/走位讀它之前就有效
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

	// 儀式中途作廢：步進歸零、瓶子回地上（未歸零＝下一輪的純函式讀到殘留步）
	SetCeremonyStep(ENiCeremonyStep::None, 0.0f);
	PendingVictimId = INDEX_NONE;
	if (CeremonyBottle.IsValid())
	{
		CeremonyBottle->ServerSetHeld(nullptr);
	}

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
		// P1 兩道閘（Docs/ANTICHEAT_PLAN.md §4.1）：
		// ① unverified 玩家（假裝雲端故障進房/上行逾時）本場一律不落雲＝金庫原封；
		// ② 無結算單（中途消費點雷射/搖晃、Logout、/attest 失敗）＝只寫主機本機槽，
		//    上雲延後到下一個結算點。**記帳待 user 裁**：斷線會丟「上一結算點之後」的
		//    雷射/搖晃現金變動（併入 attest digest 是 Phase 2 的選項）。
		if (FNiceInkNotary::IsConfigured() &&
			(!PS->bPersonaVerified || RoundSettlementToken.IsEmpty()))
		{
			return;
		}
		TArray<uint8> Bytes;
		if (UGameplayStatics::SaveGameToMemory(Save, Bytes) && Bytes.Num() > 0)
		{
			const APlayerController* PC = Cast<APlayerController>(Character->GetController());
			if (PC && PC->IsLocalController())
			{
				// listen 主機本人：不過網，直寫雲端
				if (UNiceInkPersonaSubsystem* Persona = UNiceInkPersonaSubsystem::Get(this))
				{
					if (FNiceInkNotary::IsConfigured())
					{
						// P1：host 本人同樣憑單換簽章再落雲（與遠端 ClientPersonaEnd 同構）
						const FString Sha = FNiceInkNotary::Sha256Hex(Bytes);
						TArray<uint8> Payload = Bytes;
						TWeakObjectPtr<UNiceInkPersonaSubsystem> WeakP = Persona;
						FNiceInkNotary::RequestSignPersona(Persona->GetLocalPuid(), Sha,
							RoundSettlementToken,
							[WeakP, Payload](bool bSignOk, int32 Seq, FString Sig)
							{
								if (UNiceInkPersonaSubsystem* P = WeakP.Get())
								{
									if (!bSignOk)
									{
										UE_LOG(LogTemp, Warning,
											TEXT("NiAnticheat: host /sign-persona 失敗 — 本輪落未簽版"));
									}
									P->StoreAssets(Payload, bSignOk ? Seq : 0, bSignOk ? Sig : FString());
								}
							});
					}
					else
					{
						Persona->StoreAssets(Bytes);
					}
				}
			}
			else
			{
				Character->SendPersonaToOwner(Bytes, RoundSettlementToken);
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
	PS->bPersonaVerified = true; // 本機槽路只在未配置後端時走＝旗標不消費（見 PersistCharacter）

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
	From->ServerSetFaceReady(); // 現身閘：臉到手＝這名力士可以對旁人現身了

	// 主機自己也是 viewer：直接入本機登記簿（不走 RPC）
	if (UNiceInkFaceShare* Share = UNiceInkFaceShare::Get(this))
	{
		Share->StoreBlob(Seat, Blob);
	}

	// 廣播給已報到的遠端 viewer（本人席位跳過——上傳前已入自己的簿）；
	// 同批建現身閘 ack 名單：這批 viewer 全部回報收妥後這名力士才現身
	//（單一權威制——晚一步報到的 viewer 不進名單：他自己被 veil 蓋著、看不到閃現）
	TArray<TWeakObjectPtr<ANiceInkCharacter>> Pending;
	for (const TWeakObjectPtr<ANiceInkCharacter>& V : FaceViewers)
	{
		ANiceInkCharacter* C = V.Get();
		const ANiceInkPlayerState* VPS = C ? C->GetPlayerState<ANiceInkPlayerState>() : nullptr;
		if (C && (!VPS || VPS->SeatIndex != Seat))
		{
			EnqueueFaceJob(C, Seat, Shared);
			Pending.Add(C);
		}
	}
	FaceSeatChar.Add(Seat, From);
	if (Pending.Num() == 0)
	{
		From->FaceGateShowNow(); // 房裡沒有別的觀看者（開房第一人）：即刻現身
	}
	else
	{
		FacePendingAcks.Add(Seat, Pending);
		// 5s 保底：ack 丟失/觀看者離場——照樣現身，不卡在隱形
		FTimerHandle Unused;
		TWeakObjectPtr<ANiceInkCharacter> WeakFrom = From;
		GetWorldTimerManager().SetTimer(Unused, FTimerDelegate::CreateWeakLambda(this, [WeakFrom]()
		{
			if (ANiceInkCharacter* C = WeakFrom.Get())
			{
				C->FaceGateShowNow();
			}
		}), 5.0f, false);
	}
	UE_LOG(LogTemp, Log, TEXT("NiFaceShare: host received face for seat %d (%d bytes, %d viewers)"),
		Seat, Blob.Num(), FaceViewers.Num());
}

void ANiceInkGameMode::OnViewerGotFace(ANiceInkCharacter* Viewer, int32 Seat)
{
	TArray<TWeakObjectPtr<ANiceInkCharacter>>* Pending = FacePendingAcks.Find(Seat);
	if (!Pending)
	{
		return;
	}
	Pending->RemoveAll([Viewer](const TWeakObjectPtr<ANiceInkCharacter>& W)
	{
		return !W.IsValid() || W.Get() == Viewer;
	});
	if (Pending->Num() == 0)
	{
		FacePendingAcks.Remove(Seat);
		if (ANiceInkCharacter* C = FaceSeatChar.FindRef(Seat).Get())
		{
			C->FaceGateShowNow();
		}
	}
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
	TArray<int32> ManifestSeats; // 報到當下既有的臉＝joiner veil 的等待集
	for (const TPair<int32, TSharedPtr<TArray<uint8>>>& Pair : FaceBlobs)
	{
		if (Pair.Key != OwnSeat)
		{
			ManifestSeats.Add(Pair.Key);
			EnqueueFaceJob(Viewer, Pair.Key, Pair.Value);
		}
	}
	Viewer->ClientFaceManifest(ManifestSeats);

	// 現身閘保底：報到後 10s 未收到臉（上傳失敗/CRC 拒收）＝強制 ready——
	// 名冊臉現身的誠實降級，不卡開局、不永久隱形（>veil 8s＝joiner 先掀布再現身）
	FTimerHandle Unused;
	TWeakObjectPtr<ANiceInkCharacter> WeakViewer = Viewer;
	GetWorldTimerManager().SetTimer(Unused, FTimerDelegate::CreateWeakLambda(this, [WeakViewer]()
	{
		if (ANiceInkCharacter* V = WeakViewer.Get())
		{
			V->ServerSetFaceReady(/*bNoBlobFallback=*/true); // 上傳失敗：名冊臉現身、不卡開局
		}
	}), 10.0f, false);
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
			&ANiceInkGameMode::TickFaceSend, 0.025f, /*bLoop=*/true);
	}
}

void ANiceInkGameMode::TickFaceSend()
{
	constexpr int32 ChunkSize = 16 * 1024;
	// 08-14 卡頓根治①＋二修：每收件者每 tick 1×16KB、tick=0.025s≈640KB/s「抹平」
	//（NIF2 後一張臉 ~0.7s）。單 tick 爆發會把該連線打進逐幀飽和、bFaceReady 等
	// 小屬性被餓（現身旗標晚 2.3s 實錘）；單塊 16KB＜每幀預算（帽 2MB/s）＝不飽和。
	// 不同收件者各自額度＝並行推進；同收件者多張臉照舊排隊逐張送。
	TSet<ANiceInkCharacter*> Served;
	for (int32 i = 0; i < FaceSendQueue.Num(); /*步進在迴圈尾*/)
	{
		FNiFaceSendJob& Job = FaceSendQueue[i];
		ANiceInkCharacter* C = Job.Target.Get();
		if (!C || !Job.Blob.IsValid())
		{
			FaceSendQueue.RemoveAt(i); // 收件者離場：job 作廢
			continue;
		}
		if (Served.Contains(C))
		{
			++i; // 這條連線本 tick 額度已用（後續 job 排隊等）
			continue;
		}
		Served.Add(C);
		const TArray<uint8>& B = *Job.Blob;
		if (!Job.bBegun)
		{
			FLinearColor Tone(0.4f, 0.22f, 0.13f);
			UNiceInkFaceShare::PeekTone(B, Tone); // tone 先行：膚色不等列車
			C->ClientFaceBegin(Job.Seat, B.Num(), Tone);
			Job.bBegun = true;
		}
		int32 Budget = 1;
		while (Budget-- > 0 && Job.NextOff < B.Num())
		{
			TArray<uint8> Chunk(B.GetData() + Job.NextOff, FMath::Min(ChunkSize, B.Num() - Job.NextOff));
			C->ClientFaceChunk(Job.Seat, Job.NextOff, Chunk);
			Job.NextOff += Chunk.Num();
		}
		if (Job.NextOff >= B.Num())
		{
			C->ClientFaceEnd(Job.Seat, FCrc::MemCrc32(B.GetData(), B.Num()));
			FaceSendQueue.RemoveAt(i);
			continue;
		}
		++i;
	}
	if (FaceSendQueue.Num() == 0)
	{
		GetWorldTimerManager().ClearTimer(FaceSendTimer);
	}
}

void ANiceInkGameMode::ApplyUploadedPersona(ANiceInkCharacter* Character, const TArray<uint8>& Bytes,
	int32 SigSeq, const FString& SigHex)
{
	ANiceInkPlayerState* PS = Character ? Character->GetPlayerState<ANiceInkPlayerState>() : nullptr;
	if (!PS || PS->bAssetsRestored)
	{
		return;
	}

	// P1 簽章制（Docs/ANTICHEAT_PLAN.md §4.1）：配置後端＝blob 必須驗過才套用。
	// 所有失敗路徑一律「封口＋乾淨新身」——fallback 到主機本機槽＝可被利用的回滾後門。
	if (FNiceInkNotary::IsConfigured())
	{
		const FString Puid = UNiceInkPersonaSubsystem::PuidFromNetIdString(PS->GetUniqueId().ToString());
		if (Puid.IsEmpty())
		{
			PS->bAssetsRestored = true;
			UE_LOG(LogTemp, Warning, TEXT("NiAnticheat: 無 PUID 的 persona（%s）— 乾淨新身"),
				*PS->GetPlayerName());
			return;
		}
		const FString Sha = FNiceInkNotary::Sha256Hex(Bytes);
		if (SigSeq > 0 && !FNiceInkNotary::VerifyPersonaSig(Puid, SigSeq, Sha, SigHex))
		{
			PS->bAssetsRestored = true;
			UE_LOG(LogTemp, Warning, TEXT("NiAnticheat: persona 簽章驗不過（%s seq=%d）— 乾淨新身"),
				*PS->GetPlayerName(), SigSeq);
			return;
		}
		// 防回滾：出示的序號必須＝後端帳本最新（SigSeq=0＝未簽遷移路，帳本必須也是 0）。
		// 先封口（防 TryRestoreTick 併行雙還原），套用等 latest-seq 回來。
		PS->bAssetsRestored = true;
		TWeakObjectPtr<ANiceInkGameMode> WeakThis = this;
		TWeakObjectPtr<ANiceInkCharacter> WeakChar = Character;
		TArray<uint8> BytesCopy = Bytes;
		const int32 ShownSeq = SigSeq;
		const FString PlayerName = PS->GetPlayerName();
		FNiceInkNotary::RequestLatestSeq(Puid,
			[WeakThis, WeakChar, BytesCopy, ShownSeq, PlayerName](bool bOk, int32 LatestSeq)
			{
				ANiceInkGameMode* GM = WeakThis.Get();
				ANiceInkCharacter* C = WeakChar.Get();
				if (!GM || !C)
				{
					return;
				}
				if (bOk && LatestSeq != ShownSeq)
				{
					UE_LOG(LogTemp, Warning,
						TEXT("NiAnticheat: persona 序號不符（%s 出示 %d、帳本 %d）＝回滾/失簽 — 乾淨新身"),
						*PlayerName, ShownSeq, LatestSeq);
					return; // 不套用＝乾淨新身（bAssetsRestored 已封）
				}
				if (!bOk)
				{
					// fail-open：簽章本身已驗過（內容真），只損失這一次的回滾防護
					UE_LOG(LogTemp, Warning,
						TEXT("NiAnticheat: latest-seq 後端無回應 — fail-open 套用（%s）"), *PlayerName);
				}
				GM->ApplyPersonaBytesNow(C, BytesCopy);
			});
		return;
	}

	// 未配置後端：行為與簽章制之前逐位相同
	ApplyPersonaBytesNow(Character, Bytes);
}

void ANiceInkGameMode::ApplyPersonaBytesNow(ANiceInkCharacter* Character, const TArray<uint8>& Bytes)
{
	ANiceInkPlayerState* PS = Character ? Character->GetPlayerState<ANiceInkPlayerState>() : nullptr;
	if (!PS)
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
	PS->bPersonaVerified = true; // 走到這裡＝上游驗證已過（或未配置後端＝旗標不消費）

	// 信任界線：格式與量級檢查照舊；語意的真偽由 P1 簽章＋序號在上游裁決
	//（未配置後端＝維持「語意照單全收」的記帳取捨）
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
					ApplyUploadedPersona(Character, Persona->GetCachedAssets(),
						Persona->GetCachedSeq(), Persona->GetCachedSigHex());
				}
				else if (FNiceInkNotary::IsConfigured())
				{
					// P1：本機槽 fallback＝繞簽章的回滾後門，配置後端時封死；
					// 「真的沒有雲端資產」要對後端帳本驗過才算 verified 新人
					ResolveNoPersonaClaim(Character);
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
		if (FNiceInkNotary::IsConfigured())
		{
			// P1：逾時同理封死本機槽——「假裝雲端讀失敗」不得換到未驗證的舊資產。
			// 誠實玩家的雲端故障＝這一場乾淨新身（正本仍在雲端，下次讀到照舊）。
			PS->bAssetsRestored = true;
			UE_LOG(LogTemp, Log, TEXT("NiAnticheat: %s 上行逾時 — 乾淨新身（本機槽 fallback 已封）"),
				*PS->GetPlayerName());
			return;
		}
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

// --- 防作弊 P1：公證接線（2026-08-31；Docs/ANTICHEAT_PLAN.md §4.1）---

void ANiceInkGameMode::ResolveNoPersonaClaim(ANiceInkCharacter* Character)
{
	ANiceInkPlayerState* PS = Character ? Character->GetPlayerState<ANiceInkPlayerState>() : nullptr;
	if (!PS || PS->bAssetsRestored)
	{
		return;
	}
	PS->bAssetsRestored = true; // 封口（乾淨新身先成立；verified 等帳本回話）

	const FString Puid = UNiceInkPersonaSubsystem::PuidFromNetIdString(PS->GetUniqueId().ToString());
	if (Puid.IsEmpty())
	{
		return; // 無 PUID＝不落雲的路，verified 與否無消費者
	}
	TWeakObjectPtr<ANiceInkPlayerState> WeakPS = PS;
	const FString PlayerName = PS->GetPlayerName();
	FNiceInkNotary::RequestLatestSeq(Puid,
		[WeakPS, PlayerName](bool bOk, int32 LatestSeq)
		{
			ANiceInkPlayerState* P = WeakPS.Get();
			if (!P)
			{
				return;
			}
			if (bOk && LatestSeq > 0)
			{
				// 有簽發史卻宣稱空身＝「假裝雲端故障洗白」——本場不落雲，金庫原封
				UE_LOG(LogTemp, Warning,
					TEXT("NiAnticheat: %s 宣稱無資產但帳本 seq=%d — 本場 unverified（不落雲）"),
					*PlayerName, LatestSeq);
				return;
			}
			if (!bOk)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("NiAnticheat: latest-seq 無回應（%s 空身宣稱）— fail-open 視為新人"), *PlayerName);
			}
			P->bPersonaVerified = true; // 真新人（或後端不可達的 fail-open）＝正常開局正常落雲
		});
}

void ANiceInkGameMode::RequestRoundAttest()
{
	RoundSettlementToken.Reset();
	if (!FNiceInkNotary::IsConfigured())
	{
		return;
	}
	UNiceInkPersonaSubsystem* Persona = UNiceInkPersonaSubsystem::Get(this);
	const FString HostPuid = Persona ? Persona->GetLocalPuid() : FString();
	if (HostPuid.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("NiAnticheat: host 無 PUID — 本輪無結算單（不落雲）"));
		return;
	}
	if (NotaryRoomId.IsEmpty())
	{
		NotaryRoomId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	}
	const ANiceInkGameState* GS = NIState();
	const int32 Round = GS ? GS->CurrentRound : 0;

	// digest＝回合結果的正準字串（P1 後端不驗語意、host 單見證＝過渡；Phase 2 這裡
	// 換 quorum digest 規格：全員各自簽、含自我作品雜湊）
	FString Canon = FString::Printf(TEXT("R|%s|%d|%d|%d"), *NotaryRoomId, Round,
		GS ? GS->VictimPlayerId : INDEX_NONE, GS ? GS->RevealedAuthorId : INDEX_NONE);
	if (GS)
	{
		for (const APlayerState* P : GS->PlayerArray)
		{
			const ANiceInkPlayerState* NiPS = Cast<ANiceInkPlayerState>(P);
			Canon += FString::Printf(TEXT("|%d:%d"), P ? P->GetPlayerId() : -1, NiPS ? NiPS->Cash : 0);
		}
	}
	const FTCHARToUTF8 Utf8(*Canon);
	TArray<uint8> CanonBytes;
	CanonBytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	const FString Digest = FNiceInkNotary::Sha256Hex(CanonBytes);

	TWeakObjectPtr<ANiceInkGameMode> WeakThis = this;
	FNiceInkNotary::RequestAttest(HostPuid, NotaryRoomId, Round, Digest, /*RosterSize=*/1,
		[WeakThis](bool bSettled, FString Token)
		{
			ANiceInkGameMode* GM = WeakThis.Get();
			if (!GM)
			{
				return;
			}
			if (bSettled)
			{
				GM->RoundSettlementToken = Token;
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("NiAnticheat: /attest 未結算 — 本輪 Persist 不落雲"));
			}
		});
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

FString ANiceInkGameMode::DebugRoomCenterProbe() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return TEXT("ROOMPROBE no-world");
	}

	// **只用 line trace**（2026-08-16 血價）：道場部件是 complex-as-simple 碰撞，
	// 膠囊 overlap 對三角網格查不到 ⇒ 首版「可站立」測試把整片牆判成淨空、
	// 可走域一路延伸到掃描邊界。射線打得到，overlap 打不到。
	FCollisionQueryParams Ignore(SCENE_QUERY_STAT(NiRoomProbe), /*bTraceComplex=*/true);
	for (TActorIterator<ANiceInkCharacter> It(World); It; ++It)
	{
		Ignore.AddIgnoredActor(*It); // 活體不算場地
	}

	auto FloorAt = [World, &Ignore](const FVector2D& P, float& OutZ) -> bool
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, FVector(P.X, P.Y, 150.0f),
			FVector(P.X, P.Y, -1000.0f), ECC_Visibility, Ignore))
		{
			return false;
		}
		OutZ = static_cast<float>(Hit.ImpactPoint.Z);
		return OutZ > -50.0f && OutZ < 60.0f; // 高台/桌面不算地板
	};

	// 「在房間裡」＝四面八方的水平射線都在合理距離內打到東西（牆）。
	// 房間中心＝**離最近的牆最遠的點**（pole of inaccessibility）。
	constexpr int32 NumRays = 24;
	constexpr float RayLen = 1200.0f;
	auto Enclosure = [&](const FVector2D& P, float FloorZ, float& OutMinDist, int32& OutMisses)
	{
		OutMinDist = RayLen;
		OutMisses = 0;
		for (int32 i = 0; i < NumRays; ++i)
		{
			const float Rad = FMath::DegreesToRadians(360.0f * i / NumRays);
			const FVector Dir(FMath::Cos(Rad), FMath::Sin(Rad), 0.0f);
			// 胸高＋膝高各一條：矮傢俱（長凳）與高牆都算「邊界」
			float Best = RayLen;
			bool bAnyHit = false;
			for (float H : { 45.0f, 120.0f })
			{
				FHitResult Hit;
				const FVector Start(P.X, P.Y, FloorZ + H);
				if (World->LineTraceSingleByChannel(Hit, Start, Start + Dir * RayLen,
					ECC_Visibility, Ignore))
				{
					bAnyHit = true;
					Best = FMath::Min(Best, static_cast<float>(Hit.Distance));
				}
			}
			if (!bAnyHit)
			{
				++OutMisses; // 這個方向沒有邊界＝不在封閉空間內
			}
			OutMinDist = FMath::Min(OutMinDist, Best);
		}
	};

	TArray<FString> Out;
	Out.Add(FString::Printf(TEXT("ROOMPROBE rays=%d rayLen=%.0f (line-trace only: complex-as-simple 對 overlap 不可見)"),
		NumRays, RayLen));

	// 掃描：候選中心 50cm 網格
	constexpr float Step = 50.0f;
	constexpr float ScanMin = -700.0f, ScanMax = 700.0f;
	struct FCand { FVector2D P; float Clear; };
	TArray<FCand> Inside;
	FVector2D BbMin(FLT_MAX, FLT_MAX), BbMax(-FLT_MAX, -FLT_MAX), Sum(0, 0);
	for (float X = ScanMin; X <= ScanMax; X += Step)
	{
		for (float Y = ScanMin; Y <= ScanMax; Y += Step)
		{
			const FVector2D P(X, Y);
			float Z = 0.0f;
			if (!FloorAt(P, Z))
			{
				continue;
			}
			float MinD = 0.0f;
			int32 Misses = 0;
			Enclosure(P, Z, MinD, Misses);
			if (Misses > 0 || MinD < 40.0f)
			{
				continue; // 開放側/貼牆
			}
			Inside.Add({ P, MinD });
			BbMin.X = FMath::Min(BbMin.X, X); BbMin.Y = FMath::Min(BbMin.Y, Y);
			BbMax.X = FMath::Max(BbMax.X, X); BbMax.Y = FMath::Max(BbMax.Y, Y);
			Sum += P;
		}
	}
	Out.Add(FString::Printf(TEXT("INSIDE n=%d bbox min=(%.0f,%.0f) max=(%.0f,%.0f) size=(%.0f x %.0f) mid=(%.0f,%.0f) centroid=(%.1f,%.1f)"),
		Inside.Num(), BbMin.X, BbMin.Y, BbMax.X, BbMax.Y,
		BbMax.X - BbMin.X, BbMax.Y - BbMin.Y,
		(BbMin.X + BbMax.X) * 0.5f, (BbMin.Y + BbMax.Y) * 0.5f,
		Inside.Num() ? Sum.X / Inside.Num() : 0.0f, Inside.Num() ? Sum.Y / Inside.Num() : 0.0f));
	if (Inside.Num() == 0)
	{
		return FString::Join(Out, TEXT("\n"));
	}

	// ASCII 地圖（100cm/格；數字＝該格離最近邊界的距離 ÷100，越大越中央）
	Out.Add(TEXT("MAP (100cm/cell, rows=X asc, cols=Y asc; digit=clearance/100cm, S=seat, L=lieSpot, .=outside)"));
	TMap<int32, float> Best100;
	for (const FCand& C : Inside)
	{
		const int32 Key = FMath::RoundToInt(C.P.X / 100.0f) * 10000 + FMath::RoundToInt(C.P.Y / 100.0f);
		float& V = Best100.FindOrAdd(Key, 0.0f);
		V = FMath::Max(V, C.Clear);
	}
	for (int32 xi = FMath::RoundToInt(BbMin.X / 100.0f); xi <= FMath::RoundToInt(BbMax.X / 100.0f); ++xi)
	{
		FString Row = FString::Printf(TEXT("x=%5d "), xi * 100);
		for (int32 yi = FMath::RoundToInt(BbMin.Y / 100.0f); yi <= FMath::RoundToInt(BbMax.Y / 100.0f); ++yi)
		{
			const float* V = Best100.Find(xi * 10000 + yi);
			TCHAR Ch = V ? static_cast<TCHAR>(TEXT('0') + FMath::Min(9, FMath::FloorToInt(*V / 100.0f))) : TEXT('.');
			for (const FVector2D& Sp : SeatSpots)
			{
				if (FMath::RoundToInt(Sp.X / 100.0f) == xi && FMath::RoundToInt(Sp.Y / 100.0f) == yi)
				{
					Ch = TEXT('S');
				}
			}
			if (FMath::RoundToInt(VictimLieSpot.X / 100.0f) == xi &&
				FMath::RoundToInt(VictimLieSpot.Y / 100.0f) == yi)
			{
				Ch = TEXT('L');
			}
			Row.AppendChar(Ch);
		}
		Out.Add(Row);
	}

	Inside.Sort([](const FCand& A, const FCand& B) { return A.Clear > B.Clear; });
	Out.Add(TEXT("MOST-CENTRAL (max clearance to nearest boundary):"));
	for (int32 i = 0; i < FMath::Min(8, Inside.Num()); ++i)
	{
		Out.Add(FString::Printf(TEXT("  #%d center=(%.0f,%.0f) clearance=%.0fcm"),
			i, Inside[i].P.X, Inside[i].P.Y, Inside[i].Clear));
	}

	// 現況對照
	float LieZ = 0.0f, LieClear = 0.0f;
	int32 LieMiss = 0;
	if (FloorAt(VictimLieSpot, LieZ))
	{
		Enclosure(VictimLieSpot, LieZ, LieClear, LieMiss);
	}
	Out.Add(FString::Printf(TEXT("CURRENT lieSpot=(%.0f,%.0f) clearance=%.0fcm misses=%d"),
		VictimLieSpot.X, VictimLieSpot.Y, LieClear, LieMiss));
	for (int32 i = 0; i < SeatSpots.Num(); ++i)
	{
		float Z = 0.0f, Cl = 0.0f;
		int32 Ms = 0;
		if (FloorAt(SeatSpots[i], Z))
		{
			Enclosure(SeatSpots[i], Z, Cl, Ms);
		}
		Out.Add(FString::Printf(TEXT("  seat%d=(%.0f,%.0f) clearance=%.0fcm misses=%d"),
			i, SeatSpots[i].X, SeatSpots[i].Y, Cl, Ms));
	}

	const FString Result = FString::Join(Out, TEXT("\n"));
	UE_LOG(LogTemp, Warning, TEXT("%s"), *Result);
	return Result;
}

FString ANiceInkGameMode::DebugCeremonyProbe(float CenterX, float CenterY) const
{
	// 開場儀式場地探針（施工前量測，不猜）。輸出＝機讀行。
	UWorld* World = GetWorld();
	if (!World)
	{
		return TEXT("PROBE no-world");
	}

	const FVector2D Center(CenterX, CenterY);

	// 力士全部忽略（否則：Body 擋 ECC_Visibility ⇒ 地板射線打到人頭 z≈140；
	// 膠囊重疊撞到彼此 ⇒ 整條路徑假 blocked。首輪探針就是這樣被污染的）
	FCollisionQueryParams Ignore(SCENE_QUERY_STAT(NiCeremonyProbe), /*bTraceComplex=*/true);
	for (TActorIterator<ANiceInkCharacter> It(World); It; ++It)
	{
		Ignore.AddIgnoredActor(*It);
	}

	// 地板射線（與 ProbeFloorZ 同慣例：從 +150 起——+500 會打到屋頂外側）
	auto FloorAt = [World, &Ignore](const FVector2D& P, float& OutZ, FString* OutWho) -> bool
	{
		FHitResult Hit;
		const FVector Start(P.X, P.Y, 150.0f);
		const FVector End(P.X, P.Y, -1000.0f);
		if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Ignore))
		{
			OutZ = static_cast<float>(Hit.ImpactPoint.Z);
			if (OutWho && Hit.GetActor())
			{
				*OutWho = Hit.GetActor()->GetName();
			}
			return true;
		}
		OutZ = -9999.0f;
		return false;
	};

	// 站位淨空：真膠囊尺寸（42×92）在該點的重疊測試（低空障礙＝柱/矮几都抓得到）；
	// 回報擋路者名字＝診斷用（哪個道場部件擋住哪個角位）
	auto ClearAt = [World, &Ignore](const FVector2D& P, float FloorZ, FString* OutWho) -> bool
	{
		const FVector At(P.X, P.Y, FloorZ + 92.0f);
		TArray<FOverlapResult> Overlaps;
		World->OverlapMultiByChannel(Overlaps, At, FQuat::Identity, ECC_Pawn,
			FCollisionShape::MakeCapsule(42.0f, 92.0f), Ignore);
		for (const FOverlapResult& O : Overlaps)
		{
			if (O.GetActor() && O.Component.IsValid() &&
				O.Component->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block)
			{
				if (OutWho)
				{
					*OutWho = O.GetActor()->GetName();
				}
				return false;
			}
		}
		return true;
	};

	TArray<FString> Out;
	Out.Add(FString::Printf(TEXT("PROBE center=(%.1f,%.1f) lie=(%.1f,%.1f)"),
		CenterX, CenterY, VictimLieSpot.X, VictimLieSpot.Y));

	float CenterZ = 0.0f;
	FString CenterWho;
	const bool bCenterOk = FloorAt(Center, CenterZ, &CenterWho);
	FString CenterBlocker;
	const bool bCenterClear = bCenterOk && ClearAt(Center, CenterZ, &CenterBlocker);
	Out.Add(FString::Printf(TEXT("CENTERFLOOR ok=%d z=%.1f on=%s clear=%d by=%s"),
		bCenterOk ? 1 : 0, CenterZ, *CenterWho, bCenterClear ? 1 : 0, *CenterBlocker));

	// 半徑掃描：每環 24 個角位（15°）
	static const float Radii[] = { 120.0f, 140.0f, 160.0f, 180.0f, 200.0f, 220.0f };
	constexpr int32 NumAng = 24;
	for (float R : Radii)
	{
		int32 Hits = 0, Clears = 0;
		float ZMin = TNumericLimits<float>::Max();
		float ZMax = -TNumericLimits<float>::Max();
		FString Bad;
		for (int32 i = 0; i < NumAng; ++i)
		{
			const float Ang = 360.0f * i / NumAng;
			const float Rad = FMath::DegreesToRadians(Ang);
			const FVector2D P = Center + FVector2D(FMath::Cos(Rad), FMath::Sin(Rad)) * R;
			float Z = 0.0f;
			FString Who;
			const bool bFloor = FloorAt(P, Z, nullptr);
			const bool bClear = bFloor && ClearAt(P, Z, &Who);
			if (bFloor)
			{
				++Hits;
				ZMin = FMath::Min(ZMin, Z);
				ZMax = FMath::Max(ZMax, Z);
			}
			if (bClear)
			{
				++Clears;
			}
			else
			{
				Bad += FString::Printf(TEXT("%d:%s "), static_cast<int32>(Ang),
					bFloor ? *Who : TEXT("NOFLOOR"));
			}
		}
		Out.Add(FString::Printf(TEXT("RING r=%.0f floor=%d/%d clear=%d/%d zMin=%.1f zMax=%.1f bad=[%s]"),
			R, Hits, NumAng, Clears, NumAng,
			Hits > 0 ? ZMin : -9999.0f, Hits > 0 ? ZMax : -9999.0f, *Bad));
	}

	// 席位→圈上角位的直線路徑取樣（走路會不會掉出世界／撞死）
	const int32 NumSeats = SeatSpots.Num();
	for (int32 S = 0; S < NumSeats; ++S)
	{
		const float SlotAng = 360.0f * S / FMath::Max(NumSeats, 1);
		const float Rad = FMath::DegreesToRadians(SlotAng);
		const FVector2D Slot = Center + FVector2D(FMath::Cos(Rad), FMath::Sin(Rad)) * 160.0f;
		const FVector2D From = SeatSpots[S];
		int32 Holes = 0, Blocks = 0;
		FString Who;
		constexpr int32 NumSamp = 20;
		for (int32 k = 1; k <= NumSamp; ++k)
		{
			const FVector2D P = FMath::Lerp(From, Slot, static_cast<float>(k) / NumSamp);
			float Z = 0.0f;
			FString W;
			if (!FloorAt(P, Z, nullptr))
			{
				++Holes;
			}
			else if (!ClearAt(P, Z, &W))
			{
				++Blocks;
				if (Who.IsEmpty())
				{
					Who = W;
				}
			}
		}
		Out.Add(FString::Printf(TEXT("PATH seat=%d from=(%.0f,%.0f) slot=(%.0f,%.0f) dist=%.0f holes=%d blocks=%d by=%s"),
			S, From.X, From.Y, Slot.X, Slot.Y, FVector2D::Distance(From, Slot), Holes, Blocks, *Who));
	}

	const FString Result = FString::Join(Out, TEXT("\n"));
	UE_LOG(LogTemp, Warning, TEXT("%s"), *Result);
	return Result;
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
	// 開局臉齊保險（08-14）：全員臉到手才開局＝局內永不見名冊臉（規則藏開始鈕；
	// 8s 逾時/無臉端已由 FaceReady 機制保底＝不會死鎖；PIE/robo 不受擾）
	if (GetWorld() && GetWorld()->WorldType == EWorldType::Game)
	{
		for (APlayerState* PS : GS->PlayerArray)
		{
			const ANiceInkCharacter* C = Cast<ANiceInkCharacter>(PS->GetPawn());
			if (C && !C->bFaceReady)
			{
				return;
			}
		}
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

bool ANiceInkGameMode::IsCeremonySpotClear(const FVector& At) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return true;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NiCeremonySlot), /*bTraceComplex=*/false);
	for (TActorIterator<ANiceInkCharacter> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It); // 活體不算場地（探針首輪被自己人污染的教訓）
	}
	return !World->OverlapBlockingTestByChannel(At + FVector(0.0f, 0.0f, 92.0f), FQuat::Identity,
		ECC_Pawn, FCollisionShape::MakeCapsule(42.0f, 92.0f), Params);
}

float ANiceInkGameMode::ComputeCeremonySlotOffset() const
{
	// 掃描 72 個候選偏移（5°）：先要求全角位淨空，再取「總角位移最小」者
	//（＝每個人走最短的路到自己的角位；席位序≡角向序 ⇒ 路徑天然不交叉）。
	const ANiceInkGameState* GS = NIState();
	if (!GS)
	{
		return 0.0f;
	}
	TArray<int32> Seats;
	for (const APlayerState* PS : GS->PlayerArray)
	{
		if (const ANiceInkPlayerState* NIPS = Cast<ANiceInkPlayerState>(PS))
		{
			if (NIPS->SeatIndex >= 0)
			{
				Seats.AddUnique(NIPS->SeatIndex);
			}
		}
	}
	Seats.Sort();
	const int32 N = Seats.Num();
	if (N == 0)
	{
		return 0.0f;
	}

	const FVector Center = GS->CeremonyCenter;
	const float R = GS->CeremonyRadiusCm;

	// 每位玩家從圈心看出去的方位角（＝他「本來就在的方向」）
	TArray<float> Bearings;
	Bearings.SetNum(N);
	for (int32 i = 0; i < N; ++i)
	{
		const FVector2D Spot = SeatSpots.IsValidIndex(Seats[i]) ? SeatSpots[Seats[i]] : FVector2D::ZeroVector;
		Bearings[i] = FMath::RadiansToDegrees(FMath::Atan2(Spot.Y - Center.Y, Spot.X - Center.X));
	}

	float BestOffset = 0.0f;
	float BestCost = TNumericLimits<float>::Max();
	bool bFoundClear = false;
	for (int32 k = 0; k < 72; ++k)
	{
		const float Offset = k * 5.0f;
		bool bAllClear = true;
		float Cost = 0.0f;
		for (int32 i = 0; i < N; ++i)
		{
			const float Ang = Offset + 360.0f * i / N;
			const float Rad = FMath::DegreesToRadians(Ang);
			FVector At = Center + FVector(FMath::Cos(Rad), FMath::Sin(Rad), 0.0f) * R;
			At.Z = ProbeFloorZ(At);
			if (!IsCeremonySpotClear(At))
			{
				bAllClear = false;
			}
			Cost += FMath::Abs(FMath::FindDeltaAngleDegrees(Bearings[i], Ang));
		}
		// 淨空優先於距離：一旦找到淨空解，之後只跟淨空解比
		if (bAllClear && !bFoundClear)
		{
			bFoundClear = true;
			BestCost = TNumericLimits<float>::Max();
		}
		if (bAllClear == bFoundClear && Cost < BestCost)
		{
			BestCost = Cost;
			BestOffset = Offset;
		}
	}
	UE_LOG(LogTemp, Log, TEXT("NiCeremony: slot offset %.0f deg (allClear=%d cost=%.0f n=%d)"),
		BestOffset, bFoundClear ? 1 : 0, BestCost, N);
	return BestOffset;
}

ANiceInkBottle* ANiceInkGameMode::GetOrSpawnBottle()
{
	if (CeremonyBottle.IsValid())
	{
		return CeremonyBottle.Get();
	}
	UWorld* World = GetWorld();
	const ANiceInkGameState* GS = NIState();
	if (!World || !GS)
	{
		return nullptr;
	}
	// 瓶子從此常駐房間中央（回合 2+ 的罰酒直接再撿一次＝免費復用）
	FVector At = GS->CeremonyCenter;
	At.Z = ProbeFloorZ(At);
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ANiceInkBottle* B = World->SpawnActor<ANiceInkBottle>(ANiceInkBottle::StaticClass(), At,
		FRotator::ZeroRotator, SP);
	if (B)
	{
		B->RestLocation = At;
		CeremonyBottle = B;
	}
	return B;
}

ANiceInkTvSet* ANiceInkGameMode::GetOrSpawnTvSet()
{
	if (IntroTvSet.IsValid())
	{
		return IntroTvSet.Get();
	}
	UWorld* World = GetWorld();
	const ANiceInkGameState* GS = NIState();
	if (!World || !GS)
	{
		return nullptr;
	}
	FVector At = GS->GetIntroTvLocation();
	At.Z = ProbeFloorZ(At);
	// 面朝 +Y（朝觀眾弧）；開場後留在場上＝道場家具（離舞台 310cm，不擋任何動線）
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ANiceInkTvSet* Tv = World->SpawnActor<ANiceInkTvSet>(ANiceInkTvSet::StaticClass(), At,
		FRotator(0.0f, 90.0f, 0.0f), SP);
	IntroTvSet = Tv;
	return Tv;
}

void ANiceInkGameMode::EnsureStageGeometry()
{
	// 舞台幾何寫進 GameState＝**單一來源**（GameMode 只活在伺服器，但客戶端的
	// 演出鏡頭與儀式走位都要它）。改 VictimLieSpot 一處，全鏈跟著走。
	ANiceInkGameState* GS = NIState();
	if (!GS)
	{
		return;
	}
	FVector Center(VictimLieSpot.X, VictimLieSpot.Y, 0.0f);
	Center.Z = ProbeFloorZ(Center);
	GS->CeremonyCenter = Center;
	GS->CeremonyRadiusCm = CeremonyCircleRadiusCm;
	const FTransform LieT = GetVictimLieTransform();
	GS->CeremonyLieLocation = LieT.GetLocation();
	GS->CeremonyLieYaw = LieT.Rotator().Yaw;
}

void ANiceInkGameMode::SetCeremonyStep(ENiCeremonyStep Step, float Duration)
{
	ANiceInkGameState* GS = NIState();
	if (!GS)
	{
		return;
	}
	GS->CeremonyStep = Step;
	GS->CeremonyStepStartTime = GS->GetServerWorldTimeSeconds();
	GS->CeremonyStepDuration = FMath::Max(Duration, 0.01f);
	GetWorldTimerManager().ClearTimer(PhaseTimerHandle);
	if (Step != ENiCeremonyStep::None)
	{
		GetWorldTimerManager().SetTimer(PhaseTimerHandle, this, &ANiceInkGameMode::OnCeremonyStepDone,
			GS->CeremonyStepDuration, false);
	}
	UE_LOG(LogTemp, Log, TEXT("NiCeremony: step=%d dur=%.2f"), static_cast<int32>(Step), Duration);
}

void ANiceInkGameMode::EnterBottleSpin()
{
	ANiceInkGameState* GS = NIState();
	if (!bCeremonyEnabled)
	{
		GS->SetPhase(ENiceInkPhase::BottleSpin, BottleSpinSeconds);
		SetPhaseTimer(BottleSpinSeconds, &ANiceInkGameMode::OnBottleSpinDone);
		return;
	}

	if (GS->PlayerArray.Num() == 0)
	{
		GS->SetPhase(ENiceInkPhase::Lobby, 0.0f);
		SetSessionInProgress(false);
		return;
	}

	// **先抽後演**（user 定案）：人選現在就定，酒瓶只是把它演出來。
	// 不寫進 GameState＝HUD 在轉瓶期間不劇透（Spin 結束才揭曉）。
	PendingVictimId = INDEX_NONE;
	if (DebugForcedVictimSeat >= 0)
	{
		for (APlayerState* PS : GS->PlayerArray)
		{
			const ANiceInkPlayerState* NIPS = Cast<ANiceInkPlayerState>(PS);
			if (NIPS && NIPS->SeatIndex == DebugForcedVictimSeat)
			{
				PendingVictimId = NIPS->GetPlayerId();
				break;
			}
		}
	}
	if (PendingVictimId == INDEX_NONE)
	{
		// 均勻隨機＝真公平（席位角度不等距，真物理＋扇區判定會偏心）
		PendingVictimId = GS->PlayerArray[FMath::RandRange(0, GS->PlayerArray.Num() - 1)]->GetPlayerId();
	}

	// 圍圈幾何：圈心＝躺位（崩塌終點 ≡ 躺位 ⇒ 零位移修正的承重性質）
	EnsureStageGeometry();
	GS->CeremonySlotOffsetDeg = ComputeCeremonySlotOffset();

	if (ANiceInkBottle* B = GetOrSpawnBottle())
	{
		GS->BottleStartYaw = B->GetActorRotation().Yaw;
		GS->BottleEndYaw = GS->BottleStartYaw; // Gather 期間不轉；Spin 開始才算終角
	}

	// 開場動畫：只在本房第一場、非 PIE（robo 契約零干擾）。播過＝直接 Gather。
	const bool bWantIntro = bOpeningIntroEnabled && !bOpeningIntroPlayed && GetWorld() &&
		(GetWorld()->WorldType == EWorldType::Game || bOpeningIntroForceInPIE);
	if (bWantIntro)
	{
		bOpeningIntroPlayed = true;
		BeginOpeningIntro();
		return;
	}

	GS->SetPhase(ENiceInkPhase::BottleSpin, CeremonyGatherSeconds + CeremonySpinSeconds);
	SetCeremonyStep(ENiCeremonyStep::Gather, CeremonyGatherSeconds);
}

void ANiceInkGameMode::BeginOpeningIntro()
{
	ANiceInkGameState* GS = NIState();
	GetOrSpawnTvSet();
	SeatPlayersForIntro();
	const float Total = IntroSitSeconds + IntroNoticeSeconds + IntroTvOffSeconds +
		IntroProposeSeconds + IntroRiseSeconds + CeremonyGatherSeconds + CeremonySpinSeconds;
	GS->SetPhase(ENiceInkPhase::BottleSpin, Total);
	SetCeremonyStep(ENiCeremonyStep::IntroSit, IntroSitSeconds);
}

void ANiceInkGameMode::SeatPlayersForIntro()
{
	// 入座＝一次性 teleport（與導演鏡頭的硬切**同幀**＝玩家看不見搬運）。
	// 沿用睡姿傳送的既有慣例：server 搬 actor＋ClientSyncPoseTransform 讓
	// owning client 本地落地（否則 autonomous proxy 的 yaw 永不修正——07-16 血價）。
	ANiceInkGameState* GS = NIState();
	for (APlayerState* PS : GS->PlayerArray)
	{
		const ANiceInkPlayerState* NIPS = Cast<ANiceInkPlayerState>(PS);
		ANiceInkCharacter* C = NIPS ? Cast<ANiceInkCharacter>(NIPS->GetPawn()) : nullptr;
		if (!C || NIPS->SeatIndex < 0)
		{
			continue;
		}
		FVector At = GS->GetIntroSitLocation(NIPS->SeatIndex);
		At.Z = C->GetActorLocation().Z; // 保持腳下高度（地板同層）
		const FRotator Face(0.0f, GS->GetIntroSitYawDeg(NIPS->SeatIndex), 0.0f);
		C->GetCharacterMovement()->StopMovementImmediately();
		C->GetCharacterMovement()->DisableMovement(); // 坐著＝不受理移動輸入
		C->SetActorLocationAndRotation(At, Face, false, nullptr, ETeleportType::TeleportPhysics);
		if (AController* Ctrl = C->GetController())
		{
			Ctrl->SetControlRotation(Face);
		}
		C->ClientSyncPoseTransform(FTransform(Face, At));
	}
}

void ANiceInkGameMode::ReleasePlayersFromIntro()
{
	// 起身＝恢復行走（發生在 Propose→Rise 的剪接期間；姿勢端由 UpdateCeremony 收拾）
	ANiceInkGameState* GS = NIState();
	for (APlayerState* PS : GS->PlayerArray)
	{
		if (ANiceInkCharacter* C = Cast<ANiceInkCharacter>(PS->GetPawn()))
		{
			C->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		}
	}
}

void ANiceInkGameMode::OnCeremonyStepDone()
{
	ANiceInkGameState* GS = NIState();
	if (!GS)
	{
		return;
	}

	// 受害者中離防護：抽中的人不在了＝本回合作廢重來（AbortRound 內含人數判斷）
	// 開場五拍與 Gather 還不需要受害者本人（Spin 結束才揭曉）
	const bool bNeedVictim = GS->CeremonyStep != ENiCeremonyStep::Gather &&
		!NiCeremonyStepIsIntro(GS->CeremonyStep);
	const int32 CheckId = (GS->CeremonyStep == ENiCeremonyStep::Spin) ? PendingVictimId : GS->VictimPlayerId;
	if (bNeedVictim && (CheckId == INDEX_NONE ||
		!ANiceInkCharacter::FindByPlayerId(GetWorld(), CheckId)))
	{
		UE_LOG(LogTemp, Warning, TEXT("NiCeremony: victim %d gone mid-ceremony - abort"), CheckId);
		SetCeremonyStep(ENiCeremonyStep::None, 0.0f);
		AbortRound(GS->PlayerArray.Num() >= FMath::Max(MinPlayersToStart, 2));
		return;
	}

	switch (GS->CeremonyStep)
	{
	case ENiCeremonyStep::IntroSit:
		SetCeremonyStep(ENiCeremonyStep::IntroNotice, IntroNoticeSeconds);
		break;
	case ENiCeremonyStep::IntroNotice:
		SetCeremonyStep(ENiCeremonyStep::IntroTvOff, IntroTvOffSeconds);
		break;
	case ENiCeremonyStep::IntroTvOff:
		SetCeremonyStep(ENiCeremonyStep::IntroPropose, IntroProposeSeconds);
		break;
	case ENiCeremonyStep::IntroPropose:
		// 坐→站發生在這一格剪接裡（Propose 近景→Rise 全景；中間幀不上鏡）
		ReleasePlayersFromIntro();
		SetCeremonyStep(ENiCeremonyStep::IntroRise, IntroRiseSeconds);
		break;
	case ENiCeremonyStep::IntroRise:
		SetCeremonyStep(ENiCeremonyStep::Gather, CeremonyGatherSeconds);
		break;
	case ENiCeremonyStep::Gather:
	{
		// 轉瓶終角＝瓶心指向受害者**當下實際位置**（大家剛走完位，比用角位更準）
		const ANiceInkCharacter* Victim = ANiceInkCharacter::FindByPlayerId(GetWorld(), PendingVictimId);
		float TargetYaw = GS->BottleStartYaw;
		if (Victim)
		{
			const FVector To = Victim->GetActorLocation() - GS->CeremonyCenter;
			TargetYaw = FMath::RadiansToDegrees(FMath::Atan2(To.Y, To.X));
		}
		const int32 Turns = FMath::RandRange(FMath::Max(CeremonySpinTurnsMin, 1),
			FMath::Max(CeremonySpinTurnsMax, CeremonySpinTurnsMin));
		if (const ANiceInkBottle* B = GetOrSpawnBottle())
		{
			GS->BottleStartYaw = B->GetActorRotation().Yaw;
		}
		GS->BottleEndYaw = GS->BottleStartYaw + Turns * 360.0f +
			FMath::FindDeltaAngleDegrees(GS->BottleStartYaw, TargetYaw);
		SetCeremonyStep(ENiCeremonyStep::Spin, CeremonySpinSeconds);
		break;
	}
	case ENiCeremonyStep::Spin:
		// 揭曉：此刻才寫進 GameState（HUD 的受害者欄同步亮起）
		SetCeremonyStep(ENiCeremonyStep::None, 0.0f);
		EnterSeating(PendingVictimId);
		break;
	case ENiCeremonyStep::Approach:
		SetCeremonyStep(ENiCeremonyStep::PickUp, CeremonyPickupSeconds);
		break;
	case ENiCeremonyStep::PickUp:
		// 手已在瓶頸上 ⇒ 改由手驅動瓶子，世界變換不變＝交接不可見
		if (ANiceInkBottle* B = GetOrSpawnBottle())
		{
			B->ServerSetHeld(GetVictimCharacter());
		}
		SetCeremonyStep(ENiCeremonyStep::Drink, CeremonyDrinkSeconds);
		break;
	case ENiCeremonyStep::Drink:
		if (ANiceInkBottle* B = GetOrSpawnBottle())
		{
			B->ServerDrop(); // 醉倒＝瓶先脫手
		}
		SetCeremonyStep(ENiCeremonyStep::Collapse, CeremonyCollapseSeconds);
		break;
	case ENiCeremonyStep::Collapse:
		SetCeremonyStep(ENiCeremonyStep::None, 0.0f);
		BeginVictimSleep(/*bAlreadyLying=*/true);
		OnSeatingDone();
		break;
	default:
		break;
	}
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

	// 入睡儀式（2026-08-16 user 定案「都不要有硬切」）：每一回合的入座酒／罰酒
	// 都走同一段 Approach→PickUp→Drink→Collapse——只做開場＝第二回合起又傳送
	// 落地，正好是要根除的硬切。真正的入睡在 Collapse 結束（BeginVictimSleep）。
	if (bCeremonyEnabled)
	{
		EnsureStageGeometry();
		if (FMath::IsNearlyZero(GS->CeremonySlotOffsetDeg))
		{
			GS->CeremonySlotOffsetDeg = ComputeCeremonySlotOffset();
		}
		GetOrSpawnBottle();
		const float Total = CeremonyApproachSeconds + CeremonyPickupSeconds +
			CeremonyDrinkSeconds + CeremonyCollapseSeconds;
		GS->SetPhase(ENiceInkPhase::Seating, Total);
		SetCeremonyStep(ENiCeremonyStep::Approach, CeremonyApproachSeconds);
		return;
	}

	BeginVictimSleep(/*bAlreadyLying=*/false);
	GS->SetPhase(ENiceInkPhase::Seating, SeatingSeconds);
	SetPhaseTimer(SeatingSeconds, &ANiceInkGameMode::OnSeatingDone);
}

void ANiceInkGameMode::BeginVictimSleep(bool bAlreadyLying)
{
	ANiceInkGameState* GS = NIState();
	if (!GS)
	{
		return;
	}
	const int32 VictimPlayerId = GS->VictimPlayerId;

	if (ANiceInkCharacter* Victim = GetVictimCharacter())
	{
		Victim->MulticastSetRoundIndex(GS->CurrentRound);
		// bAlreadyLying＝崩塌動畫已經把身體放到躺位 ⇒ 跳過傳送（零跳變的最後一哩）
		Victim->ServerSetAsleep(true, GetVictimLieTransform(), bAlreadyLying);

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

		// P0-3 甦醒時間下限（帳本=Docs/ANTICHEAT_PLAN.md §3）：理論最短完成時間的兩個
		// 因子（線長、v_max）都是 server 這裡算的——用 server 端 Victim 屬性＝客戶端改
		// 自己的旋鈕不影響判決；失敗重來/搖晃只會更久＝下限恆保守。
		const float VMax = Victim->TattooMaxSpeedCmPerSec();
		TraceWakeEarliestTime = (VMax > KINDA_SMALL_NUMBER && TraceParams.PerimeterCm > 0.0f)
			? GetWorld()->GetTimeSeconds() + TraceParams.PerimeterCm / VMax * TraceWakeFloorFactor
			: 0.0f;

		const int32 TraceSeed = DebugForcedTraceSeed > 0
			? DebugForcedTraceSeed
			: FMath::RandRange(1, MAX_int32 - 1);
		Victim->ClientStartTrace(TraceSeed, TraceParams);
	}
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

	// P1：判定已定局＝向公證後端換本輪結算單（其後的 Persist 點消費；
	// 1.2s 儀式窗天然吸收 HTTP 往返）
	RequestRoundAttest();

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
		// P0-1：robo 線畫也走 slot（線上識別統一不露真名；server 畫布由 ResolveDrawSlot 換回）
		const int32 WireId = GetOrAssignDrawSlot(Artist);
		// robo 線畫維持折線語義（bDotStroke=false）＋液線針＋滿流量（空陣列）；
		// StrokeSeq=0＝server 發起、無任何端預畫＝回播對消永不觸發
		Victim->MulticastPaintBegin(WireId, Color, FromUV, /*bDotStroke=*/false,
			EInkNeedle::Liner, /*Flow=*/255, /*StrokeSeq=*/0);
		TArray<FVector2D> Points;
		for (int32 Step = 1; Step <= 10; ++Step)
		{
			Points.Add(FMath::Lerp(FromUV, ToUV, Step / 10.0f));
		}
		Victim->MulticastPaintPoints(WireId, Points, TArray<uint8>());
		Victim->MulticastPaintEnd(WireId);
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

void ANiceInkGameMode::DebugRoboWake()
{
	FTimerHandle Unused;
	GetWorldTimerManager().SetTimer(Unused, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (ANiceInkCharacter* Victim = GetVictimCharacter())
		{
			Victim->ServerOpenEyesNow();
		}
	}), 0.1f, false);
}

// --- 防作弊 P0（2026-08-31；帳本=Docs/ANTICHEAT_PLAN.md §3）---

int32 ANiceInkGameMode::GetOrAssignDrawSlot(ANiceInkCharacter* Artist)
{
	const int32 AuthorId = Artist ? Artist->GetInkAuthorId() : INDEX_NONE;
	if (AuthorId == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	const ANiceInkGameState* GS = NIState();
	const int32 Round = GS ? GS->CurrentRound : 0;
	if (Round != DrawSlotRound)
	{
		// 回合換代即重洗：受害者跨回合對 slot 做風格對賬也拼不回身分
		DrawSlotByAuthor.Reset();
		AuthorByDrawSlot.Reset();
		DrawSlotRound = Round;
	}
	if (const int32* Found = DrawSlotByAuthor.Find(AuthorId))
	{
		Artist->DrawSlotId = *Found;
		return *Found;
	}
	// slot 域 1000~8999：與負值證據鍵、小整數 PlayerId（跨場 RestoreWork 真名）天然分區
	int32 Slot = INDEX_NONE;
	do
	{
		Slot = FMath::RandRange(1000, 8999);
	} while (AuthorByDrawSlot.Contains(Slot));
	DrawSlotByAuthor.Add(AuthorId, Slot);
	AuthorByDrawSlot.Add(Slot, AuthorId);
	Artist->DrawSlotId = Slot;
	return Slot;
}

int32 ANiceInkGameMode::ResolveDrawSlot(int32 WireId) const
{
	if (const int32* Found = AuthorByDrawSlot.Find(WireId))
	{
		return *Found;
	}
	return WireId;
}

bool ANiceInkGameMode::CanVictimWakeNow() const
{
	return TraceWakeEarliestTime <= 0.0f ||
		(GetWorld() && GetWorld()->GetTimeSeconds() >= TraceWakeEarliestTime);
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
