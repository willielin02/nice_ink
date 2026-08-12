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
	// 2026-08-12 加深（原 122,111,97＝對紙色 3.4:1＝小字不及格；現值 5.0:1
	// 過 WCAG 4.5 標準——字越小對比要越高，小又淡=雙重削弱是反向操作）
	static const FLinearColor InkDim    = FLinearColor::FromSRGBColor(FColor(94, 85, 72));
	static const FLinearColor AmberDeep = FLinearColor::FromSRGBColor(FColor(191, 122, 24));
}

// --- 字體角色表（2026-08-11 樣式源統一）---
// 規則：畫面上每一行字必須屬於一個角色；程式碼禁填裸字級/裸字距——改字級
// 只准改這張表。尺寸＝1080p 基準（Slate 走 viewport DPI 曲線、canvas 乘
// UiScale=ClipY/1080）。顏色慣例：標題=Paper、卡上正文=Ink、說明=InkDim、
// 警告=Ink（同級字、用色分輕重）、狀態=Amber/Red。
namespace NiType
{
	struct FRole
	{
		int32 Size;
		int32 Tracking;
		bool bSerif;   // 明朝體（標題/動作＝墨字性格）；false=圓體（正文/工具字）
		bool bBlack;   // 明朝體黑字重
		bool bBold;    // 圓體粗字重（標籤/警告＝與相鄰層的分離軸之一）
	};

	// 階梯鐵則（2026-08-12 「四層擠一號」事故後）：相鄰身分至少差兩樣
	//（尺寸／粗細／顏色），同尺寸不同身分必須粗細+顏色雙軸分離；
	// 空值不得領大字（Value 只發給有資訊的數字）
	constexpr FRole Display     {88, 120, true,  true,  false};  // 遊戲標題（僅主選單頂帶）
	constexpr FRole Title       {42,  80, true,  true,  false};  // 頁標題／房碼字母
	constexpr FRole Value       {26,   0, true,  false, false};  // 大數值（僅有資訊時）
	constexpr FRole Action      {20,   0, true,  false, false};  // 卡上主/次動作按鈕
	constexpr FRole ActionSmall {16,   0, true,  false, false};  // 導航/ghost 鈕、清單列鈕
	constexpr FRole Warning     {15,   0, false, false, true };  // 警告：粗+墨（比說明大兩階）
	constexpr FRole Body        {15,   0, false, false, false};  // 正文：欄位、輸入、副標
	constexpr FRole Note        {13,   0, false, false, false};  // 說明/狀態（淡墨；狀態=酒金/紅）
	constexpr FRole Label       {11, 200, false, false, true };  // 區段標籤：最小但粗+字距（對比已 5:1）
	constexpr FRole Micro       {10,   0, false, false, false};  // 版本戳等邊角資訊

	// 局內 canvas HUD 四級字階（HUD v1 立、同表存放；乘 UiScale 使用）
	constexpr float HudDisplay = 34.0f;
	constexpr float HudTitle   = 21.0f;
	constexpr float HudBody    = 15.0f;
	constexpr float HudSmall   = 12.0f;
}

// --- 間距網格（4 的倍數；選單 Slate 版面用）---
namespace NiSpace
{
	constexpr float XS = 4.0f;   // 值下方的貼身說明
	constexpr float S  = 8.0f;   // 標籤→欄位、同組相鄰行
	constexpr float M  = 16.0f;  // 欄位組之間、動作→狀態
	constexpr float L  = 24.0f;  // 動作區前後的大段落
	constexpr float BandTop    = 56.0f;  // 標題帶離頂（全站骨架）
	constexpr float BandBottom = 56.0f;  // 底帶離底（全站骨架）
	constexpr float CardW      = 460.0f; // 動作卡標準寬
	constexpr float CardWWide  = 560.0f; // 寬卡（設定/語言/房列表）
	constexpr float CardPadH   = 32.0f;  // 卡內距（水平）
	constexpr float CardPadV   = 28.0f;  // 卡內距（垂直）
	constexpr float BtnPadV    = 14.0f;  // 卡上動作鈕的垂直內距
}
