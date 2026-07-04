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
	bool RunFaceParsing(UTexture2D* SelfiePhoto, TArray<uint8>& OutSkinMask);

	UFUNCTION(BlueprintCallable, Category = "Selfie|Debug")
	FLinearColor RunSkinColorExtraction(UTexture2D* SelfiePhoto, const TArray<uint8>& SkinMask);

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

	// NNE model inference placeholders
	// These will be filled in when ONNX models are integrated.
	// For now they produce synthetic/test outputs.
	bool InferFaceDetection(const TArray<FColor>& Pixels, int32 Width, int32 Height, TArray<FVector2D>& OutLandmarks, float& OutYaw, TArray<FColor>& OutFrontalizedPixels, int32& OutFrontalW, int32& OutFrontalH);
	bool InferFaceParsing(const TArray<FColor>& Pixels, int32 Width, int32 Height, TArray<uint8>& OutSegmentation);
};
