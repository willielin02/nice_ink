#pragma once

#include "CoreMinimal.h"
#include "NiceInkTypes.generated.h"

class UTexture2D;

// 2026-07-15 user 指示：暫時移除拳腳、僅保留噴射。只關玩家面四閘
// （E 輸入／迷宮授予／拾取點偵測與繪製／HUD）；ServerKick 與 DebugRoboKick
// 保留可用（robo 踹飛中斷測試靠它）。要恢復＝改回 true。
inline constexpr bool GNiceInkKickEnabled = false;

// 2026-08-02 SPEC v4.0 定案 #51：噴射與拳腳移出核心循環＝未來更新內容——
// 「核心循環完全不會有這些，第一版絕對不會有」（user 逐字）。噴射全鏈封存
// （Q 輸入／ServerSpray 伺服器拒收／HUD 技能列）；程式保留非刪除，
// 未來更新回歸＝兩閘改回 true。
inline constexpr bool GNiceInkSprayEnabled = false;

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

// 入睡儀式的分拍（2026-08-16；SPEC「入場與轉酒瓶」→「入座昏睡」之間的演出）。
// **零硬切鐵則**：每一拍的結束狀態 ≡ 下一拍的起始狀態，全程無 teleport；
// 崩塌終點 ≡ GetVictimLieTransform() ⇒ 睡姿接管當幀零跳變。
//
// 開場（BottleSpin 相位）＝Gather → Spin → Approach → PickUp → Drink → Collapse
// 回合 2+（Seating 相位）＝              Approach → PickUp → Drink → Collapse
//   （罰酒／入座酒走同一序列＝user 定案「都不要有硬切」的必然推論：只做開場
//     等於第二回合起又傳送落地）
UENUM(BlueprintType)
enum class ENiCeremonyStep : uint8
{
	None,      // 非儀式期間
	Gather,    // 全員從席位走到圈上角位、面向圈心
	Spin,      // 酒瓶原地轉、ease-out 停在受害者正前方（先抽後演）
	Approach,  // 受害者走進圈心、停在瓶邊
	PickUp,    // 屈膝前彎、右臂 IK 伸向瓶頸、瓶附著到手
	Drink,     // 起身、瓶抬到嘴、頭後仰
	Collapse   // 瓶脫手、膝軟、整體翻倒到躺姿
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
