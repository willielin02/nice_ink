#pragma once

#include "CoreMinimal.h"
#include "SelfieTypes.generated.h"

class UTexture2D;

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
struct NICEINK_API FSelfieResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Selfie")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "Selfie")
	bool bFaceDetected = false;

	UPROPERTY(BlueprintReadOnly, Category = "Selfie")
	FString ErrorMessage;

	UPROPERTY(BlueprintReadOnly, Category = "Selfie")
	float HeadYaw = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Selfie")
	FLinearColor DetectedSkinTone = FLinearColor(0.75f, 0.55f, 0.42f, 1.0f);

	UPROPERTY(BlueprintReadOnly, Category = "Selfie")
	TArray<FVector2D> FaceLandmarks;

	UPROPERTY(BlueprintReadOnly, Category = "Selfie")
	TObjectPtr<UTexture2D> FaceTexture = nullptr;
};
