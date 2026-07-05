#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InkTypes.h"
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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ink", meta = (ClampMin = "256", ClampMax = "4096"))
	int32 RenderTargetResolution = 1024;

	// 固定麥克筆筆寬（UV 半徑）。SPEC：同一種筆觸、同一粗度。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink", meta = (ClampMin = "0.001", ClampMax = "0.05"))
	float MarkerUvRadius = 0.006f;

	// 單段 UV 距離超過此值視為跨 UV 島跳躍：不內插、直接斷筆重起。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink", meta = (ClampMin = "0.01", ClampMax = "0.5"))
	float MaxUvSegmentLength = 0.08f;

	// 碳黑刺青墨色（轉換後不再使用玩家原色）。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink")
	FLinearColor CarbonInkColor = FLinearColor(0.02f, 0.03f, 0.045f, 1.0f);

	UFUNCTION(BlueprintPure, Category = "Ink")
	UTextureRenderTarget2D* GetMarkerRenderTarget() const { return MarkerRT; }

	UFUNCTION(BlueprintPure, Category = "Ink")
	UTextureRenderTarget2D* GetTattooRenderTarget() const { return TattooRT; }

	// --- 作畫（每位作者可各自有一條進行中的筆劃；多作者同時畫互不干擾） ---

	UFUNCTION(BlueprintCallable, Category = "Ink")
	void BeginStroke(int32 AuthorId, FLinearColor Color, FVector2D UV);

	UFUNCTION(BlueprintCallable, Category = "Ink")
	void AddStrokePoint(int32 AuthorId, FVector2D UV);

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
	UFUNCTION(BlueprintCallable, Category = "Ink")
	void WashAllMarker();

	UFUNCTION(BlueprintCallable, Category = "Ink")
	void SetRoundIndex(int32 NewRoundIndex);

	UFUNCTION(BlueprintPure, Category = "Ink")
	int32 GetRoundIndex() const { return RoundIndex; }

	// 清空兩張 RT 並依 Works 重播全部筆劃。
	UFUNCTION(BlueprintCallable, Category = "Ink")
	void RebuildRenderTargets();

	// QA：輸出 <前綴>_marker.png / <前綴>_tattoo.png（絕對路徑前綴）。
	UFUNCTION(BlueprintCallable, Category = "Ink")
	bool ExportLayersToPng(const FString& AbsolutePathPrefix) const;

private:
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> MarkerRT;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> TattooRT;

	// 淡化 Work 的暫存合成畫布（先滿透明度畫筆劃，再整張以工作透明度疊進 TattooRT，
	// 避免半透明 stamp 重疊處的堆疊條紋）。
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> ScratchRT;

	// 程序生成的圓形麥克筆頭（實心核心＋約 2px 抗鋸齒邊緣），無資產依賴。
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> NibTexture;

	UPROPERTY()
	TArray<FInkWork> Works;

	int32 NextWorkId = 1;
	int32 RoundIndex = 0;

	// AuthorId -> 進行中筆劃所在的 WorkId / 上一個落點
	TMap<int32, int32> OpenStrokeWorkByAuthor;
	TMap<int32, FVector2D> LastPointByAuthor;

	UTextureRenderTarget2D* CreateLayerRT(const TCHAR* DebugName);
	UTexture2D* GetOrCreateNibTexture();
	FInkWork* FindWork(int32 WorkId);
	FInkWork& GetOrCreateActiveWork(int32 AuthorId);

	static float LaserOpacity(int32 LaserLevel);
	static FVector2D ClampUV(FVector2D UV);

	// 把折線（依 MaxUvSegmentLength 斷筆規則）stamp 進指定 Canvas。
	void StampPolyline(UCanvas* Canvas, const FVector2D& CanvasSize, const TArray<FVector2D>& Points, const FLinearColor& Color) const;
	void StampDot(UCanvas* Canvas, const FVector2D& CanvasSize, const FVector2D& UV, const FLinearColor& Color) const;
	void StampSegment(UCanvas* Canvas, const FVector2D& CanvasSize, const FVector2D& From, const FVector2D& To, const FLinearColor& Color) const;

	// 即時作畫用：把單一線段直接畫進 MarkerRT。
	void StampSegmentIntoMarkerRT(const FVector2D& From, const FVector2D& To, const FLinearColor& Color, bool bDotOnly);

	void DrawWorkStrokes(UCanvas* Canvas, const FVector2D& CanvasSize, const FInkWork& Work, const FLinearColor& OverrideColor, bool bUseOverrideColor) const;
	static bool SaveRTToPng(UTextureRenderTarget2D* RT, const FString& AbsoluteFilePath);
};
