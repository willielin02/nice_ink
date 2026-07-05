#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InkBodyActor.generated.h"

class UInkBodyComponent;
class UInkCanvasComponent;
class UMaterialInterface;
class UTexture2D;

// 可被畫的靜態身體（原型關卡測試用；正式遊戲的身體掛在 NiceInkCharacter 上）。
// 實作已抽到 UInkBodyComponent；本 actor 只負責把編輯器指定的
// 貼圖／膚色餵給元件並綁上畫布。
UCLASS()
class NICEINK_API AInkBodyActor : public AActor
{
	GENERATED_BODY()

public:
	AInkBodyActor();

	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ink")
	TObjectPtr<UInkBodyComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ink")
	TObjectPtr<UInkCanvasComponent> InkCanvas;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink")
	TObjectPtr<UMaterialInterface> BodyMaterial;

	// 每位玩家的臉貼圖（FacePipeline 產出，FaceUV 通道取樣）與膚色
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink|Player")
	TObjectPtr<UTexture2D> FaceTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink|Player")
	FLinearColor SkinTone = FLinearColor(0.62f, 0.42f, 0.30f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink|Player")
	TObjectPtr<UTexture2D> EyeMaskTexture;

	// 世界座標 -> 身體 UV（轉發給 UInkBodyComponent）
	UFUNCTION(BlueprintCallable, Category = "Ink")
	bool ResolveBodyUV(const FVector& WorldPosition, FVector2D& OutUV);
};
