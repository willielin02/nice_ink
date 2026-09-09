#include "SNiHud.h"

#include "NiceInkHUD.h"
#include "NiceInkCharacter.h"
#include "NiceInkGameState.h"
#include "NiceInkLocText.h"
#include "NiceInkUiTokens.h"

#include "Engine/Font.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Rendering/DrawElements.h"
#include "NiceInkPlayerState.h"
#include "NiceInkGameMode.h"
#include "InkCanvasComponent.h"
#include "NiceInkGameInstance.h"
#include "DreamTraceComponent.h"
#include "DreamMazeComponent.h"
#include "InkTypes.h"
#include "NiceInkTypes.h"
#include "Widgets/Input/SButton.h"
#include "Styling/SlateTypes.h"
#include "Brushes/SlateColorBrush.h"

// ============================================================================
// 度量
//
// **不加 SDPIScaler**：viewport widget 本來就會吃引擎的 DPI 曲線（預設 1080p → 1.0），
// 那條曲線做的事就是 canvas 端的 UiScale＝ClipY/1080。再包一層 SDPIScaler 會乘兩次。
// 所以底下所有數字都直接寫 1080p 設計單位，與 NiUi／NiType 同一個座標系。
// ============================================================================
namespace NiSlate
{
	static TSharedRef<FSlateFontMeasure> Measure()
	{
		return FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	}

	FSlateFontInfo BodyFont(UFont* Font, int32 SizePx, bool bBold, int32 Tracking)
	{
		FSlateFontInfo Info(Font, SizePx, bBold ? FName("Bold") : FName("Regular"));
		Info.LetterSpacing = Tracking;
		return Info;
	}

	FSlateFontInfo DisplayFont(UFont* Font, int32 SizePx, bool bBlack, int32 Tracking)
	{
		FSlateFontInfo Info(Font, SizePx, bBlack ? FName("SerifBlack") : FName("SerifRegular"));
		Info.LetterSpacing = Tracking;
		return Info;
	}

	float DisplayCapHeight(const FSlateFontInfo& Info)
	{
		if (!FSlateApplication::IsInitialized())
		{
			return 0.0f;
		}
		// em 從 Slate 量到的行框反推（不用名目字級——名目值會再被 pt→px 乘 1.333）
		const float LineH = Measure()->GetMaxCharacterHeight(Info, 1.0f);
		return (LineH / DisplayLinePerEm) * DisplayCapPerEm;
	}

	/** 行框底 → 基線（正值）。全大寫的字，墨跡底就是基線 ⇒ 這段就是「行框底下的空氣」。 */
	static float DescentOf(const FSlateFontInfo& Info)
	{
		if (!FSlateApplication::IsInitialized())
		{
			return 0.0f;
		}
		return -Measure()->GetBaseline(Info, 1.0f);
	}

	/** 行框頂 → 大寫墨跡頂（展示體）。 */
	static float DisplayAscentAir(const FSlateFontInfo& Info)
	{
		if (!FSlateApplication::IsInitialized())
		{
			return 0.0f;
		}
		const float LineH = Measure()->GetMaxCharacterHeight(Info, 1.0f);
		return LineH - DescentOf(Info) - DisplayCapHeight(Info);
	}

	float InkGapPadding(const FSlateFontInfo& Above, const FSlateFontInfo& Below, float InkGap, bool bBelowIsDisplay)
	{
		const float AirBelowAbove = DescentOf(Above);
		const float AirAboveBelow = bBelowIsDisplay ? DisplayAscentAir(Below) : 0.0f;
		// **不准鉗到 0**：兩個行框之間天生的空氣常常已經大於我要的墨跡間距
		// （實測標籤 13 疊房碼 96：空氣 51px，而目標是 21）⇒ padding 必須能是負的。
		// 第一版鉗了，於是規格寫 0.20 cap、畫面量到 0.485 cap——鉗位把設計悄悄改掉了。
		return InkGap - AirBelowAbove - AirAboveBelow;
	}

	float InkGapPaddingToBox(const FSlateFontInfo& Above, float InkGap)
	{
		return InkGap - DescentOf(Above);
	}

	const FSlateBrush* Brush(UTexture* Tex, float Size)
	{
		// 快取鍵只有貼圖指標：同一張圖在不同尺寸共用一個 brush 是刻意的
		// （brush 的 ImageSize 只影響 desired size，實際大小由 SBox 決定）。
		static TMap<UTexture*, TSharedPtr<FSlateImageBrush>> Cache;
		static FSlateNoResource Empty;
		if (!Tex)
		{
			return &Empty;
		}
		if (TSharedPtr<FSlateImageBrush>* Found = Cache.Find(Tex))
		{
			return Found->Get();
		}
		TSharedPtr<FSlateImageBrush> B = MakeShared<FSlateImageBrush>(Tex, FVector2D(Size, Size));
		Cache.Add(Tex, B);
		return B.Get();
	}

	FString Elide(const FString& Text, const FSlateFontInfo& Font, float MaxWidth)
	{
		if (Text.IsEmpty() || MaxWidth <= 0.0f || !FSlateApplication::IsInitialized())
		{
			return Text;
		}
		const TSharedRef<FSlateFontMeasure> M = Measure();
		if (M->Measure(Text, Font, 1.0f).X <= MaxWidth)
		{
			return Text;
		}
		const FString Dots = TEXT("…");
		const float DotsW = M->Measure(Dots, Font, 1.0f).X;
		int32 Keep = Text.Len();
		while (Keep > 1 && M->Measure(Text.Left(Keep), Font, 1.0f).X + DotsW > MaxWidth)
		{
			--Keep;
		}
		return Text.Left(Keep) + Dots;
	}

	const FSlateBrush* FaceBrush(UTexture* Tex, float Size, bool bCrop)
	{
		if (!bCrop)
		{
			return Brush(Tex, Size);
		}
		static TMap<UTexture*, TSharedPtr<FSlateImageBrush>> CropCache;
		static FSlateNoResource Empty;
		if (!Tex)
		{
			return &Empty;
		}
		if (TSharedPtr<FSlateImageBrush>* Found = CropCache.Find(Tex))
		{
			return Found->Get();
		}
		TSharedPtr<FSlateImageBrush> B = MakeShared<FSlateImageBrush>(Tex, FVector2D(Size, Size));
		// 與 canvas 端 DrawFaceTok 同一組版面 UV：整張畫進框＝膚色方塊（08-06 鐵坑）
		B->SetUVRegion(FBox2f(FVector2f(0.30f, 0.22f), FVector2f(0.70f, 0.62f)));
		CropCache.Add(Tex, B);
		return B.Get();
	}
}

// ============================================================================
// 大廳：左下的房碼塊
//
// 2026-09-08 user 逐項指出的問題，這一版全部處理：
//   ① 房碼被插進去的空格拆成四個字母 ⇒ 改用字距（一個詞，但字母分得開）
//   ② 64 不在尺度表上（舊尺度殘留） ⇒ NiType::Hero，角色表裡指名給房號的那一階
//   ③ 小標是一句散文 ⇒ 換成短標籤，與全站的祈使短語文法一致
//   ④ 標籤黏在房碼上（實測墨跡間距 14px 對 88px 字高＝0.16） ⇒ 對字高取比例
//   ⑤ 鍵帽順序與右緣相反 ⇒ 統一成「動詞在左、鍵帽在右」的閱讀順序
// ============================================================================
class SNiLobbyCode : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiLobbyCode) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ANiceInkHUD>, Hud)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Hud = InArgs._Hud;
		UFont* F = Hud.IsValid() ? Hud->GetUiFont() : nullptr;

		LabelFont = NiSlate::BodyFont(F, NiType::Small, false, /*Tracking=*/150);
		CodeFont  = NiSlate::DisplayFont(F, NiType::Hero, true, NiSlate::CodeTracking);
		VerbFont  = NiSlate::BodyFont(F, NiType::Small, false);
		KeyFont   = NiSlate::BodyFont(F, NiType::Small, true);

		// 版面比例一律對**房碼的大寫字高**取，不對名目字級取
		const float Cap = NiSlate::DisplayCapHeight(CodeFont);
		const float KickerInk = 0.20f * Cap;   // 組內：標籤黏著它的值
		const float ActionInk = 0.45f * Cap;   // 組外：複製是另一件事

		KeycapBrush = MakeShared<FSlateRoundedBoxBrush>(NiHudColor::Paper, NiUi::Radius);

		ChildSlot
		[
			SNew(SVerticalBox)

			// 小標
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
			[
				SNew(STextBlock)
				.Font(LabelFont)
				.ColorAndOpacity(NiHudColor::PaperDim)
				.ShadowOffset(Shadow(1.0f)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
				.Text(this, &SNiLobbyCode::GetHintText)
			]

			// 房碼
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
			.Padding(0.0f, NiSlate::InkGapPadding(LabelFont, CodeFont, KickerInk, true), 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Font(CodeFont)
				.ColorAndOpacity(NiHudColor::Paper)
				.ShadowOffset(Shadow(2.0f)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
				.Text(this, &SNiLobbyCode::GetCodeText)
			]

			// 動作：COPY [C]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
			.Padding(0.0f, NiSlate::InkGapPaddingToBox(CodeFont, ActionInk), 0.0f, 0.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(VerbFont)
					.ColorAndOpacity(this, &SNiLobbyCode::GetVerbColor)
					.ShadowOffset(Shadow(1.0f)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
					.Text(this, &SNiLobbyCode::GetVerbText)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				.Padding(NiUi::GapM, 0.0f, 0.0f, 0.0f)
				[
					SNew(SBox).HeightOverride(NiUi::KeycapH).MinDesiredWidth(NiUi::KeycapH)
					[
						SNew(SBorder)
						.BorderImage(KeycapBrush.Get())
						.HAlign(HAlign_Center).VAlign(VAlign_Center)
						.Padding(FMargin(NiUi::GapM, 0.0f))
						[
							SNew(STextBlock)
							.Font(KeyFont)
							.ColorAndOpacity(NiHudColor::Ink)
							.Text(FText::FromString(TEXT("C")))
						]
					]
				]
			]
		];
	}

private:
	const ANiceInkGameState* GS() const
	{
		return (Hud.IsValid() && Hud->GetWorld()) ? Hud->GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
	}

	bool IsCopied() const
	{
		const ANiceInkCharacter* C = (Hud.IsValid() && Hud->PlayerOwner)
			? Cast<ANiceInkCharacter>(Hud->PlayerOwner->GetPawn()) : nullptr;
		return C && Hud->GetWorld() && Hud->GetWorld()->GetTimeSeconds() < C->LobbyCopiedUntil;
	}

	// 文字全部走繫結而不是一次性寫死——切語言時要跟著換（Slate 是保留模式，
	// 「漏綁」不會編譯失敗，只會畫面不更新，這是這條路最容易犯的第二個錯）
	FText GetHintText() const
	{
		return FText::FromString(NiLoc::T(Hud.Get(), ENiLocKey::LobbyCodeHint).ToUpper());
	}

	FText GetCodeText() const
	{
		const ANiceInkGameState* S = GS();
		// **不插空格**——字距已經在字體上（NiSlate::CodeTracking）
		return S ? FText::FromString(S->RoomCode.ToUpper()) : FText::GetEmpty();
	}

	FText GetVerbText() const
	{
		return FText::FromString(NiLoc::T(Hud.Get(), IsCopied() ? ENiLocKey::LobbyCopied : ENiLocKey::ActCopy).ToUpper());
	}

	FSlateColor GetVerbColor() const
	{
		return IsCopied() ? NiHudColor::PaperDim : NiHudColor::Paper;
	}

	TWeakObjectPtr<ANiceInkHUD> Hud;
	FSlateFontInfo LabelFont, CodeFont, VerbFont, KeyFont;
	static FVector2D Shadow(float Px) { return FVector2D(Px, Px); }
	TSharedPtr<FSlateRoundedBoxBrush> KeycapBrush;
};


// ============================================================================
// 共用零件
// ============================================================================

/**
 * 鍵帽（鍵盤有刻字 ⇒ 寫字）。
 *
 * **為什麼不用現成的按鍵圖示**（2026-09-08 調查）：能蓋的只有單一字母
 * （Tabler 有 `square-rounded-letter-a`～`z` 共 26 個），而我們的鍵包含
 * `ENTER`／`ESC`／`TAB`／`WASD` 這些字，還要跟著 13 語走 ⇒ 圖片式的按鍵提示在構造上蓋不完。
 * 所以鍵帽必須是「字排進一個形狀裡」，能改的是**那個形狀**。
 *
 * **形狀＝實體鍵**（user：「按鍵的圖樣也可以用更專業、明顯是鍵盤按鍵的圖案」）：
 * 外層是鍵的**側身**（暗），內層是鍵的**頂面**（亮），底邊留得比其他三邊厚
 * ⇒ 讀成「從斜上方看一顆鍵」。這是實體鍵最省的畫法，而且與尺寸無關、任何字都適用。
 * 可按＝白頂面＋黑字；不可按＝淡灰頂面＋淡白字（半透明，user 定案）。
 */
class SNiKeycap : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiKeycap) {}
		SLATE_ARGUMENT(FSlateFontInfo, Font)
		SLATE_ATTRIBUTE(FText, Key)
		SLATE_ATTRIBUTE(bool, Dim)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Dim = InArgs._Dim;
		const float R = NiUi::Radius;
		const float Skirt = 3.0f;      // 底邊的厚度＝鍵的側身
		const float Wall = 1.0f;

		// 側身（暗）／頂面（亮）——兩態各一組
		Side    = MakeShared<FSlateRoundedBoxBrush>(FLinearColor(0.06f, 0.06f, 0.07f, 0.55f), R + 1.0f);
		Face    = MakeShared<FSlateRoundedBoxBrush>(NiHudColor::Paper, R);
		SideDim = MakeShared<FSlateRoundedBoxBrush>(FLinearColor(0.06f, 0.06f, 0.07f, 0.28f), R + 1.0f);
		FaceDim = MakeShared<FSlateRoundedBoxBrush>(
			FLinearColor(FLinearColor::FromSRGBColor(FColor(150, 150, 152)).R,
				FLinearColor::FromSRGBColor(FColor(150, 150, 152)).G,
				FLinearColor::FromSRGBColor(FColor(150, 150, 152)).B, 0.42f), R);

		ChildSlot
		[
			SNew(SBox).HeightOverride(NiUi::KeycapH).MinDesiredWidth(NiUi::KeycapH)
			[
				SNew(SBorder)                                   // 側身
				.BorderImage(this, &SNiKeycap::SideBrush)
				.Padding(FMargin(Wall, Wall, Wall, Skirt))      // 底邊厚 ⇒ 看得出是一顆鍵
				[
					SNew(SBorder)                               // 頂面
					.BorderImage(this, &SNiKeycap::FaceBrush)
					.HAlign(HAlign_Center).VAlign(VAlign_Center)
					.Padding(FMargin(NiUi::GapM, 0.0f))
					[
						SNew(STextBlock)
						.Font(InArgs._Font)
						.ColorAndOpacity(this, &SNiKeycap::KeyInk)
						.Text(InArgs._Key)
					]
				]
			]
		];
	}

private:
	const FSlateBrush* SideBrush() const { return Dim.Get(false) ? SideDim.Get() : Side.Get(); }
	const FSlateBrush* FaceBrush() const { return Dim.Get(false) ? FaceDim.Get() : Face.Get(); }
	/** 可按＝黑字（頂面是白的）；不可按＝淡白字（頂面是淡灰的） */
	FSlateColor KeyInk() const
	{
		return Dim.Get(false) ? FSlateColor(FLinearColor(1, 1, 1, 0.55f)) : FSlateColor(NiHudColor::Ink);
	}
	TAttribute<bool> Dim;
	TSharedPtr<FSlateRoundedBoxBrush> Side, Face, SideDim, FaceDim;
};

/**
 * 滑鼠圖＝**Kenney Input Prompts 1.5 的線稿版**（2026-09-09，user：「滑鼠用專業圖示」）。
 * 烘焙在 `Tools/AssetPrep/kenney_mouse_icons.py`，產出 `T_Ico_mouse_{left,right,scroll}`。
 *
 * 09-06 退掉 Kenney、09-08 改用 Lucide 本體＋自繪高亮，兩次的理由都是同一句——
 * 「右緣同一句話裡就有 Lucide 的動詞圖示，換家會打架」。**動詞圖示已於 09-09 全數移除**
 * ⇒ 那條理由不存在了，滑鼠是全 UI 唯一的圖示，只需要跟鍵帽相處。
 * 選線稿版而不是實心版：實心版的「哪一顆鍵亮著」是紅色＝中性制不准的第二個強調色；
 * 線稿版用「線 vs 實填」講同一件事，單色就講得完。
 */
static TSharedRef<SWidget> MakeMouseGlyph(ANiceInkHUD* Hud, ANiceInkHUD::ENiInputGlyph Glyph, bool bDim)
{
	const float S = 7.0f * NiUi::U;   // 28：與 32 的鍵帽等重
	const TCHAR* Name = (Glyph == ANiceInkHUD::ENiInputGlyph::MouseRight) ? TEXT("mouse_right")
		: (Glyph == ANiceInkHUD::ENiInputGlyph::MouseWheel) ? TEXT("mouse_scroll") : TEXT("mouse_left");
	UTexture2D* Tex = Hud ? Hud->GetIcon(Name) : nullptr;
	return SNew(SBox).WidthOverride(S).HeightOverride(S)
		[
			SNew(SImage).Image(NiSlate::Brush(Tex, S))
			.ColorAndOpacity(FLinearColor(1, 1, 1, bDim ? 0.45f : 1.0f))
		];
}

/** 一句操作提示：右緣＝[圖示][動詞][鍵]；底部＝[鍵][圖示][動詞]（鍵一律貼著它的邊） */
static TSharedRef<SWidget> MakeHintRow(ANiceInkHUD* Hud, const ANiceInkHUD::FNiControlHint& H,
	const FSlateFontInfo& VerbFont, const FSlateFontInfo& KeyFont, const FString& VerbText, bool bKeyFirst)
{
	const bool bDim = (H.State == ANiceInkHUD::ENiKeyState::Unavailable);
	FLinearColor VerbC = NiHudColor::Paper;
	if (bDim) { VerbC.A = 0.45f; }

	TSharedRef<SWidget> KeyW = (H.Glyph != ANiceInkHUD::ENiInputGlyph::None)
		? MakeMouseGlyph(Hud, H.Glyph, bDim)
		: StaticCastSharedRef<SWidget>(SNew(SNiKeycap).Font(KeyFont).Dim(bDim)
			.Key(FText::FromString(H.Key ? FString(H.Key) : FString())));

	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	if (bKeyFirst)
	{
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center)[KeyW];
	}
	// **動詞前不再放圖示**（2026-09-09；user：「我知道他在畫什麼，但是我不知道和遊戲內
	// 要表達的東西之間有什麼關聯」）。量過才拆的：21 條提示每一條都有 glyph（按哪裡）
	// ＋動詞 13 語全非空（做什麼），沒有兩條動詞字面相同；而 `footprints` 曾同時代表
	// 「起身」與「起身・結束作畫」、`play` 同時代表「開局」與「下一場」
	// ⇒ **圖示在那兩組裡不帶區別力，區別全來自文字**。UI_SYSTEM §4.1 本來就寫著
	// 「動作圖示是選配，只在動詞說不清楚時加」。
	Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(NiUi::GapM, 0, 0, 0)
	[
		SNew(STextBlock).Font(VerbFont).ColorAndOpacity(VerbC)
		.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
		.Text(FText::FromString(VerbText))
	];
	if (!bKeyFirst)
	{
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(NiUi::GapM, 0, 0, 0)[KeyW];
	}
	return Row;
}

// ============================================================================
// 操作提示：右緣縱列＋底部橫排
//
// 兩個 widget 讀**同一份** BuildControlHints（「一件事只講一次」由共用來源在構造上保證），
// 差別只有 bPosture 這一欄與排列方向。清單會隨相位／狀態變 ⇒ 用簽章比對，只在變的時候重建
// （保留模式不能每幀重建整棵樹）。
// ============================================================================
class SNiHintList : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiHintList) : _bPosture(false) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ANiceInkHUD>, Hud)
		SLATE_ARGUMENT(bool, bPosture)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Hud = InArgs._Hud;
		bPosture = InArgs._bPosture;
		UFont* F = Hud.IsValid() ? Hud->GetUiFont() : nullptr;
		VerbFont = NiSlate::DisplayFont(F, NiType::Small, false);
		KeyFont = NiSlate::BodyFont(F, NiType::Text, true);
		// SBoxPanel 是抽象的（沒有 FArguments）⇒ 兩個方向各拿具體型別
		if (bPosture)
		{
			ChildSlot[SAssignNew(HBox, SHorizontalBox)];
		}
		else
		{
			ChildSlot[SAssignNew(VBox, SVerticalBox)];
		}
	}

	virtual void Tick(const FGeometry&, const double, const float) override
	{
		Rebuild();
	}

private:
	void Rebuild()
	{
		ANiceInkHUD* H = Hud.Get();
		ANiceInkCharacter* C = (H && H->PlayerOwner) ? Cast<ANiceInkCharacter>(H->PlayerOwner->GetPawn()) : nullptr;
		const ANiceInkGameState* GS = (H && H->GetWorld()) ? H->GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
		TArray<ANiceInkHUD::FNiControlHint> All;
		if (H && GS && C)
		{
			H->BuildControlHints(GS, C, All);
		}

		// 簽章：鍵＋動詞＋狀態。只有這些變了才重建 widget 樹。
		FString Sig;
		TArray<const ANiceInkHUD::FNiControlHint*> Rows;
		for (const ANiceInkHUD::FNiControlHint& Hint : All)
		{
			if (Hint.bPosture != bPosture) { continue; }
			Rows.Add(&Hint);
			Sig += FString::Printf(TEXT("%s|%d|%d|%d;"), Hint.Key ? Hint.Key : TEXT("m"),
				static_cast<int32>(Hint.Label), static_cast<int32>(Hint.State),
				static_cast<int32>(Hint.Glyph));
		}
		if (Sig == LastSig)
		{
			return;
		}
		LastSig = Sig;

		if (HBox.IsValid()) { HBox->ClearChildren(); }
		if (VBox.IsValid()) { VBox->ClearChildren(); }
		for (int32 i = 0; i < Rows.Num(); ++i)
		{
			const ANiceInkHUD::FNiControlHint& Hint = *Rows[i];
			FString VerbText = NiLoc::T(H, Hint.Label).ToUpper();
			// 賭注寫在決定點上：搖夢的價格貼在動詞後面
			if (Hint.Label == ENiLocKey::ActShake)
			{
				VerbText += FString::Printf(TEXT("  $%d"), GetDefault<ANiceInkGameMode>()->ShakeAttackCost);
			}
			TSharedRef<SWidget> Row = MakeHintRow(H, Hint, VerbFont, KeyFont, VerbText, /*bKeyFirst=*/bPosture);
			if (bPosture)
			{
				HBox->AddSlot().AutoWidth().VAlign(VAlign_Center)
					.Padding(i ? NiUi::GapL : 0.0f, 0, 0, 0)[Row];
			}
			else
			{
				// 右緣：一列一句、列距 44（＝鍵帽高 32 ＋ 12 的行間）；整列靠右
				VBox->AddSlot().AutoHeight().HAlign(HAlign_Right)
					.Padding(0, i ? (11.0f * NiUi::U - NiUi::KeycapH) : 0.0f, 0, 0)[Row];
			}
		}
	}

	TWeakObjectPtr<ANiceInkHUD> Hud;
	TSharedPtr<SVerticalBox> VBox;
	TSharedPtr<SHorizontalBox> HBox;
	FSlateFontInfo VerbFont, KeyFont;
	FString LastSig;
	bool bPosture = false;
};

/**
 * 一顆臉像。**兩種呈現，不是兩種尺寸**：
 *   ①頭像亭的肖像＝已裁好的頭形（透明背景）⇒ 直接畫，無框無底（user 定案「不是方形照片」）。
 *   ②墊檔（亭未就緒）＝**紙框＋膚色底板＋裁切後的臉**——這是 canvas 端本來就有的樣子。
 *     搬遷時我只把「裁切」搬過來、把框與底板漏掉 ⇒ 房主剛開房那十幾秒看到的是
 *     一顆光頭浮在空中（user 實拍指出）。**墊檔本來就該看起來像墊檔，不是像壞掉的頭像。**
 * 說話中（EOS RTC）外圈一道白環：語音是這個遊戲的第一頻道；LAN／PIE 恆無。
 */
class SNiFace : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiFace) : _Size(NiUi::FaceM) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ANiceInkHUD>, Hud)
		SLATE_ARGUMENT(TWeakObjectPtr<const ANiceInkPlayerState>, PS)
		SLATE_ARGUMENT(float, Size)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Hud = InArgs._Hud;
		PS = InArgs._PS;
		Size = InArgs._Size;

		// 說話環：外圈白、內圈挖暗（與 canvas 同構）
		const float B = 2.0f;
		Ring = MakeShared<FSlateRoundedBoxBrush>(FLinearColor(1, 1, 1, 0.95f), Size * 0.5f + B * 2.0f);
		Hole = MakeShared<FSlateRoundedBoxBrush>(FLinearColor(0, 0, 0, 0.35f), Size * 0.5f + B);

		ChildSlot
		[
			SNew(SBox).WidthOverride(Size).HeightOverride(Size)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SOverlay)
					.Visibility(this, &SNiFace::RingVis)
					+ SOverlay::Slot()[SNew(SImage).Image(Ring.Get())]
					+ SOverlay::Slot().Padding(B)[SNew(SImage).Image(Hole.Get())]
				]
				+ SOverlay::Slot()[SAssignNew(InnerSlot, SBox)]
			]
		];
		Rebuild();
	}

	/**
	 * **每幀重問一次來源**（2026-09-08 血價）：第一版只在建構時解析一次，而大廳那一列
	 * 只在名字／房主身分改變時才重建 ⇒ 房主獨自開房時簽章永遠不變 ⇒ 墊檔**永遠不會**
	 * 換成真的頭像（實測 30／50／75 秒都還是墊檔）。canvas 版是每幀重解析，所以亭一好就換。
	 * 這正是保留模式的頭號風險：**漏綁不會編譯失敗，只會畫面不更新。**
	 * 只有「來源換了」才重建 widget，所以每幀的成本是一次指標比較。
	 */
	virtual void Tick(const FGeometry&, const double, const float) override
	{
		Rebuild();
	}

private:
	void Rebuild()
	{
		ANiceInkHUD* H = Hud.Get();
		bool bCrop = false;
		UTexture* Tex = (H && PS.IsValid()) ? H->GetFaceSource(PS.Get(), bCrop) : nullptr;
		if (!InnerSlot.IsValid() || (Tex == LastTex && bCrop == bLastCrop && bBuilt))
		{
			return;
		}
		LastTex = Tex; bLastCrop = bCrop; bBuilt = true;

		const FSlateBrush* Face = NiSlate::FaceBrush(Tex, Size, bCrop);
		if (!bCrop)
		{
			// 亭的肖像＝已裁好的頭形：直接畫，無框無底
			InnerSlot->SetContent(SNew(SImage).Image(Face));
			return;
		}
		const float Pad = FMath::Max(1.5f, Size * 0.05f);
		Paper = MakeShared<FSlateRoundedBoxBrush>(FLinearColor(NiHudColor::Paper.R, NiHudColor::Paper.G,
			NiHudColor::Paper.B, 0.90f), Size * 0.18f);
		Skin = MakeShared<FSlateRoundedBoxBrush>(NiHudColor::Skin, Size * 0.14f);
		InnerSlot->SetContent(
			SNew(SBorder).BorderImage(Paper.Get()).Padding(Pad)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()[SNew(SImage).Image(Skin.Get())]
				+ SOverlay::Slot()[SNew(SImage).Image(Face)]
			]);
	}

	EVisibility RingVis() const
	{
		UNiceInkGameInstance* GI = UNiceInkGameInstance::Get(Hud.Get());
		return (GI && PS.IsValid() && GI->IsPlayerTalking(PS.Get()))
			? EVisibility::HitTestInvisible : EVisibility::Collapsed;
	}
	TWeakObjectPtr<ANiceInkHUD> Hud;
	TWeakObjectPtr<const ANiceInkPlayerState> PS;
	TSharedPtr<FSlateRoundedBoxBrush> Ring, Hole, Paper, Skin;
	TSharedPtr<SBox> InnerSlot;
	TWeakObjectPtr<UTexture> LastTex;
	float Size = NiUi::FaceM;
	bool bLastCrop = false;
	bool bBuilt = false;
};

// ============================================================================
// 上緣受害者：這一相位的賭注是誰（臉＋名字）
// ============================================================================
class SNiVictimTag : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiVictimTag) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ANiceInkHUD>, Hud)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Hud = InArgs._Hud;
		ANiceInkHUD* H = Hud.Get();
		NameFont = NiSlate::BodyFont(H ? H->GetUiFont() : nullptr, NiType::Small, false);
		const float FaceS = 6.0f * NiUi::U;   // 24
		ChildSlot
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SAssignNew(FaceSlot, SBox).WidthOverride(FaceS).HeightOverride(FaceS)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(NiUi::GapS, 0, 0, 0)
			[
				SNew(SBox).WidthOverride(60.0f * NiUi::U)
				[
					SNew(STextBlock)
					.Font(NameFont)
					.ColorAndOpacity(NiHudColor::Paper)
					.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
					.Text(this, &SNiVictimTag::GetName)
				]
			]
		];
	}

private:
	const ANiceInkPlayerState* Victim() const
	{
		ANiceInkHUD* H = Hud.Get();
		const ANiceInkGameState* S = (H && H->GetWorld()) ? H->GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
		return S ? Cast<ANiceInkPlayerState>(S->FindPlayerStateById(S->VictimPlayerId)) : nullptr;
	}
	FText GetName() const
	{
		const ANiceInkPlayerState* V = Victim();
		return V ? FText::FromString(NiSlate::Elide(V->GetPlayerName(), NameFont, 60.0f * NiUi::U))
			: FText::GetEmpty();
	}
	/** 受害者會換人 ⇒ 換人時重建那顆臉（保留模式不能靠繫結換 widget） */
	virtual void Tick(const FGeometry&, const double, const float) override
	{
		const ANiceInkPlayerState* V = Victim();
		if (V == LastVictim.Get() || !FaceSlot.IsValid())
		{
			return;
		}
		LastVictim = V;
		FaceSlot->SetContent(V
			? StaticCastSharedRef<SWidget>(SNew(SNiFace).Hud(Hud).PS(V).Size(6.0f * NiUi::U))
			: SNullWidget::NullWidget);
	}

	TWeakObjectPtr<ANiceInkHUD> Hud;
	TWeakObjectPtr<const ANiceInkPlayerState> LastVictim;
	TSharedPtr<SBox> FaceSlot;
	FSlateFontInfo NameFont;
};

// ============================================================================
// 上緣：一句祈使 ＋（巡禮）第幾幅 ＋（有賭注時）受害者 ＋ 右上現金
// ============================================================================
class SNiTopBar : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiTopBar) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ANiceInkHUD>, Hud)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Hud = InArgs._Hud;
		UFont* F = Hud.IsValid() ? Hud->GetUiFont() : nullptr;
		ImpFont = NiSlate::DisplayFont(F, NiType::Heading, false);
		SmallFont = NiSlate::DisplayFont(F, NiType::Small, false);

		ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(STextBlock).Font(ImpFont).ColorAndOpacity(NiHudColor::Paper)
				.ShadowOffset(FVector2D(2, 2)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
				.Text(this, &SNiTopBar::GetImperative)
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			.Padding(0, NiUi::GapM, 0, 0)
			[
				SNew(STextBlock).Font(SmallFont).ColorAndOpacity(NiHudColor::PaperDim)
				.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
				.Visibility(this, &SNiTopBar::TourVis)
				.Text(this, &SNiTopBar::GetTourPiece)
			]
			// 受害者＝這一相位的賭注（罰酒杯已搬去右下比分：一件事只講一次）
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			.Padding(0, NiUi::GapM, 0, 0)
			[
				SNew(SNiVictimTag).Hud(Hud)
				.Visibility(this, &SNiTopBar::VictimVis)
			]
		];
	}

private:
	const ANiceInkGameState* GS() const
	{
		return (Hud.IsValid() && Hud->GetWorld()) ? Hud->GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
	}

	FText GetImperative() const
	{
		ANiceInkHUD* H = Hud.Get();
		const ANiceInkGameState* S = GS();
		if (!H || !S) { return FText::GetEmpty(); }
		ANiceInkCharacter* C = H->PlayerOwner ? Cast<ANiceInkCharacter>(H->PlayerOwner->GetPawn()) : nullptr;
		const ANiceInkPlayerState* MyPS = H->PlayerOwner ? Cast<ANiceInkPlayerState>(H->PlayerOwner->PlayerState) : nullptr;
		const bool bIsVictim = MyPS && S->VictimPlayerId == MyPS->GetPlayerId();

		if (S->CurrentPhase == ENiceInkPhase::Lobby)
		{
			// 大廳這一面只回答「人到齊了沒」——人數屬於這一句
			const FString N = FString::FromInt(S->PlayerArray.Num());
			const FString Max = FString::FromInt(FMath::Clamp(S->MaxPlayers, 4, 6));
			const bool bHostHere = H->GetWorld() && H->GetWorld()->GetNetMode() != NM_Client;
			const int32 MinStart = (H->GetWorld() && H->GetWorld()->WorldType == EWorldType::PIE) ? 2 : 4;
			const bool bReady = bHostHere ? H->CanHostStart(S) : S->PlayerArray.Num() >= MinStart;
			return FText::FromString(NiLoc::TFmt(H,
				bReady ? ENiLocKey::LobbyStatusReady : ENiLocKey::LobbyStatusWaiting, N, Max).ToUpper());
		}
		return FText::FromString(H->GetImperative(S, C, bIsVictim).ToUpper());
	}

	EVisibility VictimVis() const
	{
		// 有賭注的三個相位才顯示；自己是受害者就不必了（他知道是自己）
		ANiceInkHUD* H = Hud.Get();
		const ANiceInkGameState* S = GS();
		const ANiceInkPlayerState* MyPS = (H && H->PlayerOwner) ? Cast<ANiceInkPlayerState>(H->PlayerOwner->PlayerState) : nullptr;
		if (!S || !MyPS || !S->FindPlayerStateById(S->VictimPlayerId)) { return EVisibility::Collapsed; }
		if (S->VictimPlayerId == MyPS->GetPlayerId()) { return EVisibility::Collapsed; }
		const bool bShow = S->CurrentPhase == ENiceInkPhase::Drawing
			|| S->CurrentPhase == ENiceInkPhase::Tour
			|| S->CurrentPhase == ENiceInkPhase::Accusation;
		return bShow ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
	}

	EVisibility TourVis() const
	{
		const ANiceInkGameState* S = GS();
		return (S && S->CurrentPhase == ENiceInkPhase::Tour) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
	}

	FText GetTourPiece() const
	{
		const ANiceInkGameState* S = GS();
		if (!S) { return FText::GetEmpty(); }
		return FText::FromString(NiLoc::TFmt(Hud.Get(), ENiLocKey::TourPiece,
			FString::FromInt(FMath::Max(1, S->TourWorkNumber)),
			FString::FromInt(FMath::Max(1, S->TourWorkCount))).ToUpper());
	}

	TWeakObjectPtr<ANiceInkHUD> Hud;
	FSlateFontInfo ImpFont, SmallFont;
};

// ============================================================================
// 右上：現金＋跳字
// ============================================================================
class SNiCash : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiCash) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ANiceInkHUD>, Hud)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Hud = InArgs._Hud;
		UFont* F = Hud.IsValid() ? Hud->GetUiFont() : nullptr;
		CashFont = NiSlate::BodyFont(F, NiType::Text, true);
		DeltaFont = NiSlate::DisplayFont(F, NiType::Text, false);

		ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock).Font(CashFont).ColorAndOpacity(NiHudColor::Paper)
					.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
					.Text(this, &SNiCash::GetCash)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(NiUi::GapS, 0, 0, 0)
				[
					SNew(SBox).WidthOverride(5.0f * NiUi::U).HeightOverride(5.0f * NiUi::U)
					[
						// 現金＝**穴あき銭**（自家記號；Tools/AssetPrep/nice_ink_marks.py）。
					// 此前是 Lucide 的西式紙鈔——這個遊戲的錢不長那樣。
					SNew(SImage)
						.Image(NiSlate::Brush(Hud.IsValid() ? Hud->GetCoinIcon() : nullptr, 5.0f * NiUi::U))
						.ColorAndOpacity(NiHudColor::PaperDim)
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0, NiUi::GapS, 0, 0)
			[
				SNew(STextBlock).Font(DeltaFont)
				.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
				.ColorAndOpacity(this, &SNiCash::GetDeltaColor)
				.Visibility(this, &SNiCash::DeltaVis)
				.Text(this, &SNiCash::GetDelta)
			]
		];
	}

	virtual void Tick(const FGeometry&, const double InTime, const float) override
	{
		const ANiceInkPlayerState* PS = MyPS();
		if (!PS) { return; }
		if (LastCash == INT32_MIN) { LastCash = PS->Cash; }
		else if (PS->Cash != LastCash)
		{
			Delta = PS->Cash - LastCash;
			DeltaAt = InTime;
			LastCash = PS->Cash;
		}
		Now = InTime;
	}

private:
	const ANiceInkPlayerState* MyPS() const
	{
		return (Hud.IsValid() && Hud->PlayerOwner) ? Cast<ANiceInkPlayerState>(Hud->PlayerOwner->PlayerState) : nullptr;
	}
	FText GetCash() const
	{
		const ANiceInkPlayerState* PS = MyPS();
		return PS ? FText::AsNumber(PS->Cash) : FText::GetEmpty();
	}
	float DeltaT() const { return (DeltaAt < 0.0) ? 2.0f : static_cast<float>((Now - DeltaAt) / 1.6); }
	EVisibility DeltaVis() const { return (Delta != 0 && DeltaT() < 1.0f) ? EVisibility::HitTestInvisible : EVisibility::Collapsed; }
	FText GetDelta() const
	{
		return FText::FromString((Delta > 0 ? TEXT("+") : TEXT("−")) + FText::AsNumber(FMath::Abs(Delta)).ToString());
	}
	FSlateColor GetDeltaColor() const
	{
		FLinearColor C = (Delta > 0) ? NiHudColor::Paper : NiHudColor::Red;
		const float T = DeltaT();
		C.A = FMath::Max(0.0f, 1.0f - T * T);
		return C;
	}

	TWeakObjectPtr<ANiceInkHUD> Hud;
	FSlateFontInfo CashFont, DeltaFont;
	int32 LastCash = INT32_MIN;
	int32 Delta = 0;
	double DeltaAt = -1.0, Now = 0.0;
};



// ============================================================================
// 臉片：肖像＋（房主）皇冠＋名字。大廳列與指認列共用同一個零件。
// 名字的截斷交給 Slate 的 OverflowPolicy——此前是自己一個字一個字量（FitTok）。
// ============================================================================
class SNiFaceChip : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiFaceChip) : _FaceSize(NiUi::FaceM), _NameBudget(0.0f) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ANiceInkHUD>, Hud)
		SLATE_ARGUMENT(TWeakObjectPtr<const ANiceInkPlayerState>, PS)
		SLATE_ARGUMENT(float, FaceSize)
		SLATE_ARGUMENT(float, NameBudget)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Hud = InArgs._Hud;
		PS = InArgs._PS;
		const float FaceSize = InArgs._FaceSize;
		const ANiceInkPlayerState* P = PS.Get();
		ANiceInkHUD* H = Hud.Get();
		const bool bHost = P && P->bIsRoomHost;
		UFont* F = H ? H->GetUiFont() : nullptr;
		const FSlateFontInfo NameFont = NiSlate::BodyFont(F, NiType::Small, bHost);


		// 房主＝**字**，不是歐式王冠（2026-09-09）。`LobbyHostTag` 13 語齊全
		// （房主／部屋主／host／방장…），而冠要先被認出來、再被翻譯一次。
		TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
		V->AddSlot().AutoHeight().HAlign(HAlign_Center)
		[
			SNew(STextBlock)
			.Visibility(bHost ? EVisibility::HitTestInvisible : EVisibility::Collapsed)
			.Font(NiSlate::BodyFont(F, NiType::Small, false))
			.ColorAndOpacity(NiHudColor::PaperDim)
			.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
			.Text(FText::FromString(NiLoc::T(H, ENiLocKey::LobbyHostTag)))
		];
		V->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, NiUi::GapS * 0.5f, 0, 0)
		[
			SNew(SNiFace).Hud(Hud).PS(P).Size(FaceSize)
		];
		V->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, NiUi::GapM, 0, 0)
		[
			// **固定寬＋裁到邊界**，不是 MaxDesiredWidth：後者只限制「想要多大」，
			// 文字照樣以自然寬度置中繪製 ⇒ 兩端都被切掉、看到的是字串中段（實拍：
			// 「Willie_desktop-54」讀成「e_desktop-54」）。省略號要有真的邊界才會發生。
			SNew(SBox)
			.WidthOverride(InArgs._NameBudget > 0.0f ? InArgs._NameBudget : FaceSize * 1.6f)
			[
				SNew(STextBlock)
				.Font(NameFont)
				.ColorAndOpacity(NiHudColor::Paper)
				.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
				.Justification(ETextJustify::Center)
				.Text(FText::FromString(NiSlate::Elide(P ? P->GetPlayerName() : FString(), NameFont,
					InArgs._NameBudget > 0.0f ? InArgs._NameBudget : FaceSize * 1.6f)))
			]
		];
		ChildSlot[V];
	}

private:
	TWeakObjectPtr<ANiceInkHUD> Hud;
	TWeakObjectPtr<const ANiceInkPlayerState> PS;
};

/** 大廳底部中央：在場的人（依席位；房主有冠）。名冊會變 ⇒ 簽章比對後重建。 */
class SNiPlayerRow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiPlayerRow) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ANiceInkHUD>, Hud)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Hud = InArgs._Hud;
		ChildSlot[SAssignNew(Row, SHorizontalBox)];
	}

	virtual void Tick(const FGeometry&, const double, const float) override
	{
		ANiceInkHUD* H = Hud.Get();
		const ANiceInkGameState* GS = (H && H->GetWorld()) ? H->GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
		if (!GS) { return; }

		TArray<const ANiceInkPlayerState*> Sorted;
		for (const APlayerState* P : GS->PlayerArray)
		{
			if (const ANiceInkPlayerState* N = Cast<ANiceInkPlayerState>(P)) { Sorted.Add(N); }
		}
		Sorted.Sort([](const ANiceInkPlayerState& A, const ANiceInkPlayerState& B) { return A.SeatIndex < B.SeatIndex; });

		FString Sig;
		for (const ANiceInkPlayerState* P : Sorted)
		{
			Sig += FString::Printf(TEXT("%d:%s:%d;"), P->SeatIndex, *P->GetPlayerName(), P->bIsRoomHost ? 1 : 0);
		}
		if (Sig == LastSig) { return; }
		LastSig = Sig;

		Row->ClearChildren();
		const float Pitch = 28.0f * NiUi::U;   // 112
		for (const ANiceInkPlayerState* P : Sorted)
		{
			Row->AddSlot().AutoWidth().VAlign(VAlign_Bottom)
			[
				SNew(SBox).WidthOverride(Pitch)
				[
					SNew(SNiFaceChip).Hud(Hud).PS(P).FaceSize(NiUi::FaceM).NameBudget(Pitch - NiUi::GapM)
				]
			];
		}
	}

private:
	TWeakObjectPtr<ANiceInkHUD> Hud;
	TSharedPtr<SHorizontalBox> Row;
	FString LastSig;
};

// ============================================================================
// 右下角：規則塊 ↔ 比分（分時共用同一格；判準同源＝受害者的杯數）
// 0 杯＝這一局還沒有賭注可講，正好是新玩家需要規則的時候；第一杯之後永久變成計分板。
// ============================================================================
class SNiCorner : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiCorner) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ANiceInkHUD>, Hud)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Hud = InArgs._Hud;
		ANiceInkHUD* H = Hud.Get();
		UFont* F = H ? H->GetUiFont() : nullptr;
		TitleFont = NiSlate::DisplayFont(F, NiType::Title.Size, false);   // Title 是 FRole 不是 int（表裡兩種形狀並存）
		LineFont = NiSlate::BodyFont(F, NiType::Small, false);
		LabelFont = NiSlate::DisplayFont(F, NiType::Small, false);

		const float CupS = 10.0f * NiUi::U;   // 40
		TSharedRef<SHorizontalBox> Cups = SNew(SHorizontalBox);
		for (int32 i = 0; i < 3; ++i)
		{
			// 猪口（自家記號）：**飲んだ杯＝満（口を塗る）／これからの杯＝空（線だけ）**。
			// 此前是同一顆西式高腳杯只換 tint ⇒ 滿與空只差亮度；現在差的是「量」
			// （口の面積），在 40px 上一眼可讀。tint 仍保留＝第二杯起空杯轉紅的懸崖警示。
			Cups->AddSlot().AutoWidth().Padding(i ? CupS * 0.18f : 0.0f, 0, 0, 0)
			[
				SNew(SBox).WidthOverride(CupS).HeightOverride(CupS)
				[
					SNew(SImage)
					.Image(TAttribute<const FSlateBrush*>::CreateLambda([this, i, CupS]()
						{
							ANiceInkHUD* Hd = Hud.Get();
							// `this->` 不可省：這個函式裡有一個同名的區域變數 `Cups`（SHorizontalBox）
							return NiSlate::Brush(Hd ? Hd->GetChokoIcon(i < this->Cups()) : nullptr, CupS);
						}))
					.ColorAndOpacity(TAttribute<FSlateColor>::CreateLambda(
						[this, i]() { return CupTint(i); }))
				]
			];
		}

		ChildSlot
		[
			SNew(SVerticalBox)
			// 規則：相位名（展示體 48）＋一句怎麼贏
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
			[
				SNew(SVerticalBox)
				.Visibility(this, &SNiCorner::RulesVis)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
				[
					SNew(STextBlock).Font(TitleFont).ColorAndOpacity(NiHudColor::Paper)
					.ShadowOffset(FVector2D(2, 2)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
					.Text(this, &SNiCorner::GetRuleTitle)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0, NiUi::GapS, 0, 0)
				[
					SNew(STextBlock).Font(LineFont).ColorAndOpacity(NiHudColor::Paper)
					.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
					.Text(this, &SNiCorner::GetRuleLine)
				]
			]
			// 比分：標籤＋三個杯子（杯子是形狀，形狀比數字直觀）
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
			[
				SNew(SVerticalBox)
				.Visibility(this, &SNiCorner::ScoreVis)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
				[
					SNew(STextBlock).Font(LabelFont).ColorAndOpacity(NiHudColor::Paper)
					.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
					.Text(this, &SNiCorner::GetScoreLabel)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0, NiUi::GapS, 0, 0)
				[
					Cups
				]
			]
		];
	}

private:
	const ANiceInkGameState* GS() const
	{
		return (Hud.IsValid() && Hud->GetWorld()) ? Hud->GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
	}
	ANiceInkCharacter* Me() const
	{
		return (Hud.IsValid() && Hud->PlayerOwner) ? Cast<ANiceInkCharacter>(Hud->PlayerOwner->GetPawn()) : nullptr;
	}
	int32 Cups() const { const ANiceInkGameState* S = GS(); return S ? S->GetVictimPenaltyCups() : 0; }

	/** 規則的三組文案由（角色狀態 × 相位）決定；沒有對應的組合就不顯示。 */
	bool ResolveRule(ENiLocKey& OutTitle, ENiLocKey& OutLine) const
	{
		const ANiceInkGameState* S = GS();
		ANiceInkCharacter* C = Me();
		if (!S) { return false; }
		if (C && C->bAsleep && C->bEyesOpen) { return false; }
		if (C && C->bAsleep && !C->bEyesOpen)
		{
			OutTitle = ENiLocKey::PhaseDream; OutLine = ENiLocKey::RuleDream1; return true;
		}
		if (S->CurrentPhase == ENiceInkPhase::Drawing)
		{
			OutTitle = ENiLocKey::PhaseDrawing; OutLine = ENiLocKey::RuleDraw1; return true;
		}
		if (S->CurrentPhase == ENiceInkPhase::Tour || S->CurrentPhase == ENiceInkPhase::Accusation)
		{
			OutTitle = (S->CurrentPhase == ENiceInkPhase::Tour) ? ENiLocKey::PhaseTour : ENiLocKey::PhaseAccusation;
			OutLine = ENiLocKey::RuleAccuse1; return true;
		}
		return false;
	}

	EVisibility RulesVis() const
	{
		ENiLocKey A, B;
		return (Cups() == 0 && ResolveRule(A, B)) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
	}
	EVisibility ScoreVis() const { return Cups() > 0 ? EVisibility::HitTestInvisible : EVisibility::Collapsed; }

	FText GetRuleTitle() const
	{
		ENiLocKey T, L;
		return ResolveRule(T, L) ? FText::FromString(NiLoc::T(Hud.Get(), T).ToUpper()) : FText::GetEmpty();
	}
	FText GetRuleLine() const
	{
		ENiLocKey T, L;
		return ResolveRule(T, L) ? FText::FromString(NiLoc::T(Hud.Get(), L)) : FText::GetEmpty();
	}
	FText GetScoreLabel() const
	{
		return FText::FromString(NiLoc::T(Hud.Get(), ENiLocKey::ScorePenalty).ToUpper());
	}
	FSlateColor CupTint(int32 Index) const
	{
		const int32 Filled = Cups();
		if (Index < Filled) { return NiHudColor::Paper; }
		// 第二杯起，空杯轉紅：懸崖警示全場一眼可讀
		FLinearColor T = (Filled >= 2) ? NiHudColor::Red : NiHudColor::Paper;
		// 0.28→0.45（2026-09-09）：空杯此前與滿杯是**同一張實心圖**，只靠亮度分辨，
		// 壓到 0.28 才不會跟滿杯搶。現在空杯是**線稿**（面積本身就少很多）
		// ⇒ 沿用 0.28 會讓「還剩幾杯」直接消失。**換了畫法就要重算它的權重**，
		// 與 09-03 暈的血價同型（線性換指數之後照抄峰值，暈就不見了）。
		T.A = 0.45f;
		return T;
	}

	TWeakObjectPtr<ANiceInkHUD> Hud;
	FSlateFontInfo TitleFont, LineFont, LabelFont;
};

// ============================================================================
// 底部狀態句
//
// canvas 端的 DrawBottomHint 是**指令式**的（各處直接呼叫、當幀畫出來），而 Slate 是
// 保留模式 ⇒ 改成 HUD 上一格「這一幀想說的話」，呼叫點不必改寫，widget 每幀讀它。
// ============================================================================
class SNiBottomHint : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiBottomHint) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ANiceInkHUD>, Hud)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Hud = InArgs._Hud;
		ANiceInkHUD* H = Hud.Get();
		ChildSlot
		[
			SNew(STextBlock)
			.Font(NiSlate::DisplayFont(H ? H->GetUiFont() : nullptr, NiType::Small, false))
			.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
			.ColorAndOpacity(this, &SNiBottomHint::GetColor)
			.Visibility(this, &SNiBottomHint::Vis)
			.Text(this, &SNiBottomHint::GetText)
		];
	}

private:
	EVisibility Vis() const
	{
		return (Hud.IsValid() && !Hud->BottomHintText.IsEmpty()) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
	}
	FText GetText() const
	{
		return Hud.IsValid() ? FText::FromString(Hud->BottomHintText.ToUpper()) : FText::GetEmpty();
	}
	FSlateColor GetColor() const
	{
		return Hud.IsValid() ? Hud->BottomHintColor : FSlateColor(NiHudColor::Paper);
	}
	TWeakObjectPtr<ANiceInkHUD> Hud;
};


// ============================================================================
// 指認（受害者本人）：第幾件 ＋ 賭注 ＋ 一排候選的臉（當前嫌疑人有底與底線）
// 「這件是誰畫的」本來就是指著一張臉 ⇒ 版面就是一排臉，不是一份名單。
// ============================================================================
class SNiAccuse : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiAccuse) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ANiceInkHUD>, Hud)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Hud = InArgs._Hud;
		ANiceInkHUD* H = Hud.Get();
		UFont* F = H ? H->GetUiFont() : nullptr;
		WorkFont = NiSlate::DisplayFont(F, NiType::Text, false);
		StakeFont = NiSlate::DisplayFont(F, NiType::Small, false);

		ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(STextBlock).Font(WorkFont).ColorAndOpacity(NiHudColor::Paper)
				.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
				.Text(this, &SNiAccuse::GetWorkText)
			]
			// 賭注寫在決定點上：按 ENTER 前要看得到「錯了會怎樣」
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, NiUi::GapM, 0, 0)
			[
				SNew(STextBlock).Font(StakeFont).ColorAndOpacity(NiHudColor::PaperDim)
				.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
				.Text(this, &SNiAccuse::GetStakeText)
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, NiUi::GapL, 0, 0)
			[
				SAssignNew(Row, SHorizontalBox)
			]
		];
	}

	virtual void Tick(const FGeometry&, const double, const float) override
	{
		ANiceInkHUD* H = Hud.Get();
		const ANiceInkGameState* GS = (H && H->GetWorld()) ? H->GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
		if (!GS) { return; }

		TArray<const ANiceInkPlayerState*> Cands;
		for (const APlayerState* P : GS->PlayerArray)
		{
			const ANiceInkPlayerState* N = Cast<ANiceInkPlayerState>(P);
			if (N && N->GetPlayerId() != GS->VictimPlayerId) { Cands.Add(N); }
		}
		Cands.Sort([](const ANiceInkPlayerState& A, const ANiceInkPlayerState& B) { return A.SeatIndex < B.SeatIndex; });

		FString Sig;
		for (const ANiceInkPlayerState* P : Cands) { Sig += FString::Printf(TEXT("%d;"), P->GetPlayerId()); }
		if (Sig == LastSig) { return; }
		LastSig = Sig;

		SelBrush = MakeShared<FSlateRoundedBoxBrush>(FLinearColor(0, 0, 0, NiUi::ModalDim), NiUi::Radius);
		Row->ClearChildren();
		const float FaceS = NiUi::FaceL;   // 96：認人的時刻
		for (int32 i = 0; i < Cands.Num(); ++i)
		{
			const ANiceInkPlayerState* P = Cands[i];
			Row->AddSlot().AutoWidth().VAlign(VAlign_Bottom).Padding(i ? NiUi::GapL : 0.0f, 0, 0, 0)
			[
				SNew(SOverlay)
				// 選中的底：強調只落在深色面上 ⇒ 那塊底就是為它鋪的
				+ SOverlay::Slot()
				[
					SNew(SBorder).BorderImage(SelBrush.Get())
					.Visibility(TAttribute<EVisibility>::CreateLambda([this, P]()
					{
						return IsSuspect(P) ? EVisibility::HitTestInvisible : EVisibility::Hidden;
					}))
				]
				+ SOverlay::Slot().Padding(NiUi::GapM)
				[
					SNew(SNiFaceChip).Hud(Hud).PS(P).FaceSize(FaceS).NameBudget(FaceS + NiUi::GapL)
				]
			];
		}
	}

private:
	bool IsSuspect(const ANiceInkPlayerState* P) const
	{
		ANiceInkCharacter* C = (Hud.IsValid() && Hud->PlayerOwner)
			? Cast<ANiceInkCharacter>(Hud->PlayerOwner->GetPawn()) : nullptr;
		return C && C->GetAccuseSuspect() == P;
	}
	const ANiceInkGameState* GS() const
	{
		return (Hud.IsValid() && Hud->GetWorld()) ? Hud->GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
	}
	FText GetWorkText() const
	{
		const ANiceInkGameState* S = GS();
		ANiceInkCharacter* C = (Hud.IsValid() && Hud->PlayerOwner)
			? Cast<ANiceInkCharacter>(Hud->PlayerOwner->GetPawn()) : nullptr;
		if (!S || !C) { return FText::GetEmpty(); }
		return FText::FromString(NiLoc::TFmt(Hud.Get(), ENiLocKey::TourPiece,
			FString::FromInt(C->AccusePickNumber), FString::FromInt(S->TourWorkCount)).ToUpper());
	}
	FText GetStakeText() const
	{
		const ANiceInkGameState* S = GS();
		if (!S) { return FText::GetEmpty(); }
		return FText::FromString(NiLoc::TFmt(Hud.Get(), ENiLocKey::AccuseStake,
			FString::FromInt(S->GetVictimPenaltyCups() + 1)).ToUpper());
	}

	TWeakObjectPtr<ANiceInkHUD> Hud;
	TSharedPtr<SHorizontalBox> Row;
	TSharedPtr<FSlateRoundedBoxBrush> SelBrush;
	FSlateFontInfo WorkFont, StakeFont;
	FString LastSig;
};

/** 場間大廳：身上現在有幾件碳黑、幾件永久。 */
class SNiPostGame : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiPostGame) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ANiceInkHUD>, Hud)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Hud = InArgs._Hud;
		ANiceInkHUD* H = Hud.Get();
		ChildSlot
		[
			SNew(STextBlock)
			.Font(NiSlate::DisplayFont(H ? H->GetUiFont() : nullptr, NiType::Text, false))
			.ColorAndOpacity(NiHudColor::Paper)
			.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
			.Text(this, &SNiPostGame::GetText)
		];
	}

private:
	FText GetText() const
	{
		ANiceInkCharacter* C = (Hud.IsValid() && Hud->PlayerOwner)
			? Cast<ANiceInkCharacter>(Hud->PlayerOwner->GetPawn()) : nullptr;
		if (!C || !C->InkCanvas) { return FText::GetEmpty(); }
		const int32 Carbon = C->InkCanvas->GetWorkIdsByState(EInkWorkState::Carbon).Num();
		const int32 Perm = C->InkCanvas->GetWorkIdsByState(EInkWorkState::Permanent).Num();
		return FText::FromString(NiLoc::TFmt(Hud.Get(), ENiLocKey::PostInk,
			FString::FromInt(Carbon), FString::FromInt(Perm)).ToUpper());
	}
	TWeakObjectPtr<ANiceInkHUD> Hud;
};


// ============================================================================
// ESC 系統選單
//
// 這是整個 HUD 唯一**要吃輸入**的一層（其餘全部 HitTestInvisible）。開啟時角色端已經
// 打開游標並切成 GameAndUI ⇒ Slate 的按鈕天生收得到點擊，不必再自己做命中判定
// （canvas 版是拿 GetMousePosition 每幀跟矩形比對，而且要自己處理 AR 鏡像的翻面）。
// ============================================================================
class SNiSystemMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiSystemMenu) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ANiceInkHUD>, Hud)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Hud = InArgs._Hud;
		ANiceInkHUD* H = Hud.Get();
		UFont* F = H ? H->GetUiFont() : nullptr;
		TitleFont = NiSlate::DisplayFont(F, NiType::Title.Size, true);
		BtnFont = NiSlate::DisplayFont(F, NiType::Action.Size, true);
		BodyFont = NiSlate::BodyFont(F, NiType::Text, false);
		SmallFont = NiSlate::DisplayFont(F, NiType::Small, false);
		LabelFont = NiSlate::BodyFont(F, NiType::Small, false);
		CodeFont = NiSlate::DisplayFont(F, NiType::Title.Size, true, NiSlate::CodeTracking);

		Dim = MakeShared<FSlateColorBrush>(FLinearColor(0, 0, 0, NiUi::ModalDim));
		MakeButtonStyles();

		ChildSlot
		[
			SNew(SBorder).BorderImage(Dim.Get()).Padding(0)
			[
				SNew(SOverlay)
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()[BuildRoot()]
					+ SOverlay::Slot()[BuildHowTo()]
					+ SOverlay::Slot()[BuildSettings()]
				]
				// 底部左下：任何一頁都有的 ESC
				+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(NiUi::Margin)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SNiKeycap).Font(NiSlate::BodyFont(F, NiType::Text, true))
						.Key(FText::FromString(TEXT("ESC")))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(NiUi::GapM, 0, 0, 0)
					[
						SNew(STextBlock).Font(SmallFont).ColorAndOpacity(NiHudColor::PaperDim)
						.Text(this, &SNiSystemMenu::GetEscVerb)
					]
				]
			]
		];
	}

	/** 關掉選單時回到第一頁（狀態機的出口要關乾淨） */
	void ResetPage() { Page = 0; }

private:
	// ---- 樣式 ----
	void MakeButtonStyles()
	{
		auto Fill = [](float A) { return FSlateRoundedBoxBrush(FLinearColor(1, 1, 1, A), NiUi::Radius); };
		Normal.SetNormal(Fill(0.10f)).SetHovered(Fill(0.24f)).SetPressed(Fill(0.30f))
			.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
		// 主鈕＝實心白＋黑字（無主色；hover 只動明度）
		Primary.SetNormal(Fill(1.0f)).SetHovered(Fill(0.86f)).SetPressed(Fill(0.80f))
			.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
	}

	TSharedRef<SWidget> MenuButton(ENiLocKey Label, bool bPrimary, TFunction<void()> OnClick)
	{
		return SNew(SBox).WidthOverride(80.0f * NiUi::U).HeightOverride(13.0f * NiUi::U)
		[
			SNew(SButton)
			.ButtonStyle(bPrimary ? &Primary : &Normal)
			.ContentPadding(FMargin(0))
			.HAlign(HAlign_Center).VAlign(VAlign_Center)
			.OnClicked(FOnClicked::CreateLambda([OnClick]() { OnClick(); return FReply::Handled(); }))
			[
				SNew(STextBlock).Font(BtnFont)
				.ColorAndOpacity(bPrimary ? NiHudColor::Ink : NiHudColor::Paper)
				.Text(NiText(Label))
			]
		];
	}

	FText NiText(ENiLocKey Key) const { return FText::FromString(NiLoc::T(Hud.Get(), Key).ToUpper()); }

	// ---- 三頁 ----
	TSharedRef<SWidget> BuildRoot()
	{
		TSharedRef<SVerticalBox> Left = SNew(SVerticalBox);
		Left->AddSlot().AutoHeight().HAlign(HAlign_Left)
		[
			SNew(STextBlock).Font(TitleFont).ColorAndOpacity(NiHudColor::Paper).Text(NiText(ENiLocKey::MenuTitle))
		];
		Left->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0, NiUi::GapL, 0, NiUi::GapL)
		[
			SNew(SBox).WidthOverride(24.0f * NiUi::U).HeightOverride(5.0f)
			[
				SNew(SImage).Image(NiSlate::Brush(Hud.IsValid() ? Hud->GetInkBrushTex() : nullptr, 5.0f))
				.ColorAndOpacity(NiHudColor::White70)
			]
		];
		struct FItem { ENiLocKey Key; bool bPrimary; int32 Action; };
		const FItem Items[] = {
			{ ENiLocKey::MenuResume,   true,  0 },
			{ ENiLocKey::MenuHowToPlay, false, 1 },
			{ ENiLocKey::MenuSettings,  false, 2 },
			{ ENiLocKey::MenuLeave,     false, 3 },
		};
		for (int32 i = 0; i < 4; ++i)
		{
			const int32 Action = Items[i].Action;
			Left->AddSlot().AutoHeight().HAlign(HAlign_Left)
				.Padding(0, i ? (Action == 3 ? NiUi::GapL : NiUi::GapM) : 0.0f, 0, 0)
			[
				MenuButton(Items[i].Key, Items[i].bPrimary, [this, Action]() { DoAction(Action); })
			];
		}

		return SNew(SHorizontalBox)
			.Visibility(TAttribute<EVisibility>::CreateLambda([this]()
				{ return Page == 0 ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; }))
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)[Left]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(16.0f * NiUi::U, 0, 0, 0)
			[
				SNew(SBox).WidthOverride(80.0f * NiUi::U)[BuildRightColumn()]
			];
	}

	TSharedRef<SWidget> BuildRightColumn()
	{
		TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
		// 房碼：與大廳同一組 kicker 規則（墨跡到墨跡）
		V->AddSlot().AutoHeight().HAlign(HAlign_Left)
		[
			SNew(STextBlock).Font(LabelFont).ColorAndOpacity(NiHudColor::PaperDim)
			.Visibility(this, &SNiSystemMenu::CodeVis)
			.Text(NiText(ENiLocKey::LobbyCodeHint))
		];
		V->AddSlot().AutoHeight().HAlign(HAlign_Left)
			.Padding(0, NiSlate::InkGapPadding(LabelFont, CodeFont, 0.20f * NiSlate::DisplayCapHeight(CodeFont), true), 0, 0)
		[
			SNew(STextBlock).Font(CodeFont).ColorAndOpacity(NiHudColor::Paper)
			.Visibility(this, &SNiSystemMenu::CodeVis)
			.Text(this, &SNiSystemMenu::GetCode)
		];
		V->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0, NiUi::GapL * 2.0f, 0, NiUi::GapM)
		[
			SNew(STextBlock).Font(LabelFont).ColorAndOpacity(NiHudColor::PaperDim).Text(NiText(ENiLocKey::MenuPlayers))
		];
		V->AddSlot().AutoHeight()[SAssignNew(PlayerList, SVerticalBox)];
		return V;
	}

	TSharedRef<SWidget> BuildHowTo()
	{
		TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
		V->AddSlot().AutoHeight().HAlign(HAlign_Left)
		[
			SNew(STextBlock).Font(TitleFont).ColorAndOpacity(NiHudColor::Paper).Text(NiText(ENiLocKey::MenuHowToPlay))
		];
		static const ENiLocKey Lines[5] = { ENiLocKey::HowTo1, ENiLocKey::HowTo2, ENiLocKey::HowTo3,
			ENiLocKey::HowTo4, ENiLocKey::HowTo5 };
		for (int32 i = 0; i < 5; ++i)
		{
			const ENiLocKey K = Lines[i];
			V->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0, i ? NiUi::GapL : NiUi::GapL * 2.0f, 0, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0, 6.0f, 0, 0)
				[
					SNew(SBox).WidthOverride(3.0f * NiUi::U).HeightOverride(3.0f * NiUi::U)
					[
						SNew(SImage).Image(NiSlate::Brush(Hud.IsValid() ? Hud->GetInkDotTex() : nullptr, 3.0f * NiUi::U))
						.ColorAndOpacity(NiHudColor::Paper)
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(NiUi::GapL, 0, 0, 0)
				[
					// 換行交給 Slate（此前是自己寫的 WrapTok）
					SNew(SBox).WidthOverride(160.0f * NiUi::U - 3.0f * NiUi::U - NiUi::GapL)
					[
						SNew(STextBlock).Font(BodyFont).ColorAndOpacity(NiHudColor::Paper)
						.AutoWrapText(true).Text(NiText2(K))
					]
				]
			];
		}
		V->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, NiUi::GapL * 2.0f, 0, 0)
		[
			MenuButton(ENiLocKey::MenuBack, false, [this]() { Page = 0; })
		];
		return SNew(SBox)
			.Visibility(TAttribute<EVisibility>::CreateLambda([this]()
				{ return Page == 1 ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; }))
			[V];
	}

	TSharedRef<SWidget> BuildSettings()
	{
		TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
		V->AddSlot().AutoHeight().HAlign(HAlign_Left)
		[
			SNew(STextBlock).Font(TitleFont).ColorAndOpacity(NiHudColor::Paper).Text(NiText(ENiLocKey::MenuSettings))
		];
		V->AddSlot().AutoHeight().Padding(0, NiUi::GapL * 2.0f, 0, 0)[AdjustRow(ENiLocKey::MenuSensitivity, 0)];
		V->AddSlot().AutoHeight().Padding(0, NiUi::GapL, 0, 0)[AdjustRow(ENiLocKey::MenuVolume, 1)];
		V->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, NiUi::GapL * 2.0f, 0, 0)
		[
			MenuButton(ENiLocKey::MenuBack, false, [this]() { Page = 0; })
		];
		return SNew(SBox)
			.Visibility(TAttribute<EVisibility>::CreateLambda([this]()
				{ return Page == 2 ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; }))
			[V];
	}

	/** 一列設定：標籤左、［−］值［＋］右 */
	TSharedRef<SWidget> AdjustRow(ENiLocKey Label, int32 Which)
	{
		auto Step = [this, Which](int32 Dir)
		{
			UNiceInkGameInstance* GI = UNiceInkGameInstance::Get(Hud.Get());
			if (!GI) { return; }
			if (Which == 0)
			{
				GI->MouseSensitivityScale = FMath::Clamp(GI->MouseSensitivityScale + Dir * 0.1f, 0.2f, 3.0f);
			}
			else
			{
				GI->MasterVolume = FMath::Clamp(GI->MasterVolume + Dir * 0.05f, 0.0f, 1.0f);
				GI->UpdateBgmVolume();
			}
			GI->SaveSettings();
		};
		auto Arrow = [this, Step](const TCHAR* Glyph, int32 Dir)
		{
			return SNew(SBox).WidthOverride(NiUi::KeycapH).HeightOverride(NiUi::KeycapH)
			[
				SNew(SButton).ButtonStyle(&Normal).ContentPadding(FMargin(0))
				.HAlign(HAlign_Center).VAlign(VAlign_Center)
				.OnClicked(FOnClicked::CreateLambda([Step, Dir]() { Step(Dir); return FReply::Handled(); }))
				[
					SNew(STextBlock).Font(BtnFont).ColorAndOpacity(NiHudColor::Paper).Text(FText::FromString(Glyph))
				]
			];
		};
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(60.0f * NiUi::U)
				[
					SNew(STextBlock).Font(SmallFont).ColorAndOpacity(NiHudColor::PaperDim).Text(NiText(Label))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Arrow(TEXT("−"), -1)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(NiUi::GapM, 0)
			[
				SNew(SBox).WidthOverride(16.0f * NiUi::U)
				[
					SNew(STextBlock).Font(BtnFont).ColorAndOpacity(NiHudColor::Paper)
					.Justification(ETextJustify::Center)
					.Text(TAttribute<FText>::CreateLambda([this, Which]() { return SettingValue(Which); }))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Arrow(TEXT("＋"), +1)];
	}

	FText SettingValue(int32 Which) const
	{
		UNiceInkGameInstance* GI = UNiceInkGameInstance::Get(Hud.Get());
		if (!GI) { return FText::GetEmpty(); }
		return FText::FromString(Which == 0
			? FString::Printf(TEXT("%.1f"), GI->MouseSensitivityScale)
			: FString::Printf(TEXT("%d%%"), FMath::RoundToInt(GI->MasterVolume * 100.0f)));
	}

	void DoAction(int32 Action)
	{
		ANiceInkHUD* H = Hud.Get();
		ANiceInkCharacter* C = (H && H->PlayerOwner) ? Cast<ANiceInkCharacter>(H->PlayerOwner->GetPawn()) : nullptr;
		switch (Action)
		{
		case 0: if (C) { C->SetSystemMenuOpen(false); } Page = 0; break;
		case 1: Page = 1; break;
		case 2: Page = 2; break;
		case 3: if (UNiceInkGameInstance* GI = UNiceInkGameInstance::Get(H)) { GI->ReturnToMainMenu(FString()); } break;
		default: break;
		}
	}

	// ---- 玩家列（房主有 KICK）----
	virtual void Tick(const FGeometry&, const double, const float) override
	{
		ANiceInkHUD* H = Hud.Get();
		const ANiceInkGameState* GS = (H && H->GetWorld()) ? H->GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
		if (!GS || !PlayerList.IsValid()) { return; }
		const bool bHost = H->GetWorld()->GetNetMode() != NM_Client;

		TArray<const ANiceInkPlayerState*> Sorted;
		for (APlayerState* P : GS->PlayerArray)
		{
			if (const ANiceInkPlayerState* N = Cast<ANiceInkPlayerState>(P)) { Sorted.Add(N); }
		}
		Sorted.Sort([](const ANiceInkPlayerState& A, const ANiceInkPlayerState& B) { return A.SeatIndex < B.SeatIndex; });

		FString Sig;
		for (const ANiceInkPlayerState* P : Sorted)
		{
			Sig += FString::Printf(TEXT("%d:%s:%d;"), P->SeatIndex, *P->GetPlayerName(), P->bIsRoomHost ? 1 : 0);
		}
		if (Sig == LastSig) { return; }
		LastSig = Sig;

		PlayerList->ClearChildren();
		const float FaceS = 10.0f * NiUi::U;   // 40
		for (const ANiceInkPlayerState* P : Sorted)
		{
			TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
			Row->AddSlot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SNiFace).Hud(Hud).PS(P).Size(FaceS)
			];
			if (P->bIsRoomHost)
			{
				Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(NiUi::GapM, 0, 0, 0)
				[
					SNew(STextBlock)
					.Font(NiSlate::BodyFont(H ? H->GetUiFont() : nullptr, NiType::Small, false))
					.ColorAndOpacity(NiHudColor::PaperDim)
					.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
					.Text(FText::FromString(NiLoc::T(H, ENiLocKey::LobbyHostTag)))
				];
			}
			Row->AddSlot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(NiUi::GapM, 0, 0, 0)
			[
				SNew(STextBlock).Font(BodyFont).ColorAndOpacity(NiHudColor::Paper)
				.Text(FText::FromString(NiSlate::Elide(P->GetPlayerName(), BodyFont, 44.0f * NiUi::U)))
			];
			if (bHost && !P->bIsRoomHost)
			{
				ANiceInkPlayerState* Target = const_cast<ANiceInkPlayerState*>(P);
				Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(NiUi::GapM, 0, 0, 0)
				[
					SNew(SBox).WidthOverride(21.0f * NiUi::U).HeightOverride(NiUi::KeycapH)
					[
						SNew(SButton).ButtonStyle(&Normal).ContentPadding(FMargin(0))
						.HAlign(HAlign_Center).VAlign(VAlign_Center)
						.OnClicked(FOnClicked::CreateLambda([this, Target]()
						{
							ANiceInkHUD* Hh = Hud.Get();
							if (Hh && Hh->GetWorld())
							{
								if (ANiceInkGameMode* GM = Hh->GetWorld()->GetAuthGameMode<ANiceInkGameMode>())
								{
									GM->HostKickPlayer(Target);
								}
							}
							return FReply::Handled();
						}))
						[
							SNew(STextBlock).Font(SmallFont).ColorAndOpacity(NiHudColor::Paper).Text(NiText(ENiLocKey::MenuKick))
						]
					]
				];
			}
			PlayerList->AddSlot().AutoHeight().Padding(0, NiUi::GapM, 0, 0)[Row];
		}
	}

	FText NiText2(ENiLocKey Key) const { return FText::FromString(NiLoc::T(Hud.Get(), Key)); }
	EVisibility CodeVis() const
	{
		const ANiceInkGameState* S = (Hud.IsValid() && Hud->GetWorld())
			? Hud->GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
		return (S && !S->RoomCode.IsEmpty()) ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
	}
	FText GetCode() const
	{
		const ANiceInkGameState* S = (Hud.IsValid() && Hud->GetWorld())
			? Hud->GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
		return S ? FText::FromString(S->RoomCode.ToUpper()) : FText::GetEmpty();
	}
	FText GetEscVerb() const
	{
		return FText::FromString(NiLoc::T(Hud.Get(),
			Page == 0 ? ENiLocKey::MenuResume : ENiLocKey::MenuBack).ToUpper());
	}

	TWeakObjectPtr<ANiceInkHUD> Hud;
	TSharedPtr<SVerticalBox> PlayerList;
	TSharedPtr<FSlateColorBrush> Dim;
	FButtonStyle Normal, Primary;
	FSlateFontInfo TitleFont, BtnFont, BodyFont, SmallFont, LabelFont, CodeFont;
	FString LastSig;
	int32 Page = 0;   // 0 Root／1 HowTo／2 Settings
};


// ============================================================================
// 判決／結局橫幅（下三分之一）
//
// 96 級的字永不落在畫面中央的人身上 ⇒ 一條帶住在下三分之一：印章（墨漬先到、字晚一拍）
// → 臉＋名字 → 後果一行。賭注永遠寫在結果旁邊。
// 動畫在 Slate 是 RenderTransform／顏色的繫結，不是每幀重算座標。
// ============================================================================
class SNiBanner : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiBanner) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ANiceInkHUD>, Hud)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Hud = InArgs._Hud;
		ANiceInkHUD* H = Hud.Get();
		UFont* F = H ? H->GetUiFont() : nullptr;
		WordFont = NiSlate::DisplayFont(F, NiType::Hero, true);
		NameFont = NiSlate::DisplayFont(F, NiType::Text, false);
		LineFont = NiSlate::BodyFont(F, NiType::Small, false);

		ChildSlot
		[
			SNew(SVerticalBox)
			// 印章：墨漬在字後面，縮放 1.06→1.0（落下感）
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(SOverlay)
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(96.0f * NiUi::U).HeightOverride(36.0f * NiUi::U)
					[
						SNew(SImage)
						.Image(NiSlate::Brush(H ? H->GetInkSplatTex() : nullptr, 96.0f * NiUi::U))
						.ColorAndOpacity(this, &SNiBanner::SplatTint)
					]
				]
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Font(WordFont)
					.ShadowOffset(FVector2D(2, 2)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
					.ColorAndOpacity(this, &SNiBanner::WordTint)
					.RenderTransformPivot(FVector2D(0.5f, 0.5f))
					.RenderTransform(this, &SNiBanner::StampTransform)
					.Text(this, &SNiBanner::GetWord)
				]
			]
			// 臉＋名字
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, NiUi::GapL, 0, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SAssignNew(FaceSlot, SBox).WidthOverride(NiUi::FaceM).HeightOverride(NiUi::FaceM)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(NiUi::GapM, 0, 0, 0)
				[
					SNew(STextBlock).Font(NameFont).ColorAndOpacity(NiHudColor::Paper)
					.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
					.Text(this, &SNiBanner::GetName)
				]
			]
			// 後果一行
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, NiUi::GapM, 0, 0)
			[
				SNew(STextBlock).Font(LineFont).ColorAndOpacity(NiHudColor::PaperDim)
				.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
				.Text(this, &SNiBanner::GetLine)
			]
		];
	}

private:
	const ANiceInkGameState* GS() const
	{
		return (Hud.IsValid() && Hud->GetWorld()) ? Hud->GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
	}
	bool IsResolution() const { const ANiceInkGameState* S = GS(); return S && S->CurrentPhase == ENiceInkPhase::Resolution; }
	bool IsCorrect() const
	{
		const ANiceInkGameState* S = GS();
		return S && S->LastAccusationResult == ENiceInkAccusationResult::Correct;
	}
	const APlayerState* Subject() const
	{
		const ANiceInkGameState* S = GS();
		if (!S) { return nullptr; }
		return S->FindPlayerStateById(IsResolution() ? S->RevealedAuthorId : S->LoserPlayerId);
	}
	double Since() const
	{
		ANiceInkHUD* H = Hud.Get();
		return (H && H->GetWorld() && H->GetPhaseChangedAt() >= 0.0)
			? (H->GetWorld()->GetTimeSeconds() - H->GetPhaseChangedAt()) : 99.0;
	}
	TOptional<FSlateRenderTransform> StampTransform() const
	{
		// 落下：1.06 → 1.0，用 NiUi::StampS 的時間常數
		const float T = FMath::Clamp(static_cast<float>(Since()) / NiUi::StampS, 0.0f, 1.0f);
		const float S = FMath::Lerp(1.06f, 1.0f, T);
		return TOptional<FSlateRenderTransform>(FSlateRenderTransform(FScale2D(S, S)));
	}
	FSlateColor WordTint() const
	{
		FLinearColor C = (IsResolution() && IsCorrect()) ? NiHudColor::Paper : NiHudColor::Red;
		C.A = FMath::Clamp(static_cast<float>(Since()) / NiUi::InkInS, 0.0f, 1.0f);
		return C;
	}
	FSlateColor SplatTint() const
	{
		// 墨漬先到、字晚一拍
		FLinearColor C = NiHudColor::Black;
		C.A = 0.55f * FMath::Clamp(static_cast<float>(Since()) / (NiUi::InkInS * 0.6f), 0.0f, 1.0f);
		return C;
	}
	FText GetWord() const
	{
		const bool bRes = IsResolution();
		const ENiLocKey K = bRes ? (IsCorrect() ? ENiLocKey::BannerCorrect : ENiLocKey::BannerWrong)
			: ENiLocKey::BannerOutCold;
		return FText::FromString(NiLoc::T(Hud.Get(), K).ToUpper());
	}
	FText GetName() const
	{
		const APlayerState* PS = Subject();
		return PS ? FText::FromString(PS->GetPlayerName()) : FText::GetEmpty();
	}
	FText GetLine() const
	{
		const ANiceInkGameState* S = GS();
		if (!S) { return FText::GetEmpty(); }
		if (IsResolution())
		{
			return FText::FromString(IsCorrect()
				? NiLoc::T(Hud.Get(), ENiLocKey::ResCorrect)
				: NiLoc::TFmt(Hud.Get(), ENiLocKey::ResWrongStake, FString::FromInt(S->GetVictimPenaltyCups())));
		}
		return FText::FromString(NiLoc::T(Hud.Get(), ENiLocKey::FinaleNote));
	}

	virtual void Tick(const FGeometry&, const double, const float) override
	{
		const ANiceInkPlayerState* P = Cast<const ANiceInkPlayerState>(Subject());
		if (P == LastSubject.Get() || !FaceSlot.IsValid())
		{
			return;
		}
		LastSubject = P;
		FaceSlot->SetContent(P
			? StaticCastSharedRef<SWidget>(SNew(SNiFace).Hud(Hud).PS(P).Size(NiUi::FaceM))
			: SNullWidget::NullWidget);
	}

	TWeakObjectPtr<ANiceInkHUD> Hud;
	TWeakObjectPtr<const ANiceInkPlayerState> LastSubject;
	TSharedPtr<SBox> FaceSlot;
	FSlateFontInfo WordFont, NameFont, LineFont;
};


// ============================================================================
// 醉夢（沉睡者本人的畫面）
//
// 這一面是全黑底上的幾件字：被搖／描失敗／進度／昏死。圖案本身（描圖盤）是幾何，
// 留在 canvas；字搬過來。**注意**：此處三個相位字串本來就在字串表裡，
// 而迷宮分支的兩句是硬編英文——那條路是 v4.0 封存的元件（永不啟動），一併留在 canvas。
// ============================================================================
class SNiDream : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiDream) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ANiceInkHUD>, Hud)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Hud = InArgs._Hud;
		ANiceInkHUD* H = Hud.Get();
		UFont* F = H ? H->GetUiFont() : nullptr;
		EventFont = NiSlate::DisplayFont(F, NiType::Heading, false);
		PctFont = NiSlate::DisplayFont(F, NiType::Text, false);
		OutFont = NiSlate::DisplayFont(F, NiType::Hero, true);
		NoteFont = NiSlate::DisplayFont(F, NiType::Text, false);

		ChildSlot
		[
			SNew(SOverlay)
			// 上緣事件行：被搖／滑出線外。線本身在抖就是訊息 ⇒ 一行 32 級，不遮線。
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(NiUi::Margin)
			[
				SNew(STextBlock).Font(EventFont)
				.ShadowOffset(FVector2D(2, 2)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
				.ColorAndOpacity(this, &SNiDream::EventColor)
				.Visibility(this, &SNiDream::EventVis)
				.Text(this, &SNiDream::EventText)
			]
			// 底部進度：線本身就是進度，數字只是一個小小的確認
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(NiUi::Margin)
			[
				SNew(STextBlock).Font(PctFont).ColorAndOpacity(NiHudColor::PaperDim)
				.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
				.Visibility(this, &SNiDream::TraceVis)
				.Text(this, &SNiDream::PctText)
			]
			// 終局昏死（server 不發夢）
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				.Visibility(this, &SNiDream::OutColdVis)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(STextBlock).Font(OutFont).ColorAndOpacity(NiHudColor::Red)
					.ShadowOffset(FVector2D(2, 2)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
					.Text(this, &SNiDream::OutColdText)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, NiUi::GapM, 0, 0)
				[
					SNew(STextBlock).Font(NoteFont).ColorAndOpacity(NiHudColor::PaperDim)
					.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
					.Text(this, &SNiDream::CashText)
				]
			]
		];
	}

private:
	ANiceInkCharacter* Me() const
	{
		return (Hud.IsValid() && Hud->PlayerOwner) ? Cast<ANiceInkCharacter>(Hud->PlayerOwner->GetPawn()) : nullptr;
	}
	UDreamTraceComponent* Trace() const { ANiceInkCharacter* C = Me(); return C ? C->DreamTrace : nullptr; }
	bool TraceActive() const { UDreamTraceComponent* T = Trace(); return T && T->IsTraceActive(); }

	EVisibility TraceVis() const { return TraceActive() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; }
	EVisibility OutColdVis() const
	{
		ANiceInkCharacter* C = Me();
		const UDreamMazeComponent* M = C ? C->DreamMaze : nullptr;
		const bool bMaze = M && M->IsMazeActive();
		return (!TraceActive() && !bMaze) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
	}
	EVisibility EventVis() const
	{
		UDreamTraceComponent* T = Trace();
		return (TraceActive() && (T->IsShakeActive() || T->IsFailFlashing()))
			? EVisibility::HitTestInvisible : EVisibility::Collapsed;
	}
	FSlateColor EventColor() const
	{
		UDreamTraceComponent* T = Trace();
		return (T && T->IsShakeActive()) ? FSlateColor(NiHudColor::Paper) : FSlateColor(NiHudColor::Red);
	}
	FText EventText() const
	{
		UDreamTraceComponent* T = Trace();
		if (!T) { return FText::GetEmpty(); }
		if (T->IsShakeActive())
		{
			return FText::FromString(NiLoc::TFmt(Hud.Get(), ENiLocKey::DreamShake,
				T->GetShakeAttackerName().ToUpper()).ToUpper());
		}
		return FText::FromString(NiLoc::T(Hud.Get(), ENiLocKey::DreamSlipped).ToUpper());
	}
	FText PctText() const
	{
		UDreamTraceComponent* T = Trace();
		return T ? FText::FromString(FString::Printf(TEXT("%d%%"),
			FMath::RoundToInt(T->GetProgress01() * 100.0f))) : FText::GetEmpty();
	}
	FText OutColdText() const { return FText::FromString(NiLoc::T(Hud.Get(), ENiLocKey::BannerOutCold).ToUpper()); }
	FText CashText() const { return FText::FromString(NiLoc::T(Hud.Get(), ENiLocKey::DreamCash).ToUpper()); }

	TWeakObjectPtr<ANiceInkHUD> Hud;
	FSlateFontInfo EventFont, PctFont, OutFont, NoteFont;
};


// ============================================================================
// 墨杯（自繪葉節點）
//
// 一枚墨杯＝**變化的地＋真半透明的墨**。在單一底色上不可能顯示透明度
// （`α·色+(1−α)·白` 與摻白逐像素相等）⇒ 地必須是變化的：2×2 粗棋盤、紙色／紙色陰影。
// 這是幾何＋著色，不是排版，但它**參與版面** ⇒ 做成有 desired size 的葉節點，
// 讓 Slate 把它跟旁邊的筆圖示與百分比排在一起（此前三者的座標是我自己算的）。
// ============================================================================
class SNiInkCup : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiInkCup) : _Size(7.0f * NiUi::U) {}
		SLATE_ARGUMENT(float, Size)
		SLATE_ATTRIBUTE(FLinearColor, Ink)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		// **不用自繪 OnPaint 的透明填色 rounded box**（2026-09-08 血價）：第一版墨杯與滑鼠圖
		// 都畫成純白方塊（實測 255,255,255，連 2×2 棋盤都沒有）。而同一支 brush 型別在
		// ESC 選單的鍵帽與按鈕（**實填**）完全正確 ⇒ 差別在「填色全透明、只留外框」那個用法。
		// 這裡改成只用實填 brush ＋ SImage 的 ColorAndOpacity——上色路徑沒有第二種解釋。
		// 白框改用「外白底 ＋ 內縮 1px」做，不靠 outline 參數。
		Solid = MakeShared<FSlateColorBrush>(FLinearColor::White);
		Frame = MakeShared<FSlateRoundedBoxBrush>(FLinearColor(1, 1, 1, 0.70f), NiUi::Radius);
		const float Sz = InArgs._Size;

		TSharedRef<SGridPanel> Checker = SNew(SGridPanel);
		const FLinearColor Cells[2][2] = {
			{ NiHudColor::Paper, NiHudColor::PaperShade },
			{ NiHudColor::PaperShade, NiHudColor::Paper } };
		for (int32 Y = 0; Y < 2; ++Y)
		{
			for (int32 X = 0; X < 2; ++X)
			{
				Checker->AddSlot(X, Y)
				[
					SNew(SBox).WidthOverride(Sz * 0.5f).HeightOverride(Sz * 0.5f)
					[
						SNew(SImage).Image(Solid.Get()).ColorAndOpacity(Cells[Y][X])
					]
				];
			}
		}

		ChildSlot
		[
			SNew(SBox).WidthOverride(Sz + 2.0f).HeightOverride(Sz + 2.0f)
			[
				// 外白底＋內縮 1px＝1px 白框（沒有邊的色塊讀成漏畫的方塊）
				SNew(SBorder).BorderImage(Frame.Get()).Padding(1.0f)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()[Checker]
					+ SOverlay::Slot()
					[
						// 真半透明的墨疊在會變化的地上——「看得到格子」本身就是透明的證據
						// SImage 吃的是 FSlateColor，包一層（FLinearColor 沒有隱式轉換到 TAttribute<FSlateColor>）
						SNew(SImage).Image(Solid.Get())
						.ColorAndOpacity(TAttribute<FSlateColor>::CreateLambda(
							[Ink = InArgs._Ink]() { return FSlateColor(Ink.Get(FLinearColor::Black)); }))
					]
				]
			]
		];
	}

private:
	TSharedPtr<FSlateColorBrush> Solid;
	TSharedPtr<FSlateRoundedBoxBrush> Frame;
};

/** 底部中央：手上裝的是哪一杯（筆的剪影＋墨杯＋濃度）——畫面上唯一一份。 */
class SNiInkChip : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiInkChip) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ANiceInkHUD>, Hud)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Hud = InArgs._Hud;
		ANiceInkHUD* H = Hud.Get();
		const float Cell = 7.0f * NiUi::U;   // 28
		const float IcoS = 5.0f * NiUi::U;   // 20

		ChildSlot
		[
			SNew(SHorizontalBox)
			// **筆的剪影已拆**（2026-09-09）：那顆 `pen` 是常數，三支筆畫的是同一支筆
			// ⇒ 零狀態資訊。「現在是哪支筆」本來就由針尖環的半徑與顏色講（09-03 定案）。
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SNiInkCup).Size(Cell).Ink(this, &SNiInkChip::InkColor)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(NiUi::GapM, 0, 0, 0)
			[
				SNew(STextBlock)
				.Font(NiSlate::BodyFont(H ? H->GetUiFont() : nullptr, NiType::Text, true))
				.ColorAndOpacity(NiHudColor::Paper)
				.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
				.Visibility(this, &SNiInkChip::PctVis)
				.Text(this, &SNiInkChip::PctText)
			]
		];
	}

private:
	ANiceInkCharacter* Me() const
	{
		return (Hud.IsValid() && Hud->PlayerOwner) ? Cast<ANiceInkCharacter>(Hud->PlayerOwner->GetPawn()) : nullptr;
	}
	bool IsShader() const { ANiceInkCharacter* C = Me(); return C && C->SelectedNeedle == EInkNeedle::Shader; }
	FLinearColor InkColor() const
	{
		ANiceInkCharacter* C = Me();
		if (!C) { return FLinearColor::Black; }
		const bool bStencil = (C->SelectedNeedle == EInkNeedle::Stencil);
		FLinearColor Base = bStencil ? NiceInkStencil::Color() : FNiceInkPalette::Get(C->SelectedColorIndex);
		// 非打霧＝恆實墨（濃度只有打霧那支筆擁有）
		Base.A = ANiceInkCharacter::ShaderTierAlphaFor(IsShader() ? C->ShaderTierIdx : 2) / 255.0f;
		return Base;
	}
	EVisibility PctVis() const { return IsShader() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; }
	FText PctText() const
	{
		ANiceInkCharacter* C = Me();
		if (!C) { return FText::GetEmpty(); }
		// 百分比＝**與托盤列標同一個來源**（TierLabel），不要自己從位元組除
		return FText::FromString(FNiInkTrayLayout::TierLabel(FNiInkTrayLayout::RowFromTier(C->ShaderTierIdx)));
	}
	TWeakObjectPtr<ANiceInkHUD> Hud;
};

/** 開發者遙測（ni.DebugHud 1）：內容由 HUD 算，這裡只負責把它排出來。 */
class SNiDebugPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiDebugPanel) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ANiceInkHUD>, Hud)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Hud = InArgs._Hud;
		ANiceInkHUD* H = Hud.Get();
		Font = NiSlate::BodyFont(H ? H->GetUiFont() : nullptr, NiType::Small, false);
		ChildSlot[SAssignNew(Box, SVerticalBox)];
		for (int32 i = 0; i < MaxLines; ++i)
		{
			Box->AddSlot().AutoHeight().HAlign(HAlign_Left)
			[
				SNew(STextBlock).Font(Font).ColorAndOpacity(NiHudColor::Green)
				.Visibility(TAttribute<EVisibility>::CreateLambda([this, i]()
					{ return Lines.IsValidIndex(i) ? EVisibility::HitTestInvisible : EVisibility::Collapsed; }))
				.Text(TAttribute<FText>::CreateLambda([this, i]()
					{ return Lines.IsValidIndex(i) ? FText::FromString(Lines[i]) : FText::GetEmpty(); }))
			];
		}
	}

	virtual void Tick(const FGeometry&, const double, const float) override
	{
		Lines.Reset();
		if (ANiceInkHUD* H = Hud.Get()) { H->BuildDebugLines(Lines); }
		if (Lines.Num() > MaxLines) { Lines.SetNum(MaxLines); }
	}

private:
	static const int32 MaxLines = 8;
	TWeakObjectPtr<ANiceInkHUD> Hud;
	TSharedPtr<SVerticalBox> Box;
	TArray<FString> Lines;
	FSlateFontInfo Font;
};


// ============================================================================
// 世界錨定標籤（碳黑刺青旁的雷射價目／永久標記）
//
// **位置**來自 3D 投影（不是版面），**內容**是兩行字（是版面）。所以位置每幀由投影
// 更新到 slot 的 offset，兩行的相對關係交給 SVerticalBox——此前連行距都是手算的。
// ============================================================================
class SNiWorldTags : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiWorldTags) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ANiceInkHUD>, Hud)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Hud = InArgs._Hud;
		ANiceInkHUD* H = Hud.Get();
		UFont* F = H ? H->GetUiFont() : nullptr;
		L1Font = NiSlate::DisplayFont(F, NiType::Text, false);
		L2Font = NiSlate::DisplayFont(F, NiType::Small, false);
		ChildSlot[SAssignNew(Canvas2, SConstraintCanvas)];
	}

	virtual void Tick(const FGeometry& Geo, const double, const float) override
	{
		ANiceInkHUD* H = Hud.Get();
		ANiceInkCharacter* C = (H && H->PlayerOwner) ? Cast<ANiceInkCharacter>(H->PlayerOwner->GetPawn()) : nullptr;
		if (!H || !C || !C->InkCanvas) { Clear(); return; }

		TArray<FEntry> Want;
		const int32 Cost = H->GetLaserCost();
		const TArray<int32> Carbon = C->InkCanvas->GetWorkIdsByState(EInkWorkState::Carbon);
		for (int32 i = 0; i < Carbon.Num(); ++i)
		{
			FInkWork Wk;
			const int32 Left = C->InkCanvas->GetWork(Carbon[i], Wk) ? FMath::Max(1, 3 - Wk.LaserLevel) : 3;
			Want.Add({ Carbon[i],
				NiLoc::TFmt(H, ENiLocKey::TagLaser, FText::AsNumber(Cost).ToString()).ToUpper(),
				NiLoc::TFmt(H, ENiLocKey::TagLeft, FString::FromInt(Left)).ToUpper(),
				i == 0 ? 1.0f : 0.55f });
		}
		for (int32 Id : C->InkCanvas->GetWorkIdsByState(EInkWorkState::Permanent))
		{
			Want.Add({ Id, NiLoc::T(H, ENiLocKey::TagPermanent).ToUpper(), FString(), 0.55f });
		}

		FString Sig;
		for (const FEntry& E : Want) { Sig += FString::Printf(TEXT("%d:%s:%s;"), E.WorkId, *E.L1, *E.L2); }
		if (Sig != LastSig)
		{
			LastSig = Sig;
			Rebuild(Want);
		}

		// 位置每幀跟著投影走（內容不變就不重建 widget）
		const float Scale = Geo.Scale > 0.0f ? Geo.Scale : 1.0f;
		for (int32 i = 0; i < Slots.Num() && i < Entries.Num(); ++i)
		{
			FBox2D Box;
			const bool bOk = H->GetWorkScreenBox(Entries[i].WorkId, Box);
			if (!bOk) { Widgets[i]->SetVisibility(EVisibility::Collapsed); continue; }
			Widgets[i]->SetVisibility(EVisibility::HitTestInvisible);
			const FVector2D Ctr = Box.GetCenter() / Scale;   // 投影是裝置像素，版面是設計單位
			const float Top = FMath::Min(Box.Min.Y / Scale, Ctr.Y - 60.0f) - 9.0f * NiUi::U;
			Slots[i]->SetOffset(FMargin(Ctr.X, Top, 0.0f, 0.0f));
		}
	}

private:
	struct FEntry { int32 WorkId; FString L1; FString L2; float Alpha; };

	void Clear()
	{
		if (Canvas2.IsValid() && Slots.Num()) { Canvas2->ClearChildren(); }
		Slots.Reset(); Widgets.Reset(); Entries.Reset(); LastSig.Reset();
	}

	void Rebuild(const TArray<FEntry>& Want)
	{
		Canvas2->ClearChildren();
		Slots.Reset(); Widgets.Reset();
		Entries = Want;
		for (const FEntry& E : Want)
		{
			FLinearColor A = NiHudColor::Paper;   A.A = E.Alpha;
			FLinearColor B = NiHudColor::PaperDim; B.A = E.Alpha;
			TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
			V->AddSlot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(STextBlock).Font(L1Font).ColorAndOpacity(A)
				.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
				.Text(FText::FromString(E.L1))
			];
			if (!E.L2.IsEmpty())
			{
				V->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, NiUi::GapS, 0, 0)
				[
					SNew(STextBlock).Font(L2Font).ColorAndOpacity(B)
					.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
					.Text(FText::FromString(E.L2))
				];
			}
			TSharedRef<SWidget> W = V;
			SConstraintCanvas::FSlot* NewSlot = nullptr;
			Canvas2->AddSlot()
				.AutoSize(true)
				.Alignment(FVector2D(0.5f, 1.0f))
				.Expose(NewSlot)
				[ W ];
			Slots.Add(NewSlot);
			Widgets.Add(W);
		}
	}

	TWeakObjectPtr<ANiceInkHUD> Hud;
	TSharedPtr<SConstraintCanvas> Canvas2;
	TArray<SConstraintCanvas::FSlot*> Slots;
	TArray<TSharedRef<SWidget>> Widgets;
	TArray<FEntry> Entries;
	FSlateFontInfo L1Font, L2Font;
	FString LastSig;
};

// ============================================================================
// 根節點
// ============================================================================
void SNiHudRoot::Construct(const FArguments& InArgs)
{
	Hud = InArgs._Hud;

	ChildSlot
	[
		SNew(SOverlay)

		// 大廳：左下房碼
		// 上緣：一句祈使（＋巡禮的第幾幅）
		+ SOverlay::Slot()
		.HAlign(HAlign_Center).VAlign(VAlign_Top)
		.Padding(NiUi::Margin)
		[
			SNew(SNiTopBar).Hud(Hud)
			.Visibility(TAttribute<EVisibility>::CreateLambda([this]() { return ChromeVis(); }))
		]

		// 右上：現金
		+ SOverlay::Slot()
		.HAlign(HAlign_Right).VAlign(VAlign_Top)
		.Padding(NiUi::Margin)
		[
			SNew(SNiCash).Hud(Hud)
			.Visibility(TAttribute<EVisibility>::CreateLambda([this]() { return CashVis(); }))
		]

		// 右緣：操作提示縱列（垂直置中）
		+ SOverlay::Slot()
		.HAlign(HAlign_Right).VAlign(VAlign_Center)
		.Padding(NiUi::Margin, 0.0f)
		[
			SNew(SNiHintList).Hud(Hud).bPosture(false)
			.Visibility(TAttribute<EVisibility>::CreateLambda([this]() { return ChromeVis(); }))
		]

		// 底部中央：姿勢與移動橫排（在墨杯 chip 之上）
		+ SOverlay::Slot()
		.HAlign(HAlign_Center).VAlign(VAlign_Bottom)
		.Padding(0.0f, 0.0f, 0.0f, NiUi::Margin + 7.0f * NiUi::U + NiUi::GapL)
		[
			SNew(SNiHintList).Hud(Hud).bPosture(true)
			.Visibility(TAttribute<EVisibility>::CreateLambda([this]() { return ChromeVis(); }))
		]

		// 世界錨定：碳黑刺青旁的雷射標籤
		+ SOverlay::Slot()
		[
			SNew(SNiWorldTags).Hud(Hud)
			.Visibility(TAttribute<EVisibility>::CreateLambda([this]() { return ChromeVis(); }))
		]

		// 底部中央：手上裝的是哪一杯
		+ SOverlay::Slot()
		.HAlign(HAlign_Center).VAlign(VAlign_Bottom)
		.Padding(NiUi::Margin)
		[
			SNew(SNiInkChip).Hud(Hud)
			.Visibility(TAttribute<EVisibility>::CreateLambda([this]() { return InkChipVis(); }))
		]

		// 開發者遙測
		+ SOverlay::Slot()
		.HAlign(HAlign_Left).VAlign(VAlign_Top)
		.Padding(NiUi::Margin + 10.0f, NiUi::Margin * 3.0f, 0.0f, 0.0f)
		[
			SNew(SNiDebugPanel).Hud(Hud)
		]

		// 醉夢（沉睡者本人）
		+ SOverlay::Slot()
		[
			SNew(SNiDream).Hud(Hud)
			.Visibility(TAttribute<EVisibility>::CreateLambda([this]() { return DreamVis(); }))
		]

		// 判決／結局：下三分之一的橫幅
		+ SOverlay::Slot()
		.HAlign(HAlign_Center).VAlign(VAlign_Center)
		.Padding(0.0f, 320.0f, 0.0f, 0.0f)
		[
			SNew(SNiBanner).Hud(Hud)
			.Visibility(TAttribute<EVisibility>::CreateLambda([this]() { return BannerVis(); }))
		]

		// 指認：底部中央一排候選的臉
		+ SOverlay::Slot()
		.HAlign(HAlign_Center).VAlign(VAlign_Bottom)
		.Padding(0.0f, 0.0f, 0.0f, 96.0f)
		[
			SNew(SNiAccuse).Hud(Hud)
			.Visibility(TAttribute<EVisibility>::CreateLambda([this]() { return AccuseVis(); }))
		]

		// 場間大廳：身上有幾件
		+ SOverlay::Slot()
		.HAlign(HAlign_Center).VAlign(VAlign_Bottom)
		.Padding(0.0f, 0.0f, 0.0f, 96.0f)
		[
			SNew(SNiPostGame).Hud(Hud)
			.Visibility(TAttribute<EVisibility>::CreateLambda([this]() { return PostGameVis(); }))
		]

		// 右下：規則塊 ↔ 比分（分時共用）
		+ SOverlay::Slot()
		.HAlign(HAlign_Right).VAlign(VAlign_Bottom)
		.Padding(NiUi::Margin)
		[
			SNew(SNiCorner).Hud(Hud)
			.Visibility(TAttribute<EVisibility>::CreateLambda([this]() { return ChromeVis(); }))
		]

		// 底部中央：狀態句（在墨杯 chip 之上一段）
		+ SOverlay::Slot()
		.HAlign(HAlign_Center).VAlign(VAlign_Bottom)
		.Padding(0.0f, 0.0f, 0.0f, 46.0f)
		[
			SNew(SNiBottomHint).Hud(Hud)
			.Visibility(TAttribute<EVisibility>::CreateLambda([this]() { return ChromeVis(); }))
		]

		// 大廳：底部中央的人列
		+ SOverlay::Slot()
		.HAlign(HAlign_Center).VAlign(VAlign_Bottom)
		.Padding(NiUi::Margin)
		[
			SNew(SNiPlayerRow).Hud(Hud)
			.Visibility(TAttribute<EVisibility>::CreateLambda([this]() { return LobbyVis(); }))
		]

		// 大廳：左下房碼
		+ SOverlay::Slot()
		.HAlign(HAlign_Left).VAlign(VAlign_Bottom)
		.Padding(NiUi::Margin)
		[
			SNew(SNiLobbyCode).Hud(Hud)
			.Visibility(TAttribute<EVisibility>::CreateLambda([this]()
			{
				const ANiceInkGameState* S = (Hud.IsValid() && Hud->GetWorld())
					? Hud->GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
				return (LobbyVis() == EVisibility::HitTestInvisible && S && !S->RoomCode.IsEmpty())
					? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			}))
		]

		// ESC 選單＝**唯一吃輸入的一層**（開啟時角色端已打開游標並切成 GameAndUI）
		+ SOverlay::Slot()
		[
			SAssignNew(SysMenu, SNiSystemMenu).Hud(Hud)
			.Visibility(TAttribute<EVisibility>::CreateLambda([this]() { return SysMenuVis(); }))
		]
	];

	// 整棵樹預設不吃輸入——這是這條路最容易犯的錯（會吃掉作畫的滑鼠與鍵盤）。
	// **唯一的例外是 ESC 選單**：它自己那一層在開著的時候是 Visible（要收點擊），
	// 所以根節點必須讓命中測試穿過去 ⇒ SelfHitTestInvisible（自己不吃、子節點自己決定）。
	SetVisibility(EVisibility::SelfHitTestInvisible);
}

EVisibility SNiHudRoot::ChromeVis() const
{
	// chrome 一律隱藏的三種局面（與 canvas 端 DrawHUD 的早退判準同源）：
	// 開場動畫、系統選單開著、加入時的臉同步等待畫面。
	ANiceInkHUD* H = Hud.Get();
	const ANiceInkGameState* S = (H && H->GetWorld()) ? H->GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
	ANiceInkCharacter* C = (H && H->PlayerOwner) ? Cast<ANiceInkCharacter>(H->PlayerOwner->GetPawn()) : nullptr;
	if (!H || !S) { return EVisibility::Collapsed; }
	if (S && NiCeremonyStepIsIntro(S->CeremonyStep)) { return EVisibility::Collapsed; }
	if (C && (C->IsSystemMenuOpen() || C->IsJoinFaceSyncPending())) { return EVisibility::Collapsed; }
	return EVisibility::HitTestInvisible;
}

EVisibility SNiHudRoot::CashVis() const
{
	// 大廳沒有錢的決定 ⇒ 右上留空；睜眼未現身的受害者也不看錢
	ANiceInkHUD* H = Hud.Get();
	const ANiceInkGameState* S = (H && H->GetWorld()) ? H->GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
	ANiceInkCharacter* C = (H && H->PlayerOwner) ? Cast<ANiceInkCharacter>(H->PlayerOwner->GetPawn()) : nullptr;
	if (ChromeVis() != EVisibility::HitTestInvisible) { return EVisibility::Collapsed; }
	if (!S || S->CurrentPhase == ENiceInkPhase::Lobby) { return EVisibility::Collapsed; }
	if (C && C->bAsleep && C->bEyesOpen) { return EVisibility::Collapsed; }
	return EVisibility::HitTestInvisible;
}

EVisibility SNiHudRoot::LobbyVis() const
{
	ANiceInkHUD* H = Hud.Get();
	const ANiceInkGameState* S = (H && H->GetWorld()) ? H->GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
	if (ChromeVis() != EVisibility::HitTestInvisible) { return EVisibility::Collapsed; }
	return (S && S->CurrentPhase == ENiceInkPhase::Lobby) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
}

EVisibility SNiHudRoot::AccuseVis() const
{
	// 指認畫面只給受害者本人（其他人這時在看他挑）
	ANiceInkHUD* H = Hud.Get();
	const ANiceInkGameState* S = (H && H->GetWorld()) ? H->GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
	const ANiceInkPlayerState* MyPS = (H && H->PlayerOwner) ? Cast<ANiceInkPlayerState>(H->PlayerOwner->PlayerState) : nullptr;
	if (ChromeVis() != EVisibility::HitTestInvisible || !S || !MyPS) { return EVisibility::Collapsed; }
	const bool bIsVictim = (S->VictimPlayerId == MyPS->GetPlayerId());
	return (S->CurrentPhase == ENiceInkPhase::Accusation && bIsVictim)
		? EVisibility::HitTestInvisible : EVisibility::Collapsed;
}

EVisibility SNiHudRoot::PostGameVis() const
{
	ANiceInkHUD* H = Hud.Get();
	const ANiceInkGameState* S = (H && H->GetWorld()) ? H->GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
	if (ChromeVis() != EVisibility::HitTestInvisible || !S) { return EVisibility::Collapsed; }
	return (S->CurrentPhase == ENiceInkPhase::PostGame) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
}

EVisibility SNiHudRoot::SysMenuVis() const
{
	ANiceInkHUD* H = Hud.Get();
	ANiceInkCharacter* C = (H && H->PlayerOwner) ? Cast<ANiceInkCharacter>(H->PlayerOwner->GetPawn()) : nullptr;
	const bool bOpen = C && C->IsSystemMenuOpen();
	if (!bOpen && SysMenu.IsValid())
	{
		SysMenu->ResetPage();   // 關掉就回第一頁（狀態機的出口要關乾淨）
	}
	return bOpen ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SNiHudRoot::BannerVis() const
{
	ANiceInkHUD* H = Hud.Get();
	const ANiceInkGameState* S = (H && H->GetWorld()) ? H->GetWorld()->GetGameState<ANiceInkGameState>() : nullptr;
	if (!S) { return EVisibility::Collapsed; }
	const bool bRes = S->CurrentPhase == ENiceInkPhase::Resolution;
	const bool bFin = S->CurrentPhase == ENiceInkPhase::Finale;   // PostGame 不冒用結局橫幅
	if (bFin && !S->FindPlayerStateById(S->LoserPlayerId)) { return EVisibility::Collapsed; }
	return (bRes || bFin) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
}

EVisibility SNiHudRoot::DreamVis() const
{
	// 沉睡且閉眼＝那個人在夢裡（與 canvas 端 DrawHUD 的分支同源）
	ANiceInkHUD* H = Hud.Get();
	ANiceInkCharacter* C = (H && H->PlayerOwner) ? Cast<ANiceInkCharacter>(H->PlayerOwner->GetPawn()) : nullptr;
	if (!C || !C->bAsleep || C->bEyesOpen) { return EVisibility::Collapsed; }
	if (C->IsSystemMenuOpen()) { return EVisibility::Collapsed; }
	return EVisibility::HitTestInvisible;
}

EVisibility SNiHudRoot::InkChipVis() const
{
	// 入鎖作畫時才有「手上這一杯」（與 canvas 端 DrawHUD 的分支同源）
	ANiceInkHUD* H = Hud.Get();
	ANiceInkCharacter* C = (H && H->PlayerOwner) ? Cast<ANiceInkCharacter>(H->PlayerOwner->GetPawn()) : nullptr;
	if (ChromeVis() != EVisibility::HitTestInvisible || !C) { return EVisibility::Collapsed; }
	return (C->bLeanLocked && !C->bInkTrayOpen) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
}
