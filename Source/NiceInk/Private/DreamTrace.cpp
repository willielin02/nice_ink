#include "DreamTrace.h"

// 一筆畫圖案生成（SPEC v4.0 #49 二段、user 定案 08-02）：
// 「拿常見日式刺青的標的簡化後設計成一筆畫內容」＋「先定義不受干擾下平均完成
// 的時間，依筆速得出線條長度」——時間是設計輸入、長度是導出量（換算在 GameMode
// 發夢當下用受害者 v_max 完成；本檔只吃最終線長）。
//
// 全部模板的兩條可描性鐵律：
// 1. 自交避讓：弧距 >3cm 的兩點歐氏距離 ≥2.6×帶半寬（投影窗唯一性＋帶不自碰）。
// 2. 圓角化：控制點經 Catmull-Rom 平滑（雷紋另做角圓化）——恆速針/追趕游標
//    在尖角會切內側出帶，最小曲率半徑要 ≳1.5cm。

namespace
{
	constexpr int32 ThetaSteps = 720;   // 參數化家族（花/閉圓）的稠密取樣
	constexpr int32 CurveSubdiv = 14;   // Catmull-Rom 每段細分
	constexpr int32 MaxRetries = 32;

	constexpr float SelfArcSepCm = 3.0f;
	constexpr float SelfDistFactor = 2.6f;

	// --- Catmull-Rom（uniform）：控制折線 → 平滑稠密折線 ---
	FVector2D CatmullRom(const FVector2D& P0, const FVector2D& P1, const FVector2D& P2, const FVector2D& P3, float T)
	{
		const float T2 = T * T;
		const float T3 = T2 * T;
		return 0.5f * ((2.0f * P1) + (-P0 + P2) * T +
			(2.0f * P0 - 5.0f * P1 + 4.0f * P2 - P3) * T2 +
			(-P0 + 3.0f * P1 - 3.0f * P2 + P3) * T3);
	}

	void SmoothCurve(const TArray<FVector2D>& Ctrl, bool bClosed, TArray<FVector2D>& Out)
	{
		Out.Reset();
		const int32 N = Ctrl.Num();
		if (N < 3)
		{
			Out = Ctrl;
			return;
		}
		const int32 SegN = bClosed ? N : N - 1;
		for (int32 i = 0; i < SegN; ++i)
		{
			const FVector2D& P0 = Ctrl[bClosed ? (i - 1 + N) % N : FMath::Max(i - 1, 0)];
			const FVector2D& P1 = Ctrl[i];
			const FVector2D& P2 = Ctrl[(i + 1) % N];
			const FVector2D& P3 = Ctrl[bClosed ? (i + 2) % N : FMath::Min(i + 2, N - 1)];
			for (int32 s = 0; s < CurveSubdiv; ++s)
			{
				Out.Add(CatmullRom(P0, P1, P2, P3, static_cast<float>(s) / CurveSubdiv));
			}
		}
		if (!bClosed)
		{
			Out.Add(Ctrl.Last());
		}
	}

	// 角圓化（雷紋等方角模板）：每個內角換成沿兩邊各退 R 的兩點——CR 再平滑
	void RoundCorners(const TArray<FVector2D>& Ctrl, float R, TArray<FVector2D>& Out)
	{
		Out.Reset();
		Out.Add(Ctrl[0]);
		for (int32 i = 1; i + 1 < Ctrl.Num(); ++i)
		{
			const FVector2D& A = Ctrl[i - 1];
			const FVector2D& B = Ctrl[i];
			const FVector2D& C = Ctrl[i + 1];
			const float LA = FVector2D::Distance(A, B);
			const float LC = FVector2D::Distance(B, C);
			const float RA = FMath::Min(R, LA * 0.45f);
			const float RC = FMath::Min(R, LC * 0.45f);
			Out.Add(B + (A - B).GetSafeNormal() * RA);
			Out.Add(B + (C - B).GetSafeNormal() * RC);
		}
		Out.Add(Ctrl.Last());
	}

	// 控制點微擾（每回合同圖不同貌；小到不動辨識度）＋鏡像
	void JitterAndMirror(TArray<FVector2D>& Ctrl, FRandomStream& RS, float JitterFrac, bool bAllowMirror)
	{
		float MaxAbs = 1.0f;
		for (const FVector2D& P : Ctrl)
		{
			MaxAbs = FMath::Max(MaxAbs, static_cast<float>(P.Size()));
		}
		const bool bMirror = bAllowMirror && RS.FRand() < 0.5f;
		for (FVector2D& P : Ctrl)
		{
			P.X += RS.FRandRange(-1.0f, 1.0f) * JitterFrac * MaxAbs;
			P.Y += RS.FRandRange(-1.0f, 1.0f) * JitterFrac * MaxAbs;
			if (bMirror)
			{
				P.X = -P.X;
			}
		}
	}

	// --- 模板庫（單位任意，之後整體縮放到目標線長；y 向下同 canvas） ---

	// 參數化花環：r(θ)=1+Amp·cos(Kθ+φ)（櫻五瓣/菊十二/十六瓣＋Blob 保底同構）
	void BuildFlower(int32 K, float Amp, FRandomStream& RS, float JitterFrac, TArray<FVector2D>& Dense, bool& bClosed)
	{
		bClosed = true;
		const float Phase = RS.FRandRange(0.0f, 2.0f * PI);
		const float A = Amp * (1.0f + RS.FRandRange(-0.15f, 0.15f) * (JitterFrac > 0.0f ? 1.0f : 0.0f));
		Dense.Reset();
		for (int32 i = 0; i < ThetaSteps; ++i)
		{
			const float Theta = 2.0f * PI * i / ThetaSteps;
			const float R = 1.0f + A * FMath::Cos(K * Theta + Phase);
			Dense.Add(FVector2D(R * FMath::Cos(Theta), R * FMath::Sin(Theta)));
		}
	}

	// Blob（諧波閉圓保底；原家族）
	void BuildBlob(const FDreamTraceParams& P, FRandomStream& RS, float WobbleScale, TArray<FVector2D>& Dense, bool& bClosed)
	{
		bClosed = true;
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

	// 控制點模板（開放/閉合折線；JitterFrac 隨重試遞減）
	void BuildTemplate(EDreamTraceMotif Motif, FRandomStream& RS, float JitterFrac,
		TArray<FVector2D>& Dense, bool& bClosed)
	{
		TArray<FVector2D> Ctrl;
		bool bAllowMirror = false;
		bool bRoundCorners = false;
		bClosed = false;

		switch (Motif)
		{
		case EDreamTraceMotif::Fuji:
			// 富士山剪影＋山頂雪口微凹（開放；左右緩坡）
			Ctrl = {
				{-6.0f, 2.6f}, {-3.4f, 0.6f}, {-1.8f, -1.3f}, {-1.0f, -1.75f},
				{-0.45f, -1.5f}, {0.1f, -1.8f}, {0.7f, -1.6f}, {1.4f, -1.2f},
				{2.8f, 0.2f}, {4.6f, 1.7f}, {6.0f, 2.4f} };
			break;
		case EDreamTraceMotif::Gourd:
			// 葫蘆（瓢箪；閉合、上小下大兩球＋腰身——腰寬守自距鐵律）
			bClosed = true;
			Ctrl = {
				{0.0f, -3.0f}, {1.1f, -2.6f}, {1.5f, -1.7f}, {1.1f, -0.9f},
				{0.8f, -0.3f}, {1.6f, 0.5f}, {1.9f, 1.6f}, {1.4f, 2.7f},
				{0.0f, 3.1f}, {-1.4f, 2.7f}, {-1.9f, 1.6f}, {-1.6f, 0.5f},
				{-0.8f, -0.3f}, {-1.1f, -0.9f}, {-1.5f, -1.7f}, {-1.1f, -2.6f} };
			break;
		case EDreamTraceMotif::Snake:
			// 蛇行流水（開放 S 蛇形＋收尾小鉤＝頭）
			bAllowMirror = true;
			Ctrl = {
				{-6.0f, 0.4f}, {-4.4f, -1.2f}, {-2.8f, 0.9f}, {-1.2f, -1.1f},
				{0.4f, 1.1f}, {2.0f, -1.0f}, {3.6f, 0.9f}, {5.0f, -0.6f},
				{6.2f, -1.6f}, {6.8f, -0.9f}, {6.2f, -0.45f} };
			break;
		case EDreamTraceMotif::Wave:
			// 波浪（北齋讀法：長昇浪＋浪頭捲回；開放）
			bAllowMirror = true;
			Ctrl = {
				{-6.0f, 2.0f}, {-3.8f, 1.6f}, {-1.8f, 0.7f}, {0.2f, -0.6f},
				{1.6f, -1.9f}, {2.2f, -3.0f}, {1.2f, -3.8f}, {-0.2f, -3.4f},
				{-0.9f, -2.4f} };
			break;
		case EDreamTraceMotif::Koi:
			// 鯉魚輪廓（閉合：吻→背→分叉尾（凹口）→腹）
			bClosed = true;
			bAllowMirror = true;
			// 尾叉 V 要夠寬：首版 tips(±1.4)/notch(-3.2) 的窄叉在 108cm 尺度
			// 63/66 觸發自距重試落保底——叉寬是自距鐵律的直接參數
			Ctrl = {
				{3.6f, 0.0f}, {2.6f, -1.0f}, {0.8f, -1.5f}, {-1.2f, -1.2f},
				{-2.4f, -0.6f}, {-4.1f, -1.8f}, {-3.3f, 0.0f}, {-4.1f, 1.8f},
				{-2.4f, 0.6f}, {-1.2f, 1.2f}, {0.8f, 1.5f}, {2.6f, 1.0f} };
			break;
		case EDreamTraceMotif::Raimon:
			// 雷紋／迴字紋（開放方形迴旋；方角先圓角化再平滑——恆速針的可描性）
			bAllowMirror = true;
			bRoundCorners = true;
			Ctrl = {
				{6.0f, 4.0f}, {-6.0f, 4.0f}, {-6.0f, -4.0f}, {6.0f, -4.0f},
				{6.0f, 1.2f}, {-3.2f, 1.2f}, {-3.2f, -1.6f}, {3.2f, -1.6f} };
			break;
		default:
			break;
		}

		JitterAndMirror(Ctrl, RS, JitterFrac * 0.04f, bAllowMirror);
		if (bRoundCorners)
		{
			TArray<FVector2D> Rounded;
			RoundCorners(Ctrl, 0.9f, Rounded);
			Ctrl = MoveTemp(Rounded);
		}
		SmoothCurve(Ctrl, bClosed, Dense);
	}

	void BuildDense(EDreamTraceMotif Motif, const FDreamTraceParams& P, FRandomStream& RS,
		int32 Attempt, TArray<FVector2D>& Dense, bool& bClosed)
	{
		// 重試階梯：0=全微擾、1=半微擾、2=零微擾；仍失敗＝Blob 保底（振幅遞減必然收斂近圓）
		const float JitterFrac = Attempt == 0 ? 1.0f : (Attempt == 1 ? 0.5f : 0.0f);
		if (Attempt >= 3)
		{
			BuildBlob(P, RS, FMath::Pow(0.9f, static_cast<float>(Attempt - 3)), Dense, bClosed);
			return;
		}
		switch (Motif)
		{
		case EDreamTraceMotif::Sakura: BuildFlower(5, 0.18f, RS, JitterFrac, Dense, bClosed); break; // 谷曲率 ≥2cm
		case EDreamTraceMotif::Kiku8:  BuildFlower(8, 0.12f, RS, JitterFrac, Dense, bClosed); break;  // 谷曲率 ≥2cm
		case EDreamTraceMotif::Kiku10: BuildFlower(10, 0.08f, RS, JitterFrac, Dense, bClosed); break; // 谷曲率 ≥2cm
		case EDreamTraceMotif::Blob:   BuildBlob(P, RS, 1.0f, Dense, bClosed); break;
		default:                       BuildTemplate(Motif, RS, JitterFrac, Dense, bClosed); break;
		}
	}

	// 稠密折線 → 縮放到目標線長 → 均勻弧長重採樣
	void FinalizeFigure(const TArray<FVector2D>& DenseIn, bool bClosed, const FDreamTraceParams& P,
		FDreamTraceFigure& Out)
	{
		TArray<FVector2D> Dense = DenseIn;

		// 置中（模板重心歸零＝盤面佈局穩定）
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
				// 閉合：尾端貼近起點不再輸出（wrap 由邏輯層處理）；開放：照常輸出
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
		if (!bClosed && FVector2D::Distance(Out.Points.Last(), Dense.Last() ) > Step * 0.25f)
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

	// 弧長→中線點（RunStats 模擬用；與元件 RoutePointAtArc 同構）
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

	// 離線可描性模擬（pursuit 閘）：游標＝針投影前方 AheadCm 的路線點、針以 VMax
	// 追（與元件 autopilot/追趕同構）——回傳整條路線的最大針-中線偏差。
	// 這是「模板描得動」的離線鐵閘：任何新模板/振幅在 600 樣本上超帶＝生成期抓到，
	// 不用等 robo/真人踩雷（花瓣谷曲率事故的制度化教訓）。
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
			// 窗內投影（元件 ProjectNeedle 同構）
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

	// 自交檢查：弧距 > SelfArcSepCm 的點對最小歐氏距離（開放圖形弧距不繞環）
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
}

void FDreamTraceGen::Generate(const FDreamTraceParams& Params, int32 Seed, FDreamTraceFigure& Out)
{
	// 圖案池選圖（種子決定；空池＝Blob）
	const EDreamTraceMotif Motif = Params.MotifPool.Num() > 0
		? Params.MotifPool[FMath::Abs(Seed / 7) % Params.MotifPool.Num()]
		: EDreamTraceMotif::Blob;

	const float NeedDist = SelfDistFactor * Params.BandHalfWidthCm;
	int32 TrySeed = Seed;
	for (int32 Attempt = 0; Attempt < MaxRetries; ++Attempt)
	{
		FRandomStream RS(TrySeed);
		TArray<FVector2D> Dense;
		bool bClosed = true;
		BuildDense(Motif, Params, RS, Attempt, Dense, bClosed);
		FinalizeFigure(Dense, bClosed, Params, Out);
		Out.Motif = Attempt >= 3 ? EDreamTraceMotif::Blob : Motif;
		Out.UsedSeed = TrySeed;
		Out.Retries = Attempt;
		if (Out.IsValid() && MinSelfDistance(Out) >= NeedDist)
		{
			return;
		}
		TrySeed = TrySeed * 7919 + 104729 + Attempt; // 決定性衍生種子
	}
	// MaxRetries 用罄（理論不可達——Blob 收斂到圓）：保底輸出最後一次
}

FDreamTraceParams FDreamTraceGen::DefaultParamsForCup(int32 Cup)
{
	// user 定案設計程序（08-02 二段）：先定「不受干擾平均完成時間」→線長＝T×v_max。
	// 時間＝**統一 60 秒不隨杯數**（08-02 user 定案：時間服務作畫者的內容預算＝
	// 每回合恆定需求；難度全走精準軸——帶寬變窄＋圖案池變複雜，深杯實際時間
	// 變長的來源是失誤率＝可歸咎於手）。1.8cm/s ⇒ 線長 108cm（發夢當下由
	// GameMode 用受害者實際 v_max 重算）。
	FDreamTraceParams P;
	P.TargetTraceSeconds = 60.0f;
	switch (FMath::Clamp(Cup, 0, 2))
	{
	case 0: // 第一杯：簡單標的＋寬帶
		P.BandHalfWidthCm = 0.60f;
		P.MotifPool = { EDreamTraceMotif::Sakura, EDreamTraceMotif::Gourd, EDreamTraceMotif::Fuji };
		P.HarmonicMin = 2; P.HarmonicMax = 4; P.WobbleAmp = 0.16f; // Blob 保底參數
		break;
	case 1: // 第二杯
		P.BandHalfWidthCm = 0.50f;
		P.MotifPool = { EDreamTraceMotif::Kiku8, EDreamTraceMotif::Snake, EDreamTraceMotif::Wave };
		P.HarmonicMin = 2; P.HarmonicMax = 5; P.WobbleAmp = 0.20f;
		break;
	default: // 第三杯（生死局）：複雜標的＋最窄帶
		P.BandHalfWidthCm = 0.42f;
		P.MotifPool = { EDreamTraceMotif::Koi, EDreamTraceMotif::Raimon, EDreamTraceMotif::Kiku10 };
		P.HarmonicMin = 3; P.HarmonicMax = 6; P.WobbleAmp = 0.24f;
		break;
	}
	P.PerimeterCm = P.TargetTraceSeconds * 1.8f;
	return P;
}

FString FDreamTraceGen::RunStats(const FDreamTraceParams& Params, int32 NumSeeds)
{
	int32 RetriedFigures = 0;
	int32 TotalRetries = 0;
	int32 MaxRetriesSeen = 0;
	int32 BlobFallbacks = 0;
	float MinSelfD = TNumericLimits<float>::Max();
	float MaxR = 0.0f;
	int32 MinPts = MAX_int32;
	float AutoDevMax = 0.0f;
	int32 AutoDevViol = 0;
	TMap<EDreamTraceMotif, int32> MotifCount;

	for (int32 s = 1; s <= NumSeeds; ++s)
	{
		FDreamTraceFigure F;
		Generate(Params, s * 7717 + 13, F);
		if (F.Retries > 0)
		{
			++RetriedFigures;
		}
		if (F.Retries >= 3)
		{
			++BlobFallbacks;
		}
		TotalRetries += F.Retries;
		MaxRetriesSeen = FMath::Max(MaxRetriesSeen, F.Retries);
		MinSelfD = FMath::Min(MinSelfD, MinSelfDistance(F));
		MaxR = FMath::Max(MaxR, F.MaxAbsR);
		MinPts = FMath::Min(MinPts, F.Points.Num());
		MotifCount.FindOrAdd(F.Motif)++;

		// 可描性 pursuit 模擬（前瞻 0.9cm＝元件 autopilot 同值、vmax 1.8、60fps）
		const float Dev = SimulateTraceMaxDev(F, 0.9f, 1.8f, 1.0f / 60.0f);
		AutoDevMax = FMath::Max(AutoDevMax, Dev);
		if (Dev > Params.BandHalfWidthCm)
		{
			++AutoDevViol;
		}
	}

	FString Motifs;
	for (const auto& Pair : MotifCount)
	{
		Motifs += FString::Printf(TEXT("%d:%d "), static_cast<int32>(Pair.Key), Pair.Value);
	}
	return FString::Printf(
		TEXT("DreamTrace stats: seeds=%d len=%.0f band=%.2f | retriedFigs=%d totalRetries=%d maxRetries=%d ")
		TEXT("blobFallback=%d | minSelfDist=%.2f (need %.2f) | autoDevMax=%.2f autoDevViol=%d | ")
		TEXT("maxAbsR=%.1f minPts=%d | motifs[%s]"),
		NumSeeds, Params.PerimeterCm, Params.BandHalfWidthCm,
		RetriedFigures, TotalRetries, MaxRetriesSeen, BlobFallbacks,
		MinSelfD, SelfDistFactor * Params.BandHalfWidthCm,
		AutoDevMax, AutoDevViol,
		MaxR, MinPts, *Motifs);
}
