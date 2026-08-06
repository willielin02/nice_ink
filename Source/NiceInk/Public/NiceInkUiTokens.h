#pragma once

#include "CoreMinimal.h"

// HUD 調色盤：全部取自遊戲世界（墨、紙、酒、皮膚、迷宮薰衣草）。
// 主選單 HUD 與局內 HUD 共用——樣式只有一套。
// FromSRGBColor 鐵律：FLinearColor 直塞 0-1 顯示值會被 gamma 校正洗亮。
namespace NiHudColor
{
	static const FLinearColor Ink      = FLinearColor::FromSRGBColor(FColor(24, 19, 15));
	static const FLinearColor Paper    = FLinearColor::FromSRGBColor(FColor(242, 232, 214));
	static const FLinearColor PaperDim = FLinearColor::FromSRGBColor(FColor(168, 156, 138));
	static const FLinearColor Amber    = FLinearColor::FromSRGBColor(FColor(232, 163, 61));
	static const FLinearColor Red      = FLinearColor::FromSRGBColor(FColor(217, 79, 61));
	static const FLinearColor Green    = FLinearColor::FromSRGBColor(FColor(134, 176, 108));
	static const FLinearColor Skin     = FLinearColor::FromSRGBColor(FColor(238, 195, 168));
	static const FLinearColor Lavender = FLinearColor::FromSRGBColor(FColor(169, 163, 207));

	// 白卡制（2026-08-06 user 打回深棕全暗＝糊成一團泥；素色半透明=Meccha 式
	// 白卡＋墨字）：白面板上的文字色階與深酒金
	static const FLinearColor InkDim    = FLinearColor::FromSRGBColor(FColor(122, 111, 97));
	static const FLinearColor AmberDeep = FLinearColor::FromSRGBColor(FColor(191, 122, 24));
}
