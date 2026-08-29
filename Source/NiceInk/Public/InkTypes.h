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

// 回合證據標記（SPEC v3：不屬於墨水階梯）——噴漬與瘀青。
// 實作為保留作者 ID 的特殊 Marker works：與麥克筆同管線複寫與洗掉，
// 永不進巡禮（AuthorId < 0 過濾）、永不轉刺青。
UENUM(BlueprintType)
enum class EInkEvidenceType : uint8
{
	Sneeze, // 噴嚏（鼻）
	Piss,   // 尿（陰部）
	Shit,   // 屎（肛門）
	Bruise  // 瘀青（拳腳）
};

namespace InkEvidence
{
	// 保留作者 ID（負值＝證據，非玩家傑作）
	constexpr int32 AuthorIdFor(EInkEvidenceType Type)
	{
		switch (Type)
		{
		case EInkEvidenceType::Sneeze: return -10;
		case EInkEvidenceType::Piss:   return -11;
		case EInkEvidenceType::Shit:   return -12;
		default:                       return -20; // Bruise
		}
	}
}

// 針型（2026-07-23 雙針制；07-25 打稿制加 Stencil，user 定案）：真實刺青工作流——
// Stencil 麥克筆打稿（龍膽紫稿線、手速自由、甦醒時全洗＝從不進巡禮）→
// Liner 液線針（實線勾輪廓，寬度＝MarkerUvRadius 導出的 3.0mm；08-02 前為 3.9mm；
//   壓在稿線上＝機器沿稿自動走）＋
// Shader 打霧針（填色）。渲染半徑/點距/節拍逐針查表；SPEC 對齊由 user 統一處理。
// Stencil 排第三＝Liner/Shader 線上值與舊存檔零遷移。
UENUM(BlueprintType)
enum class EInkNeedle : uint8
{
	Liner,
	Shader,
	Stencil
};

// 稿線墨色（07-25 打稿制）：結晶紫 #703593（sRGB）→ linear——龍膽紫染料的標準色票；
// Spirit 轉印紙「高可視紫」同一染料＝全世界刺青稿線的顏色（考證 07-25）。
// 固定色不吃調色盤（稿=導引不是作品；一眼與墨區分）。
namespace NiceInkStencil
{
	inline FLinearColor Color() { return FLinearColor::FromSRGBColor(FColor(0x70, 0x35, 0x93)); }
}

// 一次落筆到抬筆的連續筆劃。Points 為身體 UV 空間折線。
// 筆寬由 NeedleType 查表（雙針制前=全域常數；舊存檔預設 Liner=原行為）。
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

	// 點刺筆劃（2026-07-22 刺青手感改制）：Points＝出墨節拍（TattooDotHz）落下的獨立
	// 針點，渲染逐點蓋章、永不內插連線——「實線」由作畫端的巡航速率上限保證
	//（v_max = k×筆寬×頻率；跨縫/跨肢的點間大跳＝誠實的兩顆點，不再有內插垃圾線）。
	// 舊存檔預設 false＝折線筆劃照舊段落渲染（不遷移）。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink")
	bool bDotStroke = false;

	// 針型（雙針制 07-23）：渲染半徑查表；舊存檔預設 Liner＝原行為
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink")
	EInkNeedle NeedleType = EInkNeedle::Liner;

	// 逐點出墨流量（07-24 十二版 手速→濃淡）：0–255＝0.0–1.0 濃度因子，與 Points
	// 逐索引對齊——打霧的灰階活在手速上（快甩=淡、慢推=濃），因子在落針當下量化
	// 進筆劃資料＝live/重放/碳黑/跨端從同一份資料得到同一濃度（決定性）。
	// 空陣列或缺項＝滿濃度（舊存檔與液線針零遷移；Liner 恆不寫=機器擁有速度）。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink")
	TArray<uint8> PointFlow;
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
