#pragma once

#include "CoreMinimal.h"
#include "SelfieTypes.h"
#include "SelfiePipeline.generated.h"

class UTexture2D;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSelfieProcessed, const FSelfieResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSelfieError, const FString&, ErrorMessage);

UCLASS(BlueprintType)
class NICEINK_API USelfiePipeline : public UObject
{
	GENERATED_BODY()

public:
	USelfiePipeline();

	UPROPERTY(BlueprintAssignable, Category = "Selfie")
	FOnSelfieProcessed OnSelfieProcessed;

	UPROPERTY(BlueprintAssignable, Category = "Selfie")
	FOnSelfieError OnSelfieError;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selfie", meta = (ClampMin = "10.0", ClampMax = "45.0"))
	float MaxHeadYaw = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selfie", meta = (ClampMin = "5.0", ClampMax = "40.0"))
	float OmbreThreshold = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selfie", meta = (ClampMin = "0.0", ClampMax = "0.1"))
	float BaldAreaThreshold = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selfie", meta = (ClampMin = "0.0", ClampMax = "0.2"))
	float BundledHairAreaThreshold = 0.08f;

	// Puppet face UV landmarks (68 points in [0,1] UV space).
	// Must be set before processing to match the target mesh's face layout.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selfie|Face")
	TArray<FVector2D> PuppetFaceLandmarks;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selfie|Face")
	int32 FaceTextureResolution = 512;

	UFUNCTION(BlueprintCallable, Category = "Selfie")
	bool LoadModels(const FString& ModelDirectory);

	UFUNCTION(BlueprintPure, Category = "Selfie")
	bool AreModelsLoaded() const { return bModelsLoaded; }

	// Main entry point: process a selfie photo and produce a character result.
	UFUNCTION(BlueprintCallable, Category = "Selfie")
	FSelfieResult ProcessSelfie(UTexture2D* SelfiePhoto);

	// Individual pipeline stages (exposed for testing/debugging)

	UFUNCTION(BlueprintCallable, Category = "Selfie|Debug")
	bool RunFaceDetection(UTexture2D* SelfiePhoto, TArray<FVector2D>& OutLandmarks, float& OutYaw, UTexture2D*& OutFrontalizedUV);

	UFUNCTION(BlueprintCallable, Category = "Selfie|Debug")
	bool RunFaceParsing(UTexture2D* SelfiePhoto, TArray<uint8>& OutSkinMask, TArray<uint8>& OutHairMask);

	UFUNCTION(BlueprintCallable, Category = "Selfie|Debug")
	FLinearColor RunSkinColorExtraction(UTexture2D* SelfiePhoto, const TArray<uint8>& SkinMask);

	UFUNCTION(BlueprintCallable, Category = "Selfie|Debug")
	FNiceInkHairColorData RunHairColorAnalysis(UTexture2D* SelfiePhoto, const TArray<uint8>& HairMask);

	UFUNCTION(BlueprintCallable, Category = "Selfie|Debug")
	int32 RunHairStyleClassification(UTexture2D* SelfiePhoto, const TArray<uint8>& HairMask);

	// Manual hair style override (when CLIP guess is wrong)
	UFUNCTION(BlueprintCallable, Category = "Selfie")
	void OverrideHairStyle(int32 HairStyleIndex);

	UFUNCTION(BlueprintPure, Category = "Selfie")
	FSelfieResult GetLastResult() const { return LastResult; }

private:
	bool bModelsLoaded = false;
	FSelfieResult LastResult;

	// Extract raw BGRA pixels from a UTexture2D
	static bool ExtractPixels(UTexture2D* Texture, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight);

	// BiSeNet parsing class indices (CelebAMask-HQ labeling)
	static constexpr uint8 BISENET_SKIN = 1;
	static constexpr uint8 BISENET_NOSE = 10;
	static constexpr uint8 BISENET_UPPER_LIP = 12;
	static constexpr uint8 BISENET_LOWER_LIP = 13;
	static constexpr uint8 BISENET_HAIR = 17;

	// NNE model inference placeholders
	// These will be filled in when ONNX models are integrated.
	// For now they produce synthetic/test outputs.
	bool InferFaceDetection(const TArray<FColor>& Pixels, int32 Width, int32 Height, TArray<FVector2D>& OutLandmarks, float& OutYaw, TArray<FColor>& OutFrontalizedPixels, int32& OutFrontalW, int32& OutFrontalH);
	bool InferFaceParsing(const TArray<FColor>& Pixels, int32 Width, int32 Height, TArray<uint8>& OutSegmentation);
	bool InferClipImageEncode(const TArray<FColor>& Pixels, int32 Width, int32 Height, TArray<float>& OutEmbedding);
	bool InferClipTextEncode(const FString& Text, TArray<float>& OutEmbedding);

	// Precomputed text embeddings for all 50 hair styles
	TArray<TArray<float>> CachedTextEmbeddings;
	bool bTextEmbeddingsCached = false;
	void EnsureTextEmbeddings();
};
