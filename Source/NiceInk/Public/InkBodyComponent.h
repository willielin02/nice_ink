#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "InkBodyComponent.generated.h"

class UInkCanvasComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTexture2D;
struct FNiceInkAvatarDef;

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

	// 診斷：回報快取三角形數、最近距離與 UV（robo 測試用）
	UFUNCTION(BlueprintCallable, Category = "Ink")
	FString DebugResolveBodyUV(const FVector& WorldPosition);

	// 彎腰用骨骼身體要共用同一個 MID（同一組 RT／貼圖參數）
	UMaterialInstanceDynamic* GetDynamicMaterial() const { return DynamicBodyMaterial; }

	// UV -> 世界座標（找包含該 UV 的三角形做重心插值）。
	// 巡禮鏡頭用：從筆劃 UV 反推身體表面位置。UV 圖集有縫隙：找不到回傳 false。
	UFUNCTION(BlueprintCallable, Category = "Ink")
	bool ResolveUVToWorld(FVector2D UV, FVector& OutWorldPosition);

private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DynamicBodyMaterial;

	bool bEyesClosed = false;

	struct FCachedTri
	{
		FVector A, B, C;
		FVector2D UVA, UVB, UVC;
	};
	TArray<FCachedTri> CachedTris;
	bool bTriCacheBuilt = false;

	bool BuildTriCache();
	void ApplyFaceTexture();
};
