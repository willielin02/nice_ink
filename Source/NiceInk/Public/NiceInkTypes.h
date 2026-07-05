#pragma once

#include "CoreMinimal.h"
#include "NiceInkTypes.generated.h"

class UTexture2D;

// SPEC v3.0 一場遊戲的相位。回合迴圈 = Seating → Drawing → Tour → Accusation → Resolution
// → (Seating | Finale)。作畫階段無固定時長：受害者按 WASD 現身即收束（定案 #17）。
UENUM(BlueprintType)
enum class ENiceInkPhase : uint8
{
	Lobby,          // 等人；桑拿房自由走動
	BottleSpin,     // 開場轉酒瓶選首位受害者（純儀式）
	Seating,        // 受害者喝入座酒、閉眼昏睡
	Drawing,        // 作畫階段（時長＝受害者小遊戲進度）
	Tour,           // 傑作巡禮：全員同步逐幅檢視
	Accusation,     // 指認：受害者選一幅、指認作者
	Resolution,     // 結算演出：猜對換人／猜錯真作者上墨＋罰酒
	Finale,         // 三杯昏死：瓜分現金＋羞辱時間＋鈦白鎖定
	PostGame        // 遊戲結束（場間大廳為 M5）
};

UENUM(BlueprintType)
enum class ENiceInkAccusationResult : uint8
{
	None,
	Correct,
	Wrong
};

// 一位玩家的 avatar 資產組（FacePipeline 產出、已入引擎）。
// 以索引複製（PlayerState.AvatarIndex），各端自行載入資產。
USTRUCT(BlueprintType)
struct FNiceInkAvatarDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Avatar")
	FString Key;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Avatar")
	FString FaceOpenPath;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Avatar")
	FString FaceClosedPath;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Avatar")
	FString EyeMaskPath;

	// FacePipeline 量測的膚色（linear）
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Avatar")
	FLinearColor SkinTone = FLinearColor(0.4f, 0.2f, 0.13f, 1.0f);
};

// 內建六人名冊（自拍上傳的運行時管線是正式版後續工作；朋友場用預生成 avatar）。
struct FNiceInkAvatars
{
	static NICEINK_API int32 Num();
	static NICEINK_API const FNiceInkAvatarDef& Get(int32 Index);
};

// 小畫家式共用調色盤（麥克筆自選色；選色是畫風的一部分，與身分無關）
struct FNiceInkPalette
{
	static NICEINK_API int32 Num();
	static NICEINK_API FLinearColor Get(int32 Index);
};
