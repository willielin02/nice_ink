#pragma once

#include "CoreMinimal.h"
#include "DreamTrace.generated.h"

// 醉夢描圖（SPEC v4.0 定案 #49；取代醉夢圓形迷宮）——一筆畫圖案。
// 圖案來源（v4.0a 三段、user 裁決「圖要來自現成向量圖源」）＝Twemoji（CC BY 4.0）
// 十六式日式標的，經 Tools/AssetPrep/TraceMotifs 管線（剪影聯集＋內輪廓接駁＋
// 三道可描性閘）烘焙為 DreamTraceMotifData.h；手雕模板全數退役（程式雕美學
// =鐵則違規的實錘）。設計程序＝先定「不受干擾平均完成時間」（統一 60s）→
// 依針速 v_max 導出線長；帶寬＝筆寬×2（user 定案；帶半寬=筆寬、從筆寬導出）。
USTRUCT(BlueprintType)
struct NICEINK_API FDreamTraceParams
{
	GENERATED_BODY()

	// 不受干擾的目標純描時間（s）——設計輸入的第一位。>0 時 GameMode 於發夢
	// 當下用受害者的 TattooMaxSpeedCmPerSec() 算 PerimeterCm＝T×v_max 後下發
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace", meta = (ClampMin = "0", ClampMax = "180"))
	float TargetTraceSeconds = 60.0f;

	// 線長（cm）。導出量（時間×針速）；欄位保留供 config 直接覆寫（Target=0 時生效）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace", meta = (ClampMin = "20", ClampMax = "300"))
	float PerimeterCm = 108.0f;

	// 帶全寬＝筆寬×此倍數（user 定案 08-02：2.0／1.8／1.6 隨杯數——酒越深帶越窄）。
	// GameMode 發夢當下用受害者 TattooNibDiameterCm 換算 BandHalfWidthCm
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace", meta = (ClampMin = "1.0", ClampMax = "4.0"))
	float BandWidthNibMult = 2.0f;

	// 路線帶半寬（cm）＝筆寬×倍數÷2 的導出值（發夢當下計算；此欄為名義預設）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float BandHalfWidthCm = 0.3f;

	// 本難度檔的圖案池（烘焙表索引 NiceInkTraceMotifs::EIdx；每回合由種子選一、
	// 允許鏡像的圖隨種子翻）；空＝Blob 諧波閉圓保底
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace")
	TArray<int32> BakedPool;

	// Blob 保底家族參數（烘焙表不可用時的決定性退路）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace", meta = (ClampMin = "2", ClampMax = "12"))
	int32 HarmonicMin = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace", meta = (ClampMin = "2", ClampMax = "12"))
	int32 HarmonicMax = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace", meta = (ClampMin = "0.0", ClampMax = "0.45"))
	float WobbleAmp = 0.16f;

	// 中線重採樣步長（cm；投影窗與繪製密度的粒度）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trace", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float SampleStepCm = 0.25f;
};

// 生成結果（受害者 client 本地；RPC 只送 Seed＋Params＝決定性重建）
struct NICEINK_API FDreamTraceFigure
{
	// 均勻弧長重採樣的中線（cm、原點＝圖心、y 向下同 canvas）。
	// bClosed：閉合＝首尾不重複、隱含 wrap；開放一筆畫＝Points[0] 起點、Last 終點
	TArray<FVector2D> Points;
	TArray<float> ArcS;
	float TotalLen = 0.0f;
	bool bClosed = true;
	int32 MotifIndex = INDEX_NONE; // 烘焙表索引；INDEX_NONE＝Blob 保底
	bool bMirrored = false;
	float MaxAbsR = 0.0f;
	int32 UsedSeed = 0;
	int32 Retries = 0;

	bool IsValid() const { return Points.Num() >= 8 && TotalLen > 1.0f; }
};

struct NICEINK_API FDreamTraceGen
{
	// 決定性生成：同 Seed＋Params ⇒ 同圖形（烘焙表選圖＋鏡像；表空/失效＝Blob）
	static void Generate(const FDreamTraceParams& Params, int32 Seed, FDreamTraceFigure& Out);

	static FDreamTraceParams DefaultParamsForCup(int32 Cup);

	// 離線統計（調參儀器）：自距/pursuit 可描性/池覆蓋
	static FString RunStats(const FDreamTraceParams& Params, int32 NumSeeds);
};
