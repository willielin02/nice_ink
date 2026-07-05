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

	// --- 場地配置（L_Sauna 的環形席位；沿用六人擺位橢圓） ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Stage")
	FVector RingCenter = FVector(-105.0f, -40.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Stage")
	float RingRadiusX = 260.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Stage")
	float RingRadiusY = 200.0f;

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

	// --- 玩家角色的入口 ---

	void RequestStartMatch();
	void HandleEmergeRequest(ANiceInkCharacter* Requester);
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
