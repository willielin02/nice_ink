#include "NiceInkHUD.h"

#include "NiceInkPortraitBooth.h"

#include "NiceInkLocText.h"

#include "CanvasItem.h"
#include "DreamMazeComponent.h"
#include "DreamTraceComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "Camera/CameraComponent.h"
#include "Fonts/FontCache.h"
#include "Fonts/FontMeasure.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerState.h"
#include "InkCanvasComponent.h"
#include "NiceInkAudio.h"
#include "NiceInkCharacter.h"
#include "NiceInkGameInstance.h"
#include "NiceInkGameMode.h"
#include "NiceInkGameState.h"
#include "NiceInkFaceShare.h"
#include "NiceInkPlayerState.h"
#include "NiceInkTypes.h"
#include "NiceInkUiTokens.h"

TAutoConsoleVariable<int32> CVarNiDebugHud(
	TEXT("ni.DebugHud"), 0,
	TEXT("1 = show developer HUD telemetry (console hints, seat/id, version stamps)."));

// 調色盤搬進 NiceInkUiTokens.h（主選單 HUD 共用同一組 token）

void ANiceInkHUD::BeginPlay()
{
	Super::BeginPlay();
	EnsureUiAssets();

	// BGM 喚起點：基底 HUD＝主選單與道場共同入口（MenuHUD 繼承本類）
	if (UNiceInkGameInstance* Inst = UNiceInkGameInstance::Get(this))
	{
		Inst->EnsureBgmPlaying(GetWorld());
	}
}

UFont* ANiceInkHUD::BuildCompositeUiFont(UObject* Outer, const TCHAR* FontName)
{
	// runtime 複合字體（Slate 的 FSlateFontInfo 只吃 UFont，裸 FontFace＝豆腐字）。
	// 六文字系統矩陣（2026-08-06 對齊 Meccha 13 語調查；08-07 抽共用——名字上
	// 局內畫面後，局內 HUD 也要全覆蓋）：
	// 預設面＝圓體（輔助）＋Zen Old Mincho（標題/動作，拉丁+日文）；
	// SubTypeface＝繁中/簡中（Han 統一碼同域→用 culture 區分）、韓文、
	// 西里爾+擴展拉丁、阿拉伯；Fallback＝源流明體（en/ja 文化下的雜漢字保底）。
	// 每個面都提供同名五席（Regular/Bold/Serif/SerifRegular/SerifBlack）＝
	// 字面名在任何文字系統下都解析得到。
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

	// Noto Sans 一族（2026-09-05 中性制；13 語同一家）：預設面＝Latin／Greek／Cyrillic；
	// SubTypeface＝繁中／簡中（Han 同碼域靠 culture 分流）、韓文、日文（其餘文化的假名與
	// 漢字）、阿拉伯；Fallback＝日文面。每面提供同名五席（Regular/Bold/Serif/SerifRegular/
	// SerifBlack）——**Serif 席位一律映到無襯線**，舊呼叫點（bSerifFace）不必改。
	UObject* SansReg  = Load(TEXT("/Game/UI/Fonts/FF_NotoSans_Regular.FF_NotoSans_Regular"));
	UObject* SansBold = Load(TEXT("/Game/UI/Fonts/FF_NotoSans_Bold.FF_NotoSans_Bold"));
	if (!SansReg && !SansBold)
	{
		return GEngine ? GEngine->GetMediumFont() : nullptr;
	}

	UFont* Composite = NewObject<UFont>(Outer, FontName);
	Composite->FontCacheType = EFontCacheType::Runtime;
	FCompositeFont& CF = Composite->GetMutableInternalCompositeFont();
	FillTypeface(CF.DefaultTypeface, SansReg, SansBold, SansBold, SansReg, SansBold);

	struct FScript
	{
		const TCHAR* Prefix;      // /Game/UI/Fonts/FF_<Prefix>_<Weight>
		const TCHAR* Cultures;    // 空=不限文化（順序即優先序：有文化限制的先列）
		std::initializer_list<TPair<int32, int32>> Ranges;
	};
	const FScript Scripts[] = {
		{ TEXT("NotoSansTC"), TEXT("zh-Hant;zh-TW;zh-HK;zh-MO"),
			{ {0x2E80, 0x303F}, {0x3400, 0x4DBF}, {0x4E00, 0x9FFF}, {0xF900, 0xFAFF}, {0xFF00, 0xFFEF} } },
		{ TEXT("NotoSansSC"), TEXT("zh-Hans;zh-CN;zh-SG;zh"),
			{ {0x2E80, 0x303F}, {0x3400, 0x4DBF}, {0x4E00, 0x9FFF}, {0xF900, 0xFAFF}, {0xFF00, 0xFFEF} } },
		{ TEXT("NotoSansKR"), TEXT(""),
			{ {0x1100, 0x11FF}, {0x3130, 0x318F}, {0xA960, 0xA97F}, {0xAC00, 0xD7FF} } },
		{ TEXT("NotoSansJP"), TEXT(""),
			{ {0x2E80, 0x303F}, {0x3040, 0x30FF}, {0x31F0, 0x31FF}, {0x3400, 0x4DBF},
			  {0x4E00, 0x9FFF}, {0xF900, 0xFAFF}, {0xFF00, 0xFFEF} } },
		{ TEXT("NotoSansArabic"), TEXT(""),
			{ {0x0600, 0x06FF}, {0x0750, 0x077F}, {0x08A0, 0x08FF}, {0xFB50, 0xFDFF}, {0xFE70, 0xFEFF} } },
	};
	for (const FScript& S : Scripts)
	{
		UObject* Reg = Load(*FString::Printf(TEXT("/Game/UI/Fonts/FF_%s_Regular.FF_%s_Regular"), S.Prefix, S.Prefix));
		UObject* Bold = Load(*FString::Printf(TEXT("/Game/UI/Fonts/FF_%s_Bold.FF_%s_Bold"), S.Prefix, S.Prefix));
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
		FillTypeface(Sub.Typeface, Reg ? Reg : Bold, Bold ? Bold : Reg, Bold ? Bold : Reg, Reg ? Reg : Bold, Bold ? Bold : Reg);
	}

	// Fallback：任何文化下撞到的雜字保底。**簡中面**不是日文面——實測 en 文化下
	// 「简体中文」的「简」在日文面缺字＝豆腐（Han 碼域先命中 JP 子面、JP 沒這個字）；
	// 簡中面的 Han 集合最廣（GB 18030 含大量繁體與日用字）。
	if (UObject* FbReg = Load(TEXT("/Game/UI/Fonts/FF_NotoSansSC_Regular.FF_NotoSansSC_Regular")))
	{
		UObject* FbBold = Load(TEXT("/Game/UI/Fonts/FF_NotoSansSC_Bold.FF_NotoSansSC_Bold"));
		FillTypeface(CF.FallbackTypeface.Typeface, FbReg, FbBold ? FbBold : FbReg,
			FbBold ? FbBold : FbReg, FbReg, FbBold ? FbBold : FbReg);
	}
	return Composite;
}

void ANiceInkHUD::EnsureUiAssets()
{
	if (UiFont)
	{
		return;
	}

	// 13 語矩陣與選單同一座（08-07 抽共用）：名字＝任何語言、局內照樣顯示
	UiFont = BuildCompositeUiFont(this, TEXT("NiUiFont"));

	IconCup    = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Icons/T_UI_Cup.T_UI_Cup"));
	IconSpray  = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Icons/T_UI_Spray.T_UI_Spray"));
	IconKick   = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Icons/T_UI_Kick.T_UI_Kick"));
	IconMarker = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Icons/T_UI_Marker.T_UI_Marker"));
	IconCash   = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Icons/T_UI_Cash.T_UI_Cash"));
	IconRotate = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Icons/T_UI_Rotate.T_UI_Rotate"));
	IconEye    = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Icons/T_UI_Eye.T_UI_Eye"));
	IconTrap   = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Icons/T_UI_Trap.T_UI_Trap"));
	IconSleep  = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Icons/T_UI_Sleep.T_UI_Sleep"));
	IconNose   = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Icons/T_UI_Nose.T_UI_Nose"));
	InMouseLeft   = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Input/T_InMouseLeft.T_InMouseLeft"));
	InMouseRight  = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Input/T_InMouseRight.T_InMouseRight"));
	InMouseScroll = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Input/T_InMouseScroll.T_InMouseScroll"));
	InMouseMove   = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Input/T_InMouseMove.T_InMouseMove"));
	PenSprite  = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Icons/T_UI_TattooPen.T_UI_TattooPen"));
	MarkerSprite = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Icons/T_UI_MarkerPen.T_UI_MarkerPen"));

	// 圓角方塊紋理：96²、角半徑 32、SDF alpha 1px 羽化——9-slice 任意尺寸取用，
	// 縮小取樣只會更平滑（canvas 三角形零 AA，圓角一律走紋理 alpha）
	if (!RoundedTex)
	{
		constexpr int32 TexSize = 96;
		constexpr float CornerR = 32.0f;
		UTexture2D* Tex = UTexture2D::CreateTransient(TexSize, TexSize, PF_B8G8R8A8);
		Tex->SRGB = false;
		Tex->NeverStream = true;
		FTexture2DMipMap& Mip = Tex->GetPlatformData()->Mips[0];
		uint8* Data = static_cast<uint8*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		const float Half = TexSize * 0.5f;
		for (int32 y = 0; y < TexSize; ++y)
		{
			for (int32 x = 0; x < TexSize; ++x)
			{
				const float Px = FMath::Abs(x + 0.5f - Half) - (Half - CornerR);
				const float Py = FMath::Abs(y + 0.5f - Half) - (Half - CornerR);
				const float Dist = FVector2D(FMath::Max(Px, 0.0f), FMath::Max(Py, 0.0f)).Size()
					+ FMath::Min(FMath::Max(Px, Py), 0.0f) - CornerR;
				const uint8 A = static_cast<uint8>(FMath::Clamp(0.5f - Dist, 0.0f, 1.0f) * 255.0f + 0.5f);
				uint8* Px4 = Data + (y * TexSize + x) * 4;
				Px4[0] = 255; Px4[1] = 255; Px4[2] = 255; Px4[3] = A; // BGRA、白底吃 tint
			}
		}
		Mip.BulkData.Unlock();
		Tex->UpdateResource();
		RoundedTex = Tex;
	}

	// 暈的衰減曲線（64×1）：alpha = (e^-kt - e^-k)/(1 - e^-k)，k=3。
	// t=0 → 1（貼著主線，也是主線自己取的那一格）、t=1 → 0（尾巴真的收乾淨；
	// 不減尾值的話最外緣會留 5% 的殘影＝又一條看得見的邊界）。
	// **為什麼是指數**：真實的陰影／光暈是貼著源頭很濃、很快掉下來、再拖一條長淡
	// 尾巴；線性衰減等速下降，邊緣有一個可辨識的終點，眼睛會把它讀成一條帶的邊界。
	if (!GlowTex)
	{
		constexpr int32 N = 64;
		constexpr float K = 3.0f;
		UTexture2D* Tex = UTexture2D::CreateTransient(N, 1, PF_B8G8R8A8);
		Tex->SRGB = false;
		Tex->NeverStream = true;
		Tex->Filter = TF_Bilinear;
		Tex->AddressX = TA_Clamp;
		Tex->AddressY = TA_Clamp;
		FTexture2DMipMap& Mip = Tex->GetPlatformData()->Mips[0];
		uint8* Data = static_cast<uint8*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		const float Tail = FMath::Exp(-K);
		for (int32 x = 0; x < N; ++x)
		{
			const float T = x / static_cast<float>(N - 1);
			const float A = (FMath::Exp(-K * T) - Tail) / (1.0f - Tail);
			uint8* Px4 = Data + x * 4;
			Px4[0] = 255; Px4[1] = 255; Px4[2] = 255; // BGRA、白底吃頂點 tint
			Px4[3] = static_cast<uint8>(FMath::Clamp(A, 0.0f, 1.0f) * 255.0f + 0.5f);
		}
		Mip.BulkData.Unlock();
		Tex->UpdateResource();
		GlowTex = Tex;
	}

	// 上緣壓暗的**垂直漸層**（1×64）。一版是 48 個 DrawRect 疊出來的，
	// **而 robo 當場抓到 `cruise tipSpd` 2.05（上限 2.00、基線 1.97）**——
	// 那條契約只有 0.5% 餘裕，針以 v_max 逐幀追游標，每幀成本一升實走距離就變長。
	// 這是本專案第三次踩同一個坑（09-03 兩次），而規矩已經寫死：
	// **「兩輪穩定重現」不可以用 flake 解釋**，而幀率工具說各段正常＝要當真迴歸查。
	// 修法不是調參數是換實作：一張貼圖 **一次 DrawTile** 取代 48 次 DrawRect，
	// 視覺完全一樣（雙線性插值比 48 階更平滑），成本回到常數。
	if (!ScrimTex)
	{
		constexpr int32 N = 64;
		UTexture2D* Tex = UTexture2D::CreateTransient(1, N, PF_B8G8R8A8);
		Tex->SRGB = false;
		Tex->NeverStream = true;
		Tex->Filter = TF_Bilinear;
		Tex->AddressX = TA_Clamp;
		Tex->AddressY = TA_Clamp;
		FTexture2DMipMap& Mip = Tex->GetPlatformData()->Mips[0];
		uint8* Data = static_cast<uint8*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		// 版面：前 65% 維持峰值（那是字所在的地），其後 smoothstep 收到 0。
		// **地是給字用的，不是給邊緣用的**——一版把最暗的地方放在沒有字的最上緣，
		// 結果巡禮的對比只從 1.27 修到 1.89（仍不及格）。
		constexpr float Hold = 0.65f;
		for (int32 y = 0; y < N; ++y)
		{
			const float T = y / static_cast<float>(N - 1);
			float A = 1.0f;
			if (T > Hold)
			{
				const float U = FMath::Clamp((T - Hold) / (1.0f - Hold), 0.0f, 1.0f);
				A = 1.0f - (U * U * (3.0f - 2.0f * U));
			}
			uint8* Px4 = Data + y * 4;
			Px4[0] = 255; Px4[1] = 255; Px4[2] = 255; // BGRA、白底吃頂點 tint
			Px4[3] = static_cast<uint8>(FMath::Clamp(A, 0.0f, 1.0f) * 255.0f + 0.5f);
		}
		Mip.BulkData.Unlock();
		Tex->UpdateResource();
		ScrimTex = Tex;
	}
}

void ANiceInkHUD::DrawRoundedBox(float X, float Y, float W, float H, float Radius, const FLinearColor& ColorIn)
{
	FLinearColor Color = ColorIn;
	Color.A *= ChromeAlphaMul * ChromeAlphaBase;
	if (!Canvas || W <= 0.0f || H <= 0.0f)
	{
		return;
	}
	X = FlipXW(X, W); // AR 鏡像（9-slice 左右對稱＝翻左緣即可）
	if (!RoundedTex)
	{
		DrawRect(Color, X, Y, W, H); // 資產缺失保底
		return;
	}
	// 螢幕角尺寸 C：鉗到不超過半寬半高；UV 角固定 1/3（紋理角 32/96）
	const float C = FMath::Clamp(Radius, 1.0f, FMath::Min(W, H) * 0.5f);
	constexpr float UvC = 32.0f / 96.0f;
	const float Xs[4] = { X, X + C, X + W - C, X + W };
	const float Ys[4] = { Y, Y + C, Y + H - C, Y + H };
	const float Us[4] = { 0.0f, UvC, 1.0f - UvC, 1.0f };
	for (int32 Row = 0; Row < 3; ++Row)
	{
		for (int32 Col = 0; Col < 3; ++Col)
		{
			const float TileW = Xs[Col + 1] - Xs[Col];
			const float TileH = Ys[Row + 1] - Ys[Row];
			if (TileW <= 0.0f || TileH <= 0.0f)
			{
				continue;
			}
			Canvas->K2_DrawTexture(RoundedTex, FVector2D(Xs[Col], Ys[Row]), FVector2D(TileW, TileH),
				FVector2D(Us[Col], Us[Row]), FVector2D(Us[Col + 1] - Us[Col], Us[Row + 1] - Us[Row]),
				Color, BLEND_Translucent);
		}
	}
}

float ANiceInkHUD::TierSize(ETextTier Tier) const
{
	switch (Tier)
	{
	case ETextTier::Display: return NiType::HudDisplay;
	case ETextTier::Title:   return NiType::HudTitle;
	case ETextTier::Body:    return NiType::HudBody;
	default:                 return NiType::HudSmall;
	}
}

namespace
{
	// 需要整形/雙向排序的碼域（阿拉伯/希伯來等 RTL＋呈現形＋印度系/泰寮緬）：
	// 命中才走 HarfBuzz 整形路——拉丁/CJK/韓文走原快路徑零變動（整形每幀有成本）
	bool NiTextNeedsShaping(const FString& Text)
	{
		for (const TCHAR C : Text)
		{
			if ((C >= 0x0590 && C <= 0x08FF) || (C >= 0x0900 && C <= 0x0DFF) ||
				(C >= 0x0E00 && C <= 0x0EFF) || (C >= 0x1000 && C <= 0x109F) ||
				(C >= 0xFB1D && C <= 0xFDFF) || (C >= 0xFE70 && C <= 0xFEFF))
			{
				return true;
			}
		}
		return false;
	}
}

float ANiceInkHUD::FlipX(float X) const
{
	return (bRTLLayout && !bMirrorSuspended && Canvas) ? Canvas->ClipX - X : X;
}

float ANiceInkHUD::FlipXW(float X, float W) const
{
	return (bRTLLayout && !bMirrorSuspended && Canvas) ? Canvas->ClipX - X - W : X;
}

FVector2D ANiceInkHUD::MeasureTok(const FString& Text, ETextTier Tier, bool bBold)
{
	if (!Canvas || !UiFont || !FSlateApplication::IsInitialized())
	{
		return FVector2D::ZeroVector;
	}
	const int32 SizePx = FMath::Max(8, FMath::RoundToInt(TierSize(Tier) * UiScale));
	// 量測與繪製必須讀同一個字面（明朝體與圓體的前進寬度不同——量一個畫另一個
	// ＝置中與右對齊全部偏掉，而且只在切了明朝體的那幾行偏）
	const FSlateFontInfo Info(UiFont, SizePx, bSerifFace
		? (bBold ? FName("SerifBlack") : FName("SerifRegular"))
		: (bBold ? FName("Bold") : FName("Regular")));
	if (NiTextNeedsShaping(Text))
	{
		// 整形量測（阿拉伯連寫後寬度≠逐字距總和）
		auto FontCache = FSlateApplication::Get().GetRenderer()->GetFontCache();
		const FShapedGlyphSequenceRef Shaped = FontCache->ShapeBidirectionalText(
			Text, Info, Canvas->GetDPIScale(), TextBiDi::ComputeBaseDirection(Text), ETextShapingMethod::Auto);
		return FVector2D(Shaped->GetMeasuredWidth(), Shaped->GetMaxTextHeight());
	}
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	return FVector2D(Measure->Measure(Text, Info, Canvas->GetDPIScale()));
}

FVector2D ANiceInkHUD::DrawTok(const FString& Text, float X, float Y, ETextTier Tier,
	const FLinearColor& ColorIn, EHAlign Align, bool bBold)
{
	if (!Canvas || !UiFont || Text.IsEmpty())
	{
		return FVector2D::ZeroVector;
	}
	if (IsMirrored())
	{
		// AR 鏡像：錨點翻面＋左右對齊互換（置中不變——ClipX-X 對 W/2 是恆等）
		X = Canvas->ClipX - X;
		Align = (Align == EHAlign::Left) ? EHAlign::Right
			: (Align == EHAlign::Right) ? EHAlign::Left : EHAlign::Center;
	}
	FLinearColor Color = ColorIn;
	Color.A *= ChromeAlphaMul * ChromeAlphaBase;
	const FVector2D Size = MeasureTok(Text, Tier, bBold);
	if (Align == EHAlign::Center)
	{
		X -= Size.X * 0.5f;
	}
	else if (Align == EHAlign::Right)
	{
		X -= Size.X;
	}
	const int32 SizePx = FMath::Max(8, FMath::RoundToInt(TierSize(Tier) * UiScale));
	// 字面名：複合字體的每一個文字系統面都提供同名五席（BuildCompositeUiFont），
	// 所以切明朝體在 13 語下都解析得到，不會掉成豆腐字。
	const FSlateFontInfo Info(UiFont, SizePx, bSerifFace
		? (bBold ? FName("SerifBlack") : FName("SerifRegular"))
		: (bBold ? FName("Bold") : FName("Regular")));

	if (NiTextNeedsShaping(Text))
	{
		// 整形路（2026-08-07）：阿拉伯文等連寫文字＝HarfBuzz 整形＋BiDi 排序後
		// 以 shaped item 上屏——FCanvasTextItem 逐碼位直畫＝斷筆+左右顛倒
		auto FontCache = FSlateApplication::Get().GetRenderer()->GetFontCache();
		const FShapedGlyphSequenceRef Shaped = FontCache->ShapeBidirectionalText(
			Text, Info, Canvas->GetDPIScale(), TextBiDi::ComputeBaseDirection(Text), ETextShapingMethod::Auto);
		FCanvasShapedTextItem Item(FVector2D(X, Y), Shaped, Color);
		if (bTokShadows)
		{
			Item.EnableShadow(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f), FVector2D(1.0f, 1.0f) * FMath::Max(1.0f, UiScale));
		}
		Canvas->DrawItem(Item);
		return Size;
	}

	FCanvasTextItem Item(FVector2D(X, Y), FText::FromString(Text), Info, Color);
	if (bTokShadows)
	{
		Item.EnableShadow(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f), FVector2D(1.0f, 1.0f) * FMath::Max(1.0f, UiScale));
	}
	Canvas->DrawItem(Item);
	return Size;
}

void ANiceInkHUD::DrawPanelBox(float X, float Y, float W, float H, float Alpha)
{
	// 素色簡約風（2026-08-05 user 定案：Meccha/Schedule I 式半透明面板）：
	// 圓角半透墨面單塊——一次繪製無疊蓋＝無接縫
	FLinearColor C = NiHudColor::Ink;
	C.A = Alpha;
	DrawRoundedBox(X, Y, W, H, 12.0f * UiScale, C);  // 與選單控制項同一階
}

void ANiceInkHUD::DrawIconRect(UTexture2D* Tex, float X, float Y, float W, float H, const FLinearColor& Tint)
{
	if (!Canvas || !Tex)
	{
		return;
	}
	X = FlipXW(X, W);
	FLinearColor T = Tint;
	T.A *= ChromeAlphaMul * ChromeAlphaBase;
	Canvas->K2_DrawTexture(Tex, FVector2D(X, Y), FVector2D(W, H),
		FVector2D::ZeroVector, FVector2D::UnitVector, T, BLEND_Translucent);
}

UTexture2D* ANiceInkHUD::GetFaceIcon(int32 AvatarIdx)
{
	if (AvatarIdx < 0 || AvatarIdx >= FNiceInkAvatars::Num())
	{
		return nullptr;
	}
	if (TObjectPtr<UTexture2D>* Found = FaceIconCache.Find(AvatarIdx))
	{
		return *Found;
	}
	UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *FNiceInkAvatars::Get(AvatarIdx).FaceOpenPath);
	FaceIconCache.Add(AvatarIdx, Tex);
	return Tex;
}

float ANiceInkHUD::DrawFaceTok(const APlayerState* PS, float X, float Y, float Size)
{
	// 臉像＝身分載體（SPEC #52 臉制）。2026-08-12 頭像亭肖像制：icon＝那顆頭
	// 本人（丁髷+膚色+臉）的 3D 正面肖像——辨識對象與場上一致；亭未就緒
	//（暖機/PIE 範圍閘）＝退回舊的貼圖裁切路墊檔。
	const ANiceInkPlayerState* NIPS = Cast<ANiceInkPlayerState>(PS);
	if (!NIPS)
	{
		return 0.0f;
	}

	UTexture* Portrait = nullptr;
	if (ANiceInkPortraitBooth* Booth = ANiceInkPortraitBooth::Get(this))
	{
		if (UNiceInkFaceShare* Share = UNiceInkFaceShare::Get(this))
		{
			if (UTexture2D* Open = Share->GetOpen(NIPS->SeatIndex))
			{
				// 快取鍵含分發版本＝換臉自動重烘
				Portrait = Booth->GetPortraitKeyed(
					FString::Printf(TEXT("seat%d_v%d"), NIPS->SeatIndex, Share->GetRevision(NIPS->SeatIndex)),
					Open, Share->GetClosed(NIPS->SeatIndex), Share->GetMask(NIPS->SeatIndex),
					Share->GetTone(NIPS->SeatIndex));
			}
		}
		if (!Portrait)
		{
			Portrait = Booth->GetPortraitRoster(NIPS->AvatarIndex);
		}
	}
	if (Portrait)
	{
		// icon＝頭的形狀（透明背景裁切成品；user 定案「不是方形照片」）——
		// 直接畫、無框無底。AR 鏡像在此翻一次（臉本體不左右翻）
		X = FlipXW(X, Size);
		TGuardValue<bool> MirrorGuard(bMirrorSuspended, true);
		Canvas->K2_DrawTexture(Portrait, FVector2D(X, Y), FVector2D(Size, Size),
			FVector2D::ZeroVector, FVector2D::UnitVector,
			FLinearColor(1.0f, 1.0f, 1.0f, ChromeAlphaMul * ChromeAlphaBase), BLEND_Translucent);
		return Size;
	}

	// --- 舊路墊檔：膚色底＋臉區 UV 裁切 ---
	UTexture2D* Face = nullptr;
	if (UNiceInkFaceShare* Share = UNiceInkFaceShare::Get(this))
	{
		Face = Share->GetOpen(NIPS->SeatIndex);
	}
	if (!Face)
	{
		Face = GetFaceIcon(NIPS->AvatarIndex);
	}
	if (!Face)
	{
		return 0.0f;
	}
	X = FlipXW(X, Size);
	TGuardValue<bool> MirrorGuard(bMirrorSuspended, true);
	FLinearColor Frame = NiHudColor::Paper;
	Frame.A = 0.9f;
	const float Pad = FMath::Max(1.5f, Size * 0.05f);
	DrawRoundedBox(X - Pad, Y - Pad, Size + Pad * 2, Size + Pad * 2, Size * 0.18f, Frame);
	DrawRoundedBox(X, Y, Size, Size, Size * 0.14f, NiHudColor::Skin);
	Canvas->K2_DrawTexture(Face, FVector2D(X, Y), FVector2D(Size, Size),
		FVector2D(0.30f, 0.22f), FVector2D(0.40f, 0.40f),
		FLinearColor(1.0f, 1.0f, 1.0f, ChromeAlphaMul * ChromeAlphaBase), BLEND_Translucent);
	return Size;
}

void ANiceInkHUD::DrawIconTok(UTexture2D* Tex, float X, float Y, float Size, const FLinearColor& Tint)
{
	if (!Canvas || !Tex)
	{
		return;
	}
	X = FlipXW(X, Size); // AR 鏡像（位置翻面、圖示本體不左右翻）
	FLinearColor T = Tint;
	T.A *= ChromeAlphaMul * ChromeAlphaBase;
	Canvas->K2_DrawTexture(Tex, FVector2D(X, Y), FVector2D(Size, Size),
		FVector2D::ZeroVector, FVector2D::UnitVector, T, BLEND_Translucent);
}

FString ANiceInkHUD::FitTok(const FString& Text, ETextTier Tier, float MaxWidthPx, bool bBold)
{
	if (MeasureTok(Text, Tier, bBold).X <= MaxWidthPx)
	{
		return Text;
	}
	const FString Ellipsis = TEXT("…");
	for (int32 Len = Text.Len() - 1; Len > 0; --Len)
	{
		const FString Candidate = Text.Left(Len) + Ellipsis;
		if (MeasureTok(Candidate, Tier, bBold).X <= MaxWidthPx)
		{
			return Candidate;
		}
	}
	return Ellipsis;
}

// ---- 即時模式 UI 互動（主選單／ESC 選單共用）----

void ANiceInkHUD::BeginUiFrame()
{
	MousePos = FVector2D::ZeroVector;
	bClickThisFrame = false;
	bClickConsumed = false;
	if (PlayerOwner)
	{
		float MX = 0.0f, MY = 0.0f;
		PlayerOwner->GetMousePosition(MX, MY);
		MousePos = FVector2D(MX, MY);
		bClickThisFrame = PlayerOwner->WasInputKeyJustPressed(EKeys::LeftMouseButton);

		// 去抖：實測同一實體點擊可在連續兩幀都讀成 just-pressed（打包版 60fps，
		// 雙重建房的元凶之一）——150ms 內的第二擊一律吞掉
		if (bClickThisFrame && GetWorld())
		{
			const double Now = GetWorld()->GetRealTimeSeconds();
			if (Now - LastUiClickTime < 0.15)
			{
				bClickThisFrame = false;
			}
			else
			{
				LastUiClickTime = Now;
			}
		}
	}
}

bool ANiceInkHUD::Button(const FString& Label, float CenterX, float Y, float W, float H,
	bool bEnabled, bool bAccent, bool bOnLight)
{
	const float X = CenterX - W * 0.5f;
	// 命中判定用物理座標（滑鼠活在物理空間；AR 鏡像時按鈕畫在翻面位置）
	const float HitX = FlipXW(X, W);
	const bool bHover = bEnabled &&
		MousePos.X >= HitX && MousePos.X <= HitX + W && MousePos.Y >= Y && MousePos.Y <= Y + H;

	// 素色簡約風：無邊框、圓角；主按鈕（accent）＝實心酒金＋墨字；
	// 白卡上（bOnLight）＝墨填墨字、深底上＝紙填紙字。
	// hover＝填色階梯跳變（硬切、不做漸變動畫）
	const float Radius = FMath::Min(12.0f * UiScale, H * 0.5f);
	FLinearColor Fill;
	FLinearColor TextColor;
	if (!bEnabled)
	{
		Fill = bOnLight ? NiHudColor::Ink : NiHudColor::Paper;
		Fill.A = 0.05f;
		TextColor = bOnLight ? NiHudColor::InkDim : NiHudColor::PaperDim;
		TextColor.A = 0.6f;
	}
	else if (bAccent)
	{
		// 中性制：主鈕＝強調色實填＋白字（強調色只落在深色面上——它自己就是那個面）
		Fill = bHover ? NiHudColor::AccentText : NiHudColor::Accent;
		Fill.A = 1.0f;
		TextColor = NiHudColor::OnAccent;
	}
	else if (bOnLight)
	{
		Fill = NiHudColor::Ink;     Fill.A = bHover ? 0.16f : 0.07f;
		TextColor = NiHudColor::Ink;
	}
	else
	{
		Fill = NiHudColor::Paper;   Fill.A = bHover ? 0.24f : 0.10f;
		TextColor = NiHudColor::Paper;
	}
	DrawRoundedBox(X, Y, W, H, Radius, Fill);

	const FVector2D TextSize = MeasureTok(Label, ETextTier::Body, bAccent);
	DrawTok(Label, CenterX, Y + (H - TextSize.Y) * 0.5f, ETextTier::Body, TextColor, EHAlign::Center, bAccent);

	if (bHover && bClickThisFrame && !bClickConsumed)
	{
		bClickConsumed = true;
		NiAudio::Play(this, ENiSound::UiClick);
		return true;
	}
	return false;
}

int32 ANiceInkHUD::AdjustRow(const FString& Label, const FString& Value, float CenterX, float Y,
	bool bLeftEnabled, bool bRightEnabled)
{
	const float RowH = 40.0f * UiScale;
	DrawTok(Label, CenterX - 40.0f * UiScale, Y + 8.0f * UiScale, ETextTier::Body, NiHudColor::PaperDim, EHAlign::Right, false);
	DrawTok(Value, CenterX + 170.0f * UiScale, Y + 8.0f * UiScale, ETextTier::Body, NiHudColor::Paper, EHAlign::Center, false);

	int32 Delta = 0;
	if (Button(TEXT("<"), CenterX + 30.0f * UiScale, Y, 36.0f * UiScale, RowH, bLeftEnabled))
	{
		Delta = -1;
	}
	if (Button(TEXT(">"), CenterX + 310.0f * UiScale, Y, 36.0f * UiScale, RowH, bRightEnabled))
	{
		Delta = +1;
	}
	return Delta;
}

void ANiceInkHUD::DrawBigTitle(const FString& Text, float CenterX, float Y, float SizePx, const FLinearColor& Color)
{
	if (!Canvas || !UiFont)
	{
		return;
	}
	const FSlateFontInfo Info(UiFont, FMath::RoundToInt(SizePx * UiScale), FName("Bold"));
	FCanvasTextItem Item(FVector2D(0, 0), FText::FromString(Text), Info, Color);
	Item.bCentreX = true;
	Item.Position = FVector2D(FlipX(CenterX), Y); // AR 鏡像（置中錨翻面）
	if (bTokShadows)
	{
		Item.EnableShadow(FLinearColor(0, 0, 0, 0.6f), FVector2D(2.0f, 2.0f) * UiScale);
	}
	Canvas->DrawItem(Item);
}

void ANiceInkHUD::DrawCupsRow(float X, float Y, float CupSize, int32 Filled, EHAlign Align)
{
	const float Step = CupSize * 1.18f;
	float StartX = X;
	if (Align == EHAlign::Center)
	{
		StartX -= (Step * 2.0f + CupSize) * 0.5f;
	}
	else if (Align == EHAlign::Right)
	{
		StartX -= Step * 2.0f + CupSize;
	}
	for (int32 i = 0; i < 3; ++i)
	{
		FLinearColor Tint;
		if (i < Filled)
		{
			Tint = NiHudColor::Paper;
		}
		else
		{
			// 第二杯起，空杯轉紅：懸崖警示全場一眼可讀
			Tint = (Filled >= 2) ? NiHudColor::Red : NiHudColor::Paper;
			Tint.A = 0.28f;
		}
		DrawIconTok(IconCup, StartX + i * Step, Y, CupSize, Tint);
	}
}

void ANiceInkHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas || !GetWorld())
	{
		return;
	}
	EnsureUiAssets();
	UiScale = Canvas->ClipY / 1080.0f;

	// AR 版面鏡像（v4.0e）：跟語言設定即時刷新（文化=ar → chrome 全鏡像）
	{
		const UNiceInkGameInstance* NiGI = UNiceInkGameInstance::Get(this);
		bRTLLayout = NiGI && FCString::Strcmp(
			NiLoc::LangCultureCode(NiGI->GetMenuLanguage()), TEXT("ar")) == 0;
	}

	const ANiceInkGameState* GS = GetWorld()->GetGameState<ANiceInkGameState>();
	ANiceInkCharacter* MyChar = PlayerOwner ? Cast<ANiceInkCharacter>(PlayerOwner->GetPawn()) : nullptr;
	const ANiceInkPlayerState* MyPS = PlayerOwner ? PlayerOwner->GetPlayerState<ANiceInkPlayerState>() : nullptr;

	BeginUiFrame(); // ESC 選單的按鈕判定
	TickAudioCues(GS);

	// 相位切換淡入（2026-09-05 中性制的動態）：所有 chrome 原語共用一個乘數，
	// 從相位改變的時刻起 NiUi::FadeS 秒 smoothstep 到 1。沒有任何動態的介面，
	// 在動起來的第一秒就會被讀成 mockup——這是「半成品感」最大的單一來源。
	ChromeAlphaBase = 1.0f;
	if (PhaseChangedAt >= 0.0)
	{
		const float T = FMath::Clamp(
			static_cast<float>((GetWorld()->GetTimeSeconds() - PhaseChangedAt) / NiUi::FadeS), 0.0f, 1.0f);
		ChromeAlphaBase = T * T * (3.0f - 2.0f * T);
	}

	// 進房載入布（08-14）：臉同步齊全前蓋整屏（本人臉上行 ack＋manifest 席位全入簿；
	// 8s 保底掀開）——搭配現身閘＝房內從頭到尾不存在頂著名冊臉的力士
	if (MyChar && MyChar->IsJoinFaceSyncPending())
	{
		DrawRect(FLinearColor(0.01f, 0.01f, 0.015f, 1.0f), 0.0f, 0.0f, Canvas->ClipX, Canvas->ClipY);
		DrawBottomHint(NiLoc::T(this, ENiLocKey::StatusJoining), NiHudColor::PaperDim);
		return;
	}

	// 沉睡端：視覺全遮蔽——黑屏＋醉夢迷宮＋姿勢面板，其他 HUD 一概不畫
	if (MyChar && MyChar->bAsleep && !MyChar->bEyesOpen)
	{
		DrawVictimSleepUI(MyChar, GS, MyPS);
		DrawControlStrip(GS, MyChar);
		DrawRulesBlock(GS, MyChar, true);
		DrawScoreCorner(GS);   // 與規則塊分時共用同一個角（見 DrawHUD 尾端）
		DrawTrapDial(MyChar);
		DrawSystemMenu(MyChar);
		DrawDebugPanel(GS, MyPS, MyChar);
		return;
	}

	// 裝睡（Shift 按住）：對旁人＝沉睡姿勢＋閉眼貼圖；對本人＝閉眼就是看不到
	//（定案 #42 恆等式：你看得到的≡臉表達的——裝睡不是免費監視器，
	// 何時敢重新睜眼本身是賭注）。黑屏上只留一行提示。
	if (MyChar && MyChar->IsFeigningSleep())
	{
		DrawRect(FLinearColor(0.01f, 0.01f, 0.015f, 1.0f), 0.0f, 0.0f, Canvas->ClipX, Canvas->ClipY);
		DrawControlStrip(GS, MyChar);
		DrawScoreCorner(GS);
		DrawTrapDial(MyChar);
		DrawSystemMenu(MyChar);
		DrawDebugPanel(GS, MyPS, MyChar);
		return;
	}

	// 開場動畫期間＝**HUD 全部讓開**（2026-08-28）。這是一段導演鏡頭的過場：
	// 準星釘在畫面正中央，而 IntroNotice 的機位正是把電視放在正中央 ⇒ 準星整整
	// 十秒壓在螢幕上；頂欄還在倒數「BOTTLE SPIN · 20s」，但這一段玩家什麼都不能操作。
	// 追記85 把它記成「v1 刻意未做」，那時特寫只有 2.6 秒＝一眼掃過；08-28 把特寫拉長到
	// 10.4 秒之後同一個瑕疵就變成盯著看的東西——**記帳過的缺口會隨著別處的改動變質，
	// 不是記了就永遠可以不修**。ESC 選單與除錯面板留著（隨時要能退出）。
	if (GS && NiCeremonyStepIsIntro(GS->CeremonyStep))
	{
		DrawSystemMenu(MyChar);
		DrawDebugPanel(GS, MyPS, MyChar);
		return;
	}

	// ESC 選單開著＝只畫選單（醒著沒有遮蔽義務；底層面板文字互疊會打架）
	if (MyChar && MyChar->IsSystemMenuOpen())
	{
		DrawSystemMenu(MyChar);
		DrawDebugPanel(GS, MyPS, MyChar);
		return;
	}

	DrawBlindOverlay(MyChar);
	DrawTopBar(GS, MyPS, MyChar);
	DrawCenterBanners(GS);

	if (GS && GS->CurrentPhase == ENiceInkPhase::Lobby)
	{
		DrawLobbyPanel(GS);
	}

	const bool bIsVictim = GS && MyPS && GS->VictimPlayerId == MyPS->GetPlayerId();
	if (GS && GS->CurrentPhase == ENiceInkPhase::Accusation && bIsVictim && MyChar)
	{
		DrawAccusePanel(GS, MyChar);
	}
	if (GS && GS->CurrentPhase == ENiceInkPhase::PostGame && MyChar)
	{
		DrawPostGamePanel(MyChar);
	}

	// 兇手轉盤覆蓋（不打斷 lean-lock 作畫；只有被踩中陷阱的兇手本人看得到）
	DrawTrapDial(MyChar);

	// 底部情境提示：每畫面至多一行（優先序：翻身表決 > 鎖定操作 > 睜眼 > 翻身入口）
	// 翻身表決只給作畫者看——偷醒的受害者不該從 HUD 白拿「有人要翻你」的情報
	const bool bDrawingArtist = GS && GS->CurrentPhase == ENiceInkPhase::Drawing &&
		MyChar && !MyChar->bAsleep && !bIsVictim;
	if (bDrawingArtist && GS->FlipProposerId != INDEX_NONE)
	{
		const APlayerState* ProposerPS = GS->FindPlayerStateById(GS->FlipProposerId);
		const bool bIVoted = MyChar->FlipAgreedProposalSerial == GS->FlipProposalSerial ||
			(MyPS && GS->FlipProposerId == MyPS->GetPlayerId());
		// 臉制（SPEC #52）：提案人名拔除——翻身是合作提案、誰提的不承重
		DrawBottomHint(NiLoc::TFmt(this, bIVoted ? ENiLocKey::HudFlipWait : ENiLocKey::HudFlipAsk,
			FString::FromInt(GS->FlipAgreeCount), FString::FromInt(GS->FlipAgreeNeeded)),
			NiHudColor::Paper);
	}
	else if (MyChar && MyChar->bLeanLocked)
	{
		// 可畫域標記＝皮膚上的 veil 殼（角色端 UpdateReachVeilShell）；HUD 只補提示行
		// 搆不到持續 >1s＝邊界開口說話（無聲失敗鐵則）：筆收起是物理訊號，
		// 這行話補上「該怎麼辦」
		// 托盤開著＝底部列與作畫提示全讓開（同一份資訊不畫兩次；提示跟著狀態走）
		if (MyChar->bInkTrayOpen)
		{
			DrawBottomHint(NiLoc::T(this, ENiLocKey::TrayRelease), NiHudColor::PaperDim);
			DrawInkTray(MyChar);
		}
		else
		{
			// 一行六項的英文句子已退役（畫面清單 2026-09-02）：它離視線中心 ~45°、
			// 落筆時根本不會被讀。操作提示改成右緣鍵帽縱列（DrawControlStrip）——
			// 形式照 Meccha 實物：**常駐但小、圖像化、貼邊**；問題從來不是常駐與否，
			// 是「句子 vs 鍵帽」。底部只留**活的狀態**：搆不到（琥珀）＝約束不是教學。
			if (MyChar->GetDrawUnreachableSeconds() > 1.0f)
			{
				DrawBottomHint(NiLoc::T(this, ENiLocKey::HudOutOfReach), NiHudColor::Paper);
			}
			DrawInkChip(MyChar); // 手上裝的是哪一杯＝畫面上唯一一份
		}
	}
	else if (MyChar && MyChar->bAsleep && MyChar->bEyesOpen)
	{
		// 無聲甦醒中：實景視野；提示只給受害者本人
		DrawBottomHint(NiLoc::T(this, ENiLocKey::HudEyesOpen), NiHudColor::Paper);
	}
	// 站著時的 F／G 提示已移進右緣操作列（DrawControlStrip）＝一件事只講一次

	// 搖晃購買回執（只給攻擊者本人；不透漏夢內結果）
	if (MyChar && GetWorld() && GetWorld()->GetTimeSeconds() < MyChar->ShakeAckFlashUntil)
	{
		DrawTok(NiLoc::T(this, MyChar->bLastShakeAckBought
			? ENiLocKey::HudShakeBought : ENiLocKey::HudShakeRefused),
			Canvas->ClipX * 0.5f, Canvas->ClipY * 0.22f, ETextTier::Title,
			MyChar->bLastShakeAckBought ? NiHudColor::Paper : NiHudColor::Red, EHAlign::Center, true);
	}

	DrawInkCrosshair(GS);
	DrawControlStrip(GS, MyChar);   // 右緣：模式與動作（glyph 上／動詞下、共用右緣）
	DrawPostureCluster(GS, MyChar); // 底部中央：姿勢與移動（橫排）
	// 右下角一格兩個佔用者、時間錯開（Meccha 同款）：沒有賭注時教規則，
	// 第一杯下去之後永久變成計分板。兩者判準同源（GetVictimPenaltyCups）＝
	// 構造上不可能同時出現，也不可能同時消失。
	DrawRulesBlock(GS, MyChar, bIsVictim);
	DrawScoreCorner(GS);
	DrawDebugPanel(GS, MyPS, MyChar);
}

void ANiceInkHUD::DrawTopScrim(float BottomY)
{
	// 漸層壓暗＝**沒有邊界的地**。面板有邊界（所以它是一個東西），漸層沒有
	// （所以它是光線）。Meccha 的「常駐 chrome 無面板」守的是「不要在畫面上多出
	// 一個東西」，不是「不准壓暗」——他們不需要壓暗只因為他們的世界本來就暗。
	// 40 段：以 Ink 為底、alpha 走 smoothstep 收到 0。段數要夠多，不然在暗天花板上
	// 會看見階梯（每段 alpha 差 <0.015 時肉眼分不出）。
	if (!Canvas || BottomY <= 0.0f)
	{
		return;
	}
	// **一版寫錯，實測抓到兩件事**（2026-09-05）：
	// ① 第一版讓 alpha 從頂端 0.55 直接 smoothstep 收到 BottomY 的 0 ⇒ 最暗的地方
	//    在沒有字的畫面最上緣，字所在的下半段只剩 0.06；巡禮對比 1.27→只到 1.89。
	//    **地是給字用的，不是給邊緣用的**（曲線已搬進 ScrimTex：前 65% 維持峰值）。
	// ② 二版用 48 個 DrawRect 疊漸層 ⇒ robo 抓到 `cruise tipSpd` 2.05（上限 2.00）。
	//    改成**一張 1×64 貼圖、一次 DrawTile**：視覺更平滑（雙線性）、成本常數。
	if (!ScrimTex)
	{
		return;
	}
	const float Tail = BottomY * 0.35f;      // 淡出尾巴（在內容之外；越長越像一條帶）
	const float Total = BottomY + Tail;
	FLinearColor C = NiHudColor::Ink;
	C.A = 0.30f * ChromeAlphaMul * ChromeAlphaBase;
	// 貼圖的 alpha 曲線 × 頂點色的 alpha＝最終濃度；白底貼圖吃 tint（同 GlowTex）
	FCanvasTileItem Tile(FVector2D(0.0f, 0.0f), ScrimTex->GetResource(),
		FVector2D(Canvas->ClipX, Total), FVector2D(0.0f, 0.0f), FVector2D(1.0f, 1.0f), C);
	Tile.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Tile);
}

void ANiceInkHUD::DrawTopBar(const ANiceInkGameState* GS, const ANiceInkPlayerState* MyPS, ANiceInkCharacter* MyChar)
{
	if (!GS || !Canvas)
	{
		return;
	}
	// 落筆時整條 chrome 淡出：橫幅與現金是「兩筆之間」的資訊。
	const bool bInking = MyChar && MyChar->bLeanLocked && MyChar->IsPenTriggerHeldLocal();
	TGuardValue<float> ChromeDim(ChromeAlphaMul, bInking ? 0.35f : 1.0f);

	const float W = Canvas->ClipX;
	const float M = NiUi::Margin * UiScale;
	const float Gap = NiUi::GapS * UiScale;

	// **無面板**（2026-09-04 照抄 Meccha）：他們局內一塊面板都沒有，對比靠文字陰影
	// （bTokShadows 本專案本來就開著）。此前這裡是 Ink α0.55 的圓角面板，而
	// 全站 alpha 有七種、沒有規則——面板拆掉，那個問題就不存在了。
	// **但無面板不等於不用管地**（2026-09-05）：實測障子牆前對比 1.27 ⇒ 先鋪一層
	// 沒有邊界的漸層（DrawTopScrim），再畫字。見該函式的註解。
	// 版面＝倒數（大數字）→ 祈使句 → 受害者，三行共用畫面中線。
	const float Remaining = GS->GetPhaseTimeRemaining();
	const FString Count = (Remaining > 0.0f)
		? FString::FromInt(FMath::CeilToInt(Remaining)) : FString();

	const ANiceInkPlayerState* VictimNIPS =
		Cast<ANiceInkPlayerState>(GS->FindPlayerStateById(GS->VictimPlayerId));
	const bool bIsVictim = MyPS && GS->VictimPlayerId == MyPS->GetPlayerId();
	const FString Imp = GetPhaseImperative(GS, MyChar, bIsVictim);
	// 大廳：房號＝畫面上唯一要唸給朋友聽的東西 ⇒ 主角數字（Hero），接在祈使句下方，
	// 與它共用同一片漸層（房號在白障子牆前，沒有地就是隱形的）。
	// 字母之間留空：它是四個要唸出來的字母，不是一個單字。
	const bool bLobbyCode = GS->CurrentPhase == ENiceInkPhase::Lobby && !GS->RoomCode.IsEmpty();
	FString SpacedCode;
	for (int32 i = 0; i < GS->RoomCode.Len(); ++i)
	{
		if (i) { SpacedCode += TEXT(" "); }
		SpacedCode.AppendChar(GS->RoomCode[i]);
	}
	const FString CodeHint = bLobbyCode ? NiLoc::T(this, ENiLocKey::LobbyCodeHint) : FString();
	// 受害者＝這一相位的賭注：臉＋名字（**罰酒杯已搬去右下角比分**——三個 20px
	// 的圖示擠在名字旁邊，等於畫面上最重要的那個數字沒有人看得見；一件事只講一次）
	const bool bShowVictim = VictimNIPS && !bIsVictim &&
		(GS->CurrentPhase == ENiceInkPhase::Drawing ||
		 GS->CurrentPhase == ENiceInkPhase::Tour ||
		 GS->CurrentPhase == ENiceInkPhase::Accusation);
	const float FaceSize = 6.0f * NiUi::U * UiScale;   // 24

	// 先量再鋪地：漸層要蓋住的是「這一幀真的畫了多高」，寫死高度會在只有一行字的
	// 相位鋪出一大片沒有理由的暗。
	{
		float ContentH = 0.0f;
		if (!Count.IsEmpty())
		{
			TGuardValue<bool> SerifGuard(bSerifFace, true);
			ContentH += MeasureTok(Count, ETextTier::Display, true).Y + Gap;
		}
		if (!Imp.IsEmpty())
		{
			ContentH += MeasureTok(Imp, ETextTier::Body, false).Y + NiUi::GapM * UiScale;
		}
		if (bLobbyCode)
		{
			ContentH += MeasureTok(SpacedCode, ETextTier::Display, true).Y + Gap
				+ MeasureTok(CodeHint, ETextTier::Small, false).Y + NiUi::GapM * UiScale;
		}
		if (bShowVictim)
		{
			ContentH += FaceSize;
		}
		if (ContentH > 0.0f)
		{
			DrawTopScrim(M + ContentH + NiUi::GapL * UiScale);
		}
	}

	float Y = M;
	if (!Count.IsEmpty())
	{
		// 倒數＝畫面上最大的一個字形 ⇒ 品牌聲部（明朝體）。這是「一款遊戲一種
		// 字體人格」那條規則落在局內的第一個位置。
		TGuardValue<bool> SerifGuard(bSerifFace, true);
		Y += DrawTok(Count, W * 0.5f, Y, ETextTier::Display, NiHudColor::Paper,
			EHAlign::Center, true).Y + Gap;
	}

	if (!Imp.IsEmpty())
	{
		// 祈使句維持圓體：它是**工具字**（要在小字級被快速讀），不是聲部。
		Y += DrawTok(Imp, W * 0.5f, Y, ETextTier::Body, NiHudColor::Paper,
			EHAlign::Center, false).Y + NiUi::GapM * UiScale;
	}

	if (bLobbyCode)
	{
		Y += DrawTok(SpacedCode, W * 0.5f, Y, ETextTier::Display, NiHudColor::Paper,
			EHAlign::Center, true).Y + Gap;
		Y += DrawTok(CodeHint, W * 0.5f, Y, ETextTier::Small, NiHudColor::PaperDim,
			EHAlign::Center, false).Y + NiUi::GapM * UiScale;
	}

	if (bShowVictim)
	{
		const FString Name = FitTok(VictimNIPS->GetPlayerName(), ETextTier::Small,
			60.0f * NiUi::U * UiScale);
		const FVector2D NameSize = MeasureTok(Name, ETextTier::Small, false);
		const float RowW = FaceSize + Gap + NameSize.X;
		float X = W * 0.5f - RowW * 0.5f;
		DrawFaceTok(VictimNIPS, X, Y, FaceSize);
		X += FaceSize + Gap;
		DrawTok(Name, X, Y + (FaceSize - NameSize.Y) * 0.5f, ETextTier::Small,
			NiHudColor::Paper, EHAlign::Left, false);
	}

	// 現金（右上；與四邊共用同一個邊距）
	if (MyPS)
	{
		const float CashIcon = 5.0f * NiUi::U * UiScale;  // 20
		DrawIconTok(IconCash, W - M - CashIcon, M, CashIcon, NiHudColor::PaperDim);
		DrawTok(FText::AsNumber(MyPS->Cash).ToString(), W - M - CashIcon - Gap, M,
			ETextTier::Body, NiHudColor::Paper, EHAlign::Right, true);
	}
}

FString ANiceInkHUD::GetPhaseImperative(const ANiceInkGameState* GS,
	ANiceInkCharacter* MyChar, bool bIsVictim) const
{
	// **每個相位一句「現在該做什麼」**（Meccha 的上緣＝沙漏＋數字＋祈使句）。
	// 我們此前上緣只有相位**名詞**（GALLERY TOUR）——名詞說明不了要做什麼。
	if (!GS)
	{
		return FString();
	}
	if (MyChar)
	{
		if (MyChar->bAsleep && !MyChar->bEyesOpen) { return NiLoc::T(this, ENiLocKey::ImpDream); }
		if (MyChar->IsFeigningSleep())             { return NiLoc::T(this, ENiLocKey::ImpFeign); }
	}
	switch (GS->CurrentPhase)
	{
	case ENiceInkPhase::Lobby:
		return NiLoc::T(this, (GetWorld() && GetWorld()->GetNetMode() != NM_Client)
			? ENiLocKey::ImpLobbyHost : ENiLocKey::ImpLobbyWait);
	case ENiceInkPhase::BottleSpin: return NiLoc::T(this, ENiLocKey::ImpBottleSpin);
	case ENiceInkPhase::Seating:    return NiLoc::T(this, ENiLocKey::ImpSeating);
	case ENiceInkPhase::Drawing:
		if (bIsVictim) { return FString(); }   // 受害者在這一相位無事可做
		return NiLoc::T(this, (MyChar && MyChar->bLeanLocked)
			? ENiLocKey::ImpDrawLocked : ENiLocKey::ImpDrawStand);
	case ENiceInkPhase::Tour:       return NiLoc::T(this, ENiLocKey::ImpTour);
	case ENiceInkPhase::Accusation:
		return NiLoc::T(this, bIsVictim ? ENiLocKey::ImpAccuseVictim : ENiLocKey::ImpAccuseOther);
	case ENiceInkPhase::PostGame:   return NiLoc::T(this, ENiLocKey::ImpPostGame);
	default: return FString();     // Resolution／Finale＝演出自己會說話
	}
}

void ANiceInkHUD::DrawRulesBlock(const ANiceInkGameState* GS,
	ANiceInkCharacter* MyChar, bool bIsVictim)
{
	// **右下角常駐規則塊**（照抄 Meccha：模式名＋兩行「怎麼贏」）。
	// 教「按哪顆鍵」與教「怎麼玩」是兩件事：兩個區塊、兩種顏色、兩個字級。
	// 我們此前完全沒有這個東西——玩家無處得知猜錯會怎樣。
	if (!GS || !Canvas)
	{
		return;
	}
	// **分時共用右下角**（2026-09-05；Meccha 同款）：他們搜索階段放模式名＋規則、
	// 開打之後換成 `残り人数`。一格一職、佔用者隨相位輪替。
	// 判準＝受害者的杯數：0＝這一局還沒有賭注可以顯示，正好是新玩家需要規則的時候；
	// 第一次猜錯之後這個角永久變成計分板（DrawScoreCorner）。
	if (GS->GetVictimPenaltyCups() > 0)
	{
		return;
	}
	ENiLocKey Title = ENiLocKey::COUNT, L1 = ENiLocKey::COUNT, L2 = ENiLocKey::COUNT;
	if (MyChar && MyChar->bAsleep && !MyChar->bEyesOpen)
	{
		Title = ENiLocKey::PhaseDream; L1 = ENiLocKey::RuleDream1; L2 = ENiLocKey::RuleDream2;
	}
	else if (GS->CurrentPhase == ENiceInkPhase::Drawing)
	{
		Title = ENiLocKey::PhaseDrawing; L1 = ENiLocKey::RuleDraw1; L2 = ENiLocKey::RuleDraw2;
	}
	else if (GS->CurrentPhase == ENiceInkPhase::Tour ||
			 GS->CurrentPhase == ENiceInkPhase::Accusation)
	{
		// **巡禮不再冒用指認的名字**（2026-09-05）：此前 Tour 與 Accusation 共用
		// 同一組文案 ⇒ 巡禮那一幀上緣寫著「look at every piece」而右下角寫著
		// 「ACCUSATION」＝一張畫面掛了兩個相位名。規則兩句可以共用（它們講的是
		// 同一場賭），**標題必須是玩家現在所在的那個相位**。
		Title = (GS->CurrentPhase == ENiceInkPhase::Tour)
			? ENiLocKey::PhaseTour : ENiLocKey::PhaseAccusation;
		L1 = ENiLocKey::RuleAccuse1; L2 = ENiLocKey::RuleAccuse2;
	}
	else
	{
		return;
	}
	const bool bInking = MyChar && MyChar->bLeanLocked && MyChar->IsPenTriggerHeldLocal();
	TGuardValue<float> ChromeDim(ChromeAlphaMul, bInking ? 0.25f : 1.0f);

	const float M = NiUi::Margin * UiScale;
	const float Gap = NiUi::GapS * UiScale;
	const float RightX = Canvas->ClipX - M;
	const FVector2D L2Size = MeasureTok(NiLoc::T(this, L2), ETextTier::Small, false);
	const FVector2D L1Size = MeasureTok(NiLoc::T(this, L1), ETextTier::Small, false);
	float Y = Canvas->ClipY - M - L2Size.Y;
	DrawTok(NiLoc::T(this, L2), RightX, Y, ETextTier::Small, NiHudColor::Paper, EHAlign::Right, false);
	Y -= L1Size.Y + Gap;
	DrawTok(NiLoc::T(this, L1), RightX, Y, ETextTier::Small, NiHudColor::Paper, EHAlign::Right, false);
	{
		// 相位名＝**聲部**（明朝體）＋酒金。
		// 綠色已拆除（2026-09-05）：那是抄「值」抄來的孤兒——實測局內用了 7 次、
		// 選單 **0 次** ⇒ 兩個載體連強調色都不同。強調色的規則是「每軌一個語義、
		// 兩個載體共用同一組」，我們的那一軌是酒金。
		// （Green 只留給 CORRECT／WRONG 那一對——成敗是語義而不是品牌色，
		// 而「成敗」這條軸在選單裡根本不存在，§4.2 的「軸不存在就不該存在」正好適用。）
		TGuardValue<bool> SerifGuard(bSerifFace, true);
		const FVector2D TSize = MeasureTok(NiLoc::T(this, Title), ETextTier::Title, true);
		Y -= TSize.Y + Gap;
		DrawTok(NiLoc::T(this, Title), RightX, Y, ETextTier::Title, NiHudColor::Paper, EHAlign::Right, true);
	}
}

void ANiceInkHUD::DrawScoreCorner(const ANiceInkGameState* GS)
{
	// 右下角比分（Meccha 的 `残り人数` ＋巨大數字）。**我們的比分是罰酒杯**：
	// 三杯就結束這一局，所以它就是這場的進度條。杯子本來就是「會被斟滿」的容器
	// ——用形狀講狀態（他們的沙漏同理），比任何數字都直觀，所以這裡放大杯子而不是
	// 印一個 "2/3"。
	if (!GS || !Canvas)
	{
		return;
	}
	const int32 Cups = GS->GetVictimPenaltyCups();
	if (Cups <= 0)
	{
		return;   // 沒有賭注 ⇒ 這個角讓給規則塊（DrawRulesBlock）
	}
	const float M = NiUi::Margin * UiScale;
	const float Gap = NiUi::GapS * UiScale;
	const float CupSize = 10.0f * NiUi::U * UiScale;   // 40＝上緣那版的兩倍
	const float RightX = Canvas->ClipX - M;

	const FString Label = NiLoc::T(this, ENiLocKey::ScorePenalty);
	const FVector2D LSize = MeasureTok(Label, ETextTier::Small, false);
	const float CupsW = CupSize * 1.18f * 2.0f + CupSize;
	float Y = Canvas->ClipY - M - CupSize;
	DrawCupsRow(RightX - CupsW, Y, CupSize, Cups);
	Y -= LSize.Y + Gap;
	DrawTok(Label, RightX, Y, ETextTier::Small, NiHudColor::Paper, EHAlign::Right, false);
}


void ANiceInkHUD::DrawCenterBanners(const ANiceInkGameState* GS)
{
	if (!GS)
	{
		return;
	}
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	// 揭曉／結局的大橫幅＝**聲部**（明朝體）：這是全戲最重的兩拍，也是局內
	// 唯一與主選單標題（`NICE INK`）同一個字級的東西 ⇒ 用同一個字體人格。
	TGuardValue<bool> SerifGuard(bSerifFace, true);

	if (GS->CurrentPhase == ENiceInkPhase::Resolution)
	{
		// 臉制（SPEC #52）：揭曉＝真作者的臉放大登場——全戲最重的一拍給臉
		const bool bCorrect = GS->LastAccusationResult == ENiceInkAccusationResult::Correct;
		const APlayerState* AuthorPS = GS->FindPlayerStateById(GS->RevealedAuthorId);
		// 臉接在橫幅**實際高度**之下（橫幅是 64 級，寫死 56 會疊在一起——09-06 截圖實錘）
		const float BannerH = DrawTok(NiLoc::T(this, bCorrect ? ENiLocKey::BannerCorrect : ENiLocKey::BannerWrong),
			W * 0.5f, H * 0.26f, ETextTier::Display, bCorrect ? NiHudColor::Paper : NiHudColor::Red, EHAlign::Center, true).Y;
		const float RevealFace = NiUi::FaceL * UiScale;
		const float FaceY = H * 0.26f + BannerH + NiUi::GapL * UiScale;
		DrawFaceTok(AuthorPS, W * 0.5f - RevealFace * 0.5f, FaceY, RevealFace);
		float RevealY = FaceY + RevealFace + NiUi::GapM * UiScale;
		if (AuthorPS)
		{
			// v4.0e：臉像＋名字並列——揭曉的臉下方跟名字
			DrawTok(FitTok(AuthorPS->GetPlayerName(), ETextTier::Body, 320.0f * UiScale, true),
				W * 0.5f, RevealY, ETextTier::Body, NiHudColor::Paper, EHAlign::Center, true);
			RevealY += 22.0f * UiScale;
		}
		DrawTok(NiLoc::T(this, bCorrect ? ENiLocKey::ResCorrect : ENiLocKey::ResWrong),
			W * 0.5f, RevealY, ETextTier::Body, NiHudColor::Paper, EHAlign::Center, false);
	}

	if (GS->CurrentPhase == ENiceInkPhase::Finale || GS->CurrentPhase == ENiceInkPhase::PostGame)
	{
		if (const APlayerState* LoserPS = GS->FindPlayerStateById(GS->LoserPlayerId))
		{
			const float BannerH = DrawTok(NiLoc::T(this, ENiLocKey::BannerOutCold), W * 0.5f, H * 0.2f,
				ETextTier::Display, NiHudColor::Red, EHAlign::Center, true).Y;
			const float LoserFace = NiUi::FaceL * UiScale;
			const float FaceY = H * 0.2f + BannerH + NiUi::GapL * UiScale;
			DrawFaceTok(LoserPS, W * 0.5f - LoserFace * 0.5f, FaceY, LoserFace);
			float LoserY = FaceY + LoserFace + NiUi::GapM * UiScale;
			DrawTok(FitTok(LoserPS->GetPlayerName(), ETextTier::Body, 320.0f * UiScale, true),
				W * 0.5f, LoserY, ETextTier::Body, NiHudColor::Paper, EHAlign::Center, true);
			LoserY += 22.0f * UiScale;
			DrawTok(NiLoc::T(this, ENiLocKey::FinaleNote),
				W * 0.5f, LoserY, ETextTier::Body, NiHudColor::Paper, EHAlign::Center, false);
		}
	}
}

void ANiceInkHUD::TickAudioCues(const ANiceInkGameState* GS)
{
	if (!GS)
	{
		return;
	}
	const ENiceInkPhase Phase = GS->CurrentPhase;
	if (!bPhaseSeeded)
	{
		// 首幀（含中途加入）：記狀態不發聲
		bPhaseSeeded = true;
		LastPhaseSeen = Phase;
		PhaseChangedAt = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		LastTourWorkSeen = GS->TourWorkId;
		return;
	}

	if (Phase != LastPhaseSeen)
	{
		switch (Phase)
		{
		case ENiceInkPhase::BottleSpin: NiAudio::Play(this, ENiSound::BottleSpin); break;
		case ENiceInkPhase::Seating:    NiAudio::Play(this, ENiSound::DrinkGulp); break; // 入座酒
		case ENiceInkPhase::Resolution:
			NiAudio::Play(this, GS->LastAccusationResult == ENiceInkAccusationResult::Correct
				? ENiSound::AccuseCorrect : ENiSound::AccuseWrong);
			break;
		case ENiceInkPhase::Finale:     NiAudio::Play(this, ENiSound::FinaleGong); break;
		// Drawing/Tour/Accusation/PostGame 的入場不發聲（Tour 由逐幅 chime 承擔；
		// Drawing 開始＝沉睡開始，甦醒側零提示鐵律的另一半）
		default: break;
		}
		LastPhaseSeen = Phase;
		PhaseChangedAt = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	}

	if (GS->TourWorkId != LastTourWorkSeen)
	{
		if (Phase == ENiceInkPhase::Tour && GS->TourWorkId != INDEX_NONE)
		{
			NiAudio::Play(this, ENiSound::TourChime);
		}
		LastTourWorkSeen = GS->TourWorkId;
	}
}

void ANiceInkHUD::DrawLobbyPanel(const ANiceInkGameState* GS)
{
	// 大廳（2026-09-05 中性制）：**無面板**。玩家在這個面上只問三件事——
	// 房號是什麼（上緣主角數字，DrawTopBar 畫、與祈使句共用同一片漸層）、
	// 誰來了（底部中央一排臉：到齊的亮、空位是淡框）、什麼時候開始（底部一行計數；
	// 房主的 ENTER 在右緣操作列，人數不足時灰）。此前那塊浮在牆上的深色面板
	// （seat 1／256／host／10,000）讀起來是一張 debug 表，拆除。
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	const float CX = W * 0.5f;
	const float Gap = NiUi::GapM * UiScale;
	const float GapL = NiUi::GapL * UiScale;

	TArray<const ANiceInkPlayerState*> Sorted;
	for (const APlayerState* PS : GS->PlayerArray)
	{
		if (const ANiceInkPlayerState* NIPS = Cast<ANiceInkPlayerState>(PS))
		{
			Sorted.Add(NIPS);
		}
	}
	Sorted.Sort([](const ANiceInkPlayerState& A, const ANiceInkPlayerState& B) { return A.SeatIndex < B.SeatIndex; });

	const int32 NumIn = Sorted.Num();
	const int32 Slots = FMath::Clamp(GS->MaxPlayers, 4, 6);
	const float Face = NiUi::FaceM * UiScale;
	const float NameH = MeasureTok(TEXT("Ag"), ETextTier::Small, false).Y;
	const float HintY = H - 46.0f * UiScale;   // DrawBottomHint 的那條線
	const float NameY = HintY - GapL - NameH;
	const float FaceY = NameY - Gap - Face;
	const float RowW = Slots * Face + (Slots - 1) * GapL;
	float X = CX - RowW * 0.5f;
	for (int32 i = 0; i < Slots; ++i, X += Face + GapL)
	{
		if (i < NumIn)
		{
			const ANiceInkPlayerState* PS = Sorted[i];
			DrawFaceTok(PS, X, FaceY, Face);
			// 房主＝粗體名字（一個席位一個名字；後綴「· host」在 72px 預算下必截斷）
			const bool bHost = PS->bIsRoomHost;
			DrawTok(FitTok(PS->GetPlayerName(), ETextTier::Small, Face + GapL * 2.0f, bHost),
				X + Face * 0.5f, NameY, ETextTier::Small, NiHudColor::Paper, EHAlign::Center, bHost);
		}
		else
		{
			// 空位：黑 25% 的淡框——要在白障子牆前也讀得到，所以是黑不是白
			FLinearColor Empty = NiHudColor::Black;
			Empty.A = 0.25f;
			DrawRoundedBox(X, FaceY, Face, Face, NiUi::Radius * UiScale, Empty);
		}
	}

	// 人數「n/房間人數」＋開局門檻（與 RequestStartMatch 同判準 CanHostStartMatch）
	const FString CountText = FString::Printf(TEXT("%d/%d"), NumIn, Slots);
	const bool bIsHost = GetWorld() && GetWorld()->GetNetMode() != NM_Client;
	if (bIsHost)
	{
		const bool bCanStart = CanHostStartMatch(GS);
		DrawBottomHint(NiLoc::TFmt(this, bCanStart ? ENiLocKey::LobbyStart : ENiLocKey::LobbyWaiting, CountText),
			bCanStart ? NiHudColor::Paper : NiHudColor::PaperDim);
	}
	else
	{
		DrawBottomHint(NiLoc::TFmt(this, ENiLocKey::LobbyWaitingHost, CountText), NiHudColor::PaperDim);
	}
}

bool ANiceInkHUD::CanHostStartMatch(const ANiceInkGameState* GS) const
{
	// 可開局＝人數夠＋全員臉齊（與 RequestStartMatch 同判準；Game 世界限定）。
	// PIE 門檻 2＝服務 robo（正式 4）——這個分歧本來就寫在原處，抽出來時原樣搬。
	if (!GS || !GetWorld())
	{
		return false;
	}
	const int32 MinStart = (GetWorld()->WorldType == EWorldType::PIE) ? 2 : 4;
	if (GS->PlayerArray.Num() < MinStart)
	{
		return false;
	}
	if (GetWorld()->WorldType == EWorldType::Game)
	{
		for (APlayerState* PS : GS->PlayerArray)
		{
			const ANiceInkCharacter* C = Cast<ANiceInkCharacter>(PS->GetPawn());
			if (C && !C->bFaceReady)
			{
				return false;
			}
		}
	}
	return true;
}

void ANiceInkHUD::DrawSystemMenu(ANiceInkCharacter* MyChar)
{
	if (!MyChar || !MyChar->IsSystemMenuOpen())
	{
		return;
	}
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	const float CX = W * 0.5f;

	// 壓暗全畫面（menu 在最上層；沉睡黑屏之上也可讀）
	FLinearColor Dim = NiHudColor::Ink;
	Dim.A = NiUi::ModalDim;
	DrawRect(Dim, 0, 0, W, H);

	DrawBigTitle(NiLoc::T(this, ENiLocKey::MenuTitle), CX, H * 0.22f, NiType::HudTitle, NiHudColor::Paper);

	UNiceInkGameInstance* GI = UNiceInkGameInstance::Get(this);
	const float RowX = CX - 40.0f * UiScale;
	float Y = H * 0.22f + 96.0f * UiScale;
	const float Step = 64.0f * UiScale;

	if (GI)
	{
		const int32 SensDelta = AdjustRow(NiLoc::T(this, ENiLocKey::MenuSensitivity),
			FString::Printf(TEXT("%.1f"), GI->MouseSensitivityScale), RowX, Y,
			GI->MouseSensitivityScale > 0.25f, GI->MouseSensitivityScale < 2.95f);
		if (SensDelta != 0)
		{
			GI->MouseSensitivityScale = FMath::Clamp(GI->MouseSensitivityScale + SensDelta * 0.1f, 0.2f, 3.0f);
			GI->SaveSettings();
		}
		Y += Step;

		const int32 VolDelta = AdjustRow(NiLoc::T(this, ENiLocKey::MenuVolume),
			FString::Printf(TEXT("%d%%"), FMath::RoundToInt(GI->MasterVolume * 100.0f)), RowX, Y,
			GI->MasterVolume > 0.01f, GI->MasterVolume < 0.99f);
		if (VolDelta != 0)
		{
			GI->MasterVolume = FMath::Clamp(GI->MasterVolume + VolDelta * 0.05f, 0.0f, 1.0f);
			GI->UpdateBgmVolume();
			GI->SaveSettings();
		}
		Y += Step + 24.0f * UiScale;
	}

	const float BtnW = 300.0f * UiScale;
	const float BtnH = 52.0f * UiScale;
	if (Button(NiLoc::T(this, ENiLocKey::MenuResume), CX, Y, BtnW, BtnH, true, true))
	{
		MyChar->SetSystemMenuOpen(false);
	}
	Y += BtnH + 14.0f * UiScale;
	if (Button(NiLoc::T(this, ENiLocKey::MenuLeave), CX, Y, BtnW, BtnH))
	{
		if (GI)
		{
			GI->ReturnToMainMenu(FString());
		}
	}
	Y += BtnH + 18.0f * UiScale;

	// 房主管理段（2026-08-13 踢人制）：listen server 上 HUD 就在伺服器行程
	// ＝直呼 GameMode 免 RPC；踢出＋本場拒再入（公開房搗亂管理）
	if (GetWorld() && GetWorld()->GetNetMode() != NM_Client)
	{
		if (ANiceInkGameState* GS = GetWorld()->GetGameState<ANiceInkGameState>())
		{
			TArray<ANiceInkPlayerState*> Others;
			for (APlayerState* PS : GS->PlayerArray)
			{
				if (ANiceInkPlayerState* NIPS = Cast<ANiceInkPlayerState>(PS))
				{
					if (!NIPS->bIsRoomHost)
					{
						Others.Add(NIPS);
					}
				}
			}
			if (Others.Num() > 0)
			{
				Others.Sort([](const ANiceInkPlayerState& A, const ANiceInkPlayerState& B)
					{ return A.SeatIndex < B.SeatIndex; });
				DrawTok(NiLoc::T(this, ENiLocKey::MenuPlayers), CX, Y, ETextTier::Small, NiHudColor::PaperDim, EHAlign::Center, false);
				Y += 26.0f * UiScale;
				const float KickW = 84.0f * UiScale;
				const float KickH = 34.0f * UiScale;
				for (ANiceInkPlayerState* NIPS : Others)
				{
					DrawTok(FitTok(NIPS->GetPlayerName(), ETextTier::Body, 200.0f * UiScale),
						CX - 150.0f * UiScale, Y + 5.0f * UiScale, ETextTier::Body, NiHudColor::Paper, EHAlign::Left, false);
					if (Button(NiLoc::T(this, ENiLocKey::MenuKick), CX + 110.0f * UiScale, Y, KickW, KickH))
					{
						if (ANiceInkGameMode* GM = GetWorld()->GetAuthGameMode<ANiceInkGameMode>())
						{
							GM->HostKickPlayer(NIPS);
						}
					}
					Y += KickH + 8.0f * UiScale;
				}
				Y += 10.0f * UiScale;
			}
		}
	}

}

void ANiceInkHUD::DrawAccusePanel(const ANiceInkGameState* GS, ANiceInkCharacter* MyChar)
{
	// 指認（受害者本人；2026-09-05 中性制）：**無面板**。「這件是誰畫的」本來就是
	// 指著一張臉 ⇒ 底部中央一排候選的臉（在場所有人、受害者除外、依席位）；
	// 當前嫌疑人＝黑 60% 底＋強調色底線＋名字用強調文字（強調色只落在深色面上，
	// 那塊底就是為它鋪的）。第幾件（work n/m）一行在臉列上方。
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	const float CX = W * 0.5f;
	const float Gap = NiUi::GapM * UiScale;
	const float GapL = NiUi::GapL * UiScale;

	TArray<const ANiceInkPlayerState*> Cands;
	for (const APlayerState* PS : GS->PlayerArray)
	{
		const ANiceInkPlayerState* NIPS = Cast<ANiceInkPlayerState>(PS);
		if (NIPS && NIPS->GetPlayerId() != GS->VictimPlayerId)
		{
			Cands.Add(NIPS);
		}
	}
	Cands.Sort([](const ANiceInkPlayerState& A, const ANiceInkPlayerState& B) { return A.SeatIndex < B.SeatIndex; });
	if (Cands.Num() == 0)
	{
		return;
	}

	const APlayerState* Suspect = MyChar->GetAccuseSuspect();
	const float Face = NiUi::FaceM * UiScale;
	const float NameH = MeasureTok(TEXT("Ag"), ETextTier::Small, false).Y;
	const float HintY = H - 46.0f * UiScale;
	const float NameY = HintY - GapL - NameH;
	const float FaceY = NameY - Gap - Face;
	const float RowW = Cands.Num() * Face + (Cands.Num() - 1) * GapL;
	const float BarH = 3.0f * UiScale;

	DrawTok(NiLoc::TFmt(this, ENiLocKey::AccuseWork, FString::FromInt(MyChar->AccusePickNumber), FString::FromInt(GS->TourWorkCount)),
		CX, FaceY - Gap - GapL - MeasureTok(TEXT("Ag"), ETextTier::Body, true).Y,
		ETextTier::Body, NiHudColor::Paper, EHAlign::Center, true);

	float X = CX - RowW * 0.5f;
	for (const ANiceInkPlayerState* PS : Cands)
	{
		const bool bSel = (Suspect == PS);
		if (bSel)
		{
			FLinearColor Bg = NiHudColor::Black;
			Bg.A = NiUi::ModalDim;
			const float BgTop = FaceY - Gap;
			const float BgBottom = NameY + NameH + Gap + BarH + Gap;
			DrawRoundedBox(X - Gap, BgTop, Face + Gap * 2.0f, BgBottom - BgTop, NiUi::Radius * UiScale, Bg);
			DrawRoundedBox(X, NameY + NameH + Gap, Face, BarH, BarH * 0.5f, NiHudColor::Accent);
		}
		DrawFaceTok(PS, X, FaceY, Face);
		DrawTok(FitTok(PS->GetPlayerName(), ETextTier::Small, Face + GapL), X + Face * 0.5f, NameY,
			ETextTier::Small, bSel ? NiHudColor::AccentText : NiHudColor::PaperDim, EHAlign::Center, false);
		X += Face + GapL;
	}
}

void ANiceInkHUD::DrawPostGamePanel(ANiceInkCharacter* MyChar)
{
	if (!MyChar || !MyChar->InkCanvas)
	{
		return;
	}
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	const int32 CarbonCount = MyChar->InkCanvas->GetWorkIdsByState(EInkWorkState::Carbon).Num();
	const int32 PermanentCount = MyChar->InkCanvas->GetWorkIdsByState(EInkWorkState::Permanent).Num();

	// 場間大廳：端詳刺青、自費雷射、開下一場
	DrawTok(NiLoc::TFmt(this, ENiLocKey::PostInk, FString::FromInt(CarbonCount), FString::FromInt(PermanentCount)),
		W * 0.5f, H - 96.0f * UiScale, ETextTier::Body, NiHudColor::Paper, EHAlign::Center, false);
}

void ANiceInkHUD::DrawBottomHint(const FString& Text, const FLinearColor& Color)
{
	DrawTok(Text, Canvas->ClipX * 0.5f, Canvas->ClipY - 46.0f * UiScale, ETextTier::Small, Color, EHAlign::Center, false);
}

void ANiceInkHUD::DrawInkCup(float X, float Y, float W, float H,
	const FLinearColor& Base, int32 TierIdx)
{
	// 一枚墨杯＝**變化的地＋真半透明的墨**。半透明的墨會讓地的明暗透出來（格子花
	// 看得見＝它是透過來的），不透明的墨把地整個蓋掉（一片死平）⇒ **「看不看得到
	// 格子」本身就是透明的證據**，而摻白的粉彩色在任何地上都是一致的一片。
	// 血價（09-02 二修）：一版用**單一紙色底**＋「十欄平行變淡就看得懂」＝**數學上
	// 錯**——`α·色 + (1−α)·白` 與「摻 (1−α) 的白」逐像素相等，user 當場問
	// 「我要怎麼看出這是透明度還是粉度」。**在單一底色上不可能顯示透明度**，
	// 這不是標示問題是資訊問題：地必須是變化的。
	// 格子＝**2×2 粗格、紙色/紙色陰影**（離線 A/B：兩大塊會被讀成「一個杯裡兩種
	// 顏色」、3×3 太碎會吃掉顏色識別；2×2 是通用慣例且保住色相）。
	// **地的對比有工作區間，不是越大越好**（09-02 三修，user：「格子的底色這麼深
	// 沒有問題嗎」）：一版用紙/**墨**＝地對比 218 個 sRGB 階（Photoshop 自己的棋盤
	// ~51＝我超標四倍），格子壓過顏色 ⇒ 每一杯都讀成髒的（實測 30% 檔離「乾淨的
	// 同色淡墨」49 階）。現值 66 階＝格子仍一眼看得到（30% 檔 51 階、60% 檔 36 階），
	// 混濁度減半（23 階）。**模式才是透明的訊號，對比大小不是**——眼睛對「有沒有
	// 花紋」極敏感，不需要靠強度說服它。暗格用紙色的陰影（同色相）而非灰或黑，
	// 讀成「同一張紙的暗處」；追記106 的灰/灰弱點是**兩格都暗**，不是對比太小。
	FLinearColor Lo = NiHudColor::Paper;
	Lo.A = 1.0f;
	FLinearColor Hi = NiHudColor::PaperShade;
	Hi.A = 1.0f;
	const float HW = W * 0.5f;
	const float HH = H * 0.5f;
	DrawRect(Lo, X, Y, HW, HH);
	DrawRect(Hi, X + HW, Y, W - HW, HH);
	DrawRect(Hi, X, Y + HH, HW, H - HH);
	DrawRect(Lo, X + HW, Y + HH, W - HW, H - HH);
	FLinearColor Ink = Base;
	Ink.A = ANiceInkCharacter::ShaderTierAlphaFor(TierIdx) / 255.0f;
	DrawRect(Ink, X, Y, W, H);
}

void ANiceInkHUD::DrawInkChip(const ANiceInkCharacter* MyChar)
{
	// 「我現在裝的是哪一杯」＝作畫時**唯一**的常駐狀態顯示（畫面清單 2026-09-02）。
	// 為什麼只剩一枚 chip：
	//   ①「哪支筆」不需要文字——三支筆在針尖的視覺本來就完全不同（稿筆紫點＋2D
	//     麥克筆／割線細針＋行進蟻／打霧粗針＋範圍圈），筆名只活在托盤（選的時候）。
	//   ②「什麼顏色」在螢幕中心的落點指示上已經有了，那裡是視線落點。
	//   ③ 只剩「濃度」需要一份持久記憶：誤用濃度是**單向不可逆**的（想要 30% 卻
	//     下了 100%＝救不回來；反過來再掃一趟就補滿），所以它值得一個常駐位置。
	// 舊 cluster 的三格直排已退役：23px 下的棋盤讀成髒點（在托盤 60px 有效的裝置
	// 縮到 23px 就失效），而且你要知道的是「我在哪一檔」不是「另外兩檔長怎樣」。
	if (!MyChar || !Canvas)
	{
		return;
	}
	// AR 鏡像豁免：與托盤同理（格序＝物理域非版面域）
	TGuardValue<bool> MirrorGuard(bMirrorSuspended, true);
	// 落筆中一起淡（與上方 chrome 同一條原則：落筆的那幾秒畫面上只留皮膚、
	// 機器、veil、落點；放開就回來）
	TGuardValue<float> ChromeDim(ChromeAlphaMul,
		MyChar->IsPenTriggerHeldLocal() ? 0.35f : 1.0f);

	// **無面板**（2026-09-04）：與上緣同一條規則。此前這裡是 Ink α0.62 的卡片，
	// 而全站面板 alpha 有七種、沒有規則；拆掉面板，對比交給杯子自己的邊框與文字陰影。
	const float Cell = 7.0f * NiUi::U * UiScale;   // 28
	const bool bStencil = (MyChar->SelectedNeedle == EInkNeedle::Stencil);
	const bool bShowPct = (MyChar->SelectedNeedle == EInkNeedle::Shader);
	const FLinearColor ChipColor = bStencil
		? NiceInkStencil::Color() : FNiceInkPalette::Get(MyChar->SelectedColorIndex);
	const int32 ChipTier = bShowPct ? MyChar->ShaderTierIdx : 2; // 非打霧＝恆實墨
	// 百分比＝**與托盤列標同一個來源**（TierLabel），不要自己從位元組除。
	const FString Pct = bShowPct
		? FNiInkTrayLayout::TierLabel(FNiInkTrayLayout::RowFromTier(MyChar->ShaderTierIdx))
		: FString();
	const FVector2D PctSize = bShowPct ? MeasureTok(Pct, ETextTier::Body, true) : FVector2D::ZeroVector;
	const float Gap = NiUi::GapM * UiScale;
	const float RowW = Cell + (bShowPct ? Gap + PctSize.X : 0.0f);
	const float X = Canvas->ClipX * 0.5f - RowW * 0.5f;
	const float Y = Canvas->ClipY - NiUi::Margin * UiScale - Cell;
	{
		// 1px 白框：與鍵帽同一個邊處理——沒有邊的色塊讀成漏畫的方塊
		FLinearColor Ring = NiHudColor::White;
		Ring.A = 0.70f;
		const float B = FMath::Max(1.0f, UiScale);
		DrawRoundedBox(X - B, Y - B, Cell + B * 2.0f, Cell + B * 2.0f, NiUi::Radius * UiScale, Ring);
	}
	DrawInkCup(X, Y, Cell, Cell, ChipColor, ChipTier);
	if (bShowPct)
	{
		DrawTok(Pct, X + Cell + Gap, Y + (Cell - PctSize.Y) * 0.5f,
			ETextTier::Body, NiHudColor::Paper, EHAlign::Left, true);
	}
}


float ANiceInkHUD::DrawKeycap(float X, float Y, const FString& Key, ENiKeyState State)
{
	// 鍵帽（2026-09-06 二版）：**深色半透明底＋1px 亮邊框**＝所有遊戲通用的「鍵盤上的一顆鍵」
	// 符號。一版是白色實心塊＋黑字＋硬陰影，讀成標籤或貼紙（user：「看不太出來是在講按鍵」）。
	// 尺寸：高 KeycapH、寬＝max(高, 字寬＋左右各 3U)。鍵名白、粗體、無陰影（底自帶對比）。
	// 狀態：可用＝黑 55% 底／白 70% 框；一次性＝紫 45% 底／紫框（訊息一樣、音量小一半）；
	// 不可用＝整顆淡（底 30%／框 25%／字 45%）。
	const float H = NiUi::KeycapH * UiScale;
	const FVector2D Size = MeasureTok(Key, ETextTier::Body, true);
	const float PadX = 3.0f * NiUi::U * UiScale;
	const float W = FMath::Max(H, Size.X + PadX * 2.0f);
	const float Rad = NiUi::Radius * UiScale;
	const float B = FMath::Max(1.0f, UiScale);

	FLinearColor Frame = (State == ENiKeyState::OneShot) ? NiHudColor::AccentText : NiHudColor::White;
	Frame.A = (State == ENiKeyState::Unavailable) ? 0.25f : 0.70f;
	FLinearColor Fill = (State == ENiKeyState::OneShot) ? NiHudColor::Accent : NiHudColor::Black;
	Fill.A = (State == ENiKeyState::Unavailable) ? 0.30f : ((State == ENiKeyState::OneShot) ? 0.45f : 0.55f);
	DrawRoundedBox(X, Y, W, H, Rad, Frame);
	DrawRoundedBox(X + B, Y + B, W - B * 2.0f, H - B * 2.0f, FMath::Max(0.0f, Rad - B), Fill);

	FLinearColor KeyInk = NiHudColor::White;
	KeyInk.A = (State == ENiKeyState::Unavailable) ? 0.45f : 1.0f;
	TGuardValue<bool> NoShadow(bTokShadows, false);
	DrawTok(Key, X + W * 0.5f, Y + (H - Size.Y) * 0.5f, ETextTier::Body, KeyInk, EHAlign::Center, true);
	return W;
}

float ANiceInkHUD::DrawMouseGlyph(float LeftX, float Y, float H, int32 Button, ENiKeyState State)
{
	// 平面滑鼠（2026-09-06）：與鍵帽同一個形狀語言——同樣的底、同樣的 1px 框、同樣的圓角
	// 邏輯（膠囊）。上半兩顆鍵用一條中線分開，按下的那顆填強調色；滾輪＝中線上一枚小膠囊。
	// 取代 Kenney 的卡通剪影：那張圖的按鍵是紅色（紅在本系統＝危險），且線條語言與鍵帽不同家。
	const float W = FMath::RoundToFloat(H * 0.68f);
	const float B = FMath::Max(1.0f, UiScale);
	const float Rad = W * 0.5f;
	const float Split = FMath::RoundToFloat(H * 0.45f);

	FLinearColor Frame = NiHudColor::White;
	Frame.A = (State == ENiKeyState::Unavailable) ? 0.25f : 0.70f;
	FLinearColor Fill = NiHudColor::Black;
	Fill.A = (State == ENiKeyState::Unavailable) ? 0.30f : 0.55f;
	DrawRoundedBox(LeftX, Y, W, H, Rad, Frame);
	DrawRoundedBox(LeftX + B, Y + B, W - B * 2.0f, H - B * 2.0f, FMath::Max(0.0f, Rad - B), Fill);
	// 分界：水平線＋（左右鍵時）上半的垂直中線
	DrawRoundedBox(LeftX + B, Y + Split, W - B * 2.0f, B, 0.0f, Frame);
	if (Button != 2)
	{
		DrawRoundedBox(LeftX + W * 0.5f - B * 0.5f, Y + B, B, Split - B, 0.0f, Frame);
	}
	FLinearColor Hot = NiHudColor::Accent;
	Hot.A = (State == ENiKeyState::Unavailable) ? 0.35f : 0.95f;
	const float BtnW = W * 0.5f - B * 1.5f;
	const float BtnH = Split - B * 2.0f;
	const float BtnR = 2.0f * UiScale;
	if (Button == 0)
	{
		DrawRoundedBox(LeftX + B, Y + B, BtnW, BtnH, BtnR, Hot);
	}
	else if (Button == 1)
	{
		DrawRoundedBox(LeftX + W * 0.5f + B * 0.5f, Y + B, BtnW, BtnH, BtnR, Hot);
	}
	else
	{
		const float WheelW = B * 3.0f;
		DrawRoundedBox(LeftX + W * 0.5f - WheelW * 0.5f, Y + H * 0.14f, WheelW, H * 0.22f, WheelW * 0.5f, Hot);
	}
	return W;
}

void ANiceInkHUD::BuildControlHints(const ANiceInkGameState* GS, ANiceInkCharacter* MyChar,
	TArray<FNiControlHint>& Out) const
{
	// **操作提示的單一正本**（2026-09-05；UI_SYSTEM §4.3）。此前這份表是散在
	// DrawControlStrip 裡的一個 switch，右緣自己畫自己的；現在右緣（模式與動作）
	// 與底部中央（姿勢與移動）**讀同一份**，差別只在 bPosture 那一欄——
	// 「一件事只講一次」由同一份來源在構造上保證，不靠人記得。
	//
	// 建這一層的理由不是好看：**沒有它，手把支援或改鍵功能一到就要全站重寫**。
	Out.Reset();
	if (!MyChar || !GS)
	{
		return;
	}
	const ANiceInkPlayerState* MyPS = MyChar->GetPlayerState<ANiceInkPlayerState>();
	const bool bIsVictim = MyPS && GS->VictimPlayerId == MyPS->GetPlayerId();
	const bool bIsHost = GetWorld() && GetWorld()->GetNetMode() != NM_Client;

	// 搖夢要花錢：買不起＝**不可按**，而不是「按了沒反應」。
	// 這是 Meccha 的灰態在我們這裡的第一個真消費者——留一顆按了不會有事的鍵，
	// 玩家會以為是自己弄錯了。
	// 價格來源＝GameMode 的 **CDO**：真正的權威在 server（客戶端沒有 GameMode），
	// 而這個值全程沒有任何程式碼改過 ⇒ CDO 與實例相等。判斷錯了只是灰錯一顆鍵，
	// 不影響權威——**但如果哪天價格變成動態的，這裡就要改成複製值**。
	const int32 ShakeCost = GetDefault<ANiceInkGameMode>()->ShakeAttackCost;
	const auto ShakeState = [&]() -> ENiKeyState
	{
		return (MyPS && MyPS->Cash >= ShakeCost)
			? ENiKeyState::OneShot : ENiKeyState::Unavailable;
	};

	if (MyChar->bAsleep && !MyChar->bEyesOpen)
	{
		Out.Add({ nullptr, InMouseLeft, ENiLocKey::ActTrace, ENiKeyState::Available, false });
		return;
	}
	if (MyChar->IsFeigningSleep())
	{
		Out.Add({ TEXT("SHIFT"), nullptr, ENiLocKey::ActWake, ENiKeyState::Available, false });
		return;
	}
	if (MyChar->bLeanLocked)
	{
		Out.Add({ nullptr, InMouseLeft,  ENiLocKey::ActInk,  ENiKeyState::Available, false });
		Out.Add({ nullptr, InMouseRight, ENiLocKey::ActCups, ENiKeyState::Available, false });
		// **SCROLL 只在打霧筆列出**：濃度只有打霧吃得到，操作表要跟著作用域收
		if (MyChar->SelectedNeedle == EInkNeedle::Shader)
		{
			Out.Add({ nullptr, InMouseScroll, ENiLocKey::ActWash, ENiKeyState::Available, false });
		}
		Out.Add({ TEXT("Q"), nullptr, ENiLocKey::ActNeedle, ENiKeyState::Available, false });
		Out.Add({ TEXT("G"), nullptr, ENiLocKey::ActShake,  ShakeState(), false });
		// **起身＝身體姿勢** ⇒ 底部中央橫排（Meccha 把 しゃがむ／立ち上がる 放那裡）。
		// 此前它排在右緣直列的最後一行＝把身體狀態混進工具列。
		Out.Add({ TEXT("WASD"), nullptr, ENiLocKey::ActStand, ENiKeyState::Available, true });
		return;
	}

	switch (GS->CurrentPhase)
	{
	case ENiceInkPhase::Lobby:
		if (bIsHost)
		{
			// 人數不足＝**不可按**。此前這顆 ENTER 長得跟可以按的一樣，而「還差幾個人」
			// 只寫在底部一行小字裡——玩家按下去沒事發生，然後去讀那行字。
			Out.Add({ TEXT("ENTER"), nullptr, ENiLocKey::ActStartMatch,
				CanHostStartMatch(GS) ? ENiKeyState::Available : ENiKeyState::Unavailable, false });
			Out.Add({ TEXT("ESC"), nullptr, ENiLocKey::ActKick, ENiKeyState::Available, false });
		}
		break;
	case ENiceInkPhase::Drawing:
		if (!bIsVictim)
		{
			Out.Add({ nullptr, InMouseRight, ENiLocKey::ActLeanIn, ENiKeyState::Available, false });
			Out.Add({ TEXT("F"), nullptr, ENiLocKey::ActFlip,  ENiKeyState::Available, false });
			Out.Add({ TEXT("G"), nullptr, ENiLocKey::ActShake, ShakeState(), false });
		}
		break;
	case ENiceInkPhase::Accusation:
		if (bIsVictim)
		{
			Out.Add({ TEXT("1-9"),   nullptr, ENiLocKey::ActPickWork, ENiKeyState::Available, false });
			Out.Add({ TEXT("TAB"),   nullptr, ENiLocKey::ActSuspect,  ENiKeyState::Available, false });
			Out.Add({ TEXT("ENTER"), nullptr, ENiLocKey::ActAccuse,   ENiKeyState::Available, false });
		}
		break;
	case ENiceInkPhase::PostGame:
		Out.Add({ TEXT("L"), nullptr, ENiLocKey::ActLaser, ENiKeyState::OneShot, false });
		if (bIsHost)
		{
			Out.Add({ TEXT("ENTER"), nullptr, ENiLocKey::ActNextMatch, ENiKeyState::Available, false });
		}
		break;
	default:
		break;   // 轉瓶／入座／巡禮／判決／結局＝純演出，沒有活著的鍵
	}
}

void ANiceInkHUD::DrawControlStrip(const ANiceInkGameState* GS, ANiceInkCharacter* MyChar)
{
	// 常駐操作列（右緣；2026-09-06 二版）：**「動詞 [鍵]」橫排成一句**，鍵貼在右緣。
	// 一版把 glyph 放上、動詞放下，兩者被讀成兩個獨立物件；橫排讀起來是「按 Q 換針」。
	// 內容來自 BuildControlHints 的單一正本；這裡只畫模式與動作（bPosture=false）。
	if (!MyChar || !Canvas || !GS)
	{
		return;
	}
	TArray<FNiControlHint> All;
	BuildControlHints(GS, MyChar, All);
	TArray<const FNiControlHint*> Rows;
	for (const FNiControlHint& H : All)
	{
		if (!H.bPosture) { Rows.Add(&H); }
	}
	if (Rows.Num() == 0)
	{
		return;
	}
	TGuardValue<float> ChromeDim(ChromeAlphaMul,
		(MyChar->bLeanLocked && MyChar->IsPenTriggerHeldLocal()) ? 0.30f : 1.0f);

	const float Pitch  = 11.0f * NiUi::U * UiScale;   // 44：一列一句
	const float CapH   = NiUi::KeycapH * UiScale;
	const float Gap    = NiUi::GapM * UiScale;
	const float RightX = Canvas->ClipX - NiUi::Margin * UiScale;
	const float VerbH  = MeasureTok(TEXT("Ag"), ETextTier::Small, false).Y;
	float Y = Canvas->ClipY * 0.5f - Rows.Num() * Pitch * 0.5f;
	for (const FNiControlHint* R : Rows)
	{
		const float GW = DrawInputGlyph(RightX, Y, R->Key, R->Tex, R->State);
		FLinearColor Verb = NiHudColor::Paper;
		if (R->State == ENiKeyState::Unavailable) { Verb.A = 0.45f; }
		DrawTok(NiLoc::T(this, R->Label), RightX - GW - Gap, Y + (CapH - VerbH) * 0.5f,
			ETextTier::Small, Verb, EHAlign::Right, false);
		Y += Pitch;
	}
}

void ANiceInkHUD::DrawPostureCluster(const ANiceInkGameState* GS, ANiceInkCharacter* MyChar)
{
	// 姿勢／移動群＝底部中央（2026-09-06 二版）：每一欄「[鍵] 動詞」橫排，欄與欄之間 GapL。
	// 版位：墨杯 chip 在最底，本群排在它正上方。
	if (!MyChar || !Canvas || !GS)
	{
		return;
	}
	TArray<FNiControlHint> All;
	BuildControlHints(GS, MyChar, All);
	TArray<const FNiControlHint*> Rows;
	for (const FNiControlHint& H : All)
	{
		if (H.bPosture) { Rows.Add(&H); }
	}
	if (Rows.Num() == 0)
	{
		return;
	}
	TGuardValue<float> ChromeDim(ChromeAlphaMul,
		(MyChar->bLeanLocked && MyChar->IsPenTriggerHeldLocal()) ? 0.30f : 1.0f);

	const float CapH = NiUi::KeycapH * UiScale;
	const float Gap  = NiUi::GapM * UiScale;
	const float ColGap = NiUi::GapL * UiScale;
	const float ChipH = 7.0f * NiUi::U * UiScale;
	const float VerbH = MeasureTok(TEXT("Ag"), ETextTier::Small, false).Y;
	const float BaseY = Canvas->ClipY - NiUi::Margin * UiScale - ChipH - ColGap - CapH;

	float TotalW = 0.0f;
	for (int32 i = 0; i < Rows.Num(); ++i)
	{
		TotalW += MeasureInputGlyph(Rows[i]->Key, Rows[i]->Tex) + Gap
			+ MeasureTok(NiLoc::T(this, Rows[i]->Label), ETextTier::Small, false).X
			+ (i > 0 ? ColGap : 0.0f);
	}
	float X = Canvas->ClipX * 0.5f - TotalW * 0.5f;
	for (const FNiControlHint* R : Rows)
	{
		const float GW = DrawInputGlyph(X, BaseY, R->Key, R->Tex, R->State, /*bLeftAnchor=*/true);
		const FString Verb = NiLoc::T(this, R->Label);
		FLinearColor VerbC = NiHudColor::Paper;
		if (R->State == ENiKeyState::Unavailable) { VerbC.A = 0.45f; }
		DrawTok(Verb, X + GW + Gap, BaseY + (CapH - VerbH) * 0.5f, ETextTier::Small, VerbC, EHAlign::Left, false);
		X += GW + Gap + MeasureTok(Verb, ETextTier::Small, false).X + ColGap;
	}
}

float ANiceInkHUD::MeasureInputGlyph(const TCHAR* Key, UTexture2D* Tex) const
{
	const float H = NiUi::KeycapH * UiScale;
	if (Tex)
	{
		return FMath::RoundToFloat(H * 0.68f);   // 平面滑鼠的寬（DrawMouseGlyph 同式）
	}
	if (!Key)
	{
		return 0.0f;
	}
	const FVector2D Size = const_cast<ANiceInkHUD*>(this)->MeasureTok(Key, ETextTier::Body, true);
	return FMath::Max(H, Size.X + 3.0f * NiUi::U * 2.0f * UiScale);
}

float ANiceInkHUD::DrawInputGlyph(float X, float Y, const TCHAR* Key, UTexture2D* Tex,
	ENiKeyState State, bool bLeftAnchor)
{
	// **鍵盤有刻字所以寫字，滑鼠沒有刻字所以畫圖**（Docs/UI_SYSTEM.md §4.1）。
	// X＝右緣（右緣縱列）或左緣（底部橫排），由 bLeftAnchor 決定。
	// Tex 現在只當「哪一顆滑鼠鍵」的識別（left／right／scroll），圖本身由 DrawMouseGlyph 畫。
	const float H = NiUi::KeycapH * UiScale;
	if (Tex)
	{
		const int32 Button = (Tex == InMouseRight) ? 1 : ((Tex == InMouseScroll) ? 2 : 0);
		const float GW = FMath::RoundToFloat(H * 0.68f);
		const float LeftX = bLeftAnchor ? X : (X - GW);
		DrawMouseGlyph(LeftX, Y, H, Button, State);
		return GW;
	}
	if (!Key)
	{
		return 0.0f;
	}
	const FVector2D Size = MeasureTok(Key, ETextTier::Body, true);
	const float W = FMath::Max(H, Size.X + 3.0f * NiUi::U * 2.0f * UiScale);
	DrawKeycap(bLeftAnchor ? X : (X - W), Y, Key, State);
	return W;
}

void ANiceInkHUD::DrawInkTray(const ANiceInkCharacter* MyChar)
{
	if (!MyChar || !Canvas || !MyChar->bInkTrayOpen)
	{
		return;
	}
	// AR 鏡像豁免：欄序＝調色盤的物理順序、列序＝滾輪方向（皆非版面域）
	TGuardValue<bool> MirrorGuard(bMirrorSuspended, true);
	// **版面隨筆而變**（09-03）：與角色端的命中測試傳同一支筆＝同一份計算
	const FNiInkTrayLayout L = FNiInkTrayLayout::Compute(Canvas->ClipX, Canvas->ClipY,
		MyChar->SelectedNeedle);
	const FVector2D Cur = MyChar->InkTrayCursor;

	// 方向選擇（09-02 user 定案）：沒有游標，**位移直接決定待選格**。
	// 這一格永遠存在（SnapCell 鉗位＋取最近）＝不會有「落在縫上／落在盤外」。
	int32 HovCol = INDEX_NONE, HovRow = INDEX_NONE;
	L.SnapCell(Cur, HovCol, HovRow);

	// 底卡（墨色半透明）：杯陣在皮膚上要有可讀的地，但只活在按住的那 0.3 秒
	// 全站卡片語言（圓角）——此前是裸的 DrawRect 直角方塊，跟正上方那個圓角相位
	// 橫幅擺在同一畫面裡不像同一款遊戲。chip 上一輪換過去了，托盤漏掉。
	DrawPanelBox(L.CardPos.X, L.CardPos.Y, L.CardSize.X, L.CardSize.Y, 0.88f);

	// 抬頭一行＝現在手上的筆（**資訊不是控制項**——筆的切換歸 Q，
	// 托盤只管杯；把筆做成可點的格子就是把兩個頻率不同的軸又併回一個選單）
	static const ENiLocKey PenKeys[3] =
		{ ENiLocKey::NeedleStencil, ENiLocKey::NeedleLiner, ENiLocKey::NeedleShader };
	const int32 PenIdx = MyChar->SelectedNeedle == EInkNeedle::Stencil ? 0
		: (MyChar->SelectedNeedle == EInkNeedle::Liner ? 1 : 2);
	{
		// 抬頭＝現在的筆 ＋ 換筆的鍵帽（鍵位畫成鍵盤上的樣子，不是句子裡的一個詞）
		const FString PenName = NiLoc::T(this, PenKeys[PenIdx]);
		const float HeadY = L.CardPos.Y + L.Pad;
		const FVector2D NameSize = MeasureTok(PenName, ETextTier::Small, true);
		DrawTok(PenName, L.CardPos.X + L.Pad, HeadY, ETextTier::Small,
			NiHudColor::Paper, EHAlign::Left, true);
		float KX = L.CardPos.X + L.Pad + NameSize.X + 14.0f * L.S;
		KX += DrawKeycap(KX, HeadY - 3.0f * L.S, TEXT("Q")) + 6.0f * L.S;
		DrawTok(NiLoc::T(this, ENiLocKey::ActNeedle), KX, HeadY, ETextTier::Small,
			NiHudColor::PaperDim, EHAlign::Left, false);
		// **當前濃度＋SCROLL 鍵帽貼在同一行的右端**（09-03 二修）：左側三個列標隨
		// 「三列」一起退役——盤只有一排就沒有列要標。濃度是「我手上是什麼」的一部分，
		// 跟筆名同一行＝同一件事講在同一個地方；而 SCROLL 鍵帽就在它旁邊，玩家看到
		// 「60% ⟵ SCROLL」再滾一下，整排一起變淡＝**因果直連的動態教學**。
		if (L.bTierAxis)
		{
			const FString Pct = FNiInkTrayLayout::TierLabel(
				FNiInkTrayLayout::RowFromTier(MyChar->ShaderTierIdx));
			const FVector2D PctSize = MeasureTok(Pct, ETextTier::Small, true);
			const float RightX = L.CardPos.X + L.CardSize.X - L.Pad;
			DrawTok(Pct, RightX, HeadY, ETextTier::Small, NiHudColor::Paper,
				EHAlign::Right, true);
			const FVector2D CapSize = MeasureTok(TEXT("SCROLL"), ETextTier::Small, true);
			const float CapW = FMath::Max(19.0f * L.S, CapSize.X + 12.0f * L.S);
			DrawKeycap(RightX - PctSize.X - 8.0f * L.S - CapW, HeadY - 3.0f * L.S,
				TEXT("SCROLL"));
		}
		// 「放開沾杯」只畫在螢幕底部那一行（畫面清單：一件事只講一次）。
		// 我先前在這裡又畫了一份＝與筆名撞成 "needlerelease"——**加了沒拆，第三次**。
	}

	// 杯陣＝**一排顏色**（09-03 二修）。每格＝紙色/PaperShade 雙色地 ＋ 該欄顏色以
	// **當前這一檔**的 alpha 真半透明疊上＝所見即所得：你看到的就是你會畫出來的。
	// 滾輪一滾整排一起變淡 ⇒「濃度是另一個軸」由**變化**講完，而不是靠並排展示
	//（並排是靜態的，玩家得先假設那三列是同一個東西的三種樣子才讀得懂）。
	// 欄數已由 Compute 依當前的筆決定（`NumCols`）——HUD 不再自己 min 一次調色盤
	// 大小（那就是「表的大小寫在兩個地方」的第二個地方）
	const int32 TrayTier = L.bTierAxis ? MyChar->ShaderTierIdx : 2; // 非打霧＝恆實墨
	for (int32 C = 0; C < L.NumCols; ++C)
	{
		// 稿筆不吃調色盤＝恆結晶紫（與 BeginStroke 同式；盤上那唯一一格就是它）
		const FLinearColor Base = L.bColorAxis
			? FNiceInkPalette::Get(C) : NiceInkStencil::Color();
		for (int32 R = 0; R < L.NumRows; ++R)   // NumRows 恆 1；保留迴圈＝版面單一來源
		{
			const int32 Tier = TrayTier;
			const FVector2D Pos = L.CellPos(C, R);
			// 「手上這杯」＝只比對顏色（濃度已不是盤的軸）。稿筆沒有顏色軸 ⇒ 唯一
			// 那格永遠就是手上這杯，否則它會因為 SelectedColorIndex 不是 0 而顯示成
			// 「沒選中」。
			const bool bSel = L.bColorAxis ? (MyChar->SelectedColorIndex == C) : true;
			const bool bHov = (HovCol == C && HovRow == R);
			// 待選格＝**畫面上唯一的選擇回饋**（游標已退役）⇒ 框要夠粗才看得到；
			// 已裝在手上的那一杯用紙色細框，兩者不衝突。
			const float B = (bHov ? 3.5f : (bSel ? 2.5f : 1.0f)) * L.S;
			FLinearColor Frame = bHov ? NiHudColor::Amber
				: (bSel ? NiHudColor::Paper : NiHudColor::PaperDim);
			Frame.A = (bSel || bHov) ? 1.0f : 0.45f;
			DrawRect(Frame, Pos.X - B, Pos.Y - B, L.Cell.X + 2.0f * B, L.Cell.Y + 2.0f * B);
			DrawInkCup(Pos.X, Pos.Y, L.Cell.X, L.Cell.Y, Base, Tier);
		}
		// **欄標（數字鍵帽 1..9,0）已於 09-03 隨數字鍵快捷一起退役**：那排鍵帽的
		// 唯一用途是教「哪個數字＝哪個顏色」，而顏色↔數字是任意映射（只能背）。
		// 快捷沒了，這排鍵帽就從「教學」變成「指向不存在的東西」——**刪掉快捷卻
		// 留著它的標示，比兩者都留更糟**。
	}

	// 托盤游標已退役（09-02 user 定案方向選擇）：畫一個要對準的游標，
	// 就是在要求玩家對準；而且它會讓人以為那顆游標在別的地方也能點。
	// 現在唯一的回饋是**待選格的高亮**——方向推過去、放開就是它。
}

void ANiceInkHUD::DrawBlindOverlay(const ANiceInkCharacter* MyChar)
{
	// 被噴致盲（該回合內）：大面積色漬遮擋視線，指認結算時解除
	if (!MyChar || !MyChar->bBlinded)
	{
		return;
	}
	FLinearColor Splat;
	switch (MyChar->BlindType)
	{
	case EInkEvidenceType::Sneeze: Splat = FLinearColor(0.5f, 0.62f, 0.3f, 0.93f); break;
	case EInkEvidenceType::Piss:   Splat = FLinearColor(0.8f, 0.68f, 0.1f, 0.93f); break;
	default:                       Splat = FLinearColor(0.24f, 0.13f, 0.04f, 0.95f); break;
	}
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	// 不規則遮蔽：幾塊交疊大色塊，留小縫（部分致盲）
	DrawRect(Splat, 0.0f, 0.0f, W * 0.62f, H * 0.75f);
	DrawRect(Splat, W * 0.45f, H * 0.18f, W * 0.55f, H * 0.62f);
	DrawRect(Splat, W * 0.12f, H * 0.55f, W * 0.72f, H * 0.45f);
	DrawRect(Splat, W * 0.3f, 0.0f, W * 0.5f, H * 0.3f);
	DrawTok(TEXT("SPLAT!"), W * 0.5f, H * 0.4f, ETextTier::Display, NiHudColor::Paper, EHAlign::Center, true);
	DrawTok(TEXT("washes off at the accusation"), W * 0.5f, H * 0.4f + 52.0f * UiScale, ETextTier::Small, NiHudColor::Paper, EHAlign::Center, false);
}

void ANiceInkHUD::DrawVictimSleepUI(ANiceInkCharacter* MyChar, const ANiceInkGameState* GS, const ANiceInkPlayerState* MyPS)
{
	// AR 鏡像豁免：描圖盤/姿勢面板＝玩法幾何（夢裡的圖形不因語言翻面）
	TGuardValue<bool> MirrorGuard(bMirrorSuspended, true);

	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;

	// 視覺全遮蔽：看不到任何人、任何筆跡、自己身上任何刺青
	DrawRect(FLinearColor(0.01f, 0.01f, 0.015f, 1.0f), 0.0f, 0.0f, W, H);

	if (!GS)
	{
		return;
	}

	// 罰酒杯已搬去**右下角比分**（2026-09-05，DrawScoreCorner）——沉睡端此前在
	// 上緣中央自己畫一份 24px 的杯列，而右下角現在也有一份 40px 的
	// ⇒ 同一件事講兩次，還是兩種大小。畫面上唯一的比分只能有一個位置。
	// （這一處是被 phase_consistency 的截圖比對翻出來的：夢境那張的上緣冒出兩個
	// 小杯子，而我以為我只在上緣拆掉了它。**「我以為我刪掉了」要用工具查。**）

	// 醉夢描圖（SPEC v4.0 定案 #49）：沿線描完＝甦醒；描出線＝重來
	UDreamTraceComponent* Trace = MyChar->DreamTrace;
	UDreamMazeComponent* Maze = MyChar->DreamMaze;
	if (Trace && Trace->IsTraceActive())
	{
		// 整圖入鏡（08-02 user 裁決：圖案是夢裡唯一的參考系——速度=完成時間與
		// 針速/帶寬比，皆縮放不變；畫面歸針/像素倍率同源已退役）。
		// 可用矩形＝杯數列之下、底部提示行之上、左右留邊——包圍盒貼合最大化
		//（圓形面板＋MaxAbsR 半徑貼合對非圓圖案縮到三~五成＝「圖太小」病根；
		// 進度%併入提示行＝整條 H*0.86 保留帶還給圖）；姿勢面板＝撞到才讓位。
		const FBox2D PanelAvail(
			FVector2D(50.0f * UiScale, 95.0f * UiScale),
			FVector2D(W - 50.0f * UiScale, H - 70.0f * UiScale));
		const FBox2D PanelAvoid(
			FVector2D(34.0f * UiScale, H - 246.0f * UiScale),
			FVector2D(234.0f * UiScale, H - 34.0f * UiScale));
		Trace->DrawTracePanel(Canvas, PanelAvail, PanelAvoid);

		// 事件行（搖晃顯名＝怒氣要有地址；失敗＝當場明講重來）——蓋在圖上、瞬態
		if (Trace->IsShakeActive())
		{
			DrawTok(NiLoc::TFmt(this, ENiLocKey::DreamShake, Trace->GetShakeAttackerName().ToUpper()),
				W * 0.5f, H * 0.16f, ETextTier::Display,
				NiHudColor::Red, EHAlign::Center, true);
		}
		else if (Trace->IsFailFlashing())
		{
			DrawTok(NiLoc::T(this, ENiLocKey::DreamSlipped),
				W * 0.5f, H * 0.16f, ETextTier::Title,
				NiHudColor::Red, EHAlign::Center, true);
		}
		DrawBottomHint(NiLoc::TFmt(this, ENiLocKey::DreamProgress,
			FString::FromInt(FMath::RoundToInt(Trace->GetProgress01() * 100.0f))), NiHudColor::Lavender);
	}
	else if (Maze && Maze->IsMazeActive())
	{
		const FVector2D PanelCenter(W * 0.5f, H * 0.46f);
		const float PanelRadius = FMath::Min(W, H) * 0.30f;
		Maze->DrawMazePanel(Canvas, PanelCenter, PanelRadius);

		switch (Maze->GetSimState())
		{
		case EDreamMazeSimState::Walking:
			DrawBottomHint(TEXT("hold LMB — walk out of the dream to wake"), NiHudColor::Lavender);
			break;
		case EDreamMazeSimState::DeathScreen:
		{
			// 兇手公開（怒氣要有地址）；度數永不顯示
			const APlayerState* KillerPS = GS->FindPlayerStateById(Maze->GetLastKillerId());
			DrawRect(FLinearColor(0.4f, 0.02f, 0.02f, 0.35f), 0.0f, 0.0f, W, H);
			// 整組抬離中央光圈（截圖自查：壓在圓盤上讀感髒）
			const float TextY = PanelCenter.Y - PanelRadius * 0.52f;
			const float IconS = 64.0f * UiScale;
			DrawIconTok(IconTrap, PanelCenter.X - IconS * 0.5f, TextY - IconS - 12.0f * UiScale, IconS, NiHudColor::Red);
			DrawTok(FString::Printf(TEXT("TRAPPED BY %s !"),
				KillerPS ? *FitTok(KillerPS->GetPlayerName().ToUpper(), ETextTier::Display, 380.0f * UiScale, true) : TEXT("???")),
				PanelCenter.X, TextY, ETextTier::Display, NiHudColor::Red, EHAlign::Center, true);
			break;
		}
		case EDreamMazeSimState::AwaitRotation:
		case EDreamMazeSimState::Rotating:
			// 不給任何關於度數的文字——盯緊圓形自己抓定位點
			DrawTok(NiLoc::T(this, ENiLocKey::DreamReels),
				PanelCenter.X, PanelCenter.Y + PanelRadius + 14.0f * UiScale, ETextTier::Body, NiHudColor::Lavender, EHAlign::Center, false);
			break;
		default:
			break;
		}
	}
	else
	{
		// 沒有迷宮＝終局昏死（server 不發夢）：昏睡不醒
		// 副標接在主標實際高度之下（Display 已是 64 級；寫死 52 會疊在一起——09-06 截圖實錘）
		const float BannerH = DrawTok(NiLoc::T(this, ENiLocKey::BannerOutCold), W * 0.5f, H * 0.4f,
			ETextTier::Display, NiHudColor::Red, EHAlign::Center, true).Y;
		DrawTok(NiLoc::T(this, ENiLocKey::DreamCash), W * 0.5f, H * 0.4f + BannerH + NiUi::GapM * UiScale,
			ETextTier::Body, NiHudColor::PaperDim, EHAlign::Center, false);
	}

	// 姿勢面板（左下）：自己身體的示意——當前姿勢與朝向。
	// 永不顯示身上墨跡的即時變化（SPEC 護欄）。
	{
		const float PanelW = 200.0f * UiScale;
		const float PanelH = 212.0f * UiScale;
		const float PanelX = 34.0f * UiScale;
		const float PanelY = H - PanelH - 34.0f * UiScale;
		// 無面板（2026-09-04）：與上緣／底部同一條規則——姿勢指示是夢裡黑屏上的
	// 元素，本來就有對比，面板只是舊語言的殘留。
		DrawTok(NiLoc::T(this, MyChar->bBodyFaceDown ? ENiLocKey::PoseFaceDown : ENiLocKey::PoseFaceUp),
			PanelX + PanelW * 0.5f, PanelY + 10.0f * UiScale, ETextTier::Small,
			MyChar->bBodyFaceDown ? NiHudColor::Amber : NiHudColor::Paper, EHAlign::Center, true);

		// 極簡人形俯視圖：頭＋軀幹＋四肢大字
		const float BodyCX = PanelX + PanelW * 0.5f;
		const float BodyCY = PanelY + PanelH * 0.56f;
		const float B = UiScale;
		const FLinearColor BodyColor = NiHudColor::Skin;
		DrawRect(BodyColor, BodyCX - 10.0f * B, BodyCY - 68.0f * B, 20.0f * B, 20.0f * B);           // 頭
		DrawRect(BodyColor, BodyCX - 16.0f * B, BodyCY - 46.0f * B, 32.0f * B, 66.0f * B);           // 軀幹
		Canvas->K2_DrawLine(FVector2D(BodyCX - 14.0f * B, BodyCY - 40.0f * B), FVector2D(BodyCX - 48.0f * B, BodyCY - 10.0f * B), 6.0f * B, BodyColor); // 左臂
		Canvas->K2_DrawLine(FVector2D(BodyCX + 14.0f * B, BodyCY - 40.0f * B), FVector2D(BodyCX + 48.0f * B, BodyCY - 10.0f * B), 6.0f * B, BodyColor); // 右臂
		Canvas->K2_DrawLine(FVector2D(BodyCX - 9.0f * B, BodyCY + 20.0f * B), FVector2D(BodyCX - 33.0f * B, BodyCY + 62.0f * B), 6.0f * B, BodyColor);  // 左腿
		Canvas->K2_DrawLine(FVector2D(BodyCX + 9.0f * B, BodyCY + 20.0f * B), FVector2D(BodyCX + 33.0f * B, BodyCY + 62.0f * B), 6.0f * B, BodyColor);  // 右腿
	}

	// 技能庫存（右下）：噴射（v4.0 定案 #51 噴射拳腳移出核心循環——雙閘全關＝
	// 整列不畫；未來更新回歸時原地起用）
	if (GNiceInkSprayEnabled || GNiceInkKickEnabled)
	{
		static const TCHAR* OriginNames[] = { TEXT("NOSE"), TEXT("CROTCH"), TEXT("BUTT") };
		const int32 OriginIdx = FMath::Clamp(static_cast<int32>(MyChar->SelectedSprayOrigin), 0, 2);
		const FString HintText = GNiceInkKickEnabled
			? FString::Printf(TEXT("Q spray from %s  ·  E kick  ·  arrows aim"), OriginNames[OriginIdx])
			: FString::Printf(TEXT("Q spray from %s (1/2/3)  ·  arrow keys aim"), OriginNames[OriginIdx]);

		// 面板寬＝量測提示行自動定寬（固定寬在小視窗溢出——截圖自查教訓）
		const float Pad = 18.0f * UiScale;
		const float PanelW = MeasureTok(HintText, ETextTier::Small, false).X + Pad * 2.0f;
		const float PanelH = 92.0f * UiScale;
		const float PanelX = W - PanelW - 34.0f * UiScale;
		const float PanelY = H - PanelH - 34.0f * UiScale;
		DrawPanelBox(PanelX, PanelY, PanelW, PanelH, 0.88f);

		const float IconS = 30.0f * UiScale;
		float X = PanelX + Pad;
		const float RowY = PanelY + 14.0f * UiScale;
		const FLinearColor SprayTint = MyChar->SprayCharges > 0 ? NiHudColor::Paper : NiHudColor::PaperDim;
		DrawIconTok(IconSpray, X, RowY, IconS, SprayTint);
		X += IconS + 6.0f * UiScale;
		X += DrawTok(FString::Printf(TEXT("x%d"), MyChar->SprayCharges), X, RowY + 3.0f * UiScale, ETextTier::Body, SprayTint, EHAlign::Left, true).X;
		if (GNiceInkKickEnabled)
		{
			X += 26.0f * UiScale;
			const FLinearColor KickTint = MyChar->KickCharges > 0 ? NiHudColor::Paper : NiHudColor::PaperDim;
			DrawIconTok(IconKick, X, RowY, IconS, KickTint);
			X += IconS + 6.0f * UiScale;
			DrawTok(FString::Printf(TEXT("x%d"), MyChar->KickCharges), X, RowY + 3.0f * UiScale, ETextTier::Body, KickTint, EHAlign::Left, true);
		}

		DrawTok(HintText, PanelX + PanelW * 0.5f, PanelY + PanelH - 30.0f * UiScale, ETextTier::Small, NiHudColor::PaperDim, EHAlign::Center, false);
	}
}

void ANiceInkHUD::DrawTrapDial(const ANiceInkCharacter* MyChar)
{
	if (!MyChar || !MyChar->bTrapDialActive || !GetWorld())
	{
		return;
	}

	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	const FVector2D Center(W - 200.0f * UiScale, H * 0.38f);
	const float Radius = 70.0f * UiScale;
	const float Remaining = FMath::Max(0.0f, MyChar->TrapDialEndTime - GetWorld()->GetTimeSeconds());

	// AR 鏡像豁免：轉盤＝玩法幾何（角度方向/指針/刻度不因語言翻面）
	TGuardValue<bool> MirrorGuard(bMirrorSuspended, true);

	DrawTok(NiLoc::T(this, ENiLocKey::TrapTitle), Center.X, Center.Y - Radius - 74.0f * UiScale, ETextTier::Body, NiHudColor::Paper, EHAlign::Center, true);
	DrawTok(NiLoc::T(this, ENiLocKey::TrapSpin), Center.X, Center.Y - Radius - 48.0f * UiScale, ETextTier::Title, NiHudColor::Amber, EHAlign::Center, true);

	// 盤面
	Canvas->K2_DrawPolygon(nullptr, Center, FVector2D(Radius + 10.0f * UiScale, Radius + 10.0f * UiScale), 32, FLinearColor(0.05f, 0.03f, 0.08f, 0.85f));
	for (int32 Tick = 0; Tick < 8; ++Tick)
	{
		const float Phi = Tick * PI / 4.0f;
		const FVector2D Dir(FMath::Sin(Phi), -FMath::Cos(Phi));
		Canvas->K2_DrawLine(Center + Dir * (Radius - 8.0f * UiScale), Center + Dir * Radius, 2.0f * UiScale, FLinearColor(0.4f, 0.35f, 0.5f, 1.0f));
	}
	const float RotIcon = 30.0f * UiScale;
	FLinearColor RotTint = NiHudColor::Lavender;
	RotTint.A = 0.45f;
	DrawIconTok(IconRotate, Center.X - RotIcon * 0.5f, Center.Y - RotIcon * 0.5f, RotIcon, RotTint);

	// 指針（正度數＝順時針）；CW 橘／CCW 青
	const float NeedlePhi = FMath::DegreesToRadians(MyChar->TrapDialAngleDeg);
	const FVector2D NeedleDir(FMath::Sin(NeedlePhi), -FMath::Cos(NeedlePhi));
	const FLinearColor NeedleColor = MyChar->TrapDialAngleDeg >= 0.0f
		? FLinearColor(1.0f, 0.55f, 0.15f, 1.0f) : FLinearColor(0.2f, 0.8f, 0.9f, 1.0f);
	Canvas->K2_DrawLine(Center, Center + NeedleDir * (Radius - 6.0f * UiScale), 4.0f * UiScale, NeedleColor);

	// 倒數條
	const float BarW = (Radius * 2.0f) * FMath::Clamp(Remaining / FMath::Max(0.1f, MyChar->TrapDialDuration), 0.0f, 1.0f);
	DrawRect(FLinearColor(0.9f, 0.2f, 0.15f, 0.9f), Center.X - Radius, Center.Y + Radius + 14.0f * UiScale, BarW, 5.0f * UiScale);

	DrawTok(FString::Printf(TEXT("%+.0f°"), MyChar->TrapDialAngleDeg),
		Center.X, Center.Y + Radius + 24.0f * UiScale, ETextTier::Title, NeedleColor, EHAlign::Center, true);
	DrawTok(NiLoc::TFmt(this, ENiLocKey::TrapHint, FString::Printf(TEXT("%.1f"), Remaining)),
		Center.X, Center.Y + Radius + 56.0f * UiScale, ETextTier::Small, NiHudColor::PaperDim, EHAlign::Center, false);
}

void ANiceInkHUD::DrawInkCrosshair(const ANiceInkGameState* GS)
{
	if (!Canvas)
	{
		return;
	}

	// AR 鏡像豁免：準星/落點/筆 viewmodel＝玩法幾何（錨定 aim 不因語言翻面）
	TGuardValue<bool> MirrorGuard(bMirrorSuspended, true);

	FLinearColor CrosshairColor = FLinearColor::White;
	if (const ANiceInkCharacter* MyChar = PlayerOwner ? Cast<ANiceInkCharacter>(PlayerOwner->GetPawn()) : nullptr)
	{
		if (MyChar->bAsleep)
		{
			return; // 沉睡：無準星（黑屏＋小遊戲）
		}
		if (MyChar->bInkTrayOpen)
		{
			return; // 托盤開著＝作畫 chrome 全讓開（雙游標/筆貼圖壓面板＝user 定罪）
		}
		CrosshairColor = MyChar->GetCurrentColor();
		CrosshairColor.A = 1.0f;

		// 筆即游標（07-20 定案）：UI 準星退役——世界上只有一支實體筆。
		// 接觸墨點＝筆尖可落墨時，在筆尖的螢幕投影畫一粒選色小點（錨在筆尖、
		// 不在螢幕中心——任何中心標記都會重新製造「兩支筆」的對照）；
		// 搆不到＝無點（筆懸空本身就是訊號），持續 >1s 由底部提示補一句話。
		if (MyChar->bLeanLocked)
		{
			// 準星歸 2D（07-22 user 定案）：落點指示固定畫在螢幕正中心（零抖動、
			// FPS 標準）——舊制「錨在世界筆尖投影」的小點會吃到解算殘差＋濾波相位差
			// 的像素跳動；「兩支筆」禁令針對的矛盾源（實體筆＋UI 準星並存）在 2D 筆制
			// 下已不存在。墨照舊從世界針尖出（偏差=解算容差 ≤1.5cm、感知可忽略）。
			// 可落墨才顯示（搆不到=無點的訊號語義保留）。
			// 07-24 皮繩制二修：畫面屬於針（作畫中相機=針 aim）⇒ 螢幕中心構造上
			// 恆=針尖=墨的出生點——2D 筆/落點/針線錨死中心即與墨重合，viewmodel
			// 恆定（07-22 定案）不破。（一修曾把筆錨到針的投影＝筆離開中心，被
			// user 打回「筆要維持在螢幕中間」——正解是畫面歸針，不是筆追針。）
			// 三工具制（07-25 打稿制）：打稿筆=龍膽紫小方點＋2D 麥克筆 viewmodel；
			// 液線針=選色小方點；霧針=筆刷範圍圈（自由揮掃要知道落在哪圈）
			const bool bShaderNeedle = MyChar->SelectedNeedle == EInkNeedle::Shader;
			const bool bStencilPen = MyChar->SelectedNeedle == EInkNeedle::Stencil;
			// 錨點：機器工具=螢幕中心（畫面歸針＝中心構造上恆=針尖）；稿筆游標制
			//（07-31）=游標的螢幕投影（相機=惰性 gaze、游標在畫面上自由移動——
			// 小畫家式；投影必用 Canvas->Project＝HUD 投影鐵律）
			FVector2D Aim(Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f);
			if (bStencilPen)
			{
				FVector CursorW;
				if (MyChar->GetStencilCursorHudWorld(CursorW))
				{
					const FVector Pr = Project(CursorW);
					if (Pr.Z > 0.0f)
					{
						Aim = FVector2D(Pr.X, Pr.Y);
					}
				}
			}
			// 落墨小點/✕ 讀的是墨閘同一個裁決（07-29 單一裁判）——皮膚紗在稜線
			// 掠射角會被透視壓成看不見的細縫（腳掌實錘），筆尖級提示任何角度都準
			const bool bReach = MyChar->IsCursorDrawable();
			if (!bReach && MyChar->HasDrawTarget())
			{
				// 游標點不可畫＝筆尖紅 ✕（硬切、美術語言 #24）
				const float A = 7.0f * UiScale;
				const FLinearColor XCol = NiHudColor::Red.CopyWithNewOpacity(0.9f);
				DrawLine(Aim.X - A, Aim.Y - A, Aim.X + A, Aim.Y + A, XCol, 2.5f * UiScale);
				DrawLine(Aim.X - A, Aim.Y + A, Aim.X + A, Aim.Y - A, XCol, 2.5f * UiScale);
			}
			// **落點指示的實心小方點已於 09-03 四版退役**：三支筆改用同一個指示器
			// （見下方 `bReach` 區塊）。它是方的只因為 `DrawRect` 最好寫，而落墨是
			// 圓的；它是實心的，蓋住的正好就是「墨會落在哪」那個位置；而且它與打霧
			// 的圈用了兩套不同的對比手法解同一個問題。三個不一致，全是堆出來的。
			if (bStencilPen)
			{
				// 2D 麥克筆 viewmodel（07-25 二修 user 定案「與機器同構：旁人 3D＋本人
				// 2D」；貼圖版=SM_Marker 染紫 Blender 渲染 T_UI_MarkerPen）：
				// 貼圖以筆尖為樞軸右傾 30°（同機器握姿）、按住 LMB＝筆壓近落點（壓筆感）。
				const bool bInkingPen = MyChar->IsPenTriggerHeldLocal() && bReach;
				constexpr float PenTiltDeg = 30.0f;
				const float TiltRad = FMath::DegreesToRadians(PenTiltDeg);
				const FVector2D AxisUp(FMath::Sin(TiltRad), -FMath::Cos(TiltRad));
				const float GapPx = (bInkingPen ? 2.0f : 14.0f) * UiScale;
				// 拉繩穩定器（08-02）：繪製中 2D 筆錨到拉繩墨尖（筆尖=墨出處；
				// 游標先走、筆沿平滑路徑追=Lazy Mouse 讀感）；小點/✕ 照舊錨生游標
				FVector2D PenAnchor = Aim;
				FVector LazyW;
				if (MyChar->GetStencilLazyTipHudWorld(LazyW))
				{
					const FVector Pr = Project(LazyW);
					if (Pr.Z > 0.0f)
					{
						PenAnchor = FVector2D(Pr.X, Pr.Y);
					}
				}
				const FVector2D TipPt = PenAnchor + AxisUp * GapPx;
				if (MarkerSprite)
				{
					// 筆尖在貼圖內的正規化座標（render_marker_ui.py 印出）
					constexpr float TipU = 0.5000f, TipV = 0.9310f;
					const float SpriteW = Canvas->ClipY * 0.46f; // 方形貼圖邊長（筆長≈0.4×ClipY）
					DrawTexture(MarkerSprite, TipPt.X - TipU * SpriteW, TipPt.Y - TipV * SpriteW,
						SpriteW, SpriteW, 0.0f, 0.0f, 1.0f, 1.0f, FLinearColor::White,
						EBlendMode::BLEND_Translucent, 1.0f, false,
						PenTiltDeg, FVector2D(TipU, TipV));
					// 稿筆針尖標籤已刪除（畫面清單 2026-09-02）：紫色 2D 麥克筆不可能認錯
				}
				else
				{
					// 貼圖缺席退路：向量筆（深筆頭＋紫筆桿＋淺尾帽）
					const FVector2D NibEnd = TipPt + AxisUp * 26.0f * UiScale;
					const FVector2D BodyEnd = TipPt + AxisUp * (Canvas->ClipY * 0.34f);
					const FVector2D CapEnd = BodyEnd + AxisUp * 16.0f * UiScale;
					const FLinearColor BodyCol = NiceInkStencil::Color() * FLinearColor(0.6f, 0.6f, 0.6f, 1.0f);
					DrawLine(NibEnd.X, NibEnd.Y, BodyEnd.X, BodyEnd.Y, BodyCol, 40.0f * UiScale);
					DrawLine(BodyEnd.X, BodyEnd.Y, CapEnd.X, CapEnd.Y,
						FLinearColor(0.82f, 0.80f, 0.86f, 1.0f), 40.0f * UiScale);
					DrawLine(TipPt.X, TipPt.Y, NibEnd.X, NibEnd.Y,
						FLinearColor(0.10f, 0.04f, 0.16f, 1.0f), 18.0f * UiScale);
					// 稿筆針尖標籤已刪除（畫面清單 2026-09-02）：紫色 2D 麥克筆不可能認錯
				}
			}
			// **三支筆共用的落點指示器**（09-03 四版，user 定案）。此前打霧是圓圈、
			// 另外兩支是實心小方點——那不是設計，是兩次不同動機的改動疊出來的。
			// 現制＝同一個畫法，**半徑就是那支筆真正的落墨半徑**（打霧 1cm、稿筆／
			// 割線 1.5mm）⇒ **換筆時圈的大小直接告訴你這支筆多粗**；小到極限時它
			// 自然看起來就是一個點，那是連續的退化，不是換一種圖形。
			if (bReach)
			{
				FVector TipW;
				if (MyChar->GetPenTipWorldForHud(TipW))
				{
					const float RadiusCm = MyChar->GetNeedleRadiusCmForHud();
					const FVector P1 = Project(TipW);
					const FVector P2 = Project(TipW + FVector(0.0f, 0.0f, RadiusCm));
					if (P1.Z > 0.0f && P2.Z > 0.0f)
					{
						// 下限 3px：3mm 的筆在遠處會投影成不到一個像素，圈要留得住
						const float Rpx = FMath::Clamp(FVector2D::Distance(
							FVector2D(P1.X, P1.Y), FVector2D(P2.X, P2.Y)), 3.0f, 600.0f);
						// **段數隨半徑**（09-03 五版，robo 定罪後修）：4~5px 的小圈畫 28 段
						// 是純浪費——弧長不到半個像素的線段，畫出來與 8 段沒有分別。
						// 這不是為了通過契約的 hack：`cruise tipSpd` 基線 1.99 對上限 2.00
						// **只有 0.5% 餘裕**，而我對 Liner 的小圈也做滿額工作（28 段＋三次
						// UV 解算）把它推到 2.02。**每幀成本會沿著離散步進洩漏進手感**
						// ——針以 v_max 逐幀追趕，幀率一降每幀就走更遠、實走距離變長。
						const int32 Segs = FMath::Clamp(FMath::RoundToInt(Rpx * 0.9f), 8, 28);
						// **圈內填色已於 09-03 四版拆除**（user：「不太懂圈圈內有不明顯
						// 的顏色顯示透明度有什麼意義」——他是對的，而且那個需求是我
						// 發明的）。兩條理由：
						// ①**一團半透明的墨沒有絕對可讀性**：要三檔並排才知道這一團是
						//   中間那檔，而三檔並排正是墨杯盤在做的事，圈裡一次只給一檔。
						//   能回答「幾 %」的是數字，不是色塊——而數字已經在 chip 上。
						// ②我把**狀態**（我現在在哪一檔／偶爾查一次／週邊的 chip 就夠）
						//   誤當成**事件**（我剛改了一檔／每次滾輪／需要當場短暫回饋）
						//   來解，於是用常駐顯示去解事件需求＝畫面永遠掛著一團東西，
						//   而真正需要回饋的那一瞬間它給的還是猜不出數值的深淺。
						//   **兩邊都沒解好。** 濃度回歸 chip（09-02 原制）。
						//
						// **輪廓＝當前墨色主線 ＋ 內暗外亮的雙向暈**（09-03 六版終案）。
						//
						// 病根（三版 user 回報「底色和筆顏色相同時看不清楚筆頭」）是**拿內容的顏色去
						// 畫指示器**：圈用當前墨色 ⇒ 畫在剛塗好的同色墨上對比為零、整個消失，而
						// 「在剛畫好的墨上繼續畫」正是打霧筆最常做的事——**最常見的使用情境剛好是這個
						// 畫法的最壞情況**（乾淨皮膚上永遠看不到這個缺陷，所以做的時候不會發現）。
						//
						// 四版曾走「真的去讀底下是什麼」：畫布在落墨當下記一張亮度圖，描邊逐段查表、
						// 用 WCAG 反解出剛好 3:1 的對比色。它是對的，但**代價太大**——64KB／人的圖、
						// 每針的記帳、洗墨後的重算、每幀的 Jacobian，而那個每幀成本被 robo 抓到
						//（`cruise tipSpd` 基線 1.99 → 2.02，兩輪穩定重現、stash 驗基線復現）。
						//
						// 六版換維度（user 追問「業界用什麼手段」後定案）：**顏色對比只是四類手段之一**，
						// 另外三類是「自己製造背景」（陰影／暈／底板）、「運動」（marching ants）、
						// 「形狀」（角括號）。暈屬於第二類——**不需要知道底色**，所以那整套取樣機制連同
						// 它的每幀成本一起沒有存在理由了（已拆除，不留死碼）。
						//
						// 為什麼暈就夠：四種情況全涵蓋——底亮 ⇒ 內側暗暈拉開；底暗 ⇒ 外側亮暈拉開；
						// 墨色與底同暗 ⇒ 外亮暈仍在；墨色與底同亮 ⇒ 內暗暈仍在。**最差是中灰底，兩側
						// 各有約 0.5 的亮度差**，而那是不讀畫面所能達到的下界（黑與白之中，必有一個與
						// 任何亮度相距 ≥0.5）。主線因此可以放心地帶當前這杯墨的顏色。
						//
						// 顏色只從全站 token 拿（三版我寫過裸數字＝冷調藍紫灰與純白，而畫面上其餘一切
						// 都是暖調 ⇒ 讀成「貼上去的」；專案本來就禁裸顏色）。
						const float LW = 1.5f * UiScale;
						// **底色亮度圖那一整套已於 09-03 六版拆除**：雙向暈屬於「自己製造
						// 背景」，**不需要知道底色** ⇒ 逐段取樣、Jacobian、64KB 的亮度圖、
						// 每針的記帳、洗墨後的重算，全部沒有消費者了。留著就是死碼，而且
						// 它的每幀成本正是把 cruise tipSpd 從 1.99 推到 2.02 的東西。
						const FLinearColor MainCol =
							(bStencilPen ? NiceInkStencil::Color() : CrosshairColor)
								.CopyWithNewOpacity(0.95f);
						// **實體三角形環帶，不是 N 條線段拼的圓**（user：「為什麼會斷斷續續的」）。
						// 用 DrawLine 拼圓有兩個各自獨立的斷法：
						//   ①每段兩端是**平頭**（Canvas 的線沒有 round cap）⇒ 相鄰段在轉角處露出楔形缺口
						//   ②**0.75px 的描邊不足一個像素**，而 Canvas 的線沒有抗鋸齒 ⇒ 像素中心沒落進
						//     那條細四邊形的段落，整段被跳過
						// 環帶是實體填充、**相鄰段共用同一組頂點** ⇒ 接縫在數學上不存在（不是「接得很好」，
						// 是根本沒有接縫）；不足一像素的部分被光柵化成部分覆蓋而不是消失。
						// 成本不變：一條 thick line 本來也是兩個三角形。
						// UIn/UOut＝在衰減貼圖上的取樣位置（0＝貼著主線最濃、1＝尾端全透明）。
						// **曲線住在貼圖裡**，所以頂點只要給兩端的 t，三角形數量不變。
						auto AddBand = [&](TArray<FCanvasUVTri>& Out, float RIn, float ROut, int32 i,
							const FLinearColor& CIn, const FLinearColor& COut,
							float UIn, float UOut)
						{
							const float A0 = 2.0f * PI * i / Segs;
							const float A1 = 2.0f * PI * (i + 1) / Segs;
							const FVector2D D0(FMath::Cos(A0), FMath::Sin(A0));
							const FVector2D D1(FMath::Cos(A1), FMath::Sin(A1));
							const FVector2D UvIn(UIn, 0.5f), UvOut(UOut, 0.5f);
							FCanvasUVTri T;
							T.V0_Pos = Aim + D0 * RIn;  T.V0_Color = CIn;  T.V0_UV = UvIn;
							T.V1_Pos = Aim + D0 * ROut; T.V1_Color = COut; T.V1_UV = UvOut;
							T.V2_Pos = Aim + D1 * ROut; T.V2_Color = COut; T.V2_UV = UvOut;
							Out.Add(T);
							T.V0_Pos = Aim + D0 * RIn;  T.V0_Color = CIn;  T.V0_UV = UvIn;
							T.V1_Pos = Aim + D1 * ROut; T.V1_Color = COut; T.V1_UV = UvOut;
							T.V2_Pos = Aim + D1 * RIn;  T.V2_Color = CIn;  T.V2_UV = UvIn;
							Out.Add(T);
						};
						// **內暗外亮的雙向暈**（09-03 六版，user 定案）。這是「自己製造背景」那一類手段
						// ——**不需要知道底色**，所以比動態求對比色更穩健，也不必為取樣付每幀成本
						//（那正是把 cruise tipSpd 從 1.99 推到 2.02 的東西）。四種情況全涵蓋：
						//   底亮 ⇒ 內側暗暈拉開；底暗 ⇒ 外側亮暈拉開；
						//   墨色與底同暗 ⇒ 外亮暈仍在；墨色與底同亮 ⇒ 內暗暈仍在。
						// **最差情況是中灰底，兩側的暈各有約 0.5 的亮度差**——仍然可見，而且那是不讀
						// 畫面所能達到的下界（黑與白之中，必有一個與任何亮度相距 ≥0.5）。
						// 暈用漸層（外緣 alpha=0）⇒ 讀起來是「一條細線帶柔邊」，不是三條並排的線。
						// **暈寬隨半徑、小圈不畫內暈**（09-03 六版二修）。兩個理由，
						// 一個設計一個成本：
						//   設計＝稿筆／割線的圈只有 4~5px 半徑，內側總共才 2~3px；
						//     2.5px 的內暈會把中心填成一坨，那不是柔邊是污漬。
						//   成本＝`cruise tipSpd` 契約 1.50~2.00、基線 1.99＝**0.5% 餘裕**。
						//     三環帶版每段 6 個三角形（線段版 4 個）＝+50%，實測把它推到
						//     2.01。小圈省掉內暈就回到 4 個。**每幀成本會沿著離散步進洩漏
						//     進手感**——針以 v_max 逐幀追趕，幀率一降每幀就走更遠。
						const float GW = FMath::Min(2.5f * UiScale, Rpx * 0.3f);
						const bool bInnerGlow = Rpx > 10.0f * UiScale;
						// 峰值 0.49（09-03 六版三修，user：「暈感不夠重，太像一圈亮圈、
						// 一圈暗圈」，要求透明度降半、但**不要擴大範圍**）。
						// **這個數字是算出來的，不是我改了他的要求**：他要的是把線性版的
						// 0.55 降到 0.275，而換成指數衰減之後——
						//   線性曲線在 [0,1] 上的平均覆蓋 = 0.500 ⇒ 他要的重量 = 0.1375
						//   指數曲線（k=3、已扣尾正規化）平均覆蓋 = 0.281
						//   ⇒ 要達到同樣的視覺重量，峰值 = 0.1375 / 0.281 = 0.489
						// 也就是說：**總墨量與他要求的一致，但貼著線的那一圈濃了 78%**
						//（0.49 vs 0.275）而尾巴掉得更快。對比保障因此留在真正需要它的
						// 位置——緊貼輪廓那幾個像素——而不是攤平成一條看得見的帶。
						const FLinearColor GlowDark = NiHudColor::Ink.CopyWithNewOpacity(0.49f);
						const FLinearColor GlowLight = NiHudColor::Paper.CopyWithNewOpacity(0.49f);
						const float MRi = Rpx - LW * 0.5f;   // 主線內緣
						const float MRo = Rpx + LW * 0.5f;   // 主線外緣
						TArray<FCanvasUVTri> Tris;
						Tris.Reserve(Segs * 6);   // 上限（大圈三環帶）
						for (int32 i = 0; i < Segs; ++i)
						{
							// 暈的兩端都給同一個顏色，**濃淡整個交給貼圖**（此前是用頂點
							// alpha 從 0 線性升到峰值＝線性衰減）。內暈的 t 由外向內遞增
							//（貼著主線的那一側 t=0）。
							if (bInnerGlow)   // 內暈（小圈略過：內側放不下，且成本要還給 cruise）
							{
								AddBand(Tris, FMath::Max(MRi - GW, 0.0f), MRi, i,
									GlowDark, GlowDark, 1.0f, 0.0f);
							}
							AddBand(Tris, MRi, MRo, i, MainCol, MainCol, 0.0f, 0.0f); // 主線（t=0＝不透明）
							AddBand(Tris, MRo, MRo + GW, i, GlowLight, GlowLight, 0.0f, 1.0f); // 外暈
						}
						// 貼圖＝衰減曲線；缺席時退回白貼圖（＝退化成沒有暈的實心環帶，
						// 仍然畫得出來，不會整個消失）
						FCanvasTriangleItem RingItem(Tris,
							GlowTex && GlowTex->GetResource() ? GlowTex->GetResource() : GWhiteTexture);
						RingItem.BlendMode = SE_BLEND_Translucent;
						Canvas->DrawItem(RingItem);
					}
				}
			}

			// 巡航導引（07-22 二改「業界式」）：皮膚面預測路徑＋行進蟻虛線——
			// 巡航步進器往前模擬 8cm 的貼膚曲線投影到螢幕，沿弧長畫虛線；
			// 虛線相位隨時間向前滾動（marching ants＝方向零歧義）、遠端漸淡、
			// 線停在剪影邊緣＝「針會在這裡釘住」的預告。指「路徑」不是位置目標點
			//（不重蹈「兩支筆」雙指標矛盾）；放開左鍵/收回死區即消失。
			{
				TArray<FVector> GuidePts;
				if (MyChar->BuildTattooGuidePath(GuidePts) >= 2)
				{
					TArray<FVector2D> Scr;
					Scr.Reserve(GuidePts.Num());
					for (const FVector& W : GuidePts)
					{
						const FVector Pr = Project(W);
						if (Pr.Z <= 0.0f)
						{
							break; // 相機後方（理論上不會發生）
						}
						Scr.Add(FVector2D(Pr.X, Pr.Y));
					}
					// 折線平滑（07-22 三修）：retopo 大三角面上逐步 trace 的落點微鋸齒
					// ＋模擬步長不完全均勻→折線毛邊；3 點滑動平均殺面片鋸齒、
					// 保留真曲率（端點錨定：起點釘針尖、終點釘邊緣截斷）
					if (Scr.Num() >= 3)
					{
						TArray<FVector2D> Smoothed = Scr;
						for (int32 i = 1; i < Scr.Num() - 1; ++i)
						{
							Smoothed[i] = (Scr[i - 1] + Scr[i] + Scr[i + 1]) / 3.0f;
						}
						Scr = MoveTemp(Smoothed);
					}
					float TotalPx = 0.0f;
					for (int32 i = 1; i < Scr.Num(); ++i)
					{
						TotalPx += FVector2D::Distance(Scr[i - 1], Scr[i]);
					}
					if (Scr.Num() >= 2 && TotalPx > 8.0f)
					{
						const float DashPx = 7.0f * UiScale;
						const float GapPx2 = 5.0f * UiScale;
						const float Period = DashPx + GapPx2;
						const float SkipPx = 12.0f * UiScale; // 讓開中心落點
						// 行進蟻：相位隨時間增加＝dash 沿路徑向外滾（前進方向）
						const float MarchPx = FMath::Fmod(static_cast<float>(
							GetWorld()->GetTimeSeconds()) * 36.0f * UiScale, Period);
						float S = 0.0f; // 投影路徑弧長（px）
						for (int32 i = 1; i < Scr.Num(); ++i)
						{
							const FVector2D A = Scr[i - 1];
							const FVector2D Bp = Scr[i];
							const float L = FVector2D::Distance(A, Bp);
							if (L <= KINDA_SMALL_NUMBER)
							{
								continue;
							}
							const FVector2D Dn = (Bp - A) / L;
							const float SEnd = S + L;
							// 段內 on 區間：s ≡ MarchPx (mod Period) 起、長 DashPx
							float Base = MarchPx +
								FMath::FloorToFloat((S - MarchPx) / Period) * Period;
							for (; Base < SEnd; Base += Period)
							{
								const float On0 = FMath::Max(FMath::Max(Base, S), SkipPx);
								const float On1 = FMath::Min(Base + DashPx, SEnd);
								if (On1 <= On0)
								{
									continue;
								}
								// 遠端漸淡（近端 0.9 → 尾端 0.25）
								const float Fade = 1.0f - 0.72f *
									FMath::Clamp(On0 / FMath::Max(TotalPx, 1.0f), 0.0f, 1.0f);
								const FLinearColor C(0.95f, 0.95f, 0.95f, 0.9f * Fade);
								const FVector2D P0 = A + Dn * (On0 - S);
								const FVector2D P1 = A + Dn * (On1 - S);
								DrawLine(P0.X, P0.Y, P1.X, P1.Y, C, 2.0f * UiScale);
							}
							S = SEnd;
						}
					}
				}
			}

			// 自由游標十字（08-04 二輪修：游標=可放很遠的目標點；虛線盡頭=十字）
			if (!bStencilPen)
			{
				FVector CurW;
				if (MyChar->GetTattooCursorHudWorld(CurW))
				{
					const FVector Pr = Project(CurW);
					if (Pr.Z > 0.0f)
					{
						const float A = 6.0f * UiScale;
						const FLinearColor CurCol(0.85f, 0.85f, 0.95f, 0.95f);
						DrawLine(Pr.X - A, Pr.Y, Pr.X + A, Pr.Y, CurCol, 1.6f * UiScale);
						DrawLine(Pr.X, Pr.Y - A, Pr.X, Pr.Y + A, CurCol, 1.6f * UiScale);
					}
				}
			}

			// FP 2D 筆（07-22「要 2D 感」二改：user 定案「筆尖要對齊落筆點＋筆身像
			// 真人握筆右傾」）：貼圖以出針口為樞軸右傾 PEN_TILT、出針口錨在準星
			// 沿筆軸外推一小段——待機=留間隙+針樁（收）、按住 LMB=針線補滿間隙且
			// 機身微壓近（伸/壓筆感）。恆定大小角度=viewmodel 體感不變。
			if (PenSprite && MyChar->IsPenMachineMode() && !bStencilPen)
			{
				const bool bInking = MyChar->IsPenTriggerHeldLocal() && bReach;
				constexpr float PenTiltDeg = 30.0f;                    // 右傾（人握筆攻角）
				constexpr float MuzU = 0.4716f, MuzV = 0.9080f;        // 出針口在貼圖內的正規化座標
				const float SpriteW = Canvas->ClipY * 0.48f;           // 方形貼圖邊長
				const float TiltRad = FMath::DegreesToRadians(PenTiltDeg);
				// 筆軸方向（準星→機身）：右上
				const FVector2D AxisUp(FMath::Sin(TiltRad), -FMath::Cos(TiltRad));
				const float GapPx = (bInking ? 16.0f : 30.0f) * UiScale; // 壓筆=機身微壓近
				const FVector2D Muz = Aim + AxisUp * GapPx;
				const FLinearColor NeedleCol(0.78f, 0.80f, 0.84f, 1.0f); // 針鋼
				// 針線粗細隨針型（shader=粗針視覺；切針的即時回饋之一）
				const float NeedlePx = (bShaderNeedle ? 5.0f : 3.0f) * UiScale;
				if (bInking)
				{
					// 伸：針線補滿出針口→準星（線先畫、貼圖蓋線頭）
					DrawLine(Muz.X, Muz.Y, Aim.X, Aim.Y, NeedleCol, NeedlePx);
				}
				else
				{
					// 收：短樁指向準星（讀出「會往哪扎」）
					const FVector2D Stub = Muz - AxisUp * 12.0f * UiScale;
					DrawLine(Muz.X, Muz.Y, Stub.X, Stub.Y, NeedleCol, NeedlePx);
				}
				// 以出針口為樞軸旋轉（RotPivot=貼圖內正規化座標）
				DrawTexture(PenSprite, Muz.X - MuzU * SpriteW, Muz.Y - MuzV * SpriteW,
					SpriteW, SpriteW, 0.0f, 0.0f, 1.0f, 1.0f, FLinearColor::White,
					EBlendMode::BLEND_Translucent, 1.0f, false,
					PenTiltDeg, FVector2D(MuzU, MuzV));
				// 針型標籤已刪除（畫面清單 2026-09-02）：三支筆在針尖的視覺本來就
				// 完全不同（稿筆＝紫點＋2D 麥克筆／割線＝細針＋行進蟻／打霧＝粗針＋
				// 範圍圈），標籤是在標一件不會認錯的事，而它就掛在視線落點上。
			}
			return;
		}

		// 未鎖定：「湊上去」的提示**已移進右緣控制列**（09-03，`DrawControlStrip`）。
		// 此前它是畫在螢幕正中央下方的一整句 "RMB — lean in"，而同一畫面右緣已經有
		// F/G 的鍵帽列 ⇒ **同一類東西兩種形式、兩個位置**。句子裡的 "RMB" 不會被
		// 讀成一顆可以按的鍵（09-02 已經為 Q 學過同一課），所以留下的是鍵帽那份。

	}

	// 未鎖定的準星（2026-09-05 重整）。此前這裡除了十字還畫一枚 24px 的色塊
	// ——「目前選色」——而它活在**除了鎖定與沉睡以外的每一個相位**：大廳、入座、
	// 巡禮、指認、判決演出。實測 8/10 個相位的畫面正中央都掛著一個黑方塊，
	// 在判決那張截圖上它就壓在真兇的臉旁邊。
	// 兩條理由拆掉它：①UI_SYSTEM §3「0° 區只准放『墨會落在哪』」，而那些相位沒有墨
	// ②「手上是哪一杯」現在由墨杯 chip 講（一件事只講一次）。
	// **十字本身留著**（Meccha 局內也有一根細白十字）：它是第一人稱的視線錨點。
	// 顏色改成紙色而不是 `GetCurrentColor()`——09-03 已經學過一次「指示器不准用
	// 內容的顏色」：預設墨色近黑，畫在暗處等於沒有準星。
	const float CenterX = Canvas->ClipX * 0.5f;
	const float CenterY = Canvas->ClipY * 0.5f;
	const float Arm = 7.0f * UiScale;
	const float Thickness = 2.0f * UiScale;
	const float Off = FMath::Max(1.0f, UiScale);
	FLinearColor Sh = NiHudColor::Ink;
	Sh.A = 0.55f;
	DrawRect(Sh, CenterX - Arm + Off, CenterY - Thickness * 0.5f + Off, Arm * 2.0f, Thickness);
	DrawRect(Sh, CenterX - Thickness * 0.5f + Off, CenterY - Arm + Off, Thickness, Arm * 2.0f);
	DrawRect(NiHudColor::Paper, CenterX - Arm, CenterY - Thickness * 0.5f, Arm * 2.0f, Thickness);
	DrawRect(NiHudColor::Paper, CenterX - Thickness * 0.5f, CenterY - Arm, Thickness, Arm * 2.0f);
}

void ANiceInkHUD::DrawDebugPanel(const ANiceInkGameState* GS, const ANiceInkPlayerState* MyPS, ANiceInkCharacter* MyChar)
{
	if (CVarNiDebugHud.GetValueOnGameThread() == 0)
	{
		return;
	}
	const float M = 16.0f * UiScale;
	const float LineH = 22.0f * UiScale;
	DrawPanelBox(M, M, 470.0f * UiScale, LineH * 4.0f + 16.0f * UiScale, 0.6f);
	float Y = M + 8.0f * UiScale;
	DrawTok(TEXT("hud-v1  ·  ni.DebugHud 1"), M + 10.0f * UiScale, Y, ETextTier::Small, NiHudColor::Green, EHAlign::Left, true);
	Y += LineH;
	if (MyPS)
	{
		DrawTok(FString::Printf(TEXT("me: %s  seat %d  id %d"), *MyPS->GetPlayerName(), MyPS->SeatIndex, MyPS->GetPlayerId()),
			M + 10.0f * UiScale, Y, ETextTier::Small, NiHudColor::PaperDim, EHAlign::Left, false);
	}
	Y += LineH;
	if (GS)
	{
		DrawTok(FString::Printf(TEXT("phase %d  round %d  victim id %d"), static_cast<int32>(GS->CurrentPhase), GS->CurrentRound, GS->VictimPlayerId),
			M + 10.0f * UiScale, Y, ETextTier::Small, NiHudColor::PaperDim, EHAlign::Left, false);
	}
	Y += LineH;
	DrawTok(TEXT("console: NiStart | NiEmerge | NiAccuse <workNo> <seat>"),
		M + 10.0f * UiScale, Y, ETextTier::Small, NiHudColor::PaperDim, EHAlign::Left, false);

	// 朝向對賬（08-15 user 抓「醒來全員鏡像到對側」、PIE 重現不出）：本人 actor/控制器/
	// 相機 yaw＋每個他人的世界方位角（相對本人）——與 server 視窗同一行對照即定罪
	if (MyChar)
	{
		Y += LineH * 1.2f;
		const AController* Ctl = MyChar->GetController();
		const float CtlYaw = Ctl ? Ctl->GetControlRotation().Yaw : 999.0f;
		const float CamYaw = MyChar->FirstPersonCamera ? MyChar->FirstPersonCamera->GetComponentRotation().Yaw : 999.0f;
		FString Line = FString::Printf(TEXT("YAW me actor=%.0f ctrl=%.0f cam=%.0f asleep=%d eyes=%d |"),
			MyChar->GetActorRotation().Yaw, CtlYaw, CamYaw, MyChar->bAsleep ? 1 : 0, MyChar->bEyesOpen ? 1 : 0);
		for (TActorIterator<ANiceInkCharacter> It(GetWorld()); It; ++It)
		{
			ANiceInkCharacter* O = *It;
			if (O == MyChar || !O->GetPlayerState())
			{
				continue;
			}
			const FVector D = O->GetActorLocation() - MyChar->GetActorLocation();
			Line += FString::Printf(TEXT(" id%d brg=%.0f d=%.0f"),
				O->GetPlayerState()->GetPlayerId(), FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X)), D.Size2D());
		}
		DrawTok(Line, M + 10.0f * UiScale, Y, ETextTier::Small, NiHudColor::Green, EHAlign::Left, false);
	}

	// 描圖現場（v4.0）：受害者端即時 summary
	if (MyChar && MyChar->DreamTrace && MyChar->DreamTrace->IsTraceActive())
	{
		Y += LineH * 1.2f;
		DrawTok(MyChar->DreamTrace->GetDebugSummary(), M + 10.0f * UiScale, Y,
			ETextTier::Small, NiHudColor::Green, EHAlign::Left, false);
	}

	// 迷宮現場（出口卡死診斷 2026-07-15）：受害者端即時 summary，兩行折顯
	if (MyChar && MyChar->DreamMaze && MyChar->DreamMaze->IsMazeActive())
	{
		const FString Summary = MyChar->DreamMaze->GetDebugSummary();
		FString L1 = Summary;
		FString L2;
		Summary.Split(TEXT(" cpS="), &L1, &L2);
		Y += LineH * 1.2f;
		DrawTok(L1, M + 10.0f * UiScale, Y, ETextTier::Small, NiHudColor::Green, EHAlign::Left, false);
		Y += LineH;
		DrawTok(TEXT("cpS=") + L2, M + 10.0f * UiScale, Y, ETextTier::Small, NiHudColor::Green, EHAlign::Left, false);
	}
}

FString ANiceInkHUD::GetPhaseLabel(ENiceInkPhase Phase) const
{
	// 2026-09-04：九個相位名此前有八個是硬編英文（只有 Drawing 進了表）。
	// 相位名現在只出現在右下角的規則塊標題，上緣改放祈使句。
	switch (Phase)
	{
	case ENiceInkPhase::Lobby:      return NiLoc::T(this, ENiLocKey::PhaseLobby);
	case ENiceInkPhase::BottleSpin: return NiLoc::T(this, ENiLocKey::PhaseBottleSpin);
	case ENiceInkPhase::Seating:    return NiLoc::T(this, ENiLocKey::PhaseSeating);
	case ENiceInkPhase::Drawing:    return NiLoc::T(this, ENiLocKey::PhaseDrawing);
	case ENiceInkPhase::Tour:       return NiLoc::T(this, ENiLocKey::PhaseTour);
	case ENiceInkPhase::Accusation: return NiLoc::T(this, ENiLocKey::PhaseAccusation);
	case ENiceInkPhase::Resolution: return NiLoc::T(this, ENiLocKey::PhaseResolution);
	case ENiceInkPhase::Finale:     return NiLoc::T(this, ENiLocKey::PhaseFinale);
	case ENiceInkPhase::PostGame:   return NiLoc::T(this, ENiLocKey::PhasePostGame);
	default: return FString();
	}
}
