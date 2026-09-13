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

	// 引擎複製 PlayerState（seamless travel／inactive 副本）只抄它自己的欄位（分數／名字／UniqueId），自訂欄位全部歸零。
	// 2026-09-13 上午的 bug 就是這樣來的（重連副本 SeatIndex=-1 ⇒ 重進者隱形）；同日 user 定案關掉重連保留
	//（GameMode::AddInactivePlayer 覆寫成空），這個覆寫留著是衛生：任何日後走到 Duplicate 的路都帶著身分。
	virtual void CopyProperties(APlayerState* PlayerState) override;

	// 入場順序（0 起算）；同時是環形席位與 avatar 名冊索引
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	int32 SeatIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	int32 AvatarIndex = 0;

	// 玩家在主選單選的臉（?Avatar= travel option／主機從 GameInstance 讀）。
	// server-only 意向值：PostLogin 派發 AvatarIndex 時優先採用（被佔用則輪派）。
	int32 DesiredAvatarIndex = INDEX_NONE;

	// server-only：跨場資產已套用（雲端上行或主機本機槽，擇一次）——
	// 防雙重還原（上行與逾時 fallback 是兩條並行路）
	bool bAssetsRestored = false;

	// server-only（P1 簽章制；Docs/ANTICHEAT_PLAN.md §4.1）：persona 經驗證進房
	//（簽章＋序號過、或「空身宣稱」對過後端帳本）。false＝本場一律不落雲——
	// 「假裝雲端故障進房洗白」的人打完整場，金庫原封不動；誠實玩家的暫時性
	// 雲端故障也因此不會被結算回寫成永久抹除。未配置後端時此旗標不消費。
	bool bPersonaVerified = false;

	// 房主（listen server 本人；2026-08-13 大廳房主標示＋ESC 踢人 UI 的依據）
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	bool bIsRoomHost = false;

	// 這個席位**不會有**自訂臉 blob（無臉端／打包失敗；server 在 ServerSetFaceReady 標）。
	// 2026-09-11 user：「頭像要嘛完整顯示、要嘛先不要顯示」——觀看端據此分辨「還在等」與「等不到」：
	// 等得到＝空著等真肖像烘好；等不到＝名冊臉是誠實的終態、可以直接畫。此前觀看端把兩者都畫成名冊臉，
	// 於是每個進房的人都先閃一張別人的臉。
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	bool bFaceNone = false;

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
