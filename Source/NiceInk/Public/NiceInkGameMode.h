#pragma once

#include "CoreMinimal.h"
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
	virtual APawn* SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot) override;

	// --- 場地配置（L_Sauna 實測：房間中央是火爐，淨空地板在北側與西側走道） ---

	// 六個席位（2D；z 由地板探測決定）。實測避開火爐與牆外。
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

	// --- 甦醒小遊戲參數（playtest 旋鈕；SPEC 待定 #2） ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Minigame")
	float MinigamePeriodSeconds = 1.6f;

	// zone 佔軸比例，索引＝當前罰酒杯數（酒越深睡越久、被畫越滿）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Minigame")
	TArray<float> MinigameZoneWidthByCup = { 0.12f, 0.09f, 0.06f };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Minigame")
	float MinigameMissCooldownSeconds = 10.0f;

	// --- 玩家角色的入口 ---

	void RequestStartMatch();
	void HandleEmergeRequest(ANiceInkCharacter* Requester, bool bForce = false);
	void HandleAccusation(ANiceInkCharacter* Accuser, int32 WorkId, int32 AccusedPlayerId);

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

	ANiceInkGameState* NIState() const;
	ANiceInkCharacter* GetVictimCharacter() const;
	ANiceInkPlayerState* FindNIPlayerState(int32 PlayerId) const;

	FTransform GetSeatTransform(int32 SeatIndex) const;
	FTransform GetVictimLieTransform() const;
	float ProbeFloorZ(const FVector& At) const;

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

	void SetPhaseTimer(float Seconds, void (ANiceInkGameMode::*Handler)());
};
