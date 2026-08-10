#pragma once

#include "CoreMinimal.h"
#include "NiceInkLocText.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SCompoundWidget.h"

class APlayerController;
class UNiceInkGameInstance;
class UNiceInkSessionSubsystem;
class SEditableTextBox;
class SVerticalBox;

// 主選單 Slate 本體（2026-08-06 白卡制：素色半透明＝毛玻璃白卡＋墨字；
// user 定案 Meccha/Schedule I 式）。全 C++ 直寫、零 UMG/Blueprint 資產——
// 「無編輯器資產」原則不變，換掉的是 canvas 的手擺排版。
// 頁面切換＝Visibility lambda 輪詢 Page 列舉（與全專案輪詢哲學同款）。
class SNiMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiMenu) {}
		SLATE_ARGUMENT(TWeakObjectPtr<APlayerController>, OwnerPC)
		SLATE_ARGUMENT(TWeakObjectPtr<class UFont>, Font)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	// robo 鉤子（NiMenuShowJoin/NiMenuJoinCode）：切加入頁＋預填房間碼
	void OpenJoinPage(const FString& PrefillCode);

	// robo 鉤子（NiMenuFontSample）：標題下顯示多文字系統取樣行（字體矩陣驗證）
	void ShowFontSample(const FString& Sample) { FontSample = Sample; }

	// 換語言後重建用：直接開在 settings 頁
	void OpenSettingsPage()
	{
		Page = EPage::Settings;
		bSettingsSeeded = false;
	}

	// robo 鉤子（NiMenuShowLang）：開語言全列頁
	void OpenLanguagePage() { Page = EPage::Language; }

	// 開個人檔案頁（robo 鉤子 NiMenuShowProfile 與主選單按鈕共用；含名字欄播種）
	void OpenProfilePage();

private:
	enum class EPage : uint8 { Root, Join, Settings, Credits, Language, Profile };
	EPage Page = EPage::Root;

	TWeakObjectPtr<APlayerController> OwnerPC;

	// --- 狀態 ---
	FString CodeBuffer;          // 房間碼輸入（4 字母）
	bool bPublicRoom = false;    // invite only（預設）／public
	bool bSearchKicked = false;  // 進 join 頁自動搜一次
	FString ErrorBanner;         // 斷線原因（host/join 動作時清除）
	bool bFaceGateNudge = false; // 臉制閘門：無臉按 Host/Join＝導個人檔案頁＋亮提示
	FString FontSample;          // 字體取樣行（robo 驗證；空=不顯示）
	double NextListRebuildTime = 0.0;
	int32 LastListStamp = -1;    // 房列表重建判定（數量+首名雜湊）

	// settings 暫存（apply 才動引擎）
	int32 PendingWindowMode = 0;
	int32 PendingResIndex = 2;
	bool bSettingsSeeded = false;

	// --- 樣式（brush/style 必須比 widget 長壽＝成員持有）---
	FSlateBrush CardBrush, SlotBrush, RuleBrush, DividerBrush, UnderlineBrush;
	FSlateBrush ChipOnBrush, ChipOffBrush;
	FButtonStyle PrimaryStyle, OnCardStyle, GhostStyle, RowStyle;
	FEditableTextBoxStyle NameBoxStyle;

	TWeakObjectPtr<class UFont> MenuFont; // MenuHUD 持有 GC（複合 UFont）
	// 字體系統：圓體=輔助（標籤/欄位/工具字）、明朝=標題與動作（墨字性格）
	FSlateFontInfo Font(float Size, bool bBold = false) const;              // M PLUS Rounded
	FSlateFontInfo Serif(float Size, bool bBlack = false, int32 Tracking = 0) const; // Zen Old Mincho
	FSlateFontInfo Label(float Size) const;                                  // 小標籤（圓體+寬字距）

	// --- 動態子區 ---
	TSharedPtr<SEditableTextBox> NameBox;
	TSharedPtr<SVerticalBox> RoomListBox;
	TSharedPtr<SHorizontalBox> FaceRowBox;              // 臉庫列（個人檔案頁）
	TArray<TSharedPtr<FSlateBrush>> FaceThumbBrushes;   // 縮圖 brush（比 widget 長壽）
	int32 LastFaceRowRev = -1;                          // FaceRevision 變動＝重建臉庫列
	void RefreshFaceRow();

	// --- helpers ---
	FText Loc(ENiLocKey Key) const;   // 目前語言字串（FText）
	FString LocS(ENiLocKey Key) const;
	void CycleLanguage(int32 Delta);  // 換語言＋通知 HUD 重建
	UNiceInkGameInstance* GI() const;
	UNiceInkSessionSubsystem* Sessions() const;
	bool IsLan() const;
	bool IsBusy() const;
	void CommitName();
	void RebuildRoomList();
	FText StatusText() const;
	FSlateColor StatusColor() const;

	TSharedRef<SWidget> BuildRootPage();
	TSharedRef<SWidget> BuildJoinPage();
	TSharedRef<SWidget> BuildSettingsPage();
	TSharedRef<SWidget> BuildCreditsPage();
	TSharedRef<SWidget> BuildLanguagePage(); // 13 語母語名全列網格（「文A」鈕入口）
	TSharedRef<SWidget> BuildProfilePage();  // 個人檔案（SPEC #52 v4.0e：名字＋自拍＋現金）
	class UNiceInkPersonaSubsystem* Persona() const;
	bool HasFace() const;                    // 臉制閘門（2026-08-10：無臉不開玩）
	void PickSelfieAndIntake();              // 檔案對話框→自拍管線
	TSharedRef<SWidget> MakeGhostButton(const FString& Label, TFunction<void()> OnClick);
	TSharedRef<SWidget> MakeChip(const FString& Label, bool bPublicValue);
	EVisibility PageVis(EPage P) const { return Page == P ? EVisibility::Visible : EVisibility::Collapsed; }
};
