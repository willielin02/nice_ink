#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DreamMaze.h"
#include "DreamMazeComponent.generated.h"

class ANiceInkCharacter;
class UCanvas;

// 醉夢迷宮的模擬狀態（受害者 client 本地）
enum class EDreamMazeSimState : uint8
{
	Inactive,      // 未收到迷宮（終局昏死也是這個態——server 不發迷宮）
	Walking,       // 走迷宮中
	DeathScreen,   // 踩中陷阱：亮兇手名（怒氣要有地址）
	AwaitRotation, // 視野已移到重生點、圓點隱藏——等兇手轉盤（藏在畫面後）
	Rotating       // 旋轉動畫：固定時長、晃動、0 度不可分辨
};

// 醉夢圓形迷宮——受害者 client 端：模擬（導航移動/踩陷阱/存檔/出口）、
// 死亡序列狀態機、旋轉顯示變換、canvas 繪製。
//
// 核心：心智地圖活在螢幕座標系。avatar 位置存迷宮座標；旋轉懲罰只動
// DisplayAngleDeg；游標輸入乘顯示旋轉的逆變換才進迷宮座標——轉完之後
// 玩家操縱的是「他看到的」。迷宮狀態不複製給任何他端（SPEC：其餘玩家一無所知）。
//
// 2026-07-13 操作改版（user 口述規格，先在網頁原型定稿再移植）：
// - 滑鼠牽引移動：滑鼠增量推虛擬游標（人物↔游標拉虛線）、按住左鍵才走；
//   按住右鍵＝瞄準模式（滑鼠讓回轉頭，Q/E 瞄準沿用）——PollLook 同一約定。
// - 廊道中線行走：迷宮內部人物永遠在廊道正中央（軌道網）；中央圓室自由移動。
// - 導航式滑行：任何位置按住左鍵都能走——局部前瞻選「更接近游標點」的走法，
//   斜指牆壁自動沿廊滑行、路口自動轉彎；刻意不做全域尋路（那會替玩家解迷宮）。
// - 陷阱地圖上完全不可見（用命記）；視錐＋牆擋光（射線投射），其餘皆黑暗。
UCLASS(ClassGroup = (NiceInk), meta = (BlueprintSpawnableComponent))
class NICEINK_API UDreamMazeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDreamMazeComponent();

	// 迷宮游標靈敏度（盤面像素／滑鼠增量單位）——2026-07-13 user 定 10
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Maze")
	float MazeCursorSensitivity = 10.0f;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ClientStartMaze 轉呼：由種子決定性重建整座迷宮（server 只送種子＋參數＋兇手名單）
	void StartMaze(int32 Seed, const FDreamMazeParams& Params, const TArray<int32>& TrapOwnerIds);
	void StopMaze();

	// ClientApplyMazeRotation 轉呼（兇手轉盤結果；含 0 度——照播動畫）
	void ApplyRotation(float AngleDeg);

	bool IsMazeActive() const { return bMazeActive; }
	EDreamMazeSimState GetSimState() const { return SimState; }
	int32 GetLastKillerId() const { return LastKillerId; }
	const FDreamMazeParams& GetParams() const { return Layout.Params; }

	// HUD 委派繪製（迷宮盤＝固定圓形面板；線段/多邊形全 canvas 直畫）
	void DrawMazePanel(UCanvas* Canvas, const FVector2D& CenterPx, float RadiusPx);

	// --- robo 除錯（只改本地狀態；RPC 發送一律發生在 TickComponent＝python guard 外） ---

	// 傳送到第 TrapIndex 個陷阱格並觸發（走真實事件鏈：client 偵測→Server RPC→兇手轉盤）
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugTriggerTrap(int32 TrapIndex);

	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugTriggerCheckpoint(int32 CheckpointType); // 0=噴射 1=拳腳

	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugTriggerExit();

	// 機器可讀摘要（robo 斷言用）：狀態/位置/角度/重生點/陷阱表/存檔點/出口/統計
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	FString GetDebugSummary() const;

	UFUNCTION(BlueprintPure, Category = "Nice Ink|Debug")
	float GetDisplayAngleDeg() const { return DisplayAngleDeg; }

private:
	bool bMazeActive = false;
	FDreamMazeLayout Layout;
	TArray<int32> TrapOwners; // 與 Layout.TrapCells 平行（兇手 PlayerId）

	// 廊道中線軌道網＋視錐遮光體（StartMaze 時從 Layout 建）
	FDreamMazeNet Net;
	FDreamMazeOccluders Occluders;

	EDreamMazeSimState SimState = EDreamMazeSimState::Inactive;
	float StateTime = 0.0f;

	FVector2D Pos = FVector2D::ZeroVector;        // 迷宮座標（cell 單位）
	// 2026-07-13 user 定案：存檔點概念取消——被抓到一律回中心原點（＝旋轉不動點，
	// 圓點在旋轉中不再需要隱藏）；噴射/拳腳點＝純技能拾取。RespawnPos 恆為原點
	//（保留欄位＝GetDebugSummary 格式不變，robo 沿用）
	FVector2D RespawnPos = FVector2D::ZeroVector;
	FVector2D HeadingMaze = FVector2D(0.0f, 1.0f);
	FVector2D CursorMazeTarget = FVector2D::ZeroVector; // 游標的迷宮座標目標點（每 tick 更新）
	float DisplayAngleDeg = 0.0f;                 // 顯示旋轉（心臟；跨死亡累積）

	// 軌道行走狀態（bOnRail=false＝中央自由區）
	bool bOnRail = false;
	int32 RailIdx = INDEX_NONE;
	float RailS = 0.0f;

	// 虛擬游標（迷宮盤像素座標，相對盤心；螢幕 y 向下）＋最後一次繪製的盤面轉換
	FVector2D CursorPanel = FVector2D(0.0f, -60.0f);
	FVector2D LastPanelCenter = FVector2D::ZeroVector;
	float LastPanelRadius = 0.0f;
	float LastPanelScale = 1.0f;

	int32 LastKillerId = INDEX_NONE;
	bool bRotationReceived = false;
	float PendingRotationDeg = 0.0f;
	float RotStartAngleDeg = 0.0f;
	TArray<float> WobbleFreq;
	TArray<float> WobblePhase;
	float WobbleAmp = 0.0f;

	bool bSprayVisited = false;
	bool bKickVisited = false;
	bool bInsideSpray = false;
	bool bInsideKick = false;
	bool bExitSent = false;

	// 待處理除錯觸發（tick 內消化，讓 RPC 走遊戲 tick）
	int32 PendingDebugTrap = INDEX_NONE;
	int32 PendingDebugCheckpoint = INDEX_NONE;
	bool bPendingDebugExit = false;

	ANiceInkCharacter* OwnerChar() const;
	void TickWalking(float DeltaTime);
	void HandleTrapTouched(int32 TrapIndex);
	void HandleCheckpointTouched(int32 CheckpointType);
	void TriggerExit();
	void BeginRotationAnim();
	float EvalRotationAnim(float NormalizedT) const;

	// --- 導航式移動（2026-07-13） ---
	void PlaceAtCell(int32 Cell);                       // 傳送到格中心並重掛軌道狀態
	void UpdateCursorHeading();                         // 滑鼠→虛擬游標→視錐朝向（死亡序列中也活著）
	void FreeMove(const FVector2D& Target, float Dist); // 中央自由區：朝游標直走＋沿邊滑＋門口捕捉
	void RailNavigate(const FVector2D& Target, float Dist); // 軌道：局部前瞻貪婪
	float RailLookahead(int32 Ri, float S, int32 Dir, const FVector2D& Target, float La, int32 Depth) const;
};
