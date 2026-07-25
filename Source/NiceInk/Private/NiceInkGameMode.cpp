#include "NiceInkGameMode.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "InkCanvasComponent.h"
#include "InkTypes.h"
#include "Kismet/GameplayStatics.h"
#include "NiceInkCharacter.h"
#include "NiceInkGameInstance.h"
#include "NiceInkGameState.h"
#include "NiceInkHUD.h"
#include "NiceInkPlayerState.h"
#include "NiceInkSaveGame.h"
#include "TimerManager.h"

ANiceInkGameMode::ANiceInkGameMode()
{
	GameStateClass = ANiceInkGameState::StaticClass();
	PlayerStateClass = ANiceInkPlayerState::StaticClass();
	HUDClass = ANiceInkHUD::StaticClass();
	DefaultPawnClass = ANiceInkCharacter::StaticClass();

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
	// 席位＝入場順序；avatar 先看玩家意向、被佔用則輪派。要在 Super 之前指定，
	// SpawnDefaultPawnFor 讀 SeatIndex 決定出生位置。
	if (ANiceInkPlayerState* PS = NewPlayer ? NewPlayer->GetPlayerState<ANiceInkPlayerState>() : nullptr)
	{
		if (PS->SeatIndex == INDEX_NONE)
		{
			PS->SeatIndex = NextSeatIndex++;

			// listen 主機本人不經 ?Name=（沒有重登入）：從 GameInstance 讀主選單設定。
			// 只在 standalone/packaged（Game world）生效——PIE 維持引擎派名，robo 不受擾。
			if (NewPlayer->IsLocalController() && GetWorld() && GetWorld()->WorldType == EWorldType::Game)
			{
				if (UNiceInkGameInstance* GI = Cast<UNiceInkGameInstance>(GetGameInstance()))
				{
					const FString Wanted = UNiceInkGameInstance::SanitizePlayerName(GI->PlayerDisplayName);
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

	// 跨場資產還原（延遲讓新客戶端的 actor channel 就緒，multicast 才到得了它）
	if (APawn* Pawn = NewPlayer ? NewPlayer->GetPawn() : nullptr)
	{
		TWeakObjectPtr<ANiceInkCharacter> WeakChar = Cast<ANiceInkCharacter>(Pawn);
		FTimerHandle Unused;
		GetWorldTimerManager().SetTimer(Unused, FTimerDelegate::CreateWeakLambda(this, [this, WeakChar]()
		{
			if (ANiceInkCharacter* C = WeakChar.Get())
			{
				RestoreCharacter(C);
			}
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

	// 開賽中不收新客（session 層 bAllowJoinInProgress=false 已擋；
	// 這裡防直連 IP 繞過 session 的路徑）
	const ANiceInkGameState* GS = NIState();
	if (GS && GS->CurrentPhase != ENiceInkPhase::Lobby && GS->CurrentPhase != ENiceInkPhase::PostGame)
	{
		ErrorMessage = TEXT("match in progress");
		return;
	}
	if (GS && GS->PlayerArray.Num() >= 6)
	{
		ErrorMessage = TEXT("room is full");
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
	}
}

// --- 跨場持久化 ---

FString ANiceInkGameMode::SaveSlotFor(const ANiceInkPlayerState* PS) const
{
	if (!PS)
	{
		return TEXT("NiceInk_Unknown");
	}
	// PIE 的玩家名帶隨機尾碼（Willie_desktop-7461A）——剝掉，改用席位穩定鍵
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
}

void ANiceInkGameMode::RestoreCharacter(ANiceInkCharacter* Character)
{
	ANiceInkPlayerState* PS = Character ? Character->GetPlayerState<ANiceInkPlayerState>() : nullptr;
	if (!PS)
	{
		return;
	}

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
	ANiceInkGameState* GS = NIState();
	if (!GS || GS->PlayerArray.Num() < 2)
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

	if (ANiceInkCharacter* Victim = GetVictimCharacter())
	{
		Victim->MulticastSetRoundIndex(GS->CurrentRound);
		Victim->ServerSetAsleep(true, GetVictimLieTransform());

		// 醉夢迷宮：難度檔＝罰酒杯數（酒越深夢越深）；種子每回合新開；
		// 陷阱＝其他玩家（洗牌後與陷阱格一一對應）。只發受害者——其餘玩家一無所知。
		const ANiceInkPlayerState* VictimPS = FindNIPlayerState(VictimPlayerId);
		const int32 Cups = VictimPS ? VictimPS->PenaltyCups : 0;
		const FDreamMazeParams MazeParams = MazeParamsPerCup.Num() > 0
			? MazeParamsPerCup[FMath::Clamp(Cups, 0, MazeParamsPerCup.Num() - 1)]
			: FDreamMazeGen::DefaultParamsForCup(Cups);

		TArray<int32> ArtistIds;
		for (APlayerState* PS : GS->PlayerArray)
		{
			if (PS && PS->GetPlayerId() != VictimPlayerId)
			{
				ArtistIds.Add(PS->GetPlayerId());
			}
		}
		for (int32 i = ArtistIds.Num() - 1; i > 0; --i)
		{
			ArtistIds.Swap(i, FMath::RandRange(0, i));
		}

		const int32 MazeSeed = FMath::RandRange(1, MAX_int32 - 1);
		Victim->ClientStartMaze(MazeSeed, MazeParams, ArtistIds);
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
		// robo 線畫維持折線語義（bDotStroke=false）＋液線針＋滿流量（空陣列）
		Victim->MulticastPaintBegin(AuthorId, Color, FromUV, /*bDotStroke=*/false,
			EInkNeedle::Liner, /*Flow=*/255);
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
