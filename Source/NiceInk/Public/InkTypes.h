#pragma once

#include "CoreMinimal.h"
#include "InkTypes.generated.h"

// SPEC v2.2 墨水三階段：麥克筆（回合結束洗掉）→ 碳黑（猜錯轉換，可雷射）→ 永久（鈦白鎖定）
UENUM(BlueprintType)
enum class EInkWorkState : uint8
{
	Marker,
	Carbon,
	Permanent
};

// 一次落筆到抬筆的連續筆劃。Points 為身體 UV 空間折線。
// 固定筆寬是全域常數（SPEC：麥克筆同一種筆觸、粗度），不存在筆劃裡。
USTRUCT(BlueprintType)
struct FInkStroke
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink")
	FLinearColor Color = FLinearColor::Black;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink")
	TArray<FVector2D> Points;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink")
	float StartTimestamp = 0.0f;
};

// 傑作：一位作者在一個回合畫下的全部筆劃。
// 巡禮、指認、碳黑轉換、鈦白鎖定、雷射淡化全部以 Work 為原子單位。
USTRUCT(BlueprintType)
struct FInkWork
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink")
	int32 WorkId = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink")
	int32 AuthorId = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink")
	int32 RoundIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink")
	EInkWorkState State = EInkWorkState::Marker;

	// 0 = 原樣；1、2 = 逐步淡化；3 = 完全清除（Work 直接移除）。
	// 鈦白鎖定時凍結當前值（淡化的鎖在淡化態）。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink")
	int32 LaserLevel = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink")
	TArray<FInkStroke> Strokes;
};
