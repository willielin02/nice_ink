#include "DreamTrace.h"

#include "DreamTraceMotifData.h"

// 一筆畫圖案生成（SPEC v4.0/v4.0a）：
// 圖案＝烘焙表（Twemoji 十六式，Tools/AssetPrep/TraceMotifs 管線產出——剪影
// 聯集＋內輪廓接駁＋離線三道閘：自交避讓/曲率/pursuit 可描完）。runtime 只做
// 選圖＋鏡像＋等比縮放到 PerimeterCm＝零美學判斷（美學在圖源設計師與 user
// 圈選兩端；手雕模板時代=程式雕美學鐵則違規，已全數退役）。
// Blob 諧波閉圓＝決定性保底（表空/失效時）。

namespace
{
	constexpr int32 ThetaSteps = 720;
	constexpr int32 MaxRetries = 8;

	constexpr float SelfArcSepCm = 3.0f;
	constexpr float SelfDistFactor = 2.6f;

	// Blob（諧波閉圓保底）
	void BuildBlob(const FDreamTraceParams& P, FRandomStream& RS, float WobbleScale, TArray<FVector2D>& Dense)
	{
		const int32 HMin = FMath::Clamp(P.HarmonicMin, 2, 12);
		const int32 HMax = FMath::Clamp(FMath::Max(P.HarmonicMax, HMin), HMin, 12);
		TArray<float> Amp, Phase;
		float Total = 0.0f;
		for (int32 K = HMin; K <= HMax; ++K)
		{
			const float Raw = RS.FRandRange(0.5f, 1.0f) / K;
			Amp.Add(Raw);
			Phase.Add(RS.FRandRange(0.0f, 2.0f * PI));
			Total += Raw;
		}
		const float Budget = FMath::Clamp(P.WobbleAmp, 0.0f, 0.45f) * WobbleScale;
		for (float& A : Amp)
		{
			A = Total > KINDA_SMALL_NUMBER ? A / Total * Budget : 0.0f;
		}
		Dense.Reset();
		for (int32 i = 0; i < ThetaSteps; ++i)
		{
			const float Theta = 2.0f * PI * i / ThetaSteps;
			float R = 1.0f;
			for (int32 k = 0; k < Amp.Num(); ++k)
			{
				R += Amp[k] * FMath::Cos((HMin + k) * Theta + Phase[k]);
			}
			Dense.Add(FVector2D(R * FMath::Cos(Theta), R * FMath::Sin(Theta)));
		}
	}

	// 稠密折線 → 置中＋縮放到目標線長 → 均勻弧長重採樣
	void FinalizeFigure(const TArray<FVector2D>& DenseIn, bool bClosed, const FDreamTraceParams& P,
		FDreamTraceFigure& Out)
	{
		TArray<FVector2D> Dense = DenseIn;

		FVector2D Center = FVector2D::ZeroVector;
		for (const FVector2D& Pt : Dense)
		{
			Center += Pt;
		}
		Center /= FMath::Max(Dense.Num(), 1);
		for (FVector2D& Pt : Dense)
		{
			Pt -= Center;
		}

		float RawLen = 0.0f;
		const int32 SegN = bClosed ? Dense.Num() : Dense.Num() - 1;
		for (int32 i = 0; i < SegN; ++i)
		{
			RawLen += FVector2D::Distance(Dense[i], Dense[(i + 1) % Dense.Num()]);
		}
		const float Scale = RawLen > KINDA_SMALL_NUMBER ? P.PerimeterCm / RawLen : 1.0f;
		for (FVector2D& Pt : Dense)
		{
			Pt *= Scale;
		}

		const float Step = FMath::Clamp(P.SampleStepCm, 0.1f, 1.0f);
		Out.Points.Reset();
		Out.ArcS.Reset();
		Out.bClosed = bClosed;
		float Walked = 0.0f;
		float Acc = 0.0f;
		Out.Points.Add(Dense[0]);
		Out.ArcS.Add(0.0f);
		for (int32 i = 0; i < SegN; ++i)
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
				if (!bClosed || P.PerimeterCm - Acc > Step * 0.5f)
				{
					Out.Points.Add(Pt);
					Out.ArcS.Add(Acc);
				}
				Walked = 0.0f;
			}
			Acc += (SegLen - Consumed);
			Walked += (SegLen - Consumed);
		}
		if (!bClosed && FVector2D::Distance(Out.Points.Last(), Dense.Last()) > Step * 0.25f)
		{
			Out.Points.Add(Dense.Last());
			Out.ArcS.Add(P.PerimeterCm);
		}
		Out.TotalLen = P.PerimeterCm;

		Out.MaxAbsR = 0.0f;
		for (const FVector2D& Pt : Out.Points)
		{
			Out.MaxAbsR = FMath::Max(Out.MaxAbsR, static_cast<float>(Pt.Size()));
		}
	}

	// 自交檢查（Blob 保底用；烘焙表在離線管線已過閘）
	float MinSelfDistance(const FDreamTraceFigure& F)
	{
		float MinD = TNumericLimits<float>::Max();
		const int32 N = F.Points.Num();
		for (int32 i = 0; i < N; ++i)
		{
			for (int32 j = i + 1; j < N; ++j)
			{
				const float ArcAB = F.ArcS[j] - F.ArcS[i];
				const float ArcSep = F.bClosed ? FMath::Min(ArcAB, F.TotalLen - ArcAB) : ArcAB;
				if (ArcSep <= SelfArcSepCm)
				{
					continue;
				}
				MinD = FMath::Min(MinD, static_cast<float>(FVector2D::Distance(F.Points[i], F.Points[j])));
			}
		}
		return MinD;
	}

	// 弧長→中線點（RunStats 模擬用）
	FVector2D StatsPointAtArc(const FDreamTraceFigure& F, float S)
	{
		const int32 N = F.Points.Num();
		float Wrapped;
		if (F.bClosed)
		{
			Wrapped = FMath::Fmod(S, F.TotalLen);
			if (Wrapped < 0.0f)
			{
				Wrapped += F.TotalLen;
			}
		}
		else
		{
			Wrapped = FMath::Clamp(S, 0.0f, F.TotalLen);
		}
		const float Step = F.TotalLen / N;
		const int32 I = FMath::Clamp(static_cast<int32>(Wrapped / Step), 0, N - (F.bClosed ? 1 : 2));
		const int32 J = (I + 1) % N;
		const float S0 = F.ArcS[I];
		float SegLen = (J == 0 ? F.TotalLen : F.ArcS[J]) - S0;
		SegLen = FMath::Max(SegLen, KINDA_SMALL_NUMBER);
		return F.Points[I] + (F.Points[J] - F.Points[I]) * FMath::Clamp((Wrapped - S0) / SegLen, 0.0f, 1.0f);
	}

	// 離線可描性模擬（pursuit 閘）：前瞻 0.7cm＝元件 autopilot 同值
	float SimulateTraceMaxDev(const FDreamTraceFigure& F, float AheadCm, float VMax, float Dt)
	{
		const int32 N = F.Points.Num();
		const float Step = F.TotalLen / N;
		const int32 Window = FMath::Clamp(FMath::CeilToInt(3.0f / Step), 2, N / 2);

		FVector2D Needle = F.Points[0];
		int32 CurIdx = 0;
		float CurS = 0.0f;
		float Prog = 0.0f;
		float MaxDev = 0.0f;
		const int32 MaxSteps = static_cast<int32>(F.TotalLen / (VMax * Dt) * 4.0f) + 100;
		for (int32 StepI = 0; StepI < MaxSteps; ++StepI)
		{
			const FVector2D Cursor = StatsPointAtArc(F, CurS + AheadCm);
			const FVector2D To = Cursor - Needle;
			const float Dist = To.Size();
			if (Dist > KINDA_SMALL_NUMBER)
			{
				Needle += To.GetSafeNormal() * FMath::Min(VMax * Dt, Dist);
			}
			float BestDist = TNumericLimits<float>::Max();
			int32 BestI = CurIdx;
			float BestS = CurS;
			for (int32 d = -Window; d <= Window; ++d)
			{
				const int32 I = F.bClosed ? ((CurIdx + d) % N + N) % N : FMath::Clamp(CurIdx + d, 0, N - 2);
				const int32 J = (I + 1) % N;
				const FVector2D AB = F.Points[J] - F.Points[I];
				const float LenSq = AB.SizeSquared();
				const float T = LenSq > KINDA_SMALL_NUMBER
					? FMath::Clamp(static_cast<float>(FVector2D::DotProduct(Needle - F.Points[I], AB)) / LenSq, 0.0f, 1.0f)
					: 0.0f;
				const FVector2D P = F.Points[I] + AB * T;
				const float D2 = FVector2D::Distance(Needle, P);
				if (D2 < BestDist)
				{
					BestDist = D2;
					BestI = I;
					const float SegLen = (J == 0 ? F.TotalLen : F.ArcS[J]) - F.ArcS[I];
					BestS = F.ArcS[I] + SegLen * T;
				}
			}
			float DeltaS = BestS - CurS;
			if (F.bClosed)
			{
				if (DeltaS > F.TotalLen * 0.5f) { DeltaS -= F.TotalLen; }
				else if (DeltaS < -F.TotalLen * 0.5f) { DeltaS += F.TotalLen; }
			}
			Prog += DeltaS;
			CurIdx = BestI;
			CurS = BestS;
			MaxDev = FMath::Max(MaxDev, BestDist);
			const bool bDone = F.bClosed ? FMath::Abs(Prog) >= F.TotalLen - 0.4f
				: CurS >= F.TotalLen - 0.4f;
			if (bDone)
			{
				break;
			}
		}
		return MaxDev;
	}
}

void FDreamTraceGen::Generate(const FDreamTraceParams& Params, int32 Seed, FDreamTraceFigure& Out)
{
	// 烘焙表路徑：選圖（種子）＋鏡像（種子）＋等比縮放——離線已過三道閘
	if (Params.BakedPool.Num() > 0)
	{
		const int32 PoolIdx = FMath::Abs(Seed / 7) % Params.BakedPool.Num();
		const int32 MotifIdx = Params.BakedPool[PoolIdx];
		if (MotifIdx >= 0 && MotifIdx < NiceInkTraceMotifs::Num)
		{
			const FDreamTraceBakedMotif& M = NiceInkTraceMotifs::Table[MotifIdx];
			const bool bMirror = M.bMirrorAllowed && (FMath::Abs(Seed / 13) % 2 == 1);
			TArray<FVector2D> Dense;
			Dense.Reserve(M.NumPoints);
			for (int32 i = 0; i < M.NumPoints; ++i)
			{
				const float X = M.Points[i * 2] * (bMirror ? -1.0f : 1.0f);
				Dense.Add(FVector2D(X, M.Points[i * 2 + 1]));
			}
			FinalizeFigure(Dense, M.bClosed, Params, Out);
			Out.MotifIndex = MotifIdx;
			Out.bMirrored = bMirror;
			Out.UsedSeed = Seed;
			Out.Retries = 0;
			if (Out.IsValid())
			{
				return;
			}
		}
	}

	// Blob 保底（決定性；振幅遞減必然收斂近圓＝必然過自交檢查）
	const float NeedDist = SelfDistFactor * Params.BandHalfWidthCm;
	int32 TrySeed = Seed;
	for (int32 Attempt = 0; Attempt < MaxRetries; ++Attempt)
	{
		FRandomStream RS(TrySeed);
		TArray<FVector2D> Dense;
		BuildBlob(Params, RS, FMath::Pow(0.9f, static_cast<float>(Attempt)), Dense);
		FinalizeFigure(Dense, true, Params, Out);
		Out.MotifIndex = INDEX_NONE;
		Out.bMirrored = false;
		Out.UsedSeed = TrySeed;
		Out.Retries = Attempt;
		if (Out.IsValid() && MinSelfDistance(Out) >= NeedDist)
		{
			return;
		}
		TrySeed = TrySeed * 7919 + 104729 + Attempt;
	}
}

FDreamTraceParams FDreamTraceGen::DefaultParamsForCup(int32 Cup)
{
	// 時間統一 60s（user 定案）；帶半寬=筆寬（user 定案「筆寬兩倍當帶寬」，
	// GameMode 發夢當下用受害者實際筆寬覆寫）；難度軸=圖案池複雜度。
	using namespace NiceInkTraceMotifs;
	FDreamTraceParams P;
	P.TargetTraceSeconds = 60.0f;
	P.BandHalfWidthCm = 0.3f;
	switch (FMath::Clamp(Cup, 0, 2))
	{
	case 0: // 第一杯：簡單剪影
		P.BakedPool = { Idx_Onigiri, Idx_Fan, Idx_Fuji, Idx_Moon, Idx_Wave };
		break;
	case 1: // 第二杯：中等
		P.BakedPool = { Idx_Dango, Idx_Lantern, Idx_Koi, Idx_Octopus, Idx_Snake };
		break;
	default: // 第三杯（生死局）：複雜標的
		P.BakedPool = { Idx_Turtle, Idx_Sakura, Idx_Torii, Idx_Oni, Idx_Momiji, Idx_Castle };
		break;
	}
	P.PerimeterCm = P.TargetTraceSeconds * 1.8f;
	return P;
}

FString FDreamTraceGen::RunStats(const FDreamTraceParams& Params, int32 NumSeeds)
{
	int32 RetriedFigures = 0;
	int32 BlobFallbacks = 0;
	float MinSelfD = TNumericLimits<float>::Max();
	float MaxR = 0.0f;
	int32 MinPts = MAX_int32;
	float AutoDevMax = 0.0f;
	int32 AutoDevViol = 0;
	TMap<int32, int32> MotifCount;

	for (int32 s = 1; s <= NumSeeds; ++s)
	{
		FDreamTraceFigure F;
		Generate(Params, s * 7717 + 13, F);
		if (F.Retries > 0)
		{
			++RetriedFigures;
		}
		if (F.MotifIndex == INDEX_NONE && Params.BakedPool.Num() > 0)
		{
			++BlobFallbacks;
		}
		MinSelfD = FMath::Min(MinSelfD, MinSelfDistance(F));
		MaxR = FMath::Max(MaxR, F.MaxAbsR);
		MinPts = FMath::Min(MinPts, F.Points.Num());
		MotifCount.FindOrAdd(F.MotifIndex)++;

		const float Dev = SimulateTraceMaxDev(F, 0.7f, 1.8f, 1.0f / 60.0f);
		AutoDevMax = FMath::Max(AutoDevMax, Dev);
		if (Dev > Params.BandHalfWidthCm)
		{
			++AutoDevViol;
		}
	}

	FString Motifs;
	for (const auto& Pair : MotifCount)
	{
		Motifs += FString::Printf(TEXT("%d:%d "), Pair.Key, Pair.Value);
	}
	return FString::Printf(
		TEXT("DreamTrace stats: seeds=%d len=%.0f band=%.2f | retriedFigs=%d blobFallback=%d | ")
		TEXT("minSelfDist=%.2f (need %.2f) | autoDevMax=%.2f autoDevViol=%d | ")
		TEXT("maxAbsR=%.1f minPts=%d | motifs[%s]"),
		NumSeeds, Params.PerimeterCm, Params.BandHalfWidthCm,
		RetriedFigures, BlobFallbacks,
		MinSelfD, SelfDistFactor * Params.BandHalfWidthCm,
		AutoDevMax, AutoDevViol,
		MaxR, MinPts, *Motifs);
}
