#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "NiceInkTypes.generated.h"

UENUM(BlueprintType)
enum class ENiceInkPhase : uint8
{
	Lobby,
	SelectingVictim,
	Binding,
	Tattooing,
	SoulGuessing,
	Reveal,
	Celebration,
	NextRound
};

UENUM(BlueprintType)
enum class ENiceInkNeedleType : uint8
{
	RoundLiner,
	RoundShader,
	Magnum,
	CurvedMagnum
};

UENUM(BlueprintType)
enum class ENiceInkBodyType : uint8
{
	Lean,
	Average,
	Heavy,
	Muscular
};

USTRUCT(BlueprintType)
struct FNeedleConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Needle")
	ENiceInkNeedleType Type = ENiceInkNeedleType::RoundLiner;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Needle", meta = (ClampMin = "1.0", ClampMax = "240.0"))
	float StrikesPerSecond = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Needle", meta = (ClampMin = "0.001", ClampMax = "0.1"))
	float UvRadius = 0.008f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Needle", meta = (ClampMin = "1", ClampMax = "24"))
	int32 DotsPerStrike = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Needle", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Opacity = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Needle")
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
	ENiceInkNeedleType NeedleType = ENiceInkNeedleType::RoundLiner;

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
struct FCharacterAppearance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	TArray<FNiceInkNamedFloat> FaceMorphs;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	TArray<FNiceInkNamedFloat> BodyMorphs;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	FLinearColor SkinTone = FLinearColor(0.75f, 0.55f, 0.42f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	FLinearColor HairColor = FLinearColor(0.04f, 0.025f, 0.015f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	ENiceInkBodyType BodyType = ENiceInkBodyType::Average;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	int32 PresetIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	int32 HairStyleIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance", meta = (ClampMin = "0.75", ClampMax = "1.25"))
	float HeightScale = 1.0f;
};
