#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NiceInkTypes.h"
#include "TattooComponent.generated.h"

class UCanvas;
class UTexture2D;
class UTextureRenderTarget2D;
class UTattooMarker;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTattooStrokeApplied, const FTattooStroke&, Stroke);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTattooMarkerChanged, ENiceInkMarkerType, MarkerType);

UCLASS(ClassGroup = (NiceInk), Blueprintable, meta = (BlueprintSpawnableComponent))
class NICEINK_API UTattooComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTattooComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(BlueprintAssignable, Category = "Tattoo")
	FOnTattooStrokeApplied OnStrokeApplied;

	UPROPERTY(BlueprintAssignable, Category = "Tattoo")
	FOnTattooMarkerChanged OnMarkerChanged;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tattoo")
	int32 RenderTargetResolution = 1024;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo")
	FLinearColor CurrentColor = FLinearColor::Black;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo")
	ENiceInkMarkerType CurrentMarkerType = ENiceInkMarkerType::FineMarker;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo|Prototype")
	bool bAutoDemoDrawing = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tattoo|Prototype", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float DemoAngularSpeed = 1.25f;

	UFUNCTION(BlueprintCallable, Category = "Tattoo")
	UTextureRenderTarget2D* InitializeRenderTarget(int32 Resolution = 1024);

	UFUNCTION(BlueprintCallable, Category = "Tattoo")
	void SetRenderTarget(UTextureRenderTarget2D* InRenderTarget);

	UFUNCTION(BlueprintPure, Category = "Tattoo")
	UTextureRenderTarget2D* GetRenderTarget() const { return RenderTarget; }

	UFUNCTION(BlueprintCallable, Category = "Tattoo")
	void ClearTattoo();

	UFUNCTION(BlueprintCallable, Category = "Tattoo")
	void SelectMarker(ENiceInkMarkerType MarkerType);

	UFUNCTION(BlueprintCallable, Category = "Tattoo")
	void SelectColor(FLinearColor InkColor);

	UFUNCTION(BlueprintCallable, Category = "Tattoo")
	void StartTattooing(const FHitResult& HitResult);

	UFUNCTION(BlueprintCallable, Category = "Tattoo")
	void BeginTattooingAtUV(FVector2D UV, float Pressure = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "Tattoo")
	void StopTattooing();

	UFUNCTION(BlueprintCallable, Category = "Tattoo")
	void SubmitStrokeAtUV(FVector2D UV, float Pressure = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "Tattoo")
	void SubmitStrokeBatch(const TArray<FTattooStroke>& Strokes);

	UFUNCTION(BlueprintCallable, Category = "Tattoo")
	void ApplyStrokeLocal(const FTattooStroke& Stroke);

	UFUNCTION(BlueprintCallable, Category = "Tattoo")
	void ApplyStrokeBatchLocal(const TArray<FTattooStroke>& Strokes);

	UFUNCTION(BlueprintCallable, Category = "Tattoo|Persistence")
	bool SaveTattooToPng(const FString& AbsoluteFilePath) const;

	UFUNCTION(BlueprintCallable, Category = "Tattoo|Persistence")
	UTexture2D* LoadTattooTextureFromPng(const FString& AbsoluteFilePath) const;

	UFUNCTION(BlueprintCallable, Category = "Tattoo|Persistence")
	bool LoadTattooFromPng(const FString& AbsoluteFilePath);

	UFUNCTION(Server, Unreliable)
	void ServerApplyStrokes(const TArray<FTattooStroke>& Strokes);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastApplyStrokes(const TArray<FTattooStroke>& Strokes);

private:
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> RenderTarget;

	UPROPERTY(Replicated)
	FTattooStrokeHistory ReplicatedHistory;

	bool bIsTattooing = false;
	bool bHasLastStrokeUV = false;
	float DemoTime = 0.0f;
	float StrikeAccumulator = 0.0f;
	FVector2D LastStrokeUV = FVector2D::ZeroVector;
	FVector2D LastDemoUV = FVector2D::ZeroVector;

	FMarkerConfig GetCurrentMarkerConfig() const;
	FTattooStroke MakeStroke(FVector2D UV, float Pressure) const;
	void DrawStroke(UCanvas* Canvas, const FVector2D& CanvasSize, const FTattooStroke& Stroke) const;
	void GenerateDemoStrokes(float DeltaTime);
	bool ValidateStroke(const FTattooStroke& Stroke) const;
};
