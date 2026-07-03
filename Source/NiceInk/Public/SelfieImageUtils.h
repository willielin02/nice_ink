#pragma once

#include "CoreMinimal.h"
#include "SelfieTypes.h"

struct NICEINK_API FSelfieImageUtils
{
	// --- Color space conversion ---

	static FLabColor RGBToLab(float InR, float InG, float InB);
	static void LabToRGB(const FLabColor& Lab, float& OutR, float& OutG, float& OutB);

	static float SRGBToLinear(float C);
	static float LinearToSRGB(float C);

	// --- Batch conversion ---

	static TArray<FLabColor> PixelsToLab(const TArray<FColor>& Pixels);

	// --- Skin color extraction ---
	// Extracts median skin tone from masked pixels in LAB space.
	// Mask: per-pixel uint8 where nonzero = skin.
	static FLinearColor ExtractSkinTone(const TArray<FColor>& Pixels, const TArray<uint8>& SkinMask, int32 Width, int32 Height);

	// --- K-means clustering ---
	// Runs K-means on masked pixels in LAB space.
	// Returns K clusters sorted by pixel count descending.
	static TArray<FColorCluster> KMeansClustering(const TArray<FLabColor>& LabPixels, const TArray<uint8>& Mask, int32 Width, int32 Height, int32 K, int32 MaxIterations = 20);

	// K-means on a vertical sub-region of the mask (YStart inclusive, YEnd exclusive).
	static TArray<FColorCluster> KMeansClusterRegion(const TArray<FLabColor>& LabPixels, const TArray<uint8>& Mask, int32 Width, int32 Height, int32 YStart, int32 YEnd, int32 K, int32 MaxIterations = 20);

	// --- Hair color analysis ---
	// Full hair color pipeline: splits into root/tip zones, runs K-means, classifies mode.
	static FNiceInkHairColorData AnalyzeHairColor(const TArray<FColor>& Pixels, const TArray<uint8>& HairMask, int32 Width, int32 Height, float OmbreThreshold = 15.0f);

	// --- Mask utilities ---

	static FIntRect GetMaskBoundingBox(const TArray<uint8>& Mask, int32 Width, int32 Height);
	static float GetMaskAreaRatio(const TArray<uint8>& Mask);
	static bool IsBundledHair(const TArray<uint8>& HairMask, int32 Width, int32 Height, float AreaThreshold = 0.08f);

	// --- Face texture generation ---
	// Generates a UV-space face texture from selfie pixels + landmarks.
	// SourceLandmarks: 68-point face landmarks in pixel coords on the selfie.
	// TargetLandmarks: corresponding UV coords on the puppet face mesh.
	// OutputSize: square texture resolution.
	static UTexture2D* GenerateFaceTexture(UObject* Outer, const TArray<FColor>& SelfiePixels, int32 SrcWidth, int32 SrcHeight, const TArray<FVector2D>& SourceLandmarks, const TArray<FVector2D>& TargetLandmarks, int32 OutputSize = 512);

	// Compute 2x3 affine matrix from 3 point pairs.
	static bool ComputeAffine(const FVector2D& Src0, const FVector2D& Src1, const FVector2D& Src2, const FVector2D& Dst0, const FVector2D& Dst1, const FVector2D& Dst2, float OutMatrix[6]);

	// Apply affine transform to a point.
	static FVector2D ApplyAffine(const float Matrix[6], const FVector2D& Point);

	// Invert a 2x3 affine matrix.
	static bool InvertAffine(const float Matrix[6], float OutInverse[6]);

	// Generate elliptical alpha mask (1.0 inside, 0.0 outside, smooth falloff).
	static TArray<float> GenerateEllipticalMask(int32 Width, int32 Height, float CenterX, float CenterY, float RadiusX, float RadiusY, float Feather = 0.08f);

private:
	static float LabF(float T);
	static float LabFInv(float T);
	static FLabColor MedianLabFromMaskedPixels(const TArray<FLabColor>& LabPixels, const TArray<uint8>& Mask, int32 Width, int32 Height);
};
