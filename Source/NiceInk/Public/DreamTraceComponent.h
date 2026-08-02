#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DreamTrace.h"
#include "DreamTraceComponent.generated.h"

class ANiceInkCharacter;
class UCanvas;

// 醉夢描圖（SPEC v4.0 定案 #49/#50）——受害者 client 端：
// 割線針機制 2D 移植（滑鼠＝意圖點、針以 v_max 恆速追趕、皮繩鉗、按住左鍵下針）、
// 越線判定（下針中針心離中線 > 帶半寬＝失敗：線全洗、針回起點、同圖重描）、
// 搖晃攻擊顯示與判定（圖形搖、針不搖＝圖下的針被相對位移；抬針＝安全）、
// canvas 繪製。狀態不複製給任何他端（作畫者看不到夢的進度＝張力來源）。
//
// 座標系（全部 cm、y 向下同 canvas）：
// - 盤面空間（panel）：針與游標活在這裡——搖晃時針在螢幕上不動（針＝你的手）。
// - 圖形空間（figure）＝盤面 − 搖晃偏移：路線帶、已描的線、投影/越線判定都在
//   這裡——圖搖走、手沒跟上＝針在圖上滑出帶＝重來。
UCLASS(ClassGroup = (NiceInk), meta = (BlueprintSpawnableComponent))
class NICEINK_API UDreamTraceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDreamTraceComponent();

	// 視覺/手感同源三定律（08-02 user 定案「視覺速度和手感都和割線筆一樣」）：
	// ①放大率同源＝夢的 px/cm 用割線視圖公式（FOV 垂直半角×名義眼距）算——
	//   同一公分在螢幕上一樣大、1.8cm/s 的針視覺速度與貼膚割線恆等；
	// ②畫面歸針＝針釘在螢幕中心、圖在下面滑（割線的相機定律原樣進夢）；
	// ③游標增益同源＝cm/滑鼠單位從 DrawAimSensitivity() 換算，非獨立旋鈕。
	// 名義眼距（cm）＝割線鎖定的眼-膚距離帶（69~86）取中
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Trace")
	float NominalDreamEyeDistCm = 75.0f;

	// 口味微調（乘在同源增益上；1.0=與割線恆等）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Trace")
	float TraceCursorGain = 1.0f;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ClientStartTrace 轉呼：由種子決定性重建圖形（server 只送種子＋參數）
	void StartTrace(int32 Seed, const FDreamTraceParams& Params);
	void StopTrace();

	// ClientApplyShake 轉呼（搖晃攻擊；受害者顯名＝怒氣要有地址）
	void ApplyShake(const FString& AttackerName, float Seconds, float AmpCm);

	bool IsTraceActive() const { return bActive; }
	bool IsShakeActive() const;
	FString GetShakeAttackerName() const { return ShakeAttackerName; }
	float GetProgress01() const { return Figure.TotalLen > 1.0f ? FMath::Abs(ProgressS) / Figure.TotalLen : 0.0f; }
	int32 GetFailCount() const { return FailCount; }
	bool IsFailFlashing() const;

	// HUD 委派繪製（圖形包圍盒貼合可用矩形＝整圖入鏡下的最大化；全 canvas 三角形直畫）
	void DrawTracePanel(UCanvas* Canvas, const FVector2D& CenterPx, const FVector2D& HalfSizePx);

	// --- robo 除錯（只設 pending；RPC 發送一律發生在 TickComponent＝python guard 外） ---

	// 自動沿線描 Seconds 秒（每 tick 游標＝針前方路線點、左鍵按住＝真實追趕/判定路徑）
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugAutopilot(float Seconds);

	// 刻意垂直偏出路線帶 Seconds 秒（越線＝重來的契約驗證）
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugVeerOff(float Seconds);

	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugSetPaint(bool bHeld);

	// 直接送出完成（等同舊迷宮 DebugTriggerExit：甦醒鏈驗證／迴歸套件喚醒用）
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugForceComplete();

	// 機器可讀摘要（robo 斷言用）
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	FString GetDebugSummary() const;

private:
	bool bActive = false;
	FDreamTraceFigure Figure;
	FDreamTraceParams Params;

	// 圖形包圍盒（圖形 cm 空間；StartTrace 時一次算好——繪製縮放與游標鉗位共用）
	FVector2D FigCenterCm = FVector2D::ZeroVector;
	FVector2D FigHalfCm = FVector2D(1.0f, 1.0f);

	// 針與游標（盤面 cm 空間）
	FVector2D NeedlePanel = FVector2D::ZeroVector;
	FVector2D CursorPanel = FVector2D::ZeroVector;
	bool bPenDown = false;
	bool bPrevPenDown = false;

	// 路線進度（圖形空間）：CurIdx＝目前最近段索引（投影窗中心）、
	// ProgressS＝從起點沿線的帶號累積弧長（|ProgressS| ≥ TotalLen ＝ 描完）
	int32 CurIdx = 0;
	float CurS = 0.0f;
	float ProgressS = 0.0f;

	// 已描的線（圖形空間；越線重來時全洗）
	TArray<FVector2D> InkFig;

	int32 FailCount = 0;
	float FailFlashUntil = 0.0f;
	bool bCompleteSent = false;
	bool bPendingComplete = false;

	// 搖晃（client 本地視覺＋判定偏移）
	float ShakeStartTime = -1000.0f;
	float ShakeEndTime = -1000.0f;
	float ShakeAmpCm = 0.0f;
	FString ShakeAttackerName;

	// 除錯 pending（tick 內消化）
	float AutopilotRemaining = 0.0f;
	float VeerRemaining = 0.0f;
	bool bDebugPaintHeld = false;
	bool bPendingForceComplete = false;

	ANiceInkCharacter* OwnerChar() const;
	float Now() const;
	FVector2D ShakeOffsetCm() const;
	// 投影窗：在 CurIdx±窗內找針（圖形空間）的最近路線點；回傳最近距離、更新 CurIdx/CurS
	float ProjectNeedle(const FVector2D& NeedleFig);
	void FailReset();
	FVector2D RoutePointAtArc(float S) const; // 弧長→中線點（wrap）
};
