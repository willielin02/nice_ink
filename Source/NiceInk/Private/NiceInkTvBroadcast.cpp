#include "NiceInkTvBroadcast.h"

#include "DreamTraceMotifData.h"
#include "NiceInkTvTelopData.h"

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

// ═══════════════════════════════════════════════════════════════════════════
// 放送 chrome（2026-08-29：user 判讀「這要營造的是新聞畫面比較合理」——同意，而且
// 理由更強：劇情片的帥是虛構的，力士羨慕的會變成明星；**新聞裡的男人是真的存在的**，
// 那個錯誤推論才站得住腳。以下這一組是「這是電視在播，不是電影」的承重訊號。）
//
// **chrome 不屬於任何一個分鏡**：它是播出鏈路加上去的，所以畫在**手持晃動之後**
// ——攝影機在晃，字幕不會跟著晃。這條分界是它讀起來像廣播而不像貼圖的關鍵。
// ═══════════════════════════════════════════════════════════════════════════

// 3×5 點陣數字。**數字是這個解析度上唯一真的能讀的字**（漢字要 11×11 才認得出來，
// 而數字 3×5 就夠）⇒ 時刻表示是全片唯一「真的有意義的文字」，也是最省的廣播訊號。
const uint8 GDigit3x5[11][5] = {
	{ 0b111, 0b101, 0b101, 0b101, 0b111 }, // 0
	{ 0b010, 0b110, 0b010, 0b010, 0b111 }, // 1
	{ 0b111, 0b001, 0b111, 0b100, 0b111 }, // 2
	{ 0b111, 0b001, 0b111, 0b001, 0b111 }, // 3
	{ 0b101, 0b101, 0b111, 0b001, 0b001 }, // 4
	{ 0b111, 0b100, 0b111, 0b001, 0b111 }, // 5
	{ 0b111, 0b100, 0b111, 0b101, 0b111 }, // 6
	{ 0b111, 0b001, 0b001, 0b001, 0b001 }, // 7
	{ 0b111, 0b101, 0b111, 0b101, 0b111 }, // 8
	{ 0b111, 0b101, 0b111, 0b001, 0b111 }, // 9
	{ 0b000, 0b010, 0b000, 0b010, 0b000 }, // 10 = ':'
};

void DrawDigits(FCan& C, float X, float Y, const int32* Glyphs, int32 Count, const FColor& Col)
{
	for (int32 g = 0; g < Count; ++g)
	{
		const int32 Idx = FMath::Clamp(Glyphs[g], 0, 10);
		for (int32 R = 0; R < 5; ++R)
			for (int32 Cx = 0; Cx < 3; ++Cx)
			{
				if (GDigit3x5[Idx][R] & (1 << (2 - Cx)))
				{
					C.Put(FMath::FloorToInt(X) + g * 4 + Cx, FMath::FloorToInt(Y) + R, Col);
				}
			}
	}
}

// 點陣片假名を一字置く。MaxX＝ワイプの右端（これで帯と字が同時に開く）。
void DrawTelopGlyph(FCan& C, int32 GlyphIdx, float X, float Y, const FColor& Col, float MaxX)
{
	using namespace NiceInkTvTelop;
	if (GlyphIdx < 0 || GlyphIdx >= Num) { return; }
	for (int32 R = 0; R < GlyphH; ++R)
	{
		for (int32 Cx = 0; Cx < GlyphW; ++Cx)
		{
			if ((Bits[GlyphIdx][R] & (1 << (GlyphW - 1 - Cx))) == 0) { continue; }
			const float Px = X + Cx;
			if (Px >= MaxX) { continue; }
			C.Put(FMath::FloorToInt(Px), FMath::FloorToInt(Y) + R, Col);
		}
	}
}

// 台標（右上）＋時刻。舊放送的常駐識別。
void DrawStationBug(FCan& C, int32 FrameNo, float FilmClockSec)
{
	const float A = 0.62f + 0.06f * FMath::Sin(FrameNo * 0.5f);
	C.Rect(110, 5, 122, 17, Pal::CapBg, 0.5f);
	C.Ellipse(116.0f, 11.0f, 4.2f, 4.2f, Pal::CapInk, A);
	C.Ellipse(116.0f, 11.0f, 2.0f, 2.0f, Pal::CapBg, A);
	C.Rect(115.0f, 7.0f, 117.0f, 15.0f, Pal::CapBg, A);
	// 時刻 20:3x：秒が進む＝「今、放送されている」。数字は実際に読める。
	const int32 Sec = FMath::Clamp(FMath::FloorToInt(FilmClockSec), 0, 59);
	const int32 D[5] = { 2, 0, 10, 3, Sec % 10 };
	C.Rect(101, 19, 122, 26, Pal::CapBg, 0.62f);
	DrawDigits(C, 102.0f, 20.0f, D, 5, Pal::CapInk);
}

// 生中継バッジ（左上）：赤地に「生」。真の字になったので記号ではなく言葉として読める。
void DrawLiveBadge(FCan& C, int32 FrameNo)
{
	const bool bBlink = ((FrameNo / 16) % 2) == 0; // ゆっくり明滅＝生きている信号
	C.Rect(6, 4, 18, 17, Rgb(176, 44, 36), bBlink ? 1.0f : 0.84f);
	C.Rect(6, 4, 18, 5, Rgb(228, 96, 78), 1.0f);
	DrawTelopGlyph(C, NiceInkTvTelop::SeiIndex, 8.0f, 6.0f, Pal::White, 1000.0f);
}

// 下三分之一（テロップ）。**08-29 二修：user viewport 判決「下面的新聞標題太大了，
// 而且裡面的文字也根本看不出來在寫什麼，也完全不像日本字」——三点とも当たり。**
//   ① 二段 19px（画面高の 20%）→ **一段 12px（12.5%）**。二段＋見出し帯は速報・重大
//      ニュースの作法で、祭の特集には過剰だった。「実物は大きい」を一段階読み違えた。
//   ② 帯幅 116px 固定 → **内容に合わせる**（46~87px）。実物のテロップは内容の幅しかない
//      ——全幅の帯は「画面に貼った板」に見える。
//   ③ 假字塊（等幅・横劃が全格を貫く）は**条码に読める**。真の点陣片假名に置き換え
//      （Tools/AssetPrep/telop_glyphs.py が焼く）。片假名を選ぶ理由＝直線構成だから
//      8×9 で成立する唯一の日本語文字（平假名の曲線も漢字の劃密度もこの尺では潰れる）。
void DrawLowerThird(FCan& C, int32 TextIdx, float Wipe)
{
	using namespace NiceInkTvTelop;
	if (Wipe <= 0.01f) { return; }
	// 上限は**標頭が持つ本数**から取る。08-29：文言を一本減らした瞬間、ここに書き写して
	// あった 4 が境界外になった——**表の大きさを二箇所に書けば、必ず片方が古くなる**。
	TextIdx = FMath::Clamp(TextIdx, 0, NumTexts - 1);
	const int32 N = TextLen[TextIdx];
	const float X0 = 6.0f, HeadW = 12.0f, Adv = 9.0f;
	const float FullW = HeadW + 2.0f + N * Adv + 2.0f;
	const float W = FullW * Sat(Wipe);
	const float Y0 = 80.0f, Y1 = 92.0f; // 12px＝画面高の 12.5%

	C.Rect(X0, Y0, X0 + W, Y1, Rgb(18, 22, 44), 0.94f);
	C.Rect(X0, Y0, X0 + W, Y0 + 1.0f, Rgb(96, 116, 168), 0.9f); // 上縁のハイライト
	if (W > HeadW) // 見出しブロック（赤地に「生」）
	{
		C.Rect(X0, Y0, X0 + HeadW, Y1, Rgb(176, 44, 36), 1.0f);
		DrawTelopGlyph(C, SeiIndex, X0 + 2.0f, Y0 + 2.0f, Pal::White, X0 + W);
	}
	for (int32 g = 0; g < N; ++g)
	{
		DrawTelopGlyph(C, Text[TextIdx][g], X0 + HeadW + 2.0f + g * Adv, Y0 + 2.0f,
			Pal::CapInk, X0 + W);
	}
}

// 手持ちの揺れ：低周波の和で 1~2px 漂う＋ゆっくり流れる。
// 08-28 版は五拍とも固定機位＋滑らかな ease＝映画の作法。**報道は人が担いでいる。**
// 画そのものをずらす（chrome より前に掛ける）＝カメラが揺れて字幕は揺れない。
void ApplyHandheld(FCan& C, float Seconds, int32 Shot)
{
	const float T = Seconds + Shot * 3.7f; // 分鏡ごとに位相をずらす＝同じ揺れの繰り返しに見えない
	const int32 Dx = FMath::RoundToInt(FMath::Sin(T * 0.9f) * 1.15f + FMath::Sin(T * 2.3f + 1.7f) * 0.6f);
	const int32 Dy = FMath::RoundToInt(FMath::Sin(T * 0.7f + 2.1f) * 0.95f + FMath::Sin(T * 1.9f + 0.4f) * 0.5f);
	if (Dx == 0 && Dy == 0) { return; }
	TArray<FColor> Src;
	Src.SetNumUninitialized(FilmPx);
	FMemory::Memcpy(Src.GetData(), C.P, FilmPx * sizeof(FColor));
	for (int32 Y = 0; Y < FilmH; ++Y)
		for (int32 X = 0; X < FilmW; ++X)
		{
			const int32 Sx = FMath::Clamp(X - Dx, 0, FilmW - 1); // 端はクランプ＝黒縁を出さない
			const int32 Sy = FMath::Clamp(Y - Dy, 0, FilmH - 1);
			C.P[Y * FilmW + X] = Src[Sy * FilmW + Sx];
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

	// テロップ（y74~93）に頭を食われないよう人群を上げる。
	// **報道のカメラマンは下三分之一があることを知って構図を切る**——これはその作法。
	DrawCrowd(C, 65.0f, 5, Ph, Pal::CrowdLit);
	DrawCrowd(C, 75.0f, 17, Ph * 1.2f, Pal::Crowd);

	for (int32 i = 0; i < 14; ++i) // 火の粉
	{
		const float Sx = Rand01(i, 1, 2) * FilmW;
		const float Sy = FMath::Fmod(80.0f - (Rand01(i, 2, 3) * 60.0f + Ph * 9.0f), 76.0f) + 12.0f;
		C.DPut(FMath::FloorToInt(Sx), FMath::FloorToInt(Sy), Pal::LampCore, 0.8f);
	}

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
	DrawCrowd(C, 56.0f, 29, Ph, Rgb(58, 48, 76)); // テロップ分だけ上げる

	// 打ち手二名（太鼓より先に＝後ろに立つ）
	for (int32 S = -1; S <= 1; S += 2)
	{
		const float Mx = 64.0f + S * 33.0f;
		DrawFestivalMan(C, Mx, 32.0f, 6.5f, 13.0f, 96.0f, Pal::SkinDark, true);
		C.Ellipse(Mx + S * 7.0f, 48.0f, 5.0f, 7.0f, Pal::InkField, 0.85f); // 肩の彫物＝伏線
		C.Ellipse(Mx + S * 7.0f, 46.0f, 2.2f, 3.0f, Pal::InkMid, 0.8f);
	}

	// 太鼓（正面）：胴＋革面＋鋲
	const float Dx = 64.0f, Dy = 61.0f, Rx = 25.0f + Hit * 1.2f, Ry = 21.0f + Hit * 1.0f;
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
		const float Hy = 44.0f - Swing * 12.0f;
		const float HxIn = Mx - S * 15.0f;
		C.Line(FVector2f(Mx - S * 8.0f, 48.0f), FVector2f(HxIn, Hy), 5.0f, Pal::SkinMid);
		C.Line(FVector2f(Mx - S * 8.0f, 48.0f), FVector2f(HxIn, Hy), 1.5f, Pal::SkinRim, 0.55f);
		C.Line(FVector2f(HxIn, Hy), FVector2f(HxIn - S * 9.0f, Hy + 11.0f), 2.0f, Pal::Cloth, 0.95f);
	}

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
		// +30 だとテロップ（y74~）に頭と肩が丸ごと埋まり、上がった腕だけが残って
		// **柵に読める**（08-29 contact sheet 実証）。頭が帯の上に出る高さへ。
		const float Hy = My + 19.0f + FMath::Sin(Ph * 2.2f + i * 0.7f) * 1.2f;
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

	// 記者（前景・左端、最後に描いて手前に被せる）：**「これは報道である」を一枚で
	// 言い切る唯一の絵**。この拍に置くのは、神輿が論証上いちばん弱い連接組織だから
	// ——その役を担わせれば尺が無駄にならない。後頭部＋差し出したマイク、逆光の縁だけ。
	{
		const FColor Fg = Rgb(26, 24, 42); // 前景＝ほぼ黒（被写体より手前＝光が回らない）
		const float Rx = 10.0f + FMath::Sin(Ph * 0.8f) * 0.7f; // 人の重心移動（手持ち揺れとは別物）
		TArray<FVector2f> Body;
		Body.Add(FVector2f(Rx - 20, 96)); Body.Add(FVector2f(Rx - 16, 58));
		Body.Add(FVector2f(Rx - 8, 52));  Body.Add(FVector2f(Rx + 8, 52));
		Body.Add(FVector2f(Rx + 16, 58)); Body.Add(FVector2f(Rx + 19, 96));
		C.Poly(Body, Fg);
		C.Ellipse(Rx, 42, 9.0f, 10.0f, Fg);
		C.Line(FVector2f(Rx + 16, 58), FVector2f(Rx + 19, 96), 1.0f, Pal::SkinRim, 0.5f);
		C.Line(FVector2f(Rx + 6, 34), FVector2f(Rx + 9, 50), 1.0f, Pal::SkinRim, 0.45f);
		C.Line(FVector2f(Rx + 12, 62), FVector2f(Rx + 26, 48), 5.0f, Fg);   // 腕
		C.Line(FVector2f(Rx + 26, 48), FVector2f(Rx + 33, 41), 2.0f, Fg);   // マイクの柄
		C.Ellipse(Rx + 35, 39, 3.4f, 3.4f, Fg);                             // ウインドスクリーン
		C.Line(FVector2f(Rx + 33, 37), FVector2f(Rx + 37, 36), 1.0f, Pal::SkinRim, 0.45f);
	}

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

}

// ⑤ 振り返り：ここが交付点。被写体は**男そのもの**なので寄る（顔を大きく）。
//    低解析度で「帥」を運ぶのは顔の造作ではなく、輪郭光・顎を上げた側面・
//    サングラスの反射・咥え煙草・風になびく半纏。
//    user 指定「甚至有點誇張、典型的帥哥刻板印象」⇒ 誇張は捨てないが、記号は
//    漫画の「キラッ」ではなく**レンズフレア**で出す（08-29 新聞画面へ改判）。
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

		// **レンズフレア**（2026-08-29）：キラッ＝四芒星は漫画／偶像の語法で、報道映像とは
		// 衝突する。user 指定の「誇張された帥哥刻板印象」は捨てない——背後の燈籠の光が
		// サングラスからレンズに入った、という**カメラに実際に起こること**に置き換える。
		// 横に伸びる筋＋核＋画面中心へ向かうゴースト＋一瞬のベーリンググレア。
		const float Fl = Seg01(U, 0.56f, 0.64f) * (1.0f - Seg01(U, 0.66f, 0.80f));
		if (Fl > 0.01f)
		{
			const FVector2f G(BrowX - FaceDir * 2.0f, Eyy - HeadRy * 0.10f);
			const float Len = 22.0f * Fl;
			const int32 Gx = FMath::FloorToInt(G.X), Gy = FMath::FloorToInt(G.Y);
			for (int32 dx = -FMath::CeilToInt(Len); dx <= FMath::CeilToInt(Len); ++dx)
			{
				// 外側の K（振り返りの進捗）を隠さないよう別名（unity build 下の C4456）
				const float Fall = 1.0f - FMath::Abs(dx) / FMath::Max(Len, 0.01f);
				C.Blend(Gx + dx, Gy, Pal::White, Fall * Fall * Fl);
				if (Fall > 0.55f) // 芯は三段の厚み＝アナモルフィックな横筋の読み
				{
					C.Blend(Gx + dx, Gy - 1, Pal::White, Fall * 0.5f * Fl);
					C.Blend(Gx + dx, Gy + 1, Pal::White, Fall * 0.5f * Fl);
				}
			}
			C.Ellipse(G.X, G.Y, 2.2f * Fl, 1.6f * Fl, Pal::White, 1.0f);
			// 画面中心へ向かうゴースト（絞りの反射）＝レンズの中で起きている証拠
			const FVector2f Ctr(64.0f, 48.0f);
			for (int32 g = 1; g <= 2; ++g)
			{
				const FVector2f P = G + (Ctr - G) * (0.38f * g);
				C.Ellipse(P.X, P.Y, 3.0f - g * 0.6f, 3.0f - g * 0.6f,
					(g == 1) ? Pal::LampCore : Pal::LampGlow, 0.30f * Fl);
			}
			// ベーリンググレア：強い光が入ると画面全体のコントラストが一瞬落ちる
			for (int32 i = 0; i < FilmPx; ++i) { C.P[i] = Mix(C.P[i], Pal::LampCore, 0.09f * Fl); }
		}
	}


	// 最後の一瞬：カメラが追いつかず僅かに露出オーバー（＝決めの一枚）
	const float Over = Seg01(U, 0.90f, 1.0f) * 0.07f;
	if (Over > 0.0f)
	{
		for (int32 i = 0; i < FilmPx; ++i) { C.P[i] = Mix(C.P[i], Pal::White, Over); }
	}
}

// 放送 chrome を一括で乗せる。**手持ち揺れの後**に呼ぶこと（カメラは揺れるが
// 字幕は揺れない＝それが「放送されている映像」の読み）。
void DrawBroadcastChrome(FCan& C, int32 Shot, float U, int32 FrameNo)
{
	// **テロップは「この報道」に属する。鏡頭には属さない。**（2026-08-29 三修）
	// 二修までは (Shot, U) で出し入れしていた——この関数が手にしていたのが鏡頭だけ
	// だったから。だが分鏡は 1.5~2.5 秒しかないので、「一拍一本」は構造的に**二秒に
	// 一度点滅する**ことを保証してしまう（10.4 秒で五回。user 判決「一直出現又消失」）。
	// 実物のテロップは 3~5 秒居座り、十秒の項目に一本か二本しか出ない。
	// ⇒ 比率表から**段全体の進捗**を復元し、その時間軸に二本だけ置く。両方とも
	//    カットをまたぐ＝「鏡頭のものではない」ことが画面上で見える。
	static const float Lo[Shot_Num] = { 0.00f, 0.14f, 0.34f, 0.55f, 0.79f };
	static const float Hi[Shot_Num] = { 0.14f, 0.34f, 0.55f, 0.79f, 1.00f };
	const int32 S = FMath::Clamp(Shot, 0, Shot_Num - 1);
	const float A = Lo[S] + Sat(U) * (Hi[S] - Lo[S]); // IntroNotice 全体での位置 0..1

	// **彫物には一度も字幕を当てない**（user 判決「為什麼新聞標題會出現刺青？感覺有點
	// 生硬」——正しい）。この報道が存在する理由は祭を報じることで、彫物はたまたま
	// 画面にいるだけ——**たまたまであることが羨ましさの根拠**（誰も売り込んでいないのに
	// あの男たちには在る）。指させば広告になるし、力士の代わりに結論を言ってしまう。
	// ⇒ 主役の二拍（A>0.55、約 4.9 秒）は chrome を一切載せない。
	struct FTelop { int32 Text; float In0, In1, Out0, Out1; };
	static const FTelop Telops[2] = {
		{ NiceInkTvTelop::Txt_Ennichi, 0.02f, 0.05f, 0.29f, 0.32f }, // 0.2~3.3s（夜祭→太鼓）
		{ NiceInkTvTelop::Txt_Mikoshi,      0.35f, 0.38f, 0.50f, 0.53f }, // 3.6~5.5s（神輿）
	};
	for (const FTelop& T : Telops)
	{
		const float Wipe = Seg01(A, T.In0, T.In1) * (1.0f - Seg01(A, T.Out0, T.Out1));
		if (Wipe > 0.01f) { DrawLowerThird(C, T.Text, Wipe); }
	}

	DrawLiveBadge(C, FrameNo);
	DrawStationBug(C, FrameNo, FMath::Fmod(FrameNo / RedrawHz, 60.0f));
}

// ═══════════════════════════════════════════════════════════════════════════
// 後段：受像機と電波の汚れ。
//
// **2026-08-29 の訂正：ここは全部「膠片」の假影だった**——粒子・剪接白閃・横ずれ。
// 剪接白閃はフィルムプリントの繋ぎ目の露出であって、**新聞映像には原理的に存在しない**。
// 電視上放的是電影，然後說它是新聞——媒介を間違えていた。
// 映像（video）の汚れに入れ替える：ドロップアウト／インターレースのちらつき／
// 受信ノイズ（暗部に沈む、色度寄り）／カット直後のスイッチャの一瞬の裂け。
// ═══════════════════════════════════════════════════════════════════════════
void PostAnalog(FCan& C, int32 FrameNo, float U)
{
	TArray<FColor> Src;
	Src.SetNumUninitialized(FilmPx);
	FMemory::Memcpy(Src.GetData(), C.P, FilmPx * sizeof(FColor));

	// ハムバー（電源同期のずれ）：ゆっくり上へ流れる明るい帯
	const float BarY = FMath::Fmod(FrameNo * 0.85f, float(FilmH + 40)) - 20.0f;
	// インターレースのちらつき（interline twitter）：奇偶の行が交互に僅かに沈む。
	// 60i の「映像らしさ」で 12Hz では再現できない部分を、この一点で代替する。
	const int32 TwitterPhase = FrameNo & 1;

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
				const float K = (1.0f - Bd / 14.0f) * 0.09f;
				Rr = FMath::RoundToInt(Rr * (1.0f + K)); Gg = FMath::RoundToInt(Gg * (1.0f + K));
				Bb = FMath::RoundToInt(Bb * (1.0f + K));
			}
			if ((Y & 1) == TwitterPhase) { Rr -= 4; Gg -= 4; Bb -= 4; }
			// 受信ノイズ：フィルム粒子（全域に均一）ではなく**暗部に沈む・色度寄り**。
			// 粒子を弱め、代わりに青にだけ強く乗せる＝ビデオのノイズの見え方。
			const float Lum = (Rr + Gg + Bb) / 765.0f;
			const float NGain = 1.0f - 0.55f * Sat(Lum);
			const int32 N = FMath::RoundToInt((Rand01(X, Y, FrameNo) * 9.0f - 4.5f) * NGain);
			const int32 NB = FMath::RoundToInt((Rand01(X, Y, FrameNo + 991) * 7.0f - 3.5f) * NGain);
			C.P[Y * FilmW + X] = Rgb(Rr + N, Gg + N, Bb + N + NB);
		}
	}

	// ドロップアウト：短い横の白ダッシュが数本。テープ／電波の代表的な傷。
	for (int32 i = 0; i < 3; ++i)
	{
		const uint32 H = Hash3(FrameNo, i, 77);
		if ((H & 7u) != 0) { continue; } // 常時ではなく時々（毎フレーム出ると汚いだけ）
		const int32 Dy = int32((H >> 3) % uint32(FilmH));
		const int32 Dx = int32((H >> 11) % uint32(FilmW - 12));
		const int32 Len = 4 + int32((H >> 19) % 9u);
		for (int32 k = 0; k < Len; ++k)
		{
			const FColor& O = C.P[Dy * FilmW + Dx + k];
			C.P[Dy * FilmW + Dx + k] = Mix(O, Pal::White, 0.75f);
		}
	}

	// カット直後：フィルムの繋ぎ目（白飛び）ではなく、**スイッチャの一瞬の同期の乱れ**。
	// 数行だけ横にずれて、次のフレームには消える。白閃は出さない。
	const float Glitch = 1.0f - Seg01(U, 0.0f, 0.030f);
	if (Glitch > 0.02f)
	{
		const int32 Shift = FMath::RoundToInt(Glitch * 5.0f);
		for (int32 Y = 0; Y < FilmH; ++Y)
		{
			if (((Y * 5 + FrameNo) % 9) > 2) { continue; }
			for (int32 X = FilmW - 1; X >= 0; --X)
			{
				C.P[Y * FilmW + X] = Src[Y * FilmW + FMath::Clamp(X - Shift, 0, FilmW - 1)];
			}
		}
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
	// 順序が意味を持つ三段：撮られた画 → **カメラの揺れ** → **放送が乗せる chrome**
	// → **電波／受像機の汚れ**。chrome が揺れたら貼り紙、汚れが chrome を避けたら合成。
	Detail::ApplyHandheld(C, Seconds, Shot);
	Detail::DrawBroadcastChrome(C, Shot, U, FrameNo);
	Detail::PostAnalog(C, FrameNo, U);
}

} // namespace NiceInkTvFilm
