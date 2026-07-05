#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "InkTypes.h"
#include "NiceInkSaveGame.generated.h"

// 跨場的角色資產：錢包與刺青（SPEC：錢包傾向持久、刺青跟著角色走——恩怨博物館）。
// 存檔鍵目前＝玩家名＋席位（PIE 可測）；正式版改 EOS product user id（M6 之後）。
UCLASS()
class NICEINK_API UNiceInkSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int32 Cash = 10000;

	// 只存碳黑／永久（麥克筆永不跨場）
	UPROPERTY()
	TArray<FInkWork> Tattoos;
};
