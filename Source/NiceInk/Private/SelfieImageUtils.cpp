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
