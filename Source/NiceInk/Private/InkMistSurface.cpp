#include "InkMistSurface.h"

void FInkMistSurface::Init(int32 InRes)
{
	Res = FMath::Clamp(InRes, 16, 8192);
	Pixels.SetNumZeroed(static_cast<int64>(Res) * Res);
	Dirty = FIntRect(0, 0, Res, Res);
	bDirty = true; // 初次配置＝整張（全透明）要上傳一次
	LutColor = FLinearColor(-1.0f, -1.0f, -1.0f, -1.0f);
}

void FInkMistSurface::Clear()
{
	if (Res <= 0)
	{
		return;
	}
	FMemory::Memzero(Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	if (Glaze.Num() > 0)
	{
		FMemory::Memzero(Glaze.GetData(), Glaze.Num() * sizeof(FColor));
	}
	if (BaseEpoch.Num() > 0)
	{
		FMemory::Memzero(BaseEpoch.GetData(), BaseEpoch.Num() * sizeof(uint16));
	}
	MarkAllDirty();
}

void FInkMistSurface::EnsureGlaze()
{
	if (Glaze.Num() == 0 && Res > 0)
	{
		Glaze.SetNumZeroed(static_cast<int64>(Res) * Res);
	}
}

void FInkMistSurface::BeginStrokeMark()
{
	// 世代圖在**第一條霧筆劃**就配（32MB）而不是等第一次罩染：否則「觸發配置的
	// 那一筆」自己的較早像素沒有世代可查＝首筆 taper 可自我罩染，且 live（中途
	// 配置）與重建（Clear 後全程有圖）不對稱＝重建等價破功。
	if (BaseEpoch.Num() == 0 && Res > 0)
	{
		BaseEpoch.SetNumZeroed(static_cast<int64>(Res) * Res);
	}
	++CurrentEpoch;
	if (CurrentEpoch == 0)
	{
		CurrentEpoch = 1; // wrap 跳過 0（0＝洗掉/清空後的「無主」哨兵）
	}
}

namespace
{
	// sRGB byte → linear（256 查表；ComposeInto 用）
	struct FNiMistSrgbLut
	{
		float T[256];
		FNiMistSrgbLut()
		{
			for (int32 I = 0; I < 256; ++I)
			{
				const float S = I / 255.0f;
				T[I] = (S <= 0.04045f) ? S / 12.92f : FMath::Pow((S + 0.055f) / 1.055f, 2.4f);
			}
		}
	};
	const FNiMistSrgbLut GNiMistSrgb;

	FORCEINLINE uint8 NiMistEncodeSrgb(float Lin)
	{
		Lin = FMath::Clamp(Lin, 0.0f, 1.0f);
		const float S = (Lin <= 0.0031308f) ? Lin * 12.92f : 1.055f * FMath::Pow(Lin, 1.0f / 2.4f) - 0.055f;
		return static_cast<uint8>(FMath::RoundToInt(S * 255.0f));
	}
}

void FInkMistSurface::ComposeInto(FColor* Out, int32 OutPitchPx, const FIntRect& Rect) const
{
	const FColor* Base = Pixels.GetData();
	const FColor* Gz = Glaze.Num() > 0 ? Glaze.GetData() : nullptr;
	for (int32 Y = Rect.Min.Y; Y < Rect.Max.Y; ++Y)
	{
		const FColor* BRow = Base + static_cast<int64>(Y) * Res;
		const FColor* GRow = Gz ? Gz + static_cast<int64>(Y) * Res : nullptr;
		FColor* ORow = Out + static_cast<int64>(Y - Rect.Min.Y) * OutPitchPx - Rect.Min.X;
		for (int32 X = Rect.Min.X; X < Rect.Max.X; ++X)
		{
			const FColor B = BRow[X];
			const FColor G = GRow ? GRow[X] : FColor(0, 0, 0, 0);
			if (G.A == 0)
			{
				ORow[X] = B;
				continue;
			}
			// 罩染 over 基底（線性域、premult）：out = g + b×(1−gA)
			const float GA = G.A / 255.0f;
			const float BA = B.A / 255.0f;
			const float K = 1.0f - GA;
			const float R = GNiMistSrgb.T[G.R] + GNiMistSrgb.T[B.R] * K;
			const float Gr = GNiMistSrgb.T[G.G] + GNiMistSrgb.T[B.G] * K;
			const float Bl = GNiMistSrgb.T[G.B] + GNiMistSrgb.T[B.B] * K;
			const float A = GA + BA * K;
			ORow[X] = FColor(NiMistEncodeSrgb(R), NiMistEncodeSrgb(Gr), NiMistEncodeSrgb(Bl),
				static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(A, 0.0f, 1.0f) * 255.0f)));
		}
	}
}

void FInkMistSurface::MarkAllDirty()
{
	Dirty = FIntRect(0, 0, Res, Res);
	bDirty = true;
}

bool FInkMistSurface::TakeDirty(FIntRect& OutRect)
{
	if (!bDirty)
	{
		return false;
	}
	OutRect = Dirty;
	bDirty = false;
	Dirty = FIntRect(0, 0, 0, 0);
	return true;
}

void FInkMistSurface::ExpandDirty(int32 X0, int32 Y0, int32 X1, int32 Y1)
{
	X0 = FMath::Clamp(X0, 0, Res);
	Y0 = FMath::Clamp(Y0, 0, Res);
	X1 = FMath::Clamp(X1, 0, Res);
	Y1 = FMath::Clamp(Y1, 0, Res);
	if (X1 <= X0 || Y1 <= Y0)
	{
		return;
	}
	if (!bDirty)
	{
		Dirty = FIntRect(X0, Y0, X1, Y1);
		bDirty = true;
		return;
	}
	Dirty.Min.X = FMath::Min(Dirty.Min.X, X0);
	Dirty.Min.Y = FMath::Min(Dirty.Min.Y, Y0);
	Dirty.Max.X = FMath::Max(Dirty.Max.X, X1);
	Dirty.Max.Y = FMath::Max(Dirty.Max.Y, Y1);
}

void FInkMistSurface::Configure(const FLinearColor& InToneCool, float InToneStrength, float InToneGamma, float InGrainAmp, float InGrainPeriodPx)
{
	if (!ToneCool.Equals(InToneCool, 0.0f) || ToneStrength != InToneStrength || ToneGamma != InToneGamma)
	{
		ToneCool = InToneCool;
		ToneStrength = InToneStrength;
		ToneGamma = InToneGamma;
		LutColor = FLinearColor(-1.0f, -1.0f, -1.0f, -1.0f); // 色相曲線變＝LUT 失效
	}
	GrainAmp = InGrainAmp;
	GrainPeriodPx = FMath::Max(InGrainPeriodPx, 2.0f); // <2px 的紋理畫不出來（低解析鐵則）
}

void FInkMistSurface::BuildLut(const FLinearColor& Color)
{
	if (Color.Equals(LutColor, 0.0f))
	{
		return;
	}
	LutColor = Color;
	// 色度閘：黑/灰墨吃滿冷移，飽和彩墨豁免（稀的紅＝粉紅不是藍）
	const float MaxC = FMath::Max3(Color.R, Color.G, Color.B);
	const float MinC = FMath::Min3(Color.R, Color.G, Color.B);
	const float ChromaGate = 1.0f - FMath::Clamp((MaxC - MinC) * 3.0f, 0.0f, 1.0f);
	for (int32 D = 0; D < 256; ++D)
	{
		const float A = D / 255.0f;
		// 濃度→色相：薄墨偏冷（皮下散射）、疊滿回到筆色——lerp 權重＝pow(密度,γ)。
		// ToneStrength=0（現預設）＝CoolBlend 恆 0＝所有色同色相只差透明度
		const float K = FMath::Pow(A, FMath::Max(ToneGamma, 0.05f));
		const float CoolBlend = (1.0f - K) * ChromaGate * FMath::Clamp(ToneStrength, 0.0f, 1.0f);
		const FLinearColor Tone(
			FMath::Lerp(Color.R, ToneCool.R, CoolBlend),
			FMath::Lerp(Color.G, ToneCool.G, CoolBlend),
			FMath::Lerp(Color.B, ToneCool.B, CoolBlend));
		// 預乘（線性域）→ sRGB 編碼＝與舊 canvas→RTF_RGBA8 路徑同儲存語義
		const FLinearColor Premult(Tone.R * A, Tone.G * A, Tone.B * A, A);
		FColor Out = Premult.ToFColor(/*bSRGB=*/true); // RGB=sRGB 編碼、A=線性量化
		Out.A = static_cast<uint8>(D); // 密度=合成判準（ToFColor 的 A 同值，顯式釘住）
		Lut[D] = Out;
	}
}

namespace
{
	FORCEINLINE uint32 NiMistHash2(int32 X, int32 Y)
	{
		uint32 H = static_cast<uint32>(X) * 0x8da6b343u + static_cast<uint32>(Y) * 0xd8163841u;
		H ^= H >> 13;
		H *= 0x9e3779b1u;
		H ^= H >> 16;
		return H;
	}
	FORCEINLINE float NiMistLatticeVal(int32 X, int32 Y)
	{
		return (NiMistHash2(X, Y) & 0xFFFFu) / 32767.5f - 1.0f;
	}
}

float FInkMistSurface::GrainAt(int32 X, int32 Y) const
{
	// 兩八度 value noise（純像素座標函數＝皮膚錨定、跨端決定性、max 冪等不破）
	float Sum = 0.0f;
	float Norm = 0.0f;
	float Period = GrainPeriodPx;
	float W = 1.0f;
	for (int32 Oct = 0; Oct < 2; ++Oct)
	{
		const float GX = X / Period;
		const float GY = Y / Period;
		const int32 X0 = FMath::FloorToInt(GX);
		const int32 Y0 = FMath::FloorToInt(GY);
		float FX = GX - X0;
		float FY = GY - Y0;
		FX = FX * FX * (3.0f - 2.0f * FX);
		FY = FY * FY * (3.0f - 2.0f * FY);
		const float V = FMath::Lerp(
			FMath::Lerp(NiMistLatticeVal(X0, Y0), NiMistLatticeVal(X0 + 1, Y0), FX),
			FMath::Lerp(NiMistLatticeVal(X0, Y0 + 1), NiMistLatticeVal(X0 + 1, Y0 + 1), FX), FY);
		Sum += V * W;
		Norm += W;
		Period *= 2.3f;
		W *= 0.6f;
	}
	return Sum / Norm;
}

void FInkMistSurface::StampDisc(const FVector2D& CenterUV, float RadiusUv, float FeatherFrac,
	const FLinearColor& Color, float Density, const FPlane* Planes, int32 NumPlanes)
{
	if (Res <= 0 || RadiusUv <= 0.0f || Density <= 0.0f)
	{
		return;
	}
	BuildLut(Color);
	Density = FMath::Clamp(Density, 0.0f, 1.0f);
	FeatherFrac = FMath::Clamp(FeatherFrac, 0.02f, 1.0f);
	const float CoreFrac = 1.0f - FeatherFrac;

	const float CxPx = static_cast<float>(CenterUV.X) * Res;
	const float CyPx = static_cast<float>(CenterUV.Y) * Res;
	const float RadPx = RadiusUv * Res;
	const int32 X0 = FMath::Clamp(FMath::FloorToInt(CxPx - RadPx), 0, Res - 1);
	const int32 X1 = FMath::Clamp(FMath::CeilToInt(CxPx + RadPx) + 1, 0, Res);
	const int32 Y0 = FMath::Clamp(FMath::FloorToInt(CyPx - RadPx), 0, Res - 1);
	const int32 Y1 = FMath::Clamp(FMath::CeilToInt(CyPx + RadPx) + 1, 0, Res);
	if (X1 <= X0 || Y1 <= Y0)
	{
		return;
	}

	const float InvRes = 1.0f / Res;
	const float InvRadPx = 1.0f / FMath::Max(RadPx, 0.001f);
	FColor* Px = Pixels.GetData();
	for (int32 Y = Y0; Y < Y1; ++Y)
	{
		const float DyPx = (Y + 0.5f) - CyPx;
		FColor* Row = Px + static_cast<int64>(Y) * Res;
		for (int32 X = X0; X < X1; ++X)
		{
			const float DxPx = (X + 0.5f) - CxPx;
			const float R01 = FMath::Sqrt(DxPx * DxPx + DyPx * DyPx) * InvRadPx;
			if (R01 >= 1.0f)
			{
				continue;
			}
			// 稿線牆：逐像素半平面測（UV 空間、相對章心）
			if (NumPlanes > 0)
			{
				const FVector2D OffUv(DxPx * InvRes, DyPx * InvRes);
				bool bBlocked = false;
				for (int32 Pi = 0; Pi < NumPlanes; ++Pi)
				{
					if (static_cast<float>(FVector2D::DotProduct(OffUv, Planes[Pi].NormalUv)) > Planes[Pi].DistUv)
					{
						bBlocked = true;
						break;
					}
				}
				if (bBlocked)
				{
					continue;
				}
			}
			// 剖面：硬心＋smoothstep 軟邊（與退役貼圖同式）
			float Prof = 1.0f;
			if (R01 > CoreFrac)
			{
				const float T = (1.0f - R01) / FeatherFrac;
				Prof = T * T * (3.0f - 2.0f * T);
			}
			float DMod = Density * Prof;
			if (GrainAmp > 0.0f)
			{
				// 皮膚錨定紋理（見 Configure 註）：乘在密度上、錨在畫布座標
				DMod = FMath::Clamp(DMod * (1.0f + GrainAmp * GrainAt(X, Y)), 0.0f, 1.0f);
			}
			const uint8 D = static_cast<uint8>(FMath::RoundToInt(DMod * 255.0f));
			if (D == 0)
			{
				continue;
			}
			FColor& Dst = Row[X];
			const int64 PxIdx = static_cast<int64>(Y) * Res + X;
			if (D >= Dst.A)
			{
				Dst = Lut[D];
				if (BaseEpoch.Num() > 0)
				{
					BaseEpoch[PxIdx] = CurrentEpoch;
				}
				if (Glaze.Num() > 0)
				{
					Glaze[PxIdx] = FColor(0, 0, 0, 0); // 新實墨＝新表面
				}
			}
			else if (Prof >= 0.999f && static_cast<uint32>(D) * 4u < static_cast<uint32>(Dst.A) * 3u &&
				BaseEpoch.Num() > 0 && BaseEpoch[PxIdx] != CurrentEpoch)
			{
				// 罩染（09-02 glazing；三修＝三道閘）：
				//   ①只有**硬心像素**罩染——軟邊掃過既有墨照舊走 max＝筆劃自己的
				//     軟邊不會把自己的硬心疊深（首驗實錘：淡檔均勻 0.305→0.358）
				//   ②新墨必須**顯著**比基底淡（<0.75×）——同檔重掃的紋理抖動不算罩染
				//   ③基底必須是**別筆**寫的（世代圖）——比值閘擋不住連續漸變：
				//     收筆淡出的尾巴在低流量端相鄰章相對落差自然 <0.75 ⇒ 自我罩染
				//    （二驗實錘：尾 0.298→0.439）。同筆＝同一次下墨＝永遠走 max。
				// 真正的跨筆跨檔罩染（淡壓滿、中壓滿、腮紅罩底調）照常通過。
				EnsureGlaze();
				FColor& G = Glaze[PxIdx];
				if (D >= G.A)
				{
					G = Lut[D];
				}
			}
		}
	}
	ExpandDirty(X0, Y0, X1, Y1);
}

void FInkMistSurface::StampDot(const FVector2D& CenterUV, float RadiusUv, float EdgeUv,
	const FLinearColor& Color, float Density)
{
	if (Res <= 0 || RadiusUv <= 0.0f || Density <= 0.0f)
	{
		return;
	}
	BuildLut(Color);
	Density = FMath::Clamp(Density, 0.0f, 1.0f);

	const float CxPx = static_cast<float>(CenterUV.X) * Res;
	const float CyPx = static_cast<float>(CenterUV.Y) * Res;
	const float RadPx = RadiusUv * Res;
	const float EdgePx = FMath::Max(EdgeUv * Res, 0.75f); // 至少次像素斜坡＝抗鋸齒
	const int32 X0 = FMath::Clamp(FMath::FloorToInt(CxPx - RadPx), 0, Res - 1);
	const int32 X1 = FMath::Clamp(FMath::CeilToInt(CxPx + RadPx) + 1, 0, Res);
	const int32 Y0 = FMath::Clamp(FMath::FloorToInt(CyPx - RadPx), 0, Res - 1);
	const int32 Y1 = FMath::Clamp(FMath::CeilToInt(CyPx + RadPx) + 1, 0, Res);
	if (X1 <= X0 || Y1 <= Y0)
	{
		return;
	}

	FColor* Px = Pixels.GetData();
	for (int32 Y = Y0; Y < Y1; ++Y)
	{
		const float DyPx = (Y + 0.5f) - CyPx;
		FColor* Row = Px + static_cast<int64>(Y) * Res;
		for (int32 X = X0; X < X1; ++X)
		{
			const float DxPx = (X + 0.5f) - CxPx;
			const float RPx = FMath::Sqrt(DxPx * DxPx + DyPx * DyPx);
			const float Prof = FMath::Clamp((RadPx - RPx) / EdgePx, 0.0f, 1.0f);
			float DMod = Density * Prof;
			if (GrainAmp > 0.0f)
			{
				DMod = FMath::Clamp(DMod * (1.0f + GrainAmp * GrainAt(X, Y)), 0.0f, 1.0f);
			}
			const uint8 D = static_cast<uint8>(FMath::RoundToInt(DMod * 255.0f));
			if (D == 0)
			{
				continue;
			}
			FColor& Dst = Row[X];
			const int64 PxIdx = static_cast<int64>(Y) * Res + X;
			if (D >= Dst.A)
			{
				Dst = Lut[D];
				if (BaseEpoch.Num() > 0)
				{
					BaseEpoch[PxIdx] = CurrentEpoch;
				}
				if (Glaze.Num() > 0)
				{
					Glaze[PxIdx] = FColor(0, 0, 0, 0); // 新實墨＝新表面
				}
			}
			else if (Prof >= 0.999f && static_cast<uint32>(D) * 4u < static_cast<uint32>(Dst.A) * 3u &&
				BaseEpoch.Num() > 0 && BaseEpoch[PxIdx] != CurrentEpoch)
			{
				// 罩染（同 StampDisc 的三閘規則）
				EnsureGlaze();
				FColor& G = Glaze[PxIdx];
				if (D >= G.A)
				{
					G = Lut[D];
				}
			}
		}
	}
	ExpandDirty(X0, Y0, X1, Y1);
}
