#include "NiceInkMenuWidget.h"

#include "NiceInkLocText.h"
#include "NiceInkMenuHUD.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
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
	const FIntPoint ResCandidates[] = {
		{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}
	};
	constexpr int32 NumResCandidates = UE_ARRAY_COUNT(ResCandidates);
	const TCHAR* WindowModeLabels[] = { TEXT("fullscreen"), TEXT("borderless"), TEXT("windowed") };

	EWindowMode::Type WindowModeFromIndex(int32 Index)
	{
		switch (Index)
		{
		case 0: return EWindowMode::Fullscreen;
		case 1: return EWindowMode::WindowedFullscreen;
		default: return EWindowMode::Windowed;
		}
	}
	int32 IndexFromWindowMode(EWindowMode::Type Mode)
	{
		switch (Mode)
		{
		case EWindowMode::Fullscreen: return 0;
		case EWindowMode::WindowedFullscreen: return 1;
		default: return 2;
		}
	}

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

FSlateFontInfo SNiMenu::Font(float Size, bool bBold) const
{
	return FSlateFontInfo(MenuFont.Get(), FMath::RoundToInt(Size),
		bBold ? FName("Bold") : FName("Regular"));
}

FSlateFontInfo SNiMenu::Serif(float Size, bool bBlack, int32 Tracking) const
{
	FSlateFontInfo Info(MenuFont.Get(), FMath::RoundToInt(Size),
		bBlack ? FName("SerifBlack") : FName("Serif"));
	Info.LetterSpacing = Tracking;
	return Info;
}

FSlateFontInfo SNiMenu::Label(float Size) const
{
	// 表單小標籤：圓體+寬字距（全大寫由呼叫端給字串）——低調、有秩序
	FSlateFontInfo Info(MenuFont.Get(), FMath::RoundToInt(Size), FName("Regular"));
	Info.LetterSpacing = 280;
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
	const FString Clean = UNiceInkGameInstance::SanitizePlayerName(NameBox->GetText().ToString());
	if (!Clean.IsEmpty() && Clean != Inst->PlayerDisplayName)
	{
		Inst->PlayerDisplayName = Clean;
		Inst->SaveSettings();
	}
	NameBox->SetText(FText::FromString(Inst->PlayerDisplayName));
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
	ChipOnBrush = MakeRounded(WithA(NiHudColor::Ink, 0.85f), 10.0f);
	ChipOffBrush = MakeRounded(WithA(NiHudColor::Ink, 0.05f), 10.0f);

	NameBoxStyle = FEditableTextBoxStyle()
		.SetBackgroundImageNormal(MakeRounded(WithA(NiHudColor::Ink, 0.06f), 10.0f))
		.SetBackgroundImageHovered(MakeRounded(WithA(NiHudColor::Ink, 0.10f), 10.0f))
		.SetBackgroundImageFocused(MakeRounded(WithA(NiHudColor::Ink, 0.10f), 10.0f))
		.SetBackgroundImageReadOnly(MakeRounded(WithA(NiHudColor::Ink, 0.04f), 10.0f))
		.SetTextStyle(FTextBlockStyle().SetFont(Font(17)).SetColorAndOpacity(NiHudColor::Ink))
		.SetFont(Font(17))
		.SetForegroundColor(NiHudColor::Ink)
		.SetFocusedForegroundColor(NiHudColor::Ink)
		.SetPadding(FMargin(14, 12));

	FString Version;
	GConfig->GetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"), TEXT("ProjectVersion"), Version, GGameIni);

	ChildSlot
	[
		SNew(SOverlay)

		// 六頁疊放、Visibility 輪詢切換
		+ SOverlay::Slot()[BuildRootPage()]
		+ SOverlay::Slot()[BuildJoinPage()]
		+ SOverlay::Slot()[BuildSettingsPage()]
		+ SOverlay::Slot()[BuildCreditsPage()]
		+ SOverlay::Slot()[BuildLanguagePage()]
		+ SOverlay::Slot()[BuildProfilePage()]

		// 版本戳（右下、極低調——資訊存在但不參與畫面）
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(0, 0, 16, 10)
		[
			SNew(STextBlock).Font(Font(10))
				.ColorAndOpacity(FLinearColor(NiHudColor::PaperDim.R, NiHudColor::PaperDim.G, NiHudColor::PaperDim.B, 0.5f))
				.Text(FText::FromString(FString::Printf(TEXT("v%s"), *Version)))
		]
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
			SNew(STextBlock).Font(Serif(16)).ColorAndOpacity(NiHudColor::Paper)
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
			SNew(STextBlock).Font(Font(14))
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

		// --- 頂帶：標題（明朝體黑字重＝墨字性格；tagline/短劃＝零功能已刪）---
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 64, 0, 0)
		[
			SNew(STextBlock).Font(Serif(88, true, 120)).ColorAndOpacity(NiHudColor::Paper)
				.Text(FText::FromString(TEXT("NICE INK")))
		]

		// 斷線原因（有才顯示；host/join 時清除）
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 12, 0, 0)
		[
			SNew(STextBlock).Font(Font(15)).ColorAndOpacity(NiHudColor::Red)
				.Visibility_Lambda([this]() { return ErrorBanner.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
				.Text_Lambda([this]() { return FText::FromString(ErrorBanner); })
		]

		// 字體取樣行（robo NiMenuFontSample 專用；平時空=隱藏）
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 16, 0, 0)
		[
			SNew(STextBlock).Font(Serif(26)).ColorAndOpacity(NiHudColor::Paper)
				.Visibility_Lambda([this]() { return FontSample.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
				.Text_Lambda([this]() { return FText::FromString(FontSample); })
		]

		// --- 中帶：毛玻璃白卡（FillHeight 上下撐開＝真垂直置中）---
		+ SVerticalBox::Slot().FillHeight(1)[SNew(SSpacer)]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			SNew(SBox).WidthOverride(430)
			[
				SNew(SBackgroundBlur).BlurStrength(14)
				.CornerRadius(FVector4(18, 18, 18, 18))
				[
					SNew(SBorder).BorderImage(&CardBrush).Padding(FMargin(34, 26, 34, 30))
					[
						SNew(SVerticalBox)

						// 身分＝名字＋臉並列（SPEC #52 v4.0e）：名字欄住個人檔案頁、
						// 舞台上跳舞的就是你；主卡只留動作
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SButton).ButtonStyle(&PrimaryStyle).IsFocusable(false)
								.HAlign(HAlign_Center).VAlign(VAlign_Center)
								.ContentPadding(FMargin(0, 15))
								.IsEnabled_Lambda([this]() { return !IsBusy(); })
								.OnClicked_Lambda([this]()
								{
									CommitName();
									ErrorBanner.Reset();
									if (UNiceInkSessionSubsystem* S = Sessions()) { S->HostSession(IsLan(), bPublicRoom); }
									return FReply::Handled();
								})
							[
								SNew(STextBlock).Font(Serif(20)).ColorAndOpacity(NiHudColor::Ink)
									.Text(Loc(ENiLocKey::HostARoom))
							]
						]

						// 可見性＝host 的子選項（小標籤+矮 chip；層級從屬不搶戲）
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 0)
						[
							SNew(STextBlock).Font(Label(10)).ColorAndOpacity(NiHudColor::InkDim)
								.Text(Loc(ENiLocKey::RoomVisibility))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1)[MakeChip(LocS(ENiLocKey::InviteOnly), false)]
							+ SHorizontalBox::Slot().AutoWidth()[SNew(SSpacer).Size(FVector2D(6, 1))]
							+ SHorizontalBox::Slot().FillWidth(1)[MakeChip(LocS(ENiLocKey::PublicRoom), true)]
						]

						+ SVerticalBox::Slot().AutoHeight().Padding(0, 20, 0, 0)
						[
							SNew(SButton).ButtonStyle(&OnCardStyle).IsFocusable(false)
								.HAlign(HAlign_Center).VAlign(VAlign_Center)
								.ContentPadding(FMargin(0, 15))
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
								SNew(STextBlock).Font(Serif(20)).ColorAndOpacity(NiHudColor::Ink)
									.Text(Loc(ENiLocKey::JoinARoom))
							]
						]
					]
				]
			]
		]

		// 狀態行（creating/looking/joining/failed）
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 14, 0, 0)
		[
			SNew(STextBlock).Font(Font(15))
				.ColorAndOpacity_Lambda([this]() { return StatusColor(); })
				.Text_Lambda([this]() { return StatusText(); })
		]

		// --- 底帶：次要動作下錨貼底（licenses 併入 settings＝主選單少一顆鈕）---
		+ SVerticalBox::Slot().FillHeight(1)[SNew(SSpacer)]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 48)
		[
			SNew(SHorizontalBox)
			// 個人檔案（SPEC #52 v4.0e：名字＋自拍＋現金＋刺青的家）
			+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0)
			[
				MakeGhostButton(LocS(ENiLocKey::Profile), [this]() { OpenProfilePage(); })
			]
			// 「文A」＝語言入口（Google 式語言符號、不依賴任何語言的文字——
			// 看不懂當前語言的玩家也找得到；2026-08-06 迷路窘境調查後補）
			+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0)
			[
				MakeGhostButton(TEXT("文A"), [this]() { Page = EPage::Language; })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0)
			[
				MakeGhostButton(LocS(ENiLocKey::SettingsBtn), [this]()
				{
					bSettingsSeeded = false;
					Page = EPage::Settings;
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0)
			[
				MakeGhostButton(LocS(ENiLocKey::Quit), [this]()
				{
					if (OwnerPC.IsValid())
					{
						UKismetSystemLibrary::QuitGame(OwnerPC.Get(), OwnerPC.Get(), EQuitPreference::Quit, false);
					}
				})
			]
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
					SNew(STextBlock).Font(Serif(42, true)).ColorAndOpacity(NiHudColor::Ink)
						.Text_Lambda([this, i]()
						{
							return FText::FromString(i < CodeBuffer.Len() ? CodeBuffer.Mid(i, 1) : FString());
						})
				]
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0, 0, 0, 8)
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

		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 56, 0, 0)
		[
			SNew(STextBlock).Font(Serif(42, true, 80)).ColorAndOpacity(NiHudColor::Paper)
				.Text(Loc(ENiLocKey::JoinARoom))
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 10, 0, 0)
		[
			SNew(STextBlock).Font(Font(14)).ColorAndOpacity(NiHudColor::PaperDim)
				.Text(Loc(ENiLocKey::AskHostCode))
		]

		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 24, 0, 0)
		[
			Slots
		]

		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 20, 0, 0)
		[
			SNew(SBox).WidthOverride(332)
			[
				SNew(SButton).ButtonStyle(&PrimaryStyle).IsFocusable(false)
					.HAlign(HAlign_Center).VAlign(VAlign_Center)
					.ContentPadding(FMargin(0, 13))
					.IsEnabled_Lambda([this]() { return !IsBusy() && CodeBuffer.Len() == 4; })
					.OnClicked_Lambda([this]()
					{
						CommitName();
						if (UNiceInkSessionSubsystem* S = Sessions()) { S->JoinRoomByCode(CodeBuffer, IsLan()); }
						return FReply::Handled();
					})
				[
					SNew(STextBlock).Font(Serif(18)).ColorAndOpacity(NiHudColor::Ink)
						.Text(Loc(ENiLocKey::JoinWithCode))
				]
			]
		]

		// 公開房＝自己的毛玻璃卡（裸文字壓在舞者身上＝讀不到——實錘）
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 30, 0, 0)
		[
			SNew(SBox).WidthOverride(560)
			[
				SNew(SBackgroundBlur).BlurStrength(14).CornerRadius(FVector4(16, 16, 16, 16))
				[
					SNew(SBorder).BorderImage(&CardBrush).Padding(FMargin(26, 18, 26, 20))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock).Font(Label(10)).ColorAndOpacity(NiHudColor::InkDim)
								.Text(Loc(ENiLocKey::PublicRooms))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
						[
							SAssignNew(RoomListBox, SVerticalBox)
						]
					]
				]
			]
		]

		+ SVerticalBox::Slot().FillHeight(1)[SNew(SSpacer)]

		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 10)
		[
			SNew(STextBlock).Font(Font(15))
				.ColorAndOpacity_Lambda([this]() { return StatusColor(); })
				.Text_Lambda([this]() { return StatusText(); })
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 56)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0)
			[
				MakeGhostButton(LocS(ENiLocKey::Refresh), [this]()
				{
					if (UNiceInkSessionSubsystem* S = Sessions()) { S->SearchSessions(IsLan()); }
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0)
			[
				MakeGhostButton(LocS(ENiLocKey::Back), [this]() { Page = EPage::Root; })
			]
		]
	];
}

TSharedRef<SWidget> SNiMenu::BuildSettingsPage()
{
	auto MakeArrow = [this](const FString& Glyph, TFunction<void()> OnClick) -> TSharedRef<SWidget>
	{
		return SNew(SButton).ButtonStyle(&OnCardStyle).IsFocusable(false)
			.HAlign(HAlign_Center).VAlign(VAlign_Center)
			.ContentPadding(FMargin(14, 6))
			.OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
			[
				SNew(STextBlock).Font(Font(16, true)).ColorAndOpacity(NiHudColor::Ink)
					.Text(FText::FromString(Glyph))
			];
	};
	auto MakeRow = [this, &MakeArrow](const FString& Label, TFunction<FString()> Value,
		TFunction<void()> OnLeft, TFunction<void()> OnRight) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Font(Font(15)).ColorAndOpacity(NiHudColor::InkDim)
					.Text(FText::FromString(Label))
			]
			+ SHorizontalBox::Slot().AutoWidth()[MakeArrow(TEXT("<"), OnLeft)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(150)
				[
					SNew(STextBlock).Font(Font(16)).ColorAndOpacity(NiHudColor::Ink).Justification(ETextJustify::Center)
						.Text_Lambda([Value]() { return FText::FromString(Value()); })
				]
			]
			+ SHorizontalBox::Slot().AutoWidth()[MakeArrow(TEXT(">"), OnRight)];
	};

	return SNew(SBox).Visibility_Lambda([this]() { return PageVis(EPage::Settings); })
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 56, 0, 0)
		[
			SNew(STextBlock).Font(Serif(42, true, 80)).ColorAndOpacity(NiHudColor::Paper)
				.Text(Loc(ENiLocKey::SettingsBtn))
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 30, 0, 0)
		[
			SNew(SBox).WidthOverride(560)
			[
				SNew(SBackgroundBlur).BlurStrength(14).CornerRadius(FVector4(18, 18, 18, 18))
				[
					SNew(SBorder).BorderImage(&CardBrush).Padding(FMargin(34, 26))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
						[
							MakeRow(LocS(ENiLocKey::WindowMode),
								[this]()
								{
									switch (PendingWindowMode)
									{
									case 0: return LocS(ENiLocKey::Fullscreen);
									case 1: return LocS(ENiLocKey::Borderless);
									default: return LocS(ENiLocKey::Windowed);
									}
								},
								[this]() { PendingWindowMode = (PendingWindowMode + 2) % 3; },
								[this]() { PendingWindowMode = (PendingWindowMode + 1) % 3; })
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
						[
							MakeRow(LocS(ENiLocKey::Resolution),
								[this]() { const FIntPoint R = ResCandidates[PendingResIndex]; return FString::Printf(TEXT("%d x %d"), R.X, R.Y); },
								[this]() { PendingResIndex = FMath::Max(0, PendingResIndex - 1); },
								[this]() { PendingResIndex = FMath::Min(NumResCandidates - 1, PendingResIndex + 1); })
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
						[
							MakeRow(LocS(ENiLocKey::MouseSensitivity),
								[this]() { const UNiceInkGameInstance* I = GI(); return FString::Printf(TEXT("%.1f"), I ? I->MouseSensitivityScale : 1.0f); },
								[this]() { if (UNiceInkGameInstance* I = GI()) { I->MouseSensitivityScale = FMath::Clamp(I->MouseSensitivityScale - 0.1f, 0.2f, 3.0f); I->SaveSettings(); } },
								[this]() { if (UNiceInkGameInstance* I = GI()) { I->MouseSensitivityScale = FMath::Clamp(I->MouseSensitivityScale + 0.1f, 0.2f, 3.0f); I->SaveSettings(); } })
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
						[
							MakeRow(LocS(ENiLocKey::MasterVolume),
								[this]() { const UNiceInkGameInstance* I = GI(); return FString::Printf(TEXT("%d%%"), I ? FMath::RoundToInt(I->MasterVolume * 100.0f) : 100); },
								[this]() { if (UNiceInkGameInstance* I = GI()) { I->MasterVolume = FMath::Clamp(I->MasterVolume - 0.05f, 0.0f, 1.0f); I->UpdateBgmVolume(); I->SaveSettings(); } },
								[this]() { if (UNiceInkGameInstance* I = GI()) { I->MasterVolume = FMath::Clamp(I->MasterVolume + 0.05f, 0.0f, 1.0f); I->UpdateBgmVolume(); I->SaveSettings(); } })
						]
						// 語言＝開全列網格頁（箭頭循環 13 個＝迷路者災難，退役）
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
						[
							SNew(SButton).ButtonStyle(&OnCardStyle).IsFocusable(false)
								.HAlign(HAlign_Center).VAlign(VAlign_Center)
								.ContentPadding(FMargin(0, 10))
								.OnClicked_Lambda([this]() { Page = EPage::Language; return FReply::Handled(); })
							[
								SNew(STextBlock).Font(Font(15))
									.ColorAndOpacity(NiHudColor::Ink)
									.Text_Lambda([this]()
									{
										const UNiceInkGameInstance* I = GI();
										return FText::FromString(FString::Printf(TEXT("文A  ·  %s"),
											*NiLoc::LangNativeName(I ? I->GetMenuLanguage() : 0)));
									})
							]
						]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 18, 0, 0)
						[
							SNew(SButton).ButtonStyle(&PrimaryStyle).IsFocusable(false)
								.ContentPadding(FMargin(28, 11))
								.OnClicked_Lambda([this]()
								{
									if (UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr)
									{
										GUS->SetFullscreenMode(WindowModeFromIndex(PendingWindowMode));
										GUS->SetScreenResolution(ResCandidates[PendingResIndex]);
										GUS->ApplySettings(false);
										GUS->SaveSettings();
									}
									return FReply::Handled();
								})
							[
								SNew(STextBlock).Font(Serif(15)).ColorAndOpacity(NiHudColor::Ink)
									.Text(Loc(ENiLocKey::ApplyDisplay))
							]
						]
					]
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 22, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0)
			[
				MakeGhostButton(LocS(ENiLocKey::Licenses), [this]() { Page = EPage::Credits; })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0)
			[
				MakeGhostButton(LocS(ENiLocKey::Back), [this]() { Page = EPage::Root; })
			]
		]
	];
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
	Box->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 56, 0, 24)
	[
		SNew(STextBlock).Font(Serif(42, true, 80)).ColorAndOpacity(NiHudColor::Paper)
			.Text(Loc(ENiLocKey::Licenses))
	];
	for (const TCHAR* Line : Lines)
	{
		Box->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 5)
		[
			SNew(STextBlock).Font(Font(14)).ColorAndOpacity(NiHudColor::Paper)
				.Text(FText::FromString(Line))
		];
	}
	Box->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 26, 0, 0)
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
							Hud->RecreateMenu(/*bOpenSettings=*/false);
						}
					}
					return FReply::Handled();
				})
			[
				SNew(STextBlock).Font(Serif(17)).ColorAndOpacity(NiHudColor::Ink)
					.Text(FText::FromString(NiLoc::LangNativeName(i)))
			]
		];
	}

	return SNew(SBox).Visibility_Lambda([this]() { return PageVis(EPage::Language); })
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 56, 0, 0)
		[
			SNew(STextBlock).Font(Serif(42, true, 80)).ColorAndOpacity(NiHudColor::Paper)
				.Text(FText::FromString(TEXT("文A")))
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 30, 0, 0)
		[
			SNew(SBox).WidthOverride(600)
			[
				SNew(SBackgroundBlur).BlurStrength(14).CornerRadius(FVector4(18, 18, 18, 18))
				[
					SNew(SBorder).BorderImage(&CardBrush).Padding(FMargin(28, 24))
					[
						Grid
					]
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 22, 0, 0)
		[
			MakeGhostButton(LocS(ENiLocKey::Back), [this]() { Page = EPage::Root; })
		]
	];
}

TSharedRef<SWidget> SNiMenu::BuildProfilePage()
{
	// 個人檔案（SPEC #52 v4.0e）：名字＋自拍＋現金的家；刺青直接看背景舞台
	// 的力士本人（雲端資產到貨即穿上，見 NiceInkMenuStage::DressDancerFromPersona）
	return SNew(SBox).Visibility_Lambda([this]() { return PageVis(EPage::Profile); })
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 56, 0, 0)
		[
			SNew(STextBlock).Font(Serif(42, true, 80)).ColorAndOpacity(NiHudColor::Paper)
				.Text(Loc(ENiLocKey::Profile))
		]
		+ SVerticalBox::Slot().FillHeight(1)[SNew(SSpacer)]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			SNew(SBox).WidthOverride(460)
			[
				SNew(SBackgroundBlur).BlurStrength(14).CornerRadius(FVector4(18, 18, 18, 18))
				[
					SNew(SBorder).BorderImage(&CardBrush).Padding(FMargin(34, 26, 34, 30))
					[
						SNew(SVerticalBox)

						// --- 名字（v4.0e：辨識＝名字＋臉並列，名字欄回歸）---
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock).Font(Label(10)).ColorAndOpacity(NiHudColor::InkDim)
								.Text(Loc(ENiLocKey::YourName))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
						[
							SAssignNew(NameBox, SEditableTextBox)
								.Style(&NameBoxStyle)
								.HintText(Loc(ENiLocKey::ClickToType))
								.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type)
								{
									CommitName();
								})
						]

						// --- 現金（雲端資產；未登入＝提示）---
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 18, 0, 0)
						[
							SNew(STextBlock).Font(Label(10)).ColorAndOpacity(NiHudColor::InkDim)
								.Text(Loc(ENiLocKey::CashLabel))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
						[
							SNew(STextBlock).Font(Serif(26)).ColorAndOpacity(NiHudColor::Ink)
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
									return (S && S->IsLoggedIn())
										? FText::FromString(TEXT("$ 10000")) // 新帳號＝入場預設
										: Loc(ENiLocKey::NotSignedIn);
								})
						]

						// --- 上傳自拍 ---
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 22, 0, 0)
						[
							SNew(SButton).ButtonStyle(&PrimaryStyle).IsFocusable(false)
								.HAlign(HAlign_Center).VAlign(VAlign_Center)
								.ContentPadding(FMargin(0, 15))
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
								SNew(STextBlock).Font(Serif(20)).ColorAndOpacity(NiHudColor::Ink)
									.Text_Lambda([this]()
									{
										// 已有臉＝「重新上傳」（user 定案 2026-08-07）
										UNiceInkPersonaSubsystem* P = Persona();
										return (P && P->HasCustomFace())
											? Loc(ENiLocKey::ReuploadSelfie) : Loc(ENiLocKey::UploadSelfie);
									})
							]
						]
						// 眉毛鐵律（2026-07-08 定案：UI 提醒、後果自負、不做補救）
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
						[
							SNew(STextBlock).Font(Font(11)).ColorAndOpacity(NiHudColor::InkDim)
								.AutoWrapText(true)
								.Text(Loc(ENiLocKey::BrowHint))
						]
						// 管線狀態列（Running/Done/Failed；Idle 隱藏）
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
						[
							SNew(STextBlock).Font(Font(13))
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
										// 附已耗秒數（管線一趟約一分鐘——別讓玩家以為當機）
										return FText::FromString(FString::Printf(TEXT("%s  %ds"),
											*LocS(ENiLocKey::FaceProcessing),
											FMath::FloorToInt32(static_cast<float>(P->GetIntakeElapsedS()))));
									case UNiceInkPersonaSubsystem::EFaceIntakeState::Done:    return Loc(ENiLocKey::FaceUpdated);
									case UNiceInkPersonaSubsystem::EFaceIntakeState::Failed:  return Loc(ENiLocKey::FaceFailed);
									default:                                                  return FText::GetEmpty();
									}
								})
						]

						// --- 臉庫（2026-08-07 user 定案：上傳過的臉全保存、點選即換）---
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 18, 0, 0)
						[
							SNew(STextBlock).Font(Label(10)).ColorAndOpacity(NiHudColor::InkDim)
								.Visibility_Lambda([this]()
								{
									return (FaceRowBox.IsValid() && FaceRowBox->NumSlots() > 0)
										? EVisibility::Visible : EVisibility::Collapsed;
								})
								.Text(Loc(ENiLocKey::SavedFaces))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
						[
							SAssignNew(FaceRowBox, SHorizontalBox)
						]
					]
				]
			]
		]
		+ SVerticalBox::Slot().FillHeight(1)[SNew(SSpacer)]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 48)
		[
			MakeGhostButton(LocS(ENiLocKey::Back), [this]()
			{
				CommitName();
				Page = EPage::Root;
			})
		]
	];
}

void SNiMenu::OpenProfilePage()
{
	Page = EPage::Profile;
	// 開頁播種名字欄（雲端偏好可能在建 UI 後才到）＋重建臉庫列
	if (NameBox.IsValid() && GI())
	{
		NameBox->SetText(FText::FromString(GI()->PlayerDisplayName));
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
		UTexture2D* Thumb = P->GetFaceThumb(Id);
		TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateBrush>();
		Brush->ImageSize = FVector2D(56, 56);
		if (Thumb)
		{
			Brush->SetResourceObject(Thumb); // GC 錨在 Persona 的 ThumbCache（UPROPERTY）
		}
		else
		{
			Brush->TintColor = FSlateColor(FLinearColor(0.62f, 0.55f, 0.48f, 1.0f)); // 無縮圖（legacy 遷移臉）＝素膚塊
		}
		FaceThumbBrushes.Add(Brush);

		FaceRowBox->AddSlot().AutoWidth().Padding(Shown == 0 ? 0.0f : 8.0f, 0, 0, 0)
		[
			SNew(SButton).ButtonStyle(&OnCardStyle).IsFocusable(false)
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
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SBox).WidthOverride(56).HeightOverride(56)
					[
						SNew(SImage).Image(Brush.Get())
					]
				]
				// 穿著中＝底部酒金線（與房碼格的下一格指示同語彙）
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0, 0, 0, 2)
				[
					SNew(SBox).WidthOverride(40).HeightOverride(3)
					[
						SNew(SImage).Image(&UnderlineBrush)
							.Visibility_Lambda([this, Id]()
							{
								UNiceInkPersonaSubsystem* PP = Persona();
								return (PP && PP->GetActiveFaceId() == Id)
									? EVisibility::Visible : EVisibility::Collapsed;
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
		RoomListBox->AddSlot().AutoHeight().Padding(0, 4)
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
				// 房主臉像待自拍上雲（PlayerDataStorage）後補（記帳）
				SNew(STextBlock).Font(Serif(16)).ColorAndOpacity(NiHudColor::Ink)
					.Justification(ETextJustify::Center)
					.Text(FText::FromString(FString::Printf(TEXT("%d / %d"), Taken, F.MaxSlots)))
			]
		];
	}
	if (Shown == 0)
	{
		RoomListBox->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 10)
		[
			SNew(STextBlock).Font(Font(15)).ColorAndOpacity(NiHudColor::InkDim)
				.Visibility_Lambda([this]()
				{
					const UNiceInkSessionSubsystem* S = Sessions();
					return (S && S->GetUiState() == ENiSessionUiState::Idle)
						? EVisibility::Visible : EVisibility::Collapsed;
				})
				.Text(Loc(ENiLocKey::NoPublicRooms))
		];
	}
}

void SNiMenu::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	// 個人檔案頁：換臉/上傳完成（FaceRevision 變動）＝重建臉庫列
	if (Page == EPage::Profile)
	{
		UNiceInkPersonaSubsystem* P = Persona();
		const int32 Rev = P ? P->GetFaceRevision() : 0;
		if (Rev != LastFaceRowRev)
		{
			LastFaceRowRev = Rev;
			RefreshFaceRow();
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
			PendingWindowMode = IndexFromWindowMode(GUS->GetFullscreenMode());
			const FIntPoint Cur = GUS->GetScreenResolution();
			PendingResIndex = 2;
			for (int32 i = 0; i < NumResCandidates; ++i)
			{
				if (ResCandidates[i] == Cur) { PendingResIndex = i; break; }
			}
		}
		bSettingsSeeded = true;
	}
}

FReply SNiMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();

	if (Key == EKeys::Escape && Page != EPage::Root)
	{
		Page = EPage::Root;
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
