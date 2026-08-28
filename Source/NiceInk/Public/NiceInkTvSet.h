// 昭和映像管電視（2026-08-27 開場動畫道具）。
// 全程式生成：引擎基本形狀（Cube/Cylinder）拼裝＝零新網格資產；
// 螢幕內容＝CanvasRenderTarget 逐幀畫（和彫紋樣輪播＝遊戲自己的描圖 motif 資料
// ——電視上的刺青就是玩家三十秒後要畫的東西，主題閉環）；
// 低保真（掃描線／閃爍）＝映像管年代感——「美術能力不足」被框翻譯成「做對了」的位置。
// 視覺＝(step, t) 的純函式（各端自算、無複製欄位）；開場後留在場上＝道場家具。
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NiceInkTvSet.generated.h"

UCLASS()
class NICEINK_API ANiceInkTvSet : public AActor
{
	GENERATED_BODY()

public:
	ANiceInkTvSet();
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

	// 2026-08-28 換裝：方塊拼裝退役，櫃體＝SM_TvRadiola
	//（"Radiola from Matrix" by Sirenko，CC-BY-4.0，信用名單必列；
	// SourceAssets/Television_Sirenko/ATTRIBUTION.txt）。
	// RT 節目畫在模型自己的玻璃槽（'TvGlass'，圓角內凹 52.5×44.7cm）——
	// 浮貼平板退役（user 抓實：平板把凸框整片蓋成螢幕）。
	UPROPERTY(VisibleAnywhere, Category = "Nice Ink|Tv")
	TObjectPtr<class UStaticMeshComponent> CabinetMesh;

	// 螢幕 RT（512×384＝4:3 映像管）；UPROPERTY＝GC 錨（Slate/MID 不保 GC 鐵坑同族）
	UPROPERTY()
	TObjectPtr<class UCanvasRenderTarget2D> ScreenRT;

	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> ScreenMid;

	UFUNCTION()
	void DrawScreen(class UCanvas* Canvas, int32 Width, int32 Height);

private:
	// 這一幀的螢幕狀態（Tick 從 GameState 導出、DrawScreen 消費）
	float ScreenOn01 = 1.0f;      // 0=黑；1=播放中
	float Collapse01 = 0.0f;      // 關機白線動畫進度
	double LastDrawTime = -1.0;   // 12Hz 節流（映像管更新率，也省 GPU）

	void DrawBroadcastFrame(class UCanvas* Canvas, int32 W, int32 H, double Now) const;
	void DrawTubeVignette(class UCanvas* Canvas, int32 W, int32 H) const;
	void DrawMotif(class UCanvas* Canvas, int32 MotifIdx, float Cx, float Cy,
		float Scale, const FLinearColor& Color, float Thickness) const;
};
