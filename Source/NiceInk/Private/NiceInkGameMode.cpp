#include "NiceInkGameMode.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "InkCanvasComponent.h"
#include "InkTypes.h"
#include "Kismet/GameplayStatics.h"
#include "NiceInkCharacter.h"
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
}

void ANiceInkGameMode::PostLogin(APlayerController* NewPlayer)
{
	// 席位＝入場順序；avatar 依席位輪流取用內建名冊。要在 Super 之前指定，
	// SpawnDefaultPawnFor 讀 SeatIndex 決定出生位置。
	if (ANiceInkPlayerState* PS = NewPlayer ? NewPlayer->GetPlayerState<ANiceInkPlayerState>() : nullptr)
	{
		if (PS->SeatIndex == INDEX_NONE)
		{
			PS->SeatIndex = NextSeatIndex++;
			PS->AvatarIndex = PS->SeatIndex % FNiceInkAvatars::Num();
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
	ANiceInkGameState* GS = NIState();
	if (!bAutoStart || !GS || GS->CurrentPhase != ENiceInkPhase::Lobby)
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

	// 小遊戲難度＝罰酒杯數（酒越深 zone 越窄）
	const ANiceInkPlayerState* VictimPS = FindNIPlayerState(VictimPlayerId);
	const int32 Cups = VictimPS ? VictimPS->PenaltyCups : 0;
	GS->MinigamePeriod = MinigamePeriodSeconds;
	GS->MinigameMissCooldown = MinigameMissCooldownSeconds;
	GS->MinigameZoneWidth = MinigameZoneWidthByCup.Num() > 0
		? MinigameZoneWidthByCup[FMath::Clamp(Cups, 0, MinigameZoneWidthByCup.Num() - 1)]
		: 0.12f;

	if (ANiceInkCharacter* Victim = GetVictimCharacter())
	{
		Victim->MulticastSetRoundIndex(GS->CurrentRound);
		Victim->ServerSetAsleep(true, GetVictimLieTransform());
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

	// 現身的前提＝無聲甦醒（第三次小遊戲成功）；robo 測試可強制
	if (!bForce && Requester->MinigameHits < 3)
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
		Victim->MulticastPaintBegin(AuthorId, Color, FromUV);
		TArray<FVector2D> Points;
		for (int32 Step = 1; Step <= 10; ++Step)
		{
			Points.Add(FMath::Lerp(FromUV, ToUV, Step / 10.0f));
		}
		Victim->MulticastPaintPoints(AuthorId, Points);
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
