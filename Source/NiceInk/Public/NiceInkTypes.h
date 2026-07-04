#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "NiceInkTypes.generated.h"

UENUM(BlueprintType)
enum class ENiceInkPhase : uint8
{
	Lobby,
	SelectingVictim,
	Drinking,
	Tattooing,
	Accusation,
	Reveal,
	Celebration,
	NextRound
};

UENUM(BlueprintType)
enum class ENiceInkMarkerType : uint8
{
	FineMarker,
	ThickMarker,
	BrushTip
};

UENUM(BlueprintType)
enum class ENiceInkBodyType : uint8
{
	Lean,
	Average,
	Heavy,
	Muscular
};

UENUM(BlueprintType)
enum class ENiceInkFaceControl : uint8
{
	HeadWidth,
	HeadHeight,
	FaceRoundness,
	ForeheadHeight,
	BrowHeight,
	BrowAngle,
	EyeSize,
	EyeSpacing,
	EyeDepth,
	EyeAngle,
	UpperEyelid,
	LowerEyelid,
	NoseBridgeHeight,
	NoseBridgeWidth,
	NoseTipSize,
	NoseTipAngle,
	NostrilWidth,
	CheekboneHeight,
	CheekboneWidth,
	CheekFullness,
	MouthWidth,
	UpperLipFullness,
	LowerLipFullness,
	MouthCornerHeight,
	ChinWidth,
	ChinHeight,
	ChinProjection,
	JawWidth,
	JawAngle,
	JawForward,
	EarSize,
	EarAngle,
	NeckThickness,
	AgeLines,
	FaceAsymmetry
};

UENUM(BlueprintType)
enum class ENiceInkBodyControl : uint8
{
	Height,
	ShoulderWidth,
	ChestSize,
	WaistSize,
	HipWidth,
	ArmMuscle,
	ArmLength,
	LegMuscle,
	LegLength,
	BodyFat,
	MuscleMass,
	Posture
};

USTRUCT(BlueprintType)
struct FMarkerConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marker")
	ENiceInkMarkerType Type = ENiceInkMarkerType::FineMarker;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marker", meta = (ClampMin = "1.0", ClampMax = "240.0"))
	float StrokesPerSecond = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marker", meta = (ClampMin = "0.001", ClampMax = "0.1"))
	float UvRadius = 0.008f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marker", meta = (ClampMin = "1", ClampMax = "24"))
	int32 DotsPerStroke = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marker", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Opacity = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marker")
	FVector2D PatternScale = FVector2D(1.0f, 1.0f);
};

USTRUCT(BlueprintType)
struct FTattooStroke
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo")
	FVector2D UV = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo")
	FLinearColor Color = FLinearColor::Black;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo")
	ENiceInkMarkerType MarkerType = ENiceInkMarkerType::FineMarker;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Pressure = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo", meta = (ClampMin = "0.001", ClampMax = "0.25"))
	float Radius = 0.008f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo")
	float Timestamp = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo")
	int32 ArtistPlayerId = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo")
	int32 TargetPlayerId = INDEX_NONE;

	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
	bool operator==(const FTattooStroke& Other) const;
};

template<>
struct TStructOpsTypeTraits<FTattooStroke> : public TStructOpsTypeTraitsBase2<FTattooStroke>
{
	enum
	{
		WithNetSerializer = true,
		WithIdenticalViaEquality = true
	};
};

USTRUCT()
struct FTattooStrokeItem : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY()
	FTattooStroke Stroke;
};

USTRUCT()
struct FTattooStrokeHistory : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FTattooStrokeItem> Items;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FTattooStrokeItem, FTattooStrokeHistory>(Items, DeltaParms, *this);
	}

	void AddStroke(const FTattooStroke& Stroke);
};

template<>
struct TStructOpsTypeTraits<FTattooStrokeHistory> : public TStructOpsTypeTraitsBase2<FTattooStrokeHistory>
{
	enum
	{
		WithNetDeltaSerializer = true
	};
};

USTRUCT(BlueprintType)
struct FNiceInkNamedFloat
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Value")
	FName Name = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Value", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Value = 0.0f;
};

USTRUCT(BlueprintType)
struct FNiceInkMorphTargetBinding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	FName ControlName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	FName MorphTargetName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	float Multiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	float Bias = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	bool bInvert = false;
};

USTRUCT(BlueprintType)
struct FCharacterAppearance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	TArray<FNiceInkNamedFloat> FaceControls;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	TArray<FNiceInkNamedFloat> FaceMorphs;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	TArray<FNiceInkNamedFloat> BodyControls;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	TArray<FNiceInkNamedFloat> BodyMorphs;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	FLinearColor SkinTone = FLinearColor(0.75f, 0.55f, 0.42f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	FLinearColor SkinUndertone = FLinearColor(1.0f, 0.82f, 0.68f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	FLinearColor EyeColor = FLinearColor(0.18f, 0.12f, 0.08f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	ENiceInkBodyType BodyType = ENiceInkBodyType::Average;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	int32 PresetIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	int32 SkinToneIndex = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	int32 SkinDetailIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance", meta = (ClampMin = "0.75", ClampMax = "1.25"))
	float HeightScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FreckleIntensity = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BlemishIntensity = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ScarIntensity = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AgeDetail = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SkinRoughness = 0.45f;
};
