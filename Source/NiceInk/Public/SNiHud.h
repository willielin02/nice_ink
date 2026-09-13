#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Fonts/SlateFontInfo.h"
#include "NiceInkKeycapData.h"

class ANiceInkHUD;
class UFont;
struct FSlateBrush;

// ============================================================================
// 局內 HUD 的 Slate 層（2026-09-08；user 定案「把遊戲中所有畫面都改成 Slate 架構」）
//
// 為什麼搬：canvas HUD 的每一個座標都是手算的，沒有排版引擎在檢查我。這輪查出來的
// 錯全部是同一類——「原始碼裡寫的數字不等於螢幕上的距離」（寫 10、畫面 16，因為拿
// 行框高當墨跡高；64 的字級上螢幕是 88px 墨跡，因為 Slate 的字級是 pt 又乘了 UiScale）。
// 同一個專案裡，有排版引擎的那一半（主選單，2390 行 Slate）一個這種錯都沒有。
//
// 分工（不是全部塞進 widget——業界通例也是這樣切）：
//   **排版與文字** ⇒ Slate widget 樹（本檔）。位置由引擎算，我不准再自己算座標。
//   **幾何與著色** ⇒ 留在 canvas 或自繪葉節點（針尖環、墨杯盤、描圖盤、轉盤）。
//                    那些不是排版問題，搬過去只是換一塊畫布。
//
// 尺度：整棵樹包在 SDPIScaler(ViewportY / 1080) 裡 ⇒ **底下所有數字都是 1080p 設計單位**，
// 跟 NiUi／NiType 同一個座標系；再也不必在每個呼叫點乘 UiScale。
// ============================================================================
namespace NiSlate
{
	// Oswald 展示體的實量比例（本專案 09-06 對實拍截圖校過，見 BigCapTopOffset 的註解）：
	// 行框 = 1.482em、大寫 = 0.81em。**只有這兩個常數是量出來的**，其餘度量一律問 Slate。
	constexpr float DisplayLinePerEm = 1.482f;
	constexpr float DisplayCapPerEm  = 0.81f;

	// 字距（1/1000 em）。展示體本來就要拉開；房碼再多拉一點，但**不是靠插空格**——
	// 插空格會把一組四碼讀成四個字母（2026-09-08 user 指出）。字距讓它們既分得開又是一個詞。
	constexpr int32 DisplayTracking = 60;
	constexpr int32 CodeTracking    = 120;

	/** 內文體（Noto Sans）。SizePx＝1080p 設計單位，直接餵 NiType 的角色值。 */
	FSlateFontInfo BodyFont(UFont* Font, int32 SizePx, bool bBold, int32 Tracking = 0);

	/** 展示體（Oswald 壓縮大寫）。呼叫端負責 ToUpper。 */
	FSlateFontInfo DisplayFont(UFont* Font, int32 SizePx, bool bBlack, int32 Tracking = DisplayTracking);

	/** 展示體的大寫字高（設計單位）——版面的比例一律對這個量取，不對名目字級取。 */
	float DisplayCapHeight(const FSlateFontInfo& Info);

	/**
	 * 鍵帽的寬度規則（2026-09-10 實測九款，見 UI_SYSTEM §15.13）。**三個載體共用這一支**。
	 *
	 * - **單一字母＝正方形**。參照無一例外：PEAK 21×21（1/2/3/4 四顆逐位相同）、
	 *   Meccha 23×23、RV There Yet ~25×25。此前我們讓字寬決定帽寬 ⇒ F 27／G 31＝
	 *   同樣是一個字母卻兩種寬度、右緣看起來沒對齊（user 指出）。
	 * - **多字母＝寬度上限 1.75×高**（Meccha SPACE 實測 42×24＝1.75、SHIFT ~1.5、
	 *   Liar's Bar TAB 48×27＝1.78；他們的 ESC 甚至是 39×38＝方的，字縮小塞進去）。
	 *   我們此前 ENTER 是 77×24＝**3.2**，那不是鍵帽，是一條。
	 * - 長鍵改用**展示體**（Oswald 壓縮大寫）並自動縮字級到塞得下為止——這正是
	 *   Meccha 的 SPACE／SHIFT 在做的事（同一個盒子裡放更多字母，只能靠壓縮體）。
	 *
	 * @param Font    複合 UI 字體（局內與選單同一座）
	 * @param Key     鍵名（"F"／"ENTER"／"WASD"）
	 * @param InOutFont 進去是預設字體，出來是實際該用的（長鍵會被換成縮小的展示體）
	 * @return 鍵帽寬度（設計單位）
	 */
	// DpiScale＝這個 widget 真正被畫出來的縮放（GameViewport->GetDPIScale()）。**必須帶**：
	// 全站普查抓到 ENTER 留白 4/3 而 ESC 6/4、單字母 5/6——排版用的是小數前進寬，渲染時每個
	// 字形貼到整數像素，非整數縮放下每個字母漂 ~1px，五個字母就是 5px。用真實縮放去問字型快取，
	// 拿到的才是渲染器實際用的前進寬。
	// OutTextLeftPad：把字放進帽裡時，文字方塊的左內距（設計單位），讓**墨跡**（不是前進框）
	// 左右各留 KeycapPad。普查量到 Slate 渲染出來的多字母比 FSlateFontMeasure 量到的寬 ~1.3px/字
	//（ENTER +5.6、SHIFT +7.3、TAB +5.3、ESC +3.9，單字母 ±0），所以寬度與位置都改成直接讀
	// 字型快取在真實縮放下的字形度量（XAdvance／HorizontalOffset／USize）＝渲染器實際用的那一套。
	float KeycapWidth(class UFont* Font, const FString& Key, FSlateFontInfo& InOutFont, float DpiScale = 1.0f);

	/** 遊戲視口目前的 Slate DPI 縮放（UserInterfaceSettings 的曲線；沒有視口＝1）。
	 *  ⚠ 不要拿它去算在建立時就固定的版面值：視窗之後被拉大，值就過期（2026-09-11 鍵帽血價）。 */
	float ViewportDpiScale(const class UWorld* World);

	// Noto Sans（Bold）的大寫字高／em（OS/2 sCapHeight ÷ unitsPerEm，fontTools 讀出）。
	constexpr float BodyCapPerEm = 0.714f;

	/**
	 * 鍵名要往上抬多少（設計單位），大寫墨跡才會在鍵帽裡垂直置中。
	 * STextBlock 置中的是**行框**（上伸部＋下伸部，複合字體裡還被 CJK 面撐高），大寫墨跡只占基線以上
	 * 的 0.714em ⇒ 落在中線之下。1080p 湊巧量到 5/5，user 的 2560×1380 實測 **9/5**（七修被打回）。
	 * 用 Slate 自己在該縮放下的行高與基線去算，不用字型檔常數猜。
	 */
	float KeycapTextLift(const FSlateFontInfo& Font, float DpiScale);

	/**
	 * **十修（2026-09-11）：一顆鍵一張貼圖，字烘在圖裡。**
	 * user 視窗（2560×1380、縮放 1.234）實測 ESC 上／下 6／5、C 7／6，而且 C 帽 25px、ESC 帽 26px。
	 * 病不在任何常數：Slate 把盒子貼齊整數像素、又把每個字形各自貼齊整數像素，兩次取整互相獨立
	 * ⇒ 帽高 26 配字高 15 剩 11＝奇數，怎麼擺都 6／5。KeycapTextLift 調的是連續值，病是離散的。
	 * 正解＝字畫進帽裡、整顆帽當一張圖重取樣（PEAK／Kenney Input Prompts 的做法）：上下邊緣的
	 * 半像素灰一樣深 ⇒ 對稱是構造保證。貼圖與尺寸表由 Tools/AssetPrep/keycap_texture.py 生成
	 *（NiKeycapData）；表裡沒有的鍵名退回 9-slice＋Slate 排字。
	 *
	 * 回傳的貼圖**沒有人保 GC**——呼叫端要存進 UPROPERTY（HUD 的 KeyTexCache）。
	 * 只有一態：不可按＝同一張圖 × NiUi::KeycapDimAlpha（十一修）。
	 */
	class UTexture2D* LoadKeycapTex(const FString& Key);

	/** 該貼圖的 brush（UV 只取帽那一段、ImageSize＝盒的設計尺寸）；依貼圖快取。 */
	const FSlateBrush* KeycapImageBrush(class UTexture* Tex, const NiKeycapData::FEntry& E);

	/**
	 * 把「墨跡到墨跡」的距離換算成 Slate 的 slot padding。
	 *
	 * 這一函式就是這次搬遷的全部理由：行框底下有一截下伸空白、行框頂上有一截上伸空白，
	 * 兩段都不是墨。以前我在每個呼叫點各自處理（然後忘記），現在只有這裡處理一次。
	 *
	 * @param Above     上面那行的字體（全大寫 ⇒ 墨跡底＝基線）
	 * @param Below     下面那行的字體
	 * @param InkGap    你要的墨跡間距（設計單位）
	 * @param bBelowIsDisplay  下面那行是展示體（用實量的大寫比例）；否則視為非文字（padding 直接是 InkGap 扣上方溢出）
	 */
	float InkGapPadding(const FSlateFontInfo& Above, const FSlateFontInfo& Below, float InkGap, bool bBelowIsDisplay);

	/** 文字下方接一個非文字元件（鍵帽、圖示）時的 padding：只要扣掉上面那行的下伸空白。 */
	float InkGapPaddingToBox(const FSlateFontInfo& Above, float InkGap);

	/**
	 * 貼圖 → Slate brush（快取；brush 必須活得比 widget 久，不能在堆疊上建）。
	 * 貼圖本身由 HUD 的 UPROPERTY 保 GC，這裡只借指標。
	 */
	const FSlateBrush* Brush(UTexture* Tex, float Size);

	/** 臉像專用：bCrop 時套用與 canvas 端同一組版面 UV（(0.30,0.22)+(0.40,0.40)）。 */
	const FSlateBrush* FaceBrush(UTexture* Tex, float Size, bool bCrop);

	/**
	 * 依實際字寬截斷並補省略號。
	 * `STextBlock` 的 `OverflowPolicy::Ellipsis` 在這裡沒有生效（實拍：名字仍以自然寬度置中、
	 * 兩端被切、讀成「e_desktop-3C」）⇒ 不再依賴那個旗標，直接問字型量測服務。
	 * 這是**量文字的推進寬度**（量測服務的本職），不是我又跑回去手算版面座標。
	 */
	FString Elide(const FString& Text, const FSlateFontInfo& Font, float MaxWidth);
}

/**
 * 局內 HUD 的 Slate 根節點。整個遊戲畫面的 UI 都掛在這底下，一個相位一層。
 * 預設整棵樹 HitTestInvisible——會吃掉作畫的滑鼠與鍵盤是這條路最容易犯的錯。
 */
class SNiHudRoot : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiHudRoot) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ANiceInkHUD>, Hud)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	/** chrome 是否該出現（開場動畫／系統選單／臉同步等待時全部讓開） */
	EVisibility ChromeVis() const;
	/** 右上現金：大廳與睜眼受害者不顯示 */
	EVisibility CashVis() const;
	/** 大廳限定的層 */
	EVisibility LobbyVis() const;
	/** 指認：只有受害者本人看得到那一排臉 */
	EVisibility AccuseVis() const;
	/** 場間大廳 */
	EVisibility PostGameVis() const;
	/** 墨杯 chip（入鎖作畫時） */
	EVisibility InkChipVis() const;
	/** 醉夢（沉睡者本人） */
	EVisibility DreamVis() const;
	/** 判決／結局橫幅 */
	EVisibility BannerVis() const;
	/** ESC 選單：唯一吃輸入的一層 */
	EVisibility SysMenuVis() const;

	TSharedPtr<class SNiSystemMenu> SysMenu;

	TWeakObjectPtr<ANiceInkHUD> Hud;
};
