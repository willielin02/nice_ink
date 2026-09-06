#pragma once

#include "CoreMinimal.h"
#include "NiceInkLocText.h"
#include "NiceInkUiTokens.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SCompoundWidget.h"

class APlayerController;
class UNiceInkGameInstance;
class UNiceInkSessionSubsystem;
class SEditableTextBox;
class SVerticalBox;
class SWrapBox;

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
	// 加入頁點空白處＝把鍵盤焦點抓回來（房號輸入靠 OnKeyDown，焦點丟了打字無聲失效）
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

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

	// robo 鉤子（NiMenuShowHost）：開開房設定頁
	void OpenHostPage() { Page = EPage::Host; }

	// 開個人檔案頁（robo 鉤子 NiMenuShowProfile 與主選單按鈕共用；含名字欄播種）
	void OpenProfilePage();

private:
	enum class EPage : uint8 { Root, Host, Join, Settings, Credits, Language, Profile };
	EPage Page = EPage::Root;

	TWeakObjectPtr<APlayerController> OwnerPC;

	// --- 狀態 ---
	FString CodeBuffer;          // 房間碼輸入（4 字母）
	bool bPublicRoom = false;    // invite only（預設）／public
	int32 HostMaxPlayers = 6;    // 房間人數（4~6；2026-08-14 定案：房主直接決定這房幾個人）
	int32 HostLangIndex = INDEX_NONE; // 公開房語言（INDEX_NONE=跟介面語言；只在公開時顯示）
	FString HostRoomName;        // 公開房房名（徵人啟事；可空；只在公開時顯示）
	bool bSearchKicked = false;  // 進 join 頁自動搜一次
	double NextAutoSearchTime = 0.0; // 列表自動更新（12s；打房號中不干擾）
	int32 JoinLangFilter = INDEX_NONE; // 列表語言過濾（-1=全部；Construct 播種=我的語言）
	bool bJoinLangOpen = false;  // 語言選擇器展開中
	FString ErrorBanner;         // 斷線原因（host/join 動作時清除）
	// 首啟創角（2026-08-12 首啟導流）：開機無臉＝落在「創建你的力士」頁、
	// 無返回無 ESC（強制上傳制的流程化——主選單的門檻提示字全數退役）；
	// 臉一到手自動進主選單（你的力士戴你的臉跳舞＝第一印象）
	bool bOnboarding = false;
	FString FontSample;          // 字體取樣行（robo 驗證；空=不顯示）
	double NextListRebuildTime = 0.0;
	int32 LastListStamp = -1;    // 房列表重建判定（數量+首名雜湊）

	// --- 2026-08-10 選單邏輯修（20 條驗收單）---
	EPage LangOrigin = EPage::Root;  // 語言頁從哪進（Root/Settings）＝返回與換語言後回到哪
	double QuitArmedUntil = 0.0;     // 離開二段確認：第一擊武裝 3 秒
	double NameSavedUntil = 0.0;     // 名字「已儲存」回饋顯示到此刻

	// --- 2026-08-11 視窗模式簡化制（user 定案）：無邊框/視窗二態即點即切、
	// 視窗＝瀏覽器式可拖拉（引擎原生）；獨占全螢幕與解析度選單退役——
	// 黑閃/去彈跳/對賬回滾的存在理由整類消滅 ---

	// settings 顯示值（1=無邊框、2=視窗；點擊即套用即存檔）
	int32 PendingWindowMode = 1;
	bool bSettingsSeeded = false;

	// --- 樣式（brush/style 必須比 widget 長壽＝成員持有）---
	FSlateBrush CardBrush, SlotBrush, RuleBrush, DividerBrush, UnderlineBrush;
	// 頁面的「地」（2026-09-05 無卡片制）：全透明——版面樹保持原狀，只是底不畫了
	FSlateBrush PageGroundBrush;
	// 鍵帽底（與局內 canvas 的 DrawKeycap 同一個形狀語言：淺底、小圓角）
	FSlateBrush KeycapBrush;
	FSlateBrush ChipOnBrush, ChipOffBrush;
	FSlateBrush InsetBrush; // 卡內分組框（歸屬用「裝在同個盒子」表達，不靠間距）
	FSlateBrush CardDividerBrush; // 卡上細分隔線（日常個人檔案頁：資產區/上傳區分家）
	FButtonStyle PrimaryStyle, OnCardStyle, GhostStyle, RowStyle;
	FButtonStyle TextStyle;     // 文字鈕（中性制：無底無框、hover 轉強調色）
	FSlateBrush AccentBarBrush; // 文字鈕 hover 的左側短棒
	FSlateBrush SegmentBrush;   // 分段控制的外框容器（六修）
	FButtonStyle DangerStyle;   // 毀滅性動作（Quit）＝ghost 但帶紅（2026-09-04 全站稽核）
	FButtonStyle FaceTileStyle; // 臉庫縮圖鈕：平常無底（icon=頭形不能再被方塊裱起來）、hover 微亮
	FEditableTextBoxStyle NameBoxStyle;

	TWeakObjectPtr<class UFont> MenuFont; // MenuHUD 持有 GC（複合 UFont）
	// 字體＝角色表制（NiType；2026-08-11 樣式源統一）：禁填裸字級——
	// 每行字引用一個角色，改字級只准改 NiceInkUiTokens.h 的表
	FSlateFontInfo Ty(const NiType::FRole& Role, bool bBold = false) const;

	// --- 動態子區 ---
	TSharedPtr<SEditableTextBox> NameBox;
	TSharedPtr<SVerticalBox> RoomListBox;
	TSharedPtr<SWrapBox> FaceRowBox;                    // 臉庫列（個人檔案頁；wrap＝放大後自動換行）
	TArray<TSharedPtr<FSlateBrush>> FaceThumbBrushes;   // 縮圖 brush（比 widget 長壽）
	int32 LastFaceRowRev = -1;                          // FaceRevision 變動＝重建臉庫列
	bool bFaceRowPending = false;                       // 有縮圖還沒烘出來（頭像亭暖機中）＝稍後重試
	double NextFaceRowRetry = 0.0;
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
	void ToggleWindowMode();       // 無邊框↔視窗（進視窗給桌面 70% 初始大小、其後用拖的）

	// 效能列（2026-08-25 幀率上限制）：正本在引擎 GameUserSettings，與視窗模式
	// 同一責任邊界＝設定頁直接讀寫它、即點即套即存
	FString FrameLimitValueText() const;
	void StepFrameLimit(int32 Dir);
	void ToggleVSync();
	void MarkPerfDefaultsTouched(); // 玩家動過＝一次性預設從此不再介入
	TSharedRef<SWidget> MakeStatusRow(); // 狀態行＋進行中取消鈕（Root/Join 共用）

	TSharedRef<SWidget> BuildRootPage();
	TSharedRef<SWidget> BuildHostPage(); // 開房設定步（可見性二選＋確認；與加入頁對稱）
	TSharedRef<SWidget> BuildJoinPage();
	TSharedRef<SWidget> BuildSettingsPage();
	TSharedRef<SWidget> BuildCreditsPage();
	TSharedRef<SWidget> BuildLanguagePage(); // 13 語母語名全列網格（「文A」鈕入口）
	TSharedRef<SWidget> BuildProfilePage();  // 個人檔案（SPEC #52 v4.0e：名字＋自拍＋現金）
	// 個人檔案卡的四塊積木（創角/日常兩模式各自組裝＝順序跟任務走）
	TSharedRef<SWidget> MakeProfileFacesBlock();
	TSharedRef<SWidget> MakeProfileNameBlock();
	TSharedRef<SWidget> MakeProfileCashBlock();
	TSharedRef<SWidget> MakeProfileUploadBlock();
	class UNiceInkPersonaSubsystem* Persona() const;
	bool HasFace() const;                    // 臉制閘門（2026-08-10：無臉不開玩）
	void PickSelfieAndIntake();              // 檔案對話框→自拍管線
	TSharedRef<SWidget> MakeGhostButton(const FString& Label, TFunction<void()> OnClick);
	// 文字鈕（2026-09-05 中性制的主要控制項）：無底無框，hover＝強調色文字＋左側短棒
	TSharedRef<SWidget> MakeTextButton(const TAttribute<FText>& Label, const NiType::FRole& Role,
		TFunction<void()> OnClick, TFunction<bool()> Enabled = nullptr, bool bDanger = false);
	TSharedRef<SWidget> MakeTextButton(const FText& Label, const NiType::FRole& Role,
		TFunction<void()> OnClick, TFunction<bool()> Enabled = nullptr, bool bDanger = false);
	TSharedRef<SWidget> MakeRootLangRow();   // 首頁欄底 13 語 chips（點選即套用）
	// 換頁淡入：頁根登記（Construct）＋ Tick 設 RenderOpacity
	TSharedPtr<SWidget> PageRoots[7];
	EPage LastPageSeen = EPage::Root;
	double PageChangedAt = -1.0;
	TSharedRef<SWidget> RegisterPage(EPage P, TSharedRef<SWidget> W)
	{
		PageRoots[static_cast<int32>(P)] = W;
		return W;
	}
	// 鍵帽＋動詞（2026-09-05）：選單也講局內那套操作語言——此前選單零鍵帽，
	// 兩個載體讀起來不像同一個產品。
	TSharedRef<SWidget> MakeKeycapHint(const FString& Key, const FString& Label);
	// 返回的唯一實作（ESC 與左下那一列共用；頁面內的 Back 鈕已拆）
	bool CanGoBack() const;
	void GoBack();
	TSharedRef<SWidget> MakeChip(const FString& Label, bool bPublicValue);
	TSharedRef<SWidget> MakeMaxPlayersRow(); // 建房頁人數 2~6 chips
	TSharedRef<SWidget> MakeHostLangRow();   // 建房頁公開房語言 chips（13 語 wrap）
	TSharedRef<SWidget> MakeJoinLangRow();   // 加入頁列表語言過濾 chips（全部+13 語 wrap）
	EVisibility PageVis(EPage P) const { return Page == P ? EVisibility::Visible : EVisibility::Collapsed; }
};
