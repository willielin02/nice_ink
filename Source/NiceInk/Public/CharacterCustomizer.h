#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NiceInkTypes.h"
#include "CharacterCustomizer.generated.h"

class USkeletalMeshComponent;

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

	UFUNCTION(BlueprintCallable, Category = "Appearance")
	void ApplyPreset(int32 Index);

	UFUNCTION(BlueprintCallable, Category = "Appearance")
	void SetFaceMorph(FName MorphName, float Value);

	UFUNCTION(BlueprintCallable, Category = "Appearance")
	void SetBodyMorph(FName MorphName, float Value);

	UFUNCTION(BlueprintCallable, Category = "Appearance")
	void SetBodyType(ENiceInkBodyType BodyType);

	UFUNCTION(BlueprintCallable, Category = "Appearance")
	void SetSkinTone(FLinearColor SkinTone);

	UFUNCTION(BlueprintCallable, Category = "Appearance")
	void SetHairStyle(int32 HairStyleIndex);

	UFUNCTION(BlueprintCallable, Category = "Appearance")
	void SetHairColor(FLinearColor HairColor);

	UFUNCTION(BlueprintCallable, Category = "Appearance")
	void ConfirmAppearance();

	UFUNCTION(BlueprintCallable, Category = "Appearance")
	void ApplyAppearanceToMesh(USkeletalMeshComponent* MeshComponent);

	UFUNCTION(BlueprintPure, Category = "Appearance")
	FCharacterAppearance GetAppearanceData() const { return Appearance; }

private:
	UFUNCTION()
	void OnRep_Appearance();

	void BroadcastAndApply();
};
