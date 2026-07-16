#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "NiceInkPlayerState.generated.h"

UCLASS()
class NICEINK_API ANiceInkPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	ANiceInkPlayerState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 入場順序（0 起算）；同時是環形席位與 avatar 名冊索引
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	int32 SeatIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	int32 AvatarIndex = 0;

	// 玩家在主選單選的臉（?Avatar= travel option／主機從 GameInstance 讀）。
	// server-only 意向值：PostLogin 派發 AvatarIndex 時優先採用（被佔用則輪派）。
	int32 DesiredAvatarIndex = INDEX_NONE;

	// 連續罰酒杯數（只數罰酒；猜對離座歸零；第三杯＝終局）
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	int32 PenaltyCups = 0;

	// 入場現金（終局唯一易手點；雷射是唯一出口）
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	int32 Cash = 10000;

	// 筆劃作者 ID（傑作分組、指認、碳黑轉換的身分原子）
	UFUNCTION(BlueprintPure, Category = "Nice Ink")
	int32 GetInkAuthorId() const { return GetPlayerId(); }
};
