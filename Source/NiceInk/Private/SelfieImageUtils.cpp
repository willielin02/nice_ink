#include "SelfieImageUtils.h"

#include "Engine/Texture2D.h"

namespace
{
constexpr float D65_X = 0.95047f;
constexpr float D65_Y = 1.00000f;
constexpr float D65_Z = 1.08883f;

constexpr float LAB_EPSILON = 0.008856f;
constexpr float LAB_KAPPA = 903.3f;
constexpr float LAB_DELTA = 6.0f / 29.0f;
}

float FSelfieImageUtils::SRGBToLinear(float C)
{
	return C <= 0.04045f ? C / 12.92f : FMath::Pow((C + 0.055f) / 1.055f, 2.4f);
}

float FSelfieImageUtils::LinearToSRGB(float C)
{
	return C <= 0.0031308f ? C * 12.92f : 1.055f * FMath::Pow(C, 1.0f / 2.4f) - 0.055f;
}

float FSelfieImageUtils::LabF(float T)
{
	return T > LAB_EPSILON ? FMath::Pow(T, 1.0f / 3.0f) : (LAB_KAPPA * T + 16.0f) / 116.0f;
}

float FSelfieImageUtils::LabFInv(float T)
{
	return T > LAB_DELTA ? T * T * T : 3.0f * LAB_DELTA * LAB_DELTA * (T - 4.0f / 29.0f);
}

FLabColor FSelfieImageUtils::RGBToLab(float InR, float InG, float InB)
{
	const float LR = SRGBToLinear(InR);
	const float LG = SRGBToLinear(InG);
	const float LB = SRGBToLinear(InB);

	const float X = 0.4124564f * LR + 0.3575761f * LG + 0.1804375f * LB;
	const float Y = 0.2126729f * LR + 0.7151522f * LG + 0.0721750f * LB;
	const float Z = 0.0193339f * LR + 0.1191920f * LG + 0.9503041f * LB;

	const float FX = LabF(X / D65_X);
	const float FY = LabF(Y / D65_Y);
	const float FZ = LabF(Z / D65_Z);

	return FLabColor(116.0f * FY - 16.0f, 500.0f * (FX - FY), 200.0f * (FY - FZ));
}

void FSelfieImageUtils::LabToRGB(const FLabColor& Lab, float& OutR, float& OutG, float& OutB)
{
	const float FY = (Lab.L + 16.0f) / 116.0f;
	const float FX = Lab.A / 500.0f + FY;
	const float FZ = FY - Lab.B / 200.0f;

	const float X = D65_X * LabFInv(FX);
	const float Y = D65_Y * LabFInv(FY);
	const float Z = D65_Z * LabFInv(FZ);

	const float LR = 3.2404542f * X - 1.5371385f * Y - 0.4985314f * Z;
	const float LG = -0.9692660f * X + 1.8760108f * Y + 0.0415560f * Z;
	const float LB = 0.0556434f * X - 0.2040259f * Y + 1.0572252f * Z;

	OutR = FMath::Clamp(LinearToSRGB(FMath::Max(0.0f, LR)), 0.0f, 1.0f);
	OutG = FMath::Clamp(LinearToSRGB(FMath::Max(0.0f, LG)), 0.0f, 1.0f);
	OutB = FMath::Clamp(LinearToSRGB(FMath::Max(0.0f, LB)), 0.0f, 1.0f);
}

FLinearColor FLabColor::ToLinearColor() const
{
	float SR, SG, SB;
	FSelfieImageUtils::LabToRGB(*this, SR, SG, SB);
	return FLinearColor(FSelfieImageUtils::SRGBToLinear(SR), FSelfieImageUtils::SRGBToLinear(SG), FSelfieImageUtils::SRGBToLinear(SB), 1.0f);
}

FLabColor FLabColor::FromLinearColor(const FLinearColor& Color)
{
	return FSelfieImageUtils::RGBToLab(FSelfieImageUtils::LinearToSRGB(Color.R), FSelfieImageUtils::LinearToSRGB(Color.G), FSelfieImageUtils::LinearToSRGB(Color.B));
}

FLabColor FLabColor::FromFColor(const FColor& InColor)
{
	return FSelfieImageUtils::RGBToLab(InColor.R / 255.0f, InColor.G / 255.0f, InColor.B / 255.0f);
}

TArray<FLabColor> FSelfieImageUtils::PixelsToLab(const TArray<FColor>& Pixels)
{
	TArray<FLabColor> Result;
	Result.SetNum(Pixels.Num());
	for (int32 I = 0; I < Pixels.Num(); ++I)
	{
		Result[I] = FLabColor::FromFColor(Pixels[I]);
	}
	return Result;
}

FLabColor FSelfieImageUtils::MedianLabFromMaskedPixels(const TArray<FLabColor>& LabPixels, const TArray<uint8>& Mask, int32 Width, int32 Height)
{
	TArray<float> LVals, AVals, BVals;
	const int32 Total = Width * Height;

	for (int32 I = 0; I < Total && I < LabPixels.Num() && I < Mask.Num(); ++I)
	{
		if (Mask[I] == 0)
		{
			continue;
		}
		if (LabPixels[I].L < 5.0f || LabPixels[I].L > 95.0f)
		{
			continue;
		}
		LVals.Add(LabPixels[I].L);
		AVals.Add(LabPixels[I].A);
		BVals.Add(LabPixels[I].B);
	}

	if (LVals.Num() == 0)
	{
		return FLabColor(50.0f, 0.0f, 0.0f);
	}

	LVals.Sort();
	AVals.Sort();
	BVals.Sort();

	const int32 Mid = LVals.Num() / 2;
	return FLabColor(LVals[Mid], AVals[Mid], BVals[Mid]);
}

FLinearColor FSelfieImageUtils::ExtractSkinTone(const TArray<FColor>& Pixels, const TArray<uint8>& SkinMask, int32 Width, int32 Height)
{
	const TArray<FLabColor> LabPixels = PixelsToLab(Pixels);
	const FLabColor MedianLab = MedianLabFromMaskedPixels(LabPixels, SkinMask, Width, Height);
	return MedianLab.ToLinearColor();
}

TArray<FColorCluster> FSelfieImageUtils::KMeansClustering(const TArray<FLabColor>& LabPixels, const TArray<uint8>& Mask, int32 Width, int32 Height, int32 K, int32 MaxIterations)
{
	return KMeansClusterRegion(LabPixels, Mask, Width, Height, 0, Height, K, MaxIterations);
}

TArray<FColorCluster> FSelfieImageUtils::KMeansClusterRegion(const TArray<FLabColor>& LabPixels, const TArray<uint8>& Mask, int32 Width, int32 Height, int32 YStart, int32 YEnd, int32 K, int32 MaxIterations)
{
	TArray<FLabColor> MaskedPixels;
	const int32 RowStart = FMath::Clamp(YStart, 0, Height);
	const int32 RowEnd = FMath::Clamp(YEnd, 0, Height);

	for (int32 Y = RowStart; Y < RowEnd; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const int32 Idx = Y * Width + X;
			if (Idx < Mask.Num() && Mask[Idx] != 0 && Idx < LabPixels.Num())
			{
				MaskedPixels.Add(LabPixels[Idx]);
			}
		}
	}

	if (MaskedPixels.Num() < K)
	{
		TArray<FColorCluster> Result;
		if (MaskedPixels.Num() > 0)
		{
			FColorCluster& C = Result.AddDefaulted_GetRef();
			C.Center = MaskedPixels[0];
			C.Count = MaskedPixels.Num();
			C.Ratio = 1.0f;
		}
		return Result;
	}

	TArray<FLabColor> Centroids;
	Centroids.SetNum(K);
	const int32 Step = FMath::Max(1, MaskedPixels.Num() / K);
	for (int32 I = 0; I < K; ++I)
	{
		Centroids[I] = MaskedPixels[FMath::Min(I * Step, MaskedPixels.Num() - 1)];
	}

	TArray<int32> Assignments;
	Assignments.SetNum(MaskedPixels.Num());

	for (int32 Iter = 0; Iter < MaxIterations; ++Iter)
	{
		for (int32 P = 0; P < MaskedPixels.Num(); ++P)
		{
			float BestDist = MAX_FLT;
			int32 BestK = 0;
			for (int32 C = 0; C < K; ++C)
			{
				const float Dist = MaskedPixels[P].Distance(Centroids[C]);
				if (Dist < BestDist)
				{
					BestDist = Dist;
					BestK = C;
				}
			}
			Assignments[P] = BestK;
		}

		TArray<FLabColor> NewCentroids;
		NewCentroids.SetNum(K);
		TArray<int32> Counts;
		Counts.SetNumZeroed(K);

		for (int32 P = 0; P < MaskedPixels.Num(); ++P)
		{
			const int32 C = Assignments[P];
			NewCentroids[C].L += MaskedPixels[P].L;
			NewCentroids[C].A += MaskedPixels[P].A;
			NewCentroids[C].B += MaskedPixels[P].B;
			Counts[C]++;
		}

		bool bConverged = true;
		for (int32 C = 0; C < K; ++C)
		{
			if (Counts[C] > 0)
			{
				NewCentroids[C].L /= Counts[C];
				NewCentroids[C].A /= Counts[C];
				NewCentroids[C].B /= Counts[C];
			}
			else
			{
				NewCentroids[C] = Centroids[C];
			}

			if (Centroids[C].Distance(NewCentroids[C]) > 0.5f)
			{
				bConverged = false;
			}
		}

		Centroids = MoveTemp(NewCentroids);
		if (bConverged)
		{
			break;
		}
	}

	TArray<FColorCluster> Result;
	Result.SetNum(K);
	TArray<int32> FinalCounts;
	FinalCounts.SetNumZeroed(K);

	for (int32 P = 0; P < MaskedPixels.Num(); ++P)
	{
		FinalCounts[Assignments[P]]++;
	}

	for (int32 C = 0; C < K; ++C)
	{
		Result[C].Center = Centroids[C];
		Result[C].Count = FinalCounts[C];
		Result[C].Ratio = MaskedPixels.Num() > 0 ? static_cast<float>(FinalCounts[C]) / MaskedPixels.Num() : 0.0f;
	}

	Result.Sort([](const FColorCluster& A, const FColorCluster& B) { return A.Count > B.Count; });
	return Result;
}

FNiceInkHairColorData FSelfieImageUtils::AnalyzeHairColor(const TArray<FColor>& Pixels, const TArray<uint8>& HairMask, int32 Width, int32 Height, float OmbreThreshold)
{
	FNiceInkHairColorData Result;
	const TArray<FLabColor> LabPixels = PixelsToLab(Pixels);

	const FIntRect BBox = GetMaskBoundingBox(HairMask, Width, Height);
	if (BBox.Width() <= 0 || BBox.Height() <= 0)
	{
		return Result;
	}

	const int32 MidY = BBox.Min.Y + BBox.Height() / 2;
	TArray<FColorCluster> RootClusters = KMeansClusterRegion(LabPixels, HairMask, Width, Height, BBox.Min.Y, MidY, 2);
	TArray<FColorCluster> TipClusters = KMeansClusterRegion(LabPixels, HairMask, Width, Height, MidY, BBox.Max.Y, 2);
	TArray<FColorCluster> FullClusters = KMeansClustering(LabPixels, HairMask, Width, Height, 3);

	if (FullClusters.Num() == 0)
	{
		return Result;
	}

	const float FullVarianceAB = [&]()
	{
		if (FullClusters.Num() < 2)
		{
			return 0.0f;
		}
		float MaxDist = 0.0f;
		for (int32 I = 0; I < FullClusters.Num(); ++I)
		{
			for (int32 J = I + 1; J < FullClusters.Num(); ++J)
			{
				if (FullClusters[J].Ratio > 0.1f)
				{
					MaxDist = FMath::Max(MaxDist, FullClusters[I].Center.DistanceAB(FullClusters[J].Center));
				}
			}
		}
		return MaxDist;
	}();

	if (FullVarianceAB < 8.0f)
	{
		Result.Mode = ENiceInkHairColorMode::Solid;
		Result.BaseColor = FullClusters[0].Center.ToLinearColor();
		return Result;
	}

	if (RootClusters.Num() >= 1 && TipClusters.Num() >= 1)
	{
		const float RootTipDist = RootClusters[0].Center.DistanceAB(TipClusters[0].Center);
		if (RootTipDist > OmbreThreshold)
		{
			Result.Mode = ENiceInkHairColorMode::Ombre;
			Result.RootColor = RootClusters[0].Center.ToLinearColor();
			Result.TipColor = TipClusters[0].Center.ToLinearColor();
			Result.BaseColor = RootClusters[0].Center.ToLinearColor();
			const float RootHeight = static_cast<float>(MidY - BBox.Min.Y);
			const float TotalHeight = static_cast<float>(BBox.Height());
			Result.RootAmount = TotalHeight > 0.0f ? RootHeight / TotalHeight : 0.5f;
			return Result;
		}
	}

	if (FullClusters.Num() >= 2 && FullClusters[1].Ratio > 0.15f)
	{
		Result.Mode = ENiceInkHairColorMode::Highlights;
		Result.BaseColor = FullClusters[0].Center.ToLinearColor();
		Result.HighlightColor = FullClusters[1].Center.ToLinearColor();
		Result.HighlightRatio = FullClusters[1].Ratio;
		return Result;
	}

	Result.Mode = ENiceInkHairColorMode::Solid;
	Result.BaseColor = FullClusters[0].Center.ToLinearColor();
	return Result;
}

FIntRect FSelfieImageUtils::GetMaskBoundingBox(const TArray<uint8>& Mask, int32 Width, int32 Height)
{
	int32 MinX = Width, MinY = Height, MaxX = -1, MaxY = -1;

	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const int32 Idx = Y * Width + X;
			if (Idx < Mask.Num() && Mask[Idx] != 0)
			{
				MinX = FMath::Min(MinX, X);
				MinY = FMath::Min(MinY, Y);
				MaxX = FMath::Max(MaxX, X);
				MaxY = FMath::Max(MaxY, Y);
			}
		}
	}

	if (MaxX < 0)
	{
		return FIntRect(0, 0, 0, 0);
	}
	return FIntRect(MinX, MinY, MaxX + 1, MaxY + 1);
}

float FSelfieImageUtils::GetMaskAreaRatio(const TArray<uint8>& Mask)
{
	if (Mask.Num() == 0)
	{
		return 0.0f;
	}

	int32 Count = 0;
	for (const uint8 V : Mask)
	{
		if (V != 0)
		{
			++Count;
		}
	}
	return static_cast<float>(Count) / Mask.Num();
}

bool FSelfieImageUtils::IsBundledHair(const TArray<uint8>& HairMask, int32 Width, int32 Height, float AreaThreshold)
{
	const float AreaRatio = GetMaskAreaRatio(HairMask);
	if (AreaRatio < 0.005f || AreaRatio >= AreaThreshold)
	{
		return false;
	}

	const FIntRect BBox = GetMaskBoundingBox(HairMask, Width, Height);
	if (BBox.Height() <= 0)
	{
		return false;
	}

	const float TopRatio = static_cast<float>(BBox.Min.Y) / Height;
	return TopRatio < 0.35f;
}

bool FSelfieImageUtils::ComputeAffine(const FVector2D& Src0, const FVector2D& Src1, const FVector2D& Src2, const FVector2D& Dst0, const FVector2D& Dst1, const FVector2D& Dst2, float OutMatrix[6])
{
	const float Det = (Src0.X - Src2.X) * (Src1.Y - Src2.Y) - (Src1.X - Src2.X) * (Src0.Y - Src2.Y);
	if (FMath::Abs(Det) < SMALL_NUMBER)
	{
		return false;
	}

	const float InvDet = 1.0f / Det;

	OutMatrix[0] = ((Dst0.X - Dst2.X) * (Src1.Y - Src2.Y) - (Dst1.X - Dst2.X) * (Src0.Y - Src2.Y)) * InvDet;
	OutMatrix[1] = ((Dst1.X - Dst2.X) * (Src0.X - Src2.X) - (Dst0.X - Dst2.X) * (Src1.X - Src2.X)) * InvDet;
	OutMatrix[2] = Dst0.X - OutMatrix[0] * Src0.X - OutMatrix[1] * Src0.Y;

	OutMatrix[3] = ((Dst0.Y - Dst2.Y) * (Src1.Y - Src2.Y) - (Dst1.Y - Dst2.Y) * (Src0.Y - Src2.Y)) * InvDet;
	OutMatrix[4] = ((Dst1.Y - Dst2.Y) * (Src0.X - Src2.X) - (Dst0.Y - Dst2.Y) * (Src1.X - Src2.X)) * InvDet;
	OutMatrix[5] = Dst0.Y - OutMatrix[3] * Src0.X - OutMatrix[4] * Src0.Y;

	return true;
}

FVector2D FSelfieImageUtils::ApplyAffine(const float Matrix[6], const FVector2D& Point)
{
	return FVector2D(Matrix[0] * Point.X + Matrix[1] * Point.Y + Matrix[2], Matrix[3] * Point.X + Matrix[4] * Point.Y + Matrix[5]);
}

bool FSelfieImageUtils::InvertAffine(const float Matrix[6], float OutInverse[6])
{
	const float Det = Matrix[0] * Matrix[4] - Matrix[1] * Matrix[3];
	if (FMath::Abs(Det) < SMALL_NUMBER)
	{
		return false;
	}

	const float InvDet = 1.0f / Det;
	OutInverse[0] = Matrix[4] * InvDet;
	OutInverse[1] = -Matrix[1] * InvDet;
	OutInverse[3] = -Matrix[3] * InvDet;
	OutInverse[4] = Matrix[0] * InvDet;
	OutInverse[2] = -(OutInverse[0] * Matrix[2] + OutInverse[1] * Matrix[5]);
	OutInverse[5] = -(OutInverse[3] * Matrix[2] + OutInverse[4] * Matrix[5]);
	return true;
}

TArray<float> FSelfieImageUtils::GenerateEllipticalMask(int32 Width, int32 Height, float CenterX, float CenterY, float RadiusX, float RadiusY, float Feather)
{
	TArray<float> Mask;
	Mask.SetNum(Width * Height);

	const float FeatherPixelsX = RadiusX * Feather;
	const float FeatherPixelsY = RadiusY * Feather;
	const float InnerRX = RadiusX - FeatherPixelsX;
	const float InnerRY = RadiusY - FeatherPixelsY;

	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const float DX = static_cast<float>(X) - CenterX;
			const float DY = static_cast<float>(Y) - CenterY;
			const float EllipseOuter = (DX * DX) / (RadiusX * RadiusX) + (DY * DY) / (RadiusY * RadiusY);
			const float EllipseInner = InnerRX > 0.0f && InnerRY > 0.0f ? (DX * DX) / (InnerRX * InnerRX) + (DY * DY) / (InnerRY * InnerRY) : EllipseOuter;

			float Alpha;
			if (EllipseInner <= 1.0f)
			{
				Alpha = 1.0f;
			}
			else if (EllipseOuter >= 1.0f)
			{
				Alpha = 0.0f;
			}
			else
			{
				Alpha = 1.0f - (EllipseOuter - 1.0f) / FMath::Max(0.001f, EllipseOuter - EllipseInner) * (EllipseOuter - 1.0f);
				Alpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
			}

			Mask[Y * Width + X] = Alpha;
		}
	}

	return Mask;
}

UTexture2D* FSelfieImageUtils::GenerateFaceTexture(UObject* Outer, const TArray<FColor>& SelfiePixels, int32 SrcWidth, int32 SrcHeight, const TArray<FVector2D>& SourceLandmarks, const TArray<FVector2D>& TargetLandmarks, int32 OutputSize)
{
	if (SelfiePixels.Num() != SrcWidth * SrcHeight || SourceLandmarks.Num() < 68 || TargetLandmarks.Num() < 68)
	{
		return nullptr;
	}

	// Use eye centers (landmarks 36-41 left eye, 42-47 right eye) and nose tip (landmark 30) for affine
	auto EyeCenter = [](const TArray<FVector2D>& Lm, int32 Start, int32 End) -> FVector2D
	{
		FVector2D Sum = FVector2D::ZeroVector;
		int32 Count = 0;
		for (int32 I = Start; I <= End; ++I)
		{
			Sum += Lm[I];
			++Count;
		}
		return Count > 0 ? Sum / Count : FVector2D::ZeroVector;
	};

	const FVector2D SrcLeftEye = EyeCenter(SourceLandmarks, 36, 41);
	const FVector2D SrcRightEye = EyeCenter(SourceLandmarks, 42, 47);
	const FVector2D SrcNose = SourceLandmarks[30];

	const FVector2D DstLeftEye = EyeCenter(TargetLandmarks, 36, 41);
	const FVector2D DstRightEye = EyeCenter(TargetLandmarks, 42, 47);
	const FVector2D DstNose = TargetLandmarks[30];

	float AffineMatrix[6];
	if (!ComputeAffine(SrcLeftEye, SrcRightEye, SrcNose, DstLeftEye * OutputSize, DstRightEye * OutputSize, DstNose * OutputSize, AffineMatrix))
	{
		return nullptr;
	}

	float InvAffine[6];
	if (!InvertAffine(AffineMatrix, InvAffine))
	{
		return nullptr;
	}

	// Elliptical mask centered at face center, sized to face bounds
	const FVector2D FaceCenter = (DstLeftEye + DstRightEye) * 0.5f * OutputSize;
	const float EyeDist = FVector2D::Distance(DstLeftEye * OutputSize, DstRightEye * OutputSize);
	const float MaskRX = EyeDist * 1.6f;
	const float MaskRY = EyeDist * 2.1f;
	const TArray<float> EllipseMask = GenerateEllipticalMask(OutputSize, OutputSize, FaceCenter.X, FaceCenter.Y, MaskRX, MaskRY, 0.12f);

	TArray<FColor> OutputPixels;
	OutputPixels.SetNum(OutputSize * OutputSize);

	for (int32 DstY = 0; DstY < OutputSize; ++DstY)
	{
		for (int32 DstX = 0; DstX < OutputSize; ++DstX)
		{
			const FVector2D SrcPoint = ApplyAffine(InvAffine, FVector2D(DstX, DstY));
			const int32 SX = FMath::RoundToInt(SrcPoint.X);
			const int32 SY = FMath::RoundToInt(SrcPoint.Y);

			const float Alpha = EllipseMask[DstY * OutputSize + DstX];
			if (Alpha <= 0.0f || SX < 0 || SX >= SrcWidth || SY < 0 || SY >= SrcHeight)
			{
				OutputPixels[DstY * OutputSize + DstX] = FColor(0, 0, 0, 0);
				continue;
			}

			const FColor& SrcColor = SelfiePixels[SY * SrcWidth + SX];
			const uint8 A = static_cast<uint8>(FMath::Clamp(Alpha * 255.0f, 0.0f, 255.0f));
			OutputPixels[DstY * OutputSize + DstX] = FColor(SrcColor.R, SrcColor.G, SrcColor.B, A);
		}
	}

	UTexture2D* Texture = UTexture2D::CreateTransient(OutputSize, OutputSize, PF_B8G8R8A8, TEXT("SelfieFaceTexture"));
	if (!Texture)
	{
		return nullptr;
	}

	void* TextureData = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(TextureData, OutputPixels.GetData(), OutputPixels.Num() * sizeof(FColor));
	Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
	Texture->UpdateResource();

	return Texture;
}
