#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InkTestPawn.generated.h"

class AInkBodyActor;
class UCameraComponent;
class UInkCanvasComponent;

// Phase 1 自測 Pawn：第一人稱飛行視角 + 準星麥克筆。
// 全部輸入用輪詢（IsInputKeyDown / GetInputMouseDelta），不依賴任何輸入綁定設定。
//
// 操作：WASD/QE 移動（Shift 加速）、滑鼠視角、左鍵按住＝畫
// 調色盤：1-9,0 選色
// 規則自測熱鍵：X 洗掉全部麥克筆、C 我的傑作轉碳黑、L 雷射第一幅碳黑、
//               P 鎖第一幅碳黑為永久、R 下一回合、F10 輸出 QA PNG
UCLASS()
class NICEINK_API AInkTestPawn : public APawn
{
	GENERATED_BODY()

public:
	AInkTestPawn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ink")
	TObjectPtr<UCameraComponent> Camera;

	// 我是誰（筆劃作者 ID；Phase 3 起改用 PlayerState PlayerId）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink")
	int32 TestAuthorId = 0;

	// 麥克筆觸及距離（公分）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink", meta = (ClampMin = "50", ClampMax = "2000"))
	float PaintReach = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ink")
	TArray<FLinearColor> Palette;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ink")
	int32 SelectedColorIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "50", ClampMax = "3000"))
	float MoveSpeed = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float LookSensitivity = 1.6f;

	UFUNCTION(BlueprintPure, Category = "Ink")
	FLinearColor GetCurrentColor() const;

	UFUNCTION(BlueprintPure, Category = "Ink")
	int32 GetPaletteSize() const { return Palette.Num(); }

private:
	float CameraPitch = 0.0f;
	bool bPainting = false;

	// 目前正在畫的畫布（跨畫布拖曳時要正確斷筆）
	TWeakObjectPtr<UInkCanvasComponent> ActiveCanvas;

	void PollMovement(APlayerController* PC, float DeltaSeconds);
	void PollPalette(APlayerController* PC);
	void PollDebugOps(APlayerController* PC);
	void PollPainting(APlayerController* PC);

	UInkCanvasComponent* TraceForCanvas(FVector2D& OutUV) const;
	void StopPainting();
	AInkBodyActor* FindAnyBodyActor() const;
};
