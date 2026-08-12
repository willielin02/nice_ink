#include "NiceInkMenuWidget.h"

#include "NiceInkLocText.h"
#include "NiceInkMenuHUD.h"

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
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SUniformGridPanel.h"
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

	FLinearColor WithA(FLinearColor C, float A) { C.A = A; return C; }
}

FText SNiMenu::Loc(ENiLocKey Key) const
{
	return FText::FromString(NiLoc::T(OwnerPC.Get(), Key));
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

bool SNiMenu::IsBusy() const
{
	const UNiceInkSessionSubsystem* S = Sessions();
	if (!S)
	{
		return false;
	}
	const ENiSessionUiState St = S->GetUiState();
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

	// AR 版面鏡像（v4.0e）：流向跟文化——SetCurrentCulture("ar") 時 Slate 整樹
	// 自動鏡像（SHorizontalBox 逆序/HAlign 翻面/命中判定跟著走）；換語言重建
	// 選單時以新文化重算。房碼格等 LTR 記號在各自子樹釘回 LeftToRight。
	SetFlowDirectionPreference(EFlowDirectionPreference::Culture);

	if (UNiceInkGameInstance* Inst = GI())
	{
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

	// --- 樣式庫（白卡制）---
	CardBrush = MakeRounded(WithA(NiHudColor::Paper, 0.78f), 18.0f);           // 毛玻璃白卡
	SlotBrush = MakeRounded(WithA(NiHudColor::Paper, 0.86f), 12.0f);           // 房碼格
	RuleBrush = MakeRounded(NiHudColor::Amber, 2.0f);                          // 標題下短劃
	DividerBrush = MakeRounded(WithA(NiHudColor::Paper, 0.14f), 1.0f);
	UnderlineBrush = MakeRounded(NiHudColor::Amber, 1.5f);

	PrimaryStyle = MakeButtonStyle(NiHudColor::Amber,
		FLinearColor::LerpUsingHSV(NiHudColor::Amber, FLinearColor::White, 0.18f),
		FLinearColor::LerpUsingHSV(NiHudColor::Amber, FLinearColor::Black, 0.12f), 12.0f);
	OnCardStyle = MakeButtonStyle(WithA(NiHudColor::Ink, 0.07f), WithA(NiHudColor::Ink, 0.15f),
		WithA(NiHudColor::Ink, 0.22f), 12.0f);
	GhostStyle = MakeButtonStyle(WithA(NiHudColor::Paper, 0.08f), WithA(NiHudColor::Paper, 0.20f),
		WithA(NiHudColor::Paper, 0.28f), 10.0f);
	RowStyle = MakeButtonStyle(WithA(NiHudColor::Paper, 0.82f), WithA(NiHudColor::Paper, 0.95f),
		WithA(NiHudColor::Paper, 0.70f), 12.0f);
	// 臉庫縮圖：平常完全無底（頭形 icon 不被方塊裱框——2026-08-12 user 抓
	//「改了貼圖沒拆底板＝還是方形」）、hover/按下才微亮
	FaceTileStyle = MakeButtonStyle(WithA(NiHudColor::Ink, 0.0f), WithA(NiHudColor::Ink, 0.08f),
		WithA(NiHudColor::Ink, 0.14f), 10.0f);
	ChipOnBrush = MakeRounded(WithA(NiHudColor::Ink, 0.85f), 10.0f);
	ChipOffBrush = MakeRounded(WithA(NiHudColor::Ink, 0.05f), 10.0f);
	InsetBrush = MakeRounded(WithA(NiHudColor::Ink, 0.05f), 14.0f);
	CardDividerBrush = MakeRounded(WithA(NiHudColor::Ink, 0.12f), 1.0f);

	// 首啟創角判定（2026-08-12）：要在建 UI 之前算——個人檔案卡的版面
	// 依模式在建構時分支（創角=任務優先／日常=你的東西在上）
	bOnboarding = !HasFace();

	NameBoxStyle = FEditableTextBoxStyle()
		.SetBackgroundImageNormal(MakeRounded(WithA(NiHudColor::Ink, 0.06f), 10.0f))
		.SetBackgroundImageHovered(MakeRounded(WithA(NiHudColor::Ink, 0.10f), 10.0f))
		.SetBackgroundImageFocused(MakeRounded(WithA(NiHudColor::Ink, 0.10f), 10.0f))
		.SetBackgroundImageReadOnly(MakeRounded(WithA(NiHudColor::Ink, 0.04f), 10.0f))
		.SetTextStyle(FTextBlockStyle().SetFont(Ty(NiType::Body)).SetColorAndOpacity(NiHudColor::Ink))
		.SetFont(Ty(NiType::Body))
		.SetForegroundColor(NiHudColor::Ink)
		.SetFocusedForegroundColor(NiHudColor::Ink)
		.SetPadding(FMargin(14, 12));

	FString Version;
	GConfig->GetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"), TEXT("ProjectVersion"), Version, GGameIni);

	ChildSlot
	[
		SNew(SOverlay)

		// 七頁疊放、Visibility 輪詢切換
		+ SOverlay::Slot()[BuildRootPage()]
		+ SOverlay::Slot()[BuildHostPage()]
		+ SOverlay::Slot()[BuildJoinPage()]
		+ SOverlay::Slot()[BuildSettingsPage()]
		+ SOverlay::Slot()[BuildCreditsPage()]
		+ SOverlay::Slot()[BuildLanguagePage()]
		+ SOverlay::Slot()[BuildProfilePage()]

		// 版本戳（右下、極低調——資訊存在但不參與畫面）
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(0, 0, 16, 10)
		[
			SNew(STextBlock).Font(Ty(NiType::Micro))
				.ColorAndOpacity(FLinearColor(NiHudColor::PaperDim.R, NiHudColor::PaperDim.G, NiHudColor::PaperDim.B, 0.5f))
				.Text(FText::FromString(FString::Printf(TEXT("v%s"), *Version)))
		]
	];

	// 首啟創角：無臉＝開機直進「創建你的力士」頁（走 OpenProfilePage＝
	// 名字欄照常播種）；臉一到手 Tick 自動轉入主選單
	if (bOnboarding)
	{
		OpenProfilePage();
	}
}

TSharedRef<SWidget> SNiMenu::MakeGhostButton(const FString& Label, TFunction<void()> OnClick)
{
	return SNew(SButton)
		.ButtonStyle(&GhostStyle)
		.IsFocusable(false)
		.ContentPadding(FMargin(26, 10))
		.OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
		[
			SNew(STextBlock).Font(Ty(NiType::ActionSmall)).ColorAndOpacity(NiHudColor::Paper)
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
		.Padding(FMargin(0, 9))
		.OnMouseButtonDown_Lambda([this, bPublicValue](const FGeometry&, const FPointerEvent&)
			{ bPublicRoom = bPublicValue; return FReply::Handled(); })
		[
			SNew(STextBlock).Font(Ty(NiType::Body))
				.ColorAndOpacity_Lambda([this, bPublicValue]()
					{ return bPublicRoom == bPublicValue ? FSlateColor(NiHudColor::Paper) : FSlateColor(NiHudColor::InkDim); })
				.Text(FText::FromString(Label))
		];
}

TSharedRef<SWidget> SNiMenu::BuildRootPage()
{

	// 垂直三帶構圖（2026-08-06 user 打回「擠上方+下方真空」後的全域構圖）：
	// 標題=頂帶、卡片=FillHeight 撐開真置中、次要動作列=下錨貼底——
	// 負空間三帶各有其主，AutoHeight 疊頂的「內容決定排版」病根拆除
	return SNew(SBox).Visibility_Lambda([this]() { return PageVis(EPage::Root); })
	[
		SNew(SVerticalBox)

		// --- 頂帶：標題（明朝體黑字重＝墨字性格；tagline/短劃＝零功能已刪）
		// 全站骨架（2026-08-11 統一）：標題帶 top 56／內容置中／底帶錨 bottom 56 ---
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, NiSpace::BandTop, 0, 0)
		[
			SNew(STextBlock).Font(Ty(NiType::Display)).ColorAndOpacity(NiHudColor::Paper)
				.Text(FText::FromString(TEXT("NICE INK")))
		]

		// 斷線原因（有才顯示；host/join 時清除）
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, NiSpace::M, 0, 0)
		[
			SNew(STextBlock).Font(Ty(NiType::Body)).ColorAndOpacity(NiHudColor::Red)
				.Visibility_Lambda([this]() { return ErrorBanner.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
				.Text_Lambda([this]() { return FText::FromString(ErrorBanner); })
		]

		// 字體取樣行（robo NiMenuFontSample 專用；平時空=隱藏）
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, NiSpace::M, 0, 0)
		[
			SNew(STextBlock).Font(Ty(NiType::Value)).ColorAndOpacity(NiHudColor::Paper)
				.Visibility_Lambda([this]() { return FontSample.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
				.Text_Lambda([this]() { return FText::FromString(FontSample); })
		]

		// --- 中帶：毛玻璃白卡（FillHeight 上下撐開＝真垂直置中）---
		+ SVerticalBox::Slot().FillHeight(1)[SNew(SSpacer)]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			SNew(SBox).WidthOverride(NiSpace::CardW)
			[
				SNew(SBackgroundBlur).BlurStrength(14)
				.CornerRadius(FVector4(18, 18, 18, 18))
				[
					SNew(SBorder).BorderImage(&CardBrush).Padding(FMargin(NiSpace::CardPadH, NiSpace::CardPadV))
					[
						SNew(SVerticalBox)

						// 身分＝名字＋臉並列（SPEC #52 v4.0e）：名字欄住個人檔案頁、
						// 舞台上跳舞的就是你；主卡只留動作。
						// 2026-08-11 兩步流（user 裁決「頂層只放動詞」）：兩顆同級鈕
						// 緊貼相鄰、中間零內容；開房設定（可見性）移進自己的小頁
						// ——與「加入房間→輸碼頁」對稱，選項活在流程裡不活在門口。
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SButton).ButtonStyle(&PrimaryStyle).IsFocusable(false)
								.HAlign(HAlign_Center).VAlign(VAlign_Center)
								.ContentPadding(FMargin(0.0f, NiSpace::BtnPadV))
								.IsEnabled_Lambda([this]() { return !IsBusy(); })
								.OnClicked_Lambda([this]()
								{
									// 臉閘門退役（2026-08-12 首啟導流）：無臉根本進不到
									// 主選單＝這裡永遠有臉
									CommitName();
									ErrorBanner.Reset();
									Page = EPage::Host;
									return FReply::Handled();
								})
							[
								SNew(STextBlock).Font(Ty(NiType::Action)).ColorAndOpacity(NiHudColor::Ink)
									.Text(Loc(ENiLocKey::HostARoom))
							]
						]

						+ SVerticalBox::Slot().AutoHeight().Padding(0, NiSpace::M, 0, 0)
						[
							SNew(SButton).ButtonStyle(&PrimaryStyle).IsFocusable(false)
								.HAlign(HAlign_Center).VAlign(VAlign_Center)
								.ContentPadding(FMargin(0.0f, NiSpace::BtnPadV))
								.IsEnabled_Lambda([this]() { return !IsBusy(); })
								.OnClicked_Lambda([this]()
								{
									CommitName();
									ErrorBanner.Reset();
									Page = EPage::Join;
									CodeBuffer.Reset();
									bSearchKicked = false;
									FSlateApplication::Get().SetAllUserFocus(AsShared());
									return FReply::Handled();
								})
							[
								SNew(STextBlock).Font(Ty(NiType::Action)).ColorAndOpacity(NiHudColor::Ink)
									.Text(Loc(ENiLocKey::JoinARoom))
							]
						]

						// （臉的門檻預告 2026-08-12 隨首啟導流退役——無臉進不到這頁，
						// 需要用字解釋的流程就是錯的流程；只留登入預告一行）
						+ SVerticalBox::Slot().AutoHeight().Padding(0, NiSpace::S, 0, 0)
						[
							SNew(STextBlock).Font(Ty(NiType::Note)).ColorAndOpacity(NiHudColor::InkDim).AutoWrapText(true)
								.Visibility_Lambda([this]()
								{
									const UNiceInkSessionSubsystem* S = Sessions();
									return (!IsLan() && S && !S->IsLoggedIn())
										? EVisibility::Visible : EVisibility::Collapsed;
								})
								.Text(Loc(ENiLocKey::SignInBrowserNote))
						]
					]
				]
			]
		]

		// 狀態行（creating/looking/joining/failed）＋進行中取消鈕（狀態要有出口）
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, NiSpace::M, 0, 0)
		[
			MakeStatusRow()
		]

		// --- 底帶：次要動作下錨貼底（licenses 併入 settings＝主選單少一顆鈕）---
		+ SVerticalBox::Slot().FillHeight(1)[SNew(SSpacer)]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, NiSpace::BandBottom)
		[
			SNew(SHorizontalBox)
			// 個人檔案（SPEC #52 v4.0e：名字＋自拍＋現金＋刺青的家）
			+ SHorizontalBox::Slot().AutoWidth().Padding(NiSpace::S, 0)
			[
				MakeGhostButton(LocS(ENiLocKey::Profile), [this]() { OpenProfilePage(); })
			]
			// 「文A」＝語言入口（Google 式語言符號、不依賴任何語言的文字——
			// 看不懂當前語言的玩家也找得到；2026-08-06 迷路窘境調查後補）
			+ SHorizontalBox::Slot().AutoWidth().Padding(NiSpace::S, 0)
			[
				MakeGhostButton(TEXT("文A"), [this]()
				{
					LangOrigin = EPage::Root; // 從哪進、返回就回哪（導航對稱）
					Page = EPage::Language;
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(NiSpace::S, 0)
			[
				MakeGhostButton(LocS(ENiLocKey::SettingsBtn), [this]()
				{
					bSettingsSeeded = false;
					Page = EPage::Settings;
				})
			]
			// 離開＝毀滅性動作：跟日常鈕拉開距離＋二段確認（手滑不再直接關遊戲）
			+ SHorizontalBox::Slot().AutoWidth().Padding(40, 0, 8, 0)
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
					SNew(STextBlock).Font(Ty(NiType::ActionSmall)).ColorAndOpacity(NiHudColor::Paper)
						.Text_Lambda([this]()
						{
							return FPlatformTime::Seconds() < QuitArmedUntil
								? Loc(ENiLocKey::ConfirmQuit) : Loc(ENiLocKey::Quit);
						})
				]
			]
		]
	];
}

// 狀態行＋取消鈕（Root/Join 共用）：hosting/searching/joining 進行中給一個出口
TSharedRef<SWidget> SNiMenu::MakeStatusRow()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock).Font(Ty(NiType::Body))
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

TSharedRef<SWidget> SNiMenu::BuildHostPage()
{
	// 開房設定步（2026-08-11 user 裁決兩步流「頂層只放動詞」）：
	// 主卡按開房間→這裡選可見性→確認開房——與「加入房間→輸碼頁」對稱，
	// 開房的選項活在開房的流程裡，不再夾在兩個同級入口中間
	return SNew(SBox).Visibility_Lambda([this]() { return PageVis(EPage::Host); })
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, NiSpace::BandTop, 0, 0)
		[
			SNew(STextBlock).Font(Ty(NiType::Title)).ColorAndOpacity(NiHudColor::Paper)
				.Text(Loc(ENiLocKey::HostARoom))
		]
		+ SVerticalBox::Slot().FillHeight(1)[SNew(SSpacer)]

		// 可見性二選（白卡承載——chips 是墨色系、裸放黑背景讀不到）
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			SNew(SBox).WidthOverride(NiSpace::CardW)
			[
				SNew(SBackgroundBlur).BlurStrength(14).CornerRadius(FVector4(18, 18, 18, 18))
				[
					SNew(SBorder).BorderImage(&CardBrush).Padding(FMargin(NiSpace::CardPadH, NiSpace::CardPadV))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock).Font(Ty(NiType::Label)).ColorAndOpacity(NiHudColor::InkDim)
								.Text(Loc(ENiLocKey::RoomVisibility))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, NiSpace::S, 0, 0)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1)[MakeChip(LocS(ENiLocKey::InviteOnly), false)]
							+ SHorizontalBox::Slot().AutoWidth()[SNew(SSpacer).Size(FVector2D(6, 1))]
							+ SHorizontalBox::Slot().FillWidth(1)[MakeChip(LocS(ENiLocKey::PublicRoom), true)]
						]
						// 當前選項的後果說明（第一次玩也能預期）
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, NiSpace::S, 0, 0)
						[
							SNew(STextBlock).Font(Ty(NiType::Note)).ColorAndOpacity(NiHudColor::InkDim)
								.Text_Lambda([this]()
								{
									return Loc(bPublicRoom ? ENiLocKey::PublicDesc : ENiLocKey::InviteOnlyDesc);
								})
						]
					]
				]
			]
		]

		// 確認開房
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, NiSpace::L, 0, 0)
		[
			SNew(SBox).WidthOverride(332)
			[
				SNew(SButton).ButtonStyle(&PrimaryStyle).IsFocusable(false)
					.HAlign(HAlign_Center).VAlign(VAlign_Center)
					.ContentPadding(FMargin(0.0f, NiSpace::BtnPadV))
					.IsEnabled_Lambda([this]() { return !IsBusy(); })
					.OnClicked_Lambda([this]()
					{
						ErrorBanner.Reset();
						if (UNiceInkSessionSubsystem* S = Sessions()) { S->HostSession(IsLan(), bPublicRoom); }
						return FReply::Handled();
					})
				[
					SNew(STextBlock).Font(Ty(NiType::Action)).ColorAndOpacity(NiHudColor::Ink)
						.Text(Loc(ENiLocKey::HostARoom))
				]
			]
		]

		// 狀態/錯誤貼著動作＋進行中取消鈕
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, NiSpace::M, 0, 0)
		[
			MakeStatusRow()
		]

		+ SVerticalBox::Slot().FillHeight(1)[SNew(SSpacer)]

		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, NiSpace::BandBottom)
		[
			MakeGhostButton(LocS(ENiLocKey::Back), [this]() { Page = EPage::Root; })
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
			SNew(SBox).WidthOverride(74).HeightOverride(88)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()[SNew(SImage).Image(&SlotBrush)]
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Font(Ty(NiType::Title)).ColorAndOpacity(NiHudColor::Ink)
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

		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, NiSpace::BandTop, 0, 0)
		[
			SNew(STextBlock).Font(Ty(NiType::Title)).ColorAndOpacity(NiHudColor::Paper)
				.Text(Loc(ENiLocKey::JoinARoom))
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, NiSpace::S, 0, 0)
		[
			SNew(STextBlock).Font(Ty(NiType::Body)).ColorAndOpacity(NiHudColor::PaperDim)
				.Text(Loc(ENiLocKey::AskHostCode))
		]
		+ SVerticalBox::Slot().FillHeight(1)[SNew(SSpacer)]

		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			Slots
		]

		// 輸入方式要被告知：房號＝直接敲鍵盤（格子不可點、沒有這行就只能猜）
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, NiSpace::S, 0, 0)
		[
			SNew(STextBlock).Font(Ty(NiType::Note)).ColorAndOpacity(NiHudColor::PaperDim)
				.Text(Loc(ENiLocKey::TypeCodeHint))
		]

		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, NiSpace::M, 0, 0)
		[
			SNew(SBox).WidthOverride(332)
			[
				SNew(SButton).ButtonStyle(&PrimaryStyle).IsFocusable(false)
					.HAlign(HAlign_Center).VAlign(VAlign_Center)
					.ContentPadding(FMargin(0.0f, NiSpace::BtnPadV))
					.IsEnabled_Lambda([this]() { return !IsBusy() && CodeBuffer.Len() == 4; })
					.OnClicked_Lambda([this]()
					{
						CommitName();
						if (UNiceInkSessionSubsystem* S = Sessions()) { S->JoinRoomByCode(CodeBuffer, IsLan()); }
						return FReply::Handled();
					})
				[
					SNew(STextBlock).Font(Ty(NiType::Action)).ColorAndOpacity(NiHudColor::Ink)
						.Text(Loc(ENiLocKey::JoinWithCode))
				]
			]
		]

		// 狀態/錯誤緊貼動作（輸錯房號的訊息不再沉到螢幕底）＋進行中取消鈕
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, NiSpace::M, 0, 0)
		[
			MakeStatusRow()
		]

		// 公開房：沒房＝一行字（空盒子不上桌）、有房才展開白卡列表
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, NiSpace::L, 0, 0)
		[
			SNew(SHorizontalBox)
			.Visibility_Lambda([this]()
			{
				const UNiceInkSessionSubsystem* S = Sessions();
				if (!S || S->GetUiState() != ENiSessionUiState::Idle)
				{
					return EVisibility::Collapsed; // 搜尋中＝狀態行在講話
				}
				for (const FNiFoundSession& F : S->GetFoundSessions())
				{
					if (F.bPublic) { return EVisibility::Collapsed; }
				}
				return EVisibility::Visible;
			})
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).Font(Ty(NiType::Note)).ColorAndOpacity(NiHudColor::PaperDim)
					.Text(Loc(ENiLocKey::NoPublicRooms))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(NiSpace::M, 0, 0, 0)
			[
				MakeGhostButton(LocS(ENiLocKey::Refresh), [this]()
				{
					if (UNiceInkSessionSubsystem* S = Sessions()) { S->SearchSessions(IsLan()); }
				})
			]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			SNew(SBox).WidthOverride(NiSpace::CardWWide)
				.Visibility_Lambda([this]()
				{
					const UNiceInkSessionSubsystem* S = Sessions();
					if (!S) { return EVisibility::Collapsed; }
					for (const FNiFoundSession& F : S->GetFoundSessions())
					{
						if (F.bPublic) { return EVisibility::Visible; }
					}
					return EVisibility::Collapsed;
				})
			[
				SNew(SBackgroundBlur).BlurStrength(14).CornerRadius(FVector4(16, 16, 16, 16))
				[
					SNew(SBorder).BorderImage(&CardBrush).Padding(FMargin(NiSpace::CardPadH, NiSpace::CardPadV))
					[
						SNew(SVerticalBox)
						// 標籤列＋重新整理（刷新鈕住在它管的列表旁＝歸屬歸位）
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
							[
								SNew(STextBlock).Font(Ty(NiType::Label)).ColorAndOpacity(NiHudColor::InkDim)
									.Text(Loc(ENiLocKey::PublicRooms))
							]
							+ SHorizontalBox::Slot().AutoWidth()
							[
								SNew(SButton).ButtonStyle(&OnCardStyle).IsFocusable(false)
									.ContentPadding(FMargin(14, 4))
									.IsEnabled_Lambda([this]() { return !IsBusy(); })
									.OnClicked_Lambda([this]()
									{
										if (UNiceInkSessionSubsystem* S = Sessions()) { S->SearchSessions(IsLan()); }
										return FReply::Handled();
									})
								[
									SNew(STextBlock).Font(Ty(NiType::Note)).ColorAndOpacity(NiHudColor::Ink)
										.Text(Loc(ENiLocKey::Refresh))
								]
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, NiSpace::S, 0, 0)
						[
							SAssignNew(RoomListBox, SVerticalBox)
						]
					]
				]
			]
		]

		+ SVerticalBox::Slot().FillHeight(1)[SNew(SSpacer)]

		// 底帶只剩導航（狀態行已上移貼動作、刷新已歸列表卡）
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, NiSpace::BandBottom)
		[
			MakeGhostButton(LocS(ENiLocKey::Back), [this]() { Page = EPage::Root; })
		]
	];
}

TSharedRef<SWidget> SNiMenu::BuildSettingsPage()
{
	// Enabled：整列可用性（無邊框下解析度＝死旋鈕→停用＋說明，因果不再斷）
	auto MakeArrow = [this](const FString& Glyph, TFunction<void()> OnClick,
		TFunction<bool()> Enabled) -> TSharedRef<SWidget>
	{
		return SNew(SButton).ButtonStyle(&OnCardStyle).IsFocusable(false)
			.HAlign(HAlign_Center).VAlign(VAlign_Center)
			.ContentPadding(FMargin(14, 6))
			.IsEnabled_Lambda([Enabled]() { return !Enabled || Enabled(); })
			.OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
			[
				SNew(STextBlock).Font(Ty(NiType::Body, /*bBold=*/true)).ColorAndOpacity(NiHudColor::Ink)
					.Text(FText::FromString(Glyph))
			];
	};
	auto MakeRow = [this, &MakeArrow](const FString& Label, TFunction<FString()> Value,
		TFunction<void()> OnLeft, TFunction<void()> OnRight,
		TFunction<bool()> Enabled = nullptr) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Font(Ty(NiType::Body)).ColorAndOpacity(NiHudColor::InkDim)
					.Text(FText::FromString(Label))
			]
			+ SHorizontalBox::Slot().AutoWidth()[MakeArrow(TEXT("<"), OnLeft, Enabled)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(150)
				[
					SNew(STextBlock).Font(Ty(NiType::Body)).Justification(ETextJustify::Center)
						.ColorAndOpacity_Lambda([Enabled]() -> FSlateColor
						{
							return (!Enabled || Enabled()) ? FSlateColor(NiHudColor::Ink) : FSlateColor(NiHudColor::InkDim);
						})
						.Text_Lambda([Value]() { return FText::FromString(Value()); })
				]
			]
			+ SHorizontalBox::Slot().AutoWidth()[MakeArrow(TEXT(">"), OnRight, Enabled)];
	};

	return SNew(SBox).Visibility_Lambda([this]() { return PageVis(EPage::Settings); })
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, NiSpace::BandTop, 0, 0)
		[
			SNew(STextBlock).Font(Ty(NiType::Title)).ColorAndOpacity(NiHudColor::Paper)
				.Text(Loc(ENiLocKey::SettingsBtn))
		]
		+ SVerticalBox::Slot().FillHeight(1)[SNew(SSpacer)]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			SNew(SBox).WidthOverride(NiSpace::CardWWide)
			[
				SNew(SBackgroundBlur).BlurStrength(14).CornerRadius(FVector4(18, 18, 18, 18))
				[
					SNew(SBorder).BorderImage(&CardBrush).Padding(FMargin(NiSpace::CardPadH, NiSpace::CardPadV))
					[
						SNew(SVerticalBox)

						// --- 全頁單一模型（2026-08-11 user 定案）：改了就生效、就存檔。
						// 視窗模式＝無邊框/視窗二態即點即切（同為合成器視窗＝不經顯示器
						// 重同步、不黑閃）；視窗＝瀏覽器式可拖拉（引擎原生 bAllowWindowResize）。
						// 獨占全螢幕與解析度選單退役——效能旋鈕改渲染比例（零切換零延遲）---
						+ SVerticalBox::Slot().AutoHeight().Padding(0, NiSpace::XS)
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
						+ SVerticalBox::Slot().AutoHeight().Padding(0, NiSpace::XS)
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

						+ SVerticalBox::Slot().AutoHeight().Padding(0, NiSpace::XS)
						[
							MakeRow(LocS(ENiLocKey::MouseSensitivity),
								[this]() { const UNiceInkGameInstance* I = GI(); return FString::Printf(TEXT("%.1f"), I ? I->MouseSensitivityScale : 1.0f); },
								[this]() { if (UNiceInkGameInstance* I = GI()) { I->MouseSensitivityScale = FMath::Clamp(I->MouseSensitivityScale - 0.1f, 0.2f, 3.0f); I->SaveSettings(); } },
								[this]() { if (UNiceInkGameInstance* I = GI()) { I->MouseSensitivityScale = FMath::Clamp(I->MouseSensitivityScale + 0.1f, 0.2f, 3.0f); I->SaveSettings(); } })
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, NiSpace::XS)
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
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, NiSpace::BandBottom)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(NiSpace::S, 0)
			[
				MakeGhostButton(LocS(ENiLocKey::Licenses), [this]() { Page = EPage::Credits; })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(NiSpace::S, 0)
			[
				MakeGhostButton(LocS(ENiLocKey::Back), [this]() { Page = EPage::Root; })
			]
		]
	];
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
		TEXT("M PLUS Rounded 1c font — (c) M+ FONTS PROJECT, SIL Open Font License 1.1"),
		TEXT("Zen Old Mincho font — (c) Yoshimichi Ohira, SIL Open Font License 1.1"),
		TEXT("GenRyuMin font — (c) ButTaiwan / Source Han Serif, SIL Open Font License 1.1"),
		TEXT("Noto Serif family & Noto Naskh Arabic — (c) The Noto Project Authors, SIL OFL 1.1"),
		TEXT("icons adapted from game-icons.net — CC BY 3.0"),
		TEXT("\"Sauna\" 3d scene by local.yany (sketchfab) — CC BY 4.0"),
		TEXT(""),
		TEXT("full license texts: THIRD_PARTY_NOTICES.md next to the game executable"),
	};
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	Box->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, NiSpace::BandTop, 0, 0)
	[
		SNew(STextBlock).Font(Ty(NiType::Title)).ColorAndOpacity(NiHudColor::Paper)
			.Text(Loc(ENiLocKey::Licenses))
	];
	Box->AddSlot().FillHeight(1)[SNew(SSpacer)];
	for (const TCHAR* Line : Lines)
	{
		Box->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, NiSpace::XS)
		[
			SNew(STextBlock).Font(Ty(NiType::Body)).ColorAndOpacity(NiHudColor::Paper)
				.Text(FText::FromString(Line))
		];
	}
	Box->AddSlot().FillHeight(1)[SNew(SSpacer)];
	Box->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, NiSpace::BandBottom)
	[
		MakeGhostButton(LocS(ENiLocKey::Back), [this]() { Page = EPage::Settings; })
	];
	return SNew(SBox).Visibility_Lambda([this]() { return PageVis(EPage::Credits); })[Box];
}

TSharedRef<SWidget> SNiMenu::BuildLanguagePage()
{
	// 13 語母語名全列（兩欄網格）：任何語言狀態下玩家都能一眼掃到自己的母語——
	// 「看不懂就到不了設定」窘境的正解（2026-08-06 業界慣例調查）
	TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel).SlotPadding(FMargin(5));
	const int32 Current = GI() ? GI()->GetMenuLanguage() : 0;
	for (int32 i = 0; i < NiLoc::NumLangs; ++i)
	{
		const bool bSelected = i == Current;
		Grid->AddSlot(i % 2, i / 2)
		[
			SNew(SButton).ButtonStyle(bSelected ? &PrimaryStyle : &OnCardStyle).IsFocusable(false)
				.HAlign(HAlign_Center).VAlign(VAlign_Center)
				.ContentPadding(FMargin(0, 12))
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
				SNew(STextBlock).Font(Ty(NiType::ActionSmall)).ColorAndOpacity(NiHudColor::Ink)
					.Text(FText::FromString(NiLoc::LangNativeName(i)))
			]
		];
	}

	return SNew(SBox).Visibility_Lambda([this]() { return PageVis(EPage::Language); })
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, NiSpace::BandTop, 0, 0)
		[
			// 頁內標題＝「語言」（入口鈕才需要「文A」救援語義——進到頁裡
			// 的人已脫離迷路情境，正文的母語名網格自己會說話）
			SNew(STextBlock).Font(Ty(NiType::Title)).ColorAndOpacity(NiHudColor::Paper)
				.Text(Loc(ENiLocKey::Language))
		]
		+ SVerticalBox::Slot().FillHeight(1)[SNew(SSpacer)]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			SNew(SBox).WidthOverride(NiSpace::CardWWide)
			[
				SNew(SBackgroundBlur).BlurStrength(14).CornerRadius(FVector4(18, 18, 18, 18))
				[
					SNew(SBorder).BorderImage(&CardBrush).Padding(FMargin(NiSpace::CardPadH, NiSpace::CardPadV))
					[
						Grid
					]
				]
			]
		]
		+ SVerticalBox::Slot().FillHeight(1)[SNew(SSpacer)]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, NiSpace::BandBottom)
		[
			MakeGhostButton(LocS(ENiLocKey::Back), [this]() { Page = LangOrigin; })
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
			SNew(STextBlock).Font(Ty(NiType::Label)).ColorAndOpacity(NiHudColor::InkDim)
				.Visibility_Lambda([this]()
				{
					return (FaceRowBox.IsValid() && FaceRowBox->NumSlots() > 0)
						? EVisibility::Visible : EVisibility::Collapsed;
				})
				.Text(Loc(ENiLocKey::SavedFaces))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, NiSpace::S, 0, 0)
		[
			SAssignNew(FaceRowBox, SHorizontalBox)
		];
}

TSharedRef<SWidget> SNiMenu::MakeProfileNameBlock()
{
	// 名字（可選——隨機名可用）
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock).Font(Ty(NiType::Label)).ColorAndOpacity(NiHudColor::InkDim)
				.Text(Loc(ENiLocKey::YourName))
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
				SNew(STextBlock).Font(Ty(NiType::Note)).ColorAndOpacity(NiHudColor::InkDim)
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
				SNew(STextBlock).Font(Ty(NiType::Note)).ColorAndOpacity(NiHudColor::Amber)
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
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock).Font(Ty(NiType::Label)).ColorAndOpacity(NiHudColor::InkDim)
				.Text(Loc(ENiLocKey::CashLabel))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, NiSpace::XS, 0, 0)
		[
			SNew(STextBlock).ColorAndOpacity(NiHudColor::Ink)
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
			SNew(STextBlock).Font(Ty(NiType::Note)).ColorAndOpacity(NiHudColor::InkDim)
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
			SNew(STextBlock).Font(Ty(NiType::Warning)).ColorAndOpacity(NiHudColor::Ink)
				.AutoWrapText(true)
				.Text(Loc(ENiLocKey::BrowHint))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, NiSpace::S, 0, 0)
		[
			SNew(STextBlock).Font(Ty(NiType::Note)).ColorAndOpacity(NiHudColor::InkDim)
				.AutoWrapText(true)
				.Text(Loc(ENiLocKey::PrivacyHint))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, NiSpace::M, 0, 0)
		[
			SNew(SButton).ButtonStyle(&PrimaryStyle).IsFocusable(false)
				.HAlign(HAlign_Center).VAlign(VAlign_Center)
				.ContentPadding(FMargin(0.0f, NiSpace::BtnPadV))
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
				SNew(STextBlock).Font(Ty(NiType::Action)).ColorAndOpacity(NiHudColor::Ink)
					.Text_Lambda([this]()
					{
						// 已有臉＝「重新上傳」（user 定案 2026-08-07）
						UNiceInkPersonaSubsystem* P = Persona();
						return (P && P->HasCustomFace())
							? Loc(ENiLocKey::ReuploadSelfie) : Loc(ENiLocKey::UploadSelfie);
					})
			]
		]
		// 管線狀態列（Running/Done/Failed；Idle 隱藏；Running 附秒數防「當機了嗎」）
		+ SVerticalBox::Slot().AutoHeight().Padding(0, NiSpace::S, 0, 0)
		[
			SNew(STextBlock).Font(Ty(NiType::Note))
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
						? FSlateColor(NiHudColor::Red) : FSlateColor(NiHudColor::InkDim);
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
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, NiSpace::BandTop, 0, 0)
		[
			SNew(STextBlock).Font(Ty(NiType::Title)).ColorAndOpacity(NiHudColor::Paper)
				// 首啟創角＝同一頁換帽子：標題「創建你的力士」
				.Text_Lambda([this]()
				{
					return Loc(bOnboarding ? ENiLocKey::CreateYourRikishi : ENiLocKey::Profile);
				})
		]
		+ SVerticalBox::Slot().FillHeight(1)[SNew(SSpacer)]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			SNew(SBox).WidthOverride(NiSpace::CardW)
			[
				SNew(SBackgroundBlur).BlurStrength(14).CornerRadius(FVector4(18, 18, 18, 18))
				[
					SNew(SBorder).BorderImage(&CardBrush).Padding(FMargin(NiSpace::CardPadH, NiSpace::CardPadV))
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
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, NiSpace::BandBottom)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(NiSpace::S, 0)
			[
				SNew(SBox).Visibility_Lambda([this]() { return bOnboarding ? EVisibility::Collapsed : EVisibility::Visible; })
				[
					MakeGhostButton(LocS(ENiLocKey::Back), [this]()
					{
						CommitName();
						Page = EPage::Root;
					})
				]
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
						SNew(STextBlock).Font(Ty(NiType::ActionSmall)).ColorAndOpacity(NiHudColor::Paper)
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
		UTexture* Thumb = P->GetFaceThumb(Id);
		TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateBrush>();
		Brush->ImageSize = FVector2D(56, 56);
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

		FaceRowBox->AddSlot().AutoWidth().Padding(Shown == 0 ? 0.0f : 8.0f, 0, 0, 0)
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
					SNew(SBox).WidthOverride(56).HeightOverride(56)
					[
						SNew(SImage).Image(Brush.Get())
					]
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 2, 0, 0)
				[
					SNew(SBox).WidthOverride(40).HeightOverride(3)
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
	case ENiSessionUiState::Searching: return Loc(ENiLocKey::StatusLooking);
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
		if (!F.bPublic || Shown >= 5)
		{
			continue; // 私房永不列出；版面上限 5 列
		}
		++Shown;
		const int32 Taken = FMath::Clamp(F.MaxSlots - F.OpenSlots, 0, F.MaxSlots);
		const int32 JoinIndex = F.SearchIndex;
		RoomListBox->AddSlot().AutoHeight().Padding(0, NiSpace::XS)
		[
			SNew(SButton).ButtonStyle(&OnCardStyle).IsFocusable(false)
				.ContentPadding(FMargin(22, 12))
				.IsEnabled_Lambda([this]() { return !IsBusy(); })
				.OnClicked_Lambda([this, JoinIndex]()
				{
					CommitName();
					if (UNiceInkSessionSubsystem* Sub = Sessions()) { Sub->JoinFoundSession(JoinIndex); }
					return FReply::Handled();
				})
			[
				// 臉制（SPEC #52）：房主名退場——陌生人本來就不認識名字；
				// 房主臉像待自拍上雲（PlayerDataStorage）後補（記帳）。
				// 在那之前補 ping＝至少多一個可區辨/可判斷的訊號
				SNew(STextBlock).Font(Ty(NiType::ActionSmall)).ColorAndOpacity(NiHudColor::Ink)
					.Justification(ETextJustify::Center)
					.Text(FText::FromString(F.PingMs > 0
						? FString::Printf(TEXT("%d / %d   ·   %d ms"), Taken, F.MaxSlots, F.PingMs)
						: FString::Printf(TEXT("%d / %d"), Taken, F.MaxSlots)))
			]
		];
	}
	// （卡內空狀態退役 2026-08-12：沒房時整張卡收起、頁面上一行字代班）
}

void SNiMenu::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

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
				S->SearchSessions(IsLan());
				bSearchKicked = true;
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
					Stamp = Stamp * 31 + GetTypeHash(F.OwnerName) + F.OpenSlots + (F.bPublic ? 7 : 0);
				}
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
		if (bOnboarding && Page == EPage::Profile)
		{
			return FReply::Handled(); // 創角中無返回
		}
		switch (Page)
		{
		case EPage::Credits:  Page = EPage::Settings; break;
		case EPage::Language: Page = LangOrigin; break;
		case EPage::Profile:  CommitName(); Page = EPage::Root; break;
		default:              Page = EPage::Root; break;
		}
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
