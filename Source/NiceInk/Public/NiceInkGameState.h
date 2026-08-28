#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameState.h"
#include "NiceInkTypes.h"
#include "NiceInkGameState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNiceInkPhaseChanged, ENiceInkPhase, NewPhase);

UCLASS()
class NICEINK_API ANiceInkGameState : public AGameState
{
	GENERATED_BODY()

public:
	ANiceInkGameState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(BlueprintAssignable, Category = "Nice Ink")
	FOnNiceInkPhaseChanged OnPhaseChanged;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Phase, Category = "Nice Ink")
	ENiceInkPhase CurrentPhase = ENiceInkPhase::Lobby;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	int32 CurrentRound = 0;

	// 房間碼（主機建房時生成、全員可見——大廳顯示給朋友唸；無 session 流程
	//（PIE/robo/直連）時為空＝HUD 不畫）
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	FString RoomCode;

	// 房間人數（2026-08-14 定案：房主建房時直接決定這房幾個人 4~6、坐滿關門；
	// PreLogin 執法＋大廳「n/max」顯示。開局門檻 4 是另一顆閘門＝藏在開始鈕）
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	int32 MaxPlayers = 6;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	int32 VictimPlayerId = INDEX_NONE;

	// 巡禮中的傑作（受害者畫布上的 WorkId）與其作者；非巡禮階段為 INDEX_NONE
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	int32 TourWorkId = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	int32 TourWorkNumber = 0; // 第幾幅（1 起算，給 HUD）

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	int32 TourWorkCount = 0;

	// 巡禮清單（tour 順序＝編號 1..N）；指認 UI 與鏡頭聚焦都靠它
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	TArray<int32> TourWorkIdList;

	// 結算中被聚焦的傑作（被選那幅——猜錯時當眾轉碳黑）
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	int32 ResolutionWorkId = INDEX_NONE;

	// 上一次指認的結果（Resolution 演出用）
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	ENiceInkAccusationResult LastAccusationResult = ENiceInkAccusationResult::None;

	// 猜錯時揭曉的真作者（此刻全員都知道那幅是誰畫的）
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	int32 RevealedAuthorId = INDEX_NONE;

	// 終局輸家
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	int32 LoserPlayerId = INDEX_NONE;

	// 計時相位的結束時間（server world time）；非計時相位為 0
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	float PhaseEndServerTime = 0.0f;

	// --- 入睡儀式（2026-08-16）---
	// 客戶端的所有儀式視覺都是 (Step, t, 這幾個參數, 世界幾何) 的**純函式**：
	// 無狀態、無累積、遲到者自動對齊。t = (ServerNow - StepStartTime)/StepDuration。

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	ENiCeremonyStep CeremonyStep = ENiCeremonyStep::None;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	float CeremonyStepStartTime = 0.0f;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	float CeremonyStepDuration = 0.0f;

	// 圍圈幾何（伺服器在儀式開始時算一次後複製——GameMode 只活在伺服器，
	// 但客戶端的走位/姿勢/鏡頭都要這組數字）
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	FVector CeremonyCenter = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	float CeremonyRadiusCm = 160.0f;

	// 崩塌終點（＝GameMode::GetVictimLieTransform）。**必須複製**：客戶端也要跑
	// 同一條翻倒插值，而 GameMode 只活在伺服器
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	FVector CeremonyLieLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	float CeremonyLieYaw = 0.0f;

	// 圍圈角位的旋轉偏移（度）；伺服器掃描擇優後複製＝客戶端零重算分歧
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	float CeremonySlotOffsetDeg = 0.0f;

	// 席位 → 圍圈角位：**保序指派**（席位序恰為道場的角向序——實測 seat0..5 的
	// 方位角 323/37/143/184/217/287 為循環遞增）⇒ 路徑天然不交叉。
	// 回傳該席位在場上的排名（0..N-1）；不在場回 INDEX_NONE。
	UFUNCTION(BlueprintPure, Category = "Nice Ink")
	int32 GetCeremonySlotRank(int32 SeatIndex) const;

	UFUNCTION(BlueprintPure, Category = "Nice Ink")
	float GetCeremonySlotAngleDeg(int32 SeatIndex) const;

	UFUNCTION(BlueprintPure, Category = "Nice Ink")
	FVector GetCeremonySlotLocation(int32 SeatIndex) const;

	// 酒瓶轉動：起點角與終點角（度，**不取模**＝終角含整圈數）。
	// 減速曲線各端自算 ⇒ 逐位相同的終角＝畫面與宣判永不矛盾
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	float BottleStartYaw = 0.0f;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	float BottleEndYaw = 0.0f;

	// 儀式進度 t∈[0,1]（純讀；非儀式期間回 0）
	UFUNCTION(BlueprintPure, Category = "Nice Ink")
	float GetCeremonyAlpha() const;

	// --- 開場動畫幾何（2026-08-27；全部＝CeremonyCenter 的純函式，各端逐位相同）---
	// 電視：舞台中心往北牆方向（−Y）310cm、面朝 +Y（朝觀眾群）。
	// 道場實測（部件包圍盒）：長廳 y −352..406，中心 y=40 ⇒ 電視 y≈−270、離牆 80cm。
	UFUNCTION(BlueprintPure, Category = "Nice Ink")
	FVector GetIntroTvLocation() const;

	// 觀眾席：以電視為圓心、半徑 260cm 的扇形弧（每席 17°、面朝電視）。
	// 席位序＝弧上左→右；空席就是空位，不重排（決定性）。
	UFUNCTION(BlueprintPure, Category = "Nice Ink")
	FVector GetIntroSitLocation(int32 SeatIndex) const;

	UFUNCTION(BlueprintPure, Category = "Nice Ink")
	float GetIntroSitYawDeg(int32 SeatIndex) const;

	// --- 翻身提案（2026-07-15 user 定案：作畫者之一提出、其餘作畫者同意後翻身）---
	// 一次一案；INDEX_NONE＝無提案。表決細節在 GameMode（server-only），這裡只放 HUD 顯示位。

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	int32 FlipProposerId = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	int32 FlipAgreeCount = 0;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	int32 FlipAgreeNeeded = 0;

	// 每開一案 +1：client 用來對「這一案」表態去重
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	int32 FlipProposalSerial = 0;

	// v3.3 註：往復指標小遊戲參數已退役——醉夢迷宮的參數只發給受害者本人
	// （ClientStartMaze），不進 GameState：其餘玩家一無所知是規則本體。

	UFUNCTION(BlueprintCallable, Category = "Nice Ink")
	void SetPhase(ENiceInkPhase NewPhase, float DurationSeconds);

	UFUNCTION(BlueprintPure, Category = "Nice Ink")
	float GetPhaseTimeRemaining() const;

	UFUNCTION(BlueprintPure, Category = "Nice Ink")
	APlayerState* FindPlayerStateById(int32 PlayerId) const;

private:
	UFUNCTION()
	void OnRep_Phase();
};
