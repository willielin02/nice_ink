#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NiceInkMenuStage.generated.h"

class ACameraActor;
class ADirectionalLight;
class AAIController;
class ANiceInkCharacter;
class UBoxComponent;

// 主選單舞台（2026-08-06 user 定案：半透明按鈕後面＝自己的力士跟著 BGM 跳舞）。
// 全程式生成（零關卡資產）：隱形地板＋相機＋無影平行光×2＋力士替身＋編舞。
// 舞步規格（user 原話）：搭配音樂用現有移動彈跳為主效果、跟著音樂左右橫移讓肉晃、
// 必要時轉圈或調整朝向——一切用現有移動系統（摺り足步態＋Jiggle 彈簧自然發生）。
UCLASS()
class NICEINK_API ANiceInkMenuStage : public AActor
{
	GENERATED_BODY()

public:
	ANiceInkMenuStage();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// --- 編舞旋鈕（拍長=user 耳測定案 67-68 BPM＝偵測值 0.44425 的兩倍頻；
	// 相位/loop 漂移=beat_probe.py 實測：首拍 0.2421s、loop≠整數拍每圈 +204ms
	// →取模修正。舞步=user 定案 2026-08-06：四拍一循環——兩拍由左到右
	//（從最左開始）、兩拍由右到左，全程面對玩家側身移動）---
	UPROPERTY(EditAnywhere, Category = "Nice Ink|Stage")
	float BeatSec = 0.88849f;     // 拍長（67.53 BPM；user 耳測 67-68 定案）

	UPROPERTY(EditAnywhere, Category = "Nice Ink|Stage")
	float BeatOffsetS = 0.2421f;  // 拍網格相位（首拍在檔案時間）

	UPROPERTY(EditAnywhere, Category = "Nice Ink|Stage")
	float LoopDurS = 100.6039f;   // loop 全長（取模防跨圈漂移）

	UPROPERTY(EditAnywhere, Category = "Nice Ink|Stage")
	int32 BeatsPerSide = 2;       // 單程拍數（2 拍走完一個方向＝四拍一來回）

	UPROPERTY(EditAnywhere, Category = "Nice Ink|Stage")
	float SwayCm = 180.0f;        // 橫移半幅（±；全寬舞台：畫面半寬 232cm 留 15% 邊距
	                              // ——每趟穿過毛玻璃卡後面，主角用整個舞台）

	UPROPERTY(EditAnywhere, Category = "Nice Ink|Stage")
	int32 SpinEveryBeats = 0;     // 0=不轉（user 定案「全程面對玩家」；旋鈕留未來變化拍）

	UPROPERTY(EditAnywhere, Category = "Nice Ink|Stage")
	float CamDistCm = 560.0f;

	UPROPERTY(EditAnywhere, Category = "Nice Ink|Stage")
	float CamHeightCm = 108.0f;

	// UE FOV＝水平角（16:9 下 45°≈垂直 26°；36 是望遠壓臉——首輪實錘）
	UPROPERTY(EditAnywhere, Category = "Nice Ink|Stage")
	float CamFovDeg = 45.0f;

	// mesh 前向＝actor -X（首輪截圖背對鏡頭實錘）→面向 +X 相機＝yaw 180
	UPROPERTY(EditAnywhere, Category = "Nice Ink|Stage")
	float FaceCameraYaw = 180.0f;

	UPROPERTY(EditAnywhere, Category = "Nice Ink|Stage")
	float KeyLux = 3.6f;          // 主光（無影；fullbright 讀感）

	UPROPERTY(EditAnywhere, Category = "Nice Ink|Stage")
	float FillLux = 2.4f;         // 補光（反向；2560 實測 1.6 時腿/褌沉進黑背景）

	UPROPERTY(EditAnywhere, Category = "Nice Ink|Stage")
	float ExposureBias = 9.6f;    // 手動曝光（鎖死＝不隨畫面泵動）

private:
	UPROPERTY() TObjectPtr<UBoxComponent> Floor;
	UPROPERTY() TObjectPtr<ANiceInkCharacter> Dancer;
	UPROPERTY() TObjectPtr<AAIController> DancerAI;
	UPROPERTY() TObjectPtr<ACameraActor> Camera;

	int32 LastBeat = -1;
	float SpinYawRemaining = 0.0f; // >0＝旋轉中（每拍觸發後遞減到 0）
	float CurrentYaw = 180.0f;     // 面向相機（見 FaceCameraYaw）

	// persona 穿戴輪詢（SPEC #52 v4.0e：舞台力士＝自己的臉＋自己的刺青）
	int32 AppliedFaceRev = 0;      // 已套用的自訂臉版本（0=尚未；換臉即重套）
	bool bCloudTattoosApplied = false;
	void DressDancerFromPersona();

	double BeatClock(const UWorld* World) const;
};
