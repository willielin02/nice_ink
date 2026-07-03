#pragma once

#include "CoreMinimal.h"
#include "SelfieTypes.generated.h"

class UTexture2D;

UENUM(BlueprintType)
enum class ENiceInkHairColorMode : uint8
{
	Solid       UMETA(DisplayName = "Solid"),
	Ombre       UMETA(DisplayName = "Ombre"),
	Highlights  UMETA(DisplayName = "Highlights")
};

struct NICEINK_API FLabColor
{
	float L = 0.0f;
	float A = 0.0f;
	float B = 0.0f;

	FLabColor() = default;
	FLabColor(float InL, float InA, float InB) : L(InL), A(InA), B(InB) {}

	float DistanceAB(const FLabColor& Other) const
	{
		const float DA = A - Other.A;
		const float DB = B - Other.B;
		return FMath::Sqrt(DA * DA + DB * DB);
	}

	float Distance(const FLabColor& Other) const
	{
		const float DL = L - Other.L;
		const float DA = A - Other.A;
		const float DB = B - Other.B;
		return FMath::Sqrt(DL * DL + DA * DA + DB * DB);
	}

	FLinearColor ToLinearColor() const;
	static FLabColor FromLinearColor(const FLinearColor& Color);
	static FLabColor FromFColor(const FColor& Color);
};

struct NICEINK_API FColorCluster
{
	FLabColor Center;
	int32 Count = 0;
	float Ratio = 0.0f;
};

USTRUCT(BlueprintType)
struct NICEINK_API FNiceInkHairColorData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hair")
	ENiceInkHairColorMode Mode = ENiceInkHairColorMode::Solid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hair")
	FLinearColor BaseColor = FLinearColor(0.04f, 0.025f, 0.015f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hair")
	FLinearColor RootColor = FLinearColor(0.04f, 0.025f, 0.015f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hair")
	FLinearColor TipColor = FLinearColor(0.04f, 0.025f, 0.015f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hair", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RootAmount = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hair")
	FLinearColor HighlightColor = FLinearColor(0.6f, 0.45f, 0.25f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hair", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HighlightRatio = 0.0f;
};

USTRUCT(BlueprintType)
struct NICEINK_API FHairStyleEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hair")
	FString DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hair")
	FString ClipPrompt;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hair")
	FSoftObjectPath GroomAssetPath;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hair")
	bool bMale = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hair")
	int32 Index = 0;
};

USTRUCT(BlueprintType)
struct NICEINK_API FSelfieResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Selfie")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "Selfie")
	bool bFaceDetected = false;

	UPROPERTY(BlueprintReadOnly, Category = "Selfie")
	bool bHairDetected = false;

	UPROPERTY(BlueprintReadOnly, Category = "Selfie")
	bool bBundledHair = false;

	UPROPERTY(BlueprintReadOnly, Category = "Selfie")
	FString ErrorMessage;

	UPROPERTY(BlueprintReadOnly, Category = "Selfie")
	float HeadYaw = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Selfie")
	FLinearColor DetectedSkinTone = FLinearColor(0.75f, 0.55f, 0.42f, 1.0f);

	UPROPERTY(BlueprintReadOnly, Category = "Selfie")
	FNiceInkHairColorData HairColor;

	UPROPERTY(BlueprintReadOnly, Category = "Selfie")
	int32 MatchedHairStyleIndex = -1;

	UPROPERTY(BlueprintReadOnly, Category = "Selfie")
	FString MatchedHairStyleName;

	UPROPERTY(BlueprintReadOnly, Category = "Selfie")
	TArray<FVector2D> FaceLandmarks;

	UPROPERTY(BlueprintReadOnly, Category = "Selfie")
	TObjectPtr<UTexture2D> FaceTexture = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Selfie")
	TArray<int32> CandidateHairStyles;
};
