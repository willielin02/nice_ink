#include "CharacterCustomizer.h"

#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"

namespace
{
void UpsertMorph(TArray<FNiceInkNamedFloat>& Morphs, FName MorphName, float Value)
{
	const float ClampedValue = FMath::Clamp(Value, 0.0f, 1.0f);
	for (FNiceInkNamedFloat& Morph : Morphs)
	{
		if (Morph.Name == MorphName)
		{
			Morph.Value = ClampedValue;
			return;
		}
	}

	FNiceInkNamedFloat& NewMorph = Morphs.AddDefaulted_GetRef();
	NewMorph.Name = MorphName;
	NewMorph.Value = ClampedValue;
}

void ApplyMorphs(USkeletalMeshComponent* MeshComponent, const TArray<FNiceInkNamedFloat>& Morphs)
{
	for (const FNiceInkNamedFloat& Morph : Morphs)
	{
		if (!Morph.Name.IsNone())
		{
			MeshComponent->SetMorphTarget(Morph.Name, Morph.Value);
		}
	}
}
}

UCharacterCustomizer::UCharacterCustomizer()
{
	SetIsReplicatedByDefault(true);
}

void UCharacterCustomizer::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UCharacterCustomizer, Appearance);
}

void UCharacterCustomizer::ApplyPreset(int32 Index)
{
	Appearance.PresetIndex = Index;
	Appearance.FaceMorphs.Reset();
	Appearance.BodyMorphs.Reset();

	const float PresetBias = static_cast<float>(Index % 5) * 0.15f;
	UpsertMorph(Appearance.FaceMorphs, TEXT("JawWidth"), 0.25f + PresetBias);
	UpsertMorph(Appearance.FaceMorphs, TEXT("CheekFullness"), 0.35f + PresetBias * 0.5f);
	UpsertMorph(Appearance.FaceMorphs, TEXT("NoseWidth"), 0.3f + PresetBias * 0.35f);
	UpsertMorph(Appearance.FaceMorphs, TEXT("EyeSize"), 0.55f - PresetBias * 0.2f);
	UpsertMorph(Appearance.BodyMorphs, TEXT("BodyFat"), Index % 2 == 0 ? 0.25f : 0.55f);
	BroadcastAndApply();
}

void UCharacterCustomizer::SetFaceMorph(FName MorphName, float Value)
{
	UpsertMorph(Appearance.FaceMorphs, MorphName, Value);
	BroadcastAndApply();
}

void UCharacterCustomizer::SetBodyMorph(FName MorphName, float Value)
{
	UpsertMorph(Appearance.BodyMorphs, MorphName, Value);
	BroadcastAndApply();
}

void UCharacterCustomizer::SetBodyType(ENiceInkBodyType BodyType)
{
	Appearance.BodyType = BodyType;
	switch (BodyType)
	{
	case ENiceInkBodyType::Lean:
		UpsertMorph(Appearance.BodyMorphs, TEXT("BodyFat"), 0.1f);
		UpsertMorph(Appearance.BodyMorphs, TEXT("MuscleMass"), 0.35f);
		break;
	case ENiceInkBodyType::Average:
		UpsertMorph(Appearance.BodyMorphs, TEXT("BodyFat"), 0.35f);
		UpsertMorph(Appearance.BodyMorphs, TEXT("MuscleMass"), 0.35f);
		break;
	case ENiceInkBodyType::Heavy:
		UpsertMorph(Appearance.BodyMorphs, TEXT("BodyFat"), 0.75f);
		UpsertMorph(Appearance.BodyMorphs, TEXT("MuscleMass"), 0.25f);
		break;
	case ENiceInkBodyType::Muscular:
		UpsertMorph(Appearance.BodyMorphs, TEXT("BodyFat"), 0.28f);
		UpsertMorph(Appearance.BodyMorphs, TEXT("MuscleMass"), 0.8f);
		break;
	}
	BroadcastAndApply();
}

void UCharacterCustomizer::SetSkinTone(FLinearColor SkinTone)
{
	Appearance.SkinTone = SkinTone;
	BroadcastAndApply();
}

void UCharacterCustomizer::SetHairStyle(int32 HairStyleIndex)
{
	Appearance.HairStyleIndex = HairStyleIndex;
	BroadcastAndApply();
}

void UCharacterCustomizer::SetHairColor(FLinearColor HairColor)
{
	Appearance.HairColor = HairColor;
	BroadcastAndApply();
}

void UCharacterCustomizer::ConfirmAppearance()
{
	BroadcastAndApply();
}

void UCharacterCustomizer::ApplyAppearanceToMesh(USkeletalMeshComponent* MeshComponent)
{
	if (!MeshComponent)
	{
		MeshComponent = GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	}

	if (!MeshComponent)
	{
		return;
	}

	ApplyMorphs(MeshComponent, Appearance.FaceMorphs);
	ApplyMorphs(MeshComponent, Appearance.BodyMorphs);

	if (AActor* Owner = GetOwner())
	{
		Owner->SetActorScale3D(FVector(1.0f, 1.0f, Appearance.HeightScale));
	}

	for (int32 Index = 0; Index < MeshComponent->GetNumMaterials(); ++Index)
	{
		UMaterialInstanceDynamic* DynamicMaterial = MeshComponent->CreateAndSetMaterialInstanceDynamic(Index);
		if (DynamicMaterial)
		{
			DynamicMaterial->SetVectorParameterValue(TEXT("SkinTone"), Appearance.SkinTone);
			DynamicMaterial->SetVectorParameterValue(TEXT("HairColor"), Appearance.HairColor);
		}
	}
}

void UCharacterCustomizer::OnRep_Appearance()
{
	BroadcastAndApply();
}

void UCharacterCustomizer::BroadcastAndApply()
{
	ApplyAppearanceToMesh(nullptr);
	OnAppearanceChanged.Broadcast();
}
