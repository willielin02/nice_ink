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
	// 13 以下唯一的例外（2026-09-11 user：「玩家名稱的字體可以小一點」）：大廳席位格底下的玩家名。
	// 名字是臉的註腳、不是被讀的內文（臉才是身分載體，08-06 定案雙載體），寬度又被 80 的格鎖住；
	// 13 級的名字比格寬（Hanamichi 84 > 80）⇒ 左右各突出 2px，user 讀成「左側被切到」。
	constexpr int32 Caption = 11;

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
	constexpr FRole NameTag     {Caption,   0, false, false, false}; // 席位格下的玩家名（白；與 Note 差字級＋透明度兩軸）

	// **所有鍵名一個字體、一個字級**（2026-09-10；user：「按鍵的字體大小有均一致嗎？」→
	// 「我要讓所有字高對齊改之前的 F、G」）＝ **Noto Sans Bold 13pt**，大寫字高 13px（0.54H）。
	// 三修一度全改 Oswald 10pt（字高 11）——比原本的 F／G 矮 2px，user 指名要回到原本的高度。
	// 長鍵不縮字：ENTER 在這個字級要 ~2.25×H，所以寬度檔位開到 2.25（見 NiUi::KeycapMaxRatio）。
	// canvas 端（墨杯盤的 Q）走 ETextTier::Key，同一個數字換算成 px（Slate 是 pt）。
	constexpr int32 KeyLabel = Small;

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

	// **鍵帽高 32 → 24**（2026-09-10）：32 是量 Meccha 實物來的，但我只抄了鍵沒抄字——
	// §15.12 實測「鍵高÷動詞字高」我們 2.0，而 RV 1.4／PEAK ~1.4／Meccha ~1.3
	// ⇒ 同樣的畫法在我們這裡會比參照吵一倍。24 ＝ 6U（在網格上）、比 16px 的動詞字高 1.5 倍。
	constexpr float KeycapH   = 6.0f * U;   // 24 鍵帽高
	constexpr float KeycapR   = 1.0f * U;   //  4 鍵帽圓角＝全站唯一的圓角
	// 鍵帽現制＝**實心白＋深色字**的 9-slice 貼圖 T_UI_Keycap／T_UI_KeycapDim
	//（2026-09-10 三版，user：「把整個遊戲的按鍵指引都改成 Meccha／PEAK 同款」；
	// 烘焙＝Tools/AssetPrep/keycap_texture.py，三版史與 09-06 的否決都寫在那支的檔頭）。
	// **三個載體共用同一顆**：局內 Slate（SNiKeycap）／墨杯盤的 canvas（DrawKeycap）／主選單。
	// **十修（2026-09-11）：字也烘進圖裡，一顆鍵一張**（/Game/UI/Keys/T_Key_<KEY>；尺寸表＝NiceInkKeycapData.h，
	// 由烘焙腳本生成）。Slate 排字在非整數縮放下對盒子與字形各自取整 ⇒ 上下留白必有一顆差 1px（user 視窗實測
	// ESC 6/5）；字進貼圖後整顆帽只取一次整＝對稱是構造保證。下面的 9-slice 與留白常數只剩表外鍵名的保底在用。
	// **不可按＝可按的那張圖整顆乘透明度**（2026-09-11 十一修，user 定案：「在可按的樣子的基礎上調整透明度即可」）。
	// 值與同一列的動詞文字、滑鼠圖示的不可按態同一個（0.45）＝整列一起退後，不是鍵自己換一種畫法。
	// 09-10 的空心灰框版退役（當時理由＝乘 alpha 在障子牆上只剩 19 階對比；user 知情選擇）。
	constexpr float KeycapDimAlpha = 0.45f;
	constexpr float KeycapSlice = 0.375f;   // 9-slice 邊界（貼圖比例）＝角 12px、中間帶才拉伸
	// **鍵帽寬度只准落在四檔**（2026-09-10）：1.0／1.25／1.5／1.75 × 高——這是真鍵盤的比例，
	// Kenney 的同一套圖就是 1.00（字母／ENTER／ESC）、1.33（TAB）、1.50（SHIFT）、1.71（SPACE）；
	// Meccha SPACE 實測 1.75、SHIFT ~1.5。「字寬＋留白」連續取值會讓每一顆鍵都是自己的寬度，
	// 一欄看起來像量出來的不像設計的；四檔讓 ESC 與 TAB 同寬、ENTER 與 WASD 同寬。
	// 我們此前 ENTER 77×24＝**3.2**＝把一個單字撐成一條。單鍵一律 1.0（正方形）。
	// 規則住 NiSlate::KeycapWidth（三個載體共用）。
	// **字與帽邊的留白＝所有鍵同一個值**（2026-09-10 五修；user：「多字母的鍵的字與邊框之間的距離
	// 有與單字母的鍵統一嗎？」——沒有：單字母 7~8、多字母 4~5，因為多字母是跳到「剛好塞得下」的檔位）。
	// 現制：多字母寬度＝字寬＋2×KeycapPad，**不再跳檔**；單字母維持方格（Noto Bold 13 的大寫
	// 平均 ~12px 寬 ⇒ 方格裡的留白 ≈ (24−12)/2 = 6，與 KeycapPad 同值 ⇒ 兩種鍵留白視覺一致；
	// F 這種窄字母會多 1~2px，那是方格的天性，每一把鍵盤都如此）。
	// 離散檔位（1.0/1.25/…）退役：它與「留白一致」互斥——檔位保證的是寬度的整齊，不是留白的整齊。
	constexpr float KeycapPad      = 1.5f * U;    // 6：字與帽邊的左右留白（單字母／多字母同值）
	// 全站普查（§15.13 六修）：Slate 畫出來的字串比它任何量測 API 寬 ~0.5px/字（ENTER +2.5、SHIFT +2）。
	// 字**置中**放（誤差左右對分），帽寬再補這個差 ⇒ 左右留白回到 6±0.5。
	constexpr float KeycapRenderSlackPerChar = 0.5f;
	// 上限只防呆（超長鍵名）。2.75 那版把 ENTER（需 67）鉗到 66 ⇒ 全站普查量到它的留白 3/3
	// 而別顆 5/6——**鉗位一咬到，留白就不一致**；留白一致是 user 的要求，寬度不是。
	constexpr float KeycapMaxRatio = 4.0f;
	constexpr float Margin    = 6.0f * U;   // 24 四邊共用邊距
	constexpr float RowPitch  = 16.0f * U;  // 64 操作提示的列距
	constexpr float GapS      = 1.0f * U;   //  4 glyph↔動詞、鍵帽之間
	constexpr float GapM      = 2.0f * U;   //  8
	constexpr float GapL      = 4.0f * U;   // 16 段落之間
	constexpr float FaceS     = 6.0f * U;   // 24 上緣受害者臉
	constexpr float FaceM     = 16.0f * U;  // 64 列表的臉（大廳席位）
	// 大廳席位格（2026-09-11 user 定案：「放上該房間對應人數的框框，依進入順序填入大頭」；同日二修：
	// 「不要標，所有人的框框一模一樣，固定順序，最左邊就是房主」）。
	// 格＝臉 64 ＋ 四邊 8 ＝ 80；黑 25% 圓角底、**無外框**（三修 user：「格子不需要框框，無論有沒有人」）。房主不另標
	//（九款調查：PEAK／Lethal／Content Warning／Liar's Bar 這一派合作派對遊戲都不標，房主＝能按 START 的人）。
	// §11 原文：「底部一排臉（空位＝黑 25% 淡框）」。
	// 五修（09-11 user：「需要更大一點的頭貼」→ 選 80）：大廳臉 64→80。80 是 64（列表）與 96（揭曉）之間唯一
	// 落在 4px 網格上又保住「大廳 < 揭曉」階梯的值；用 96 等於宣告大廳跟認人的時刻一樣重要。
	constexpr float FaceSeat  = 20.0f * U;  // 80 大廳席位的臉
	constexpr float SeatPad   = 2.0f * U;   //  8 臉到格邊
	constexpr float SeatFrame = FaceSeat + 2.0f * SeatPad;   // 96
	constexpr float SeatPitch = 32.0f * U;  // 128 席距（格 96 ＋ 間隙 32；12.9 二修的 112 是格 80 時的值）
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
