#include "SNiHud.h"

#include "NiceInkHUD.h"
#include "NiceInkCharacter.h"
#include "NiceInkGameState.h"
#include "NiceInkLocText.h"
#include "NiceInkUiTokens.h"

#include "Engine/Font.h"
#include "Fonts/FontCache.h"
#include "Engine/GameViewportClient.h"
#include "Engine/UserInterfaceSettings.h"
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
#include "Widgets/Input/SSlider.h"
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

	float ViewportDpiScale(const UWorld* World)
	{
		const UGameViewportClient* GVC = World ? World->GetGameViewport() : nullptr;
		if (!GVC)
		{
			return 1.0f;
		}
		FVector2D Size(0, 0);
		GVC->GetViewportSize(Size);
		return GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(FIntPoint(FMath::RoundToInt(Size.X), FMath::RoundToInt(Size.Y)));
	}

	float KeycapTextLift(const FSlateFontInfo& Font, float DpiScale)
	{
		if (!FSlateApplication::IsInitialized())
		{
			return 0.0f;
		}
		const float S = FMath::Max(DpiScale, 0.01f);
		const float LineH   = Measure()->GetMaxCharacterHeight(Font, S);   // 行框高（Slate 實際排的）
		const float Descent = -Measure()->GetBaseline(Font, S);            // 行框底→基線
		const float CapH    = BodyCapPerEm * Font.Size * (96.0f / 72.0f) * S;
		// 墨跡中心（從行框頂）＝ LineH − Descent − CapH/2；行框中心＝ LineH/2 ⇒ 正值＝墨跡偏下了這麼多
		return (LineH * 0.5f - Descent - CapH * 0.5f) / S;
	}

	float KeycapWidth(UFont* Font, const FString& Key, FSlateFontInfo& InOutFont, float DpiScale)
	{
		const float H = NiUi::KeycapH;
		// **每一顆鍵同一個字體、同一個字級**（NiType::KeyLabel；三修，user：「字體大小有均一致嗎」）
		// ——單字母與 ENTER 都是。二修讓單鍵留在 Noto Sans Bold 13、長鍵換 Oswald 9，
		// 同一欄裡 F／G 又大又粗、ESC／WASD 又小又細＝兩套字，一眼就看得出來。
		// user 定案（2026-09-10）：**所有鍵名對齊改之前 F／G 的字高**＝Noto Sans Bold 13pt（大寫 13px）。
		// 長鍵不縮字，寬度跟著字走（所以四檔之外再開 2.0／2.25）。
		InOutFont = BodyFont(Font, NiType::KeyLabel, /*bBold=*/true);
		if (!FSlateApplication::IsInitialized())
		{
			return H;
		}
		// **直接讀渲染器的字形度量**（真實 DPI 縮放下的字型快取）：XAdvance＝渲染器實際的前進寬
		// （已含 hinting 的整數捨入，這正是 FSlateFontMeasure 量不到、害多字母每字漂 ~1.3px 的那一段）；
		// HorizontalOffset／USize＝墨跡的左緣與寬。留白要對**墨跡**算，user 量的就是墨跡到帽邊。
		const float Scale = FMath::Max(DpiScale, 0.01f);
		FCharacterList& Chars = FSlateApplication::Get().GetRenderer()->GetFontCache()->GetCharacterList(InOutFont, Scale);
		float Pen = 0.0f, InkL = TNumericLimits<float>::Max(), InkR = -TNumericLimits<float>::Max();
		for (const TCHAR C : Key)
		{
			const FCharacterEntry& E = Chars.GetCharacter(C, EFontFallback::FF_Max);
			if (!E.Valid) { continue; }
			InkL = FMath::Min(InkL, Pen + E.HorizontalOffset);
			InkR = FMath::Max(InkR, Pen + E.HorizontalOffset + E.USize);
			Pen += E.XAdvance;                       // 鍵名全大寫拉丁，忽略 kerning
		}
		if (InkR <= InkL)
		{
			return H;
		}
		const float InkW = (InkR - InkL) / Scale;    // 設計單位
		// **字一律置中**（七修）：六修把字靠左、用量到的墨跡左緣算左內距——量測比渲染窄，
		// 誤差全堆到右邊（ENTER 5/4）。置中讓誤差左右對分；帽寬再補渲染多出來的那一點。
		// **單鍵恆為正方形**：帽寬不准由字寬決定（否則 F 27／G 31＝同一種東西兩個尺寸）
		if (Key.Len() <= 1)
		{
			return H;
		}
		const float Slack = NiUi::KeycapRenderSlackPerChar * Key.Len();
		return FMath::Clamp(InkW + Slack + NiUi::KeycapPad * 2.0f, H, H * NiUi::KeycapMaxRatio);
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

	UTexture2D* LoadKeycapTex(const FString& Key)
	{
		if (!NiKeycapData::Find(Key))
		{
			return nullptr;
		}
		const FString Name = FString::Printf(TEXT("T_Key_%s"), *Key);
		return LoadObject<UTexture2D>(nullptr, *FString::Printf(TEXT("/Game/UI/Keys/%s.%s"), *Name, *Name));
	}

	const FSlateBrush* KeycapImageBrush(UTexture* Tex, const NiKeycapData::FEntry& E)
	{
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
		TSharedPtr<FSlateImageBrush> B = MakeShared<FSlateImageBrush>(Tex, FVector2D(E.BoxW, E.BoxH));
		// 貼圖右邊補到 2 的冪（引擎 NPOT 不生 mip）⇒ 只取帽那一段
		B->SetUVRegion(FBox2f(FVector2f(0.0f, 0.0f), FVector2f(E.U1, 1.0f)));
		Cache.Add(Tex, B);
		return B.Get();
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
 * **形狀＝實心白的 9-slice 貼圖 `T_UI_Keycap`／`T_UI_KeycapDim`**（2026-09-10 三版；
 * user：「有辦法把整個遊戲的按鍵指引都改成 Meccha／PEAK 同款嗎？」）。
 *
 * 三版都是被畫面推翻的：一版白面＋黑框（user：「黑底框很突兀」＝兩個顏色）→
 * 二版白線稿（與滑鼠同家，但 §15.12 實測九款出貨遊戲裡**沒有一款拿空心框當鍵**——
 * 空心框在那些遊戲裡是**道具格**的語彙）→ 三版實心白＋深色字＝Meccha 23px／PEAK 21px 的做法。
 *
 * **09-06 曾否決過白色實心塊**（原話：「看不太出來是在講按鍵」），這一版避開那次的三個成因：
 * 硬陰影改軟投影（貼紙感）／鍵高 32→24（§15.12：我們的鍵是動詞字高的 2.0 倍，參照 1.3~1.4）／
 * 旁邊的滑鼠現在是線稿，而「實心鍵＋線稿滑鼠」有出貨先例（RV There Yet 同一行就是這樣）。
 *
 * **十修（2026-09-11）：一顆鍵一張圖，字烘在裡面**（NiKeycapData；見 NiSlate::LoadKeycapTex）；
 * 9-slice＋Slate 排字只剩表外鍵名的保底。
 * **十一修：不可按＝同一張圖整顆 × NiUi::KeycapDimAlpha**（user 定案「在可按的樣子的基礎上調整透明度」）。
 * 09-10 的兩態各一張（空心灰框）退役；當時「乘 alpha 在障子牆只剩 19 階」的顧慮已向 user 報告，知情選擇。
 */
class SNiKeycap : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiKeycap) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ANiceInkHUD>, Hud)
		SLATE_ARGUMENT(FSlateFontInfo, Font)
		SLATE_ATTRIBUTE(FText, Key)
		SLATE_ATTRIBUTE(bool, Dim)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Dim = InArgs._Dim;
		ANiceInkHUD* H = InArgs._Hud.Get();
		const FString KeyStr = InArgs._Key.Get().ToString();

		// **十修：表裡有的鍵名＝一顆鍵一張圖，字烘在裡面**（見 NiSlate::LoadKeycapTex 的註解）。
		// 整顆帽是一張圖 ⇒ Slate 只對這一個盒子取一次整 ⇒ 字到四邊的留白對稱由構造保證。
		// **不可按＝同一張圖整顆乘透明度**（十一修，user 定案）——與同列動詞、滑鼠圖示的 0.45 同值。
		if (const NiKeycapData::FEntry* E = NiKeycapData::Find(KeyStr))
		{
			if (UTexture2D* Tex = H ? H->GetKeyTex(KeyStr) : nullptr)
			{
				KeyImg = NiSlate::KeycapImageBrush(Tex, *E);
				ChildSlot
				[
					SNew(SBox).WidthOverride(E->BoxW).HeightOverride(E->BoxH)
					[
						SNew(SImage).Image(KeyImg).ColorAndOpacity(this, &SNiKeycap::CapTint)
					]
				];
				return;
			}
		}

		// 保底：9-slice＋Slate 排字（表裡沒有的鍵名；留白在非整數縮放下 ±1px）；不可按同樣整顆乘透明度
		Cap = MakeCapBrush(H ? H->GetKeycapTex() : nullptr);

		// 尺寸規則走共用的那一支（單鍵恆方／長鍵上限 1.75×高、字級自動縮）——
		// 三個載體同一條規則，才不會又長出第二種鍵。
		FSlateFontInfo KeyFont = InArgs._Font;
		// **一律用設計單位（縮放 1.0）算，不讀視窗 DPI**（2026-09-11 九修）：user 的 bat 開 960×540
		// 再手動拉到 2560×1380——widget 在 0.5 倍時建立、把抬升量與帽寬照 0.5 倍算死，視窗放大後
		// 全錯；我自己測永遠一開始就是 2560×1380，所以永遠「量到是對的」。設計單位算好、
		// 由 Slate 等比縮放，才跟視窗什麼時候被拉大無關。
		const float Dpi = 1.0f;
		const float CapW = NiSlate::KeycapWidth(H ? H->GetUiFont() : nullptr,
			InArgs._Key.Get().ToString(), KeyFont, Dpi);
		// 字一律置中：帽寬已含留白，左右內距歸零（量測誤差由置中左右對分，見 KeycapWidth）。
		// 垂直：行框置中會讓大寫墨跡偏下 ⇒ 用底內距把它抬回墨跡置中（KeycapTextLift）
		const EHorizontalAlignment TextAlign = HAlign_Center;
		const float Lift = NiSlate::KeycapTextLift(KeyFont, Dpi);
		const FMargin TextPad(0.0f, FMath::Max(0.0f, -2.0f * Lift), 0.0f, FMath::Max(0.0f, 2.0f * Lift));

		// 單字母：WidthOverride(H)＝方格；多字母：不鎖寬，帽＝字的 desired size＋左右各 KeycapPad
		//（留白由排版引擎自己畫字的那把尺決定，見 KeycapWidth 的註解）
		TSharedRef<SBox> Box = SNew(SBox).HeightOverride(NiUi::KeycapH)
			[
				SNew(SBorder)
				.BorderImage(Cap.Get())
				.BorderBackgroundColor(this, &SNiKeycap::CapTint)
				.HAlign(TextAlign).VAlign(VAlign_Center)
				.Padding(TextPad)
				[
					SNew(STextBlock)
					.Font(KeyFont)
					.ColorAndOpacity(this, &SNiKeycap::KeyInk)
					// **鍵名不加陰影**：帽本身就是對比（白帽黑字／深帽白字）。09-06 那版
					// 白塊＋黑字＋**硬陰影**被打回成「貼紙」，陰影正是成因之一。
					.Text(InArgs._Key)
				]
			];
		if (CapW > 0.0f) { Box->SetWidthOverride(CapW); }
		ChildSlot[Box];
	}

private:
	/** 9-slice brush；貼圖缺席時退實心白圓角，整顆鍵不會消失 */
	static TSharedRef<FSlateBrush> MakeCapBrush(UTexture2D* Tex)
	{
		if (!Tex)
		{
			// 資產缺席的保底：實心白圓角（少了軟投影，但讀法一致）
			return MakeShared<FSlateRoundedBoxBrush>(NiHudColor::Paper, NiUi::KeycapR);
		}
		TSharedRef<FSlateBrush> B = MakeShared<FSlateBrush>();
		B->SetResourceObject(Tex);
		B->DrawAs = ESlateBrushDrawType::Box;
		B->Margin = FMargin(NiUi::KeycapSlice);
		B->ImageSize = FVector2f(NiUi::KeycapH, NiUi::KeycapH);
		return B;
	}
	/** 不可按＝整顆（帽＋字＋投影）× KeycapDimAlpha；可按＝原樣（十一修，user 定案） */
	FSlateColor CapTint() const
	{
		return FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, Dim.Get(false) ? NiUi::KeycapDimAlpha : 1.0f));
	}
	const FSlateBrush* KeyImg = nullptr;      // 一顆鍵一張（十修）；brush 由 NiSlate 的快取持有
	/** 保底路的字：可按＝Ink；不可按＝Ink × KeycapDimAlpha（與帽同一個倍率） */
	FSlateColor KeyInk() const
	{
		FLinearColor C = NiHudColor::Ink;
		C.A = Dim.Get(false) ? NiUi::KeycapDimAlpha : 1.0f;
		return FSlateColor(C);
	}
	TAttribute<bool> Dim;
	TSharedPtr<FSlateBrush> Cap;
};

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
					// 2026-09-09：這裡本來自己拼一顆平的鍵帽（實色 Paper 圓角方塊、無側身），
					// 於是大廳的 [C] 與右緣提示的鍵**長得不一樣**。改用同一個 widget ⇒
					// 立體鍵帽自動跟上，往後改形狀也只有一個地方要改。
					SNew(SNiKeycap).Hud(Hud).Font(KeyFont).Key(FText::FromString(TEXT("C")))
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
	// 與鍵帽**同高**（2026-09-10：鍵帽 32→24 時這裡沒跟著改，同一欄裡滑鼠 28、鍵 24＝縮了鍵沒縮鄰居）
	const float S = NiUi::KeycapH;
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
		: StaticCastSharedRef<SWidget>(SNew(SNiKeycap).Hud(Hud).Font(KeyFont).Dim(bDim)
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
		// 鍵名 Text→Small（2026-09-10）：鍵帽從 32 降到 24，18pt 的字在裡面只剩內距
		KeyFont = NiSlate::BodyFont(F, NiType::Small, true);
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
	SLATE_BEGIN_ARGS(SNiFaceChip) : _FaceSize(NiUi::FaceM), _NameBudget(0.0f), _NameSize(NiType::Small), _Frame(nullptr), _MarkHost(true) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ANiceInkHUD>, Hud)
		SLATE_ARGUMENT(TWeakObjectPtr<const ANiceInkPlayerState>, PS)
		SLATE_ARGUMENT(float, FaceSize)
		SLATE_ARGUMENT(float, NameBudget)
		/** 名字字級（NiType）；大廳席位格＝Caption（user：「字體可以小一點」），指認列照舊 Small */
		SLATE_ARGUMENT(int32, NameSize)
		/** 席位格的框（大廳）；nullptr＝不畫格（指認列）。brush 由呼叫端持有、必須比本 widget 長壽。 */
		SLATE_ARGUMENT(const FSlateBrush*, Frame)
		/** 要不要標房主（文字標＋粗體名）。大廳＝false（2026-09-11 user 定案：不標，靠固定順序最左＝房主）。 */
		SLATE_ARGUMENT(bool, MarkHost)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Hud = InArgs._Hud;
		PS = InArgs._PS;
		const float FaceSize = InArgs._FaceSize;
		const ANiceInkPlayerState* P = PS.Get();
		ANiceInkHUD* H = Hud.Get();
		const bool bHost = InArgs._MarkHost && P && P->bIsRoomHost;
		UFont* F = H ? H->GetUiFont() : nullptr;
		const FSlateFontInfo NameFont = NiSlate::BodyFont(F, InArgs._NameSize, bHost);


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
		TSharedRef<SWidget> Face = SNew(SNiFace).Hud(Hud).PS(P).Size(FaceSize);
		if (InArgs._Frame)
		{
			// 席位格（2026-09-11）：臉裝進 80 的格裡，格的框講狀態（空位／玩家／房主），見 NiUi::SeatFrame
			Face = SNew(SBorder).BorderImage(InArgs._Frame).Padding(NiUi::SeatPad)
				.HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(FaceSize).HeightOverride(FaceSize)[Face]
				];
		}
		V->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, NiUi::GapS * 0.5f, 0, 0)
		[
			Face
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
				// 截斷量測比渲染窄 ~0.5px/字（§15.13 六修），預算扣 4 讓名字真的落在盒子裡、不突出格邊
				.Text(FText::FromString(NiSlate::Elide(P ? P->GetPlayerName() : FString(), NameFont,
					(InArgs._NameBudget > 0.0f ? InArgs._NameBudget : FaceSize * 1.6f) - 4.0f)))
			]
		];
		ChildSlot[V];
	}

private:
	TWeakObjectPtr<ANiceInkHUD> Hud;
	TWeakObjectPtr<const ANiceInkPlayerState> PS;
};

/**
 * 大廳底部中央：**這一房有幾個位子就幾個格**（GameState.MaxPlayers，房主開房時定 4~6），
 * 依加入順序（SeatIndex）從左填入臉，**房主一律排最左**（排序時先房主再 SeatIndex，不靠「房主恰好是 0 號」）。
 * 2026-09-11 user 兩輪定案：先「最左框要不一樣」，看過實拍與九款大廠調查後改為
 * 「**不要標，所有人的框一模一樣，固定順序，大家就知道最左邊是房主**」——與 PEAK／Lethal Company／
 * Content Warning／Liar's Bar 同一派（合作派對類不標房主；房主＝能按 START 的人）。
 * 格＝黑 25% 圓角底、**無外框**（三修，user：「格子不需要框框，無論框框內是否有人」）；有人沒人只差臉。
 * 房主文字標與粗體名在大廳關掉（MarkHost=false）。
 * 這正是 §11 表裡「底部一排臉（空位＝黑 25% 淡框）」——09-08 搬 Slate 時空位格漏搬了，這次補回。
 * 名冊或人數會變 ⇒ 簽章比對後重建。
 */
class SNiPlayerRow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiPlayerRow) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ANiceInkHUD>, Hud)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Hud = InArgs._Hud;
		// brush 要比 widget 長壽 ⇒ 成員持有（Slate brush 不保 GC 的問題這裡沒有：純色，無貼圖）
		// 三修（2026-09-11 user：「下面的格子不需要框框，無論框框內是否有人」）：格＝黑 25% 的圓角底，**沒有外框**。
		// 有人／沒人只差臉在不在；容量由六塊底講。
		FLinearColor Ground = NiHudColor::Black; Ground.A = 0.25f;
		FrameEmpty  = MakeShared<FSlateRoundedBoxBrush>(Ground, NiUi::Radius);
		FramePlayer = FrameEmpty;
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
		// 房主永遠最左（這是唯一講「誰是房主」的訊號），其後依加入順序
		Sorted.Sort([](const ANiceInkPlayerState& A, const ANiceInkPlayerState& B)
		{
			if (A.bIsRoomHost != B.bIsRoomHost) { return A.bIsRoomHost; }
			return A.SeatIndex < B.SeatIndex;
		});

		// 格數＝這一房的位子數；名冊比位子多（不該發生）時仍把人全畫出來
		const int32 Seats = FMath::Max(GS->MaxPlayers, Sorted.Num());

		FString Sig = FString::Printf(TEXT("seats=%d;"), Seats);
		for (const ANiceInkPlayerState* P : Sorted)
		{
			Sig += FString::Printf(TEXT("%d:%s:%d;"), P->SeatIndex, *P->GetPlayerName(), P->bIsRoomHost ? 1 : 0);
		}
		if (Sig == LastSig) { return; }
		LastSig = Sig;

		UFont* F = H->GetUiFont();
		// 名字＝Caption（四修，user：「字體可以小一點」），預算＝格寬 SeatFrame（五修起 96）：名字不得比它頭上的格寬
		//（此前 Small 13＋預算 104：Hanamichi 84 > 格 80，左右各突出 2px，user 讀成「左側被切到」）
		const FSlateFontInfo NameFont = NiSlate::BodyFont(F, NiType::Caption, false);
		Row->ClearChildren();
		const float Pitch = NiUi::SeatPitch;
		for (int32 i = 0; i < Seats; ++i)
		{
			const ANiceInkPlayerState* P = (i < Sorted.Num()) ? Sorted[i] : nullptr;
			TSharedRef<SWidget> Cell = SNullWidget::NullWidget;
			if (P)
			{
				Cell = SNew(SNiFaceChip).Hud(Hud).PS(P).FaceSize(NiUi::FaceSeat).NameBudget(NiUi::SeatFrame)
					.NameSize(NiType::Caption).Frame(FramePlayer.Get()).MarkHost(false);
			}
			else
			{
				// 空位＝同尺寸的格＋一行空名字（撐住高度，讓一排格的底邊對齊），框 25%
				Cell = SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, NiUi::GapS * 0.5f, 0, 0)
					[
						SNew(SBorder).BorderImage(FrameEmpty.Get()).Padding(NiUi::SeatPad)
						[
							SNew(SBox).WidthOverride(NiUi::FaceSeat).HeightOverride(NiUi::FaceSeat)
						]
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, NiUi::GapM, 0, 0)
					[
						SNew(STextBlock).Font(NameFont).Text(FText::FromString(TEXT(" ")))
					];
			}
			Row->AddSlot().AutoWidth().VAlign(VAlign_Bottom)
			[
				SNew(SBox).WidthOverride(Pitch).HAlign(HAlign_Center)[Cell]
			];
		}
	}

private:
	TWeakObjectPtr<ANiceInkHUD> Hud;
	TSharedPtr<SHorizontalBox> Row;
	TSharedPtr<FSlateBrush> FrameEmpty, FramePlayer;
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
		// 房碼在暫停選單裡是一則資訊不是標題（大廳左下已用主角字級講過一次）：展示體 Heading 級
		//（三修用 Value 18＝比它的小標還窄、主從顛倒，user 打回）
		CodeFont = NiSlate::DisplayFont(F, NiType::Heading, true, NiSlate::CodeTracking);

		// 模態的地＝這一層全螢幕黑 62%（UI_SYSTEM §11.2「模態＝黑 60%」）。它就是面板：實機
		// 量過（2026-09-16，-game 2560×1380）障子牆 204 → 78，白字對比 7.5:1。**不要在它上面
		// 再疊一塊有邊界的面板**——那是主選單設定頁的做法，而主選單沒有全螢幕暗底；同一畫面
		// 兩層模態處理，全遊戲沒有第二處（09-16 一版犯過、當天拆掉）。
		Dim = MakeShared<FSlateColorBrush>(FLinearColor(0, 0, 0, NiUi::ModalDim));
		Divider = MakeShared<FSlateColorBrush>(FLinearColor(1, 1, 1, 0.14f));
		{
			// 席位列的底＝黑 25% 圓角（大廳席位格同一種底；有人沒人只差內容）
			FLinearColor Ground = NiHudColor::Black; Ground.A = 0.25f;
			RowGround = MakeShared<FSlateRoundedBoxBrush>(Ground, NiUi::Radius);
			// 七修（2026-09-19 踢人鈕搬到卡外、hover 才出現）用的兩塊：
			//  ClearBrush ＝完全透明，但 SBorder 照樣參與命中測試（Slate 的命中看幾何不看 alpha）
			//               ⇒ 拿它當「這一列」的 hover 感測器，範圍含卡與卡外的鈕。
			//  RowHotVeil ＝白 10%，與這個選單文字鈕的 hover 同一個值（一種 hover 一個樣子）。
			ClearBrush = MakeShared<FSlateColorBrush>(FLinearColor(0, 0, 0, 0));
			RowHotVeil = MakeShared<FSlateRoundedBoxBrush>(FLinearColor(1, 1, 1, 0.10f), NiUi::Radius);
			// 每人音量：SSlider 只負責拖曳，**軌與拇指都畫成全透明**；看得見的是那排楔形格子。
			SliderBar = MakeShared<FSlateColorBrush>(FLinearColor(0, 0, 0, 0));
			SliderThumb = MakeShared<FSlateColorBrush>(FLinearColor(0, 0, 0, 0));
			VolStyle = FSliderStyle()
				.SetNormalBarImage(*SliderBar).SetHoveredBarImage(*SliderBar).SetDisabledBarImage(*SliderBar)
				.SetNormalThumbImage(*SliderThumb).SetHoveredThumbImage(*SliderThumb).SetDisabledThumbImage(*SliderThumb)
				.SetBarThickness(4.0f);
			VolStep = MakeShared<FSlateRoundedBoxBrush>(FLinearColor::White, 2.0f);
			// **滑桿＝§11.2 八個元件之外的第九個**（2026-09-19，user：「我們也需要調整該玩家的音量」）。
			// 不用設定頁那組 `‹ 值 ›` 步進器的理由：步進器是為了「你要讀那個數字」而存在的（靈敏度 1.0、
			// 音量 100%、on／off），一頁三列；每人音量不是要讀數字，是「把這個人轉小聲」的手勢，
			// 六列各配一組箭頭＋數字會把這張表變成試算表。滑桿在這件事上近乎普世（Content Warning／PEAK／
			// Discord／每個作業系統）——**抄的是「這個任務的既成形狀」這條規則，不是抄它們的樣式**。
		}
		AccentBar = MakeShared<FSlateColorBrush>(FLinearColor::White);
		RowLabelFont = NiSlate::BodyFont(F, NiType::Label.Size, false, NiType::Label.Tracking);
		ValueFont = NiSlate::BodyFont(F, NiType::Body.Size, false);
		RowTextFont = NiSlate::BodyFont(F, NiType::Action.Size, false);   // 席位列的名字與錢：臉 80 配 24（內文體、常規字重）
		// 席位列的踢人鈕＝角色表的「次要動作／導覽」（內文體 18）：這一列的正文是名字與錢（24），
		// 動作不該跟正文一樣響；內文體不轉大寫（展示體才大寫）。
		ActionSmallFont = NiSlate::BodyFont(F, NiType::ActionSmall.Size, false);
		MakeButtonStyles();

		ChildSlot
		[
			SNew(SBorder).BorderImage(Dim.Get()).Padding(0)
			[
				SNew(SOverlay)
				// 六修（2026-09-18 user：「MENU 欄位突然往上擺是什麼原因？如果要置中，應該如何定義置中的確切位置才能符合整個遊戲的
				// UI 設計規範？」）——左欄與中欄各自掛各自的錨點，不再共用一列：
				// ① 左欄（動作）＝主選單動詞欄同一個錨：x=72 左軸、頂線＝剩餘高的 36%（上下撐開器 0.36／0.64，與主選單 0.22／0.78
				//    同一種寫法、同一條線）。它的高度只由自己決定，中欄長高不會把它往上推——五修四／五版就是這樣被推的：三欄同一列，
				//    撐開器分的是「扣掉那一列之後的剩餘」，六列 96 讓剩餘變小、36% 跟著變小，MENU 從 380 爬到 158。
				// ② 中欄（這一頁要做的事）＝模態內容的錨：**螢幕正中**（水平＝畫面寬的一半、垂直＝畫面高的一半，量的是整塊的盒）
				//    ——與墨杯盤／指認面板／兇手轉盤同一條規則（§11.3：模態＝置中於螢幕）；4～6 席長短不同的表都繞同一個中心長。
				//    水平置中不再靠兩側等寬空盒：外框左右對稱、直接 HAlign_Center。
				// 兩個錨各自成立 ⇒ 左欄的頂線與中欄的頂線不再保證同高，這是刻意的：它們是兩種東西（出口／內容）。
				+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Fill).Padding(Gutter - TextAxis, 0, 0, 0)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().FillHeight(0.36f)[SNew(SSpacer)]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBox).WidthOverride(ColRW + TextAxis).HAlign(HAlign_Left)
						.Visibility(TAttribute<EVisibility>::CreateLambda([this]()
							{ return GetPage() == 0 ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; }))
						[BuildRoot()]
					]
					+ SVerticalBox::Slot().FillHeight(0.64f)[SNew(SSpacer)]
				]
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()[BuildPlayersColumn()]
					+ SOverlay::Slot()[BuildHowTo()]
					+ SOverlay::Slot()[BuildSettings()]
				]
				// 底部左下：任何一頁都有的 ESC
				+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(NiUi::Margin)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SNiKeycap).Hud(Hud).Font(NiSlate::BodyFont(F, NiType::Small, true))
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
	void ResetPage() { SetPage(0); }

private:
	// ---- 樣式 ----
	void MakeButtonStyles()
	{
		auto Fill = [](float A) { return FSlateRoundedBoxBrush(FLinearColor(1, 1, 1, A), NiUi::Radius); };
		// 這個選單只剩一種鈕＝文字鈕（無底、hover 才淡淡浮起）：灰底方塊與實心主鈕 09-17 三修全部退役
		//（灰方塊貼在一起讀成一塊灰板；RESUME 是預設動作，不該是畫面最響的東西）。
		TextBtn.SetNormal(Fill(0.0f)).SetHovered(Fill(0.10f)).SetPressed(Fill(0.16f))
			.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
	}

	FText NiText(ENiLocKey Key) const { return FText::FromString(NiLoc::T(Hud.Get(), Key).ToUpper()); }

	/** 文字鈕＝主選單 MakeTextButton 的同款（無底無框、行高 Action×2.25、hover 左側 3px 短棒）。
	 *  2026-09-17 user：「這個畫面的 UI 實在太醜了」——此前三顆非主鈕是 10% 灰底方塊、彼此貼 8px，
	 *  讀成一塊灰板；主選單首頁早就是文字鈕堆疊，同一款遊戲不該有兩種按鈕語言。 */
	TSharedRef<SWidget> TextButton(ENiLocKey Label, TFunction<void()> OnClick, bool bDanger)
	{
		// 毀滅性動作平時退一階（白 70%）、hover 才轉紅：輕重靠顏色講，不靠距離講
		const FLinearColor Hot = bDanger ? NiHudColor::Red : NiHudColor::AccentText;
		const FLinearColor Idle = bDanger ? NiHudColor::White70 : NiHudColor::Paper;
		TSharedPtr<SButton> Btn;
		SAssignNew(Btn, SButton).ButtonStyle(&TextBtn).IsFocusable(false).ContentPadding(FMargin(0))
			.OnClicked(FOnClicked::CreateLambda([OnClick]() { OnClick(); return FReply::Handled(); }));
		TWeakPtr<SButton> Weak = Btn;
		auto Hovered = [Weak]() { const TSharedPtr<SButton> B = Weak.Pin(); return B.IsValid() && B->IsHovered(); };
		Btn->SetContent(
			SNew(SBox).HeightOverride(NiType::Action.Size * 2.25f).VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(3.0f).HeightOverride(NiType::Action.Size * 0.8f)
					[
						SNew(SImage).Image(AccentBar.Get()).ColorAndOpacity(Hot)
						.Visibility_Lambda([Hovered]() { return Hovered() ? EVisibility::HitTestInvisible : EVisibility::Hidden; })
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(NiUi::GapL, 0, 0, 0)
				[
					SNew(STextBlock).Font(BtnFont)
					.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
					.ColorAndOpacity_Lambda([Hovered, Hot, Idle]() { return FSlateColor(Hovered() ? Hot : Idle); })
					.Text(NiText(Label))
				]
			]);
		return Btn.ToSharedRef();
	}

	// 版面常數：左軸 72（＝主選單 NiMenuLeftGutter）、文字鈕短棒＋間距 19、欄寬 320、欄距 64
	static constexpr float Gutter = 18.0f * NiUi::U;
	static constexpr float TextAxis = 3.0f + NiUi::GapL;
	static constexpr float ColRW = 96.0f * NiUi::U;   // 兩側欄（動作／玩家）384：名字上限 16 字 ≈ 160px，靴子不會離名字太遠
	static constexpr float CenterW = 160.0f * NiUi::U; // 中欄 640：一句標題＋一句副句／說明五條／設定三列都住得下

	// ---- 三頁 ----
	TSharedRef<SWidget> BuildRoot()
	{
		// 輕重（2026-09-17 三修，user：「不知道輕重，也不知道每個東西應該放哪裡才有美感」）：
		// - 這一頁沒有標題。48 級的 MENU 什麼都沒說，它跟房碼並排就是兩個標題在打架；退成一行小標。
		// - 動作清單一個字級一個字重：RESUME 不再是實心白塊（它是預設動作，ESC 鍵帽已經在說），
		//   LEAVE THE ROOM 用顏色退一階＋隔 24，hover 才轉紅。
		// - 玩家清單是次要資訊：臉 32、名字一種字級，房碼只是清單上方一行小字。
		// - 所有文字掛同一條左軸（TextAxis 補回文字鈕的短棒位）。
		TSharedRef<SVerticalBox> Left = SNew(SVerticalBox);
		Left->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(TextAxis, 0, 0, 0)
		[
			SNew(STextBlock).Font(RowLabelFont).ColorAndOpacity(NiHudColor::PaperDim)
			.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
			.Text(NiText(ENiLocKey::MenuTitle))
		];
		Left->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0, NiUi::GapM, 0, 0)
		[
			TextButton(ENiLocKey::MenuResume, [this]() { DoAction(0); }, false)
		];
		Left->AddSlot().AutoHeight().HAlign(HAlign_Left)
		[
			TextButton(ENiLocKey::MenuHowToPlay, [this]() { DoAction(1); }, false)
		];
		Left->AddSlot().AutoHeight().HAlign(HAlign_Left)
		[
			TextButton(ENiLocKey::MenuSettings, [this]() { DoAction(2); }, false)
		];
		Left->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0, NiUi::Margin, 0, 0)
		[
			TextButton(ENiLocKey::MenuLeave, [this]() { DoAction(3); }, true)
		];

		return Left;
	}

	/** 中欄標題＋副句（三頁共用的畫法）：展示體 Title 48 置中、副句內文 18 白 70% 置中，兩行相距 GapL */
	TSharedRef<SWidget> CenterHead(ENiLocKey Head, ENiLocKey Sub, bool bHasSub, const FSlateFontInfo& HeadFontIn)
	{
		// 標題吃欄寬自動換行（首頁那句在 48 級展示體下寬 830 > 欄 640＝實拍被切成「HE ROOM KEEPS GOIN(」；
		// 一句話的標題用 Heading 32，頁名一個詞才用 Title 48）
		TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
		V->AddSlot().AutoHeight().HAlign(HAlign_Fill)
		[
			SNew(STextBlock).Font(HeadFontIn).ColorAndOpacity(NiHudColor::Paper).Justification(ETextJustify::Center)
			.AutoWrapText(true)
			.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
			.Text(NiText(Head))
		];
		if (bHasSub)
		{
			V->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, NiUi::GapL, 0, 0)
			[
				SNew(STextBlock).Font(BodyFont).ColorAndOpacity(NiHudColor::White70).Justification(ETextJustify::Center)
				.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
				.Text(NiText2(Sub))
			];
		}
		return V;
	}

	TSharedRef<SWidget> BuildPlayersColumn()
	{
		// 首頁正中央＝這一頁要做的事：看房裡有誰、要不要踢（五修三版；user：「這有在正中央嗎？你有思考過排版嗎？頭像可以大一點嗎？」）。
		// 二版把「左對齊的清單」塞進一個置中的盒＝盒置中、內容偏左（房碼小標 370 寬、單人列 200 寬＝臉落在中心左邊 190px）。
		// 三版＝**整塊以中軸對稱**：房碼（小標＋展示體）置中、PLAYERS 置中、玩家＝一排席位格橫向置中（大廳底列同一種東西：
		// 臉 96 在上、名字在下、靴子在名字下），人數 2～6 都對稱、每張臉都是主角。與左欄的小標同一條頂線。
		// 房碼沒有時整組連同它的內距一起消失（Collapsed）。
		TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
		V->AddSlot().AutoHeight().HAlign(HAlign_Center)
		[
			SNew(SBox).Padding(FMargin(0, 0, 0, NiUi::Margin))
			.Visibility(this, &SNiSystemMenu::CodeVis)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(STextBlock).Font(RowLabelFont).ColorAndOpacity(NiHudColor::PaperDim).Justification(ETextJustify::Center)
					.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
					.Text(NiText(ENiLocKey::LobbyCodeHint))
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					.Padding(0, NiSlate::InkGapPadding(RowLabelFont, CodeFont, 0.20f * NiSlate::DisplayCapHeight(CodeFont), true), 0, 0)
				[
					SNew(STextBlock).Font(CodeFont).ColorAndOpacity(NiHudColor::Paper).Justification(ETextJustify::Center)
					.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
					.Text(this, &SNiSystemMenu::GetCode)
				]
			]
		];
		V->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, NiUi::GapL)
		[
			SNew(STextBlock).Font(RowLabelFont).ColorAndOpacity(NiHudColor::PaperDim).Justification(ETextJustify::Center)
			.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
			.Text(NiText(ENiLocKey::MenuPlayers))
		];
		V->AddSlot().AutoHeight().HAlign(HAlign_Center)[SAssignNew(PlayerList, SVerticalBox)];
		return SNew(SBox)
			.Visibility(TAttribute<EVisibility>::CreateLambda([this]()
				{ return GetPage() == 0 ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; }))
			[V];
	}

	TSharedRef<SWidget> BuildHowTo()
	{
		// 中欄：標題置中，五條說明靠左排在 640 的欄裡（條列置中會讀成詩）；標題到第一條 Margin 24
		TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
		V->AddSlot().AutoHeight().HAlign(HAlign_Fill)[CenterHead(ENiLocKey::MenuHowToPlay, ENiLocKey::MenuHowToPlay, false, TitleFont)];
		static const ENiLocKey Lines[5] = { ENiLocKey::HowTo1, ENiLocKey::HowTo2, ENiLocKey::HowTo3,
			ENiLocKey::HowTo4, ENiLocKey::HowTo5 };
		for (int32 i = 0; i < 5; ++i)
		{
			const ENiLocKey K = Lines[i];
			V->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0, i ? NiUi::GapL : NiUi::Margin, 0, 0)
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
					SNew(SBox).WidthOverride(CenterW - 3.0f * NiUi::U - NiUi::GapL)
					[
						SNew(STextBlock).Font(BodyFont).ColorAndOpacity(NiHudColor::Paper)
						.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
						.AutoWrapText(true).Text(NiText2(K))
					]
				]
			];
		}
		// 頁內 BACK 鈕拆除（2026-09-17）：ESC 在子頁＝回上一頁、左下鍵帽寫著 BACK——一個意圖一個位置（主選單 09-05 同一條）
		return SNew(SBox).WidthOverride(CenterW)
			.Visibility(TAttribute<EVisibility>::CreateLambda([this]()
				{ return GetPage() == 1 ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; }))
			[V];
	}

	TSharedRef<SWidget> BuildSettings()
	{
		// 列的畫法照抄主選單設定頁的 MakeRow（2026-09-16 user：「按鍵、內容等等的都看不清楚」
		// ——此前這頁是 24px 方塊裡塞展示體的 −／＋（展示體沒有這兩個字形，面裡只剩一道底線）、
		// 值欄 64px 把 100% 切成 00%）：列＝標籤｜‹ 值 ›，列底 14% 髮線。地＝全螢幕暗底，不另給面板。
		// 中欄：標題＋副句「改了立刻生效」置中，三列設定在 640 的欄裡；副句到第一列 Margin 24
		TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
		V->AddSlot().AutoHeight().HAlign(HAlign_Fill)[CenterHead(ENiLocKey::MenuSettings, ENiLocKey::SettingsNote, true, TitleFont)];
		V->AddSlot().AutoHeight().Padding(0, NiUi::Margin, 0, 0)[AdjustRow(ENiLocKey::MenuSensitivity, 0)];
		V->AddSlot().AutoHeight()[AdjustRow(ENiLocKey::MenuVolume, 1)];
		// 走路晃動（2026-09-15 user 定案：設定開關、預設開；局內也能切＝相機當幀跟著變）
		V->AddSlot().AutoHeight()[AdjustRow(ENiLocKey::HeadBob, 2)];
		// 頁內 BACK 鈕拆除（同 HowTo）
		return SNew(SBox).WidthOverride(CenterW)
			.Visibility(TAttribute<EVisibility>::CreateLambda([this]()
				{ return GetPage() == 2 ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; }))
			[V];
	}

	/** 一列設定（主選單 MakeRow 的同款）：標籤左、‹ 值 › 右、列底髮線 */
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
			else if (Which == 1)
			{
				GI->MasterVolume = FMath::Clamp(GI->MasterVolume + Dir * 0.05f, 0.0f, 1.0f);
				GI->UpdateBgmVolume();
			}
			else
			{
				GI->bHeadBobEnabled = !GI->bHeadBobEnabled; // 二態：兩個箭頭都是切換
			}
			GI->SaveSettings();
		};
		// 箭號＝細字元、無底（灰方塊讀成試算表）；‹ › 在內文體裡有字形
		auto Arrow = [this, Step](const TCHAR* Glyph, int32 Dir)
		{
			return SNew(SButton).ButtonStyle(&TextBtn).ContentPadding(FMargin(12, 4))
				.HAlign(HAlign_Center).VAlign(VAlign_Center)
				.OnClicked(FOnClicked::CreateLambda([Step, Dir]() { Step(Dir); return FReply::Handled(); }))
				[
					SNew(STextBlock).Font(ValueFont).ColorAndOpacity(NiHudColor::White70)
					.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
					.Text(FText::FromString(Glyph))
				];
		};
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
				[
					// 列標籤＝大寫小標：設定列是「標籤｜控制」，標籤不該跟值一樣大
					SNew(STextBlock).Font(RowLabelFont).ColorAndOpacity(NiHudColor::PaperDim)
					.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
					.Text(NiText(Label))
				]
				+ SHorizontalBox::Slot().AutoWidth()[Arrow(TEXT("‹"), -1)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(150.0f)
					[
						SNew(STextBlock).Font(ValueFont).ColorAndOpacity(NiHudColor::Paper)
						.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
						.Justification(ETextJustify::Center)
						.Text(TAttribute<FText>::CreateLambda([this, Which]() { return SettingValue(Which); }))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth()[Arrow(TEXT("›"), +1)]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).HeightOverride(1.0f)[SNew(SImage).Image(Divider.Get())]
			];
	}

	FText SettingValue(int32 Which) const
	{
		UNiceInkGameInstance* GI = UNiceInkGameInstance::Get(Hud.Get());
		if (!GI) { return FText::GetEmpty(); }
		if (Which == 2)
		{
			// 二態值＝內文體小寫 on／off（主選單同款；不走 NiText 的 ToUpper）
			return FText::FromString(NiLoc::T(Hud.Get(), GI->bHeadBobEnabled ? ENiLocKey::OptionOn : ENiLocKey::OptionOff));
		}
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
		case 0: if (C) { C->SetSystemMenuOpen(false); } SetPage(0); break;
		case 1: SetPage(1); break;
		case 2: SetPage(2); break;
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
		// 房主永遠第一列（大廳同一條規則：這是唯一講「誰是房主」的訊號），其後依加入順序
		Sorted.Sort([](const ANiceInkPlayerState& A, const ANiceInkPlayerState& B)
		{
			if (A.bIsRoomHost != B.bIsRoomHost) { return A.bIsRoomHost; }
			return A.SeatIndex < B.SeatIndex;
		});
		// 列數＝這一房的位子數（房主開房時定 4~6）；名冊比位子多（不該發生）時仍把人全畫出來
		const int32 Seats = FMath::Max(GS->MaxPlayers, Sorted.Num());

		// **踢人鈕的字進簽章**（2026-09-19）：它現在是譯文，玩家在同一個 ESC 選單的設定頁就能換語言
		// ⇒ 不進簽章的話列不會重建，鈕會留著上一個語言的字。放字串本身而不是語言索引＝來源換了就一定重建。
		const FString KickLabel = NiLoc::T(H, ENiLocKey::MenuKick);
		// 哪一列是我——**自己那一列不給音量條**（調不了自己的音量）。進簽章是因為它可能晚一點才解析得到。
		const ANiceInkPlayerState* MyPS = H->PlayerOwner
			? Cast<ANiceInkPlayerState>(H->PlayerOwner->PlayerState) : nullptr;
		FString Sig = FString::Printf(TEXT("seats=%d;host=%d;kick=%s;me=%d;"), Seats, bHost ? 1 : 0,
			*KickLabel, MyPS ? MyPS->GetPlayerId() : -1);
		for (const ANiceInkPlayerState* P : Sorted)
		{
			Sig += FString::Printf(TEXT("%d:%s:%d:%d;"), P->SeatIndex, *P->GetPlayerName(), P->bIsRoomHost ? 1 : 0, P->Cash);
		}
		if (Sig == LastSig) { return; }
		LastSig = Sig;

		PlayerList->ClearChildren();
		// 五修四版（2026-09-18 user 逐字：「給我改成一個個橫列排下來的樣子，一樣先預留這個房間應該有幾個位置，等人加入再把大頭和
		// 名字及對應人的錢數字放進去，給我編排好設計」）：
		// 一列＝一席。列＝黑 25% 圓角底的橫條（大廳席位格同一種「底」：有人沒人只差內容），寬 RowW、高 RowH。
		// 尺寸（五修五版，user：「頭部大小可以再更大一點嗎？應該設為多少比較符合設計語言？」）＝**照抄大廳席位格**：
		// 臉 FaceSeat 80、臉到底邊 SeatPad 8 ⇒ 列高＝SeatFrame 96——同一個物件（席位）在兩個畫面同一個尺寸；四版的 64 是「次要清單」
		// 的臉（FaceM），而這張表是這一頁的主角。臉變大，字跟著上一階：名字與錢從 Body 18 → Action 24（相鄰身分差兩軸：臉 80 對字 18
		// 是 4.4 倍＝字讀成註腳；24 是 3.3 倍＝表的正文），銭從 20 → 24（與字同高）；元件間距從 16 → 24（組外距，臉與字是兩個東西）；
		// 列寬 480 → 560（名字預算仍 ≥ 16 字）。列距維持 8（組內）——六列是一張表，不是六張卡。
		// 內容由左到右＝臉 80｜名字（24、吃剩餘寬、縮寫）｜錢（數字 24 靠右＋銭 24）｜動作欄 24（靴子：房主畫面、
		// 非房主列；其餘列放同寬空盒＝錢的欄位在每一列對齊）。空席＝只有底條。整塊置中（BuildPlayersColumn 的 HAlign_Center）。
		// **五修的 8／24 已於六修作廢**（下面那段）——上面那句「臉到底邊 SeatPad 8 ⇒ 列高 SeatFrame 96」
		// 只剩「列高是臉＋兩個內距推出來的」這條推導還成立，值本身已換。
		// **間距（2026-09-19 六修）**：五修是框邊 8／元件間 24＝1︰3 的**反比**（框咬著內容、內容彼此散開
		// ——同一列的臉／名字／錢／靴子屬於同一個人，框的邊界才是人與人的分界）。
		// 六修一版 user：「內容與框邊、內容之間的間距都改成 12」⇒ 兩者同值；
		// 二版 user 看過實拍：「內容之間的間距都改成 16；內容與框邊維持 12」⇒ **框邊 12／元件間 16**。
		// 列高只由框邊內距決定（臉 80 ＋ 兩個 12）＝104，所以二版沒有動到列高。
		// **八修（2026-09-19，user：「我們也需要調整該玩家的音量」）——這一列從「名冊」變成「控制台」。**
		// 七修把踢出移到卡外、hover 才出現，理由是「臉／名字／錢是他的屬性，動作混進去不對」。
		// 音量一進來那個理由就消滅了：這一列現在裝的是 **我對這個人的兩件事**（聽他多大聲、要不要踢他），
		// 正是 Content Warning（頭像→VOL→紅色 KICK 同一張卡）與 PEAK（靴子貼著音量滑桿）那張卡的性質
		// ——而那也正是「踢人放在裡面不突兀」的原因。所以踢出**收回列內、常駐**，與兩款參照對齊；
		// 把兩個對同一個人的控制分放兩處（音量在內、踢出在外）反而比七修之前更糟。
		// 列寬 560 → 840：臉 80 ｜名字 ｜錢 150 ｜音量 128 ｜踢出（譯文寬）。840 是讓**德文**（rauswerfen
		// ＝最長的譯文）也保得住「名字 ≥242＝16 字不縮寫」（§13.6 四修 user 定案）的最小值。
		// hover 的整列亮起留著——兩個控制在同一列時，「我現在在動哪一個人」值得被講出來。
		const float RowH = NiUi::SeatRowFrame; // 104 ＝ 臉 80 ＋ 兩個框邊內距 12
		const float RowW = 210.0f * NiUi::U;   // 840
		const float VolW = 32.0f * NiUi::U;    // 128：音量條
		// 楔形音軌的格數：16 格 × 6 寬 ＋ 15 個 2 的間隙 ＝ 126（VolW 128），高 4→32。
		const int32 NiVolSteps = 16;
		const float FaceS = NiUi::FaceSeat;    // 80
		const float PadIn = NiUi::SeatRowPad;  // 12：框邊 ↔ 內容
		const float Gap   = NiUi::SeatRowGap;  // 16：內容 ↔ 內容
		const float CoinS = 6.0f * NiUi::U;    // 24：與 24 級數字同高
		// **動作欄的寬度由譯文決定，不是寫死的**（2026-09-19 靴子換成文字鈕）：同一顆鈕在 ja「キック」是 3 字、
		// 在 de「rauswerfen」是 10 字，差三倍以上。寫死一個寬度只會讓某些語言被切或某些語言空一大塊 ⇒ 量了再排。
		// 一場局裡所有人同一個語言 ⇒ 每一列的鈕等寬，錢的欄位照樣對齊。下限 KeycapH 防譯文極短時鈕小到按不到。
		const FText KickText = FText::FromString(KickLabel);
		const float KickInk = FSlateApplication::IsInitialized()
			? NiSlate::Measure()->Measure(KickLabel, ActionSmallFont).X : 0.0f;
		const float ActionW = FMath::Max(NiUi::KeycapH, FMath::CeilToFloat(KickInk) + 2.0f * NiUi::GapM);
		// **不做成「hover 臉就變踢出鈕」**（user 曾提的另一案）：§11.3 明文臉是身分的唯一載體，而臉在指認
		// 相位**已經是可點的**（點臉＝指認嫌疑人）⇒ 同一個手勢在兩個畫面會意思相反，其中一邊還不可逆。
		// 仍然一鍵即踢、無確認（09-17 定案不動）。
		// 名字吃剩下的：840 −（框 12×2 ＋ 臉 80 ＋ 錢 150 ＋ 音量 128 ＋ 踢出 ＋ 四個 16 的間距）。
		const float NameW = RowW - PadIn - FaceS - Gap - 150.0f - Gap - VolW - Gap - ActionW - Gap - PadIn;
		for (int32 i = 0; i < Seats; ++i)
		{
			const ANiceInkPlayerState* P = (i < Sorted.Num()) ? Sorted[i] : nullptr;
			TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
			// 動作欄的占位（空席與房主自己那一列＝同寬空盒，讓每一列的卡等寬、整塊仍然置中）
			TSharedRef<SWidget> Action = SNew(SBox).WidthOverride(ActionW);
			if (P)
			{
				Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(PadIn, 0, 0, 0)
				[
					SNew(SBox).WidthOverride(FaceS).HeightOverride(FaceS)[SNew(SNiFace).Hud(Hud).PS(P).Size(FaceS)]
				];
				Row->AddSlot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(Gap, 0, 0, 0)
				[
					SNew(STextBlock).Font(RowTextFont).ColorAndOpacity(NiHudColor::Paper)
					.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
					.Text(FText::FromString(NiSlate::Elide(P->GetPlayerName(), RowTextFont, NameW)))
				];
				// 錢＝數字＋穴あき銭（數字 Paper、銭 PaperDim）。
				// **間距 4 → 8**（2026-09-19，user：「現在錢的數字和錢的符號之間的間距是？？？」）：
				// 4 是從右上角現金顯示 `SNiCash` 抄來的，而**那裡的現金旁邊沒有別的東西，4 是它唯一的間距**；
				// 這一列裡它的鄰居全是 12，4 就讀成黏上去的。又一次抄值沒抄前提。
				// 不跟著元件間距走的理由：數字與銭是**一個數值加它的單位**，不是兩個並列的成員——給成
				// 一樣的值會讓銭離數字和離靴子一樣遠，它就不再屬於那個數字。8＝§14.3 明文的「組內」值，
				// 對元件間距（六修二版起 16）仍然小，分組保住、而且比一版的 12 更清楚。
				//（user 原本要 6，但 6 不在 §14.1.3 的 Carbon 級距表上也推導不出來——專案裡唯一的 6 是
				// KeycapPad，那是 (24−12)/2 的幾何結果，不是挑的。）
				Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(Gap, 0, 0, 0)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock).Font(RowTextFont).ColorAndOpacity(NiHudColor::Paper)
						.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
						.Text(FText::AsNumber(P->Cash))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(NiUi::GapM, 0, 0, 0)
					[
						SNew(SBox).WidthOverride(CoinS).HeightOverride(CoinS)
						[
							SNew(SImage).Image(NiSlate::Brush(H ? H->GetCoinIcon() : nullptr, CoinS))
							.ColorAndOpacity(NiHudColor::PaperDim)
						]
					]
				];
				// **這個人的音量**（八修）：0~2、1＝原樣，正是 `IVoiceChatUser::SetPlayerVolume` 的值域，
				// 不另發明一套。滑桿走 0~1 ⇒ ×2 換算；**預設落在正中央**＝「原樣」，往左轉小、拉到底就是靜音。
				// **不另給靜音鈕**：EOS 那邊 SetPlayerVolume(0) 與 SetPlayerMuted 是兩個 API，但玩家心裡只有
				// 一件事；多一顆鈕就多一個要對賬的狀態。
				// 值的正本在 GameInstance（`GetPlayerVoiceVolume`），**不每幀去問 EOS**——09-07 血價：
				// 一版每幀每張臉都叫 GetVoiceChatUserInterface，PIE 開始五秒就 D3D12 E_OUTOFMEMORY。
				// **自己那一列不給音量條**（八修二版，實拍抓到）：你調不了自己的音量，那是一個
				// 按了不會發生任何事的控制項。改放同寬空盒 ⇒ 其餘欄位照樣對齊（與踢出欄同一個處理）。
				if (P == MyPS)
				{
					Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(Gap, 0, 0, 0)
					[
						SNew(SBox).WidthOverride(VolW)
					];
				}
				else
				{
					ANiceInkPlayerState* VolTarget = const_cast<ANiceInkPlayerState*>(P);
					auto VolAt = [this, VolTarget]()
					{
						const UNiceInkGameInstance* GI = UNiceInkGameInstance::Get(Hud.Get());
						return GI ? GI->GetPlayerVoiceVolume(VolTarget) * 0.5f : 0.5f;   // 0~1
					};
					// 看得見的軌＝一排逐格長高的格子（16 格 × 6 寬 ＋ 15 個 2 的間隙 ＝ 126，欄寬 128）。
					// 高度 4→32 對稱於中線。亮／暗以格子的位置與現值比大小 ⇒ 填到哪裡就是多大聲。
					TSharedRef<SHorizontalBox> Steps = SNew(SHorizontalBox);
					for (int32 S = 0; S < NiVolSteps; ++S)
					{
						const float Frac = (NiVolSteps > 1) ? (S / float(NiVolSteps - 1)) : 0.0f;
						Steps->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(S ? 2.0f : 0.0f, 0, 0, 0)
						[
							SNew(SBox).WidthOverride(6.0f).HeightOverride(4.0f + Frac * 28.0f)
							[
								SNew(SImage).Image(VolStep.Get())
								.ColorAndOpacity_Lambda([VolAt, Frac]()
									{ return FSlateColor(VolAt() >= Frac ? NiHudColor::Paper : NiHudColor::White45); })
							]
						];
					}
					Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(Gap, 0, 0, 0)
					[
						SNew(SBox).WidthOverride(VolW)
						[
							SNew(SOverlay)
							+ SOverlay::Slot().VAlign(VAlign_Center)[Steps]
							+ SOverlay::Slot()
							[
								SNew(SSlider).Style(&VolStyle).IndentHandle(false)
								.Value_Lambda(VolAt)
								.OnValueChanged_Lambda([this, VolTarget](float V)
								{
									if (UNiceInkGameInstance* GI = UNiceInkGameInstance::Get(Hud.Get()))
									{
										GI->SetPlayerVoiceVolume(VolTarget, V * 2.0f);
									}
								})
							]
						]
					];
				}
				// 動作欄：踢人的**文字鈕**（只鎖寬不鎖高——2026-09-19 實拍打回：一版寫了
				// HeightOverride(KeycapH=24)，而 18pt 的字行框約 32 ⇒ 字被橫向裁掉一半。
				// 鍵帽的 24 是「圖形的高」，拿來當「文字的盒」是抄值沒抄前提）。
				if (bHost && !P->bIsRoomHost)
				{
					// **踢人＝寫著字的文字鈕**（2026-09-19 user：「把靴子換掉，換成符合我們遊戲設計語言的按鈕，
					// 按鈕上就用玩家選擇的語言告訴玩家這是踢人的按鈕」）。09-17 的 24×24 靴子退役。
					// 選這個形式的依據＝§11.2 的八個元件裡只有兩種鈕：**主鈕**（強調色實填，一頁一顆）與
					// **文字鈕**（無底無框、hover 才浮起）。一張表上最多五顆，主鈕的「一頁一顆」用不了 ⇒ 文字鈕。
					// 字級＝`NiType::ActionSmall`（內文體 18）＝角色表裡「次要動作／導覽」那一格：這一列的正文是
					// 名字與錢（24），動作不該跟正文一樣響。內文體**不轉大寫**（展示體才大寫，設定頁的 on／off 同款）。
					// 危險態沿用 TextButton 的同一條語言：平時白 70%、hover 轉紅。
					// 仍是一鍵即踢、無確認（09-17 定案不動；PEAK／Content Warning／Gartic 同派），本場拒再入。
					ANiceInkPlayerState* Target = const_cast<ANiceInkPlayerState*>(P);
					TSharedPtr<SButton> Btn;
					SAssignNew(Btn, SButton).ButtonStyle(&TextBtn)
						.ContentPadding(FMargin(NiUi::GapM, NiUi::GapS))
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
						}));
					TWeakPtr<SButton> Weak = Btn;
					Btn->SetContent(
						SNew(STextBlock).Font(ActionSmallFont)
						.ShadowOffset(FVector2D(1, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.6f))
						.ColorAndOpacity_Lambda([Weak]()
						{
							const TSharedPtr<SButton> B = Weak.Pin();
							return FSlateColor((B.IsValid() && B->IsHovered()) ? NiHudColor::Red : NiHudColor::White70);
						})
						.Text(KickText));
					Action = SNew(SBox).WidthOverride(ActionW)[Btn.ToSharedRef()];
				}
				// 踢出**收回列內、常駐**（八修）：這一列現在是控制台，兩個對同一個人的控制不該分放兩處。
				Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(Gap, 0, PadIn, 0)[Action];
			}

			// hover 掛在**最外層的 SBorder**（全透明、只為了收 hover）：整列亮起＝「我現在在動哪一個人」。
			// 兩個控制在同一列時這句值得被講出來；七修那版「鈕在卡外、hover 才出現」已隨踢出收回而退役。
			TSharedPtr<SBorder> RowHover;
			SAssignNew(RowHover, SBorder).BorderImage(ClearBrush.Get()).Padding(0);
			TWeakPtr<SBorder> WeakRow = RowHover;
			auto RowHot = [WeakRow]()
			{
				const TSharedPtr<SBorder> B = WeakRow.Pin();
				return B.IsValid() && B->IsHovered();
			};
			RowHover->SetContent(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox).WidthOverride(RowW).HeightOverride(RowH)
					[
						SNew(SOverlay)
						+ SOverlay::Slot()[SNew(SImage).Image(RowGround.Get())]
						// hover 的地＝白 10%，與這個選單的文字鈕 hover 同一個值（一種 hover 一個樣子）。
						// 疊在列底上、壓在內容下 ⇒ 整列一起亮，機制自己現身。
						+ SOverlay::Slot()
						[
							SNew(SImage).Image(RowHotVeil.Get())
							.Visibility_Lambda([RowHot]()
								{ return RowHot() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
						]
						+ SOverlay::Slot().VAlign(VAlign_Center)[Row]
					]
				]);

			PlayerList->AddSlot().AutoHeight().Padding(0, i ? NiUi::GapM : 0.0f, 0, 0)
			[
				RowHover.ToSharedRef()
			];
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
			GetPage() == 0 ? ENiLocKey::MenuResume : ENiLocKey::MenuBack).ToUpper());
	}

	// 頁碼（0 Root／1 HowTo／2 Settings）的單一來源＝HUD 的 SysMenuPage：角色端的 ESC 判斷
	//（子頁＝回第一頁、首頁＝關閉）與 robo 鉤子 DebugRoboSystemMenuPage 都讀寫同一份。
	int32 GetPage() const { return Hud.IsValid() ? Hud->GetSysMenuPage() : 0; }
	void SetPage(int32 P) { if (Hud.IsValid()) { Hud->SetSysMenuPage(P); } }

	TWeakObjectPtr<ANiceInkHUD> Hud;
	TSharedPtr<SVerticalBox> PlayerList;   // 一列一席
	TSharedPtr<FSlateColorBrush> Dim;          // 全螢幕模態暗底＝這個選單唯一的「面板」
	TSharedPtr<FSlateColorBrush> Divider;      // 設定列底的 14% 髮線
	TSharedPtr<FSlateColorBrush> AccentBar;    // 文字鈕 hover 的左側短棒
	TSharedPtr<FSlateBrush> RowGround;         // 席位列的黑 25% 圓角底
	TSharedPtr<FSlateBrush> RowHotVeil;        // 席位列 hover 的白 10%（疊在底上、壓在內容下）
	TSharedPtr<FSlateBrush> ClearBrush;        // 全透明：只為了讓外層 SBorder 收得到 hover
	TSharedPtr<FSlateBrush> SliderBar, SliderThumb, VolStep;  // 每人音量：透明軌／透明拇指／楔形的一格
	FSliderStyle VolStyle;
	FButtonStyle TextBtn;
	FSlateFontInfo TitleFont, BtnFont, BodyFont, SmallFont, LabelFont, CodeFont, RowTextFont;
	FSlateFontInfo RowLabelFont, ValueFont;    // 設定列：大寫小標（字距 150）／內文 18
	FSlateFontInfo ActionSmallFont;            // 席位列的踢人鈕：內文體 18（次要動作）
	FString LastSig;
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
