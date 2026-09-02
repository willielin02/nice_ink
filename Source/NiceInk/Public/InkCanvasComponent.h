#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InkTypes.h"
#include "InkBodyComponent.h" // FInkSurfacePatch（縫區表面落墨快取）
#include "Kismet/KismetRenderingLibrary.h" // FDrawToRenderTargetContext（批次蓋章成員）
#include "InkCanvasComponent.generated.h"

class UCanvas;
class UTexture2D;
class UTextureRenderTarget2D;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnInkCanvasChanged);

// 一具身體的墨水畫布。
//
// 筆劃向量資料（Works）是唯一真相源；兩張 Render Target 只是渲染快取：
//   MarkerRT — 麥克筆層（State == Marker，玩家原色）
//   TattooRT — 刺青層（Carbon / Permanent，碳黑墨色，透明度隨雷射淡化）
// 洗掉／轉換／雷射一律改資料後 RebuildRenderTargets() 重播。
//
// Phase 1 為單機版：Begin/Add/EndStroke 直接本地呼叫。
// Phase 3 連線化時在外層包 RPC 複製筆劃資料，本元件邏輯不變。
UCLASS(ClassGroup = (NiceInk), Blueprintable, meta = (BlueprintSpawnableComponent))
class NICEINK_API UInkCanvasComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInkCanvasComponent();

	virtual void BeginPlay() override;

	UPROPERTY(BlueprintAssignable, Category = "Ink")
	FOnInkCanvasChanged OnCanvasChanged;

	// 圖層生滅通知（2026-08-25 惰性配置制）：材質綁著的那張貼圖換人了，消費端
	// 必須重綁。刻意與 OnCanvasChanged 分開——後者每一筆都廣播，重綁不該進熱路徑。
	UPROPERTY(BlueprintAssignable, Category = "Ink")
	FOnInkCanvasChanged OnLayersChanged;

	// 「這一層目前不存在」時綁給材質的替身：4×4 全透明。
	// 與「配好卻整張清成透明的 4096 RT」在取樣上逐位相同（常數的雙線性內插還是
	// 同一個常數）＝惰性配置對畫面是恆等變換。
	static UTexture2D* GetEmptyInkTexture();

	// 4096（07-22 六修 user「近看鋸齒」）：紋素密度 0.617→1.234 px/mm、筆寬
	// 2.35→4.7px＝鋸齒尺度減半。代價=VRAM 每身 Marker+Tattoo 34→134MB、六人房
	// ~800MB（fullbright 專案 GPU 閒置可吞；嫌重退 3072）。筆寬/針距全是 UV/實體
	// 域常數＝解析度改動零耦合；RT=各端快取、筆劃向量=真相＝跨端一致不受影響。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ink", meta = (ClampMin = "256", ClampMax = "4096"))
	int32 RenderTargetResolution = 4096;

	// 固定麥克筆筆寬（UV 半徑）。SPEC v3.1：細筆尖——皮膚是跨場資源，細筆控制通膨。
	// sumo 均勻密度圖集實測 0.617 px/mm @2048；08-02 user 定值筆寬 3.0mm（原 3.9mm
	// /0.000584）→ 半徑 1.5mm × 0.617 ÷ 2048 = 0.000452 UV。
	// 同步錨：Character.TattooNibDiameterCm（守恆式速度換算）必須跟著改。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink", meta = (ClampMin = "0.0003", ClampMax = "0.05"))
	float MarkerUvRadius = 0.000452f;

	// --- Shader＝細針點排針（07-23 十一版 user 規格：「超級小、細、密集、半透明
	// 的點；弧形排針中間約 30% 不透明度、愈往外愈透明、最外層 5%」）：真打霧的
	// 構成=密集半透明墨點（微觀是點、讀感是面——每針的墨在皮下暈開且密到互融）。
	// **半透明點會在畫布上真實疊加融合**（胡椒點版的不透明點只能靠肉眼混色=本作
	// 視距死路）：密度夠=平滑灰面+細緻針點紋理，疊趟平滑變深。
	// 墨進霧層（銳化不咬半透明——線層銳化以 0.5 為門檻，30% 的點會被整片擦掉）。---

	// **圓章的半徑**（UV；1cm ⇒ 落墨總寬 2cm——08-02 user 終值；與
	// Character.ShaderBrushRadiusCm（HUD 圈）同步）。
	// 2026-09-01 語義變更：原為「排帶的半寬」（長方形排針的橫向半長），排帶制
	// 退役後它就是圓章的半徑——HUD 那個圈第一次真的等於落墨範圍。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink", meta = (ClampMin = "0.001", ClampMax = "0.02"))
	float ShaderRowHalfWidthUv = 0.00301f;

	// 每排針點數（49＝user 定值 49RM 真實排針規格；帶內槽距 ~0.4mm、點徑 1.3mm
	// ⇒ 相鄰點重疊 ~3 倍＝墨在真皮層暈開互融——正常視距讀感=平滑灰色水洗面、
	// 看不見單點；「點」只活在細噪質感層）
	// 勞動量校準：λ=(K×πr²)/(排距×帶寬)
	// =49×π×0.065²/(0.2×2.0)≈1.63
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink", meta = (ClampMin = "4", ClampMax = "128"))
	int32 ShaderRowDotCount = 49;

	// 針點半徑（UV；1.3mm 直徑＝暈開後的墨點腳印——單點越小越淡越不可見，
	// 下限=霧層紋素地板 1.23px/mm）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink", meta = (ClampMin = "0.00005", ClampMax = "0.002"))
	float ShaderStippleUvRadius = 0.0002f;

	// **一枚章在帶心的濃度**（2026-09-01 改制；原語義＝單顆針點的濃度，針點已退役）。
	// 帶內均勻、鐘形退役（十六版 user 定案「打霧=塗色」：漸層剖面與塗均勻物理衝突）。
	//
	// 這是打霧唯一的濃度旋鈕，也是**兩支筆質感能不能對上的那個數字**：
	// 黑墨疊在膚色上＝ink×α+skin×(1−α)，ink≈0 ⇒ α 不夠滿時結果就是「被乘暗的皮膚」
	//（與皮膚同色相＝污漬讀感），只有 α→1 中性黑才主導。實測割線墨區 α 中位 1.00、
	// 62% 面積 α>0.9；改制前的打霧中位 0.71、只有 14% ⇒ 一半以上的面積根本不是墨。
	//
	// 導出式（沿路 3 枚章重疊、包絡 cos 給 1.0/0.25/0.25）：
	//   一趟核心 α = 1−(1−a)(1−0.25a)²
	//   a=0.49 ⇒ 0.60（暗皮膚）｜a=0.70 ⇒ 0.78（墨主導）｜**a=1.0 ⇒ 1.00** ← 現值
	//
	// **1.0 ＝ 冪等（2026-09-01 三修，user：「顏色要填得均勻真的不容易」）**：
	// 病根是 over 疊合——**同一處經過兩次就比一次深**，而手控制不了重疊量 ⇒ 均勻
	// 是手感成就而非構造。舊註解宣稱「相鄰掃軌在羽化區互補成平＝塗均勻由構造保證、
	// 不靠手穩」是**假的**：離線量測（scratchpad/band2.py）那只在唯一一個掃軌間距
	// 上成立，間距一偏（8~20mm 全掃）密度起伏最差 32.8%。
	// 帶心 α=1 ⇒ 第一趟就飽和 ⇒ **再掃只擴大範圍、不再加深**（1 疊 1 還是 1）＝
	// over 在核心區退化成冪等 ⇒ **重疊變免費**，玩家「多掃一點保險」的直覺從被
	// 懲罰（條紋）變成正確。均勻自此是數學性質，與手穩無關。
	// 連帶：割線也是 α=1 的實心墨 ⇒ 兩支筆**同墨同濃度**，差別只剩筆寬 3mm vs 20mm。
	// **代價（user 已知並同意）：漸層（暈し）作為表達手段消失**——濃淡不可控，
	// 要淡只能少畫面積。想要回層次就把這個值往回拉（0.70／0.49 見上表）。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink", meta = (ClampMin = "0.01", ClampMax = "1"))
	float ShaderStippleAlphaCenter = 1.0f;

	// **圓章外圈的軟邊寬（mm）——不是「暈し」**（2026-09-02 勘誤）。
	//
	// 它的職責只有一個：讓筆劃邊緣不是鋸齒。**筆的邊緣要接近硬的**，因為只有硬邊
	// 才跟割線讀成同一罐墨，也才在 20 秒巡禮／靜音小螢幕上讀得出形狀。
	//
	// **5.0 是錯的（09-01 當日短暫值），已回 2.0**：當時我把「暈し」誤當成「筆的
	// 邊緣要糊」，把軟邊放到半徑的一半 ⇒ user viewport 直接讀成**噴槍**。
	// **筆永遠糊 ≠ 暈し**——暈し 是作畫者在邊界上做出來的漸層（由「在哪裡收手」
	// 決定），不是筆的固有屬性；把它烘進筆裡，等於每一筆都被迫糊。
	// 同批也釐清：梳齒不是軟邊窄造成的，是**長方形圖章**造成的（已由圓章修掉）——
	// 那一刀不該順手動這個值。
	//
	// 記帳：真正可控的 暈し（濃淡由玩家選、重疊不加深）需要「離散濃度檔＋max 合成」，
	// 而 UE Canvas 沒有 max 混合模式（已查列舉：SE_BLEND_MAX 是計數哨兵不是模式）
	// ⇒ 要自訂合成路徑。未做，記在這裡。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink", meta = (ClampMin = "0.5", ClampMax = "10"))
	float ShaderFillFeatherMm = 2.0f;

	// 真皮層暈開半徑（mm；十四版建制、十五版 0.4→0.8）：每顆針點的墨在烘製端
	// 做等向高斯擴散——「暈開」必須在高解析烘製域完成再正確積分下取樣（RT 紋素
	// 0.81mm 裝不下 1.3mm 點的漸層；欠取樣 aliasing=十三版刮痕真兇）。
	// 0.8=點完全互融、帶內噪聲歸零（十五版實測 0.4 殘留 std/mean 0.15 的紋素噪聲
	// ——貼膚特寫下被材質 bilinear 放大成沿格線的脊狀怪絲；0.8mm 紋素畫布上
	// 「細噪質感」不可保留，user 規格本就是「看不見點的平滑水洗面」）。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float ShaderMistBleedMm = 0.8f;

	// 冷墨底色（逐通道 max 疊在筆色上）——**2026-09-01 起預設關閉（0,0,0）**。
	//
	// 原委：07-24 十一版加它，是為了治 user 抓的「髒（棕色瘀青感）」——半透明
	// 純黑疊暖膚色＝棕。原理沒錯（真墨在皮下散射偏藍＝Tyndall）。
	// **但它是墨色上的硬地板，不是濃度的函數**，所以疊到滿也照樣被拉藍：
	// 實測（Saved/robo_*_mist.png 反預乘）打霧疊滿＝sRGB(56,66,89) 板岩藍，
	// 而割線的同一支「1 號黑」＝sRGB(35,35,35)。**同一個調色盤鍵、兩種墨**，
	// 且填色再怎麼疊都回不到線的黑 ⇒ 和彫的「地が濃く」在構造上做不出來。
	// 彩墨也不豁免（紅的 G 通道 0.0144→0.055，ΔE 9.4）。
	//
	// user 定案（09-01）：**兩支筆的融合度優先於單支筆的擬真**。
	// 常數解不存在——離線優化（scratchpad/floor.py）掃遍 9³ 組合證明：任何
	// 能在 α≈0.5 壓掉棕色的地板，都會同時把 α=1 那端拉走（α=0.5 時皮膚佔一半，
	// 要移動合成色就得大幅移動墨色）。所以只有兩個自洽的選擇，取融合：
	//   關掉 ⇒ 兩支筆逐位同墨（疊滿都是 35,35,35、彩墨 ΔE=0）；代價＝薄墨處
	//          回到暖灰／棕（**但割線的抗鋸齒邊本來就是這個棕**，兩端因此一致）。
	//   開著 ⇒ 薄墨漂亮，但濃墨端永遠分家（＝改前的狀態）。
	// **正解是把冷色做成濃度的函數並同時作用於線層與霧層**（薄→藍灰、厚→筆色），
	// 那要動 M_InkBodyChar 的圖；5.7 的 python 列舉不到材質節點，且 MarkerSharpen
	// 不是可調參數 ⇒ 留給有編輯器手動作業的那一輪。**這個旋鈕保留＝那一輪的入口。**
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink")
	FLinearColor ShaderMistCoolFloor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);

	// --- 墨的光學（2026-09-02；密度→外觀的唯一函數，烘在 CPU 光柵器的 LUT）---
	// 薄墨偏冷（Tyndall：黑墨稀時皮下散射偏藍灰）、疊滿回到筆色——上面那顆常數
	// 地板想做而做錯位置的事，做成**濃度的函數**就自洽了（09-01 離線優化證明常數
	// 解不存在：能壓掉中間態棕色的地板必同時拉走疊滿端）。值＝07-24 驗證過的冷色。
	// 彩墨由光柵器內的色度閘豁免（稀的紅＝粉紅不是藍）。
	//
	// **Strength 預設 0＝關（09-02 user 指令「所有顏色同色、只差透明度」）**：
	// 三檔之間色相完全不動。代價記帳（量測過、非猜測）：淡黑疊暖膚＝與皮膚同色相
	// ＝可能讀成瘀青棕（07-24「髒」定罪、09-01 α<0.7 實測）——viewport 若證實髒
	// 讀感，把 Strength 調回 1.0 即恢復薄墨偏冷。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MistToneCoolStrength = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink")
	FLinearColor MistToneCoolColor = FLinearColor(0.040f, 0.055f, 0.100f);

	// 色相曲線指數：pow(密度, γ) 決定「多快回到筆色」（1=線性；<1 更快回、>1 更慢）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink", meta = (ClampMin = "0.1", ClampMax = "4"))
	float MistToneCurveGamma = 1.0f;

	// 皮膚錨定紋理：乘在墨密度上的畫布座標噪場（畫布＝UV0＝皮膚 ⇒ 錨在皮膚）。
	// 顆粒錨皮膚＝讀成「墨活在皮膚裡」；錨筆劃＝讀成髒筆（五輪雲斑教訓）。
	// 振幅刻意小（5%）；尺度 ~3.6mm＝4.4 紋素（>2 紋素才畫得出來，針點 1mm 級
	// 畫不出來就不畫＝電視低解析鐵則）。0＝關。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink", meta = (ClampMin = "0.0", ClampMax = "0.3"))
	float MistGrainAmp = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink", meta = (ClampMin = "2.0", ClampMax = "32.0"))
	float MistGrainPeriodPx = 4.4f;

	// 霧層 RT（十一版升 4096=細針點解析需求；VRAM 每身 +50MB、六人房 +300MB）
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ink", meta = (ClampMin = "256", ClampMax = "4096"))
	int32 MistRenderTargetResolution = 4096;

	// 針型→UV 半徑查表（段落內插 fallback 用；排針走 StampNeedleDot）
	float NeedleUvRadius(EInkNeedle Needle) const
	{
		return MarkerUvRadius;
	}

	// 霧層現制（09-02）＝CPU 光柵器上傳的 transient 貼圖（材質端與舊 RT 同語義）
	UFUNCTION(BlueprintPure, Category = "Ink")
	UTexture2D* GetMistLayerTexture() const { return MistTex; }

	// 單段 UV 距離超過此值視為跨 UV 島跳躍：不內插、直接斷筆重起。
	// 0.012 ≈ 4cm 皮膚距離（0.617px/mm@2048）——合法段（幀間細分 ≤1cm）遠低於此；
	// 舊值 0.08 ≈ 16cm＝縫區短跳全數漏過被內插成毛邊/橫劃（07-20 直接畫制實錘）。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink", meta = (ClampMin = "0.005", ClampMax = "0.5"))
	float MaxUvSegmentLength = 0.012f;

	// 碳黑刺青墨色（轉換後不再使用玩家原色）。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink")
	FLinearColor CarbonInkColor = FLinearColor(0.02f, 0.03f, 0.045f, 1.0f);

	UFUNCTION(BlueprintPure, Category = "Ink")
	UTextureRenderTarget2D* GetMarkerRenderTarget() const { return MarkerRT; }

	UFUNCTION(BlueprintPure, Category = "Ink")
	UTextureRenderTarget2D* GetTattooRenderTarget() const { return TattooRT; }

	// --- 作畫（每位作者可各自有一條進行中的筆劃；多作者同時畫互不干擾） ---

	// bDotStroke（2026-07-22 刺青手感）：點刺筆劃——逐點蓋章不連線（見 FInkStroke 註）。
	// 玩家作畫路徑一律 true；robo/證據/舊管線維持 false＝折線語義。
	// Needle（07-23 雙針制）：針型存進筆劃，渲染半徑查表。
	// Flow（07-24 十二版）：該點出墨流量 0–255（手速→濃淡；預設滿濃度=舊行為）。
	// Tier（09-02 灰洗分檔）：整條筆劃的濃度檔 0–255（只有 Shader 消費；預設實檔）。
	UFUNCTION(BlueprintCallable, Category = "Ink")
	void BeginStroke(int32 AuthorId, FLinearColor Color, FVector2D UV, bool bDotStroke = false,
		EInkNeedle Needle = EInkNeedle::Liner, uint8 Flow = 255, uint8 Tier = 255);

	UFUNCTION(BlueprintCallable, Category = "Ink")
	void AddStrokePoint(int32 AuthorId, FVector2D UV, uint8 Flow = 255);

	// 批次蓋章（十一版效能修）：一批 AddStrokePoint 只開關一次 RT canvas context
	// ——細針點排（每點 20 tile）逐點開關 4096 霧層 context 會拖垮幀率（robo
	// superfast 實錘 848→454 排）。Begin 以 AuthorId 的開筆針型定層；批內
	// StampIntoLayerRT 同層直畫、異層/未開退回逐點路徑（冪等安全）。
	void BeginStampBatchFor(int32 AuthorId);
	void EndStampBatch();

	UFUNCTION(BlueprintCallable, Category = "Ink")
	void EndStroke(int32 AuthorId);

	// --- 規則操作（以 Work 為原子） ---

	UFUNCTION(BlueprintPure, Category = "Ink")
	const TArray<FInkWork>& GetWorks() const { return Works; }

	UFUNCTION(BlueprintPure, Category = "Ink")
	bool GetWork(int32 WorkId, FInkWork& OutWork) const;

	UFUNCTION(BlueprintPure, Category = "Ink")
	TArray<int32> GetWorkIdsByState(EInkWorkState State) const;

	// 作者在當前回合的麥克筆傑作（沒有則回傳 INDEX_NONE）。
	UFUNCTION(BlueprintPure, Category = "Ink")
	int32 GetActiveWorkId(int32 AuthorId) const;

	// 該作者進行中筆劃的最新落點（實體筆對位用；無進行中筆劃回傳 false）
	UFUNCTION(BlueprintPure, Category = "Ink")
	bool GetLastPointForAuthor(int32 AuthorId, FVector2D& OutUV) const;

	// 猜錯 → 真作者刷碳黑：整幅傑作轉為碳黑刺青。
	UFUNCTION(BlueprintCallable, Category = "Ink")
	bool ConvertWorkToCarbon(int32 WorkId);

	// 終局鈦白：碳黑 → 永久（淡化級凍結）。
	UFUNCTION(BlueprintCallable, Category = "Ink")
	bool LockWorkPermanent(int32 WorkId);

	// 場間大廳雷射：碳黑 +1 級淡化，第 3 級整幅移除。永久刺青無效。
	UFUNCTION(BlueprintCallable, Category = "Ink")
	bool ApplyLaserToWork(int32 WorkId);

	// 回合結算：所有麥克筆傑作洗掉（碳黑／永久不受影響）。
	// 證據標記（保留作者 ID）也是 Marker 態，隨之一同洗掉（SPEC：僅該回合有效）。
	UFUNCTION(BlueprintCallable, Category = "Ink")
	void WashAllMarker();

	// 打稿制（07-25）：只洗稿線筆劃（Stencil 針型）＋清空掉的 Marker 作品——
	// 甦醒收束時呼叫（GameMode EnterTour → Multicast）
	UFUNCTION(BlueprintCallable, Category = "Ink")
	void WashStencil();

	// 證據標記：以 Seed 決定的濺射圖案蓋在 UV 附近（跨端同種子＝同圖案）。
	// 噴漬＝大片不規則濺點；瘀青＝緊密圓斑。
	UFUNCTION(BlueprintCallable, Category = "Ink")
	void AddEvidenceMark(EInkEvidenceType Type, FVector2D UV, int32 Seed);

	UFUNCTION(BlueprintCallable, Category = "Ink")
	void SetRoundIndex(int32 NewRoundIndex);

	UFUNCTION(BlueprintPure, Category = "Ink")
	int32 GetRoundIndex() const { return RoundIndex; }

	// 清空兩張 RT 並依 Works 重播全部筆劃。
	UFUNCTION(BlueprintCallable, Category = "Ink")
	void RebuildRenderTargets();

	// 跨場刺青還原：整幅塞回畫布（保留原 WorkId；NextWorkId 跳過避免撞號）。
	UFUNCTION(BlueprintCallable, Category = "Ink")
	void RestoreWork(const FInkWork& Work);

	// QA：輸出 <前綴>_marker.png / <前綴>_tattoo.png（絕對路徑前綴）。
	// 惰性配置制下會先把三層都補齊再傾印——robo 契約恆得到三個檔，語義不變。
	UFUNCTION(BlueprintCallable, Category = "Ink")
	bool ExportLayersToPng(const FString& AbsolutePathPrefix);

	// 入睡瞬間預熱作畫層（Marker＋Mist）：第一針才配置 2×64MB 會當場掉一幀，
	// 而第一針正是手感最敏感的時刻。釘住到甦醒為止（見 ReleaseDrawLayerPin）。
	UFUNCTION(BlueprintCallable, Category = "Ink")
	void PrewarmDrawLayers();

	// 解除預熱釘選（甦醒）：之後這兩層的存活只由「Works 裡真的有筆劃」決定。
	UFUNCTION(BlueprintCallable, Category = "Ink")
	void ReleaseDrawLayerPin();

private:
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> MarkerRT;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> TattooRT;

	// 霧層（07-23 三版建制；**2026-09-02 改 CPU 軟體光柵器**）：Shader 筆劃的專屬
	// 渲染快取——與線層分離＝銳化不咬霧。現制＝CPU 權威緩衝（FInkMistSurface，
	// max 合成＝灰洗分檔的均勻性構造保證）＋髒區上傳到這張 transient 貼圖；
	// 材質端只看到一張貼圖＝與舊 RT 同語義（sRGB 預乘位元組）。
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> MistTex;
	TSharedPtr<class FInkMistSurface> MistSurface;
	// 刺青層重建時的霧成分暫存（碳黑的霧＝同一支 CPU 光柵器 ⇒ 轉碳黑那一刻
	// 外觀零跳變；rasterize→上傳→一次 tile 打底，液線 canvas 直畫在上）
	TSharedPtr<class FInkMistSurface> MistScratchSurface;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> MistScratchTex;
	// 重建期間把霧光柵導向 scratch（見 RebuildRenderTargets 刺青段）；null＝霧層本體
	class FInkMistSurface* MistTargetOverride = nullptr;
	// 霧批次：批內只累積髒區、EndStampBatch 一次上傳
	bool bMistBatchOpen = false;

	// 淡化 Work 的暫存合成畫布（先滿透明度畫筆劃，再整張以工作透明度疊進 TattooRT，
	// 避免半透明 stamp 重疊處的堆疊條紋）。
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> ScratchRT;

	// 程序生成的圓形麥克筆頭（實心核心＋線性覆蓋斜坡），無資產依賴。
	// 羽化帶寬以「**畫布紋素**」為單位訂定（08-29：舊版寫的是筆頭貼圖的 2px，
	// 被 17.3× 縮小後只剩 0.12 紋素＝等於沒有羽化）——常數住 .cpp 檔頭。
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> NibTexture;

	// 軟霧筆頭（余弦鐘形衰減到零＝airbrush 讀感；峰值不透明度由 stamp 時的
	// 色彩縮放給、貼圖只存形狀）
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> SoftNibTexture;

	UPROPERTY()
	TArray<FInkWork> Works;

	int32 NextWorkId = 1;
	int32 RoundIndex = 0;

	// AuthorId -> 進行中筆劃所在的 WorkId / 上一個落點
	TMap<int32, int32> OpenStrokeWorkByAuthor;
	TMap<int32, FVector2D> LastPointByAuthor;
	// AuthorId -> 排向脈絡（跨縫大跳沿用上一個有效方向；live 增量與重放同構）
	TMap<int32, FVector2D> LastRowDirByAuthor;

	// 批次蓋章狀態（BeginStampBatchFor/EndStampBatch 之間有效）
	UCanvas* BatchCanvas = nullptr;
	FVector2D BatchCanvasSize = FVector2D::ZeroVector;
	UTextureRenderTarget2D* BatchRT = nullptr;
	FDrawToRenderTargetContext BatchContext;

	// 縫區表面落墨快取（mutable：stamp 鏈 const、快取是純加速）——
	// 相鄰的排共用同一塊補丁（半徑 2.2cm、中心移 >0.5cm 才重建）
	mutable TWeakObjectPtr<UInkBodyComponent> CachedBody;
	mutable FInkSurfacePatch SeamPatch;
	mutable FVector SeamPatchCenterWorld = FVector::ZeroVector;
	mutable bool bSeamPatchValid = false;
	UInkBodyComponent* ResolveBody() const;

	// --- 惰性圖層配置（2026-08-25）---
	// 病史：四張 4096² RGBA8 各 64 MiB 在 BeginPlay 無條件配給**每一個**角色，
	// 而每個 client 都持有房裡所有人的整套 ⇒ 崩潰現場實測 Render Target 2D
	// 1034.75 MB（16 張），大廳階段卻一筆墨都還沒畫。現制＝需要哪層才配哪層。
	bool bDrawLayersPinned = false; // 受害者入睡期間釘住 Marker/Mist（防第一針掉幀）

	struct FLayerNeeds
	{
		bool bMarker = false;
		bool bMist = false;
		bool bTattoo = false;
		bool bScratch = false;
	};
	// 從 Works（唯一真相）算出哪幾層真的有東西要畫
	FLayerNeeds ComputeLayerNeeds() const;

	UTextureRenderTarget2D* EnsureMarkerRT();
	UTextureRenderTarget2D* EnsureTattooRT();
	UTextureRenderTarget2D* EnsureScratchRT();
	// 霧層（CPU 光柵器＋貼圖）配置／上傳／釋放
	bool EnsureMistLayer();
	void EnsureMistScratch();
	void FlushMistUpload();
	void ReleaseMistLayer(bool& bOutChanged);
	static UTexture2D* CreateMistTexture(UObject* Outer, const TCHAR* Name, int32 Res);
	// 以 TSharedPtr 收 surface＝把緩衝**釘住到渲染執行緒複製完成**（09-02 崩潰修：
	// 上傳是排隊的、零複製直讀緩衝指標——release/拆場搶在複製前釋放 surface
	// ＝render thread memcpy 讀已釋放記憶體＝user 目擊的 D3D12RHI ACCESS_VIOLATION）
	static void UploadMistSurface(const TSharedPtr<class FInkMistSurface>& Surface, UTexture2D* Tex, bool bFull);
	// 只放掉自己的指標、不碰 RHI 資源：材質／脖子 MID 可能還指著它，交給
	// UObject 生命週期收（消費端一重綁就沒人參照＝下次 GC 自然回收 VRAM）。
	// 手動 ReleaseResource 會在「已釋放卻仍被取樣」的那一幀出事。
	static void ReleaseLayer(TObjectPtr<UTextureRenderTarget2D>& Slot, bool& bOutChanged);

	UTextureRenderTarget2D* CreateLayerRT(const TCHAR* DebugName, int32 Resolution);
	UTexture2D* GetOrCreateNibTexture();
	UTexture2D* GetOrCreateSoftNibTexture();
	FInkWork* FindWork(int32 WorkId);
	FInkWork& GetOrCreateActiveWork(int32 AuthorId);

	static float LaserOpacity(int32 LaserLevel);
	static FVector2D ClampUV(FVector2D UV);

	// 把折線（依 MaxUvSegmentLength 斷筆規則）stamp 進指定 Canvas；
	// bDots＝點刺筆劃：逐點蓋章、點間永不內插；Needle＝該筆劃的針型；
	// PointFlow＝逐點流量（null/缺項=滿濃度，與 Points 逐索引對齊）。
	// Tier01＝該筆劃的濃度檔（09-02 分檔；只有 Shader 消費）。Canvas 對純霧重播
	// 可為 null（霧走 CPU 光柵器，不需要 canvas context）。
	void StampPolyline(UCanvas* Canvas, const FVector2D& CanvasSize, const TArray<FVector2D>& Points, const FLinearColor& Color, bool bDots, EInkNeedle Needle, const TArray<uint8>* PointFlow = nullptr, const TArray<uint8>* PointWall = nullptr, float Tier01 = 1.0f) const;
	// 針型分派的單針蓋章：Liner=實心圓（麥克筆寬）；Shader=細針點排（RowDirUv=
	// 行進方向、排垂直於它；null=無方向資訊（筆劃首點）→只落中央一點。方向由
	// 相鄰兩點推導＝重放/碳黑/跨端從同一份點序列得到同一排、零新資料）。
	// Flow＝濃度因子 0–1（手速→濃淡；Liner 忽略=機器擁有速度）。
	// Wall4＝該點的稿線牆（4 位元組編碼，見 FInkStroke::PointWall；null=無牆）。
	void StampNeedleDot(UCanvas* Canvas, const FVector2D& CanvasSize, const FVector2D& UV, const FLinearColor& Color, EInkNeedle Needle, const FVector2D* RowDirUv, float Flow = 1.0f, const uint8* Wall4 = nullptr, float Tier01 = 1.0f) const;
	void StampMistRow(const FVector2D& UV, const FLinearColor& Color, const FVector2D* RowDirUv, float Flow, const uint8* Wall4, float Tier01) const;
	void StampMistSegment(const FVector2D& From, const FVector2D& To, const FLinearColor& Color, float Tier01) const;

	// --- 稿線擋墨（2026-09-01 建制；09-02 v2＝半平面裁切）---
	// 填色掃到自己畫的稿線就停：手照樣自由亂掃，邊界由稿線保證乾淨。
	// 只擋**同一位作者**的稿線（別人的框關不住你的墨＝無圍城騷擾），
	// 且只擋 Shader（割線有沿稿自動走，語義不同）。
	//
	// **v2 為什麼是半平面（09-02，白刺定罪）**：v1 只會把章「橫向（±Perp）夾窄」，
	// 且只在行進方向 ±0.3R 的窗內查——正對稿線衝過去時線落在窗外＝查不到，圓章前半
	// 直接壓過線 ⇒ user 截圖的放射白刺（長度上限恰為一個半徑、方向垂直輪廓）。
	// 削章模型只有一個自由度＝斜的、正面的牆構造上削不出來。v2 改成：對每一枚章，
	// 收集半徑 R 內同作者稿線段的**半平面**（保留章心那一側），用它精確裁切圓章
	// ——任意方向都對、轉角兩條線自然圍出角、首點也有牆。
	struct FNiInkWallPlane
	{
		FVector2D NormalUv = FVector2D(1.0, 0.0); // 由章心指向牆的單位法線
		float DistUv = 0.0f;                      // 保留 dot(p−章心, N) <= Dist 的一側
	};
	// 收集章心 CenterUV 半徑 R（=ShaderRowHalfWidthUv）內的牆平面，聚類合併後
	// 取最近的至多 2 個（方向差 >25° 才算不同牆＝轉角）。FrameDir 只用於退化案
	//（章心正壓在稿線上＝法線不定，取線段法線朝行進前方＝保留來的那一側）。
	void ComputeStencilWallPlanes(int32 AuthorId, const FVector2D& CenterUV, const FVector2D& FrameDir,
		FNiInkWallPlane OutPlanes[2], int32& OutNum) const;
	// 平面 ↔ 逐點 4 位元組（角度相對 FrameDir、1.4° 解析度；距離 R/254、~0.04mm）。
	// FrameDir＝行進方向（首點=(1,0)）——live 與重放由同一份點序列推導＝編解碼一致。
	static void EncodeWallPlanes(const FNiInkWallPlane* Planes, int32 Num, const FVector2D& FrameDir,
		float HalfWUv, uint8 Out[4]);
	static int32 DecodeWallPlanes(const uint8* In4, const FVector2D& FrameDir, float HalfWUv,
		FNiInkWallPlane OutPlanes[2]);
	// 縫區排章（07-24 跨縫制）：沿表面攤平補丁逐點落墨——點沿真實表面跨縫映射
	// ＝縫兩側自動接續、不可能蓋到圖集上的無關島。回 false=退回平面圓章。
	// WallPlanes＝已解碼的牆平面（UV 框、相對章心；逐點測 dot>Dist 即跳過）。
	// Density＝Tier×Flow×AlphaCenter（CPU max 合成下逐點直寫目標濃度）。
	bool StampMistRowOnSurface(const FVector2D& UV, const FLinearColor& Ink, const FVector2D& RowDirUv, float Density, const FNiInkWallPlane* WallPlanes, int32 NumWallPlanes) const;
	void StampDot(UCanvas* Canvas, const FVector2D& CanvasSize, const FVector2D& UV, const FLinearColor& Color, float UvRadius) const;
	void StampSegment(UCanvas* Canvas, const FVector2D& CanvasSize, const FVector2D& From, const FVector2D& To, const FLinearColor& Color, float UvRadius) const;

	// 即時作畫用：針型路由（Liner→MarkerRT、Shader→MistRT）。RowDirUv=排向脈絡；
	// Flow=該點濃度因子（手速→濃淡）。
	void StampIntoLayerRT(const FVector2D& From, const FVector2D& To, const FLinearColor& Color, bool bDotOnly, EInkNeedle Needle, const FVector2D* RowDirUv = nullptr, float Flow = 1.0f, const uint8* Wall4 = nullptr, float Tier01 = 1.0f);

	// 重播時的針型過濾：麥克筆重建分兩趟（線層只畫 Liner、霧層只畫 Shader）；
	// 刺青層一趟全畫（碳黑的霧=軟黑填色直接進 TattooRT，銳化不咬刺青層）
	enum class ENeedleFilter : uint8 { All, LinerOnly, ShaderOnly };
	void DrawWorkStrokes(UCanvas* Canvas, const FVector2D& CanvasSize, const FInkWork& Work, const FLinearColor& OverrideColor, bool bUseOverrideColor, ENeedleFilter Filter) const;
	static bool SaveRTToPng(UTextureRenderTarget2D* RT, const FString& AbsoluteFilePath);
};
