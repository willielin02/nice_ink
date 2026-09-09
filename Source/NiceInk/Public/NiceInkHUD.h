#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "HAL/IConsoleManager.h"
#include "NiceInkHUD.generated.h"

enum class ENiceInkPhase : uint8;
// 操作提示表要把「動詞」存成字串表的鍵（NiceInkLocText.h）。前置宣告而不是
// include：標頭層不需要那張 13 語表，只需要知道這個列舉的底層型別。
enum class ENiLocKey : uint8;
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
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void DrawHUD() override;

	/** Slate 層要用同一座複合字體矩陣（局內與選單只有一套字體人格） */
	UFont* GetUiFont() const { return UiFont; }

protected:
	// 局內 HUD 的 Slate 根節點（2026-09-08 起：排版與文字歸 Slate，幾何仍在 canvas）。
	// 不是 UPROPERTY——Slate widget 由 shared ptr 管生命週期。
	TSharedPtr<class SNiHudRoot> HudRoot;

protected:
	// ---- 設計 token：字階（Slate 字級，乘 UiScale）／對齊 ----
	// （protected：主選單 HUD 繼承共用同一套 token helpers——樣式只有一套）
public:
	enum class ETextTier : uint8 { Display, Title, Body, Small };
protected:
	enum class EHAlign : uint8 { Left, Center, Right };

	float UiScale = 1.0f; // ClipY / 1080：所有尺寸的唯一縮放來源

	// 文字投影開關：投影是給疊在 3D 場景上的字用的；平面深底（主選單）或
	// 深色面板上開投影＝小字邊緣髒掉（08-06 user 抓「字雜亂」的主因）
	bool bTokShadows = true;

	// 字體人格（2026-09-05）：**一款遊戲只有一種字體人格，兩個載體共用**。
	// 這是照抄 Meccha 的**規則**（他們全站單一圓體、零襯線）；**值**是我們自己的
	// ——他們的人格是兒童廣告顏料，我們的是墨 ⇒ 明朝體。所以對齊的動作不是把
	// 選單的明朝體拿掉，是把它推進局內（此前選單 10 個字級角色有 5 個明朝體，
	// 局內四個 tier 連 serif 旗標都沒有＝一款遊戲兩種人格）。
	// 掛在最底層原語上、用 TGuardValue 圈範圍——與 ChromeAlphaMul／bMirrorSuspended
	// 同一個 pattern，呼叫端不必逐個改顏色。
	// 分工：**聲部**（相位名／倒數／揭曉橫幅＝這款遊戲在說話）＝明朝體；
	//       **工具字**（動詞／鍵名／數值／狀態）＝圓體（小字級的可讀性優先）。
	bool bSerifFace = false;

	// 鍵位狀態（2026-09-05；照抄 Meccha 的狀態系統）：他們的右緣是
	// **白＝可用／灰＝不可用（鍵帽與象形圖一起變灰）／黃＝一次性動作／
	// 綠膠囊＝切換中** 四態，而我們此前只有一個 `bAccent` 布林、語義還是
	// 「這條最重要」＝一個通道兩個語義（UI_SYSTEM §4.4 早就要求收斂）。
	// **刻意只做三態**：`ToggleOn` 在我們的遊戲裡沒有消費者——雷射（L）與搖夢（G）
	// 都是一次性動作、裝睡（SHIFT）是按住不是切換。§4.2 鐵則「軸不存在的時候，
	// 它的輸入、它的格子、它的操作表那一行都不該存在」同樣適用於狀態通道本身：
	// 造一個沒有人用的視覺狀態，就是替下一個人埋一個「這個灰色是什麼意思」。
public:
	enum class ENiKeyState : uint8
	{
		Available,    // 現在可按（紙色鍵帽、白色滑鼠圖、紙色動詞）
		Unavailable,  // 現在不可按（鍵帽／glyph／動詞**一起**淡下去——只淡一個會讀成排版錯誤）
		OneShot,      // 一次性動作（酒金鍵帽）——按下去就發生、且要花掉某種東西
	};

protected:

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
	// Lucide 線圖示（2026-09-06 二批；ISC）：一家圖示、按名惰性載入。/Game/UI/Icons/T_Ico_<name>
	UPROPERTY() TMap<FName, TObjectPtr<UTexture2D>> LucideCache;
	UTexture2D* Ico(const TCHAR* Name);
	UTexture2D* ActionIcon(ENiLocKey Label);   // 操作提示動詞→圖示（沒有＝nullptr）
	void DrawRevealBand(float Y0, float Y1);   // 揭曉／結局橫幅後方的全寬暗帶（三批）
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
	// runtime 生成的**上緣壓暗漸層**（1×64，垂直）。用貼圖而不是疊 N 個 DrawRect：
	// 疊出來的那一版被 robo 的 `cruise tipSpd` 契約抓到（2.05 對上限 2.00）——
	// 那條契約只有 0.5% 餘裕，每幀成本會沿著針的離散步進洩漏進手感。
	UPROPERTY() TObjectPtr<UTexture2D> ScrimTex;
	// 墨＝全站唯一的簽名材質（2026-09-07 大改；Tools/AssetPrep/ink_ui_textures.py）：
	// 印章墨漬／一筆刷痕／滲墨邊／軟墨點。白底 alpha、畫時吃 tint。
	UPROPERTY() TObjectPtr<UTexture2D> InkSplatTex;
	UPROPERTY() TObjectPtr<UTexture2D> InkBrushTex;
	UPROPERTY() TObjectPtr<UTexture2D> InkEdgeTex;
	UPROPERTY() TObjectPtr<UTexture2D> InkDotTex;
	void EnsureUiAssets();

	// ---- 墨的原語（2026-09-07）----
	float InkIn01(double SinceS, float DurS) const;                       // smoothstep 0→1
	void DrawInkLine(float X, float Y, float W, float H, const FLinearColor& Color, float Frac = 1.0f); // 一筆刷痕（Frac＝畫到幾成）
	void DrawInkBand(float Y0, float Y1, float Alpha);                     // 全寬暗帶＋上下滲墨邊
	void DrawBottomScrim(float TopY);                                       // 下緣漸層（與 DrawTopScrim 對稱）
	// 作品的螢幕包圍盒（筆劃點 UV→世界→投影）＋手刷的框（2026-09-07：巡禮／指認／場間）
	bool ComputeWorkScreenBox(class ANiceInkCharacter* WorkOwner, int32 WorkId, FBox2D& Out);
	void DrawWorkFrame(const FBox2D& Box, float Alpha);
	void DrawWorkFocusFrame(const class ANiceInkGameState* GS, class ANiceInkCharacter* MyChar);
	void DrawBodyMap(const class ANiceInkCharacter* MyChar);                 // 鎖定中：你在身體的哪裡
	void DrawLaserTags(class ANiceInkCharacter* MyChar);                     // 場間：碳黑刺青旁的雷射標籤

	// ---- 現金跳字（無主色之後「剛剛什麼變了」全靠動態）----
	int32 LastFrameWorkId = INDEX_NONE;
	double FrameShownAt = -1.0;
	int32 LastCashSeen = INT32_MIN;
	int32 CashDelta = 0;
	double CashDeltaAt = -1.0;

	// ---- ESC 選單頁（Root／怎麼玩／設定）----
	enum class ESysMenuPage : uint8 { Root, HowTo, Settings };
	ESysMenuPage SysMenuPage = ESysMenuPage::Root;
public:
	void SetSysMenuPage(int32 Page) { SysMenuPage = static_cast<ESysMenuPage>(FMath::Clamp(Page, 0, 2)); }
protected:

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
	// 相位切換淡入的乘數（2026-09-05）：DrawHUD 每幀由 PhaseChangedAt 算出，
	// 與 ChromeAlphaMul 相乘後才進原語——兩者分開是因為 ChromeAlphaMul 在各區塊
	// 以 TGuardValue 設**絕對值**，淡入若也走它會被蓋掉。
	float ChromeAlphaBase = 1.0f;
	double PhaseChangedAt = -1.0;
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

	// 左右調整列：回傳 -1／0／+1

	void DrawBigTitle(const FString& Text, float CenterX, float Y, float SizePx, const FLinearColor& Color);
	FVector2D MeasureBig(const FString& Text, float SizePx);
	float BigCapTopOffset(float SizePx, float& OutCapH);
	// 行框頂 → 基線（＝全大寫字串的**墨跡底**）。堆疊文字要量墨跡不能量行框：
	// 行框底下那截下伸空白會偷偷加進間距（2026-09-08 血價：小標與房號之間我寫 10，眼睛看到 16）。
	float TokBaselineFromTop(ETextTier Tier, bool bBold);
	// 行框頂 → 大寫墨跡頂（DrawTok 版的 BigCapTopOffset；只在展示體有效，內文體回 0）
	float TokCapTopOffset(ETextTier Tier, bool bBold);
	// 依可用寬度斷行（局內說明用；Slate 那邊改用 AutoWrapText）
	TArray<FString> WrapTok(const FString& Text, ETextTier Tier, float MaxW, bool bBold);

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

	// 房主現在能不能開局（人數＋全員臉齊）。**兩個消費者**：底部那行狀態，
	// 以及操作列 ENTER 的可按／不可按。同一個判準寫兩份必有一邊會舊
	//（那會表現成「字說還差一個人、鍵卻是亮的」）。
	bool CanHostStartMatch(const ANiceInkGameState* GS) const;

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
	float DrawKeycap(float X, float Y, const FString& Key,
		ENiKeyState State = ENiKeyState::Available);
	// 平面滑鼠（2026-09-06）：Button 0=左鍵 1=右鍵 2=滾輪；回傳寬度
	float DrawMouseGlyph(float LeftX, float Y, float H, int32 Button, ENiKeyState State);

	// 上緣漸層壓暗（2026-09-05）。**這是整份對齊工作裡唯一一條「不能照抄」的**：
	// Meccha 的「常駐 chrome 無面板」成立，前提是他們的世界是暗的——實測他們的
	// chrome 背景亮度 27／80／84／155（最亮是打光的綠地板），而我們的道場有一整面
	// 打亮的白障子牆，實測 208 ⇒ 上緣祈使句對比只有 **1.27**（近乎隱形）。
	// 我先前假設「他們的白字描邊比較強、照抄就好」——**實測推翻**：他們字邊比背景
	// 暗 1.7~27 階，我們 7.7~10，同一個量級。差的不是手法，是前提。
	// 所以這裡自己發明，但守住他們那條規則的**意思**：常駐＝無邊界，模態＝有面板。
	// 漸層沒有邊界（alpha 連續收到 0），所以它不是面板。
	void DrawTopScrim(float BottomY);

	// 姿勢／移動群（2026-09-05；Meccha 的底部中央橫排）：他們有**兩個**操作群
	// ——右緣＝模式與動作（直排）、底部中央＝姿勢與移動（橫排、鍵帽在上動詞在下）。
	// 我們此前把 `WASD 起身` 塞進右緣直列＝把身體狀態混進工具列。
	void DrawPostureCluster(const class ANiceInkGameState* GS, class ANiceInkCharacter* MyChar);

	// 右下角比分（2026-09-05；Meccha 的 `残り人数` ＋巨大數字）：
	// **罰酒杯就是我們的比分**——三杯結束這一局。此前它是三個 20px 圖示擠在
	// 受害者臉旁邊，畫面上沒有任何東西大到會被一眼看見。
	// 與規則塊**分時共用同一個角**（Meccha 同款：搜索階段教規則、開打後放比分）：
	// 判準＝`杯數 == 0`（第一回合沒有賭注可以顯示 ⇒ 這時候正好教規則；
	// 第一次猜錯之後這個角就永久變成計分板）。
	void DrawScoreCorner(const class ANiceInkGameState* GS);


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
	// glyph＝鍵帽（有刻字）或滑鼠圖（沒有刻字）；回傳寬度。
	// **X＝右緣時自己往左扣寬度**；bLeftAnchor=true 改成 X＝左緣（底部橫排用）。
	float DrawInputGlyph(float X, float Y, const TCHAR* Key, class UTexture2D* Tex,
		ENiKeyState State, bool bLeftAnchor = false);
	float MeasureInputGlyph(const TCHAR* Key, class UTexture2D* Tex) const;

	// ---- 操作提示的單一正本（2026-09-05；UI_SYSTEM §4.3 的第一號工程）----
	// 此前每個呼叫點手寫 `Rows.Add({ TEXT("Q"), ... })`，散在一個 120 行的 switch 裡。
	// 沒有這一層，**手把支援或改鍵功能一到就要全站重寫**——這是建它的唯一理由，
	// 不是為了好看。同時它讓「這個相位有哪些鍵」變成可以被閘門讀的資料。
public:
	struct FNiControlHint
	{
		const TCHAR* Key;        // 鍵名（鍵盤有刻字＝寫字）；nullptr＝用 Tex
		UTexture2D* Tex;         // 滑鼠圖（沒有刻字＝畫圖）
		ENiLocKey   Label;       // 動詞（必須進字串表）
		ENiKeyState State;
		bool        bPosture;    // true＝身體姿勢/移動 ⇒ 底部中央橫排；false＝右緣直排
	};
	// 依（相位 × 角色 × 狀態）解出這一幀所有的操作提示——右緣與底部共用同一份，
	// 差別只在 bPosture 這一欄。**一件事只講一次**由「同一份來源」在構造上保證。
	void BuildControlHints(const class ANiceInkGameState* GS, class ANiceInkCharacter* MyChar,
		TArray<FNiControlHint>& Out) const;

	// ---- Slate 層的取用面（2026-09-08）----
	// 這些原本是 protected 的內部工具，改成 public 讓 widget 讀同一份來源：
	// 「一件事只講一次」的保證來自共用來源，不是來自我記得要同步兩份。
	bool CanHostStart(const class ANiceInkGameState* GS) const { return CanHostStartMatch(GS); }
	UTexture2D* GetActionIcon(ENiLocKey Label) { return ActionIcon(Label); }
	UTexture2D* GetIcon(const TCHAR* Name) { return Ico(Name); }
	UTexture2D* GetCashIcon() const { return IconCash; }
	UTexture2D* GetCupIcon() const { return IconCup; }
	UTexture2D* GetInkBrushTex() const { return InkBrushTex; }
	UTexture2D* GetInkDotTex() const { return InkDotTex; }
	UTexture2D* GetInkSplatTex() const { return InkSplatTex; }
	double GetPhaseChangedAt() const { return PhaseChangedAt; }
	/** 開發者遙測的內容（ni.DebugHud 1）；排版歸 Slate（SNiDebugPanel） */
	void BuildDebugLines(TArray<FString>& Out) const;
	/** 某一件作品在螢幕上的框（世界錨定標籤要用；裝置像素） */
	bool GetWorkScreenBox(int32 WorkId, FBox2D& Out);
	int32 GetLaserCost() const;

	// 底部狀態句（2026-09-08）：canvas 端的 DrawBottomHint 是指令式的，Slate 是保留模式
	// ⇒ 這裡放「這一幀想說的話」，呼叫點原樣不動，widget 每幀讀它。DrawHUD 開頭清空。
	FString BottomHintText;
	FLinearColor BottomHintColor = FLinearColor::White;
	FString GetImperative(const class ANiceInkGameState* GS, class ANiceInkCharacter* MyChar, bool bIsVictim) const
	{
		return GetPhaseImperative(GS, MyChar, bIsVictim);
	}
	/** 滑鼠圖是哪一顆：0 左／1 右／2 滾輪／-1 不是滑鼠 */
	int32 MouseButtonOf(const UTexture2D* Tex) const
	{
		if (Tex == InMouseRight) { return 1; }
		if (Tex == InMouseScroll) { return 2; }
		if (Tex == InMouseLeft || Tex == InMouseMove) { return 0; }
		return -1;
	}
	/** 落筆時整條 chrome 淡下去（canvas 端是 ChromeAlphaMul；Slate 端綁在 widget 的 opacity 上） */
	float GetChromeAlpha() const;
	/** 那顆頭的肖像（頭像亭；缺席回 nullptr ⇒ 呼叫端不畫臉） */
	/**
	 * 臉像的來源：**一次回傳「哪張圖」與「要不要裁」**（2026-09-08 血價）。
	 *
	 * 這兩件事一開始是兩個函式（GetFacePortrait ／ FacePortraitNeedsCrop），而它們**必須互相同意**。
	 * 頭像亭還在烘焙的那個時間窗裡它們會不同意：解析器一路退回「名冊的整張臉貼圖」，
	 * 判定式卻只看到「這個席位有臉資料」就回答「不用裁」⇒ 拿一張要裁的圖不裁 ⇒
	 * 整張臉縮進 64px 的框裡＝一顆很小的頭浮在空白中（user 實拍指出）。
	 * **凡是「兩個回答必須一致」的東西，就不該是兩個函式。**
	 *
	 * 亭的肖像已經是裁好的頭形（不裁）；名冊整張臉貼圖要照 canvas 端同一組 FaceUV
	 * （(0.30,0.22)+(0.40,0.40)）裁出臉。
	 */
	UTexture* GetFaceSource(const class APlayerState* PS, bool& bOutNeedsCrop);
	/** 舊呼叫點的別名（不需要知道裁切與否時用） */
	UTexture* GetFacePortrait(const class APlayerState* PS)
	{
		bool bUnused = false;
		return GetFaceSource(PS, bUnused);
	}
};
