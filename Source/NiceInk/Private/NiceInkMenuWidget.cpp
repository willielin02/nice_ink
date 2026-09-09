#include "NiceInkMenuWidget.h"
#include "Engine/Texture2D.h"

#include "NiceInkLocText.h"
#include "NiceInkMenuHUD.h"
#include "Engine/Font.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GenericPlatform/GenericApplication.h" // FDisplayMetrics（視窗模式初始大小）
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "NiceInkGameInstance.h"
#include "NiceInkPersonaSubsystem.h"
#include "NiceInkSaveGame.h"
#include "NiceInkSessionSubsystem.h"
#include "NiceInkUiTokens.h"
#if PLATFORM_WINDOWS
// 現代檔案對話框（IFileOpenDialog，Vista+）：引擎 DesktopPlatform 走
// GetOpenFileNameW 古典模板＝高 DPI 下被點陣放大（2026-08-07 user 抓「畫質低」）
// ——自接 COM，順帶 Shipping 也能用（不再依賴 Developer 模組）
#include "Windows/AllowWindowsPlatformTypes.h"
#include <shobjidl.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBackgroundBlur.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{

	FSlateBrush MakeRounded(const FLinearColor& Tint, float Radius)
	{
		FSlateBrush B;
		B.DrawAs = ESlateBrushDrawType::RoundedBox;
		B.TintColor = Tint;
		B.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		B.OutlineSettings.CornerRadii = FVector4(Radius, Radius, Radius, Radius);
		B.OutlineSettings.Width = 0.0f;
		return B;
	}

	FButtonStyle MakeButtonStyle(const FLinearColor& Normal, const FLinearColor& Hover,
		const FLinearColor& Pressed, float Radius)
	{
		FButtonStyle S;
		S.SetNormal(MakeRounded(Normal, Radius));
		S.SetHovered(MakeRounded(Hover, Radius));
		S.SetPressed(MakeRounded(Pressed, Radius));
		S.SetNormalPadding(FMargin(0));
		S.SetPressedPadding(FMargin(0));
		return S;
	}

	// 外框控制項（2026-09-05）：**外框承載強調色，填色保持中性**。
	// 這是從 Meccha 選單實機截圖抄來的規則（他們是深底＋綠框），值換成我們的酒金。
	// 好處不只是像他們：實心色塊在黑地上是畫面裡最亮的一大片，會把視線從
	// 背景那隻跳舞的力士身上整個搶走；外框只畫邊界，中間讓場景透出來。
	FSlateBrush MakeOutlinedBrush(const FLinearColor& Fill, const FLinearColor& Outline,
		float Width, float Radius)
	{
		FSlateBrush B = MakeRounded(Fill, Radius);
		B.OutlineSettings.Color = FSlateColor(Outline);
		B.OutlineSettings.Width = Width;
		return B;
	}

	FButtonStyle MakeOutlinedStyle(const FLinearColor& Normal, const FLinearColor& Hover,
		const FLinearColor& Pressed, const FLinearColor& Outline, float Width, float Radius)
	{
		FButtonStyle S;
		S.SetNormal(MakeOutlinedBrush(Normal, Outline, Width, Radius));
		S.SetHovered(MakeOutlinedBrush(Hover, Outline, Width, Radius));
		S.SetPressed(MakeOutlinedBrush(Pressed, Outline, Width, Radius));
		S.SetNormalPadding(FMargin(0));
		S.SetPressedPadding(FMargin(0));
		return S;
	}

	FLinearColor WithA(FLinearColor C, float A) { C.A = A; return C; }

	// 全站文字陰影（2026-09-06）：六款參照的每一個字都有陰影或描邊，無例外；
	// 選單此前一個都沒有＝字直接躺在世界上、換個背景就隱形。掛在每個 STextBlock 上。
	const FVector2D NiTextShadowOffset(1.0f, 1.0f);
	const FLinearColor NiTextShadowColor(0.0f, 0.0f, 0.0f, 0.55f);

	// 白卡制的**一個開關**（2026-09-05）：Meccha 的選單完全沒有卡片，控制項直接浮在
	// 即時場景上；我們的白卡此前正好把背景那隻跳舞的力士切開一半（09-05 設定頁
	// 實機截圖可證）。拆的方式是把卡片的底換成全透明、模糊強度歸零——
	// **樹的結構一個字都沒動** ⇒ user 若要退回毛玻璃白卡，改這兩個常數就回去了，
	// 不必再動七個頁面的版面。
	constexpr float NiMenuCardBlur = 0.0f;   // 毛玻璃強度（原 14）
	// 卡內距（原 NiSpace::CardPadH/V = 32/28）。**內距是給「卡的邊」用的**——
	// 沒有卡就沒有邊要閃，留著只會讓按鈕比標題多縮 32px（實測左緣 106 vs 75，
	// user viewport 打回「這是什麼排版？」）。退回白卡制時與 NiMenuCardBlur 一起改回。
	constexpr float NiMenuGroundPad = 0.0f;
	// 底帶要替左下的 [ESC] Back 讓位：那一列是**頁框的 chrome**、疊在每一頁上，
	// 而各頁的底帶此前只留 BandBottom ⇒ 設定頁的 Licenses 與它疊在一起。
	// 56 = ESC 列高(~40) + GapL(16)，與 BandBottom 同階。
	constexpr float NiMenuBottomReserve = 56.0f;

	// 首頁左緣（2026-09-05）：Meccha 的主選單堆疊靠左，讓出中央給即時場景。
	// 我們的場景是跳舞的力士（畫面中央偏右）——置中的白卡此前正好把他切一半。
	// 72 = NiSpace::L(24) × 3：落在既有網格上，不是隨手數字。
	constexpr float NiMenuLeftGutter = 72.0f;
	// 文字鈕（MakeTextButton）的左緣：短棒 3px ＋ 間距 M 落在欄外，文字本體與標題同軸
	constexpr float NiMenuTextBtnGutter = NiMenuLeftGutter - 3.0f - NiSpace::M;
}

FText SNiMenu::Loc(ENiLocKey Key) const
{
	return FText::FromString(NiLoc::T(OwnerPC.Get(), Key));
}

FText SNiMenu::Up(const FText& In)
{
	// 展示體＝全大寫；CJK／阿拉伯 ToUpper 是恆等
	return FText::FromString(In.ToString().ToUpper());
}

FString SNiMenu::LocS(ENiLocKey Key) const
{
	return NiLoc::T(OwnerPC.Get(), Key);
}

void SNiMenu::CycleLanguage(int32 Delta)
{
	UNiceInkGameInstance* Inst = GI();
	if (!Inst)
	{
		return;
	}
	Inst->ApplyLanguage((Inst->GetMenuLanguage() + Delta + NiLoc::NumLangs) % NiLoc::NumLangs);
	// 靜態文字全在 Construct 定死＝換語言用重建（延後一 tick：不在自己的
	// 點擊回呼裡拆自己）
	if (OwnerPC.IsValid())
	{
		if (ANiceInkMenuHUD* Hud = Cast<ANiceInkMenuHUD>(OwnerPC->GetHUD()))
		{
			Hud->RecreateMenu(/*bOpenSettings=*/true);
		}
	}
}

FSlateFontInfo SNiMenu::Ty(const NiType::FRole& Role, bool bBold) const
{
	// 角色→字面：明朝體（Serif/SerifBlack）＝標題與動作；圓體（Regular/Bold）
	// ＝正文與工具字。字級/字距只活在 NiType 表——這裡永不出現裸數字
	const FName Face = Role.bSerif
		? (Role.bBlack ? FName("SerifBlack") : FName("Serif"))
		: ((Role.bBold || bBold) ? FName("Bold") : FName("Regular"));
	FSlateFontInfo Info(MenuFont.Get(), Role.Size, Face);
	Info.LetterSpacing = Role.Tracking;
	return Info;
}

UNiceInkGameInstance* SNiMenu::GI() const
{
	return OwnerPC.IsValid() ? Cast<UNiceInkGameInstance>(OwnerPC->GetGameInstance()) : nullptr;
}

UNiceInkSessionSubsystem* SNiMenu::Sessions() const
{
	UNiceInkGameInstance* Inst = GI();
	return Inst ? Inst->GetSubsystem<UNiceInkSessionSubsystem>() : nullptr;
}

bool SNiMenu::IsLan() const
{
	return !UNiceInkSessionSubsystem::IsOnlineServiceConfigured();
}

bool SNiMenu::IsHostJoinLocked() const
{
	// 主頁兩顆主動詞只被**真正互斥**的動作鎖住：開房中、加入中。搜房不算——搜房中按 HOST
	// 沒有任何衝突（此前三種忙碌一視同仁，搜房那 5 秒把主動詞一起壓暗）。
	const UNiceInkSessionSubsystem* S = Sessions();
	if (!S)
	{
		return false;
	}
	const ENiSessionUiState St = S->GetUiState();
	return St == ENiSessionUiState::Hosting || St == ENiSessionUiState::Joining;
}

bool SNiMenu::IsBusy() const
{
	const UNiceInkSessionSubsystem* S = Sessions();
	if (!S)
	{
		return false;
	}
	const ENiSessionUiState St = S->GetUiState();
	if (St == ENiSessionUiState::Searching && S->IsBackgroundSearching())
	{
		return false; // 背景自動更新＝UI 靜音（按鈕不變灰、狀態列不講話）
	}
	return St == ENiSessionUiState::Hosting || St == ENiSessionUiState::Searching ||
		St == ENiSessionUiState::Joining;
}

void SNiMenu::CommitName()
{
	UNiceInkGameInstance* Inst = GI();
	if (!Inst || !NameBox.IsValid())
	{
		return;
	}
	// 只有「玩家真的改了字」才落檔成自訂名——輸入框顯示的平台/保底名原樣按下
	// Enter 不算自訂（名字繼續跟平台走）
	const FString Clean = UNiceInkGameInstance::SanitizePlayerName(NameBox->GetText().ToString());
	if (!Clean.IsEmpty() && Clean != Inst->GetEffectiveDisplayName())
	{
		Inst->PlayerDisplayName = Clean;
		Inst->SaveSettings();
		NameSavedUntil = FPlatformTime::Seconds() + 2.0; // 存了要說（無聲儲存修）
	}
	// 呈現規則：自訂名＝實值；無自訂名＝欄位留空、保底/平台名走 hint（淡字）——
	// 隨機名不再偽裝成「你已取好的名字」
	NameBox->SetText(Inst->PlayerDisplayName.IsEmpty()
		? FText::GetEmpty() : FText::FromString(Inst->GetEffectiveDisplayName()));
}

void SNiMenu::OpenJoinPage(const FString& PrefillCode)
{
	Page = EPage::Join;
	CodeBuffer = PrefillCode.TrimStartAndEnd().ToUpper().Left(4);
	bSearchKicked = false;
	FSlateApplication::Get().SetAllUserFocus(AsShared());
}

void SNiMenu::Construct(const FArguments& InArgs)
{
	OwnerPC = InArgs._OwnerPC;
	MenuFont = InArgs._Font;
	if (UTexture2D* Logo = InArgs._LogoTex.Get())
	{
		LogoBrush.SetResourceObject(Logo);
		LogoBrush.DrawAs = ESlateBrushDrawType::Image;
		// 長寬比是常數，不讀貼圖：未 cook 的 -game 下貼圖在建構當幀還是 32×32 的佔位
		// （非同步編譯），GetSizeX／GetSurfaceWidth 都回 32 ⇒ 標誌被拉成正方形（實錄 2026-09-06）。
		// 來源＝Tools/AssetPrep/make_logo.py 產出 893×303；改標誌就改這個常數。
		constexpr float LogoAspect = 303.0f / 893.0f;
		LogoBrush.ImageSize = FVector2D(893.0f, 303.0f);
		LogoW = 400.0f;                                                // 畫面寬的 21%
		LogoH = LogoW * LogoAspect;
	}

	// AR 版面鏡像（v4.0e）：流向跟文化——SetCurrentCulture("ar") 時 Slate 整樹
	// 自動鏡像（SHorizontalBox 逆序/HAlign 翻面/命中判定跟著走）；換語言重建
	// 選單時以新文化重算。房碼格等 LTR 記號在各自子樹釘回 LeftToRight。
	SetFlowDirectionPreference(EFlowDirectionPreference::Culture);

	if (UNiceInkGameInstance* Inst = GI())
	{
		// 列表語言過濾預設＝我的語言（配對邊界=語言；「全部」是雙語者的顯式出口）
		JoinLangFilter = Inst->GetMenuLanguage();
		ErrorBanner = Inst->ConsumeDisconnectReason();
	}

	// 靜默登入（persistentauth-only、絕不彈瀏覽器）：進房前把雲端 persona
	// （名字/偏好/現金/刺青）拉下來給個人檔案頁與舞台力士
	if (UNiceInkSessionSubsystem* S = Sessions())
	{
		S->TrySilentLogin();
	}

	// 獨占全螢幕退役（2026-08-11）：舊 ini 殘留的 Fullscreen 一律遷移到無邊框
	if (UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		if (GUS->GetFullscreenMode() == EWindowMode::Fullscreen)
		{
			GUS->SetFullscreenMode(EWindowMode::WindowedFullscreen);
			GUS->ApplySettings(false);
			GUS->SaveSettings();
		}
	}

	// --- 樣式庫（2026-09-05 中性制：白／黑／一個強調色；無卡片；圓角一個值）---
	// user 定案作廢 08-06 白卡明朝制：「我其實只是想要一個成熟、適合的 UIUX 系統……
	// 中性、簡約、通用就足夠了，讓遊戲內容站出來」。原則寫在 NiceInkUiTokens.h。
	const float R = NiSpace::Radius;
	CardBrush = MakeRounded(WithA(NiHudColor::White, 0.0f), R);
	PageGroundBrush = MakeRounded(WithA(NiHudColor::White, 0.0f), R);
	KeycapBrush = MakeOutlinedBrush(WithA(NiHudColor::Black, 0.70f), WithA(NiHudColor::White, 0.85f), 1.0f, R);   // 與局內 DrawKeycap 同形
	SlotBrush = MakeOutlinedBrush(WithA(NiHudColor::White, 0.05f), WithA(NiHudColor::White, 0.30f), 1.0f, R);   // 房碼格：1px 外框（10% 白底在深地上看不見）
	RuleBrush = MakeRounded(NiHudColor::AccentText, 1.0f);
	DividerBrush = MakeRounded(WithA(NiHudColor::White, 0.14f), 1.0f);
	UnderlineBrush = MakeRounded(NiHudColor::AccentText, 1.0f);
	AccentBarBrush = MakeRounded(NiHudColor::White, 1.0f);        // 顏色由 SImage 的 tint 給

	// 主動作＝強調色實填＋白字（一頁只有一顆）
	// 主鈕（六修）：金框金字，hover 才微填——金當大面積填色是「工程師 UI」最強的訊號，
	// Liar's Bar 的金只用在字與線。
	// 主鈕（2026-09-06 七修）：**實心白＋黑字**。六款參照的主動作全部實心（PEAK START、
	// Among Us CREATE、Liar's READY），線框只給次要動作；1px 金框＝Bootstrap 的 btn-outline。
	PrimaryStyle = MakeButtonStyle(WithA(NiHudColor::White, 0.92f), NiHudColor::White,
		WithA(NiHudColor::White, 0.78f), R);
	// 停用＝底降到 35%（Slate 預設的停用是整顆半透明疊灰＝糊成一塊）；黑字照舊
	PrimaryStyle.SetDisabled(MakeRounded(WithA(NiHudColor::White, 0.35f), R));
	// 分段控制的容器：1px 白 22% 外框，把幾個 chip 收成一個控制項
	SegmentBrush = MakeOutlinedBrush(WithA(NiHudColor::White, 0.04f), WithA(NiHudColor::White, 0.22f), 1.0f, R);
	// 地上的次要控制項（設定頁箭頭、列表 chip）：白 7% 微填
	OnCardStyle = MakeButtonStyle(WithA(NiHudColor::White, 0.07f), WithA(NiHudColor::White, 0.16f),
		WithA(NiHudColor::White, 0.24f), R);
	// 導覽／輔助動作：無底、細白框
	// 次鈕：白 14% 底、無框（線框退役——一頁三種框就是沒有元件系統）
	GhostStyle = MakeButtonStyle(WithA(NiHudColor::White, 0.14f), WithA(NiHudColor::White, 0.22f),
		WithA(NiHudColor::White, 0.30f), R);
	// 文字鈕（首頁動作堆疊）：無底無框，hover＝文字轉強調色＋左側短棒（MakeTextButton）
	TextStyle = MakeButtonStyle(WithA(NiHudColor::White, 0.0f), WithA(NiHudColor::White, 0.0f),
		WithA(NiHudColor::White, 0.06f), R);
	// 毀滅性動作（Quit）：紅外框
	DangerStyle = MakeOutlinedStyle(WithA(NiHudColor::White, 0.0f), WithA(NiHudColor::Red, 0.18f),
		WithA(NiHudColor::Red, 0.30f), WithA(NiHudColor::Red, 0.75f), 1.0f, R);
	// 清單列（公開房列表）
	RowStyle = MakeOutlinedStyle(WithA(NiHudColor::White, 0.05f), WithA(NiHudColor::White, 0.12f),
		WithA(NiHudColor::White, 0.20f), WithA(NiHudColor::White, 0.18f), 1.0f, R);
	// 臉庫縮圖：平常無底（頭形 icon 不裱框）、hover 微亮
	FaceTileStyle = MakeButtonStyle(WithA(NiHudColor::White, 0.0f), WithA(NiHudColor::White, 0.10f),
		WithA(NiHudColor::White, 0.18f), R);
	// chip：選中＝強調色實填＋白字；未選＝白 8% 微填＋白 70% 字
	ChipOnBrush = MakeRounded(WithA(NiHudColor::White, 0.14f), R);   // 選中＝微亮底＋金字（見 chip 文字色）
	ChipOffBrush = MakeRounded(WithA(NiHudColor::White, 0.0f), R);   // 未選＝無底，只有字
	InsetBrush = MakeRounded(WithA(NiHudColor::White, 0.05f), R);
	// 模態面板材料（三批）：黑 62% 圓角 8——設定這種「清單」要有一個容器才讀成一個物件
	// （REPO／PEAK 的設定頁都是一塊深色面板；浮字讀成還沒套樣式的表格）。
	PanelBrush = MakeRounded(WithA(NiHudColor::Black, 0.62f), 8.0f);
	CardDividerBrush = MakeRounded(WithA(NiHudColor::White, 0.14f), 1.0f);
	// 首啟創角判定（2026-08-12）：要在建 UI 之前算——個人檔案卡的版面
	// 依模式在建構時分支（創角=任務優先／日常=你的東西在上）
	bOnboarding = !HasFace();

	NameBoxStyle = FEditableTextBoxStyle()
		.SetBackgroundImageNormal(MakeRounded(WithA(NiHudColor::Paper, 0.06f), NiSpace::Radius))
		.SetBackgroundImageHovered(MakeRounded(WithA(NiHudColor::Paper, 0.10f), NiSpace::Radius))
		.SetBackgroundImageFocused(MakeRounded(WithA(NiHudColor::Paper, 0.13f), NiSpace::Radius))
		.SetBackgroundImageReadOnly(MakeRounded(WithA(NiHudColor::Paper, 0.04f), NiSpace::Radius))
		.SetTextStyle(FTextBlockStyle().SetFont(Ty(NiType::Body)).SetColorAndOpacity(NiHudColor::Paper))
		.SetFont(Ty(NiType::Body))
		.SetForegroundColor(NiHudColor::Paper)
		.SetFocusedForegroundColor(NiHudColor::Paper)
		.SetPadding(FMargin(14, 12));

	FString Version;
	GConfig->GetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"), TEXT("ProjectVersion"), Version, GGameIni);

	ChildSlot
	[
		SNew(SOverlay)

		// 七頁疊放、Visibility 輪詢切換；頁根登記給 Tick 做換頁淡入
		+ SOverlay::Slot()[RegisterPage(EPage::Root, BuildRootPage())]
		+ SOverlay::Slot()[RegisterPage(EPage::Host, BuildHostPage())]
		+ SOverlay::Slot()[RegisterPage(EPage::Join, BuildJoinPage())]
		+ SOverlay::Slot()[RegisterPage(EPage::Settings, BuildSettingsPage())]
		+ SOverlay::Slot()[RegisterPage(EPage::Credits, BuildCreditsPage())]
		+ SOverlay::Slot()[RegisterPage(EPage::Language, BuildLanguagePage())]
		+ SOverlay::Slot()[RegisterPage(EPage::Profile, BuildProfilePage())]

		// **[ESC] 返回（左下）**（2026-09-05）：Meccha 的每一個子頁左下角都掛著
		// 這一列，而我們的選單此前**一顆鍵帽都沒有**——局內是鍵帽語言、選單是
		// 純滑鼠語言，兩個載體讀起來不像同一個產品。
		// 這是整份選單對齊裡**單項最便宜、效果最大**的一刀：ESC 本來就已經能返回
		//（OnKeyDown 早就處理了），缺的只是「告訴玩家它存在」。
		// 只在子頁顯示——首頁沒有「上一頁」，留一顆按了不會有事的鍵是 §4.2 的禁忌。
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom)
			.Padding(NiMenuLeftGutter, 0, 0, 28)
		[
			// **可點**：它既是提示也是按鈕（滑鼠玩家不會被鍵盤提示擋在門外），
			// 而頁面內原本那幾顆 Back 已經拆掉——一個意圖只有一個位置。
			SNew(SButton)
				.ButtonStyle(&FaceTileStyle)   // 無底、hover 才微亮＝不與內容爭
				.IsFocusable(false)
				.ContentPadding(FMargin(4, 4))
				.Visibility_Lambda([this]()
					{ return CanGoBack() ? EVisibility::Visible : EVisibility::Collapsed; })
				.OnClicked_Lambda([this]() { GoBack(); return FReply::Handled(); })
			[
				MakeKeycapHint(TEXT("ESC"), LocS(ENiLocKey::Back))
			]
		]

		// 頁尾（右下）：語言選擇器「文A 語言名」＋版本戳——大廠主選單的底：低調、但頁面有底
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(0, 0, NiSpace::L, 28)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton).ButtonStyle(&TextStyle).IsFocusable(false)
					.ContentPadding(FMargin(NiSpace::S, NiSpace::XS))
					.Visibility_Lambda([this]() { return Page == EPage::Root ? EVisibility::Visible : EVisibility::Collapsed; })
					.OnClicked_Lambda([this]() { LangOrigin = EPage::Root; Page = EPage::Language; return FReply::Handled(); })
				[
					SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Note)).ColorAndOpacity(NiHudColor::White70)
						.Text(FText::FromString(FString::Printf(TEXT("\u6587A  %s"),
							*NiLoc::LangNativeName(GI() ? GI()->GetMenuLanguage() : 0))))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(NiSpace::M, 0, 0, 0)
			[
				SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Micro))
					.ColorAndOpacity(NiHudColor::White25)
					.Text(FText::FromString(FString::Printf(TEXT("v%s"), *Version)))
			]
		]
	];

	// 首啟創角：無臉＝開機直進「創建你的力士」頁（走 OpenProfilePage＝
	// 名字欄照常播種）；臉一到手 Tick 自動轉入主選單
	if (bOnboarding)
	{
		OpenProfilePage();
	}
}

bool SNiMenu::CanGoBack() const
{
	// 首啟創角＝強制上傳制，無路可退（語言頁除外，LangOrigin 會把它送回創角頁）
	return Page != EPage::Root && !(bOnboarding && Page == EPage::Profile);
}

void SNiMenu::GoBack()
{
	// **返回只有一個實作**（2026-09-05）：此前這段邏輯只活在 OnKeyDown 裡，
	// 而畫面上的「Back」鈕各自寫 `Page = EPage::Root`——ESC 與滑鼠是兩條路，
	// 授權頁那顆還特地寫成回設定頁。抽成一個之後，左下那一列
	// `[ESC] Back` 同時是提示與按鈕：**一個意圖、一個實作、一個位置**。
	if (!CanGoBack())
	{
		return;
	}
	switch (Page)
	{
	case EPage::Credits:  Page = EPage::Settings; break;
	case EPage::Language: Page = LangOrigin; break;
	case EPage::Profile:  CommitName(); Page = EPage::Root; break;
	case EPage::Join:
		// 根治（2026-09-06）：搜房只服務加入頁，離開它就沒有消費者——立刻取消前景搜尋，
		// 否則 LAN 的 5 秒引擎逾時會讓主頁的 HOST／JOIN 暗上好幾秒、狀態列還寫著 looking for rooms。
		if (UNiceInkSessionSubsystem* S = Sessions())
		{
			if (S->GetUiState() == ENiSessionUiState::Searching && !S->IsBackgroundSearching())
			{
				S->CancelMenuAction();
			}
		}
		Page = EPage::Root; break;
	default:              Page = EPage::Root; break;
	}
}

TSharedRef<SWidget> SNiMenu::MakeKeycapHint(const FString& Key, const FString& Label)
{
	// 鍵帽＋動詞（Slate 版）。形式與局內 canvas 的 DrawKeycap 一致：
	// **淺底圓角＋深色鍵名**，右邊接動詞。鍵名不翻譯（鍵盤上刻的就是那幾個字母），
	// 動詞必須進字串表——這條規則兩個載體共用。
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBorder).BorderImage(&KeycapBrush).Padding(FMargin(12, 4))
			[
				SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Label)).ColorAndOpacity(NiHudColor::White)
					.Text(FText::FromString(Key))
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(NiSpace::S, 0, 0, 0)
		[
			SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Body)).ColorAndOpacity(NiHudColor::Paper)
				.Text(FText::FromString(Label))
		];
}

TSharedRef<SWidget> SNiMenu::MakeGhostButton(const FString& Label, TFunction<void()> OnClick)
{
	return SNew(SButton)
		.ButtonStyle(&GhostStyle)
		.IsFocusable(false)
		.ContentPadding(FMargin(26, 10))
		.OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
		[
			SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::ActionSmall)).ColorAndOpacity(NiHudColor::Paper)
				.Text(FText::FromString(Label))
		];
}

TSharedRef<SWidget> SNiMenu::MakeChip(const FString& Label, bool bPublicValue)
{
	// SBorder 版（SButton 的 style 不是 attribute、換不了選中態）：
	// 選中＝墨底紙字、未選＝薄墨底暗字；點擊直設狀態
	return SNew(SBorder)
		.BorderImage_Lambda([this, bPublicValue]() -> const FSlateBrush*
			{ return bPublicRoom == bPublicValue ? &ChipOnBrush : &ChipOffBrush; })
		.HAlign(HAlign_Center).VAlign(VAlign_Center)
		.Padding(FMargin(0, 8))
		.OnMouseButtonDown_Lambda([this, bPublicValue](const FGeometry&, const FPointerEvent&)
			{ bPublicRoom = bPublicValue; return FReply::Handled(); })
		[
			SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Body))
				.ColorAndOpacity_Lambda([this, bPublicValue]()
					{ return bPublicRoom == bPublicValue ? FSlateColor(NiHudColor::AccentText) : FSlateColor(NiHudColor::PaperDim); })
				.Text(FText::FromString(Label))
		];
}

TSharedRef<SWidget> SNiMenu::BuildRootPage()
{
	// 首頁（2026-09-05 中性制）：**左欄一疊文字、場景佔右**。
	// 玩家在這一面只問「我要開房還是加入」⇒ 兩個主動詞（Heading），其餘降一階
	// （Text、70% 白）。13 語 chip 直接攤在欄底——看不懂當前語言的人不必先找到
	// 一顆「文A」。沒有卡、沒有框、沒有實心色塊：場景（道場裡跳舞的力士）是主角。
	return SNew(SBox).Visibility_Lambda([this]() { return PageVis(EPage::Root); })
	[
		SNew(SVerticalBox)

		// 標誌字（2026-09-06）：貼圖＝Oswald 排字＋墨滴（Tools/AssetPrep/make_logo.py）。
		// 六款參照每一款都有一個佔畫面 15～25% 寬的標誌；沒有標誌的標題讀成佔位文字。
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(NiMenuLeftGutter, NiSpace::BandTop, 0, 0)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SBox).WidthOverride(LogoW).HeightOverride(LogoH)
					.Visibility(LogoBrush.GetResourceObject() ? EVisibility::HitTestInvisible : EVisibility::Collapsed)
				[
					SNew(SImage).Image(&LogoBrush).ColorAndOpacity(NiHudColor::White)
				]
			]
			+ SOverlay::Slot()
			[
				SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Display)).ColorAndOpacity(NiHudColor::White)
					.Visibility(LogoBrush.GetResourceObject() ? EVisibility::Collapsed : EVisibility::HitTestInvisible)
					.Text(FText::FromString(TEXT("NICE INK")))
			]
		]

		// 斷線原因（有才顯示；host/join 時清除）
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(NiMenuLeftGutter, NiSpace::M, 0, 0)
		[
			SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Body)).ColorAndOpacity(NiHudColor::Red)
				.Visibility_Lambda([this]() { return ErrorBanner.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
				.Text_Lambda([this]() { return FText::FromString(ErrorBanner); })
		]

		// 字體取樣行（robo NiMenuFontSample 專用；平時空=隱藏）
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(NiMenuLeftGutter, NiSpace::M, 0, 0)
		[
			SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Value)).ColorAndOpacity(NiHudColor::White)
				.Visibility_Lambda([this]() { return FontSample.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
				.Text_Lambda([this]() { return FText::FromString(FontSample); })
		]

		// 主頁縱向節奏（二批）：動詞欄從畫面高 ~36% 開始（REPO 的欄位置），下方留 64% 給世界；
		// 比例用 FillHeight 給，不寫死像素（解析度一變就歪）。
		+ SVerticalBox::Slot().FillHeight(0.22f)[SNew(SSpacer)]
		// --- 主動詞（兩顆，Heading）---
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(NiMenuTextBtnGutter, 0, 0, 0)
		[
			MakeTextButton(Loc(ENiLocKey::HostARoom), NiType::Action, [this]()
			{
				CommitName();
				ErrorBanner.Reset();
				Page = EPage::Host;
			}, [this]() { return !IsHostJoinLocked(); })
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(NiMenuTextBtnGutter, 0, 0, 0)
		[
			MakeTextButton(Loc(ENiLocKey::JoinARoom), NiType::Action, [this]()
			{
				CommitName();
				ErrorBanner.Reset();
				Page = EPage::Join;
				CodeBuffer.Reset();
				bSearchKicked = false;
				FSlateApplication::Get().SetAllUserFocus(AsShared());
			}, [this]() { return !IsHostJoinLocked(); })
		]
		// 登入預告（線上模式且未登入才亮）
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(NiMenuLeftGutter, 0, 0, 0)
		[
			// 可見性掛在外層、內距放在盒內：Collapsed 時整格（含內距）一起消失，
			// 否則槽的內距會留下一段幽靈間距（實測 Join→Profile 多 7px）
			SNew(SBox).WidthOverride(NiSpace::ColumnW).Padding(FMargin(0, NiSpace::S, 0, 0))
				.Visibility_Lambda([this]()
				{
					const UNiceInkSessionSubsystem* S = Sessions();
					return (!IsLan() && S && !S->IsLoggedIn())
						? EVisibility::Visible : EVisibility::Collapsed;
				})
			[
				SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Note)).ColorAndOpacity(NiHudColor::White70).AutoWrapText(true)
					.Text(Loc(ENiLocKey::SignInBrowserNote))
			]
		]

		// --- 其餘動詞：同尺寸同間距（大廠主選單＝一欄同尺寸的四到六個項目）---
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(NiMenuTextBtnGutter, 0, 0, 0)
		[
			MakeTextButton(Loc(ENiLocKey::Profile), NiType::Action, [this]() { OpenProfilePage(); })
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(NiMenuTextBtnGutter, 0, 0, 0)
		[
			MakeTextButton(Loc(ENiLocKey::SettingsBtn), NiType::Action, [this]()
			{
				bSettingsSeeded = false;
				Page = EPage::Settings;
			})
		]
		// 離開＝毀滅性動作：二段確認（第一擊武裝 3 秒；文字自己會說「確定？」）
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(NiMenuTextBtnGutter, NiSpace::L, 0, 0)
		[
			MakeTextButton(TAttribute<FText>::CreateLambda([this]()
				{
					return FPlatformTime::Seconds() < QuitArmedUntil
						? Loc(ENiLocKey::ConfirmQuit) : Loc(ENiLocKey::Quit);
				}), NiType::Action, [this]()
				{
					const double Now = FPlatformTime::Seconds();
					if (Now < QuitArmedUntil)
					{
						if (OwnerPC.IsValid())
						{
							UKismetSystemLibrary::QuitGame(OwnerPC.Get(), OwnerPC.Get(), EQuitPreference::Quit, false);
						}
					}
					else
					{
						QuitArmedUntil = Now + 3.0;
					}
				}, nullptr, /*bDanger=*/true)
		]

		// 狀態行（creating/looking/joining/failed）＋進行中取消鈕
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(NiMenuLeftGutter, NiSpace::M, 0, 0)
		[
			MakeStatusRow()
		]
		+ SVerticalBox::Slot().FillHeight(0.78f)[SNew(SSpacer)]
	];
}

TSharedRef<SWidget> SNiMenu::MakeTextButton(const TAttribute<FText>& Label, const NiType::FRole& Role,
	TFunction<void()> OnClick, TFunction<bool()> Enabled, bool bDanger)
{
	// 文字鈕（中性制的主要控制項）：無底、無框；hover＝文字轉強調色＋左側 3px 短棒。
	// 短棒佔位恆在（Hidden 不是 Collapsed）⇒ 文字不會在 hover 時位移。
	// 主動詞（Heading）＝白；次要動詞（Text）＝白 70%；毀滅性＝hover 轉紅。
	const bool bPrimary = Role.Size >= 24;
	const FLinearColor Idle = bPrimary ? NiHudColor::White : NiHudColor::White70;
	const FLinearColor Hot = bDanger ? NiHudColor::Red : NiHudColor::AccentText;

	TSharedPtr<SButton> Btn;
	// 停用不走 SButton::IsEnabled——Slate 的停用效果會把整顆壓暗，主動詞看起來比次要還淡。
	// 改成點擊閘＋文字 45% 白（與全站的「不可用」同一種表達）。
	SAssignNew(Btn, SButton).ButtonStyle(&TextStyle).IsFocusable(false)
		.ContentPadding(FMargin(0, 0))
		.OnClicked_Lambda([OnClick, Enabled]() { if (!Enabled || Enabled()) { OnClick(); } return FReply::Handled(); });
	TWeakPtr<SButton> Weak = Btn;
	auto Hovered = [Weak]() { const TSharedPtr<SButton> B = Weak.Pin(); return B.IsValid() && B->IsHovered(); };
	auto Usable = [Enabled]() { return !Enabled || Enabled(); };

	// 固定行高（字級 ×2）：清單的節奏由行高給，不由每列的內容決定
	Btn->SetContent(
		SNew(SBox).HeightOverride(Role.Size * 2.25f).VAlign(VAlign_Center)
		[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(3.0f).HeightOverride(Role.Size * 0.8f)
			[
				SNew(SImage).Image(&AccentBarBrush).ColorAndOpacity(Hot)
					.Visibility_Lambda([Hovered]() { return Hovered() ? EVisibility::HitTestInvisible : EVisibility::Hidden; })
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(NiSpace::M, 0, 0, 0)
		[
			SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(Role))
				.ColorAndOpacity_Lambda([Hovered, Usable, Idle, Hot]()
				{
					if (!Usable()) { return FSlateColor(NiHudColor::White45); }
					return FSlateColor(Hovered() ? Hot : Idle);
				})
				// 展示體角色＝全大寫（CJK 恆等）
				.Text_Lambda([Label, bUp = Role.bSerif]()
				{
					return bUp ? FText::FromString(Label.Get().ToString().ToUpper()) : Label.Get();
				})
		]
		]);
	return Btn.ToSharedRef();
}

TSharedRef<SWidget> SNiMenu::MakeTextButton(const FText& Label, const NiType::FRole& Role,
	TFunction<void()> OnClick, TFunction<bool()> Enabled, bool bDanger)
{
	return MakeTextButton(TAttribute<FText>(Label), Role, MoveTemp(OnClick), MoveTemp(Enabled), bDanger);
}

TSharedRef<SWidget> SNiMenu::MakeRootLangRow()
{
	// 13 語母語名 chips（首頁欄底）：點選即套用、重建選單（靜態文字在 Construct 定死）。
	// 母語名自己會說話——任何語言狀態下玩家都能一眼掃到自己的。
	TSharedRef<SWrapBox> Row = SNew(SWrapBox).UseAllottedSize(true);
	const int32 Cur = GI() ? GI()->GetMenuLanguage() : 0;
	for (int32 i = 0; i < NiLoc::NumLangs; ++i)
	{
		const bool bSel = (i == Cur);
		Row->AddSlot().Padding(0, 0, NiSpace::S, NiSpace::S)
		[
			SNew(SBorder).BorderImage(bSel ? &ChipOnBrush : &ChipOffBrush)
				.HAlign(HAlign_Center).VAlign(VAlign_Center)
				.Padding(FMargin(10, 4))
				.OnMouseButtonDown_Lambda([this, i](const FGeometry&, const FPointerEvent&)
				{
					if (UNiceInkGameInstance* Inst = GI())
					{
						Inst->ApplyLanguage(i);
					}
					if (OwnerPC.IsValid())
					{
						if (ANiceInkMenuHUD* Hud = Cast<ANiceInkMenuHUD>(OwnerPC->GetHUD()))
						{
							Hud->RecreateMenu(/*bOpenSettings=*/false);
						}
					}
					return FReply::Handled();
				})
			[
				SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Note))
					.ColorAndOpacity(bSel ? NiHudColor::AccentText : NiHudColor::White45)
					.Text(FText::FromString(NiLoc::LangNativeName(i)))
			]
		];
	}
	return Row;
}

// 狀態行＋取消鈕（Root/Join 共用）：hosting/searching/joining 進行中給一個出口
TSharedRef<SWidget> SNiMenu::MakeStatusRow()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Body))
				.ColorAndOpacity_Lambda([this]() { return StatusColor(); })
				.Text_Lambda([this]() { return StatusText(); })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(NiSpace::M, 0, 0, 0)
		[
			SNew(SBox)
				.Visibility_Lambda([this]() { return IsBusy() ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				MakeGhostButton(LocS(ENiLocKey::CancelBtn), [this]()
				{
					if (UNiceInkSessionSubsystem* S = Sessions()) { S->CancelMenuAction(); }
				})
			]
		];
}

FReply SNiMenu::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// 加入頁：房號輸入走 OnKeyDown＝要有鍵盤焦點；點到頁面任何空白處把焦點抓回來
	//（焦點被搶走時打字無聲失效＝狀態不可見的死路）
	if (Page == EPage::Join)
	{
		FSlateApplication::Get().SetAllUserFocus(AsShared());
	}
	return FReply::Unhandled();
}

TSharedRef<SWidget> SNiMenu::MakeMaxPlayersRow()
{
	// 房間人數 4~6（2026-08-14 user 裁決「房主直接決定這房幾個人」：一個數字
	// 一個語義——坐滿關門；「至少 4 人開局」是規則不是設定、藏在開始鈕）
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	for (int32 N = 4; N <= 6; ++N)
	{
		Row->AddSlot().FillWidth(1)
		[
			SNew(SBorder)
				.BorderImage_Lambda([this, N]() -> const FSlateBrush*
					{ return HostMaxPlayers == N ? &ChipOnBrush : &ChipOffBrush; })
				.HAlign(HAlign_Center).VAlign(VAlign_Center)
				.Padding(FMargin(0, 8))
				.OnMouseButtonDown_Lambda([this, N](const FGeometry&, const FPointerEvent&)
					{ HostMaxPlayers = N; return FReply::Handled(); })
			[
				SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Body))
					.ColorAndOpacity_Lambda([this, N]()
						{ return HostMaxPlayers == N ? FSlateColor(NiHudColor::AccentText) : FSlateColor(NiHudColor::PaperDim); })
					.Text(FText::AsNumber(N))
			]
		];
	}
	return SNew(SBorder).BorderImage(&SegmentBrush).Padding(FMargin(2))[Row];
}

TSharedRef<SWidget> SNiMenu::MakeHostLangRow()
{
	// 公開房語言（2026-08-14 user 定案「做」）：預設跟介面語言、可改——
	// 介面語言≠想玩的語言的人（英文介面開繁中房/雙語玩家開外語房）有出口。
	// 13 語母語名 chips wrap 排列（與文A 頁同「母語名自己會說話」原則）
	TSharedRef<SWrapBox> Row = SNew(SWrapBox).UseAllottedSize(true);
	for (int32 i = 0; i < NiLoc::NumLangs; ++i)
	{
		Row->AddSlot().Padding(0, 0, 6, 6)
		[
			SNew(SBorder)
				.BorderImage_Lambda([this, i]() -> const FSlateBrush*
				{
					const int32 Cur = HostLangIndex != INDEX_NONE ? HostLangIndex
						: (GI() ? GI()->GetMenuLanguage() : 0);
					return Cur == i ? &ChipOnBrush : &ChipOffBrush;
				})
				.HAlign(HAlign_Center).VAlign(VAlign_Center)
				.Padding(FMargin(12, 6))
				.OnMouseButtonDown_Lambda([this, i](const FGeometry&, const FPointerEvent&)
					{ HostLangIndex = i; return FReply::Handled(); })
			[
				SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Note))
					.ColorAndOpacity_Lambda([this, i]()
					{
						const int32 Cur = HostLangIndex != INDEX_NONE ? HostLangIndex
							: (GI() ? GI()->GetMenuLanguage() : 0);
						return Cur == i ? FSlateColor(NiHudColor::AccentText) : FSlateColor(NiHudColor::PaperDim);
					})
					.Text(FText::FromString(NiLoc::LangNativeName(i)))
			]
		];
	}
	return Row;
}

TSharedRef<SWidget> SNiMenu::MakeJoinLangRow()
{
	// 列表語言過濾（規模版）：[全部語言]+13 母語名 chips；選定即重搜收合。
	// EOS 走查詢端過濾（SearchSessions LangFilter）、LAN 顯示層過濾
	auto PickLang = [this](int32 Lang)
	{
		JoinLangFilter = Lang;
		bJoinLangOpen = false;
		if (UNiceInkSessionSubsystem* S = Sessions())
		{
			S->SearchSessions(IsLan(), JoinLangFilter); // 忙碌中=重入護欄自擋，12s 自動更新會補
		}
	};
	auto MakeLangChip = [this, PickLang](int32 Lang, const FText& Label) -> TSharedRef<SWidget>
	{
		return SNew(SBorder)
			.BorderImage_Lambda([this, Lang]() -> const FSlateBrush*
				{ return JoinLangFilter == Lang ? &ChipOnBrush : &ChipOffBrush; })
			.HAlign(HAlign_Center).VAlign(VAlign_Center)
			.Padding(FMargin(12, 6))
			.OnMouseButtonDown_Lambda([PickLang, Lang](const FGeometry&, const FPointerEvent&)
				{ PickLang(Lang); return FReply::Handled(); })
			[
				SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Note))
					.ColorAndOpacity_Lambda([this, Lang]()
						{ return JoinLangFilter == Lang ? FSlateColor(NiHudColor::AccentText) : FSlateColor(NiHudColor::PaperDim); })
					.Text(Label)
			];
	};
	TSharedRef<SWrapBox> Row = SNew(SWrapBox).UseAllottedSize(true);
	Row->AddSlot().Padding(0, 0, 6, 6)[MakeLangChip(INDEX_NONE, Loc(ENiLocKey::AllLanguages))];
	for (int32 i = 0; i < NiLoc::NumLangs; ++i)
	{
		Row->AddSlot().Padding(0, 0, 6, 6)[MakeLangChip(i, FText::FromString(NiLoc::LangNativeName(i)))];
	}
	return Row;
}

TSharedRef<SWidget> SNiMenu::BuildHostPage()
{
	// 開房設定步（2026-08-11 user 裁決兩步流「頂層只放動詞」）：
	// 主卡按開房間→這裡選可見性→確認開房——與「加入房間→輸碼頁」對稱，
	// 開房的選項活在開房的流程裡，不再夾在兩個同級入口中間
	return SNew(SBox).Visibility_Lambda([this]() { return PageVis(EPage::Host); })
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(NiMenuLeftGutter, NiSpace::BandTop, 0, 0)
		[
			SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Title)).ColorAndOpacity(NiHudColor::Paper)
				.Text(Up(Loc(ENiLocKey::HostARoom)))
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(NiMenuLeftGutter, NiSpace::XL, 0, 0)
		[
			SNew(SBox).WidthOverride(NiSpace::CardW)
			[
				SNew(SBackgroundBlur).BlurStrength(NiMenuCardBlur).CornerRadius(FVector4(18, 18, 18, 18))
				[
					SNew(SBorder).BorderImage(&PageGroundBrush).Padding(FMargin(NiMenuGroundPad))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Label)).ColorAndOpacity(NiHudColor::PaperDim)
								.Text(Loc(ENiLocKey::RoomVisibility))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, NiSpace::S, 0, 0)
						[
							SNew(SBorder).BorderImage(&SegmentBrush).Padding(FMargin(2))
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1)[MakeChip(LocS(ENiLocKey::InviteOnly), false)]
								+ SHorizontalBox::Slot().FillWidth(1)[MakeChip(LocS(ENiLocKey::PublicRoom), true)]
							]
						]
						// 人數上限 2~6（2026-08-13 房主人數設定；chips 同可見性語彙）
						+ SVerticalBox::Slot().AutoHeight().Padding(0, NiSpace::M, 0, 0)
						[
							SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Label)).ColorAndOpacity(NiHudColor::PaperDim)
								.Text(Loc(ENiLocKey::MaxPlayersLabel))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, NiSpace::S, 0, 0)
						[
							MakeMaxPlayersRow()
						]
						// 公開房語言（只在公開時展開——邀請制房語言無作用不佔版面）
						+ SVerticalBox::Slot().AutoHeight().Padding(0, NiSpace::M, 0, 0)
						[
							SNew(SBox).Visibility_Lambda([this]()
								{ return bPublicRoom ? EVisibility::Visible : EVisibility::Collapsed; })
							[
								SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Label)).ColorAndOpacity(NiHudColor::PaperDim)
									.Text(Loc(ENiLocKey::Language))
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, NiSpace::S, 0, 0)
						[
							SNew(SBox).Visibility_Lambda([this]()
								{ return bPublicRoom ? EVisibility::Visible : EVisibility::Collapsed; })
							[
								MakeHostLangRow()
							]
						]
						// 公開房房名＝徵人啟事（2026-08-14 user 定案：讓瀏覽者
						// 知道這房在找怎樣的人；可空、只在公開時展開）
						+ SVerticalBox::Slot().AutoHeight().Padding(0, NiSpace::M, 0, 0)
						[
							SNew(SBox).Visibility_Lambda([this]()
								{ return bPublicRoom ? EVisibility::Visible : EVisibility::Collapsed; })
							[
								SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Label)).ColorAndOpacity(NiHudColor::PaperDim)
									.Text(Loc(ENiLocKey::RoomNameLabel))
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, NiSpace::S, 0, 0)
						[
							SNew(SBox).Visibility_Lambda([this]()
								{ return bPublicRoom ? EVisibility::Visible : EVisibility::Collapsed; })
							[
								SNew(SEditableTextBox)
									.Style(&NameBoxStyle)
									.HintText(Loc(ENiLocKey::RoomNameHint))
									// 打字即截 24＋即時入袋（不等 commit——按開房間
									// 時焦點可能還在框裡）
									.OnTextChanged_Lambda([this](const FText& T)
									{
										HostRoomName = T.ToString().Left(24);
									})
							]
						]
					]
				]
			]
		]

		// 確認開房
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(NiMenuLeftGutter, NiSpace::L, 0, 0)
		[
			SNew(SBox).WidthOverride(NiSpace::ColumnW)
			[
				SNew(SButton).ButtonStyle(&PrimaryStyle).IsFocusable(false)
					.HAlign(HAlign_Center).VAlign(VAlign_Center)
					.ContentPadding(FMargin(NiSpace::L, 10.0f))
					.IsEnabled_Lambda([this]() { return !IsBusy(); })
					.OnClicked_Lambda([this]()
					{
						ErrorBanner.Reset();
						if (UNiceInkSessionSubsystem* S = Sessions()) { S->HostSession(IsLan(), bPublicRoom, HostMaxPlayers, HostLangIndex, HostRoomName); }
						return FReply::Handled();
					})
				[
					SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Action)).ColorAndOpacity(NiHudColor::OnAccent).ShadowOffset(FVector2D::ZeroVector)
						.Text(Up(Loc(ENiLocKey::HostARoom)))
				]
			]
		]

		// 狀態/錯誤貼著動作＋進行中取消鈕
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(NiMenuLeftGutter, NiSpace::M, 0, 0)
		[
			MakeStatusRow()
		]

		+ SVerticalBox::Slot().FillHeight(1)[SNew(SSpacer)]

		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(NiMenuLeftGutter, 0, 0, NiSpace::BandBottom + NiMenuBottomReserve)
		[
			SNew(SSpacer)   // Back 已歸左下 [ESC] Back（2026-09-05：一個意圖一個位置）
		]
	];
}

TSharedRef<SWidget> SNiMenu::BuildJoinPage()
{
	// 房碼格：4 個白格、字母墨色、下一格酒金底線。
	// 房間碼＝拉丁字母 LTR 記號（唸給朋友聽的順序）——AR 鏡像下也不得逆序
	TSharedRef<SHorizontalBox> Slots = SNew(SHorizontalBox);
	Slots->SetFlowDirectionPreference(EFlowDirectionPreference::LeftToRight);
	for (int32 i = 0; i < 4; ++i)
	{
		Slots->AddSlot().AutoWidth().Padding(i == 0 ? 0 : 12, 0, 0, 0)
		[
			SNew(SBox).WidthOverride(64).HeightOverride(80)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()[SNew(SImage).Image(&SlotBrush)]
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Title)).ColorAndOpacity(NiHudColor::Paper)
						.Text_Lambda([this, i]()
						{
							return FText::FromString(i < CodeBuffer.Len() ? CodeBuffer.Mid(i, 1) : FString());
						})
				]
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0, 0, 0, NiSpace::S)
				[
					SNew(SBox).WidthOverride(40).HeightOverride(3)
					[
						SNew(SImage).Image(&UnderlineBrush)
							.Visibility_Lambda([this, i]()
							{
								return i == CodeBuffer.Len() ? EVisibility::Visible : EVisibility::Collapsed;
							})
					]
				]
			]
		];
	}

	return SNew(SBox).Visibility_Lambda([this]() { return PageVis(EPage::Join); })
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(NiMenuLeftGutter, NiSpace::BandTop, 0, 0)
		[
			SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Title)).ColorAndOpacity(NiHudColor::Paper)
				.Text(Up(Loc(ENiLocKey::JoinARoom)))
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(NiMenuLeftGutter, NiSpace::S, 0, 0)
		[
			SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Body)).ColorAndOpacity(NiHudColor::PaperDim)
				.Text(Loc(ENiLocKey::AskHostCode))
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(NiMenuLeftGutter, NiSpace::XL, 0, 0)
		[
			Slots
		]

		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(NiMenuLeftGutter, NiSpace::M, 0, 0)
		[
			SNew(SBox).WidthOverride(NiSpace::ColumnW)
			[
				SNew(SButton).ButtonStyle(&PrimaryStyle).IsFocusable(false)
					.HAlign(HAlign_Center).VAlign(VAlign_Center)
					.ContentPadding(FMargin(NiSpace::L, 10.0f))
					// 搜尋中也可按（JoinRoomByCode 的搭便車機制受理）；只擋
					// Joining/Hosting
					.IsEnabled_Lambda([this]()
					{
						if (CodeBuffer.Len() != 4) { return false; }
						const UNiceInkSessionSubsystem* S = Sessions();
						if (!S) { return false; }
						const ENiSessionUiState St = S->GetUiState();
						return St != ENiSessionUiState::Joining && St != ENiSessionUiState::Hosting;
					})
					.OnClicked_Lambda([this]()
					{
						CommitName();
						if (UNiceInkSessionSubsystem* S = Sessions()) { S->JoinRoomByCode(CodeBuffer, IsLan()); }
						return FReply::Handled();
					})
				[
					SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Action)).ColorAndOpacity(NiHudColor::OnAccent).ShadowOffset(FVector2D::ZeroVector)
						.Text(Up(Loc(ENiLocKey::JoinWithCode)))
				]
			]
		]

		// 狀態/錯誤緊貼動作（輸錯房號的訊息不再沉到螢幕底）＋進行中取消鈕
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(NiMenuLeftGutter, NiSpace::M, 0, 0)
		[
			MakeStatusRow()
		]

		// 公開房：沒房＝一行字（空盒子不上桌）、有房才展開白卡列表
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(NiMenuLeftGutter, NiSpace::L, 0, 0)
		[
			SNew(SHorizontalBox)
			.Visibility_Lambda([this]()
			{
				const UNiceInkSessionSubsystem* S = Sessions();
				if (!S || (S->GetUiState() != ENiSessionUiState::Idle && !S->IsBackgroundSearching()))
				{
					return EVisibility::Collapsed; // 前景搜尋中＝狀態行在講話；背景更新不藏
				}
				for (const FNiFoundSession& F : S->GetFoundSessions())
				{
					if (F.bPublic && (JoinLangFilter == INDEX_NONE || F.LangIndex == JoinLangFilter))
					{
						return EVisibility::Collapsed;
					}
				}
				return EVisibility::Visible;
			})
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Note)).ColorAndOpacity(NiHudColor::PaperDim)
					.Text(Loc(ENiLocKey::NoPublicRooms))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(NiSpace::M, 0, 0, 0)
			[
				MakeGhostButton(LocS(ENiLocKey::Refresh), [this]()
				{
					if (UNiceInkSessionSubsystem* S = Sessions()) { S->SearchSessions(IsLan()); }
				})
			]
			// 空狀態出口（2026-08-14 列表 UX）：死路變轉化——沒房就自己開
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(NiSpace::S, 0, 0, 0)
			[
				MakeGhostButton(LocS(ENiLocKey::OpenPublicShortcut), [this]()
				{
					bPublicRoom = true; // 從公開列表來＝預選公開
					Page = EPage::Host;
				})
			]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(NiMenuLeftGutter, 0, 0, 0)
		[
			SNew(SBox).WidthOverride(NiSpace::CardWWide)
				.Visibility_Lambda([this]()
				{
					const UNiceInkSessionSubsystem* S = Sessions();
					if (!S) { return EVisibility::Collapsed; }
					for (const FNiFoundSession& F : S->GetFoundSessions())
					{
						if (F.bPublic && (JoinLangFilter == INDEX_NONE || F.LangIndex == JoinLangFilter))
						{
							return EVisibility::Visible;
						}
					}
					return EVisibility::Collapsed;
				})
			[
				SNew(SBackgroundBlur).BlurStrength(NiMenuCardBlur).CornerRadius(FVector4(18, 18, 18, 18))
				[
					SNew(SBorder).BorderImage(&PageGroundBrush).Padding(FMargin(NiMenuGroundPad))
					[
						SNew(SVerticalBox)
						// 標籤列＋語言過濾 chip＋重新整理（鈕住在它管的列表旁）
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
							[
								SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Label)).ColorAndOpacity(NiHudColor::PaperDim)
									.Text(Loc(ENiLocKey::PublicRooms))
							]
							// 語言過濾 chip：顯示現值（母語名/全部語言）、點開選擇列
							+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, NiSpace::S, 0)
							[
								SNew(SButton).ButtonStyle(&OnCardStyle).IsFocusable(false)
									.ContentPadding(FMargin(14, 4))
									.OnClicked_Lambda([this]()
									{
										bJoinLangOpen = !bJoinLangOpen;
										return FReply::Handled();
									})
								[
									SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Note)).ColorAndOpacity(NiHudColor::Paper)
										.Text_Lambda([this]()
										{
											return JoinLangFilter == INDEX_NONE
												? Loc(ENiLocKey::AllLanguages)
												: FText::FromString(NiLoc::LangNativeName(JoinLangFilter));
										})
								]
							]
							+ SHorizontalBox::Slot().AutoWidth()
							[
								SNew(SButton).ButtonStyle(&OnCardStyle).IsFocusable(false)
									.ContentPadding(FMargin(14, 4))
									.IsEnabled_Lambda([this]() { return !IsBusy(); })
									.OnClicked_Lambda([this]()
									{
										if (UNiceInkSessionSubsystem* S = Sessions()) { S->SearchSessions(IsLan(), JoinLangFilter); }
										return FReply::Handled();
									})
								[
									SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Note)).ColorAndOpacity(NiHudColor::Paper)
										.Text(Loc(ENiLocKey::Refresh))
								]
							]
						]
						// 語言選擇列（chip 點開才展；選定即重搜收合）
						+ SVerticalBox::Slot().AutoHeight().Padding(0, NiSpace::S, 0, 0)
						[
							SNew(SBox).Visibility_Lambda([this]()
								{ return bJoinLangOpen ? EVisibility::Visible : EVisibility::Collapsed; })
							[
								MakeJoinLangRow()
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, NiSpace::S, 0, 0)
						[
							// 超過約 5 列由捲動接手（上限 8 列；卡片高度封頂）
							SNew(SBox).MaxDesiredHeight(300)
							[
								SNew(SScrollBox)
								+ SScrollBox::Slot()
								[
									SAssignNew(RoomListBox, SVerticalBox)
								]
							]
						]
					]
				]
			]
		]

		+ SVerticalBox::Slot().FillHeight(1)[SNew(SSpacer)]

		// 底帶只剩導航（狀態行已上移貼動作、刷新已歸列表卡）
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(NiMenuLeftGutter, 0, 0, NiSpace::BandBottom + NiMenuBottomReserve)
		[
			SNew(SSpacer)   // Back 已歸左下 [ESC] Back（2026-09-05：一個意圖一個位置）
		]
	];
}

TSharedRef<SWidget> SNiMenu::BuildSettingsPage()
{
	// Enabled：整列可用性（無邊框下解析度＝死旋鈕→停用＋說明，因果不再斷）
	auto MakeArrow = [this](const FString& Glyph, TFunction<void()> OnClick,
		TFunction<bool()> Enabled) -> TSharedRef<SWidget>
	{
		// 箭號＝細字元、無底（灰方塊讀成試算表）
		return SNew(SButton).ButtonStyle(&TextStyle).IsFocusable(false)
			.HAlign(HAlign_Center).VAlign(VAlign_Center)
			.ContentPadding(FMargin(12, 4))
			.IsEnabled_Lambda([Enabled]() { return !Enabled || Enabled(); })
			.OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
			[
				SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Body)).ColorAndOpacity(NiHudColor::White70)
					.Text(FText::FromString(Glyph == TEXT("<") ? TEXT("\u2039") : TEXT("\u203A")))
			];
	};
	auto MakeRow = [this, &MakeArrow](const FString& Label, TFunction<FString()> Value,
		TFunction<void()> OnLeft, TFunction<void()> OnRight,
		TFunction<bool()> Enabled = nullptr) -> TSharedRef<SWidget>
	{
		// 每列＝標籤／箭號／值／箭號，列底一條 10% 髮線（列與列之間的節奏由線給，不由方塊給）
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
			[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
			[
				// 列標籤＝大寫小標（三批）：設定列是「標籤｜控制」，標籤不該跟值一樣大
				SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Label)).ColorAndOpacity(NiHudColor::PaperDim)
					.Text(FText::FromString(Label.ToUpper()))
			]
			+ SHorizontalBox::Slot().AutoWidth()[MakeArrow(TEXT("<"), OnLeft, Enabled)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(150)
				[
					SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Body)).Justification(ETextJustify::Center)
						.ColorAndOpacity_Lambda([Enabled]() -> FSlateColor
						{
							return (!Enabled || Enabled()) ? FSlateColor(NiHudColor::Paper) : FSlateColor(NiHudColor::PaperDim);
						})
						.Text_Lambda([Value]() { return FText::FromString(Value()); })
				]
			]
			+ SHorizontalBox::Slot().AutoWidth()[MakeArrow(TEXT(">"), OnRight, Enabled)]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).HeightOverride(1.0f)[SNew(SImage).Image(&DividerBrush)]
			];
	};

	return SNew(SBox).Visibility_Lambda([this]() { return PageVis(EPage::Settings); })
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(NiMenuLeftGutter, NiSpace::BandTop, 0, 0)
		[
			SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Title)).ColorAndOpacity(NiHudColor::Paper)
				.Text(Up(Loc(ENiLocKey::SettingsBtn)))
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(NiMenuLeftGutter, NiSpace::XL, 0, 0)
		[
			SNew(SBox).WidthOverride(NiSpace::CardWWide)
			[
				SNew(SBackgroundBlur).BlurStrength(8.0f).CornerRadius(FVector4(8, 8, 8, 8))
				[
					SNew(SBorder).BorderImage(&PanelBrush).Padding(FMargin(NiSpace::L, NiSpace::M))
					[
						SNew(SVerticalBox)

						// --- 全頁單一模型（2026-08-11 user 定案）：改了就生效、就存檔。
						// 視窗模式＝無邊框/視窗二態即點即切（同為合成器視窗＝不經顯示器
						// 重同步、不黑閃）；視窗＝瀏覽器式可拖拉（引擎原生 bAllowWindowResize）。
						// 獨占全螢幕與解析度選單退役——效能旋鈕改渲染比例（零切換零延遲）---
						+ SVerticalBox::Slot().AutoHeight()
						[
							MakeRow(LocS(ENiLocKey::WindowMode),
								[this]()
								{
									// 玩家語言：無邊框就是「全螢幕」（Borderless=實作詞退役）
									return PendingWindowMode == 1
										? LocS(ENiLocKey::Fullscreen) : LocS(ENiLocKey::Windowed);
								},
								[this]() { ToggleWindowMode(); },
								[this]() { ToggleWindowMode(); })
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							MakeRow(LocS(ENiLocKey::RenderScale),
								[this]()
								{
									const UNiceInkGameInstance* I = GI();
									return FString::Printf(TEXT("%d%%"), I ? FMath::RoundToInt(I->RenderScalePct) : 100);
								},
								[this]() { if (UNiceInkGameInstance* I = GI()) { I->RenderScalePct = FMath::Clamp(I->RenderScalePct - 5.0f, 50.0f, 100.0f); I->ApplyRenderScale(); I->SaveSettings(); } },
								[this]() { if (UNiceInkGameInstance* I = GI()) { I->RenderScalePct = FMath::Clamp(I->RenderScalePct + 5.0f, 50.0f, 100.0f); I->ApplyRenderScale(); I->SaveSettings(); } })
						]

						// 幀率上限（2026-08-25）：引擎預設無上限＝顯卡被拉到滿速去畫
						// 每秒 500 張（實測空道場 1280×720 GPU 成本只有 1.05ms/幀）。
						// 風扇狂叫、筆電發燙、多開直接把機器打爆——煞車必須存在，
						// 而且要是玩家看得到、改得動的那種。
						+ SVerticalBox::Slot().AutoHeight()
						[
							MakeRow(LocS(ENiLocKey::FrameLimit),
								[this]() { return FrameLimitValueText(); },
								[this]() { StepFrameLimit(-1); },
								[this]() { StepFrameLimit(+1); })
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							MakeRow(LocS(ENiLocKey::VSyncLabel),
								[this]()
								{
									const UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
									return LocS((GUS && GUS->IsVSyncEnabled()) ? ENiLocKey::OptionOn : ENiLocKey::OptionOff);
								},
								[this]() { ToggleVSync(); },
								[this]() { ToggleVSync(); })
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							MakeRow(LocS(ENiLocKey::MouseSensitivity),
								[this]() { const UNiceInkGameInstance* I = GI(); return FString::Printf(TEXT("%.1f"), I ? I->MouseSensitivityScale : 1.0f); },
								[this]() { if (UNiceInkGameInstance* I = GI()) { I->MouseSensitivityScale = FMath::Clamp(I->MouseSensitivityScale - 0.1f, 0.2f, 3.0f); I->SaveSettings(); } },
								[this]() { if (UNiceInkGameInstance* I = GI()) { I->MouseSensitivityScale = FMath::Clamp(I->MouseSensitivityScale + 0.1f, 0.2f, 3.0f); I->SaveSettings(); } })
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							MakeRow(LocS(ENiLocKey::MasterVolume),
								[this]() { const UNiceInkGameInstance* I = GI(); return FString::Printf(TEXT("%d%%"), I ? FMath::RoundToInt(I->MasterVolume * 100.0f) : 100); },
								[this]() { if (UNiceInkGameInstance* I = GI()) { I->MasterVolume = FMath::Clamp(I->MasterVolume - 0.05f, 0.0f, 1.0f); I->UpdateBgmVolume(); I->SaveSettings(); } },
								[this]() { if (UNiceInkGameInstance* I = GI()) { I->MasterVolume = FMath::Clamp(I->MasterVolume + 0.05f, 0.0f, 1.0f); I->UpdateBgmVolume(); I->SaveSettings(); } })
						]
					// （語言入口唯一化 2026-08-11 user 裁決：語言只住主選單「文A」鈕
						// ——它是救援入口、任何語言狀態都找得到；設定頁重複列砍除）
					]
				]
			]
		]
		+ SVerticalBox::Slot().FillHeight(1)[SNew(SSpacer)]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(NiMenuLeftGutter, 0, 0, NiSpace::BandBottom + NiMenuBottomReserve)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(NiSpace::S, 0)
			[
				MakeGhostButton(LocS(ENiLocKey::Licenses), [this]() { Page = EPage::Credits; })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(NiSpace::S, 0)
			[
				SNew(SSpacer)   // Back 已歸左下 [ESC] Back（2026-09-05：一個意圖一個位置）
			]
		]
	];
}

FString SNiMenu::FrameLimitValueText() const
{
	const UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	const float Limit = GUS ? GUS->GetFrameRateLimit() : 0.0f;
	return (Limit <= 0.0f)
		? LocS(ENiLocKey::Unlimited)
		: FString::Printf(TEXT("%d FPS"), FMath::RoundToInt(Limit));
}

void SNiMenu::StepFrameLimit(int32 Dir)
{
	UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!GUS)
	{
		return;
	}
	const TArray<float>& Choices = UNiceInkGameInstance::GetFrameRateLimitChoices();
	const float Current = GUS->GetFrameRateLimit();

	// 目前值找不到就落在最接近的一格（玩家可能從 ini 手改過任意值）
	int32 Index = 0;
	float BestErr = TNumericLimits<float>::Max();
	for (int32 I = 0; I < Choices.Num(); ++I)
	{
		const float Err = FMath::Abs(Choices[I] - Current);
		if (Err < BestErr) { BestErr = Err; Index = I; }
	}
	Index = (Index + Dir + Choices.Num()) % Choices.Num();

	GUS->SetFrameRateLimit(Choices[Index]);
	GUS->ApplyNonResolutionSettings(); // 不碰解析度／視窗模式
	GUS->SaveSettings();
	MarkPerfDefaultsTouched();
}

void SNiMenu::ToggleVSync()
{
	UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!GUS)
	{
		return;
	}
	GUS->SetVSyncEnabled(!GUS->IsVSyncEnabled());
	GUS->ApplyNonResolutionSettings(); // 不碰解析度／視窗模式
	GUS->SaveSettings();
	MarkPerfDefaultsTouched();
}

void SNiMenu::MarkPerfDefaultsTouched()
{
	// 玩家親手動過＝往後任何版本的預設遷移都不准再碰（就算他選了「無上限」
	// 或把 VSync 關掉也算數）。**旗標與版次要分開**：自動遷移也會推進版次，
	// 兩者混用就分不出「系統設的」與「玩家選的」。
	if (UNiceInkGameInstance* I = GI())
	{
		I->bPerfTouchedByPlayer = true;
		I->PerfDefaultsVersion = UNiceInkGameInstance::NiPerfDefaultsVersion;
		I->SaveSettings();
	}
}

void SNiMenu::ToggleWindowMode()
{
	// 無邊框↔視窗（同為合成器視窗＝不黑閃）。進視窗模式給桌面 70% 的初始
	// 大小——其後像瀏覽器一樣拖邊角改大小（引擎原生，bAllowWindowResize 預設開）
	PendingWindowMode = (PendingWindowMode == 1) ? 2 : 1;
	UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!GUS)
	{
		return;
	}
	if (PendingWindowMode == 2)
	{
		FDisplayMetrics DM;
		FSlateApplication::Get().GetCachedDisplayMetrics(DM);
		const FIntPoint WindowSize(
			FMath::Max(960, FMath::RoundToInt(DM.PrimaryDisplayWidth * 0.7f)),
			FMath::Max(540, FMath::RoundToInt(DM.PrimaryDisplayHeight * 0.7f)));
		GUS->SetScreenResolution(WindowSize);
		GUS->SetFullscreenMode(EWindowMode::Windowed);
	}
	else
	{
		GUS->SetFullscreenMode(EWindowMode::WindowedFullscreen);
	}
	GUS->ApplySettings(false);
	GUS->SaveSettings();
}

TSharedRef<SWidget> SNiMenu::BuildCreditsPage()
{
	const TCHAR* Lines[] = {
		TEXT("nice ink uses these third-party works:"),
		TEXT(""),
		TEXT("Noto Sans family (Latin/JP/TC/SC/KR/Arabic) — (c) The Noto Project Authors, SIL Open Font License 1.1"),
		TEXT("Oswald — (c) The Oswald Project Authors (Vernon Adams), SIL Open Font License 1.1"),
		TEXT("icons adapted from game-icons.net — CC BY 3.0"),
		TEXT("Lucide icons — (c) Lucide Contributors, ISC License"),
		TEXT("\"Sauna\" 3d scene by local.yany (sketchfab) — CC BY 4.0"),
		TEXT("\"Radiola from Matrix\" 3d model by Sirenko (sketchfab) — CC BY 4.0"),
		TEXT(""),
		TEXT("full license texts: THIRD_PARTY_NOTICES.md next to the game executable"),
	};
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	Box->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(NiMenuLeftGutter, NiSpace::BandTop, 0, 0)
	[
		SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Title)).ColorAndOpacity(NiHudColor::Paper)
			.Text(Up(Loc(ENiLocKey::Licenses)))
	];
	Box->AddSlot().FillHeight(1)[SNew(SSpacer)];
	for (const TCHAR* Line : Lines)
	{
		Box->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(NiMenuLeftGutter, NiSpace::XS, 0, 0)
		[
			SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Note)).ColorAndOpacity(NiHudColor::White70)
				.Text(FText::FromString(Line))
		];
	}
	Box->AddSlot().FillHeight(1)[SNew(SSpacer)];
	Box->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, NiSpace::BandBottom + NiMenuBottomReserve)
	[
		SNew(SSpacer)   // Back 已歸左下 [ESC] Back（2026-09-05：一個意圖一個位置）
	];
	return SNew(SBox).Visibility_Lambda([this]() { return PageVis(EPage::Credits); })[Box];
}

TSharedRef<SWidget> SNiMenu::BuildLanguagePage()
{
	// 13 語母語名全列（兩欄網格）：任何語言狀態下玩家都能一眼掃到自己的母語——
	// 「看不懂就到不了設定」窘境的正解（2026-08-06 業界慣例調查）
	TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel).SlotPadding(FMargin(0, 2));
	const int32 Current = GI() ? GI()->GetMenuLanguage() : 0;
	for (int32 i = 0; i < NiLoc::NumLangs; ++i)
	{
		const bool bSelected = i == Current;
		Grid->AddSlot(i % 2, i / 2)
		[
			SNew(SButton).ButtonStyle(&TextStyle).IsFocusable(false)
				.HAlign(HAlign_Left).VAlign(VAlign_Center)
				.ContentPadding(FMargin(12, 8))
				.OnClicked_Lambda([this, i]()
				{
					if (UNiceInkGameInstance* Inst = GI())
					{
						Inst->ApplyLanguage(i);
					}
					if (OwnerPC.IsValid())
					{
						if (ANiceInkMenuHUD* Hud = Cast<ANiceInkMenuHUD>(OwnerPC->GetHUD()))
						{
							// 從哪進就回哪（重建後落回出發頁；設定進＝回設定）
							Hud->RecreateMenu(/*bOpenSettings=*/LangOrigin == EPage::Settings);
						}
					}
					return FReply::Handled();
				})
			[
				SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::ActionSmall)).ColorAndOpacity(bSelected ? NiHudColor::AccentText : NiHudColor::White70)
					.Text(FText::FromString(NiLoc::LangNativeName(i)))
			]
		];
	}

	return SNew(SBox).Visibility_Lambda([this]() { return PageVis(EPage::Language); })
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(NiMenuLeftGutter, NiSpace::BandTop, 0, 0)
		[
			// 頁內標題＝「語言」（入口鈕才需要「文A」救援語義——進到頁裡
			// 的人已脫離迷路情境，正文的母語名網格自己會說話）
			SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Title)).ColorAndOpacity(NiHudColor::Paper)
				.Text(Up(Loc(ENiLocKey::Language)))
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(NiMenuLeftGutter, NiSpace::XL, 0, 0)
		[
			SNew(SBox).WidthOverride(NiSpace::CardWWide)
			[
				SNew(SBackgroundBlur).BlurStrength(NiMenuCardBlur).CornerRadius(FVector4(18, 18, 18, 18))
				[
					SNew(SBorder).BorderImage(&PageGroundBrush).Padding(FMargin(NiMenuGroundPad))
					[
						Grid
					]
				]
			]
		]
		+ SVerticalBox::Slot().FillHeight(1)[SNew(SSpacer)]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(NiMenuLeftGutter, 0, 0, NiSpace::BandBottom + NiMenuBottomReserve)
		[
			SNew(SSpacer)   // Back 已歸左下 [ESC] Back（2026-09-05：一個意圖一個位置）
		]
	];
}

// --- 個人檔案卡的四塊積木（創角/日常各自組裝）---

TSharedRef<SWidget> SNiMenu::MakeProfileFacesBlock()
{
	// 臉庫（日常模式的主角：這頁的身分本體=臉；空庫自動隱藏）
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Label)).ColorAndOpacity(NiHudColor::PaperDim)
				.Visibility_Lambda([this]()
				{
					return (FaceRowBox.IsValid() && FaceRowBox->GetChildren()->Num() > 0)
						? EVisibility::Visible : EVisibility::Collapsed;
				})
				.Text(Loc(ENiLocKey::SavedFaces))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, NiSpace::S, 0, 0)
		[
			SAssignNew(FaceRowBox, SWrapBox).UseAllottedSize(true)
		];
}

TSharedRef<SWidget> SNiMenu::MakeProfileNameBlock()
{
	// 名字（可選——隨機名可用）
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Label)).ColorAndOpacity(NiHudColor::PaperDim)
					.Text(Loc(ENiLocKey::YourName))
			]
			// **欄位計數器**（2026-09-05；Meccha 每個輸入框右上都有 `11/30` 綠字）：
			// 上限此前只在打字被無聲截斷的那一刻才會被發現。數字用強調色＝
			// 「這是一個會擋你的界線」，與其他說明小字分開。
			+ SHorizontalBox::Slot().FillWidth(1).HAlign(HAlign_Right)
			[
				SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Label)).ColorAndOpacity(NiHudColor::Amber)
					.Text_Lambda([this]() -> FText
					{
						const int32 N = NameBox.IsValid() ? NameBox->GetText().ToString().Len() : 0;
						return FText::FromString(FString::Printf(TEXT("%d/16"), N));
					})
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, NiSpace::S, 0, 0)
		[
			SAssignNew(NameBox, SEditableTextBox)
				.Style(&NameBoxStyle)
				// 無自訂名＝欄位空、hint 顯示目前生效的保底/平台名（淡字）
				// ——隨機名不再偽裝成已取好的名字
				.HintText_Lambda([this]() -> FText
				{
					const UNiceInkGameInstance* I = GI();
					return I ? FText::FromString(I->GetEffectiveDisplayName())
						: Loc(ENiLocKey::ClickToType);
				})
				// 打字即截斷（SanitizePlayerName 的 16 碼元上限只在 commit 後
				// 生效——輸入框裡先擋，玩家不會打一長串然後被無聲砍掉）
				.OnTextChanged_Lambda([this](const FText& T)
				{
					const FString S = T.ToString();
					if (S.Len() > 16 && NameBox.IsValid())
					{
						NameBox->SetText(FText::FromString(S.Left(16)));
					}
				})
				.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type)
				{
					CommitName();
				})
		]
		// 名字狀態列：隨機名說明 ↔ 「已儲存」回饋（存了要說）
		+ SVerticalBox::Slot().AutoHeight().Padding(0, NiSpace::XS, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Note)).ColorAndOpacity(NiHudColor::PaperDim)
					.Visibility_Lambda([this]()
					{
						const UNiceInkGameInstance* I = GI();
						const bool bSavedShowing = FPlatformTime::Seconds() < NameSavedUntil;
						return (I && I->PlayerDisplayName.IsEmpty() && !bSavedShowing)
							? EVisibility::Visible : EVisibility::Collapsed;
					})
					.Text(Loc(ENiLocKey::NameFallbackNote))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Note)).ColorAndOpacity(NiHudColor::Amber)
					.Visibility_Lambda([this]()
					{
						return FPlatformTime::Seconds() < NameSavedUntil
							? EVisibility::Visible : EVisibility::Collapsed;
					})
					.Text(Loc(ENiLocKey::NameSavedNote))
			]
		];
}

TSharedRef<SWidget> SNiMenu::MakeProfileCashBlock()
{
	// 現金（僅日常模式；空值不領大字——$ —/$ … 用內文級、有真數字才 Value）
	// 2026-09-07：沒有值（LAN／未登入）就整塊不畫——出貨畫面上不准有「$ —」這種佔位符
	return SNew(SVerticalBox)
		.Visibility_Lambda([this]()
		{
			UNiceInkPersonaSubsystem* P = Persona();
			const UNiceInkSessionSubsystem* S = Sessions();
			const bool bHasData = P && P->GetCloudSaveView();
			return (bHasData || (S && S->IsLoggedIn()) || (!IsLan() && S && !S->IsLoggedIn()))
				? EVisibility::Visible : EVisibility::Collapsed;
		})
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Label)).ColorAndOpacity(NiHudColor::PaperDim)
				.Text(Loc(ENiLocKey::CashLabel))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, NiSpace::XS, 0, 0)
		[
			SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).ColorAndOpacity(NiHudColor::Paper)
				.Font_Lambda([this]() -> FSlateFontInfo
				{
					UNiceInkPersonaSubsystem* P = Persona();
					const bool bHasData = P && P->GetCloudSaveView();
					return Ty(bHasData ? NiType::Value : NiType::Body);
				})
				.Text_Lambda([this]() -> FText
				{
					if (UNiceInkPersonaSubsystem* P = Persona())
					{
						if (UNiceInkSaveGame* Save = P->GetCloudSaveView())
						{
							return FText::FromString(FString::Printf(TEXT("$ %d"), Save->Cash));
						}
					}
					const UNiceInkSessionSubsystem* S = Sessions();
					// 已登入但雲端未到＝載入中；未登入＝無值
					return (S && S->IsLoggedIn())
						? FText::FromString(TEXT("$ …"))
						: FText::FromString(TEXT("$ —"));
				})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, NiSpace::XS, 0, 0)
		[
			SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Note)).ColorAndOpacity(NiHudColor::PaperDim)
				.AutoWrapText(true)
				.Visibility_Lambda([this]()
				{
					// 行動句只在「線上模式且未登入」時亮
					const UNiceInkSessionSubsystem* S = Sessions();
					return (!IsLan() && S && !S->IsLoggedIn())
						? EVisibility::Visible : EVisibility::Collapsed;
				})
				.Text(Loc(ENiLocKey::SignInBrowserNote))
		];
}

TSharedRef<SWidget> SNiMenu::MakeProfileUploadBlock()
{
	// 上傳區塊：必讀（警告級眉毛鐵律＋隱私）→動作→狀態
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Warning)).ColorAndOpacity(NiHudColor::Paper)
				.AutoWrapText(true)
				.Text(Loc(ENiLocKey::BrowHint))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, NiSpace::S, 0, 0)
		[
			SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Note)).ColorAndOpacity(NiHudColor::PaperDim)
				.AutoWrapText(true)
				.Text(Loc(ENiLocKey::PrivacyHint))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, NiSpace::M, 0, 0)
		[
			SNew(SButton).ButtonStyle(&PrimaryStyle).IsFocusable(false)
				.HAlign(HAlign_Center).VAlign(VAlign_Center)
				.ContentPadding(FMargin(NiSpace::L, 10.0f))
				.IsEnabled_Lambda([this]()
				{
					UNiceInkPersonaSubsystem* P = Persona();
					return !P || P->GetIntakeState() != UNiceInkPersonaSubsystem::EFaceIntakeState::Running;
				})
				.OnClicked_Lambda([this]()
				{
					PickSelfieAndIntake();
					return FReply::Handled();
				})
			[
				SNew(STextBlock).Font(Ty(NiType::Action)).ColorAndOpacity(NiHudColor::OnAccent)
					.Text_Lambda([this]()
					{
						// 已有臉＝「重新上傳」（user 定案 2026-08-07）
						UNiceInkPersonaSubsystem* P = Persona();
						return Up((P && P->HasCustomFace())
							? Loc(ENiLocKey::ReuploadSelfie) : Loc(ENiLocKey::UploadSelfie));
					})
			]
		]
		// 管線狀態列（Running/Done/Failed；Idle 隱藏；Running 附秒數防「當機了嗎」）
		+ SVerticalBox::Slot().AutoHeight().Padding(0, NiSpace::S, 0, 0)
		[
			SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Note))
				.Visibility_Lambda([this]()
				{
					UNiceInkPersonaSubsystem* P = Persona();
					return (P && P->GetIntakeState() != UNiceInkPersonaSubsystem::EFaceIntakeState::Idle)
						? EVisibility::Visible : EVisibility::Collapsed;
				})
				.ColorAndOpacity_Lambda([this]() -> FSlateColor
				{
					UNiceInkPersonaSubsystem* P = Persona();
					return (P && P->GetIntakeState() == UNiceInkPersonaSubsystem::EFaceIntakeState::Failed)
						? FSlateColor(NiHudColor::Red) : FSlateColor(NiHudColor::PaperDim);
				})
				.Text_Lambda([this]() -> FText
				{
					UNiceInkPersonaSubsystem* P = Persona();
					if (!P)
					{
						return FText::GetEmpty();
					}
					switch (P->GetIntakeState())
					{
					case UNiceInkPersonaSubsystem::EFaceIntakeState::Running:
						return FText::FromString(FString::Printf(TEXT("%s  %ds"),
							*LocS(ENiLocKey::FaceProcessing),
							FMath::FloorToInt32(static_cast<float>(P->GetIntakeElapsedS()))));
					case UNiceInkPersonaSubsystem::EFaceIntakeState::Done:    return Loc(ENiLocKey::FaceUpdated);
					case UNiceInkPersonaSubsystem::EFaceIntakeState::Failed:  return Loc(ENiLocKey::FaceFailed);
					default:                                                  return FText::GetEmpty();
					}
				})
		];
}

TSharedRef<SWidget> SNiMenu::BuildProfilePage()
{
	// 個人檔案（SPEC #52 v4.0e）：名字＋自拍＋現金的家；刺青直接看背景舞台
	// 的力士本人（雲端資產到貨即穿上，見 NiceInkMenuStage::DressDancerFromPersona）
	return SNew(SBox).Visibility_Lambda([this]() { return PageVis(EPage::Profile); })
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(NiMenuLeftGutter, NiSpace::BandTop, 0, 0)
		[
			SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Title)).ColorAndOpacity(NiHudColor::Paper)
				// 首啟創角＝同一頁換帽子：標題「創建你的力士」
				.Text_Lambda([this]()
				{
					return Up(Loc(bOnboarding ? ENiLocKey::CreateYourRikishi : ENiLocKey::Profile));
				})
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(NiMenuLeftGutter, NiSpace::XL, 0, 0)
		[
			SNew(SBox).WidthOverride(NiSpace::CardW)
			[
				SNew(SBackgroundBlur).BlurStrength(NiMenuCardBlur).CornerRadius(FVector4(18, 18, 18, 18))
				[
					SNew(SBorder).BorderImage(&PageGroundBrush).Padding(FMargin(NiMenuGroundPad))
					[
						// 兩模式各自組裝（建構時分支；創角完成＝RecreateMenu 重生日常版）：
						// 創角＝任務優先（必讀→上傳→名字）；日常＝線上=你是誰/你有什麼
						//（名字→現金）、線下=臉的管理區（臉庫→必讀→重新上傳＝同一件
						// 工具的狀態/規則/動作，2026-08-12 user 裁決不拆家）
						bOnboarding
						? StaticCastSharedRef<SWidget>(
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()[MakeProfileUploadBlock()]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, NiSpace::L, 0, 0)[MakeProfileNameBlock()])
						: StaticCastSharedRef<SWidget>(
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()[MakeProfileNameBlock()]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, NiSpace::M, 0, 0)[MakeProfileCashBlock()]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, NiSpace::L, 0, 0)
							[
								SNew(SBox).HeightOverride(1.0f)[SNew(SImage).Image(&CardDividerBrush)]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, NiSpace::L, 0, 0)[MakeProfileFacesBlock()]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, NiSpace::M, 0, 0)[MakeProfileUploadBlock()])
					]
				]
			]
		]
		+ SVerticalBox::Slot().FillHeight(1)[SNew(SSpacer)]
		// 底帶雙模式：平常＝返回；首啟創角＝文A（第一次開機可能要換語言）＋離開
		//（傳完臉自動進主選單＝不需要「完成」鈕；返回/ESC 在創角中不存在）
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(NiMenuLeftGutter, 0, 0, NiSpace::BandBottom + NiMenuBottomReserve)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(NiSpace::S, 0)
			[
				// Back 已歸左下 [ESC] Back（2026-09-05）——它會走 GoBack()，
				// 而 GoBack() 在 Profile 頁本來就會先 CommitName()。
				SNew(SSpacer)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(NiSpace::S, 0)
			[
				SNew(SBox).Visibility_Lambda([this]() { return bOnboarding ? EVisibility::Visible : EVisibility::Collapsed; })
				[
					MakeGhostButton(TEXT("文A"), [this]()
					{
						LangOrigin = EPage::Profile; // 語言頁返回＝回創角頁
						Page = EPage::Language;
					})
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(NiSpace::S, 0)
			[
				SNew(SBox).Visibility_Lambda([this]() { return bOnboarding ? EVisibility::Visible : EVisibility::Collapsed; })
				[
					SNew(SButton).ButtonStyle(&GhostStyle).IsFocusable(false)
						.ContentPadding(FMargin(26, 10))
						.OnClicked_Lambda([this]()
						{
							const double Now = FPlatformTime::Seconds();
							if (Now < QuitArmedUntil)
							{
								if (OwnerPC.IsValid())
								{
									UKismetSystemLibrary::QuitGame(OwnerPC.Get(), OwnerPC.Get(), EQuitPreference::Quit, false);
								}
							}
							else
							{
								QuitArmedUntil = Now + 3.0;
							}
							return FReply::Handled();
						})
					[
						SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::ActionSmall)).ColorAndOpacity(NiHudColor::Paper)
							.Text_Lambda([this]()
							{
								return FPlatformTime::Seconds() < QuitArmedUntil
									? Loc(ENiLocKey::ConfirmQuit) : Loc(ENiLocKey::Quit);
							})
					]
				]
			]
		]
	];
}

void SNiMenu::OpenProfilePage()
{
	Page = EPage::Profile;
	// 開頁播種名字欄（雲端偏好可能在建 UI 後才到）＋重建臉庫列。
	// 只播自訂名；保底/平台名走 hint 淡字（隨機名不冒充已取的名字）
	if (NameBox.IsValid() && GI())
	{
		NameBox->SetText(GI()->PlayerDisplayName.IsEmpty()
			? FText::GetEmpty() : FText::FromString(GI()->GetEffectiveDisplayName()));
	}
	RefreshFaceRow();
}

void SNiMenu::RefreshFaceRow()
{
	if (!FaceRowBox.IsValid())
	{
		return;
	}
	FaceRowBox->ClearChildren();
	FaceThumbBrushes.Reset();
	bFaceRowPending = false;

	UNiceInkPersonaSubsystem* P = Persona();
	if (!P)
	{
		return;
	}
	int32 Shown = 0;
	for (const FString& Id : P->ListLibraryFaceIds())
	{
		if (Shown >= 6)
		{
			break; // 版面上限：最新六張（更舊的仍在磁碟，未做翻頁）
		}
		// 縮圖尺寸（2026-08-12 user「太小」→ 56→84；wrap 列自動換行 4+2）
		constexpr float TilePx = 84.0f;
		UTexture* Thumb = P->GetFaceThumb(Id);
		TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateBrush>();
		Brush->ImageSize = FVector2D(TilePx, TilePx);
		if (Thumb)
		{
			Brush->SetResourceObject(Thumb); // GC 錨在 Persona 的 ThumbCache（UPROPERTY）
		}
		else
		{
			// 頭像亭暖機中或工件缺席＝素膚塊墊檔；Tick 稍後重試補烘
			bFaceRowPending = true;
			Brush->TintColor = FSlateColor(FLinearColor(0.62f, 0.55f, 0.48f, 1.0f));
		}
		FaceThumbBrushes.Add(Brush);

		FaceRowBox->AddSlot().Padding(0, 0, 8, 8)
		[
			SNew(SButton).ButtonStyle(&FaceTileStyle).IsFocusable(false)
				.ContentPadding(FMargin(3))
				.OnClicked_Lambda([this, Id]()
				{
					if (UNiceInkPersonaSubsystem* PP = Persona())
					{
						PP->ActivateFace(Id); // FaceRevision 遞增→舞台力士下一 tick 換臉
					}
					return FReply::Handled();
				})
			[
				// 頭形 icon＋穿著中金線在「頭像下方」（壓在下巴上=2026-08-12 抓錯）
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBox).WidthOverride(TilePx).HeightOverride(TilePx)
					[
						SNew(SImage).Image(Brush.Get())
					]
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 2, 0, 0)
				[
					SNew(SBox).WidthOverride(60).HeightOverride(3)
					[
						SNew(SImage).Image(&UnderlineBrush)
							.Visibility_Lambda([this, Id]()
							{
								UNiceInkPersonaSubsystem* PP = Persona();
								return (PP && PP->GetActiveFaceId() == Id)
									? EVisibility::Visible : EVisibility::Hidden;
							})
					]
				]
			]
		];
		++Shown;
	}
}

UNiceInkPersonaSubsystem* SNiMenu::Persona() const
{
	UNiceInkGameInstance* Inst = GI();
	return Inst ? Inst->GetSubsystem<UNiceInkPersonaSubsystem>() : nullptr;
}

bool SNiMenu::HasFace() const
{
	const UNiceInkPersonaSubsystem* P = Persona();
	return P && P->HasCustomFace();
}

void SNiMenu::PickSelfieAndIntake()
{
#if PLATFORM_WINDOWS
	// IFileOpenDialog（現代對話框、DPI 原生清晰；Shipping 可用）
	FString Picked;
	FWindowsPlatformMisc::CoInitialize();
	IFileOpenDialog* Dialog = nullptr;
	if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&Dialog))) && Dialog)
	{
		const COMDLG_FILTERSPEC Filters[] = { { L"Images", L"*.jpg;*.jpeg;*.png" } };
		Dialog->SetFileTypes(UE_ARRAY_COUNT(Filters), Filters);
		const void* Parent = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared());
		if (SUCCEEDED(Dialog->Show(reinterpret_cast<HWND>(const_cast<void*>(Parent)))))
		{
			IShellItem* Item = nullptr;
			if (SUCCEEDED(Dialog->GetResult(&Item)) && Item)
			{
				PWSTR Path = nullptr;
				if (SUCCEEDED(Item->GetDisplayName(SIGDN_FILESYSPATH, &Path)) && Path)
				{
					Picked = FString(Path);
					CoTaskMemFree(Path);
				}
				Item->Release();
			}
		}
		Dialog->Release();
	}
	if (!Picked.IsEmpty())
	{
		if (UNiceInkPersonaSubsystem* P = Persona())
		{
			P->BeginSelfieIntake(FPaths::ConvertRelativePathToFull(Picked));
		}
	}
#endif
}

FText SNiMenu::StatusText() const
{
	const UNiceInkSessionSubsystem* S = Sessions();
	if (!S)
	{
		return FText::GetEmpty();
	}
	switch (S->GetUiState())
	{
	case ENiSessionUiState::Hosting:   return Loc(ENiLocKey::StatusCreating);
	case ENiSessionUiState::Searching:
		// 背景自動更新＝靜音（不講「搜尋中」——列表掛著舊內容等新結果）
		return S->IsBackgroundSearching() ? FText::GetEmpty() : Loc(ENiLocKey::StatusLooking);
	case ENiSessionUiState::Joining:   return Loc(ENiLocKey::StatusJoining);
	case ENiSessionUiState::Failed:
	{
		const int32 Key = S->GetLastErrorKey();
		return Key >= 0
			? FText::FromString(NiLoc::TFmt(OwnerPC.Get(), static_cast<ENiLocKey>(Key), S->GetLastErrorParam()))
			: FText::FromString(S->GetLastError()); // 無鍵＝英文原文保底
	}
	default:                           return FText::GetEmpty();
	}
}

FSlateColor SNiMenu::StatusColor() const
{
	const UNiceInkSessionSubsystem* S = Sessions();
	return (S && S->GetUiState() == ENiSessionUiState::Failed) ? FSlateColor(NiHudColor::Red) : FSlateColor(NiHudColor::Amber);
}

void SNiMenu::RebuildRoomList()
{
	if (!RoomListBox.IsValid())
	{
		return;
	}
	RoomListBox->ClearChildren();
	UNiceInkSessionSubsystem* S = Sessions();
	if (!S)
	{
		return;
	}
	int32 Shown = 0;
	for (const FNiFoundSession& F : S->GetFoundSessions())
	{
		if (!F.bPublic)
		{
			continue; // 私房永不列出
		}
		// 語言過濾（顯示層＝LAN 的過濾點；EOS 已在查詢端過濾、此處恆過）：
		// 指定語言時，異語言與無屬性舊房都不上桌
		if (JoinLangFilter != INDEX_NONE && F.LangIndex != JoinLangFilter)
		{
			continue;
		}
		++Shown; // 無上限（規模版）：高度由捲動盒接手
		const int32 Taken = FMath::Clamp(F.MaxSlots - F.OpenSlots, 0, F.MaxSlots);
		const int32 JoinIndex = F.SearchIndex;
		const int32 MyLang = GI() ? GI()->GetMenuLanguage() : 0;
		const bool bDiffLang = F.LangIndex >= 0 && F.LangIndex < NiLoc::NumLangs && F.LangIndex != MyLang;
		// 人數點點：酒金實心=已入座、暗色空心=空位——「幾缺幾」掃一眼可讀
		FString DotsFilled, DotsEmpty;
		for (int32 D = 0; D < Taken; ++D) { DotsFilled += TEXT("●"); }
		for (int32 D = Taken; D < F.MaxSlots; ++D) { DotsEmpty += TEXT("○"); }
		RoomListBox->AddSlot().AutoHeight().Padding(0, NiSpace::XS)
		[
			SNew(SButton).ButtonStyle(&OnCardStyle).IsFocusable(false)
				.ContentPadding(FMargin(22, 12))
				// 可點判準＝「加入流程能否受理」而非「有沒有搜尋在跑」——
				// 串流讓房 0.2s 就上桌，前景搜尋窗內也要能點（JoinFoundSession
				// 受理 Searching；淡化 5 秒才能點=兩規則打架的舊病）
				.IsEnabled_Lambda([this]()
				{
					const UNiceInkSessionSubsystem* S = Sessions();
					if (!S) { return false; }
					const ENiSessionUiState St = S->GetUiState();
					return St != ENiSessionUiState::Joining && St != ENiSessionUiState::Hosting;
				})
				.OnClicked_Lambda([this, JoinIndex]()
				{
					CommitName();
					if (UNiceInkSessionSubsystem* Sub = Sessions()) { Sub->JoinFoundSession(JoinIndex); }
					return FReply::Handled();
				})
			[
				// 2026-08-14 列表 UX 重製（玩家掃視序：這房找誰→講什麼話→
				// 幾缺幾→ping）。臉制（SPEC #52）：房主名退場；房主臉像待
				// 自拍上雲後補（記帳）。
				SNew(SHorizontalBox)
				// 左欄：房名主行（無名公開房＝顯示房號——公開房任人可入、
				// 碼無私密性，反成可唸出口的身分錨；「不顯他房碼」規則的
				// 保護對象=私房，私房永不上列表＝意圖不變）＋異語言母語名副行
				+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Body)).ColorAndOpacity(NiHudColor::Paper)
							.Text(FText::FromString(F.RoomName.IsEmpty() ? F.Code : F.RoomName))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
					[
						SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Note)).ColorAndOpacity(NiHudColor::PaperDim)
							.Visibility(bDiffLang ? EVisibility::Visible : EVisibility::Collapsed)
							.Text(FText::FromString(bDiffLang ? NiLoc::LangNativeName(F.LangIndex) : FString()))
					]
				]
				// 右欄：人數點點＋ping 副行（右對齊＝數字欄語義）
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Body)).ColorAndOpacity(NiHudColor::Amber)
								.Text(FText::FromString(DotsFilled))
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Body)).ColorAndOpacity(NiHudColor::PaperDim)
								.Text(FText::FromString(DotsEmpty))
						]
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0, 2, 0, 0)
					[
						SNew(STextBlock).ShadowOffset(NiTextShadowOffset).ShadowColorAndOpacity(NiTextShadowColor).Font(Ty(NiType::Note)).ColorAndOpacity(NiHudColor::PaperDim)
							.Visibility(F.PingMs > 0 ? EVisibility::Visible : EVisibility::Collapsed)
							.Text(FText::FromString(FString::Printf(TEXT("%d ms"), F.PingMs)))
					]
				]
			]
		];
	}
	// （卡內空狀態退役 2026-08-12：沒房時整張卡收起、頁面上一行字代班）
}

void SNiMenu::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	// 換頁淡入（2026-09-05 中性制的動態）：0.2s smoothstep。只淡入不淡出——
	// 上一頁在 Visibility 切換的同一幀就 Collapsed，淡出需要延後隱藏，不值得一層狀態機。
	if (Page != LastPageSeen)
	{
		LastPageSeen = Page;
		PageChangedAt = InCurrentTime;
	}
	{
		const float T = FMath::Clamp(static_cast<float>((InCurrentTime - PageChangedAt) / 0.2), 0.0f, 1.0f);
		const float A = (PageChangedAt < 0.0) ? 1.0f : T * T * (3.0f - 2.0f * T);
		for (int32 i = 0; i < UE_ARRAY_COUNT(PageRoots); ++i)
		{
			if (PageRoots[i].IsValid())
			{
				PageRoots[i]->SetRenderOpacity(i == static_cast<int32>(Page) ? A : 1.0f);
			}
		}
	}

	// 個人檔案頁：換臉/上傳完成（FaceRevision 變動）＝重建臉庫列；
	// 頭像亭暖機期間的縮圖每 0.5s 補烘重試
	if (Page == EPage::Profile)
	{
		UNiceInkPersonaSubsystem* P = Persona();
		const int32 Rev = P ? P->GetFaceRevision() : 0;
		if (Rev != LastFaceRowRev)
		{
			LastFaceRowRev = Rev;
			RefreshFaceRow();
		}
		else if (bFaceRowPending && InCurrentTime >= NextFaceRowRetry)
		{
			NextFaceRowRetry = InCurrentTime + 0.5;
			bFaceRowPending = false;
			RefreshFaceRow();
		}
	}

	// 首啟創角：臉一到手＝自動進主選單（你的力士戴著你的臉在跳舞＝第一印象）；
	// 選單重建成日常版面（RecreateMenu 內部延遲一 tick＝Tick 內呼叫安全）
	if (bOnboarding && HasFace())
	{
		bOnboarding = false;
		Page = EPage::Root;
		if (OwnerPC.IsValid())
		{
			if (ANiceInkMenuHUD* Hud = Cast<ANiceInkMenuHUD>(OwnerPC->GetHUD()))
			{
				Hud->RecreateMenu(/*bOpenSettings=*/false);
			}
		}
	}

	if (Page == EPage::Join)
	{
		if (!bSearchKicked)
		{
			if (UNiceInkSessionSubsystem* S = Sessions())
			{
				S->SearchSessions(IsLan(), JoinLangFilter);
				bSearchKicked = true;
				NextAutoSearchTime = InCurrentTime + 12.0;
			}
		}
		// 列表自動更新（2026-08-14 列表 UX）：留在頁上每 12s 背景重搜——
		// 陳舊列表=按了進不去。打房號中（CodeBuffer 非空）＝走碼路不干擾；
		// 撞上手動搜尋/加入中＝跳過（JoinRoomByCode 另有搭便車護欄）
		else if (CodeBuffer.IsEmpty() && InCurrentTime >= NextAutoSearchTime)
		{
			NextAutoSearchTime = InCurrentTime + 12.0;
			if (UNiceInkSessionSubsystem* S = Sessions())
			{
				if (S->GetUiState() == ENiSessionUiState::Idle)
				{
					S->SearchSessions(IsLan(), JoinLangFilter, /*bBackground=*/true);
				}
			}
		}
		// 房列表：內容戳記變了才重建（每 0.5s 檢查一次）
		if (InCurrentTime >= NextListRebuildTime)
		{
			NextListRebuildTime = InCurrentTime + 0.5;
			int32 Stamp = 0;
			if (const UNiceInkSessionSubsystem* S = Sessions())
			{
				for (const FNiFoundSession& F : S->GetFoundSessions())
				{
					Stamp = Stamp * 31 + GetTypeHash(F.OwnerName) + F.OpenSlots + (F.bPublic ? 7 : 0)
						+ F.LangIndex * 13 + GetTypeHash(F.RoomName) + GetTypeHash(F.Code);
				}
				Stamp = Stamp * 31 + JoinLangFilter; // 換過濾值＝重建（LAN 顯示層過濾）
				Stamp = Stamp * 31 + S->GetFoundSessions().Num();
			}
			if (Stamp != LastListStamp)
			{
				LastListStamp = Stamp;
				RebuildRoomList();
			}
		}
	}

	if (!bSettingsSeeded && Page == EPage::Settings)
	{
		if (const UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr)
		{
			// 只剩無邊框/視窗二態；非視窗一律顯示為無邊框
			PendingWindowMode = GUS->GetFullscreenMode() == EWindowMode::Windowed ? 2 : 1;
		}
		bSettingsSeeded = true;
	}
}

FReply SNiMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();

	// ESC＝與「返回」同一條路（同一個意圖不得有兩個目的地）：
	// 授權頁回設定、語言頁回出發頁、個人檔案頁存名字；
	// 首啟創角＝無路可退（強制上傳制），語言頁除外（LangOrigin=創角頁）
	if (Key == EKeys::Escape && Page != EPage::Root)
	{
		GoBack();
		return FReply::Handled();
	}

	if (Page == EPage::Join)
	{
		const FString KeyStr = Key.ToString();
		if (KeyStr.Len() == 1 && FChar::IsAlpha(KeyStr[0]) && CodeBuffer.Len() < 4)
		{
			CodeBuffer.AppendChar(FChar::ToUpper(KeyStr[0]));
			return FReply::Handled();
		}
		if (Key == EKeys::BackSpace && CodeBuffer.Len() > 0)
		{
			CodeBuffer.LeftChopInline(1);
			return FReply::Handled();
		}
		if (Key == EKeys::Enter && CodeBuffer.Len() == 4 && !IsBusy())
		{
			CommitName();
			if (UNiceInkSessionSubsystem* S = Sessions()) { S->JoinRoomByCode(CodeBuffer, IsLan()); }
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}
