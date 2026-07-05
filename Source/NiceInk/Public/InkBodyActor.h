#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InkBodyActor.generated.h"

class UInkCanvasComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;
class UTexture2D;

// 可被畫的身體：靜態網格（受害者昏迷不動，Phase 1 用固定姿勢）＋墨水畫布。
// BodyMaterial 需有兩個貼圖參數（MarkerRTParam / TattooRTParam），
// BeginPlay 時以 Dynamic Material Instance 接上兩張層 RT。
UCLASS()
class NICEINK_API AInkBodyActor : public AActor
{
	GENERATED_BODY()

public:
	AInkBodyActor();

	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ink")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ink")
	TObjectPtr<UInkCanvasComponent> InkCanvas;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink")
	TObjectPtr<UMaterialInterface> BodyMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink")
	FName MarkerRTParam = TEXT("MarkerRT");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink")
	FName TattooRTParam = TEXT("TattooRT");

	// 每位玩家的臉貼圖（FacePipeline 產出，FaceUV 通道取樣）與膚色
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink|Player")
	TObjectPtr<UTexture2D> FaceTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink|Player")
	FLinearColor SkinTone = FLinearColor(0.62f, 0.42f, 0.30f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink|Player")
	FName FaceTexParam = TEXT("FaceTex");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink|Player")
	FName SkinToneParam = TEXT("SkinTone");

	// 繪畫圖集所在的 UV 通道。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink", meta = (ClampMin = "0", ClampMax = "7"))
	int32 UvChannel = 0;

	// 世界座標 -> 身體 UV（自研解析：最近三角形 + 重心插值，直讀網格 CPU 資料。
	// 不用 FindCollisionUV——它的 FaceIndex->UV 對應在本網格上損壞，回傳亂 UV）。
	UFUNCTION(BlueprintCallable, Category = "Ink")
	bool ResolveBodyUV(const FVector& WorldPosition, FVector2D& OutUV);

private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DynamicBodyMaterial;

	struct FCachedTri
	{
		FVector A, B, C;
		FVector2D UVA, UVB, UVC;
	};
	TArray<FCachedTri> CachedTris;
	bool bTriCacheBuilt = false;

	bool BuildTriCache();
};
