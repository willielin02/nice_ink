#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NiceInkTypes.h"
#include "SelfieTypes.h"
#include "CharacterCustomizer.generated.h"

class USkeletalMeshComponent;
class UTexture2D;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAppearanceChanged);

UCLASS(ClassGroup = (NiceInk), Blueprintable, meta = (BlueprintSpawnableComponent))
class NICEINK_API UCharacterCustomizer : public UActorComponent
{
	GENERATED_BODY()

public:
	UCharacterCustomizer();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(BlueprintAssignable, Category = "Appearance")
	FOnAppearanceChanged OnAppearanceChanged;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_Appearance, Category = "Appearance")
	FCharacterAppearance Appearance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance|Morph Mapping")
	TArray<FNiceInkMorphTargetBinding> FaceMorphBindings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance|Morph Mapping")
	TArray<FNiceInkMorphTargetBinding> BodyMorphBindings;

	UFUNCTION(BlueprintCallable, Category = "Appearance")
	void ApplyPreset(int32 Index);

	UFUNCTION(BlueprintCallable, Category = "Appearance|Face")
	void SetFaceControl(ENiceInkFaceControl Control, float Value);

	UFUNCTION(BlueprintCallable, Category = "Appearance")
	void SetFaceMorph(FName MorphName, float Value);

	UFUNCTION(BlueprintCallable, Category = "Appearance|Body")
	void SetBodyControl(ENiceInkBodyControl Control, float Value);

	UFUNCTION(BlueprintCallable, Category = "Appearance")
	void SetBodyMorph(FName MorphName, float Value);

	UFUNCTION(BlueprintCallable, Category = "Appearance")
	void SetBodyType(ENiceInkBodyType BodyType);

	UFUNCTION(BlueprintCallable, Category = "Appearance|Body")
	void SetHeightScale(float HeightScale);

	UFUNCTION(BlueprintCallable, Category = "Appearance")
	void SetSkinTone(FLinearColor SkinTone);

	UFUNCTION(BlueprintCallable, Category = "Appearance|Skin")
	void SetSkinTonePreset(int32 SkinToneIndex);

	UFUNCTION(BlueprintCallable, Category = "Appearance|Skin")
	void SetSkinDetails(int32 SkinDetailIndex, float FreckleIntensity, float BlemishIntensity, float ScarIntensity, float AgeDetail);

	UFUNCTION(BlueprintCallable, Category = "Appearance|Skin")
	void SetSkinUndertone(FLinearColor SkinUndertone);

	UFUNCTION(BlueprintCallable, Category = "Appearance|Face")
	void SetEyeColor(FLinearColor EyeColor);

	UFUNCTION(BlueprintCallable, Category = "Appearance")
	void ConfirmAppearance();

	UFUNCTION(BlueprintCallable, Category = "Appearance")
	void ApplyAppearanceToMesh(USkeletalMeshComponent* MeshComponent);

	UFUNCTION(BlueprintPure, Category = "Appearance|Face")
	static FName GetFaceControlName(ENiceInkFaceControl Control);

	UFUNCTION(BlueprintPure, Category = "Appearance|Body")
	static FName GetBodyControlName(ENiceInkBodyControl Control);

	UFUNCTION(BlueprintPure, Category = "Appearance|Face")
	TArray<FName> GetFaceControlNames() const;

	UFUNCTION(BlueprintPure, Category = "Appearance|Body")
	TArray<FName> GetBodyControlNames() const;

	UFUNCTION(BlueprintPure, Category = "Appearance")
	FCharacterAppearance GetAppearanceData() const { return Appearance; }

	// --- Selfie Pipeline Integration ---

	UFUNCTION(BlueprintCallable, Category = "Appearance|Selfie")
	void ApplySelfieResult(const FSelfieResult& SelfieResult);

	UFUNCTION(BlueprintCallable, Category = "Appearance|Selfie")
	void ApplyFaceTexture(UTexture2D* FaceTexture);

	UFUNCTION(BlueprintCallable, Category = "Appearance|Selfie")
	void ApplyDetectedSkinTone(FLinearColor SkinTone);

	UPROPERTY(BlueprintReadOnly, Category = "Appearance|Selfie")
	TObjectPtr<UTexture2D> CurrentFaceTexture;

	// Material parameter names for selfie-driven appearance
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance|Selfie|Params")
	FName FaceTextureParam = TEXT("FaceTexture");

private:
	UFUNCTION()
	void OnRep_Appearance();

	void BuildDefaultMorphBindings();
	void SetFaceControlInternal(ENiceInkFaceControl Control, float Value);
	void SetBodyControlInternal(ENiceInkBodyControl Control, float Value);
	void ApplyBindingsForControl(const FName ControlName, const TArray<FNiceInkNamedFloat>& Controls, const TArray<FNiceInkMorphTargetBinding>& Bindings, TArray<FNiceInkNamedFloat>& Morphs);
	void RebuildMorphTargetsFromControls();
	void BroadcastAndApply();
};
