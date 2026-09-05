#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "HAL/IConsoleManager.h"
#include "NiceInkHUD.generated.h"

enum class ENiceInkPhase : uint8;
class UFont;
class UTexture2D;

// 開發者遙測總開關（0=出貨畫面；1=console 指令、seat、版本戳等全部顯示）
extern TAutoConsoleVariable<int32> CVarNiDebugHud;

UCLASS()
class NICEINK_API ANiceInkHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void DrawHUD() override;

protected:
	// ---- 設計 token：字階（Slate 字級，乘 UiScale）／對齊 ----
	// （protected：主選單 HUD 繼承共用同一套 token helpers——樣式只有一套）
	enum class ETextTier : uint8 { Display, Title, Body, Small };
	enum class EHAlign : uint8 { Left, Center, Right };

	float UiScale = 1.0f; // ClipY / 1080：所有尺寸的唯一縮放來源

	// 文字投影開關：投影是給疊在 3D 場景上的字用的；平面深底（主選單）或
	// 深色面板上開投影＝小字邊緣髒掉（08-06 user 抓「字雜亂」的主因）
	bool bTokShadows = true;

	float TierSize(ETextTier Tier) const;

public:
	// 六文字系統複合字體（13 語矩陣：圓體+Zen 預設、繁/簡/韓/西里爾+擴拉丁/
	// 阿拉伯 SubTypeface、源流明體 fallback）——選單與局內 HUD 共用同一座矩陣
	//（2026-08-07：名字回歸局內後，局內只掛 M+ 兩面＝韓/阿/非日系漢字豆腐——
	// 抽共用根治；建置失敗回傳引擎 MediumFont 保底）
	static UFont* BuildCompositeUiFont(UObject* Outer, const TCHAR* FontName);

protected:
	// ---- UI 資產（runtime 字體＋圖示；BeginPlay 載入，UPROPERTY 保 GC）----
	UPROPERTY() TObjectPtr<UFont> UiFont;
	UPROPERTY() TObjectPtr<UTexture2D> IconCup;
	UPROPERTY() TObjectPtr<UTexture2D> IconSpray;
	UPROPERTY() TObjectPtr<UTexture2D> IconKick;
	UPROPERTY() TObjectPtr<UTexture2D> IconMarker;
	// FP 2D 筆（07-22 viewmodel 制）：入鎖時畫在畫面上緣的刺青機貼圖＋出針口→墨點針線
	UPROPERTY() TObjectPtr<UTexture2D> PenSprite;
	// FP 2D 麥克筆（07-25 打稿制）：SM_Marker 的染紫渲染（Blender headless）——
	// 缺席時退向量筆
	UPROPERTY() TObjectPtr<UTexture2D> MarkerSprite;
	UPROPERTY() TObjectPtr<UTexture2D> IconCash;
	UPROPERTY() TObjectPtr<UTexture2D> IconRotate;
	UPROPERTY() TObjectPtr<UTexture2D> IconEye;
	UPROPERTY() TObjectPtr<UTexture2D> IconTrap;
	UPROPERTY() TObjectPtr<UTexture2D> IconSleep;
	UPROPERTY() TObjectPtr<UTexture2D> IconNose;
	// 輸入 glyph（Kenney Input Prompts 1.5, CC0；調色盤已烘進 PNG）
	// **鍵盤有刻字所以寫字，滑鼠沒有刻字所以畫圖**——見 Docs/UI_SYSTEM.md §4.1
	UPROPERTY() TObjectPtr<UTexture2D> InMouseLeft;
	UPROPERTY() TObjectPtr<UTexture2D> InMouseRight;
	UPROPERTY() TObjectPtr<UTexture2D> InMouseScroll;
	UPROPERTY() TObjectPtr<UTexture2D> InMouseMove;
	// runtime 生成的圓角方塊（SDF alpha＝抗鋸齒；canvas 三角形零 AA 的繞道）
	UPROPERTY() TObjectPtr<UTexture2D> RoundedTex;
	// runtime 生成的**暈衰減曲線**（64×1，alpha 走指數）。Canvas 的頂點顏色只能
	// 線性插值，而線性衰減有一個看得出來的終點 ⇒ 讀成「一條有邊界的帶」而不是
	// 「從線滲出去的光」（user：「太像一圈亮圈、一圈暗圈」）。把曲線放進貼圖，
	// **三角形數量完全不變**就能得到任意非線性——這一點很重要，因為每幀成本已經
	// 被 cruise tipSpd 那條契約抓過兩次。
	// t=0（貼著主線）alpha=1 ⇒ 主線自己也用這張貼圖的 t=0，一張圖服務兩者。
	UPROPERTY() TObjectPtr<UTexture2D> GlowTex;
	void EnsureUiAssets();

	// 臉像＝全 UI 身分載體（2026-08-06 SPEC #52 臉制定案：名字退出畫面）
	UPROPERTY() TMap<int32, TObjectPtr<UTexture2D>> FaceIconCache;
	class UTexture2D* GetFaceIcon(int32 AvatarIdx);
	// 畫玩家臉像（含圓角紙框）；回傳實際寬度（0=查無臉）
	float DrawFaceTok(const class APlayerState* PS, float X, float Y, float Size);

	// 圓角半透明面板／按鈕底（9-slice 取樣 RoundedTex；素色簡約風的唯一面元件）
	void DrawRoundedBox(float X, float Y, float W, float H, float Radius, const FLinearColor& ColorIn);

	// ---- AR 版面鏡像（2026-08-07 SPEC v4.0e）----
	// 文化=ar 時整個 UI chrome 水平鏡像：座標一律以 LTR 邏輯空間書寫，
	// 鏡像只發生在繪製原語（DrawTok/DrawRoundedBox/DrawFaceTok/DrawIconTok）
	// 與 Button 命中判定這一層＝一次且僅一次。遊戲幾何（描圖盤/轉盤/準星/
	// 致盲潑漬/調色盤數字鍵序）以 TGuardValue 掛起豁免。
	bool bRTLLayout = false;        // DrawHUD 每幀跟語言設定刷新
	bool bMirrorSuspended = false;  // 原語內部與豁免區掛起（防雙重鏡像）

	// ---- chrome 淡出（2026-09-02 畫面清單）----
	// 落筆時上方橫幅/現金淡到 35%：它們是「兩筆之間才會看」的環境資訊，落筆當下
	// 只是亮的東西在視野邊緣。乘數掛在**最底層原語**（DrawTok/DrawRoundedBox/
	// DrawIconTok/K2_DrawTexture 路徑），用 TGuardValue 圈住要淡的區塊——與
	// bMirrorSuspended 同一個 pattern，呼叫端不必逐個 colour 改。
	float ChromeAlphaMul = 1.0f;
	bool IsMirrored() const { return bRTLLayout && !bMirrorSuspended; }
	float FlipX(float X) const;              // 錨點鏡像
	float FlipXW(float X, float W) const;    // 矩形左緣鏡像

	// ---- 繪製 helper（全 HUD 只准經過這組，樣式不得繞道自畫）----
	FVector2D DrawTok(const FString& Text, float X, float Y, ETextTier Tier,
		const FLinearColor& ColorIn, EHAlign Align = EHAlign::Left, bool bBold = false);
	FVector2D MeasureTok(const FString& Text, ETextTier Tier, bool bBold);
	// 寬度截斷（名字類自由文字的顯示保險）：超寬裁字尾補「…」。
	// SanitizePlayerName 的 16 字上限是碼元數——CJK 全形 16 字≈拉丁 32 字寬，
	// 碼元截斷擋不住版面衝突，顯示端一律走量測截斷
	FString FitTok(const FString& Text, ETextTier Tier, float MaxWidthPx, bool bBold = false);
	void DrawPanelBox(float X, float Y, float W, float H, float Alpha = 0.72f);
	void DrawIconTok(UTexture2D* Tex, float X, float Y, float Size, const FLinearColor& Tint);
	// 非方形圖示（輸入 glyph 裁過透明邊之後寬高比不是 1）
	void DrawIconRect(class UTexture2D* Tex, float X, float Y, float W, float H, const FLinearColor& Tint);
	void DrawCupsRow(float X, float Y, float CupSize, int32 Filled, EHAlign Align = EHAlign::Left);

	// ---- 即時模式 UI 互動（主選單／ESC 選單共用；每幀 BeginUiFrame 後才可用）----
	FVector2D MousePos = FVector2D::ZeroVector;
	bool bClickThisFrame = false;
	bool bClickConsumed = false;
	double LastUiClickTime = -1.0;

	void BeginUiFrame();

	// 即時模式按鈕：畫＋判定一次完成；回傳「本幀被點下」。
	// bOnLight＝畫在白卡上（墨字墨填）；否則畫在深底上（紙字紙填）。
	bool Button(const FString& Label, float CenterX, float Y, float W, float H,
		bool bEnabled = true, bool bAccent = false, bool bOnLight = false);

	// 左右調整列：回傳 -1／0／+1
	int32 AdjustRow(const FString& Label, const FString& Value, float CenterX, float Y,
		bool bLeftEnabled = true, bool bRightEnabled = true);

	void DrawBigTitle(const FString& Text, float CenterX, float Y, float SizePx, const FLinearColor& Color);

	// ---- 畫面（相位 × 角色）----
	void DrawTopBar(const class ANiceInkGameState* GS, const class ANiceInkPlayerState* MyPS, class ANiceInkCharacter* MyChar);
	void DrawCenterBanners(const ANiceInkGameState* GS);

	// 相位轉換音效（client 端輪詢偵測；無聲甦醒相關轉換刻意無音）
	void TickAudioCues(const ANiceInkGameState* GS);
	ENiceInkPhase LastPhaseSeen = static_cast<ENiceInkPhase>(0); // Lobby
	int32 LastTourWorkSeen = INDEX_NONE;
	bool bPhaseSeeded = false;

	// 大廳（Lobby 相位）：玩家列表＋主機開始提示
	void DrawLobbyPanel(const ANiceInkGameState* GS);

	// ESC 系統選單：繼續／靈敏度／音量／離開房間（任何相位可開）
	void DrawSystemMenu(class ANiceInkCharacter* MyChar);
	void DrawAccusePanel(const ANiceInkGameState* GS, class ANiceInkCharacter* MyChar);
	void DrawPostGamePanel(class ANiceInkCharacter* MyChar);
	void DrawBottomHint(const FString& Text, const FLinearColor& Color);

	// 鎖定中唯一的常駐狀態（09-02 畫面清單＝Docs/DRAW_HUD_INVENTORY.md）：
	// 一枚墨杯 chip（當前色×濃度＋百分比）。筆名與色票列全部退役——筆在針尖
	// 的視覺本來就不同、顏色在落點指示上已經有了，只有濃度需要持久記憶
	//（誤用濃度是單向不可逆的）。
	void DrawInkChip(const class ANiceInkCharacter* MyChar);

	// 鍵帽圖形（Meccha 實物語言）：小圓角方塊＋鍵名。**鍵位要畫成鍵盤上的樣子**，
	// 寫成句子裡的一個英文詞玩家不會把它讀成「一顆可以按的鍵」。回傳寬度。
	float DrawKeycap(float X, float Y, const FString& Key, bool bAccent = false);


	// 常駐操作列（右緣縱列，鍵帽＋動詞，隨狀態增減）——形式照 Meccha 實物：
	// 它是常駐的，但小、圖像化、貼邊；差別不在常駐與否，在句子 vs 鍵帽。
	void DrawControlStrip(const class ANiceInkGameState* GS, class ANiceInkCharacter* MyChar);

	// 墨杯盤（09-02）：10 色 × 3 稀釋度的杯陣＋托盤游標——版面與角色端命中測試
	// 共用 FNiInkTrayLayout::Compute（版面寫兩份必有一邊會舊）
	void DrawInkTray(const class ANiceInkCharacter* MyChar);

	// 一枚墨杯（雙色地＋真半透明墨）——cluster 與托盤共用同一套透明度語言
	void DrawInkCup(float X, float Y, float W, float H,
		const FLinearColor& Base, int32 TierIdx);

	// （07-29：可畫域標記改制為皮膚上的 veil 殼——角色端 UpdateReachVeilShell；
	// 螢幕空間版 DrawReachVeil 退役：蓋到地板/與收筆閘兩套來源互相說謊）

	// 沉睡端全套：視覺全遮蔽黑屏＋醉夢圓形迷宮＋姿勢面板（SPEC 定案 #3、#30/#31）
	void DrawVictimSleepUI(class ANiceInkCharacter* MyChar, const class ANiceInkGameState* GS, const class ANiceInkPlayerState* MyPS);

	// 兇手轉盤（SPEC 定案 #31）：滾輪選度數、5 秒自動送出——只有兇手本人看得到
	void DrawTrapDial(const class ANiceInkCharacter* MyChar);

	void DrawBlindOverlay(const class ANiceInkCharacter* MyChar);
	// MyPS 參數已於 09-03 移除：唯一的消費者是「湊近」提示，而那份已併進控制列
	void DrawInkCrosshair(const class ANiceInkGameState* GS);
	void DrawDebugPanel(const class ANiceInkGameState* GS, const class ANiceInkPlayerState* MyPS, class ANiceInkCharacter* MyChar);

	FString GetPhaseLabel(ENiceInkPhase Phase) const;
	// 2026-09-04（照抄 Meccha 的三段式）：上緣＝現在該做什麼、右下＝這個相位怎麼贏。
	// 相位名從上緣搬到右下規則塊的標題——上緣講**動作**，名詞放在規則塊裡。
	FString GetPhaseImperative(const class ANiceInkGameState* GS,
		class ANiceInkCharacter* MyChar, bool bIsVictim) const;
	void DrawRulesBlock(const class ANiceInkGameState* GS,
		class ANiceInkCharacter* MyChar, bool bIsVictim);
	// glyph＝鍵帽（有刻字）或滑鼠圖（沒有刻字）；回傳寬度
	float DrawInputGlyph(float X, float Y, const TCHAR* Key, class UTexture2D* Tex, bool bAccent);
};
