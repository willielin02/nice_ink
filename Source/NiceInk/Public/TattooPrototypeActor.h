#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TattooPrototypeActor.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
class UTattooComponent;
class APlayerController;

UCLASS()
class NICEINK_API ATattooPrototypeActor : public AActor
{
	GENERATED_BODY()

public:
	ATattooPrototypeActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tattoo")
	TObjectPtr<UStaticMeshComponent> PlaneMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tattoo")
	TObjectPtr<UTattooComponent> TattooComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo")
	TObjectPtr<UMaterialInterface> PlaneMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo")
	FName TattooTextureParameter = TEXT("TattooTexture");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo")
	bool bStartAutoDemoOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo|Prototype")
	bool bEnableMousePaintingInPIE = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo|Prototype")
	bool bEnablePrototypeHotkeys = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo|Palette")
	TArray<FLinearColor> PrototypePalette;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tattoo|Palette")
	int32 SelectedPaletteIndex = 0;

	UFUNCTION(BlueprintCallable, Category = "Tattoo|Palette")
	bool SelectPaletteColor(int32 PaletteIndex);

	UFUNCTION(BlueprintPure, Category = "Tattoo|Palette")
	FLinearColor GetSelectedPaletteColor() const;

	UFUNCTION(BlueprintPure, Category = "Tattoo|Palette")
	TArray<FLinearColor> GetPaletteColors() const { return PrototypePalette; }

private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DynamicPlaneMaterial;

	bool bMousePainting = false;

	void HandlePrototypeHotkeys(APlayerController* PlayerController);
	bool ResolveMousePaintUV(FVector2D& OutUV) const;
};
