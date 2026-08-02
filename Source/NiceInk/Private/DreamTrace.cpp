#include "DreamTrace.h"

namespace
{
	// 稠密取樣步數（θ 域；重採樣前的形狀解析度）
	constexpr int32 ThetaSteps = 720;
	constexpr int32 MaxRetries = 32;

	// 自交避讓約束（帶不得自碰＝投影窗唯一性）：弧距 > SelfArcSepCm 的兩點
	// 歐氏距離必須 ≥ SelfDistFactor × BandHalfWidth
	constexpr float SelfArcSepCm = 3.0f;
	constexpr float SelfDistFactor = 2.6f;

	// 單次嘗試：由諧波組合建形＋縮放到目標周長＋均勻弧長重採樣
	void BuildAttempt(const FDreamTraceParams& P, int32 Seed, float WobbleScale, FDreamTraceFigure& Out)
	{
		FRandomStream RS(Seed);

		const int32 HMin = FMath::Clamp(P.HarmonicMin, 2, 12);
		const int32 HMax = FMath::Clamp(FMath::Max(P.HarmonicMax, HMin), HMin, 12);

		// 振幅配額：Σ|a_k| = WobbleAmp×WobbleScale（低頻多分＝大彎、高頻少分＝細節）
		TArray<float> Amp;
		TArray<float> Phase;
		float Total = 0.0f;
		for (int32 K = HMin; K <= HMax; ++K)
		{
			const float Raw = RS.FRandRange(0.5f, 1.0f) / static_cast<float>(K);
			Amp.Add(Raw);
			Phase.Add(RS.FRandRange(0.0f, 2.0f * PI));
			Total += Raw;
		}
		const float Budget = FMath::Clamp(P.WobbleAmp, 0.0f, 0.45f) * WobbleScale;
		for (float& A : Amp)
		{
			A = Total > KINDA_SMALL_NUMBER ? A / Total * Budget : 0.0f;
		}

		// 稠密取樣（R0=1）＋周長量測
		TArray<FVector2D> Dense;
		Dense.Reserve(ThetaSteps);
		for (int32 i = 0; i < ThetaSteps; ++i)
		{
			const float Theta = 2.0f * PI * static_cast<float>(i) / static_cast<float>(ThetaSteps);
			float R = 1.0f;
			for (int32 k = 0; k < Amp.Num(); ++k)
			{
				R += Amp[k] * FMath::Cos((HMin + k) * Theta + Phase[k]);
			}
			Dense.Add(FVector2D(R * FMath::Cos(Theta), R * FMath::Sin(Theta)));
		}
		float RawLen = 0.0f;
		for (int32 i = 0; i < Dense.Num(); ++i)
		{
			RawLen += FVector2D::Distance(Dense[i], Dense[(i + 1) % Dense.Num()]);
		}
		const float Scale = RawLen > KINDA_SMALL_NUMBER ? P.PerimeterCm / RawLen : 1.0f;
		for (FVector2D& Pt : Dense)
		{
			Pt *= Scale;
		}

		// 均勻弧長重採樣
		const float Step = FMath::Clamp(P.SampleStepCm, 0.1f, 1.0f);
		Out.Points.Reset();
		Out.ArcS.Reset();
		float Walked = 0.0f;   // 距上一輸出點的殘距
		float Acc = 0.0f;      // 累積弧長
		Out.Points.Add(Dense[0]);
		Out.ArcS.Add(0.0f);
		for (int32 i = 0; i < Dense.Num(); ++i)
		{
			const FVector2D A = Dense[i];
			const FVector2D B = Dense[(i + 1) % Dense.Num()];
			const float SegLen = FVector2D::Distance(A, B);
			if (SegLen < KINDA_SMALL_NUMBER)
			{
				continue;
			}
			float Consumed = 0.0f;
			while (Walked + (SegLen - Consumed) >= Step)
			{
				const float Need = Step - Walked;
				Consumed += Need;
				Acc += Need;
				const FVector2D Pt = A + (B - A) * (Consumed / SegLen);
				// 尾端貼近起點時不再輸出（閉合 wrap 由邏輯層處理）
				if (P.PerimeterCm - Acc > Step * 0.5f)
				{
					Out.Points.Add(Pt);
					Out.ArcS.Add(Acc);
				}
				Walked = 0.0f;
			}
			Acc += (SegLen - Consumed);
			Walked += (SegLen - Consumed);
		}
		Out.TotalLen = P.PerimeterCm;

		Out.MaxAbsR = 0.0f;
		for (const FVector2D& Pt : Out.Points)
		{
			Out.MaxAbsR = FMath::Max(Out.MaxAbsR, static_cast<float>(Pt.Size()));
		}
	}

	// 自交檢查：回傳「弧距>SelfArcSepCm 的點對」的最小歐氏距離
	float MinSelfDistance(const FDreamTraceFigure& F)
	{
		float MinD = TNumericLimits<float>::Max();
		const int32 N = F.Points.Num();
		for (int32 i = 0; i < N; ++i)
		{
			for (int32 j = i + 1; j < N; ++j)
			{
				const float ArcAB = F.ArcS[j] - F.ArcS[i];
				const float ArcSep = FMath::Min(ArcAB, F.TotalLen - ArcAB); // 環上最短弧距
				if (ArcSep <= SelfArcSepCm)
				{
					continue;
				}
				MinD = FMath::Min(MinD, static_cast<float>(FVector2D::Distance(F.Points[i], F.Points[j])));
			}
		}
		return MinD;
	}
}

void FDreamTraceGen::Generate(const FDreamTraceParams& Params, int32 Seed, FDreamTraceFigure& Out)
{
	const float NeedDist = SelfDistFactor * Params.BandHalfWidthCm;
	int32 TrySeed = Seed;
	for (int32 Attempt = 0; Attempt < MaxRetries; ++Attempt)
	{
		// 振幅逐次收斂（0.9^n）＝重試必然收斂到近圓＝必然通過自交檢查
		const float WobbleScale = FMath::Pow(0.9f, static_cast<float>(Attempt));
		BuildAttempt(Params, TrySeed, WobbleScale, Out);
		Out.UsedSeed = TrySeed;
		Out.Retries = Attempt;
		if (MinSelfDistance(Out) >= NeedDist)
		{
			return;
		}
		TrySeed = TrySeed * 7919 + 104729 + Attempt; // 決定性衍生種子
	}
	// MaxRetries 用罄（理論上不可達——WobbleScale→0 是圓）：保底輸出最後一次
}

FDreamTraceParams FDreamTraceGen::DefaultParamsForCup(int32 Cup)
{
	FDreamTraceParams P;
	switch (FMath::Clamp(Cup, 0, 2))
	{
	case 0: // 第一杯：純描 ≈ 25s
		P.PerimeterCm = 45.0f;
		P.BandHalfWidthCm = 0.60f;
		P.HarmonicMin = 2;
		P.HarmonicMax = 4;
		P.WobbleAmp = 0.16f;
		break;
	case 1: // 第二杯：≈ 36s
		P.PerimeterCm = 65.0f;
		P.BandHalfWidthCm = 0.50f;
		P.HarmonicMin = 2;
		P.HarmonicMax = 5;
		P.WobbleAmp = 0.20f;
		break;
	default: // 第三杯（生死局）：≈ 50s
		P.PerimeterCm = 90.0f;
		P.BandHalfWidthCm = 0.42f;
		P.HarmonicMin = 3;
		P.HarmonicMax = 6;
		P.WobbleAmp = 0.24f;
		break;
	}
	return P;
}

FString FDreamTraceGen::RunStats(const FDreamTraceParams& Params, int32 NumSeeds)
{
	int32 RetriedFigures = 0;
	int32 TotalRetries = 0;
	int32 MaxRetriesSeen = 0;
	float MinSelfD = TNumericLimits<float>::Max();
	float MinR = TNumericLimits<float>::Max();
	float MaxR = 0.0f;
	int32 MinPts = MAX_int32;
	int32 MaxPts = 0;

	for (int32 s = 1; s <= NumSeeds; ++s)
	{
		FDreamTraceFigure F;
		Generate(Params, s * 7717 + 13, F);
		if (F.Retries > 0)
		{
			++RetriedFigures;
		}
		TotalRetries += F.Retries;
		MaxRetriesSeen = FMath::Max(MaxRetriesSeen, F.Retries);
		MinSelfD = FMath::Min(MinSelfD, MinSelfDistance(F));
		MinR = FMath::Min(MinR, F.MaxAbsR);
		MaxR = FMath::Max(MaxR, F.MaxAbsR);
		MinPts = FMath::Min(MinPts, F.Points.Num());
		MaxPts = FMath::Max(MaxPts, F.Points.Num());
	}

	return FString::Printf(
		TEXT("DreamTrace stats: seeds=%d perim=%.0f band=%.2f | retriedFigs=%d totalRetries=%d maxRetries=%d | ")
		TEXT("minSelfDist=%.2f (need %.2f) | maxAbsR=[%.1f..%.1f] pts=[%d..%d]"),
		NumSeeds, Params.PerimeterCm, Params.BandHalfWidthCm,
		RetriedFigures, TotalRetries, MaxRetriesSeen,
		MinSelfD, SelfDistFactor * Params.BandHalfWidthCm,
		MinR, MaxR, MinPts, MaxPts);
}
