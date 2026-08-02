#pragma once

#include "CoreMinimal.h"
#include "DreamMaze.h"
#include "DreamTrace.h"
#include "GameFramework/GameMode.h"
#include "NiceInkTypes.h"
#include "NiceInkGameMode.generated.h"

class ANiceInkCharacter;
class ANiceInkGameState;
class ANiceInkPlayerState;

// SPEC v3.0 回合狀態機（server 權威）。
//
// Lobby → BottleSpin → ┌ Seating → Drawing → Tour → Accusation → Resolution ┐
//                      └───────────（猜對換人／猜錯罰酒再畫）←──────────────┘
//                                                    └ 第三杯 → Finale → PostGame
//
// 作畫階段沒有計時器：受害者按 WASD 現身即收束（定案 #17）。
UCLASS()
class NICEINK_API ANiceInkGameMode : public AGameMode
{
	GENERATED_BODY()

public:
	ANiceInkGameMode();

	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId,
		FString& ErrorMessage) override;
	virtual void Logout(AController* Exiting) override;
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId,
		const FString& Options, const FString& Portal = TEXT("")) override;
	virtual APawn* SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot) override;

	// --- 場地配置（座標系沿用桑拿房實測；L_Dojo 道場已以地板探針驗證全席位落在開放地板，
	//     道場 actor 基準點為此西移 250cm——見 CLAUDE.md 陷阱年鑑「地板探針」條） ---

	// 六個席位（2D；z 由地板探測決定）。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Stage")
	TArray<FVector2D> SeatSpots = {
		FVector2D(155.0f, -40.0f),   // 東長凳
		FVector2D(100.0f, 150.0f),   // 東北地板
		FVector2D(-100.0f, 150.0f),  // 西北地板
		FVector2D(-235.0f, 60.0f),   // 西長凳
		FVector2D(-300.0f, -150.0f), // 西南走道
		FVector2D(100.0f, -250.0f),  // 南側地板
	};

	// 受害者仰躺位置（淨空地板；頭朝 +X）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Stage")
	FVector2D VictimLieSpot = FVector2D(0.0f, 75.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Stage")
	float VictimLieYaw = 0.0f;

	// --- 流程參數 ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Flow")
	bool bAutoStart = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Flow", meta = (ClampMin = "2", ClampMax = "6"))
	int32 MinPlayersToStart = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Flow")
	float AutoStartDelay = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Flow")
	float BottleSpinSeconds = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Flow")
	float SeatingSeconds = 3.0f;

	// 巡禮每幅約 20 秒（SPEC；playtest 調整）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Flow")
	float TourSecondsPerWork = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Flow")
	float ResolutionSeconds = 6.0f;

	// 終局羞辱時間長度
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Flow")
	float FinaleSeconds = 30.0f;

	// 罰酒三杯制（SPEC 定案 #12；與人數無關）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Flow")
	int32 PenaltyCupsToFinale = 3;

	// --- 醉夢圓形迷宮（SPEC v3.3 甦醒小遊戲；待定 #2 全部旋鈕在 FDreamMazeParams） ---

	// 每杯一組難度檔（索引＝受害者當前罰酒杯數——酒越深夢越深）。
	// Config 可由 DefaultGame.ini 覆寫；ini 陣列語意＝先 !MazeParamsPerCup=ClearArray
	// 再逐條 +MazeParamsPerCup=(...)，否則會疊在 ctor 預設之後。
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Maze")
	TArray<FDreamMazeParams> MazeParamsPerCup;

	// SPEC 定案 #31 常數：兇手轉盤 5 秒（非難度旋鈕，改它＝改 SPEC）
	static constexpr float TrapDialSeconds = 5.0f;

	// --- 醉夢描圖（SPEC v4.0 定案 #49/#50；迷宮退役） ---

	// 每杯一組難度檔（索引＝受害者當前罰酒杯數——酒越深夢越深）。
	// Config 覆寫語意同 MazeParamsPerCup（先 !Clear 再逐條 +）。
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Trace")
	TArray<FDreamTraceParams> TraceParamsPerCup;

	// 搖晃攻擊（定案 #50；費用佔位 500＝金額錨定連動 SPEC 待定 #6/#18）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Economy")
	int32 ShakeAttackCost = 500;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Trace")
	float ShakeAttackSeconds = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Trace")
	float ShakeAttackAmpCm = 1.2f;

	// 每攻擊者冷卻（防機關槍連砸；金錢是主限流、冷卻是節拍保底）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Trace")
	float ShakeAttackCooldownSec = 4.0f;

	// 搖晃攻擊路由（Character 的 Server RPC 轉進來）：驗相位/身分/現金/冷卻
	// →扣款→受害者端套用（攻擊者顯名）；回執只給攻擊者
	void HandleShakeAttack(class ANiceInkCharacter* Attacker);

	// --- 迷宮事件路由（Character 的 Server RPC 轉進來） ---

	// 受害者踩中陷阱：驗證後只通知兇手開轉盤＋掛失效保險（逾時/掉線＝0 度）
	void HandleMazeTrapHit(class ANiceInkCharacter* Victim, int32 KillerPlayerId);
	void HandleTrapDialSubmit(class ANiceInkCharacter* Killer, float AngleDeg);

	// --- 玩家角色的入口 ---

	void RequestStartMatch();
	void HandleEmergeRequest(ANiceInkCharacter* Requester, bool bForce = false);
	void HandleAccusation(ANiceInkCharacter* Accuser, int32 WorkId, int32 AccusedPlayerId);

	// 場間大廳：雷射自己最舊的碳黑一級（自費；三級清除；永久無效）
	void HandleLaserRequest(ANiceInkCharacter* Requester);

	// 翻身提案（2026-07-15 user 定案）：作畫者之一提出、「其餘的人」＝
	// 全體非受害者玩家全數同意後翻身；一次一案、逾時作廢；非 ragdoll。
	void HandleFlipPropose(ANiceInkCharacter* Proposer);
	void HandleFlipAgree(ANiceInkCharacter* Agreer);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Economy")
	int32 LaserCostPerPass = 2000;

	// 作畫許可（server 權威）：作畫階段畫受害者；終局羞辱時間畫輸家。
	bool CanPaintOn(const ANiceInkCharacter* Painter, const ANiceInkCharacter* Target) const;

	// --- Robo-test 鉤子（python 驅動的自動驗證用）---
	// python 執行期間 FEditorScriptExecutionGuard 會把 RPC 全部壓成本地執行，
	// multicast 出不了網；這些鉤子把動作排進 timer，回呼在遊戲 tick 裡跑（guard 外），
	// RPC 走正常網路路徑。也是後續里程碑的 headless 測試工具。

	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugRoboStroke(FVector2D FromUV, FVector2D ToUV, int32 ColorIndex);

	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugRoboEmerge();

	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugRoboAccuse(bool bCorrect);

	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugRoboSpray(float AimYawWorld, uint8 OriginType);

	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugRoboKick(float AimYawWorld);

	// 迷宮：代兇手送轉盤度數（timer-deferred；robo 驗證受害者端旋轉用）
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugRoboMazeDial(float AngleDeg);

	// 翻身：跳過表決直接執行（timer-deferred；robo 驗證背面姿勢/翻面後作畫用）
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugRoboFlip();

	// 迷宮生成統計（純計算、無 RPC——python 可直呼）；報表字串回傳＋進 log
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	FString DebugMazeStats(int32 NumSeeds, int32 Cup);

	// 描圖生成統計（v4.0 調參儀器；純計算可直呼）
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	FString DebugTraceStats(int32 NumSeeds, int32 Cup);

	// 搖晃攻擊（timer-deferred；以第一位非受害者玩家為攻擊者走真實 Handle 路徑）
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugRoboShake();

	// robo 測試：指定開場受害者的席位（-1＝隨機，正式行為）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Debug")
	int32 DebugForcedVictimSeat = -1;

private:
	FTimerHandle PhaseTimerHandle;
	FTimerHandle AutoStartTimerHandle;

	int32 NextSeatIndex = 0;
	TArray<int32> TourWorkIds;
	int32 TourCursor = 0;

	// Resolution 演出後要接的分支
	int32 PendingNextVictimId = INDEX_NONE;
	bool bPendingFinale = false;

	// 搖晃攻擊冷卻表（server-only；每回合 EnterSeating 清空）
	TMap<int32, float> LastShakeTimeByPlayer;

	// 兇手轉盤 pending（一次一件；受害者死亡序列中不會再踩）
	int32 PendingDialKillerId = INDEX_NONE;
	TWeakObjectPtr<ANiceInkCharacter> PendingDialVictim;
	FTimerHandle DialFailsafeHandle;
	void ResolveTrapDial(float AngleDeg);

	// 翻身表決（server-only；顯示位在 GameState）
	TSet<int32> FlipAgreedIds;
	FTimerHandle FlipTimeoutHandle;
	void MaybeExecuteFlip();
	void ClearFlipProposal();

	ANiceInkGameState* NIState() const;
	ANiceInkCharacter* GetVictimCharacter() const;
	ANiceInkPlayerState* FindNIPlayerState(int32 PlayerId) const;

	FTransform GetSeatTransform(int32 SeatIndex) const;
	FTransform GetVictimLieTransform() const;
	float ProbeFloorZ(const FVector& At) const;

	// avatar 派發：優先玩家意向（DesiredAvatarIndex），被佔用則從席位起輪派空位
	int32 PickAvatarFor(const class ANiceInkPlayerState* PS) const;

	void MaybeScheduleAutoStart();
	void EnterBottleSpin();
	void OnBottleSpinDone();
	void EnterSeating(int32 VictimPlayerId);
	void OnSeatingDone();
	void EnterTour();
	void AdvanceTour();
	void EnterAccusation();
	void OnResolutionDone();
	void EnterFinale();
	void OnFinaleDone();

	// 回合結算清場：全員洗麥克筆與證據標記、解除致盲（SPEC：指認結算時一同洗掉）
	void RoundCleanupAllCharacters();

	// 斷線兜底：受害者中離／人數不足＝本回合作廢（洗掉、清 pending），
	// 人夠→重新轉瓶續攤；不夠→回大廳等人。
	void AbortRound(bool bEnoughPlayers);

	// 相位切換時全員強制起身（貼臉鎖定不跨相位）
	void ForceExitAllLeans();

	// 跨場持久化（錢包＋刺青）。存檔鍵＝玩家名（去 PIE 尾碼）＋席位；
	// 正式版改 EOS product user id。
	FString SaveSlotFor(const class ANiceInkPlayerState* PS) const;
	void PersistCharacter(ANiceInkCharacter* Character);
	void RestoreCharacter(ANiceInkCharacter* Character);

	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void PersistAllCharacters();

	void SetPhaseTimer(float Seconds, void (ANiceInkGameMode::*Handler)());
};
