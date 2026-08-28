#include "NiceInkTvBroadcast.h"

#include "DreamTraceMotifData.h"

namespace NiceInkTvFilm
{
// 具名子 namespace（不是匿名）：unity build 會把不同 .cpp 的匿名 namespace 併進同一個 TU，
// 同名 helper 直接撞成 C2084（SmoothStep01 血價，見 CLAUDE.md 陷阱年鑑）。
namespace Detail
{

// ═══════════════════════════════════════════════════════════════════════════
// 色域校準（2026-08-28 量出來的；憑感覺改會整片變黑洞）
//
// 螢幕材質 M_TvScreen＝unlit，Emissive = ScreenTex × 8.0；道場曝光 bias 5.2。
// 舊版電視畫面的兩個「在 user viewport 裡亮度正常」的實測值：
//   演播藍 FLinearColor(0.085,0.115,0.30) ＝ sRGB byte (82, 95,149)
//   皮膚   FLinearColor(0.66 ,0.47 ,0.36) ＝ sRGB byte (212,182,162)
// ⇒ 本片全部在 **sRGB byte 域**作畫（貼圖 SRGB=true，取樣時硬體轉回線性），
//   值域刻意壓在 40~255：**純黑在這個曝光下讀成「螢幕破了一個洞」，不是夜色**。
//   夜的讀感靠「藍色相 + 燈籠的高對比亮點」，不靠絕對暗度。
// ═══════════════════════════════════════════════════════════════════════════

FORCEINLINE FColor Rgb(int32 R, int32 G, int32 B)
{
	return FColor(static_cast<uint8>(FMath::Clamp(R, 0, 255)),
		static_cast<uint8>(FMath::Clamp(G, 0, 255)),
		static_cast<uint8>(FMath::Clamp(B, 0, 255)), 255);
}

FORCEINLINE FColor Mix(const FColor& A, const FColor& B, float T)
{
	T = FMath::Clamp(T, 0.0f, 1.0f);
	return Rgb(FMath::RoundToInt(A.R + (B.R - A.R) * T),
		FMath::RoundToInt(A.G + (B.G - A.G) * T),
		FMath::RoundToInt(A.B + (B.B - A.B) * T));
}

// ── 調色盤 ────────────────────────────────────────────────────────────────
namespace Pal
{
	// 夜空／遠景
	const FColor SkyTop   = Rgb( 54,  62, 116);
	const FColor SkyLow   = Rgb(116, 104, 148);
	const FColor Hill     = Rgb( 48,  50,  90);
	const FColor Struct   = Rgb( 40,  42,  76); // 櫓・建物剪影
	const FColor Crowd    = Rgb( 34,  36,  66);
	const FColor CrowdLit = Rgb( 72,  66, 102);
	// 火・提燈
	const FColor LampCore = Rgb(255, 246, 210);
	const FColor LampGlow = Rgb(242, 170,  84);
	const FColor LampDeep = Rgb(198, 106,  46);
	const FColor WarmAir  = Rgb(126,  92, 104); // 夜氣裡的暖霧（背景加成用）
	// 肌
	const FColor SkinLit  = Rgb(208, 164, 126);
	const FColor SkinMid  = Rgb(158, 114,  86);
	const FColor SkinDark = Rgb(102,  72,  60);
	const FColor SkinRim  = Rgb(255, 232, 196); // 逆光輪廓＝「帥」的承重光
	// 布
	const FColor Indigo   = Rgb( 56,  72, 130);
	const FColor IndigoLo = Rgb( 34,  44,  88);
	const FColor Cloth    = Rgb(214, 220, 238);
	// 金（神輿）
	const FColor Gold     = Rgb(214, 168,  88);
	const FColor GoldHi   = Rgb(255, 228, 156);
	const FColor GoldLo   = Rgb(138,  98,  48);
	// 和彫：墨は「黒に近い藍」。08-28 一輪目は InkLight が明るすぎて背中全体が
	// 紫の塊に読めた（＝彫物ではなくリュックサックに見える）ので落とした。
	const FColor InkField = Rgb( 42,  34,  64);  // 地の墨
	const FColor InkDeep  = Rgb( 24,  18,  40);  // 最も濃い部分
	const FColor InkMid   = Rgb( 92,  78, 132);  // ぼかしの中間調（波・雲）
	const FColor InkLight = Rgb(206, 198, 220);  // 抜き（肌を残す明部）＝主図はここ
	const FColor InkRed   = Rgb(178,  54,  44);  // 差し色（牡丹・紅葉）
	// 放送 UI
	const FColor CapBg    = Rgb( 26,  28,  48);
	const FColor CapInk   = Rgb(224, 228, 242);
	const FColor Smoke    = Rgb(168, 164, 178);
	const FColor White    = Rgb(255, 255, 255);
	const FColor Hair     = Rgb( 36,  30,  46);
}

// ── 亂數／有序抖動 ────────────────────────────────────────────────────────
const uint8 GBayer4[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };

FORCEINLINE float BayerAt(int32 X, int32 Y)
{
	return (GBayer4[(Y & 3) * 4 + (X & 3)] + 0.5f) / 16.0f;
}

FORCEINLINE uint32 Hash3(int32 X, int32 Y, int32 Z)
{
	uint32 H = uint32(X) * 374761393u + uint32(Y) * 668265263u + uint32(Z) * 2246822519u;
	H = (H ^ (H >> 13)) * 1274126177u;
	return H ^ (H >> 16);
}

FORCEINLINE float Rand01(int32 X, int32 Y, int32 Z)
{
	return (Hash3(X, Y, Z) & 0xFFFFFF) / 16777216.0f;
}

FORCEINLINE float Ease(float T) { return T * T * (3.0f - 2.0f * T); }
FORCEINLINE float EaseOut(float T) { return 1.0f - (1.0f - T) * (1.0f - T); }
FORCEINLINE float Sat(float T) { return FMath::Clamp(T, 0.0f, 1.0f); }
FORCEINLINE float Seg01(float T, float A, float B) { return (B > A) ? Sat((T - A) / (B - A)) : 0.0f; }

// ── 畫布 ──────────────────────────────────────────────────────────────────
// 邊緣一律硬邊（零反鋸齒）：128×96 上的柔邊會讀成「現代的模糊圖」而不是舊電視；
// 需要軟過渡的地方（暈開／額彫ぼかし／輝光）一律走 Bayer 抖動＝映像管年代的正確語言。
struct FCan
{
	FColor* P = nullptr;

	FORCEINLINE bool In(int32 X, int32 Y) const
	{
		return uint32(X) < uint32(FilmW) && uint32(Y) < uint32(FilmH);
	}
	FORCEINLINE void Put(int32 X, int32 Y, const FColor& C)
	{
		if (In(X, Y)) { P[Y * FilmW + X] = C; }
	}
	FORCEINLINE void Blend(int32 X, int32 Y, const FColor& C, float A)
	{
		if (A <= 0.004f || !In(X, Y)) { return; }
		FColor& D = P[Y * FilmW + X];
		D = (A >= 0.996f) ? C : Mix(D, C, A);
	}
	// 抖動落點：柔度靠像素的開/關密度表達，不靠混色
	FORCEINLINE void DPut(int32 X, int32 Y, const FColor& C, float A)
	{
		if (A > BayerAt(X, Y)) { Put(X, Y, C); }
	}
	void Rect(float X0, float Y0, float X1, float Y1, const FColor& C, float A = 1.0f)
	{
		const int32 IX0 = FMath::FloorToInt(FMath::Min(X0, X1));
		const int32 IX1 = FMath::CeilToInt(FMath::Max(X0, X1));
		const int32 IY0 = FMath::FloorToInt(FMath::Min(Y0, Y1));
		const int32 IY1 = FMath::CeilToInt(FMath::Max(Y0, Y1));
		for (int32 Y = IY0; Y < IY1; ++Y)
			for (int32 X = IX0; X < IX1; ++X) { Blend(X, Y, C, A); }
	}
	void Ellipse(float Cx, float Cy, float Rx, float Ry, const FColor& C, float A = 1.0f)
	{
		if (Rx <= 0.0f || Ry <= 0.0f) { return; }
		const int32 IY0 = FMath::FloorToInt(Cy - Ry), IY1 = FMath::CeilToInt(Cy + Ry);
		for (int32 Y = IY0; Y <= IY1; ++Y)
		{
			const float Dy = (Y + 0.5f - Cy) / Ry;
			if (FMath::Abs(Dy) > 1.0f) { continue; }
			const float HalfW = Rx * FMath::Sqrt(FMath::Max(0.0f, 1.0f - Dy * Dy));
			for (int32 X = FMath::FloorToInt(Cx - HalfW); X <= FMath::CeilToInt(Cx + HalfW); ++X)
			{
				if (FMath::Abs(X + 0.5f - Cx) <= HalfW) { Blend(X, Y, C, A); }
			}
		}
	}
	// 徑向輝光（提燈／火）：中心實、外圈抖動散開
	void Glow(float Cx, float Cy, float R, const FColor& C, float A)
	{
		const int32 IY0 = FMath::FloorToInt(Cy - R), IY1 = FMath::CeilToInt(Cy + R);
		const int32 IX0 = FMath::FloorToInt(Cx - R), IX1 = FMath::CeilToInt(Cx + R);
		for (int32 Y = IY0; Y <= IY1; ++Y)
			for (int32 X = IX0; X <= IX1; ++X)
			{
				const float D = FVector2f(X + 0.5f - Cx, Y + 0.5f - Cy).Size() / FMath::Max(R, 0.001f);
				if (D >= 1.0f) { continue; }
				DPut(X, Y, C, A * (1.0f - D) * (1.0f - D));
			}
	}
	void Line(FVector2f A, FVector2f B, float Thick, const FColor& C, float Al = 1.0f)
	{
		const float Len = (B - A).Size();
		const int32 Steps = FMath::Max(1, FMath::CeilToInt(Len * 2.0f));
		const float R = FMath::Max(0.5f, Thick * 0.5f);
		for (int32 i = 0; i <= Steps; ++i)
		{
			const FVector2f Q = A + (B - A) * (float(i) / Steps);
			if (R <= 0.7f) { Blend(FMath::FloorToInt(Q.X), FMath::FloorToInt(Q.Y), C, Al); }
			else { Ellipse(Q.X, Q.Y, R, R, C, Al); }
		}
	}
	// 掃描列多邊形填充（凹多邊形可用）
	void Poly(const TArray<FVector2f>& Pt, const FColor& C, float A = 1.0f)
	{
		if (Pt.Num() < 3) { return; }
		float MinY = Pt[0].Y, MaxY = Pt[0].Y;
		for (const FVector2f& V : Pt) { MinY = FMath::Min(MinY, V.Y); MaxY = FMath::Max(MaxY, V.Y); }
		const int32 IY0 = FMath::Max(0, FMath::FloorToInt(MinY));
		const int32 IY1 = FMath::Min(FilmH - 1, FMath::CeilToInt(MaxY));
		TArray<float, TInlineAllocator<32>> Xs;
		for (int32 Y = IY0; Y <= IY1; ++Y)
		{
			const float Sy = Y + 0.5f;
			Xs.Reset();
			for (int32 i = 0, N = Pt.Num(); i < N; ++i)
			{
				const FVector2f& V0 = Pt[i];
				const FVector2f& V1 = Pt[(i + 1) % N];
				if ((V0.Y <= Sy) == (V1.Y <= Sy)) { continue; }
				Xs.Add(V0.X + (Sy - V0.Y) * (V1.X - V0.X) / (V1.Y - V0.Y));
			}
			if (Xs.Num() < 2) { continue; }
			Xs.Sort();
			for (int32 i = 0; i + 1 < Xs.Num(); i += 2)
			{
				for (int32 X = FMath::FloorToInt(Xs[i]); X <= FMath::CeilToInt(Xs[i + 1]); ++X)
				{
					if (X + 0.5f >= Xs[i] && X + 0.5f <= Xs[i + 1]) { Blend(X, Y, C, A); }
				}
			}
		}
	}
};

// ── 描圖 motif → 實心版型 ─────────────────────────────────────────────────
// 主題閉環（08-27 定案）：電視上的圖＝玩家三十秒後在夢裡要描的圖，資料原樣共用。
// 但**線框在這個尺寸讀成一團亂麻**（08-28 contact sheet 一輪實證：蛇被讀成隨機曲線）——
// 和彫在 40px 上是靠「實心色塊 + 紅色重點 + 覆蓋範圍」被認出來的，不是靠輪廓細節。
// 所以這裡填實心（記 2）、輪廓另記（記 1），讓呼叫端各自上色。
void StampMotif(uint8* Stencil, int32 MotifIdx, float Cx, float Cy, float FitW, float FitH, bool bFlipX)
{
	if (MotifIdx < 0 || MotifIdx >= NiceInkTraceMotifs::Num) { return; }
	const FDreamTraceBakedMotif& M = NiceInkTraceMotifs::Table[MotifIdx];
	if (M.NumPoints < 3) { return; }

	float MinX = M.Points[0], MaxX = M.Points[0], MinY = M.Points[1], MaxY = M.Points[1];
	for (int32 i = 1; i < M.NumPoints; ++i)
	{
		MinX = FMath::Min(MinX, M.Points[i * 2]);     MaxX = FMath::Max(MaxX, M.Points[i * 2]);
		MinY = FMath::Min(MinY, M.Points[i * 2 + 1]); MaxY = FMath::Max(MaxY, M.Points[i * 2 + 1]);
	}
	const float S = FMath::Min(FitW / FMath::Max(MaxX - MinX, 0.01f), FitH / FMath::Max(MaxY - MinY, 0.01f));
	const float Ox = (MinX + MaxX) * 0.5f, Oy = (MinY + MaxY) * 0.5f;

	TArray<FVector2f> Pt;
	Pt.Reserve(M.NumPoints);
	for (int32 i = 0; i < M.NumPoints; ++i)
	{
		const float X = (M.Points[i * 2] - Ox) * S * (bFlipX ? -1.0f : 1.0f);
		const float Y = -(M.Points[i * 2 + 1] - Oy) * S; // motif 是 y 朝上，畫布 y 朝下
		Pt.Add(FVector2f(Cx + X, Cy + Y));
	}

	float MnY = Pt[0].Y, MxY = Pt[0].Y;
	for (const FVector2f& V : Pt) { MnY = FMath::Min(MnY, V.Y); MxY = FMath::Max(MxY, V.Y); }
	TArray<float, TInlineAllocator<48>> Xs;
	for (int32 Y = FMath::Max(0, FMath::FloorToInt(MnY)); Y <= FMath::Min(FilmH - 1, FMath::CeilToInt(MxY)); ++Y)
	{
		const float Sy = Y + 0.5f;
		Xs.Reset();
		for (int32 i = 0, N = Pt.Num(); i < N; ++i)
		{
			const FVector2f& V0 = Pt[i];
			const FVector2f& V1 = Pt[(i + 1) % N];
			if ((V0.Y <= Sy) == (V1.Y <= Sy)) { continue; }
			Xs.Add(V0.X + (Sy - V0.Y) * (V1.X - V0.X) / (V1.Y - V0.Y));
		}
		if (Xs.Num() < 2) { continue; }
		Xs.Sort();
		for (int32 i = 0; i + 1 < Xs.Num(); i += 2)
		{
			for (int32 X = FMath::Max(0, FMath::FloorToInt(Xs[i]));
				X <= FMath::Min(FilmW - 1, FMath::CeilToInt(Xs[i + 1])); ++X)
			{
				if (X + 0.5f >= Xs[i] && X + 0.5f <= Xs[i + 1]) { Stencil[Y * FilmW + X] = 2; }
			}
		}
	}
	for (int32 i = 0, N = Pt.Num(); i < N; ++i)
	{
		const FVector2f A = Pt[i], B = Pt[(i + 1) % N];
		const int32 Steps = FMath::Max(1, FMath::CeilToInt((B - A).Size() * 2.0f));
		for (int32 k = 0; k <= Steps; ++k)
		{
			const FVector2f Q = A + (B - A) * (float(k) / Steps);
			const int32 X = FMath::FloorToInt(Q.X), Y = FMath::FloorToInt(Q.Y);
			if (uint32(X) < uint32(FilmW) && uint32(Y) < uint32(FilmH)) { Stencil[Y * FilmW + X] = 1; }
		}
	}
}

// ═══════════════════════════════════════════════════════════════════════════
// 共用元素
// ═══════════════════════════════════════════════════════════════════════════

void DrawNightSky(FCan& C, float Horizon)
{
	for (int32 Y = 0; Y < FilmH; ++Y)
	{
		const float T = FMath::Pow(Sat(Y / FMath::Max(Horizon, 1.0f)), 0.75f);
		const FColor Row = Mix(Pal::SkyTop, Pal::SkyLow, T);
		for (int32 X = 0; X < FilmW; ++X) { C.Put(X, Y, Row); }
	}
}

void DrawLantern(FCan& C, float X, float Y, float R, float Bright)
{
	C.Glow(X, Y, R * 3.2f, Pal::LampDeep, 0.42f * Bright);
	C.Ellipse(X, Y, R, R * 1.25f, Mix(Pal::LampDeep, Pal::LampGlow, Bright), 1.0f);
	C.Ellipse(X, Y, R * 0.55f, R * 0.8f, Mix(Pal::LampGlow, Pal::LampCore, Bright), 1.0f);
	C.Blend(FMath::FloorToInt(X), FMath::FloorToInt(Y + R * 1.5f), Pal::LampDeep, 0.8f);
}

void DrawLanternString(FCan& C, FVector2f A, FVector2f B, float Sag, int32 Count, int32 Seed, float Phase)
{
	for (int32 i = 0; i <= Count; ++i)
	{
		const float T = float(i) / Count;
		const float X = FMath::Lerp(A.X, B.X, T);
		const float Y = FMath::Lerp(A.Y, B.Y, T) + Sag * FMath::Sin(T * PI);
		const float Sway = FMath::Sin(Phase * 2.0f + i * 0.9f) * 0.6f;
		if (i > 0)
		{
			const float PT = float(i - 1) / Count;
			C.Line(FVector2f(FMath::Lerp(A.X, B.X, PT), FMath::Lerp(A.Y, B.Y, PT) + Sag * FMath::Sin(PT * PI)),
				FVector2f(X, Y), 1.0f, Pal::Struct, 0.7f);
		}
		DrawLantern(C, X + Sway, Y + 2.0f, 2.0f, 0.75f + 0.25f * FMath::Sin(Phase * 3.0f + i * 1.7f + Seed));
	}
}

void DrawCrowd(FCan& C, float TopY, int32 Seed, float Phase, const FColor& Col)
{
	for (int32 X = -4; X < FilmW + 4; X += 5)
	{
		const float J = Rand01(X, Seed, 7);
		const float Cx = X + J * 3.0f;
		const float R = 3.0f + J * 1.6f;
		const float Bob = FMath::Sin(Phase * 1.6f + X * 0.4f) * 0.8f;
		C.Ellipse(Cx, TopY + R + Bob, R, R * 1.05f, Col);
		C.Rect(Cx - R * 1.7f, TopY + R * 1.7f + Bob, Cx + R * 1.7f, FilmH, Col);
	}
}

// 台標（右上）：舊放送的常駐識別記號。真文字在 128px 上必成豆腐，改用記號。
void DrawStationBug(FCan& C, int32 FrameNo)
{
	const float A = 0.55f + 0.08f * FMath::Sin(FrameNo * 0.5f);
	C.Rect(110, 7, 121, 18, Pal::CapBg, 0.45f);
	C.Ellipse(115.5f, 12.5f, 4.2f, 4.2f, Pal::CapInk, A);
	C.Ellipse(115.5f, 12.5f, 2.0f, 2.0f, Pal::CapBg, A);
	C.Rect(114.5f, 8.5f, 116.5f, 16.5f, Pal::CapBg, A);
}

// 字幕帶：抽象「字」塊。128px 寬上真的日文字會糊成噪點，
// 而 2~4 條橫劃＋1~2 條豎劃的塊在低解析下正是漢字的讀感（舊影片字幕的正確抽象）。
void DrawCaption(FCan& C, float Y, int32 GlyphCount, int32 Seed, float Reveal)
{
	const float X0 = 10.0f, GW = 7.0f;
	C.Rect(X0 - 3.0f, Y - 2.0f, X0 + GlyphCount * GW + 3.0f, Y + 11.0f, Pal::CapBg, 0.72f);
	const int32 Shown = FMath::Clamp(FMath::CeilToInt(GlyphCount * Reveal), 0, GlyphCount);
	for (int32 g = 0; g < Shown; ++g)
	{
		const float Gx = X0 + g * GW;
		const uint32 H = Hash3(g, Seed, 31);
		const int32 Bars = 2 + int32(H % 3u);
		for (int32 b = 0; b < Bars; ++b)
		{
			const float By = Y + 1.0f + b * (7.0f / FMath::Max(Bars - 1, 1));
			C.Rect(Gx, By, Gx + 5.0f, By + 1.0f, Pal::CapInk, 0.95f);
		}
		const int32 Verts = 1 + int32((H >> 5) % 2u);
		for (int32 v = 0; v < Verts; ++v)
		{
			const float Vx = Gx + 1.0f + v * 2.5f;
			C.Rect(Vx, Y + 1.0f, Vx + 1.0f, Y + 8.0f, Pal::CapInk, 0.95f);
		}
	}
}

// 祭りの男（太鼓・担ぎ手の量産体）：頭＋なで肩＋胴。
// 08-28 一輪目は胴が「ただの縦棒」で電柱に読めた ⇒ **肩の傾斜が人体の識別特徴**。
void DrawFestivalMan(FCan& C, float Mx, float HeadY, float HeadR, float ShoulderHalf,
	float BottomY, const FColor& Body, bool bHachimaki)
{
	const float ShY = HeadY + HeadR * 1.9f;
	TArray<FVector2f> Torso;
	Torso.Add(FVector2f(Mx - ShoulderHalf * 0.45f, ShY - HeadR * 0.7f)); // 首の付け根
	Torso.Add(FVector2f(Mx - ShoulderHalf, ShY + 1.5f));                 // 肩（なで肩）
	Torso.Add(FVector2f(Mx - ShoulderHalf * 0.80f, BottomY));
	Torso.Add(FVector2f(Mx + ShoulderHalf * 0.80f, BottomY));
	Torso.Add(FVector2f(Mx + ShoulderHalf, ShY + 1.5f));
	Torso.Add(FVector2f(Mx + ShoulderHalf * 0.45f, ShY - HeadR * 0.7f));
	C.Poly(Torso, Body);
	C.Line(Torso[0], Torso[1], 1.0f, Pal::SkinRim, 0.85f); // 逆光の肩線
	C.Line(Torso[1], Torso[2], 1.0f, Pal::SkinRim, 0.75f);
	C.Ellipse(Mx, HeadY, HeadR, HeadR * 1.1f, Body);
	C.Line(FVector2f(Mx - HeadR, HeadY - HeadR * 0.3f), FVector2f(Mx, HeadY - HeadR * 1.05f),
		1.0f, Pal::SkinRim, 0.8f);
	if (bHachimaki)
	{
		C.Rect(Mx - HeadR - 0.5f, HeadY - HeadR * 0.75f, Mx + HeadR + 0.5f, HeadY - HeadR * 0.2f,
			Pal::Cloth, 0.92f);
	}
}

// ═══════════════════════════════════════════════════════════════════════════
// 主役の體：肩幅→腰の輪郭（背面）。Shot_Reveal / Shot_Turn 共用同一具身體。
// ═══════════════════════════════════════════════════════════════════════════
struct FTorso
{
	float Cx = 64.0f;
	float ShoulderY = 36.0f;
	float HipY = 96.0f;
	float HalfShoulder = 30.0f;
	float HalfHip = 21.0f;
	float ArmSpread = 5.0f;
	float ArmDrop = 42.0f;

	// 該掃描列的軀幹半寬（含背闊肌外弧＝倒三角的「帥」由此而來）
	float HalfAt(float Y) const
	{
		const float T = Sat((Y - ShoulderY) / FMath::Max(HipY - ShoulderY, 1.0f));
		const float Lat = 1.0f + 0.13f * FMath::Sin(Sat(T * 1.35f) * PI);
		return FMath::Lerp(HalfShoulder, HalfHip, T) * Lat;
	}
	float ArmK(float Y) const { return Sat((Y - ShoulderY) / FMath::Max(ArmDrop, 1.0f)); }
	float ArmX(int32 Side, float Y) const
	{
		return Cx + Side * ((HalfShoulder - 4.0f) + ArmSpread * ArmK(Y));
	}
	float ArmR(float Y) const { return FMath::Lerp(8.0f, 5.5f, ArmK(Y)); }
};

// 身體遮罩（軀幹＋雙臂＋頭）→ 之後用它做逆光輪廓光；輪廓光是這一段的承重光源。
void BuildBodyMask(uint8* Mask, const FTorso& T, float HeadCx, float HeadCy, float HeadRx, float HeadRy)
{
	FMemory::Memset(Mask, 0, FilmPx);
	auto Stamp = [&](int32 X, int32 Y)
	{
		if (uint32(X) < uint32(FilmW) && uint32(Y) < uint32(FilmH)) { Mask[Y * FilmW + X] = 1; }
	};

	for (int32 Y = FMath::FloorToInt(T.ShoulderY - 4.0f); Y < FilmH; ++Y)
	{
		float Half = T.HalfAt(FMath::Max(float(Y), T.ShoulderY));
		if (Y < T.ShoulderY) // 肩線：從脖根往外圓弧收上去
		{
			const float K = Sat((T.ShoulderY - Y) / 4.0f);
			Half *= FMath::Sqrt(FMath::Max(0.0f, 1.0f - K * K));
		}
		for (int32 X = FMath::FloorToInt(T.Cx - Half); X <= FMath::CeilToInt(T.Cx + Half); ++X) { Stamp(X, Y); }
	}
	for (int32 S = -1; S <= 1; S += 2)
	{
		for (int32 Y = FMath::FloorToInt(T.ShoulderY); Y < FilmH; ++Y)
		{
			const float Px = T.ArmX(S, static_cast<float>(Y)), R = T.ArmR(static_cast<float>(Y));
			for (int32 X = FMath::FloorToInt(Px - R); X <= FMath::CeilToInt(Px + R); ++X) { Stamp(X, Y); }
		}
	}
	for (int32 Y = FMath::FloorToInt(HeadCy); Y < FMath::CeilToInt(T.ShoulderY + 1.0f); ++Y)
	{
		for (int32 X = FMath::FloorToInt(HeadCx - HeadRx * 0.44f);
			X <= FMath::CeilToInt(HeadCx + HeadRx * 0.44f); ++X) { Stamp(X, Y); }
	}
	for (int32 Y = FMath::FloorToInt(HeadCy - HeadRy); Y <= FMath::CeilToInt(HeadCy + HeadRy); ++Y)
	{
		const float Dy = (Y + 0.5f - HeadCy) / HeadRy;
		if (FMath::Abs(Dy) > 1.0f) { continue; }
		const float HW = HeadRx * FMath::Sqrt(FMath::Max(0.0f, 1.0f - Dy * Dy));
		for (int32 X = FMath::FloorToInt(HeadCx - HW); X <= FMath::CeilToInt(HeadCx + HW); ++X) { Stamp(X, Y); }
	}
}

// 依遮罩上肌色＋逆光輪廓（左右各一道亮邊＝背後是燈籠的火）
void PaintBody(FCan& C, const uint8* Mask, float RimBoost)
{
	auto M = [&](int32 X, int32 Y) -> uint8
	{
		return (uint32(X) < uint32(FilmW) && uint32(Y) < uint32(FilmH)) ? Mask[Y * FilmW + X] : 0;
	};
	for (int32 Y = 0; Y < FilmH; ++Y)
		for (int32 X = 0; X < FilmW; ++X)
		{
			if (!M(X, Y)) { continue; }
			const float Vign = 1.0f - 0.18f * FMath::Abs((X - 64.0f) / 64.0f);
			FColor Base = Mix(Pal::SkinDark, Pal::SkinMid, Vign);
			const bool bEdge = !M(X - 1, Y) || !M(X + 1, Y) || !M(X, Y - 1);
			if (bEdge)
			{
				Base = Mix(Base, Pal::SkinRim, Sat(0.85f * RimBoost));
			}
			else if (!M(X - 2, Y) || !M(X + 2, Y))
			{
				Base = Mix(Base, Pal::SkinLit, Sat(0.45f * RimBoost));
			}
			C.Put(X, Y, Base);
		}
}

// 満背和彫。**読ませたいのは覆蓋の形**（背中一面＋七分袖＋背骨の抜き）であって
// 紋様の細部ではない。08-28 一輪目は領域が矩形だったので「背負っている物」に見えた
// ⇒ 楕円のぼかし境界にした（額彫＝墨は輪郭で終わらず散って消える）。
// 構図は本物の一面彫りに合わせる：地の墨 → 波（背景）→ 主図（折鶴＝抜き）→ 差し色（桜）。
void PaintBackpiece(FCan& C, const uint8* Mask, const FTorso& T, float TopY, float BotY)
{
	if (BotY <= TopY + 8.0f) { return; }
	const float MidY = (TopY + BotY) * 0.5f;
	const float RadY = (BotY - TopY) * 0.5f;
	const float HalfMid = T.HalfAt(MidY);

	TArray<uint8> Sten; Sten.SetNumZeroed(FilmPx);
	// 雲・波の帯（和彫の「地」）：斜めに流れる三本。**低解析度で「彫物だ」と読ませて
	// いるのはこの縞のリズム**であって主図の細部ではない（08-28 二輪目の実証：
	// 折鶴を実心で置いたら「白い星のロゴ」に読めた＝明暗が逆。本物の和彫は
	// 地が濃くて、明るいのはぼかしと抜きの細い帯だけ）。
	for (int32 b = 0; b < 3; ++b)
	{
		const float Base = MidY + RadY * (-0.46f + b * 0.50f);
		const float Slope = (b % 2 == 0) ? 0.30f : -0.26f;
		for (int32 X = 0; X < FilmW; ++X)
		{
			const float Rx = X - T.Cx;
			const float Yc = Base + Slope * Rx + FMath::Sin(Rx * 0.14f + b * 2.1f) * 2.6f;
			const int32 Y0 = FMath::FloorToInt(Yc);
			for (int32 k = 0; k < 5; ++k)
			{
				const int32 Py = Y0 + k;
				if (uint32(Py) < uint32(FilmH)) { Sten[Py * FilmW + X] = (k == 0) ? 4 : 3; }
			}
		}
	}
	// 主図＝折鶴（描図 motif 原様＝主題閉環）。**輪郭だけ**置く：実心にすると
	// 明部が支配して看板になる。線なら「濃い地の上に白く抜いた図」＝彫物の読み。
	TArray<uint8> Fig; Fig.SetNumZeroed(FilmPx);
	StampMotif(Fig.GetData(), NiceInkTraceMotifs::Idx_Crane,
		T.Cx + HalfMid * 0.08f, MidY - RadY * 0.30f, HalfMid * 0.92f, RadY * 0.66f, false);
	// 差し色＝桜（牡丹の代わり）。赤は一箇所だけ大きく＝目が行く場所を作る。
	TArray<uint8> Red; Red.SetNumZeroed(FilmPx);
	StampMotif(Red.GetData(), NiceInkTraceMotifs::Idx_Sakura,
		T.Cx - HalfMid * 0.52f, MidY + RadY * 0.44f, 21.0f, 21.0f, false);
	StampMotif(Red.GetData(), NiceInkTraceMotifs::Idx_Sakura,
		T.Cx + HalfMid * 0.58f, MidY + RadY * 0.08f, 13.0f, 13.0f, true);

	for (int32 Y = FMath::Max(0, FMath::FloorToInt(TopY - 4.0f));
		Y <= FMath::Min(FilmH - 1, FMath::CeilToInt(BotY + 4.0f)); ++Y)
	{
		const float Half = T.HalfAt(FMath::Max(float(Y), T.ShoulderY)) * 0.97f;
		for (int32 X = FMath::Max(0, FMath::FloorToInt(T.Cx - Half));
			X <= FMath::Min(FilmW - 1, FMath::CeilToInt(T.Cx + Half)); ++X)
		{
			if (!Mask[Y * FilmW + X]) { continue; }
			const float Dx = (X + 0.5f - T.Cx) / FMath::Max(Half, 1.0f);
			const float Dy = (Y + 0.5f - MidY) / FMath::Max(RadY, 1.0f);
			float Cov = 1.0f - Seg01(FMath::Sqrt(Dx * Dx + Dy * Dy), 0.80f, 1.06f);
			// 背骨の抜き：上半分だけ（下は繋がる＝本物の一面彫りの割り）
			const float SpineOn = 1.0f - Seg01(float(Y), MidY - RadY * 0.05f, MidY + RadY * 0.40f);
			Cov *= FMath::Lerp(1.0f, Seg01(FMath::Abs(X + 0.5f - T.Cx), 0.6f, 2.2f), SpineOn);
			if (Cov <= 0.02f) { continue; }

			const uint8 S = Sten[Y * FilmW + X];   // 3=ぼかしの帯 4=帯の明縁
			const uint8 F = Fig[Y * FilmW + X];    // 1=主図の輪郭（抜き線）
			const uint8 R = Red[Y * FilmW + X];    // 2=桜の実心 1=その輪郭
			FColor Ink = (((X * 7 + Y * 11) % 13) < 3) ? Pal::InkDeep : Pal::InkField;
			if (S == 3) { Ink = Pal::InkMid; }
			if (S == 4) { Ink = Pal::InkLight; }
			if (R == 2) { Ink = Pal::InkRed; }
			else if (R == 1) { Ink = Pal::InkDeep; }
			if (F == 1) { Ink = Pal::InkLight; }   // 主図の線が最前面＝図がぼやけない
			C.DPut(X, Y, Ink, Cov);
		}
	}

	// 七分袖：上腕の途中でぼかして終わる＝和彫の一番分かりやすい識別特徴
	const float SleeveEnd = FMath::Lerp(TopY, BotY, 0.62f);
	for (int32 S = -1; S <= 1; S += 2)
	{
		for (int32 Y = FMath::Max(0, FMath::FloorToInt(T.ShoulderY));
			Y <= FMath::Min(FilmH - 1, FMath::CeilToInt(SleeveEnd)); ++Y)
		{
			const float Ax = T.ArmX(S, float(Y)), Ar = T.ArmR(float(Y));
			const float Fade = 1.0f - Seg01(float(Y), SleeveEnd - 13.0f, SleeveEnd);
			for (int32 X = FMath::Max(0, FMath::FloorToInt(Ax - Ar));
				X <= FMath::Min(FilmW - 1, FMath::CeilToInt(Ax + Ar)); ++X)
			{
				if (!Mask[Y * FilmW + X]) { continue; }
				if (FMath::Abs(X + 0.5f - T.Cx) < T.HalfAt(FMath::Max(float(Y), T.ShoulderY)) * 0.96f) { continue; }
				const int32 M3 = (X * 3 + Y * 5) % 11;
				const FColor Ink = (M3 < 2) ? Pal::InkMid : ((M3 < 4) ? Pal::InkDeep : Pal::InkField);
				C.DPut(X, Y, Ink, Fade * 0.95f);
			}
		}
	}
}

// 横顔の突出（鼻・唇・顎）を**身體遮罩に足す**。
// 08-28 四輪目の実証：稜線を頭の楕円の**内側**に線で引いても、シルエットは
// 卵のままなので「サングラスを掛けた坊主」に読める。**顔はシルエットの
// 出っ張りで読まれる**（低解析度では特にそう）⇒ 遮罩に足してから塗れば、
// PaintBody の縁検出が鼻筋と顎に勝手に輪郭光を乗せる。
// K=0 では全点が頭蓋の内側に潰れる＝後頭部のときは何も出ない（構造で保証）。
void StampProfileIntoMask(uint8* Mask, float HeadCx, float HeadCy, float HeadRx, float HeadRy,
	float K, float FaceDir)
{
	// (前方への張り出し倍率, 頭の縦半径に対する高さ)
	static const float Prof[8][2] = {
		{ 0.62f, -0.35f },  // 眉の上（頭蓋の表面から始める）
		{ 1.24f,  0.06f },  // 鼻先＝一番出るところ
		{ 0.88f,  0.20f },  // 鼻下
		{ 1.06f,  0.33f },  // 上唇
		{ 0.86f,  0.46f },  // 口角
		{ 1.12f,  0.66f },  // 顎（尖らせる＝user 指定の「誇張」）
		{ 0.42f,  0.92f },  // 顎裏
		{ 0.10f,  0.66f },  // 首側へ戻る
	};
	TArray<FVector2f> Pt;
	Pt.Reserve(8);
	for (int32 i = 0; i < 8; ++i)
	{
		Pt.Add(FVector2f(HeadCx + FaceDir * HeadRx * FMath::Lerp(0.55f, Prof[i][0], K),
			HeadCy + HeadRy * Prof[i][1]));
	}
	float MinY = Pt[0].Y, MaxY = Pt[0].Y;
	for (const FVector2f& V : Pt) { MinY = FMath::Min(MinY, V.Y); MaxY = FMath::Max(MaxY, V.Y); }
	TArray<float, TInlineAllocator<16>> Xs;
	for (int32 Y = FMath::Max(0, FMath::FloorToInt(MinY)); Y <= FMath::Min(FilmH - 1, FMath::CeilToInt(MaxY)); ++Y)
	{
		const float Sy = Y + 0.5f;
		Xs.Reset();
		for (int32 i = 0; i < 8; ++i)
		{
			const FVector2f& V0 = Pt[i];
			const FVector2f& V1 = Pt[(i + 1) % 8];
			if ((V0.Y <= Sy) == (V1.Y <= Sy)) { continue; }
			Xs.Add(V0.X + (Sy - V0.Y) * (V1.X - V0.X) / (V1.Y - V0.Y));
		}
		if (Xs.Num() < 2) { continue; }
		Xs.Sort();
		for (int32 i = 0; i + 1 < Xs.Num(); i += 2)
		{
			for (int32 X = FMath::Max(0, FMath::FloorToInt(Xs[i]));
				X <= FMath::Min(FilmW - 1, FMath::CeilToInt(Xs[i + 1])); ++X)
			{
				if (X + 0.5f >= Xs[i] && X + 0.5f <= Xs[i + 1]) { Mask[Y * FilmW + X] = 1; }
			}
		}
	}
}

// リーゼント（ポンパドール）：Turn=0 で後頭部、1 で横顔。
// 低解析度で「イケてる」を運ぶのは造作ではなくこの盛りのシルエット。
void DrawPompadour(FCan& C, float HeadCx, float HeadCy, float HeadRx, float HeadRy,
	float Turn, float FaceDir)
{
	TArray<FVector2f> Hair;
	const float Fx = HeadCx + FaceDir * HeadRx;
	const float Bx = HeadCx - FaceDir * HeadRx;
	Hair.Add(FVector2f(Bx - FaceDir * 1.5f, HeadCy + HeadRy * 0.15f));
	Hair.Add(FVector2f(Bx - FaceDir * 2.5f, HeadCy - HeadRy * 0.55f));
	Hair.Add(FVector2f(HeadCx, HeadCy - HeadRy * 1.24f));
	Hair.Add(FVector2f(Fx + FaceDir * 2.4f * Turn, HeadCy - HeadRy * (1.04f - 0.10f * Turn)));
	Hair.Add(FVector2f(Fx + FaceDir * 0.5f, HeadCy - HeadRy * 0.34f));
	Hair.Add(FVector2f(HeadCx, HeadCy - HeadRy * 0.30f));
	C.Poly(Hair, Pal::Hair);
	C.Line(Hair[1], Hair[2], 1.0f, Pal::SkinRim, 0.85f);
	C.Line(Hair[2], Hair[3], 1.0f, Pal::SkinRim, 0.9f);
}

// 半纏（法被）：無地の藍＋白い衿＋背中の白い家紋。
// 08-28 一輪目は白の縦縞にしたら「ストライプのシャツ」に読めた ⇒ 紋一つで足りる。
void DrawHappi(FCan& C, const uint8* Mask, const FTorso& T, float CoatY, float Ph)
{
	for (int32 Y = FMath::Max(0, FMath::FloorToInt(CoatY)); Y < FilmH; ++Y)
	{
		const float Wave = FMath::Sin(Ph * 3.0f + Y * 0.35f) * 2.0f * Seg01(float(Y), CoatY + 6.0f, FilmH);
		for (int32 X = 0; X < FilmW; ++X)
		{
			if (!Mask[Y * FilmW + X]) { continue; }
			const int32 Wx = X + FMath::RoundToInt(Wave);
			const float Half = T.HalfAt(FMath::Max(float(Y), T.ShoulderY));
			const bool bEdge = FMath::Abs(Wx + 0.5f - T.Cx) > Half - 2.0f;
			C.Put(Wx, Y, bEdge ? Pal::IndigoLo : Pal::Indigo);
		}
	}
	// 家紋（白丸に横一文字）——背中の中央、衿から少し下
	const float CrestY = CoatY + 13.0f;
	if (CrestY < FilmH - 4.0f)
	{
		C.Ellipse(T.Cx, CrestY, 7.0f, 7.0f, Pal::Cloth, 0.95f);
		C.Ellipse(T.Cx, CrestY, 4.6f, 4.6f, Pal::Indigo, 1.0f);
		C.Rect(T.Cx - 4.6f, CrestY - 1.0f, T.Cx + 4.6f, CrestY + 1.0f, Pal::Cloth, 1.0f);
	}
	// 衿（滑り落ちた縁）＝白い一本線：ここが「脱げていく」の読みどころ
	const float HalfC = T.HalfAt(FMath::Max(CoatY, T.ShoulderY));
	C.Line(FVector2f(T.Cx - HalfC, CoatY), FVector2f(T.Cx + HalfC, CoatY), 2.0f, Pal::Cloth, 0.95f);
}

// ═══════════════════════════════════════════════════════════════════════════
// 分鏡
// ═══════════════════════════════════════════════════════════════════════════

// ① 夜祭遠景：櫓・提燈・人群。這一拍時電視在全景裡很小，只要讀得出
//    「一台亮著的電視在放有動靜的暖色畫面」就夠。
void ShotYomatsuri(FCan& C, float U, float Seconds, int32 FrameNo)
{
	const float Ph = Seconds * 3.0f; // 物理速度＝実秒（鏡長を変えても揺れ／拍は変わらない）
	DrawNightSky(C, 62.0f);

	TArray<FVector2f> Hill;
	Hill.Add(FVector2f(-4, FilmH));
	for (int32 i = 0; i <= 8; ++i)
	{
		const float X = -4.0f + i * (FilmW + 8.0f) / 8.0f;
		Hill.Add(FVector2f(X, 58.0f - 6.0f * FMath::Sin(i * 1.3f) - 3.0f * FMath::Sin(i * 2.7f)));
	}
	Hill.Add(FVector2f(FilmW + 4, FilmH));
	C.Poly(Hill, Pal::Hill);

	C.Glow(64, 70, 52, Pal::WarmAir, 0.55f); // 祭場の熱気

	// 櫓（やぐら）
	const float Bx = 64.0f, BaseY = 76.0f, TopY = 30.0f;
	C.Line(FVector2f(Bx - 13, BaseY), FVector2f(Bx - 8, TopY + 12), 2.0f, Pal::Struct);
	C.Line(FVector2f(Bx + 13, BaseY), FVector2f(Bx + 8, TopY + 12), 2.0f, Pal::Struct);
	C.Line(FVector2f(Bx - 8, BaseY - 16), FVector2f(Bx + 8, BaseY - 16), 1.0f, Pal::Struct);
	C.Rect(Bx - 11, TopY + 10, Bx + 11, TopY + 13, Pal::Struct);
	TArray<FVector2f> Roof;
	Roof.Add(FVector2f(Bx - 16, TopY + 10)); Roof.Add(FVector2f(Bx, TopY - 1));
	Roof.Add(FVector2f(Bx + 16, TopY + 10));
	C.Poly(Roof, Pal::Struct);
	{
		const float Beat = FMath::Sin(Ph * 3.1f);
		C.Ellipse(Bx, TopY + 6.0f, 2.0f, 2.4f, Pal::Struct);
		C.Line(FVector2f(Bx, TopY + 8), FVector2f(Bx + 4, TopY + 4 - 2.0f * Beat), 1.0f, Pal::Struct);
	}

	DrawLanternString(C, FVector2f(-6, 26), FVector2f(Bx - 14, TopY + 2), 6.0f, 7, 3, Ph);
	DrawLanternString(C, FVector2f(Bx + 14, TopY + 2), FVector2f(FilmW + 6, 26), 6.0f, 7, 11, Ph);
	DrawLanternString(C, FVector2f(-6, 46), FVector2f(FilmW + 6, 44), 5.0f, 11, 23, Ph * 0.8f);

	DrawCrowd(C, 74.0f, 5, Ph, Pal::CrowdLit);
	DrawCrowd(C, 84.0f, 17, Ph * 1.2f, Pal::Crowd);

	for (int32 i = 0; i < 14; ++i) // 火の粉
	{
		const float Sx = Rand01(i, 1, 2) * FilmW;
		const float Sy = FMath::Fmod(80.0f - (Rand01(i, 2, 3) * 60.0f + Ph * 9.0f), 76.0f) + 12.0f;
		C.DPut(FMath::FloorToInt(Sx), FMath::FloorToInt(Sy), Pal::LampCore, 0.8f);
	}

	DrawCaption(C, 80.0f, 6, 101, Seg01(U, 0.10f, 0.45f) * (1.0f - Seg01(U, 0.80f, 0.95f)));
	DrawStationBug(C, FrameNo);
}

// ② 太鼓：裸上身の男二人。祭＝身體と汗；「刺青が祭の中にある」の伏線。
void ShotTaiko(FCan& C, float U, float Seconds, int32 FrameNo)
{
	const float Ph = Seconds * 3.0f; // 物理速度＝実秒（鏡長を変えても揺れ／拍は変わらない）
	const float Beat = FMath::Sin(Ph * 4.4f);
	const float Hit = FMath::Max(0.0f, Beat);

	for (int32 Y = 0; Y < FilmH; ++Y)
	{
		const FColor Row = Mix(Rgb(74, 62, 104), Rgb(120, 84, 82), Sat(Y / 95.0f));
		for (int32 X = 0; X < FilmW; ++X) { C.Put(X, Y, Row); }
	}
	for (int32 i = 0; i < 7; ++i)
	{
		const float Bx = 8.0f + i * 19.0f + FMath::Sin(Ph + i) * 1.5f;
		const float By = 14.0f + Rand01(i, 4, 9) * 16.0f;
		C.Glow(Bx, By, 9.0f, Pal::LampGlow, 0.40f);
		C.Ellipse(Bx, By, 3.0f, 3.4f, Pal::LampCore, 0.85f);
	}
	DrawCrowd(C, 62.0f, 29, Ph, Rgb(58, 48, 76));

	// 打ち手二名（太鼓より先に＝後ろに立つ）
	for (int32 S = -1; S <= 1; S += 2)
	{
		const float Mx = 64.0f + S * 33.0f;
		DrawFestivalMan(C, Mx, 36.0f, 6.5f, 13.0f, 96.0f, Pal::SkinDark, true);
		C.Ellipse(Mx + S * 7.0f, 52.0f, 5.0f, 7.0f, Pal::InkField, 0.85f); // 肩の彫物＝伏線
		C.Ellipse(Mx + S * 7.0f, 50.0f, 2.2f, 3.0f, Pal::InkMid, 0.8f);
	}

	// 太鼓（正面）：胴＋革面＋鋲
	const float Dx = 64.0f, Dy = 66.0f, Rx = 25.0f + Hit * 1.2f, Ry = 21.0f + Hit * 1.0f;
	C.Ellipse(Dx, Dy, Rx + 3.0f, Ry + 3.0f, Pal::GoldLo);
	C.Ellipse(Dx, Dy, Rx, Ry, Mix(Rgb(196, 158, 118), Pal::GoldHi, Hit * 0.5f));
	C.Ellipse(Dx, Dy, Rx * 0.86f, Ry * 0.86f, Rgb(214, 178, 134));
	for (int32 i = 0; i < 16; ++i)
	{
		const float A = 2.0f * PI * i / 16.0f;
		C.Blend(FMath::FloorToInt(Dx + FMath::Cos(A) * (Rx + 1.4f)),
			FMath::FloorToInt(Dy + FMath::Sin(A) * (Ry + 1.4f)), Pal::GoldHi, 0.9f);
	}
	if (Hit > 0.90f) { C.Glow(Dx, Dy, 30.0f, Pal::LampCore, 0.16f); }

	// 振り上げる腕＋撥（太鼓の手前）
	for (int32 S = -1; S <= 1; S += 2)
	{
		const float Mx = 64.0f + S * 33.0f;
		const float Swing = (S < 0) ? Beat : -Beat;
		const float Hy = 48.0f - Swing * 12.0f;
		const float HxIn = Mx - S * 15.0f;
		C.Line(FVector2f(Mx - S * 8.0f, 52.0f), FVector2f(HxIn, Hy), 5.0f, Pal::SkinMid);
		C.Line(FVector2f(Mx - S * 8.0f, 52.0f), FVector2f(HxIn, Hy), 1.5f, Pal::SkinRim, 0.55f);
		C.Line(FVector2f(HxIn, Hy), FVector2f(HxIn - S * 9.0f, Hy + 11.0f), 2.0f, Pal::Cloth, 0.95f);
	}

	DrawCaption(C, 80.0f, 5, 202, Seg01(U, 0.05f, 0.35f));
	DrawStationBug(C, FrameNo);
}

// ③ 神輿渡御：横搖（pan）。担ぎ手の腕に彫物＝「刺青と祭は同じ場所にある」。
void ShotMikoshi(FCan& C, float U, float Seconds, int32 FrameNo)
{
	const float Ph = Seconds * 3.0f; // 物理速度＝実秒（鏡長を変えても揺れ／拍は変わらない）
	const float Pan = FMath::Lerp(22.0f, -22.0f, EaseOut(U));
	DrawNightSky(C, 60.0f);
	C.Glow(64, 62, 56, Pal::WarmAir, 0.5f);
	DrawLanternString(C, FVector2f(-8 + Pan * 0.4f, 20), FVector2f(FilmW + 8 + Pan * 0.4f, 24), 5.0f, 9, 41, Ph);

	const float Mx = 64.0f + Pan, My = 44.0f + FMath::Sin(Ph * 2.2f) * 1.8f;
	C.Rect(Mx - 18, My - 4, Mx + 18, My + 14, Pal::Gold);
	C.Rect(Mx - 18, My - 4, Mx + 18, My + 1, Pal::GoldHi, 0.55f);
	TArray<FVector2f> MRoof;
	MRoof.Add(FVector2f(Mx - 25, My - 4)); MRoof.Add(FVector2f(Mx - 8, My - 16));
	MRoof.Add(FVector2f(Mx + 8, My - 16)); MRoof.Add(FVector2f(Mx + 25, My - 4));
	C.Poly(MRoof, Pal::GoldLo);
	C.Line(FVector2f(Mx - 25, My - 4), FVector2f(Mx - 8, My - 16), 1.0f, Pal::GoldHi, 0.9f);
	C.Line(FVector2f(Mx + 8, My - 16), FVector2f(Mx + 25, My - 4), 1.0f, Pal::GoldHi, 0.9f);
	C.Ellipse(Mx, My - 20, 2.4f, 3.2f, Pal::GoldHi); // 鳳凰
	C.Line(FVector2f(Mx, My - 22), FVector2f(Mx + 5, My - 26), 1.0f, Pal::GoldHi);
	C.Line(FVector2f(Mx, My - 22), FVector2f(Mx - 5, My - 25), 1.0f, Pal::GoldHi);
	C.Ellipse(Mx, My + 5, 6.0f, 6.0f, Pal::LampCore, 0.9f);                                 // 御神燈
	C.Line(FVector2f(Mx - 48, My + 15), FVector2f(Mx + 48, My + 15), 3.0f, Pal::GoldLo);    // 担ぎ棒

	// 担ぎ手：頭＋なで肩＋掲げた両腕（腕が棒に届いていることが読みどころ）
	for (int32 i = -5; i <= 5; ++i)
	{
		const float Hx = Mx + i * 11.0f + FMath::Sin(Ph * 2.0f + i) * 0.8f;
		const float Hy = My + 30.0f + FMath::Sin(Ph * 2.2f + i * 0.7f) * 1.2f;
		const bool bInk = (i == -3 || i == 1 || i == 4);
		DrawFestivalMan(C, Hx, Hy, 4.6f, 9.5f, 96.0f, bInk ? Pal::InkField : Pal::SkinDark, false);
		for (int32 S = -1; S <= 1; S += 2)
		{
			C.Line(FVector2f(Hx + S * 8.0f, Hy + 10.0f), FVector2f(Hx + S * 10.0f, My + 16.0f),
				4.0f, bInk ? Pal::InkField : Pal::SkinMid);
			C.Line(FVector2f(Hx + S * 8.0f, Hy + 10.0f), FVector2f(Hx + S * 10.0f, My + 16.0f),
				1.2f, Pal::SkinRim, 0.6f);
		}
		if (bInk)
		{
			C.Ellipse(Hx - 9, My + 24, 2.6f, 6.0f, Pal::InkMid, 0.6f);
			C.Ellipse(Hx + 3, Hy + 16, 2.4f, 3.0f, Pal::InkRed, 0.65f);
		}
	}

	DrawCaption(C, 80.0f, 7, 303, Seg01(U, 0.08f, 0.40f) * (1.0f - Seg01(U, 0.85f, 1.0f)));
	DrawStationBug(C, FrameNo);
}

// 主役の背景（ぼけた祭＋背後の火）：Reveal / Turn 共用
void DrawHeroBackdrop(FCan& C, float Ph, float Zoom, int32 Lanterns, int32 Seed)
{
	for (int32 Y = 0; Y < FilmH; ++Y)
	{
		const FColor Row = Mix(Rgb(66, 60, 106), Rgb(112, 78, 84), Sat(Y / 95.0f));
		for (int32 X = 0; X < FilmW; ++X) { C.Put(X, Y, Row); }
	}
	for (int32 i = 0; i < Lanterns; ++i)
	{
		const float Bx = 4.0f + i * (120.0f / Lanterns) + FMath::Sin(Ph * 0.8f + i) * 1.2f;
		const float By = 8.0f + Rand01(i, Seed, 12) * 30.0f;
		C.Glow(Bx, By, 11.0f * Zoom, Pal::LampGlow, 0.34f);
		C.Ellipse(Bx, By, 2.6f * Zoom, 3.0f * Zoom, Pal::LampCore, 0.7f);
	}
	DrawCrowd(C, 58.0f, 61, Ph, Rgb(52, 46, 78));
	C.Glow(64, 44, 42.0f * Zoom, Pal::WarmAir, 0.55f); // 背後の火＝逆光源
}

// ④ 主役登場：半纏が肩から滑り落ち、満背和彫が逆光の中に現れる。
//    これが「刺青＝あの人たちのもの」を力士に見せる一拍。被写体は**背中**。
void ShotReveal(FCan& C, float U, float Seconds, int32 FrameNo)
{
	const float Ph = Seconds * 3.0f; // 物理速度＝実秒（鏡長を変えても揺れ／拍は変わらない）
	const float Zoom = 1.06f + 0.18f * EaseOut(U);
	DrawHeroBackdrop(C, Ph, Zoom, 9, 6);

	FTorso T;
	T.Cx = 64.0f;
	T.ShoulderY = 38.0f;
	T.HipY = 104.0f;
	T.HalfShoulder = 30.0f * Zoom;
	T.HalfHip = 21.0f * Zoom;
	T.ArmSpread = 5.0f * Zoom;
	T.ArmDrop = 44.0f;
	const float HeadRx = 10.5f * Zoom, HeadRy = 12.0f * Zoom;
	const float HeadCy = T.ShoulderY - 12.0f * Zoom;

	TArray<uint8> Mask; Mask.SetNumUninitialized(FilmPx);
	BuildBodyMask(Mask.GetData(), T, T.Cx, HeadCy, HeadRx, HeadRy);
	PaintBody(C, Mask.GetData(), 1.0f);
	DrawPompadour(C, T.Cx, HeadCy, HeadRx, HeadRy, 0.0f, 1.0f);

	// 半纏が滑り落ちる：上緣 Y が肩→腰へ。彫物は落ちた分だけ見えていく。
	const float CoatY = FMath::Lerp(T.ShoulderY - 1.0f, T.HipY - 10.0f, Ease(Seg01(U, 0.06f, 0.62f)));
	PaintBackpiece(C, Mask.GetData(), T, T.ShoulderY + 4.0f, CoatY - 2.0f);
	DrawHappi(C, Mask.GetData(), T, CoatY, Ph);

	DrawCaption(C, 82.0f, 8, 404, Seg01(U, 0.30f, 0.60f));
	DrawStationBug(C, FrameNo);
}

// ⑤ 振り返り：ここが交付点。被写体は**男そのもの**なので寄る（顔を大きく）。
//    低解析度で「帥」を運ぶのは顔の造作ではなく、輪郭光・顎を上げた側面・
//    サングラスの反射・咥え煙草・風になびく半纏。
//    user 指定「甚至有點誇張、典型的帥哥刻板印象」⇒ キラッ（星の煌めき）を入れる。
void ShotTurn(FCan& C, float U, float Seconds, int32 FrameNo)
{
	const float Ph = Seconds * 3.0f; // 物理速度＝実秒（鏡長を変えても揺れ／拍は変わらない）
	const float Zoom = 1.55f + 0.14f * EaseOut(U);
	const float Turn = Ease(Seg01(U, 0.08f, 0.52f)); // 0=背中 1=横顔
	const float FaceDir = 1.0f;                      // 右を向く
	DrawHeroBackdrop(C, Ph, Zoom, 7, 8);

	FTorso T;
	T.Cx = 62.0f - 4.0f * Turn; // 振り返りで重心が僅かに寄る
	T.ShoulderY = 58.0f;        // 低い＝肩から下でフレームアウト＝寄りの構図
	T.HipY = 128.0f;
	T.HalfShoulder = 30.0f * Zoom;
	T.HalfHip = 22.0f * Zoom;
	T.ArmSpread = 5.0f * Zoom;
	T.ArmDrop = 46.0f;
	const float HeadRx = 10.5f * Zoom * FMath::Lerp(1.0f, 0.82f, Turn);
	const float HeadRy = 12.5f * Zoom;
	const float HeadCx = T.Cx + 3.0f * Turn;
	const float HeadCy = T.ShoulderY - 14.0f * Zoom;

	TArray<uint8> Mask; Mask.SetNumUninitialized(FilmPx);
	BuildBodyMask(Mask.GetData(), T, HeadCx, HeadCy, HeadRx, HeadRy);
	const float K = Seg01(Turn, 0.24f, 1.0f);
	// 顔はシルエットで読まれる ⇒ 塗る前に横顔を遮罩へ足す（縁の輪郭光は自動で乗る）
	StampProfileIntoMask(Mask.GetData(), HeadCx, HeadCy, HeadRx, HeadRy, K, FaceDir);
	PaintBody(C, Mask.GetData(), 1.0f);

	// 背は見えたまま（＝力士が羨むもの）。フレーム下端まで彫物。
	PaintBackpiece(C, Mask.GetData(), T, T.ShoulderY + 4.0f, T.HipY - 30.0f);

	DrawPompadour(C, HeadCx, HeadCy, HeadRx, HeadRy, Turn, FaceDir);

	if (K > 0.02f)
	{
		const float Eyy = HeadCy - HeadRy * 0.06f;
		const float BrowX = HeadCx + FaceDir * HeadRx * FMath::Lerp(0.55f, 1.10f, K); // 鼻筋の付け根
		// サングラス：眉から頬までの黒帯＋耳へ抜ける蔓。鼻筋まで届かせる＝掛けて見える。
		C.Rect(HeadCx - HeadRx * 0.55f, Eyy - HeadRy * 0.13f, BrowX, Eyy + HeadRy * 0.13f,
			Rgb(22, 20, 32), 0.94f * K);
		C.Line(FVector2f(HeadCx - HeadRx * 0.55f, Eyy), FVector2f(HeadCx - HeadRx * 0.92f, Eyy + 1.0f),
			1.5f, Rgb(22, 20, 32), K);
		C.Line(FVector2f(HeadCx - HeadRx * 0.10f, Eyy + HeadRy * 0.08f),
			FVector2f(BrowX - FaceDir * 1.5f, Eyy - HeadRy * 0.09f), 1.0f, Pal::White, 0.85f * K);

		// 咥え煙草＋煙（口角に咥える）
		const float Cig = Seg01(U, 0.22f, 0.42f);
		if (Cig > 0.0f)
		{
			const FVector2f Mouth(HeadCx + FaceDir * HeadRx * FMath::Lerp(0.55f, 1.02f, K),
				HeadCy + HeadRy * 0.36f);
			C.Line(Mouth, Mouth + FVector2f(FaceDir * 8.0f, 1.0f), 1.5f, Pal::Cloth, 0.95f * Cig);
			C.Ellipse(Mouth.X + FaceDir * 8.6f, Mouth.Y + 1.0f, 1.2f, 1.2f, Pal::InkRed, Cig);
			C.Blend(FMath::FloorToInt(Mouth.X + FaceDir * 8.6f), FMath::FloorToInt(Mouth.Y + 1.0f),
				Pal::LampCore, 0.85f * Cig);
			for (int32 s = 0; s < 6; ++s)
			{
				const float Sy = Mouth.Y - 3.0f - s * 2.6f - FMath::Fmod(Ph * 3.0f, 2.6f);
				const float Sx = Mouth.X + FaceDir * (9.0f + FMath::Sin(Ph * 2.0f + s) * 2.4f);
				C.DPut(FMath::FloorToInt(Sx), FMath::FloorToInt(Sy), Pal::Smoke,
					0.55f * Cig * (1.0f - s * 0.15f));
			}
		}

		// キラッ：user 指定の「誇張された帥哥刻板印象」の記号。約 0.15 秒だけレンズ端で光る。
		const float Sp = Seg01(U, 0.58f, 0.66f) * (1.0f - Seg01(U, 0.68f, 0.80f));
		if (Sp > 0.01f)
		{
			const FVector2f G(BrowX - FaceDir * 2.0f, Eyy - HeadRy * 0.10f);
			const float L = 9.0f * Sp;
			C.Line(G - FVector2f(L, 0), G + FVector2f(L, 0), 1.0f, Pal::White, 1.0f);
			C.Line(G - FVector2f(0, L), G + FVector2f(0, L), 1.0f, Pal::White, 1.0f);
			C.Line(G - FVector2f(L, L) * 0.42f, G + FVector2f(L, L) * 0.42f, 1.0f, Pal::White, 0.85f);
			C.Line(G - FVector2f(-L, L) * 0.42f, G + FVector2f(-L, L) * 0.42f, 1.0f, Pal::White, 0.85f);
			C.Ellipse(G.X, G.Y, 1.8f, 1.8f, Pal::White, 1.0f);
		}
	}

	DrawCaption(C, 82.0f, 6, 505, Seg01(U, 0.05f, 0.30f) * (1.0f - Seg01(U, 0.88f, 1.0f)));
	DrawStationBug(C, FrameNo);

	// 最後の一瞬：カメラが追いつかず僅かに露出オーバー（＝決めの一枚）
	const float Over = Seg01(U, 0.90f, 1.0f) * 0.07f;
	if (Over > 0.0f)
	{
		for (int32 i = 0; i < FilmPx; ++i) { C.P[i] = Mix(C.P[i], Pal::White, Over); }
	}
}

// ═══════════════════════════════════════════════════════════════════════════
// 後段：アナログの汚れ。これを乗せて初めて「本物の古いテレビ」になる。
// （画は 128×96 の側で汚す；走査線と暗角は 512 の側＝二つの解像度が要る）
// ═══════════════════════════════════════════════════════════════════════════
void PostAnalog(FCan& C, int32 FrameNo, float U)
{
	TArray<FColor> Src;
	Src.SetNumUninitialized(FilmPx);
	FMemory::Memcpy(Src.GetData(), C.P, FilmPx * sizeof(FColor));

	// ハムバー（電源同期のずれ）：ゆっくり上へ流れる明るい帯
	const float BarY = FMath::Fmod(FrameNo * 1.7f, float(FilmH + 40)) - 20.0f;

	for (int32 Y = 0; Y < FilmH; ++Y)
	{
		for (int32 X = 0; X < FilmW; ++X)
		{
			const FColor& S = Src[Y * FilmW + X];
			// 色度の横滲み（NTSC）：R は左、B は右から拾う＝縁に色が付く
			const FColor& L = Src[Y * FilmW + FMath::Max(X - 1, 0)];
			const FColor& R2 = Src[Y * FilmW + FMath::Min(X + 1, FilmW - 1)];
			int32 Rr = FMath::RoundToInt(S.R * 0.62f + L.R * 0.38f);
			int32 Gg = S.G;
			int32 Bb = FMath::RoundToInt(S.B * 0.62f + R2.B * 0.38f);
			// ゴースト（多重反射）：3px 左の像が薄く重なる
			const FColor& Gh = Src[Y * FilmW + FMath::Max(X - 3, 0)];
			Rr += FMath::RoundToInt(Gh.R * 0.09f); Gg += FMath::RoundToInt(Gh.G * 0.09f);
			Bb += FMath::RoundToInt(Gh.B * 0.09f);
			const float Bd = FMath::Abs(Y - BarY);
			if (Bd < 14.0f)
			{
				const float K = (1.0f - Bd / 14.0f) * 0.10f;
				Rr = FMath::RoundToInt(Rr * (1.0f + K)); Gg = FMath::RoundToInt(Gg * (1.0f + K));
				Bb = FMath::RoundToInt(Bb * (1.0f + K));
			}
			const int32 N = int32(Rand01(X, Y, FrameNo) * 15.0f) - 7; // フィルム粒子
			C.P[Y * FilmW + X] = Rgb(Rr + N, Gg + N, Bb + N);
		}
	}

	// カット直後の一瞬：スプライス（白飛び＋横ずれ）
	const float Splice = 1.0f - Seg01(U, 0.0f, 0.055f);
	if (Splice > 0.02f)
	{
		const int32 Shift = FMath::RoundToInt(Splice * 4.0f);
		for (int32 Y = 0; Y < FilmH; ++Y)
		{
			if (((Y + FrameNo) % 7) != 0) { continue; }
			for (int32 X = FilmW - 1; X >= 0; --X)
			{
				C.P[Y * FilmW + X] = Src[Y * FilmW + FMath::Clamp(X - Shift, 0, FilmW - 1)];
			}
		}
		for (int32 i = 0; i < FilmPx; ++i) { C.P[i] = Mix(C.P[i], Pal::White, Splice * 0.35f); }
	}
}

} // namespace Detail

// ═══════════════════════════════════════════════════════════════════════════

void ResolveShot(ENiCeremonyStep Step, float Alpha, float StepSeconds, double Now,
	int32& OutShot, float& OutU, float& OutSeconds)
{
	// IntroNotice（電視特寫）内での分鏡の**比率**。絶対秒ではなく比率で持つ理由＝
	// user が IntroNoticeSeconds を触ったとき全段が等比で伸縮し、しかも動画の物理速度は
	// OutSeconds という別経路が保つから、一つの旋鈕だけで長さを決められる。
	//   夜祭 0.14 ／ 太鼓 0.20 ／ 神輿 0.21 ／ 登場 0.24 ／ 振り返り 0.21
	//   （IntroNoticeSeconds=10.4s なら 1.46 / 2.08 / 2.18 / 2.50 / 2.18 秒）
	static const float Lo[5] = { 0.00f, 0.14f, 0.34f, 0.55f, 0.79f };
	static const float Hi[5] = { 0.14f, 0.34f, 0.55f, 0.79f, 1.00f };
	static const int32 Seq[5] = { Shot_Yomatsuri, Shot_Taiko, Shot_Mikoshi, Shot_Reveal, Shot_Turn };

	Alpha = Detail::Sat(Alpha);
	StepSeconds = FMath::Max(StepSeconds, 0.01f);

	if (Step == ENiCeremonyStep::IntroSit)
	{
		// 全景の間はテレビが画面上 20px 程度＝何が映っていても読めない。ここは
		// 「番組が点いている」だけを担当し、**番組本体は寄ってから頭出しで流す**。
		// U は字幕が出ている帯に留める（出入りを起こさない＝ちらつかない）。
		OutShot = Shot_Yomatsuri;
		OutU = 0.35f + 0.30f * Alpha;
		OutSeconds = Alpha * StepSeconds;
		return;
	}
	if (Step == ENiCeremonyStep::IntroNotice)
	{
		for (int32 i = 0; i < 5; ++i)
		{
			if (Alpha < Hi[i] || i == 4)
			{
				OutShot = Seq[i];
				OutU = Detail::Sat((Alpha - Lo[i]) / (Hi[i] - Lo[i]));
				OutSeconds = (Alpha - Lo[i]) * StepSeconds;
				return;
			}
		}
	}
	if (Step == ENiCeremonyStep::IntroTvOff)
	{
		// 収束中は決めの一枚で**静止**（Seconds を固定＝揺れも煙も止まる。
		// 潰れていく絵の中で提燈だけ揺れていたら「まだ生きている」に読める）。
		OutShot = Shot_Turn;
		OutU = 1.0f;
		OutSeconds = 2.2f;
		return;
	}
	// 儀式外（保険）：遠景を自走させる
	OutShot = Shot_Yomatsuri;
	OutU = 0.5f;
	OutSeconds = static_cast<float>(FMath::Fmod(Now, 60.0));
}

void RenderFrame(int32 Shot, float U, float Seconds, int32 FrameNo, FColor* OutPixels)
{
	if (!OutPixels) { return; }
	Detail::FCan C; C.P = OutPixels;
	U = Detail::Sat(U);
	Seconds = FMath::Max(Seconds, 0.0f);

	switch (Shot)
	{
	case Shot_Taiko:   Detail::ShotTaiko(C, U, Seconds, FrameNo); break;
	case Shot_Mikoshi: Detail::ShotMikoshi(C, U, Seconds, FrameNo); break;
	case Shot_Reveal:  Detail::ShotReveal(C, U, Seconds, FrameNo); break;
	case Shot_Turn:    Detail::ShotTurn(C, U, Seconds, FrameNo); break;
	default:           Detail::ShotYomatsuri(C, U, Seconds, FrameNo); break;
	}
	Detail::PostAnalog(C, FrameNo, U);
}

} // namespace NiceInkTvFilm
