#include "NiceInkMenuHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Fonts/SlateFontInfo.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/ConfigCacheIni.h"
#include "NiceInkGameInstance.h"
#include "NiceInkSessionSubsystem.h"
#include "NiceInkTypes.h"
#include "NiceInkUiTokens.h"

namespace
{
	// 解析度候選（apply 時鉗到桌面之內由引擎處理）
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

	// 名字輸入的鍵→字元表（輪詢制；shift＝大寫／底線）
	struct FNiTypeKey { FKey Key; TCHAR Lower; TCHAR Upper; };
	const TArray<FNiTypeKey>& TypeKeys()
	{
		static TArray<FNiTypeKey> Keys;
		if (Keys.IsEmpty())
		{
			const TCHAR* Letters = TEXT("abcdefghijklmnopqrstuvwxyz");
			for (int32 i = 0; i < 26; ++i)
			{
				const FString KeyName = FString::Chr(TEXT('A') + i);
				Keys.Add({ FKey(*KeyName), Letters[i], static_cast<TCHAR>(TEXT('A') + i) });
			}
			const FKey DigitKeys[] = { EKeys::Zero, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four,
				EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
			const FKey NumPadKeys[] = { EKeys::NumPadZero, EKeys::NumPadOne, EKeys::NumPadTwo, EKeys::NumPadThree,
				EKeys::NumPadFour, EKeys::NumPadFive, EKeys::NumPadSix, EKeys::NumPadSeven, EKeys::NumPadEight, EKeys::NumPadNine };
			for (int32 i = 0; i < 10; ++i)
			{
				const TCHAR Digit = static_cast<TCHAR>(TEXT('0') + i);
				Keys.Add({ DigitKeys[i], Digit, Digit });
				Keys.Add({ NumPadKeys[i], Digit, Digit });
			}
			Keys.Add({ EKeys::Hyphen, TEXT('-'), TEXT('_') });
		}
		return Keys;
	}
}

void ANiceInkMenuHUD::BeginPlay()
{
	Super::BeginPlay();

	if (UNiceInkGameInstance* Inst = GI())
	{
		NameBuffer = Inst->PlayerDisplayName;
		AvatarSel = Inst->PreferredAvatar;
		ErrorBanner = Inst->ConsumeDisconnectReason();
	}
	// EOS 已設定→預設線上房（設定它就是為了用它）；否則鎖 LAN
	bUseLan = !UNiceInkSessionSubsystem::IsOnlineServiceConfigured();
}

UNiceInkGameInstance* ANiceInkMenuHUD::GI() const
{
	return UNiceInkGameInstance::Get(this);
}

UNiceInkSessionSubsystem* ANiceInkMenuHUD::Sessions() const
{
	const UNiceInkGameInstance* Inst = GI();
	return Inst ? Inst->GetSubsystem<UNiceInkSessionSubsystem>() : nullptr;
}

UTexture2D* ANiceInkMenuHUD::GetFaceThumb(int32 Index)
{
	if (Index < 0 || Index >= FNiceInkAvatars::Num())
	{
		return nullptr;
	}
	if (FaceThumbs.Num() != FNiceInkAvatars::Num())
	{
		FaceThumbs.SetNum(FNiceInkAvatars::Num());
	}
	if (!FaceThumbs[Index])
	{
		FaceThumbs[Index] = LoadObject<UTexture2D>(nullptr, *FNiceInkAvatars::Get(Index).FaceOpenPath);
	}
	return FaceThumbs[Index];
}

void ANiceInkMenuHUD::PollNameTyping()
{
	if (!PlayerOwner || !bNameFocused)
	{
		return;
	}
	const bool bShift = PlayerOwner->IsInputKeyDown(EKeys::LeftShift) || PlayerOwner->IsInputKeyDown(EKeys::RightShift);
	for (const FNiTypeKey& K : TypeKeys())
	{
		if (NameBuffer.Len() < 16 && PlayerOwner->WasInputKeyJustPressed(K.Key))
		{
			NameBuffer.AppendChar(bShift ? K.Upper : K.Lower);
		}
	}
	if (PlayerOwner->WasInputKeyJustPressed(EKeys::BackSpace) && NameBuffer.Len() > 0)
	{
		NameBuffer.LeftChopInline(1);
	}
	if (PlayerOwner->WasInputKeyJustPressed(EKeys::Enter) || PlayerOwner->WasInputKeyJustPressed(EKeys::Escape))
	{
		bNameFocused = false;
		CommitName();
	}
}

void ANiceInkMenuHUD::CommitName()
{
	if (UNiceInkGameInstance* Inst = GI())
	{
		const FString Clean = UNiceInkGameInstance::SanitizePlayerName(NameBuffer);
		if (!Clean.IsEmpty() && Clean != Inst->PlayerDisplayName)
		{
			Inst->PlayerDisplayName = Clean;
			Inst->SaveSettings();
		}
		NameBuffer = Inst->PlayerDisplayName;
	}
}

void ANiceInkMenuHUD::DrawHUD()
{
	// 刻意不走 ANiceInkHUD::DrawHUD（那是局內畫面）；只借它的 helpers 與資產
	AHUD::DrawHUD();

	if (!Canvas || !GetWorld() || !PlayerOwner)
	{
		return;
	}
	EnsureUiAssets();
	UiScale = Canvas->ClipY / 1080.0f;

	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;

	BeginUiFrame();

	// 空世界：整面墨色打底
	DrawRect(NiHudColor::Ink, 0, 0, W, H);

	switch (Page)
	{
	case EMenuPage::Root:     DrawRootPage(W, H); break;
	case EMenuPage::Join:     DrawJoinPage(W, H); break;
	case EMenuPage::Settings: DrawSettingsPage(W, H); break;
	case EMenuPage::Credits:  DrawCreditsPage(W, H); break;
	}

	// ESC＝子頁返回
	if (Page != EMenuPage::Root && PlayerOwner->WasInputKeyJustPressed(EKeys::Escape))
	{
		Page = EMenuPage::Root;
	}

	// 版本戳（右下、常駐——不是 debug 遙測）
	FString Version;
	GConfig->GetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"), TEXT("ProjectVersion"), Version, GGameIni);
	DrawTok(FString::Printf(TEXT("v%s"), *Version), W - 18.0f * UiScale, H - 30.0f * UiScale,
		ETextTier::Small, NiHudColor::PaperDim, EHAlign::Right, false);
}

void ANiceInkMenuHUD::DrawRootPage(float W, float H)
{
	const float CX = W * 0.5f;

	DrawBigTitle(TEXT("NICE INK"), CX, 96.0f * UiScale, 84.0f, NiHudColor::Paper);
	DrawTok(TEXT("sumo, sake, and regrettable tattoos"), CX, 236.0f * UiScale,
		ETextTier::Body, NiHudColor::PaperDim, EHAlign::Center, false);

	// 斷線原因橫幅（點任意處清除）
	if (!ErrorBanner.IsEmpty())
	{
		DrawTok(ErrorBanner, CX, 266.0f * UiScale, ETextTier::Body, NiHudColor::Red, EHAlign::Center, true);
		if (bClickThisFrame)
		{
			ErrorBanner.Reset();
		}
	}

	// --- 名字輸入框 ---
	const float FieldW = 340.0f * UiScale;
	const float FieldH = 46.0f * UiScale;
	const float FieldX = CX - FieldW * 0.5f;
	const float FieldY = 300.0f * UiScale;
	DrawTok(TEXT("your name"), FieldX - 16.0f * UiScale, FieldY + 12.0f * UiScale,
		ETextTier::Body, NiHudColor::PaperDim, EHAlign::Right, false);

	FLinearColor FieldFill = NiHudColor::Ink;
	FieldFill.A = 0.9f;
	DrawRect(FieldFill, FieldX, FieldY, FieldW, FieldH);
	const FLinearColor FieldEdge = bNameFocused ? NiHudColor::Amber : NiHudColor::PaperDim;
	const float B = FMath::Max(1.0f, 2.0f * UiScale);
	DrawRect(FieldEdge, FieldX, FieldY, FieldW, B);
	DrawRect(FieldEdge, FieldX, FieldY + FieldH - B, FieldW, B);
	DrawRect(FieldEdge, FieldX, FieldY, B, FieldH);
	DrawRect(FieldEdge, FieldX + FieldW - B, FieldY, B, FieldH);

	CaretPhase += GetWorld()->GetDeltaSeconds();
	const bool bCaretOn = bNameFocused && FMath::Fmod(CaretPhase, 1.0) < 0.55;
	const FString Shown = NameBuffer + (bCaretOn ? TEXT("|") : TEXT(""));
	DrawTok(Shown.IsEmpty() ? TEXT("click to type...") : Shown, FieldX + 14.0f * UiScale, FieldY + 11.0f * UiScale,
		ETextTier::Body, Shown.IsEmpty() ? NiHudColor::PaperDim : NiHudColor::Paper, EHAlign::Left, false);

	const bool bFieldHover = MousePos.X >= FieldX && MousePos.X <= FieldX + FieldW &&
		MousePos.Y >= FieldY && MousePos.Y <= FieldY + FieldH;
	if (bClickThisFrame && !bClickConsumed)
	{
		if (bFieldHover)
		{
			bNameFocused = true;
			bClickConsumed = true;
		}
		else if (bNameFocused)
		{
			bNameFocused = false;
			CommitName();
		}
	}
	PollNameTyping();

	// --- avatar 選擇（auto＝席位輪派）---
	const float AvY = 380.0f * UiScale;
	const int32 NumAvatars = FNiceInkAvatars::Num();
	const FString AvLabel = AvatarSel == INDEX_NONE
		? FString(TEXT("auto"))
		: FNiceInkAvatars::Get(AvatarSel).Key;
	const int32 AvDelta = AdjustRow(TEXT("face"), AvLabel, CX - 40.0f * UiScale, AvY);
	if (AvDelta != 0)
	{
		// 域＝[-1, NumAvatars)，-1＝auto；環形
		int32 V = AvatarSel + AvDelta;
		if (V < INDEX_NONE) V = NumAvatars - 1;
		if (V >= NumAvatars) V = INDEX_NONE;
		AvatarSel = V;
		if (UNiceInkGameInstance* Inst = GI())
		{
			Inst->PreferredAvatar = AvatarSel;
			Inst->SaveSettings();
		}
	}
	if (UTexture2D* Thumb = GetFaceThumb(AvatarSel))
	{
		const float TS = 84.0f * UiScale;
		DrawIconTok(Thumb, CX + 330.0f * UiScale, AvY - TS * 0.28f, TS, FLinearColor::White);
	}

	// --- 連線模式（B1：EOS 未設定＝鎖 LAN）---
	const bool bOnlineAvailable = UNiceInkSessionSubsystem::IsOnlineServiceConfigured();
	const float NetY = 448.0f * UiScale;
	const int32 NetDelta = AdjustRow(TEXT("network"), bUseLan ? TEXT("lan") : TEXT("online"),
		CX - 40.0f * UiScale, NetY, bOnlineAvailable, bOnlineAvailable);
	if (NetDelta != 0 && bOnlineAvailable)
	{
		bUseLan = !bUseLan;
	}
	if (!bOnlineAvailable)
	{
		DrawTok(TEXT("online rooms need EOS credentials (Docs/EOS_SETUP.md) — lan only for now"),
			CX, NetY + 44.0f * UiScale, ETextTier::Small, NiHudColor::PaperDim, EHAlign::Center, false);
	}

	// --- 主按鈕列 ---
	const float BtnW = 320.0f * UiScale;
	const float BtnH = 54.0f * UiScale;
	UNiceInkSessionSubsystem* S = Sessions();
	const bool bBusy = S && (S->GetUiState() == ENiSessionUiState::Hosting ||
		S->GetUiState() == ENiSessionUiState::Searching || S->GetUiState() == ENiSessionUiState::Joining);

	if (Button(TEXT("host a room"), CX, 540.0f * UiScale, BtnW, BtnH, !bBusy, true) && S)
	{
		CommitName();
		S->HostSession(bUseLan);
	}
	if (Button(TEXT("join a room"), CX, 606.0f * UiScale, BtnW, BtnH, !bBusy))
	{
		CommitName();
		Page = EMenuPage::Join;
		bSearchKicked = false;
	}
	if (Button(TEXT("settings"), CX, 672.0f * UiScale, BtnW, BtnH))
	{
		bSettingsSeeded = false;
		Page = EMenuPage::Settings;
	}
	if (Button(TEXT("licenses"), CX, 738.0f * UiScale, BtnW, BtnH))
	{
		Page = EMenuPage::Credits;
	}
	if (Button(TEXT("quit"), CX, 804.0f * UiScale, BtnW, BtnH))
	{
		UKismetSystemLibrary::QuitGame(this, PlayerOwner, EQuitPreference::Quit, false);
	}

	DrawSessionStatusLine(CX, 880.0f * UiScale);
}

void ANiceInkMenuHUD::DrawSessionStatusLine(float CenterX, float Y)
{
	const UNiceInkSessionSubsystem* S = Sessions();
	if (!S)
	{
		return;
	}
	switch (S->GetUiState())
	{
	case ENiSessionUiState::Hosting:
		DrawTok(TEXT("creating room..."), CenterX, Y, ETextTier::Body, NiHudColor::Amber, EHAlign::Center, false);
		break;
	case ENiSessionUiState::Searching:
		DrawTok(TEXT("searching..."), CenterX, Y, ETextTier::Body, NiHudColor::Amber, EHAlign::Center, false);
		break;
	case ENiSessionUiState::Joining:
		DrawTok(TEXT("joining..."), CenterX, Y, ETextTier::Body, NiHudColor::Amber, EHAlign::Center, false);
		break;
	case ENiSessionUiState::Failed:
		DrawTok(S->GetLastError(), CenterX, Y, ETextTier::Body, NiHudColor::Red, EHAlign::Center, false);
		break;
	default:
		break;
	}
}

void ANiceInkMenuHUD::DrawJoinPage(float W, float H)
{
	const float CX = W * 0.5f;
	DrawBigTitle(TEXT("join a room"), CX, 100.0f * UiScale, 44.0f, NiHudColor::Paper);

	UNiceInkSessionSubsystem* S = Sessions();
	if (S && !bSearchKicked)
	{
		S->SearchSessions(bUseLan);
		bSearchKicked = true;
	}

	const float ListY = 220.0f * UiScale;
	const float RowH = 52.0f * UiScale;
	const float RowW = 560.0f * UiScale;

	if (S)
	{
		const TArray<FNiFoundSession>& Found = S->GetFoundSessions();
		const int32 MaxRows = 9;
		for (int32 i = 0; i < Found.Num() && i < MaxRows; ++i)
		{
			const FNiFoundSession& F = Found[i];
			const int32 Taken = FMath::Clamp(F.MaxSlots - F.OpenSlots, 0, F.MaxSlots);
			const FString Row = FString::Printf(TEXT("%s   ·   %d/%d in   ·   %d ms"),
				*F.OwnerName, Taken, F.MaxSlots, F.PingMs);
			if (Button(Row, CX, ListY + i * (RowH + 10.0f * UiScale), RowW, RowH,
				S->GetUiState() != ENiSessionUiState::Joining))
			{
				S->JoinFoundSession(i);
			}
		}
		if (Found.IsEmpty() && S->GetUiState() == ENiSessionUiState::Idle)
		{
			DrawTok(TEXT("nothing yet — refresh to search again"), CX, ListY + 10.0f * UiScale,
				ETextTier::Body, NiHudColor::PaperDim, EHAlign::Center, false);
		}
	}

	DrawSessionStatusLine(CX, H - 220.0f * UiScale);

	const float BtnW = 240.0f * UiScale;
	const float BtnH = 50.0f * UiScale;
	const bool bBusy = S && (S->GetUiState() == ENiSessionUiState::Searching ||
		S->GetUiState() == ENiSessionUiState::Joining);
	if (Button(TEXT("refresh"), CX - 140.0f * UiScale, H - 150.0f * UiScale, BtnW, BtnH, !bBusy) && S)
	{
		S->SearchSessions(bUseLan);
	}
	if (Button(TEXT("back"), CX + 140.0f * UiScale, H - 150.0f * UiScale, BtnW, BtnH))
	{
		Page = EMenuPage::Root;
	}
}

void ANiceInkMenuHUD::DrawSettingsPage(float W, float H)
{
	const float CX = W * 0.5f;
	DrawBigTitle(TEXT("settings"), CX, 100.0f * UiScale, 44.0f, NiHudColor::Paper);

	UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	UNiceInkGameInstance* Inst = GI();
	if (!GUS || !Inst)
	{
		return;
	}

	if (!bSettingsSeeded)
	{
		PendingWindowMode = IndexFromWindowMode(GUS->GetFullscreenMode());
		const FIntPoint Cur = GUS->GetScreenResolution();
		PendingResIndex = 2; // 預設落 1920x1080
		for (int32 i = 0; i < NumResCandidates; ++i)
		{
			if (ResCandidates[i] == Cur)
			{
				PendingResIndex = i;
				break;
			}
		}
		bSettingsSeeded = true;
	}

	const float RowX = CX - 40.0f * UiScale;
	float Y = 240.0f * UiScale;
	const float Step = 64.0f * UiScale;

	const int32 WmDelta = AdjustRow(TEXT("window mode"), WindowModeLabels[PendingWindowMode], RowX, Y);
	PendingWindowMode = (PendingWindowMode + WmDelta + 3) % 3;
	Y += Step;

	const FIntPoint Res = ResCandidates[PendingResIndex];
	const int32 ResDelta = AdjustRow(TEXT("resolution"),
		FString::Printf(TEXT("%d x %d"), Res.X, Res.Y), RowX, Y,
		PendingResIndex > 0, PendingResIndex < NumResCandidates - 1);
	PendingResIndex = FMath::Clamp(PendingResIndex + ResDelta, 0, NumResCandidates - 1);
	Y += Step;

	const int32 SensDelta = AdjustRow(TEXT("mouse sensitivity"),
		FString::Printf(TEXT("%.1f"), Inst->MouseSensitivityScale), RowX, Y,
		Inst->MouseSensitivityScale > 0.25f, Inst->MouseSensitivityScale < 2.95f);
	if (SensDelta != 0)
	{
		Inst->MouseSensitivityScale = FMath::Clamp(Inst->MouseSensitivityScale + SensDelta * 0.1f, 0.2f, 3.0f);
		Inst->SaveSettings();
	}
	Y += Step;

	const int32 VolDelta = AdjustRow(TEXT("master volume"),
		FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Inst->MasterVolume * 100.0f)), RowX, Y,
		Inst->MasterVolume > 0.01f, Inst->MasterVolume < 0.99f);
	if (VolDelta != 0)
	{
		Inst->MasterVolume = FMath::Clamp(Inst->MasterVolume + VolDelta * 0.05f, 0.0f, 1.0f);
		Inst->SaveSettings();
	}
	Y += Step + 30.0f * UiScale;

	const float BtnW = 240.0f * UiScale;
	const float BtnH = 50.0f * UiScale;
	if (Button(TEXT("apply display"), CX - 140.0f * UiScale, Y, BtnW, BtnH, true, true))
	{
		GUS->SetFullscreenMode(WindowModeFromIndex(PendingWindowMode));
		GUS->SetScreenResolution(ResCandidates[PendingResIndex]);
		GUS->ApplySettings(false);
		GUS->SaveSettings();
	}
	if (Button(TEXT("back"), CX + 140.0f * UiScale, Y, BtnW, BtnH))
	{
		Page = EMenuPage::Root;
	}
}

void ANiceInkMenuHUD::DrawCreditsPage(float W, float H)
{
	const float CX = W * 0.5f;
	DrawBigTitle(TEXT("licenses"), CX, 100.0f * UiScale, 44.0f, NiHudColor::Paper);

	// 完整條文在隨遊戲發佈的 THIRD_PARTY_NOTICES.md
	const TCHAR* Lines[] = {
		TEXT("nice ink uses these third-party works:"),
		TEXT(""),
		TEXT("M PLUS Rounded 1c font — (c) M+ FONTS PROJECT, SIL Open Font License 1.1"),
		TEXT("icons adapted from game-icons.net — CC BY 3.0"),
		TEXT("\"Sauna\" 3d scene by local.yany (sketchfab) — CC BY 4.0"),
		TEXT(""),
		TEXT("full license texts: THIRD_PARTY_NOTICES.md next to the game executable"),
	};
	float Y = 240.0f * UiScale;
	for (const TCHAR* Line : Lines)
	{
		DrawTok(Line, CX, Y, ETextTier::Body, NiHudColor::Paper, EHAlign::Center, false);
		Y += 34.0f * UiScale;
	}

	if (Button(TEXT("back"), CX, H - 150.0f * UiScale, 240.0f * UiScale, 50.0f * UiScale))
	{
		Page = EMenuPage::Root;
	}
}
