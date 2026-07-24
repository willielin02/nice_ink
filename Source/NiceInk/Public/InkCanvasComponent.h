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

	// 4096（07-22 六修 user「近看鋸齒」）：紋素密度 0.617→1.234 px/mm、筆寬
	// 2.35→4.7px＝鋸齒尺度減半。代價=VRAM 每身 Marker+Tattoo 34→134MB、六人房
	// ~800MB（fullbright 專案 GPU 閒置可吞；嫌重退 3072）。筆寬/針距全是 UV/實體
	// 域常數＝解析度改動零耦合；RT=各端快取、筆劃向量=真相＝跨端一致不受影響。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ink", meta = (ClampMin = "256", ClampMax = "4096"))
	int32 RenderTargetResolution = 4096;

	// 固定麥克筆筆寬（UV 半徑）。SPEC v3.1：細筆尖——皮膚是跨場資源，細筆控制通膨。
	// sumo 均勻密度圖集實測 0.617 px/mm @2048 → 0.000584 UV 半徑 ≈ 3.8mm 簽字筆
	//（筆實體半徑 1.938mm 不變；char17 時代 0.898 px/mm → 0.00085）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink", meta = (ClampMin = "0.0005", ClampMax = "0.05"))
	float MarkerUvRadius = 0.000584f;

	// --- Shader＝細針點排針（07-23 十一版 user 規格：「超級小、細、密集、半透明
	// 的點；弧形排針中間約 30% 不透明度、愈往外愈透明、最外層 5%」）：真打霧的
	// 構成=密集半透明墨點（微觀是點、讀感是面——每針的墨在皮下暈開且密到互融）。
	// **半透明點會在畫布上真實疊加融合**（胡椒點版的不透明點只能靠肉眼混色=本作
	// 視距死路）：密度夠=平滑灰面+細緻針點紋理，疊趟平滑變深。
	// 墨進霧層（銳化不咬半透明——線層銳化以 0.5 為門檻，30% 的點會被整片擦掉）。---

	// 排半寬（UV；1.5cm ⇒ 帶寬 3cm）——與 Character.ShaderBrushRadiusCm（HUD 圈）同步
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink", meta = (ClampMin = "0.001", ClampMax = "0.02"))
	float ShaderRowHalfWidthUv = 0.00452f;

	// 每排針點數（24 ⇒ 帶內橫向點距 ~1.25mm）
	// 勞動量校準（user 定案「5 趟近實心」）：覆蓋期望 λ=(K×πr²)/(排距×帶寬)≈1.0、
	// 單趟平均上色 ≈ 1-exp(-λ×ᾱ) ≈ 31% ⇒ 5 趟 ≈ 85% 近實心
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink", meta = (ClampMin = "4", ClampMax = "40"))
	int32 ShaderRowDotCount = 24;

	// 針點半徑（UV；1.8mm 直徑——覆蓋率的主槓桿；再小=幾何覆蓋率塌掉、費力）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink", meta = (ClampMin = "0.00005", ClampMax = "0.002"))
	float ShaderStippleUvRadius = 0.000271f;

	// 單點不透明度：沿排弧形衰減（中央→邊緣）＝排針接觸壓力剖面=帶緣羽化
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink", meta = (ClampMin = "0.01", ClampMax = "1"))
	float ShaderStippleAlphaCenter = 0.45f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink", meta = (ClampMin = "0.0", ClampMax = "1"))
	float ShaderStippleAlphaEdge = 0.08f;

	// 冷墨底色（逐通道 max 疊在筆色上）：半透明純黑疊暖膚色=棕=髒讀感；
	// 真墨在皮下散射偏藍（Tyndall）——黑墨拉到暗藍灰、彩墨幾乎不受影響
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink")
	FLinearColor ShaderMistCoolFloor = FLinearColor(0.04f, 0.055f, 0.10f, 0.0f);

	// 霧層 RT（十一版升 4096=細針點解析需求；VRAM 每身 +50MB、六人房 +300MB）
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ink", meta = (ClampMin = "256", ClampMax = "4096"))
	int32 MistRenderTargetResolution = 4096;

	// 針型→UV 半徑查表（段落內插 fallback 用；排針走 StampNeedleDot）
	float NeedleUvRadius(EInkNeedle Needle) const
	{
		return MarkerUvRadius;
	}

	UFUNCTION(BlueprintPure, Category = "Ink")
	UTextureRenderTarget2D* GetMistRenderTarget() const { return MistRT; }

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
	UFUNCTION(BlueprintCallable, Category = "Ink")
	void BeginStroke(int32 AuthorId, FLinearColor Color, FVector2D UV, bool bDotStroke = false,
		EInkNeedle Needle = EInkNeedle::Liner);

	UFUNCTION(BlueprintCallable, Category = "Ink")
	void AddStrokePoint(int32 AuthorId, FVector2D UV);

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
	UFUNCTION(BlueprintCallable, Category = "Ink")
	bool ExportLayersToPng(const FString& AbsolutePathPrefix) const;

private:
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> MarkerRT;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> TattooRT;

	// 霧層（07-23 三版）：Shader 筆劃的專屬渲染快取——與線層分離＝銳化不咬霧
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> MistRT;

	// 淡化 Work 的暫存合成畫布（先滿透明度畫筆劃，再整張以工作透明度疊進 TattooRT，
	// 避免半透明 stamp 重疊處的堆疊條紋）。
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> ScratchRT;

	// 程序生成的圓形麥克筆頭（實心核心＋約 2px 抗鋸齒邊緣），無資產依賴。
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> NibTexture;

	// 軟霧筆頭（余弦鐘形衰減到零＝airbrush 讀感；峰值不透明度由 stamp 時的
	// 色彩縮放給、貼圖只存形狀）
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> SoftNibTexture;

	// 細針點排條帶（十一版效能修）：整排 20 顆軟點＋弧形透明度＋抖動預烘成
	// 8 個變體堆疊的條帶紋理——每排 1 個旋轉 tile 取代 20 個 item（robo superfast
	// 實錘 item 提交成本拖垮幀率）；變體由 UV 雜湊選=決定性不變
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> StippleRowTexture;

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

	UTextureRenderTarget2D* CreateLayerRT(const TCHAR* DebugName, int32 Resolution);
	UTexture2D* GetOrCreateNibTexture();
	UTexture2D* GetOrCreateSoftNibTexture();
	UTexture2D* GetOrCreateStippleRowTexture();
	FInkWork* FindWork(int32 WorkId);
	FInkWork& GetOrCreateActiveWork(int32 AuthorId);

	static float LaserOpacity(int32 LaserLevel);
	static FVector2D ClampUV(FVector2D UV);

	// 把折線（依 MaxUvSegmentLength 斷筆規則）stamp 進指定 Canvas；
	// bDots＝點刺筆劃：逐點蓋章、點間永不內插；Needle＝該筆劃的針型。
	void StampPolyline(UCanvas* Canvas, const FVector2D& CanvasSize, const TArray<FVector2D>& Points, const FLinearColor& Color, bool bDots, EInkNeedle Needle) const;
	// 針型分派的單針蓋章：Liner=實心圓（麥克筆寬）；Shader=細針點排（RowDirUv=
	// 行進方向、排垂直於它；null=無方向資訊（筆劃首點）→只落中央一點。方向由
	// 相鄰兩點推導＝重放/碳黑/跨端從同一份點序列得到同一排、零新資料）
	void StampNeedleDot(UCanvas* Canvas, const FVector2D& CanvasSize, const FVector2D& UV, const FLinearColor& Color, EInkNeedle Needle, const FVector2D* RowDirUv) const;
	void StampMistRow(UCanvas* Canvas, const FVector2D& CanvasSize, const FVector2D& UV, const FLinearColor& Color, const FVector2D* RowDirUv) const;
	// 縫區排章（07-24 跨縫制）：沿表面攤平補丁逐點落墨——點沿真實表面跨縫映射
	// ＝縫兩側自動接續、不可能蓋到圖集上的無關島。回 false=退回平面條帶。
	bool StampMistRowOnSurface(UCanvas* Canvas, const FVector2D& CanvasSize, const FVector2D& UV, const FLinearColor& Ink, const FVector2D& RowDirUv) const;
	void StampDot(UCanvas* Canvas, const FVector2D& CanvasSize, const FVector2D& UV, const FLinearColor& Color, float UvRadius) const;
	void StampSegment(UCanvas* Canvas, const FVector2D& CanvasSize, const FVector2D& From, const FVector2D& To, const FLinearColor& Color, float UvRadius) const;

	// 即時作畫用：針型路由（Liner→MarkerRT、Shader→MistRT）。RowDirUv=排向脈絡。
	void StampIntoLayerRT(const FVector2D& From, const FVector2D& To, const FLinearColor& Color, bool bDotOnly, EInkNeedle Needle, const FVector2D* RowDirUv = nullptr);

	// 重播時的針型過濾：麥克筆重建分兩趟（線層只畫 Liner、霧層只畫 Shader）；
	// 刺青層一趟全畫（碳黑的霧=軟黑填色直接進 TattooRT，銳化不咬刺青層）
	enum class ENeedleFilter : uint8 { All, LinerOnly, ShaderOnly };
	void DrawWorkStrokes(UCanvas* Canvas, const FVector2D& CanvasSize, const FInkWork& Work, const FLinearColor& OverrideColor, bool bUseOverrideColor, ENeedleFilter Filter) const;
	static bool SaveRTToPng(UTextureRenderTarget2D* RT, const FString& AbsoluteFilePath);
};
