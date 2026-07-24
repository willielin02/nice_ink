#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "InkBodyComponent.generated.h"

class UInkCanvasComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTexture2D;
struct FNiceInkAvatarDef;

// 連續作畫補丁（2026-07-18 平面畫布制，user 定案「連續、不預切」）：
// 以皮膚上任一點為中心、沿表面 BFS 鉸鏈展開（hinge-unfold）攤平成 2D 圖表。
// Chart 座標＝公分平面（原點=選點、+Y≈朝頭側）；頂點以「位置焊接」跨 UV 縫連續
// ——攤平畫布天生看不到圖集縫。獨佔判定＝快取三角形位圖相交（精確、零模糊）。
struct FInkSurfacePatch
{
	struct FTriCorner
	{
		FVector2D Chart = FVector2D::ZeroVector; // 攤平座標（cm）
		FVector2D UV0 = FVector2D::ZeroVector;   // 墨水圖集
		FVector2D UV1 = FVector2D::ZeroVector;   // FaceUV（臉貼圖）
		FColor Color = FColor::White;            // FaceMask 頂點色
	};
	struct FPatchTri
	{
		int32 CacheTri = INDEX_NONE; // UInkBodyComponent 快取索引（獨佔位圖的座標系）
		FTriCorner C[3];
	};

	int32 SeedTri = INDEX_NONE;
	float RadiusCm = 0.0f;
	TArray<FPatchTri> Tris;
	TBitArray<> TriMask;           // 佔用位圖（與快取三角形數同長）
	TArray<FVector> BoundarySegs;  // 邊界線段（元件本地空間、成對存放；HUD 圈用）

	bool IsValid() const { return Tris.Num() > 0; }
	void Reset() { SeedTri = INDEX_NONE; Tris.Reset(); TriMask.Reset(); BoundarySegs.Reset(); }

	// 攤平座標 → 墨水 UV0（補丁內三角形掃描＋重心插值）。落在補丁外＝false（邊緣裁筆）。
	bool ChartToUV0(const FVector2D& ChartPt, FVector2D& OutUV0) const;

	// 兩補丁是否共用任何皮膚三角形（獨佔判定；不同網格＝不重疊）
	static bool Overlaps(const FInkSurfacePatch& A, const FInkSurfacePatch& B);
};

// 可被畫的身體網格：材質綁定（Marker/Tattoo RT、臉貼圖、膚色、眼球禁畫遮罩）
// ＋ 世界座標→UV 解析 ＋ 睜/閉眼貼圖切換。
// 掛在 InkBodyActor（靜態展示）或 NiceInkCharacter（玩家）上都可用；
// 元件會動（角色走動、受害者被搬動）沒關係——解析走元件的當前變換。
UCLASS(ClassGroup = (NiceInk), meta = (BlueprintSpawnableComponent))
class NICEINK_API UInkBodyComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	UInkBodyComponent();

	// 基底材質（M_InkBodyChar）。留空則用網格上的槽 0 材質。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink")
	TObjectPtr<UMaterialInterface> BodyMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink|Player")
	TObjectPtr<UTexture2D> FaceOpenTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink|Player")
	TObjectPtr<UTexture2D> FaceClosedTexture;

	// 禁畫遮罩（墨水圖集 UV0 空間；白=可畫、黑=眼球）。膠帶語義，落墨路徑零判定。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink|Player")
	TObjectPtr<UTexture2D> EyeMaskTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink|Player")
	FLinearColor SkinTone = FLinearColor(0.62f, 0.42f, 0.30f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink")
	FName MarkerRTParam = TEXT("MarkerRT");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink")
	FName TattooRTParam = TEXT("TattooRT");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink")
	FName MistRTParam = TEXT("MistRT");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink")
	FName FaceTexParam = TEXT("FaceTex");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink")
	FName SkinToneParam = TEXT("SkinTone");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink")
	FName EyeMaskParam = TEXT("InkEyeMask");

	// 繪畫圖集所在的 UV 通道
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink", meta = (ClampMin = "0", ClampMax = "7"))
	int32 UvChannel = 0;

	// 名冊 avatar 套用（載入貼圖＋膚色）
	void ApplyAvatar(const FNiceInkAvatarDef& Avatar);

	// 建 MID 並綁定畫布 RT 與玩家貼圖。可重複呼叫（貼圖換了之後重綁）。
	UFUNCTION(BlueprintCallable, Category = "Ink")
	void BindCanvas(UInkCanvasComponent* Canvas);

	// 沉睡表現的核心：閉眼＝換 _Closed 臉貼圖；睜眼＝換回。
	UFUNCTION(BlueprintCallable, Category = "Ink")
	void SetEyesClosed(bool bClosed);

	// 換身體網格（站姿↔睡姿）。兩個網格共用同一套 UV 圖集，
	// 墨水 RT 原樣沿用；三角快取失效重建。
	UFUNCTION(BlueprintCallable, Category = "Ink")
	void SwapBodyMesh(UStaticMesh* NewMesh);

	UFUNCTION(BlueprintPure, Category = "Ink")
	bool AreEyesClosed() const { return bEyesClosed; }

	// 世界座標 -> 身體 UV（最近三角形 + 重心插值，直讀網格 CPU 資料。
	// 不用 FindCollisionUV——它的 FaceIndex->UV 對應在本網格上損壞，回傳亂 UV）。
	// MaxDistance：命中點離網格的容許距離（麥克筆 10cm；膠囊命中的噴射／拳腳放寬）。
	UFUNCTION(BlueprintCallable, Category = "Ink")
	bool ResolveBodyUV(const FVector& WorldPosition, FVector2D& OutUV, float MaxDistance = 10.0f);

	// C++ 過載：PreferNearUV＝縫區連續性偏好。UV 島縫上同一皮膚點到縫兩側三角形
	// 幾乎等距，浮點雜訊逐幀換島＝筆跡毛邊/橫劃（07-20 直接畫制實錘）。
	// 給了上一點：距離並列（5mm 鬆弛帶）時選 UV 離上一點近的島＝遲滯消抖。
	bool ResolveBodyUV(const FVector& WorldPosition, FVector2D& OutUV, float MaxDistance,
		const FVector2D* PreferNearUV);

	// 診斷：回報快取三角形數、最近距離與 UV（robo 測試用）
	UFUNCTION(BlueprintCallable, Category = "Ink")
	FString DebugResolveBodyUV(const FVector& WorldPosition);

	// 彎腰用骨骼身體要共用同一個 MID（同一組 RT／貼圖參數）
	UMaterialInstanceDynamic* GetDynamicMaterial() const { return DynamicBodyMaterial; }

	// UV -> 世界座標（找包含該 UV 的三角形做重心插值）。
	// 巡禮鏡頭用：從筆劃 UV 反推身體表面位置。UV 圖集有縫隙：找不到回傳 false。
	UFUNCTION(BlueprintCallable, Category = "Ink")
	bool ResolveUVToWorld(FVector2D UV, FVector& OutWorldPosition);

	// 同上並帶出該處表面法線（實體筆的立筆方向用）
	bool ResolveUVToWorldWithNormal(FVector2D UV, FVector& OutWorldPosition, FVector& OutNormal);

	// 以世界點為中心建連續作畫補丁（沿皮膚 BFS 展開攤平；跨 UV 縫連續）。
	// 決定性：同網格＋同點＋同半徑＝所有端建出同一個補丁（server 驗獨佔、client 建畫布）。
	// MaxSeedDistance：世界點離皮膚太遠＝失敗（打到別的東西）。
	bool BuildSurfacePatch(const FVector& WorldCenter, float RadiusCm, FInkSurfacePatch& OutPatch,
		float MaxSeedDistance = 10.0f);

	// --- 縫感知（07-24 排針跨縫制）：UV 圖集縫附近的面積蓋章要走表面補丁，
	// 平面蓋章會被縫裁出直線界線＋把墨蓋到圖集上排在隔壁的無關島（viewport 實錘）---

	// UV 是否落在「距縫 < ~2.5cm」的三角形上（快速路徑閘）。縫=UV 不連續的焊接邊
	// ＋快取邊界邊（褌洞/網格外緣——出界蓋章同樣是汙染）
	bool IsUVNearSeam(const FVector2D& UV);

	// UV → 快取三角形（UV 網格索引加速；圖集縫隙=INDEX_NONE）
	int32 FindTriAtUV(const FVector2D& UV);

	// 已知三角形上的 UV → 世界位置（UV 重心插值；給補丁種子——免全網格最近點掃描）
	bool UVToWorldOnTri(int32 TriIndex, const FVector2D& UV, FVector& OutWorld);

	// 以已知種子三角形建補丁（縫區逐排建補丁的成本關鍵：跳過種子掃描）
	bool BuildSurfacePatchFromTri(int32 SeedTri, const FVector& WorldCenter, float RadiusCm,
		FInkSurfacePatch& OutPatch);

private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DynamicBodyMaterial;

	bool bEyesClosed = false;

	struct FCachedTri
	{
		FVector A, B, C;
		FVector2D UVA, UVB, UVC;
		// 平面畫布制擴充：FaceUV（UV1）＋FaceMask 頂點色（攤平網格要能演臉與睜閉眼）
		FVector2D UV1A, UV1B, UV1C;
		FColor ColA, ColB, ColC;
		// 位置焊接頂點 id（跨 UV 縫連續的表面拓樸；BFS 鄰接與攤平圖表都用它）
		int32 W[3] = { INDEX_NONE, INDEX_NONE, INDEX_NONE };
	};
	TArray<FCachedTri> CachedTris;
	TArray<FVector> WeldPos;      // 焊接頂點代表位置（本地空間）
	TArray<int32> TriAdj;         // 三角形鄰接（tri*3+edge → 鄰 tri；INDEX_NONE=邊界）
	bool bTriCacheBuilt = false;

	// 縫資料（lazy；換網格失效）：每 tri 近縫旗標＋UV 網格索引
	bool bSeamDataBuilt = false;
	TArray<uint8> TriNearSeam;
	TArray<TArray<int32>> UvGridCells; // UvGridDim² 格、每格=UV bbox 蓋到的 tri 清單
	static constexpr int32 UvGridDim = 256;
	bool BuildSeamData();

	// BuildSurfacePatch 的共用核心（種子已定）
	bool BuildSurfacePatchInternal(int32 Seed, const FVector& SeedPointLocal, float RadiusCm,
		FInkSurfacePatch& OutPatch);

	bool BuildTriCache();
	void ApplyFaceTexture();
};
