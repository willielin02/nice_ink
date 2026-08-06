#include "NiceInkMenuHUD.h"

#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/GameViewportClient.h"
#include "NiceInkGameInstance.h"
#include "NiceInkMenuWidget.h"

UFont* ANiceInkMenuHUD::BuildMenuFont()
{
	// runtime 複合字體（Slate 的 FSlateFontInfo 只吃 UFont，裸 FontFace＝豆腐字）。
	// 六文字系統矩陣（2026-08-06 對齊 Meccha 13 語調查）：
	// 預設面＝圓體（輔助）＋Zen Old Mincho（標題/動作，拉丁+日文）；
	// SubTypeface＝繁中/簡中（Han 統一碼同域→用 culture 區分）、韓文、
	// 西里爾+擴展拉丁、阿拉伯；Fallback＝Noto Serif TC（en 文化下的雜漢字保底）。
	// 每個面都提供同名五席（Regular/Bold/Serif/SerifRegular/SerifBlack）＝
	// FSlateFontInfo 的字面名在任何文字系統下都解析得到。
	auto Load = [](const TCHAR* Path) -> UObject*
	{
		return StaticLoadObject(UObject::StaticClass(), nullptr, Path);
	};
	auto FillTypeface = [](FTypeface& T, UObject* Regular, UObject* Bold, UObject* Serif,
		UObject* SerifRegular, UObject* SerifBlack)
	{
		const TPair<const TCHAR*, UObject*> Slots[] = {
			{ TEXT("Regular"), Regular }, { TEXT("Bold"), Bold }, { TEXT("Serif"), Serif },
			{ TEXT("SerifRegular"), SerifRegular }, { TEXT("SerifBlack"), SerifBlack },
		};
		for (const auto& S : Slots)
		{
			if (S.Value)
			{
				FTypefaceEntry& E = T.Fonts.AddDefaulted_GetRef();
				E.Name = S.Key;
				E.Font = FFontData(S.Value);
			}
		}
	};

	UObject* MPlusMed = Load(TEXT("/Game/UI/Fonts/FF_MPlusRounded_Medium.FF_MPlusRounded_Medium"));
	UObject* MPlusXB = Load(TEXT("/Game/UI/Fonts/FF_MPlusRounded_XBold.FF_MPlusRounded_XBold"));
	UObject* ZenReg = Load(TEXT("/Game/UI/Fonts/FF_ZenOldMincho_Regular.FF_ZenOldMincho_Regular"));
	UObject* ZenBold = Load(TEXT("/Game/UI/Fonts/FF_ZenOldMincho_Bold.FF_ZenOldMincho_Bold"));
	UObject* ZenBlack = Load(TEXT("/Game/UI/Fonts/FF_ZenOldMincho_Black.FF_ZenOldMincho_Black"));
	if (!MPlusMed && !ZenBold)
	{
		return GEngine ? GEngine->GetMediumFont() : nullptr;
	}

	UFont* Composite = NewObject<UFont>(this, TEXT("NiMenuFont"));
	Composite->FontCacheType = EFontCacheType::Runtime;
	FCompositeFont& CF = Composite->GetMutableInternalCompositeFont();
	FillTypeface(CF.DefaultTypeface, MPlusMed, MPlusXB, ZenBold, ZenReg, ZenBlack);

	struct FScript
	{
		const TCHAR* Prefix;      // /Game/UI/Fonts/FF_<Prefix>_<Weight>
		const TCHAR* Cultures;    // 空=不限文化
		std::initializer_list<TPair<int32, int32>> Ranges;
		bool bHasBlack;
	};
	const FScript Scripts[] = {
		// Han 統一碼：繁簡同碼域，靠 culture 分流（ja/en 不吃、走預設 Zen）。
		// 繁中=源流明體（舊式明體＝Zen 同屬；2026-08-06 user 打回「繁中怎會沒古風」
		// 後換裝；Noto TC 留庫備用）
		{ TEXT("GenRyuMin"), TEXT("zh-Hant;zh-TW;zh-HK;zh-MO"),
			{ {0x2E80, 0x303F}, {0x3400, 0x4DBF}, {0x4E00, 0x9FFF}, {0xF900, 0xFAFF} }, true },
		{ TEXT("NotoSerifSC"), TEXT("zh-Hans;zh-CN;zh-SG;zh"),
			{ {0x2E80, 0x303F}, {0x3400, 0x4DBF}, {0x4E00, 0x9FFF}, {0xF900, 0xFAFF} }, true },
		// 韓文（碼域獨占、不限文化）
		{ TEXT("NotoSerifKR"), TEXT(""),
			{ {0x1100, 0x11FF}, {0x3130, 0x318F}, {0xA960, 0xA97F}, {0xAC00, 0xD7FF} }, true },
		// 西里爾＋擴展拉丁（土耳其文 İığş 等；基本拉丁留 Zen）
		{ TEXT("NotoSerif"), TEXT(""),
			{ {0x0100, 0x024F}, {0x0400, 0x052F} }, false },
		// 阿拉伯（含連寫呈現形；RTL 整形由 Slate ICU 處理）
		{ TEXT("NotoNaskh"), TEXT(""),
			{ {0x0600, 0x06FF}, {0x0750, 0x077F}, {0x08A0, 0x08FF}, {0xFB50, 0xFDFF}, {0xFE70, 0xFEFF} }, false },
	};
	for (const FScript& S : Scripts)
	{
		UObject* Reg = Load(*FString::Printf(TEXT("/Game/UI/Fonts/FF_%s_Regular.FF_%s_Regular"), S.Prefix, S.Prefix));
		UObject* Bold = Load(*FString::Printf(TEXT("/Game/UI/Fonts/FF_%s_Bold.FF_%s_Bold"), S.Prefix, S.Prefix));
		UObject* Black = S.bHasBlack
			? Load(*FString::Printf(TEXT("/Game/UI/Fonts/FF_%s_Black.FF_%s_Black"), S.Prefix, S.Prefix))
			: Bold;
		if (!Reg && !Bold)
		{
			continue; // 缺面＝該文字系統走 fallback（不擋其他系統）
		}
		FCompositeSubFont& Sub = CF.SubTypefaces.AddDefaulted_GetRef();
		Sub.Cultures = S.Cultures;
		for (const auto& R : S.Ranges)
		{
			Sub.CharacterRanges.Add(FInt32Range(R.Key, R.Value));
		}
		FillTypeface(Sub.Typeface, Reg, Bold, Bold, Reg, Black);
	}

	// Fallback：en/ja 文化下撞到的雜漢字保底（源流明體＝與 Zen 同屬舊式、字符繼承思源）
	if (UObject* FbReg = Load(TEXT("/Game/UI/Fonts/FF_GenRyuMin_Regular.FF_GenRyuMin_Regular")))
	{
		UObject* FbBold = Load(TEXT("/Game/UI/Fonts/FF_GenRyuMin_Bold.FF_GenRyuMin_Bold"));
		UObject* FbBlack = Load(TEXT("/Game/UI/Fonts/FF_GenRyuMin_Black.FF_GenRyuMin_Black"));
		FillTypeface(CF.FallbackTypeface.Typeface, FbReg, FbBold ? FbBold : FbReg,
			FbBold ? FbBold : FbReg, FbReg, FbBlack ? FbBlack : FbReg);
	}
	return Composite;
}

void ANiceInkMenuHUD::BeginPlay()
{
	Super::BeginPlay();

	// BGM 喚起（舊版繼承 ANiceInkHUD 順帶做掉；改繼承 AHUD 後要自己叫——
	// 斷樂＝舞台編舞的時間零點也沒了）
	if (UNiceInkGameInstance* Inst = UNiceInkGameInstance::Get(this))
	{
		Inst->EnsureBgmPlaying(GetWorld());
	}

	if (GEngine && GEngine->GameViewport && PlayerOwner)
	{
		MenuFont = BuildMenuFont();
		Menu = SNew(SNiMenu).OwnerPC(PlayerOwner).Font(MenuFont);
		GEngine->GameViewport->AddViewportWidgetContent(Menu.ToSharedRef(), /*ZOrder=*/10);
	}
}

void ANiceInkMenuHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Menu.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Menu.ToSharedRef());
	}
	Menu.Reset();
	Super::EndPlay(EndPlayReason);
}

void ANiceInkMenuHUD::RoboOpenJoinPage(const FString& PrefillCode)
{
	if (Menu.IsValid())
	{
		Menu->OpenJoinPage(PrefillCode);
	}
}

void ANiceInkMenuHUD::RoboShowFontSample(const FString& Sample)
{
	if (Menu.IsValid())
	{
		Menu->ShowFontSample(Sample);
	}
}

void ANiceInkMenuHUD::RoboOpenLanguagePage()
{
	if (Menu.IsValid())
	{
		Menu->OpenLanguagePage();
	}
}

void ANiceInkMenuHUD::RecreateMenu(bool bOpenSettings)
{
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, bOpenSettings]()
	{
		if (!GEngine || !GEngine->GameViewport || !PlayerOwner)
		{
			return;
		}
		if (Menu.IsValid())
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(Menu.ToSharedRef());
		}
		Menu = SNew(SNiMenu).OwnerPC(PlayerOwner).Font(MenuFont);
		if (bOpenSettings)
		{
			Menu->OpenSettingsPage();
		}
		GEngine->GameViewport->AddViewportWidgetContent(Menu.ToSharedRef(), /*ZOrder=*/10);
	}));
}
