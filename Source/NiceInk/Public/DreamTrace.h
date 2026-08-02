#pragma once

#include "CoreMinimal.h"
#include "DreamTrace.generated.h"

// 醉夢描圖（SPEC v4.0 定案 #49；取代醉夢圓形迷宮）——割糖餅圖形的程序化生成。
// 一組＝一個難度檔；GameMode 依受害者罰酒杯數換檔（TraceParamsPerCup[0..2]，
// 酒越深夢越深：路線更長、更彎、帶更窄）。
USTRUCT(BlueprintType)
struct NICEINK_API FDreamTraceParams
{
	GENERATED_BODY()

	// 路線周長（cm；盤面公分＝與割線 v_max 同單位——回合時長的主旋鈕：
	// 純描時間 ≈ 周長 ÷ v_max（1.8cm/s），加失敗/搖晃後即回合長度）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace", meta = (ClampMin = "20", ClampMax = "300"))
	float PerimeterCm = 45.0f;

	// 路線帶半寬（cm）：下針中針心離中線超過此值＝越線＝重來
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace", meta = (ClampMin = "0.2", ClampMax = "2.0"))
	float BandHalfWidthCm = 0.6f;

	// 輪廓諧波域（割糖餅形狀的彎曲複雜度）：r(θ)=R0(1+Σ a_k cos(kθ+φ_k))
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace", meta = (ClampMin = "2", ClampMax = "12"))
	int32 HarmonicMin = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace", meta = (ClampMin = "2", ClampMax = "12"))
	int32 HarmonicMax = 4;

	// 半徑相對振幅總量（Σ|a_k|；<0.5 保證 r 恆正）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace", meta = (ClampMin = "0.0", ClampMax = "0.45"))
	float WobbleAmp = 0.16f;

	// 中線重採樣步長（cm；投影窗與繪製密度的粒度）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float SampleStepCm = 0.25f;
};

// 生成結果（受害者 client 本地；RPC 只送 Seed＋Params＝決定性重建）
struct NICEINK_API FDreamTraceFigure
{
	// 均勻弧長重採樣的閉合中線（cm、原點＝圖心、y 向下同 canvas；首尾不重複、隱含 wrap）
	TArray<FVector2D> Points;
	// 每點累積弧長（ArcS[0]=0；TotalLen＝閉合全長）
	TArray<float> ArcS;
	float TotalLen = 0.0f;
	float MaxAbsR = 0.0f;  // 佈局用（縮放進面板）
	int32 UsedSeed = 0;    // 自交避讓重試後實際使用的種子（決定性）
	int32 Retries = 0;

	bool IsValid() const { return Points.Num() >= 8 && TotalLen > 1.0f; }
};

struct NICEINK_API FDreamTraceGen
{
	// 決定性生成：同 Seed＋Params ⇒ 同圖形。內建自交避讓（帶不得自碰＝投影窗
	// 唯一性保證）：弧距 > 3cm 的兩點歐氏距離必須 ≥ 2.6×BandHalfWidth，違反則
	// 以決定性衍生種子重試（最多 32 次；諧波振幅逐次收斂＝必然收斂到近圓）。
	static void Generate(const FDreamTraceParams& Params, int32 Seed, FDreamTraceFigure& Out);

	static FDreamTraceParams DefaultParamsForCup(int32 Cup);

	// 離線統計（調參儀器；SPEC 待定 #2 的描圖版）：周長/重試率/最小自距分布
	static FString RunStats(const FDreamTraceParams& Params, int32 NumSeeds);
};
