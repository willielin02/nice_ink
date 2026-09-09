#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Fonts/SlateFontInfo.h"

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
