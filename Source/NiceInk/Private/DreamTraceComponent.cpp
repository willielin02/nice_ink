#include "DreamTraceComponent.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "NiceInkAudio.h"
#include "NiceInkCharacter.h"

namespace
{
	// 投影窗半徑（cm）：針單 tick 位移 ≤ v_max×dt ≈ 0.1cm，窗 3cm 綽綽有餘；
	// 生成器保證弧距 >3cm 的路線自距 ≥2.6×帶半寬＝窗內投影唯一
	constexpr float ProjWindowCm = 3.0f;

	// 完成判定的閉合容差（cm）
	constexpr float CompleteSlackCm = 0.35f;

	// 失敗紅閃時長（s）
	constexpr float FailFlashSec = 0.6f;

	// 搖晃包絡（進場/離場緩衝；中段全幅）
	constexpr float ShakeEaseIn = 0.15f;
	constexpr float ShakeEaseOut = 0.35f;

	// 盤面配色（沿用醉夢近黑底語彙；全部 FromSRGBColor——canvas 上屏做 gamma 校正，
	// 顯示值直塞 FLinearColor 會整組變亮）
	const FLinearColor TraceDiscColor = FLinearColor::FromSRGBColor(FColor(12, 11, 24));
	const FLinearColor TraceBandFill = FLinearColor::FromSRGBColor(FColor(58, 54, 88));
	const FLinearColor TraceBandEdge = FLinearColor::FromSRGBColor(FColor(126, 120, 168));
	const FLinearColor TraceInkColor = FLinearColor::FromSRGBColor(FColor(242, 237, 217));
	const FLinearColor TraceStartColor = FLinearColor::FromSRGBColor(FColor(217, 194, 122));
	const FLinearColor TraceNeedleColor = FLinearColor::FromSRGBColor(FColor(255, 255, 255));
	const FLinearColor TraceCursorColor = FLinearColor::FromSRGBColor(FColor(185, 179, 214));
	const FLinearColor TraceFailColor = FLinearColor::FromSRGBColor(FColor(180, 30, 30));

	// --- canvas 三角形工具（同一層要按順序疊＝全三角形；粗 polyline 端點/折點補圓盤） ---

	void AddTri(TArray<FCanvasUVTri>& Out, const FVector2D& A, const FVector2D& B, const FVector2D& C,
		const FLinearColor& ColA, const FLinearColor& ColB, const FLinearColor& ColC)
	{
		FCanvasUVTri T;
		T.V0_Pos = A; T.V1_Pos = B; T.V2_Pos = C;
		T.V0_Color = ColA; T.V1_Color = ColB; T.V2_Color = ColC;
		T.V0_UV = T.V1_UV = T.V2_UV = FVector2D::ZeroVector;
		Out.Add(T);
	}

	void AddQuad(TArray<FCanvasUVTri>& Out, const FVector2D& A, const FVector2D& B, float HalfWidthPx,
		const FLinearColor& Col)
	{
		FVector2D Dir = B - A;
		if (!Dir.Normalize())
		{
			return;
		}
		const FVector2D N(-Dir.Y, Dir.X);
		const FVector2D A0 = A + N * HalfWidthPx, A1 = A - N * HalfWidthPx;
		const FVector2D B0 = B + N * HalfWidthPx, B1 = B - N * HalfWidthPx;
		AddTri(Out, A0, B0, B1, Col, Col, Col);
		AddTri(Out, A0, B1, A1, Col, Col, Col);
	}

	void AddDisc(TArray<FCanvasUVTri>& Out, const FVector2D& C, float RadiusPx, const FLinearColor& Col,
		int32 Segs = 10)
	{
		for (int32 i = 0; i < Segs; ++i)
		{
			const float T0 = 2.0f * PI * i / Segs;
			const float T1 = 2.0f * PI * (i + 1) / Segs;
			AddTri(Out,
				C,
				C + FVector2D(FMath::Cos(T0), FMath::Sin(T0)) * RadiusPx,
				C + FVector2D(FMath::Cos(T1), FMath::Sin(T1)) * RadiusPx,
				Col, Col, Col);
		}
	}
}

UDreamTraceComponent::UDreamTraceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(false); // 夢的進度不複製——作畫者看不到＝張力來源
}

ANiceInkCharacter* UDreamTraceComponent::OwnerChar() const
{
	return Cast<ANiceInkCharacter>(GetOwner());
}

float UDreamTraceComponent::Now() const
{
	return GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
}

void UDreamTraceComponent::StartTrace(int32 Seed, const FDreamTraceParams& InParams)
{
	Params = InParams;
	FDreamTraceGen::Generate(Params, Seed, Figure);
	if (!Figure.IsValid())
	{
		bActive = false;
		return;
	}

	bActive = true;
	NeedlePanel = Figure.Points[0];
	CursorPanel = NeedlePanel;
	bPenDown = false;
	bPrevPenDown = false;
	CurIdx = 0;
	CurS = 0.0f;
	ProgressS = 0.0f;
	InkFig.Reset();
	FailCount = 0;
	FailFlashUntil = 0.0f;
	bCompleteSent = false;
	bPendingComplete = false;
	ShakeStartTime = ShakeEndTime = -1000.0f;
	ShakeAmpCm = 0.0f;
	ShakeAttackerName.Reset();
	AutopilotRemaining = 0.0f;
	VeerRemaining = 0.0f;
	bDebugPaintHeld = false;
	bPendingForceComplete = false;
}

void UDreamTraceComponent::StopTrace()
{
	bActive = false;
}

void UDreamTraceComponent::ApplyShake(const FString& AttackerName, float Seconds, float AmpCm)
{
	// 已無聲睜眼＝夢已離場：搖晃靜默失效（連音效都不給——睜眼後的受害者不該
	// 免費得知「有人剛砸錢」；server 端照扣＝攻擊者砸空自擔）
	const ANiceInkCharacter* C = OwnerChar();
	if (!bActive || !C || C->bEyesOpen)
	{
		return;
	}
	ShakeStartTime = Now();
	ShakeEndTime = ShakeStartTime + FMath::Max(Seconds, 0.2f);
	ShakeAmpCm = AmpCm;
	ShakeAttackerName = AttackerName;
	// 沉睡者音效層全域靜音只豁免 UiClick——夢內自身回饋走它（情報遮蔽不破：
	// 這是受害者自己的夢、與房間事件無關）
	NiAudio::Play(OwnerChar(), ENiSound::UiClick, 0.5f);
}

bool UDreamTraceComponent::IsShakeActive() const
{
	const float T = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	return T >= ShakeStartTime && T <= ShakeEndTime + ShakeEaseOut;
}

bool UDreamTraceComponent::IsFailFlashing() const
{
	return GetWorld() && GetWorld()->GetTimeSeconds() < FailFlashUntil;
}

FVector2D UDreamTraceComponent::ShakeOffsetCm() const
{
	const float T = Now();
	if (T < ShakeStartTime || T > ShakeEndTime + ShakeEaseOut)
	{
		return FVector2D::ZeroVector;
	}
	// 包絡：進場 0.15s 拉滿、結束後 0.35s 收斂到零（偏移歸零＝抬針等過去的
	// 玩家針自動回到原本的圖上位置——攻擊買到的是暫停不是必中）
	float Env = 1.0f;
	if (T < ShakeStartTime + ShakeEaseIn)
	{
		Env = (T - ShakeStartTime) / ShakeEaseIn;
	}
	else if (T > ShakeEndTime)
	{
		Env = 1.0f - (T - ShakeEndTime) / ShakeEaseOut;
	}
	const float TT = (T - ShakeStartTime) * 2.0f * PI;
	// 非諧和頻率組＝不可預測的晃（但連續、無跳點——針的相對位移速度有限）
	const FVector2D Wobble(
		FMath::Sin(TT * 1.48f) + 0.5f * FMath::Sin(TT * 3.67f + 1.7f),
		FMath::Sin(TT * 1.86f + 0.9f) + 0.5f * FMath::Sin(TT * 3.07f));
	return Wobble * (ShakeAmpCm * Env * 0.6f);
}

FVector2D UDreamTraceComponent::RoutePointAtArc(float S) const
{
	if (Figure.Points.Num() < 2)
	{
		return FVector2D::ZeroVector;
	}
	float Wrapped;
	if (Figure.bClosed)
	{
		Wrapped = FMath::Fmod(S, Figure.TotalLen);
		if (Wrapped < 0.0f)
		{
			Wrapped += Figure.TotalLen;
		}
	}
	else
	{
		Wrapped = FMath::Clamp(S, 0.0f, Figure.TotalLen); // 開放一筆畫：起終點鉗位
	}
	// 均勻步長＝索引可直接估算
	const float Step = Figure.TotalLen / Figure.Points.Num();
	const int32 I = FMath::Clamp(static_cast<int32>(Wrapped / Step), 0, Figure.Points.Num() - (Figure.bClosed ? 1 : 2));
	const int32 J = (I + 1) % Figure.Points.Num();
	const float S0 = Figure.ArcS[I];
	float SegLen = (J == 0 ? Figure.TotalLen : Figure.ArcS[J]) - S0;
	SegLen = FMath::Max(SegLen, KINDA_SMALL_NUMBER);
	const float T = FMath::Clamp((Wrapped - S0) / SegLen, 0.0f, 1.0f);
	return Figure.Points[I] + (Figure.Points[J] - Figure.Points[I]) * T;
}

float UDreamTraceComponent::ProjectNeedle(const FVector2D& NeedleFig)
{
	const int32 N = Figure.Points.Num();
	const float Step = Figure.TotalLen / N;
	const int32 Window = FMath::Clamp(FMath::CeilToInt(ProjWindowCm / Step), 2, N / 2);

	float BestDist = TNumericLimits<float>::Max();
	int32 BestI = CurIdx;
	float BestS = CurS;

	for (int32 d = -Window; d <= Window; ++d)
	{
		// 閉合＝環上取窗；開放＝鉗在 [0, N-2]（最後一段止於終點）
		const int32 I = Figure.bClosed
			? ((CurIdx + d) % N + N) % N
			: FMath::Clamp(CurIdx + d, 0, N - 2);
		const int32 J = (I + 1) % N;
		const FVector2D& A = Figure.Points[I];
		const FVector2D& B = Figure.Points[J];
		const FVector2D AB = B - A;
		const float LenSq = AB.SizeSquared();
		const float T = LenSq > KINDA_SMALL_NUMBER
			? FMath::Clamp(static_cast<float>(FVector2D::DotProduct(NeedleFig - A, AB)) / LenSq, 0.0f, 1.0f)
			: 0.0f;
		const FVector2D P = A + AB * T;
		const float Dist = FVector2D::Distance(NeedleFig, P);
		if (Dist < BestDist)
		{
			BestDist = Dist;
			BestI = I;
			const float SegLen = (J == 0 ? Figure.TotalLen : Figure.ArcS[J]) - Figure.ArcS[I];
			BestS = Figure.ArcS[I] + SegLen * T;
		}
	}

	// 帶號進度累積：閉合走 wrap 域最短差（窗 ±3cm ≪ 周長，唯一）；開放直接差
	float DeltaS = BestS - CurS;
	if (Figure.bClosed)
	{
		if (DeltaS > Figure.TotalLen * 0.5f)
		{
			DeltaS -= Figure.TotalLen;
		}
		else if (DeltaS < -Figure.TotalLen * 0.5f)
		{
			DeltaS += Figure.TotalLen;
		}
	}
	ProgressS += DeltaS;
	CurIdx = BestI;
	CurS = BestS;
	return BestDist;
}

void UDreamTraceComponent::FailReset()
{
	++FailCount;
	FailFlashUntil = Now() + FailFlashSec;
	InkFig.Reset();
	NeedlePanel = Figure.Points[0] + ShakeOffsetCm(); // 針回起點（圖形空間的起點＋當下偏移）
	CursorPanel = NeedlePanel;
	CurIdx = 0;
	CurS = 0.0f;
	ProgressS = 0.0f;
	bPenDown = false;
	NiAudio::Play(OwnerChar(), ENiSound::UiClick, 0.9f);
}

void UDreamTraceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	ANiceInkCharacter* C = OwnerChar();
	if (!bActive || !C || !C->IsLocallyControlled() || !C->bAsleep || C->bEyesOpen)
	{
		return;
	}

	// 完成送出（pending 消化在 tick＝python guard 外；一回合恰一次）
	if ((bPendingComplete || bPendingForceComplete) && !bCompleteSent)
	{
		bCompleteSent = true;
		bPendingComplete = false;
		bPendingForceComplete = false;
		C->ServerTraceComplete();
		return;
	}
	if (bCompleteSent)
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(C->GetController());
	if (!PC)
	{
		return;
	}

	const FVector2D Offset = ShakeOffsetCm();

	// --- 輸入：滑鼠→游標（純積分）；LMB＝下針 ---
	// 增益同源（定律③）：割線的角增益 × 名義眼距＝cm/單位——與貼膚割線同手感
	const float SensCm = C->DrawAimSensitivity() * (PI / 180.0f) * NominalDreamEyeDistCm * TraceCursorGain;
	float MX = 0.0f, MY = 0.0f;
	PC->GetInputMouseDelta(MX, MY);
	CursorPanel.X += MX * SensCm;
	CursorPanel.Y -= MY * SensCm; // canvas y 向下

	bPrevPenDown = bPenDown;
	bPenDown = PC->IsInputKeyDown(EKeys::LeftMouseButton) || bDebugPaintHeld;

	// --- robo 除錯駕駛（消化在輸入之後＝覆寫游標/左鍵；走同一條追趕/判定路徑） ---
	if (AutopilotRemaining > 0.0f)
	{
		AutopilotRemaining -= DeltaTime;
		// 曲率安全前瞻 0.9cm：模板谷曲率半徑 ≥2cm 下弦垂 ≈0.05cm；首版 2cm 前瞻
		// 在花瓣谷（ρ≈0.9cm 的舊振幅）切內側出帶＝t4/t7 事故——前瞻是曲率的函數
		const float Ahead = 0.9f;
		CursorPanel = RoutePointAtArc(CurS + Ahead) + Offset;
		bPenDown = true;
	}
	else if (VeerRemaining > 0.0f)
	{
		VeerRemaining -= DeltaTime;
		// 垂直於路線方向硬拉出帶
		const FVector2D A = RoutePointAtArc(CurS);
		const FVector2D B = RoutePointAtArc(CurS + 0.5f);
		FVector2D Dir = B - A;
		Dir.Normalize();
		CursorPanel = NeedlePanel + FVector2D(-Dir.Y, Dir.X) * 2.5f;
		bPenDown = true;
	}

	// 下針瞬間：游標收回針上（皮繩張力歸零起手＝割線「起點重合」語義的持針版）
	if (bPenDown && !bPrevPenDown)
	{
		CursorPanel = NeedlePanel;
	}

	// 游標鉗位：盤面邏輯域（整圖入鏡＝圖案範圍＋餘裕）防跑飛；
	// 皮繩鉗＝游標最多跑在針前皮繩長
	const float Bound = Figure.MaxAbsR + 4.0f;
	CursorPanel.X = FMath::Clamp(CursorPanel.X, -Bound, Bound);
	CursorPanel.Y = FMath::Clamp(CursorPanel.Y, -Bound, Bound);
	const float LeashCm = FMath::Max(C->TattooChaseLeashCm, 0.5f);
	const FVector2D ToCursor = CursorPanel - NeedlePanel;
	if (bPenDown && ToCursor.Size() > LeashCm)
	{
		CursorPanel = NeedlePanel + ToCursor.GetSafeNormal() * LeashCm;
	}

	// --- 針追趕（割線機制：機器擁有速度、手擁有路徑）＋越線判定 ---
	if (bPenDown)
	{
		const float VMax = C->TattooMaxSpeedCmPerSec();
		const FVector2D To = CursorPanel - NeedlePanel;
		const float Dist = To.Size();
		if (Dist > KINDA_SMALL_NUMBER)
		{
			const float StepCm = FMath::Min(VMax * DeltaTime, Dist);
			NeedlePanel += To.GetSafeNormal() * StepCm;
		}

		// 判定在圖形空間：圖被搖走＝針在圖上滑動
		const FVector2D NeedleFig = NeedlePanel - Offset;
		const float DistToRoute = ProjectNeedle(NeedleFig);
		if (DistToRoute > Params.BandHalfWidthCm)
		{
			FailReset();
			return;
		}

		// 記墨（圖形空間；重來全洗）
		if (InkFig.Num() == 0 || FVector2D::Distance(InkFig.Last(), NeedleFig) >= 0.15f)
		{
			InkFig.Add(NeedleFig);
		}

		// 描完：閉合＝繞完一整圈（帶號累積、方向自由）；開放＝走到終點
		const bool bDone = Figure.bClosed
			? FMath::Abs(ProgressS) >= Figure.TotalLen - CompleteSlackCm
			: CurS >= Figure.TotalLen - CompleteSlackCm;
		if (bDone)
		{
			bPendingComplete = true;
		}
	}
	// 抬針＝針凍在原地（盤面空間）、滑鼠自由；搖晃期間抬針＝安全
}

// --- robo 除錯 ---

void UDreamTraceComponent::DebugAutopilot(float Seconds)
{
	AutopilotRemaining = Seconds;
	VeerRemaining = 0.0f;
}

void UDreamTraceComponent::DebugVeerOff(float Seconds)
{
	VeerRemaining = Seconds;
	AutopilotRemaining = 0.0f;
}

void UDreamTraceComponent::DebugSetPaint(bool bHeld)
{
	bDebugPaintHeld = bHeld;
}

void UDreamTraceComponent::DebugForceComplete()
{
	bPendingForceComplete = true;
}

FString UDreamTraceComponent::GetDebugSummary() const
{
	const FVector2D P0 = Figure.Points.Num() > 0 ? Figure.Points[0] : FVector2D::ZeroVector;
	return FString::Printf(
		TEXT("traceActive=%d pen=%d curS=%.2f prog=%.2f total=%.2f fails=%d shake=%d ")
		TEXT("needle=(%.2f,%.2f) cursor=(%.2f,%.2f) sent=%d seed=%d retries=%d pts=%d ")
		TEXT("p0=(%.3f,%.3f) band=%.2f perim=%.1f maxR=%.2f motif=%d closed=%d"),
		bActive ? 1 : 0, bPenDown ? 1 : 0, CurS, ProgressS, Figure.TotalLen,
		FailCount, IsShakeActive() ? 1 : 0,
		NeedlePanel.X, NeedlePanel.Y, CursorPanel.X, CursorPanel.Y,
		bCompleteSent ? 1 : 0, Figure.UsedSeed, Figure.Retries, Figure.Points.Num(),
		P0.X, P0.Y, Params.BandHalfWidthCm, Params.PerimeterCm, Figure.MaxAbsR,
		static_cast<int32>(Figure.Motif), Figure.bClosed ? 1 : 0);
}

// --- 繪製 ---

void UDreamTraceComponent::DrawTracePanel(UCanvas* Canvas, const FVector2D& CenterPx, float RadiusPx)
{
	if (!bActive || !Canvas || !Figure.IsValid())
	{
		return;
	}

	// 整圖入鏡（08-02 user 裁決）：夢裡沒有絕對參考物——圖案自己就是唯一參考系。
	// 「速度」只有兩個有意義的定義：完成時間（線長÷針速=60s）與針速/帶寬比
	//（每秒 ~1.5 個帶寬＝割線「每秒幾個筆寬」的同構讀感）——兩者都縮放不變；
	// 手感（游標增益/帶寬）全在公分域＝縮放不變。先前「px/cm 對齊割線視圖」
	// 拿螢幕像素當參考系＝把割線視圖才有的實物參考搬進沒有參考物的夢＝錯誤
	// 座標系（整圖大於螢幕八倍、圖案辨識度歸零——user 抓「這是什麼圖案」實錘）。
	const float Scale = (RadiusPx * 0.86f) / FMath::Max(Figure.MaxAbsR + Params.BandHalfWidthCm + 1.5f, 1.0f);

	const FVector2D OffsetCm = ShakeOffsetCm();
	auto PanelToPx = [&](const FVector2D& Cm) { return CenterPx + Cm * Scale; };
	auto FigToPx = [&](const FVector2D& Cm) { return CenterPx + (Cm + OffsetCm) * Scale; };

	// 螢幕外裁剪（整張圖遠大於視窗——canvas 不裁、自己裁）
	const float CullMargin = 80.0f;
	auto OnScreen = [&](const FVector2D& Px)
	{
		return Px.X > -CullMargin && Px.X < Canvas->ClipX + CullMargin &&
			Px.Y > -CullMargin && Px.Y < Canvas->ClipY + CullMargin;
	};

	// 2) 路線帶（圖形空間；粗帶＝逐段 quad＋逐點圓盤——同色不透明疊蓋無縫；
	//    開放一筆畫不封口、兩端圓帽由端點圓盤天然提供）
	//    渲染抽點：0.25cm 全點畫帶 quad 太密——隔點取樣（視覺連續由圓盤補）
	{
		const float BandPx = Params.BandHalfWidthCm * Scale;
		TArray<FVector2D> Px;
		Px.Reserve(Figure.Points.Num() / 2 + 2);
		for (int32 i = 0; i < Figure.Points.Num(); i += 2)
		{
			Px.Add(FigToPx(Figure.Points[i]));
		}
		if (!Figure.bClosed)
		{
			Px.Add(FigToPx(Figure.Points.Last())); // 終點必入列（抽點不可吃掉端帽）
		}
		const int32 SegN = Figure.bClosed ? Px.Num() : Px.Num() - 1;
		auto SegVisible = [&](int32 i)
		{
			return OnScreen(Px[i]) || OnScreen(Px[(i + 1) % Px.Num()]);
		};

		TArray<FCanvasUVTri> Tris;
		for (int32 i = 0; i < Px.Num(); ++i)
		{
			if (i < SegN && SegVisible(i))
			{
				AddQuad(Tris, Px[i], Px[(i + 1) % Px.Num()], BandPx, TraceBandFill);
			}
			if (OnScreen(Px[i]))
			{
				AddDisc(Tris, Px[i], BandPx, TraceBandFill, 8);
			}
		}
		Canvas->K2_DrawTriangle(nullptr, Tris);

		// 帶輪廓羽化裙（三角形零 AA——自畫 ~1.5px 漸層裙；逐段可見才鋪）
		{
			const FLinearColor Clear = TraceBandFill.CopyWithNewOpacity(0.0f);
			TArray<FCanvasUVTri> Skirt;
			for (int32 i = 0; i < SegN; ++i)
			{
				if (!SegVisible(i))
				{
					continue;
				}
				const FVector2D& A = Px[i];
				const FVector2D& B = Px[(i + 1) % Px.Num()];
				FVector2D Dir = B - A;
				if (!Dir.Normalize())
				{
					continue;
				}
				const FVector2D N(-Dir.Y, Dir.X);
				for (float Side : { 1.0f, -1.0f })
				{
					const FVector2D A0 = A + N * (BandPx * Side);
					const FVector2D B0 = B + N * (BandPx * Side);
					const FVector2D A1 = A + N * ((BandPx + 1.5f) * Side);
					const FVector2D B1 = B + N * ((BandPx + 1.5f) * Side);
					AddTri(Skirt, A0, B0, B1, TraceBandFill, TraceBandFill, Clear);
					AddTri(Skirt, A0, B1, A1, TraceBandFill, Clear, Clear);
				}
			}
			Canvas->K2_DrawTriangle(nullptr, Skirt);
		}

		// 中線提示（細、暗一階——「沿這條描」的讀點）
		TArray<FCanvasUVTri> Mid;
		for (int32 i = 0; i < Px.Num(); ++i)
		{
			if (i < SegN && SegVisible(i))
			{
				AddQuad(Mid, Px[i], Px[(i + 1) % Px.Num()], 1.0f, TraceBandEdge);
			}
			if (OnScreen(Px[i]))
			{
				AddDisc(Mid, Px[i], 1.0f, TraceBandEdge, 6);
			}
		}
		Canvas->K2_DrawTriangle(nullptr, Mid);
	}

	// 3) 已描的線（圖形空間＝跟著圖搖；亮色、蓋在帶上；螢幕外裁剪）
	if (InkFig.Num() >= 1)
	{
		TArray<FCanvasUVTri> Tris;
		for (int32 i = 0; i + 1 < InkFig.Num(); ++i)
		{
			const FVector2D A = FigToPx(InkFig[i]);
			const FVector2D B = FigToPx(InkFig[i + 1]);
			if (!OnScreen(A) && !OnScreen(B))
			{
				continue;
			}
			AddQuad(Tris, A, B, 2.2f, TraceInkColor);
			AddDisc(Tris, A, 2.2f, TraceInkColor, 8);
		}
		AddDisc(Tris, FigToPx(InkFig.Last()), 2.2f, TraceInkColor, 8);
		Canvas->K2_DrawTriangle(nullptr, Tris);
	}

	// 4) 起點記號（金圈）；開放一筆畫再加終點記號（實心金點——「描到這裡」）
	{
		TArray<FCanvasUVTri> Tris;
		const FVector2D StartPx = FigToPx(Figure.Points[0]);
		if (OnScreen(StartPx))
		{
			AddDisc(Tris, StartPx, 6.0f, TraceStartColor, 12);
			AddDisc(Tris, StartPx, 3.2f, TraceDiscColor, 10);
		}
		if (!Figure.bClosed)
		{
			const FVector2D EndPx = FigToPx(Figure.Points.Last());
			if (OnScreen(EndPx))
			{
				AddDisc(Tris, EndPx, 5.0f, TraceStartColor, 12);
			}
		}
		if (Tris.Num() > 0)
		{
			Canvas->K2_DrawTriangle(nullptr, Tris);
		}
	}

	// 5) 針（盤面空間＝搖晃時螢幕上不動）＋游標十字
	{
		TArray<FCanvasUVTri> Tris;
		AddDisc(Tris, PanelToPx(NeedlePanel), 4.0f, TraceNeedleColor, 12);
		const FVector2D Cur = PanelToPx(CursorPanel);
		AddQuad(Tris, Cur + FVector2D(-6.0f, 0.0f), Cur + FVector2D(6.0f, 0.0f), 1.2f, TraceCursorColor);
		AddQuad(Tris, Cur + FVector2D(0.0f, -6.0f), Cur + FVector2D(0.0f, 6.0f), 1.2f, TraceCursorColor);
		Canvas->K2_DrawTriangle(nullptr, Tris);
	}

	// 6) 失敗紅閃（越線＝重來的當場回饋）：螢幕邊框紅框——不用頂點 alpha 蓋盤
	//（canvas 三角形 alpha 混合路徑不可信＝首輪截圖自查抓到不透明大紅餅；
	// 不透明色向底色 lerp＝零混合依賴的淡出）
	if (IsFailFlashing())
	{
		const float K = FMath::Clamp((FailFlashUntil - Now()) / FailFlashSec, 0.0f, 1.0f);
		const FLinearColor FrameCol = FMath::Lerp(TraceDiscColor, TraceFailColor, K * 0.9f);
		const float W = Canvas->ClipX;
		const float H = Canvas->ClipY;
		const float T = 8.0f; // 框厚 px
		TArray<FCanvasUVTri> Tris;
		auto AddRect = [&](const FVector2D& TL, const FVector2D& BR)
		{
			AddTri(Tris, TL, FVector2D(BR.X, TL.Y), BR, FrameCol, FrameCol, FrameCol);
			AddTri(Tris, TL, BR, FVector2D(TL.X, BR.Y), FrameCol, FrameCol, FrameCol);
		};
		AddRect(FVector2D(0, 0), FVector2D(W, T));
		AddRect(FVector2D(0, H - T), FVector2D(W, H));
		AddRect(FVector2D(0, T), FVector2D(T, H - T));
		AddRect(FVector2D(W - T, T), FVector2D(W, H - T));
		Canvas->K2_DrawTriangle(nullptr, Tris);
	}
}
