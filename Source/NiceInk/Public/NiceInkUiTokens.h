#pragma once

#include "CoreMinimal.h"

// ============================================================================
// UI 設計 token（2026-09-05 中性制；user 定案「作廢 08-06 白卡明朝制，改簡約中性」）
//
// 系統只有這麼多：
//   顏色 ＝ 白（四級透明度）／黑（模態與壓暗）／一個強調色（當前・選中・確認）／紅（危險）
//   字體 ＝ 展示體 Oswald（大寫、拉字距）＋內文體 Noto Sans（13 語同家）——兩種人格
//   字級 ＝ 13／18／96 三級（頁標題 48、動詞 24、相位 32），展示體只有 Medium／Bold
//   間距 ＝ 4px 網格
//   圓角 ＝ 一個值
//
// 原則：UI 與內容要在不同的音域。世界是暖的（皮膚、木頭、榻榻米、和紙），UI 就無彩；
// 強調色只准畫在深色面上，永遠不直接落在世界上（此前酒金動詞疊皮膚 1.41、
// 紙色疊障子牆 1.27＝暖疊暖的分離度天生不夠）。
//
// 舊 token 名（Paper／Ink／Amber…）保留為別名，讓既有呼叫點不必逐一改名——
// 語義對照見各別名旁的註解。FromSRGBColor 鐵律：FLinearColor 直塞 0-1 顯示值會被
// gamma 校正洗亮。
// ============================================================================
namespace NiHudColor
{
	// --- 中性四色 ---
	static const FLinearColor White    = FLinearColor::FromSRGBColor(FColor(242, 242, 242));
	static const FLinearColor Black    = FLinearColor::FromSRGBColor(FColor(10, 10, 12));
	// 強調色＝**清酒金**（2026-09-06 五款參照實測後定案，見 UI_SYSTEM §11.9）：
	// 沒有品牌色的遊戲從世界的光裡取一個調亮的顏色（Liar's Bar 的金＝燈光與威士忌），
	// 我們世界的光＝酒——入座酒與罰酒是每回合的儀式、罰酒杯是唯一的比分。
	// 08-06 的酒金失敗在位置（壓在同明度的皮膚上），不在色相；現制強調色只准落在深色面上。
	// Accent＝填色（選中 chip／房號塊；上面放**黑字** OnAccent 9:1，白字只有 1.9:1 禁用）
	// AccentText＝深底上的強調文字與外框（對黑 12:1；**不准落在世界上**）
	// 結晶紫退役：它是稿線的墨色，畫面上那支筆就是紫的＝一色兩義。
	// **無主色**（2026-09-07 user 定案：「UI 一定需要一個主色嗎？」→ 拿掉）：這是一個
	// 關於「顏色落在皮膚上」的遊戲——Crayola 十色的墨、碳黑、皮膚、木頭、榻榻米，畫面裡的
	// 色彩全是內容，HUD 再帶一個主色就是跟墨搶；清酒金尤其糟（與皮膚、木頭同是暖色，
	// G 鍵帽在膚色上那塊金幾乎讀不出來）。主色的兩份工作改由別的通道做：
	// 「現在能按」＝形狀與明度（實心白鍵帽 vs 黑底白字 vs 35% 空心）；
	// 「剛剛什麼變了」＝動態（滲入、印章落下、現金跳字）。紅保留＝語義色（失去東西）。
	// 舊名 Accent／AccentText／OnAccent 保留為白／白／黑，既有呼叫點不必改。
	static const FLinearColor Accent     = FLinearColor::FromSRGBColor(FColor(242, 242, 242));
	static const FLinearColor AccentText = FLinearColor::FromSRGBColor(FColor(242, 242, 242));
	static const FLinearColor OnAccent   = FLinearColor::FromSRGBColor(FColor(10, 10, 12));
	static const FLinearColor Red      = FLinearColor::FromSRGBColor(FColor(224, 82, 70));

	// 白的四級（文字／chrome 的層次只用透明度分，不另造灰）
	static const FLinearColor White70  = FLinearColor(White.R, White.G, White.B, 0.70f);
	static const FLinearColor White45  = FLinearColor(White.R, White.G, White.B, 0.45f);
	static const FLinearColor White25  = FLinearColor(White.R, White.G, White.B, 0.25f);

	// --- 別名（既有呼叫點；語義對照）---
	static const FLinearColor Paper    = White;        // 主文字／chrome
	static const FLinearColor PaperDim = White70;      // 次要文字（標籤、說明）
	static const FLinearColor Ink      = Black;        // 面板底／壓暗／鍵帽字
	static const FLinearColor InkDim   = FLinearColor::FromSRGBColor(FColor(96, 96, 100));
	static const FLinearColor Amber    = White;        // 舊「酒金文字」＝現在就是白（無主色）
	static const FLinearColor AmberDeep = White;       // 舊「深酒金」＝白
	static const FLinearColor Green    = White;        // 成功＝白（失敗＝Red，語義色）
	// 遊戲幾何專用（沉睡黑屏／墨杯棋盤）——不是 chrome 色
	static const FLinearColor Skin     = FLinearColor::FromSRGBColor(FColor(238, 195, 168));
	static const FLinearColor Lavender = FLinearColor::FromSRGBColor(FColor(169, 163, 207));
	// 透明度棋盤的暗格：白的陰影版（地對比工作區間 15~70 sRGB 階，見 DrawInkCup）
	static const FLinearColor PaperShade = FLinearColor::FromSRGBColor(FColor(178, 178, 178));
}

// --- 字體角色表 ---
// 規則：畫面上每一行字必須屬於一個角色；程式碼禁填裸字級／裸字距——改字級只准改這張表。
// 尺寸＝1080p 基準（Slate 走 viewport DPI 曲線、canvas 乘 UiScale=ClipY/1080）。
// **四階**：14 標籤／18 內文與動詞／28 標題與祈使句／64 主角數字（房號、倒數、標題字）。
// 相鄰身分至少差兩軸（尺寸／字重／透明度）；空值不得領大字。
namespace NiType
{
	struct FRole
	{
		int32 Size;
		int32 Tracking;
		bool bSerif;   // **展示字體**（2026-09-06：Oswald 壓縮大寫；欄位名沿用＝字面槽位名沒改）
		bool bBlack;   // 展示字體的粗檔（SerifBlack＝Oswald Bold；否則 Serif＝Oswald Medium）
		bool bBold;    // 內文字體的粗檔（Noto Sans Bold）
	};

	// **兩種字體人格、三級尺度**（2026-09-06 第一批「怎麼畫」；六款參照的共同紀律）：
	// 展示體＝Oswald（粗壓縮、全大寫、字距拉開）給標誌／頁標題／相位／主角數字／主鈕；
	// 內文體＝Noto Sans 給說明與清單。尺度只留 13 標籤／18 內文／96 主角，頁標題 48、
	// 動詞 24——28 與 36 兩級刪除：全部字落在 14～28 的「文件區間」就是沒有節奏。
	// 展示體的文字在呼叫端一律 ToUpper（CJK／阿拉伯不受影響）。
	constexpr int32 Hero    = 96;
	constexpr int32 Heading = 32;
	constexpr int32 Text    = 18;
	constexpr int32 Small   = 13;

	constexpr FRole Display     {Hero,     60, true,  true,  true };  // 遊戲標題（主選單；有標誌貼圖時退居備援）
	constexpr FRole Title       {48,       60, true,  true,  true };  // 頁標題：展示體大寫
	constexpr FRole Value       {Text,     30, true,  false, true };  // 有資訊的數值：展示體 Medium
	constexpr FRole Action      {24,       60, true,  true,  true };  // 主動作（文字鈕／主鈕）：展示體大寫
	constexpr FRole ActionSmall {Text,      0, false, false, false}; // 次要動作／導覽
	constexpr FRole Warning     {Small,     0, false, false, true };  // 警告：粗＋白
	constexpr FRole Body        {Text,      0, false, false, false}; // 正文、欄位、輸入
	constexpr FRole Note        {Small,     0, false, false, false}; // 說明／狀態（White70）
	constexpr FRole Label       {Small,   150, false, false, false}; // 區段標籤：小＋字距、不粗（粗會跟內文搶）
	constexpr FRole Micro       {Small,     0, false, false, false}; // 版本戳（White25）

	// 局內 canvas HUD 四級（同一張表；乘 UiScale 使用）
	constexpr float HudDisplay = static_cast<float>(Hero);
	constexpr float HudTitle   = static_cast<float>(Heading);
	constexpr float HudBody    = static_cast<float>(Text);
	constexpr float HudSmall   = static_cast<float>(Small);
}

// --- 局內 HUD 尺度（基準 U = 4px @1080p，所有尺寸與間距都是 U 的整數倍）---
// 鍵帽高 32／列距 64／邊距 24 是量 Meccha 1080p 實物得來的（見 Docs/UI_SYSTEM.md），
// 中性制沿用——它們是尺度不是風格。儀器＝Tools/UiCheck/hud_mock.py。
namespace NiUi
{
	constexpr float U = 4.0f;

	constexpr float KeycapH   = 8.0f * U;   // 32 鍵帽高
	constexpr float KeycapR   = 1.0f * U;   //  4 鍵帽圓角＝全站唯一的圓角
	constexpr float Margin    = 6.0f * U;   // 24 四邊共用邊距
	constexpr float RowPitch  = 16.0f * U;  // 64 操作提示的列距
	constexpr float GapS      = 1.0f * U;   //  4 glyph↔動詞、鍵帽之間
	constexpr float GapM      = 2.0f * U;   //  8
	constexpr float GapL      = 4.0f * U;   // 16 段落之間
	constexpr float FaceS     = 6.0f * U;   // 24 上緣受害者臉
	constexpr float FaceM     = 16.0f * U;  // 64 列表的臉（大廳席位）
	constexpr float FaceL     = 24.0f * U;  // 96 揭曉的臉
	constexpr float Radius    = 1.0f * U;   //  4 全站唯一的圓角（面板、chip、鍵帽同值）
	constexpr float ModalDim  = 0.62f;      // 模態底（黑 62%）
	constexpr float FadeS     = 0.25f;      // 相位切換 chrome 淡入淡出（秒）
	// 動態（2026-09-07）：單色 UI 沒有主色告訴你「什麼變了」，全靠這兩個數字。
	// 滲入＝alpha 0→1 同時往上浮 InkRisePx；印章落下＝縮放 1.06→1.0。
	constexpr float InkInS    = 0.18f;
	constexpr float InkOutS   = 0.12f;
	constexpr float InkRisePx = 6.0f;
	constexpr float StampS    = 0.12f;
}

// --- 間距網格（4 的倍數；選單 Slate 版面用）---
namespace NiSpace
{
	constexpr float XS = 4.0f;
	constexpr float S  = 8.0f;
	constexpr float M  = 16.0f;
	constexpr float L  = 24.0f;
	constexpr float XL = 48.0f;
	constexpr float BandTop    = 56.0f;  // 標題帶離頂
	constexpr float BandBottom = 56.0f;  // 底帶離底
	constexpr float ColumnW    = 520.0f; // 內容欄寬（左欄；場景佔右）
	constexpr float CardW      = ColumnW;   // 別名（既有呼叫點）
	constexpr float CardWWide  = 620.0f;
	constexpr float CardPadH   = 0.0f;      // 無卡片制：內距歸零（別名留給舊呼叫點）
	constexpr float CardPadV   = 0.0f;
	constexpr float BtnPadV    = 12.0f;
	constexpr float Radius     = 4.0f;      // 與 NiUi::Radius 同值
}
