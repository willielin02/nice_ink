#include "CharacterCustomizer.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/Texture2D.h"
#include "HairStyleDatabase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"

namespace
{
void UpsertNamedFloat(TArray<FNiceInkNamedFloat>& Values, FName Name, float Value)
{
	if (Name.IsNone())
	{
		return;
	}

	const float ClampedValue = FMath::Clamp(Value, 0.0f, 1.0f);
	for (FNiceInkNamedFloat& NamedValue : Values)
	{
		if (NamedValue.Name == Name)
		{
			NamedValue.Value = ClampedValue;
			return;
		}
	}

	FNiceInkNamedFloat& NewValue = Values.AddDefaulted_GetRef();
	NewValue.Name = Name;
	NewValue.Value = ClampedValue;
}

bool FindNamedFloat(const TArray<FNiceInkNamedFloat>& Values, FName Name, float& OutValue)
{
	for (const FNiceInkNamedFloat& NamedValue : Values)
	{
		if (NamedValue.Name == Name)
		{
			OutValue = NamedValue.Value;
			return true;
		}
	}

	return false;
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

void AddBinding(TArray<FNiceInkMorphTargetBinding>& Bindings, FName ControlName, FName MorphTargetName, float Multiplier = 1.0f, float Bias = 0.0f, bool bInvert = false)
{
	FNiceInkMorphTargetBinding& Binding = Bindings.AddDefaulted_GetRef();
	Binding.ControlName = ControlName;
	Binding.MorphTargetName = MorphTargetName;
	Binding.Multiplier = Multiplier;
	Binding.Bias = Bias;
	Binding.bInvert = bInvert;
}

FLinearColor GetSkinTonePresetColor(int32 SkinToneIndex)
{
	static const FLinearColor SkinTones[] = {
		FLinearColor(0.96f, 0.78f, 0.62f, 1.0f),
		FLinearColor(0.86f, 0.62f, 0.45f, 1.0f),
		FLinearColor(0.74f, 0.50f, 0.35f, 1.0f),
		FLinearColor(0.55f, 0.34f, 0.22f, 1.0f),
		FLinearColor(0.36f, 0.22f, 0.15f, 1.0f),
		FLinearColor(0.20f, 0.13f, 0.09f, 1.0f)
	};

	const int32 ClampedIndex = FMath::Clamp(SkinToneIndex, 0, UE_ARRAY_COUNT(SkinTones) - 1);
	return SkinTones[ClampedIndex];
}
}

UCharacterCustomizer::UCharacterCustomizer()
{
	SetIsReplicatedByDefault(true);
	BuildDefaultMorphBindings();
}

void UCharacterCustomizer::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UCharacterCustomizer, Appearance);
}

void UCharacterCustomizer::ApplyPreset(int32 Index)
{
	Appearance.PresetIndex = Index;
	Appearance.FaceControls.Reset();
	Appearance.FaceMorphs.Reset();
	Appearance.BodyControls.Reset();
	Appearance.BodyMorphs.Reset();

	switch (FMath::Abs(Index) % 5)
	{
	case 0:
		SetFaceControlInternal(ENiceInkFaceControl::HeadWidth, 0.45f);
		SetFaceControlInternal(ENiceInkFaceControl::FaceRoundness, 0.45f);
		SetFaceControlInternal(ENiceInkFaceControl::EyeSize, 0.55f);
		SetFaceControlInternal(ENiceInkFaceControl::NoseBridgeHeight, 0.48f);
		SetFaceControlInternal(ENiceInkFaceControl::NoseTipSize, 0.42f);
		SetFaceControlInternal(ENiceInkFaceControl::CheekFullness, 0.45f);
		SetFaceControlInternal(ENiceInkFaceControl::MouthWidth, 0.48f);
		SetFaceControlInternal(ENiceInkFaceControl::JawWidth, 0.46f);
		SetBodyType(ENiceInkBodyType::Average);
		break;
	case 1:
		SetFaceControlInternal(ENiceInkFaceControl::HeadWidth, 0.34f);
		SetFaceControlInternal(ENiceInkFaceControl::HeadHeight, 0.62f);
		SetFaceControlInternal(ENiceInkFaceControl::CheekboneHeight, 0.68f);
		SetFaceControlInternal(ENiceInkFaceControl::CheekboneWidth, 0.38f);
		SetFaceControlInternal(ENiceInkFaceControl::NoseBridgeWidth, 0.32f);
		SetFaceControlInternal(ENiceInkFaceControl::NostrilWidth, 0.30f);
		SetFaceControlInternal(ENiceInkFaceControl::ChinProjection, 0.42f);
		SetBodyType(ENiceInkBodyType::Lean);
		break;
	case 2:
		SetFaceControlInternal(ENiceInkFaceControl::HeadWidth, 0.62f);
		SetFaceControlInternal(ENiceInkFaceControl::FaceRoundness, 0.74f);
		SetFaceControlInternal(ENiceInkFaceControl::CheekFullness, 0.76f);
		SetFaceControlInternal(ENiceInkFaceControl::NoseTipSize, 0.62f);
		SetFaceControlInternal(ENiceInkFaceControl::MouthWidth, 0.58f);
		SetFaceControlInternal(ENiceInkFaceControl::JawWidth, 0.60f);
		SetBodyType(ENiceInkBodyType::Heavy);
		break;
	case 3:
		SetFaceControlInternal(ENiceInkFaceControl::BrowHeight, 0.42f);
		SetFaceControlInternal(ENiceInkFaceControl::BrowAngle, 0.67f);
		SetFaceControlInternal(ENiceInkFaceControl::EyeDepth, 0.66f);
		SetFaceControlInternal(ENiceInkFaceControl::NoseBridgeHeight, 0.67f);
		SetFaceControlInternal(ENiceInkFaceControl::JawWidth, 0.76f);
		SetFaceControlInternal(ENiceInkFaceControl::JawAngle, 0.72f);
		SetFaceControlInternal(ENiceInkFaceControl::ChinWidth, 0.68f);
		SetBodyType(ENiceInkBodyType::Muscular);
		break;
	default:
		SetFaceControlInternal(ENiceInkFaceControl::HeadWidth, 0.50f);
		SetFaceControlInternal(ENiceInkFaceControl::FaceAsymmetry, 0.25f);
		SetFaceControlInternal(ENiceInkFaceControl::EyeSpacing, 0.58f);
		SetFaceControlInternal(ENiceInkFaceControl::EyeAngle, 0.42f);
		SetFaceControlInternal(ENiceInkFaceControl::NoseTipAngle, 0.56f);
		SetFaceControlInternal(ENiceInkFaceControl::MouthCornerHeight, 0.56f);
		SetFaceControlInternal(ENiceInkFaceControl::AgeLines, 0.30f);
		SetBodyType(ENiceInkBodyType::Average);
		break;
	}

	SetSkinTonePreset(Index % 6);
	Appearance.HairStyleIndex = FMath::Abs(Index) % 6;
	Appearance.BrowStyleIndex = FMath::Abs(Index + 1) % 5;
	Appearance.FacialHairStyleIndex = FMath::Abs(Index + 2) % 4;
	Appearance.MakeupStyleIndex = FMath::Abs(Index + 3) % 4;
	Appearance.MakeupIntensity = FMath::Clamp(0.1f * static_cast<float>(FMath::Abs(Index) % 5), 0.0f, 1.0f);
	RebuildMorphTargetsFromControls();
	BroadcastAndApply();
}

void UCharacterCustomizer::SetFaceControl(ENiceInkFaceControl Control, float Value)
{
	SetFaceControlInternal(Control, Value);
	BroadcastAndApply();
}

void UCharacterCustomizer::SetFaceMorph(FName MorphName, float Value)
{
	UpsertNamedFloat(Appearance.FaceMorphs, MorphName, Value);
	BroadcastAndApply();
}

void UCharacterCustomizer::SetBodyControl(ENiceInkBodyControl Control, float Value)
{
	SetBodyControlInternal(Control, Value);
	BroadcastAndApply();
}

void UCharacterCustomizer::SetBodyMorph(FName MorphName, float Value)
{
	UpsertNamedFloat(Appearance.BodyMorphs, MorphName, Value);
	BroadcastAndApply();
}

void UCharacterCustomizer::SetBodyType(ENiceInkBodyType BodyType)
{
	Appearance.BodyType = BodyType;
	switch (BodyType)
	{
	case ENiceInkBodyType::Lean:
		SetBodyControlInternal(ENiceInkBodyControl::BodyFat, 0.12f);
		SetBodyControlInternal(ENiceInkBodyControl::MuscleMass, 0.32f);
		SetBodyControlInternal(ENiceInkBodyControl::ShoulderWidth, 0.38f);
		SetBodyControlInternal(ENiceInkBodyControl::WaistSize, 0.30f);
		SetBodyControlInternal(ENiceInkBodyControl::HipWidth, 0.38f);
		break;
	case ENiceInkBodyType::Average:
		SetBodyControlInternal(ENiceInkBodyControl::BodyFat, 0.38f);
		SetBodyControlInternal(ENiceInkBodyControl::MuscleMass, 0.38f);
		SetBodyControlInternal(ENiceInkBodyControl::ShoulderWidth, 0.48f);
		SetBodyControlInternal(ENiceInkBodyControl::WaistSize, 0.45f);
		SetBodyControlInternal(ENiceInkBodyControl::HipWidth, 0.48f);
		break;
	case ENiceInkBodyType::Heavy:
		SetBodyControlInternal(ENiceInkBodyControl::BodyFat, 0.76f);
		SetBodyControlInternal(ENiceInkBodyControl::MuscleMass, 0.30f);
		SetBodyControlInternal(ENiceInkBodyControl::ShoulderWidth, 0.58f);
		SetBodyControlInternal(ENiceInkBodyControl::WaistSize, 0.72f);
		SetBodyControlInternal(ENiceInkBodyControl::HipWidth, 0.66f);
		break;
	case ENiceInkBodyType::Muscular:
		SetBodyControlInternal(ENiceInkBodyControl::BodyFat, 0.26f);
		SetBodyControlInternal(ENiceInkBodyControl::MuscleMass, 0.84f);
		SetBodyControlInternal(ENiceInkBodyControl::ShoulderWidth, 0.76f);
		SetBodyControlInternal(ENiceInkBodyControl::ChestSize, 0.76f);
		SetBodyControlInternal(ENiceInkBodyControl::ArmMuscle, 0.78f);
		SetBodyControlInternal(ENiceInkBodyControl::LegMuscle, 0.74f);
		break;
	}

	BroadcastAndApply();
}

void UCharacterCustomizer::SetHeightScale(float HeightScale)
{
	Appearance.HeightScale = FMath::Clamp(HeightScale, 0.75f, 1.25f);
	SetBodyControlInternal(ENiceInkBodyControl::Height, FMath::GetMappedRangeValueClamped(FVector2D(0.75f, 1.25f), FVector2D(0.0f, 1.0f), Appearance.HeightScale));
	BroadcastAndApply();
}

void UCharacterCustomizer::SetSkinTone(FLinearColor SkinTone)
{
	Appearance.SkinTone = SkinTone;
	BroadcastAndApply();
}

void UCharacterCustomizer::SetSkinTonePreset(int32 SkinToneIndex)
{
	Appearance.SkinToneIndex = FMath::Clamp(SkinToneIndex, 0, 5);
	Appearance.SkinTone = GetSkinTonePresetColor(Appearance.SkinToneIndex);
	BroadcastAndApply();
}

void UCharacterCustomizer::SetSkinDetails(int32 SkinDetailIndex, float FreckleIntensity, float BlemishIntensity, float ScarIntensity, float AgeDetail)
{
	Appearance.SkinDetailIndex = FMath::Max(0, SkinDetailIndex);
	Appearance.FreckleIntensity = FMath::Clamp(FreckleIntensity, 0.0f, 1.0f);
	Appearance.BlemishIntensity = FMath::Clamp(BlemishIntensity, 0.0f, 1.0f);
	Appearance.ScarIntensity = FMath::Clamp(ScarIntensity, 0.0f, 1.0f);
	Appearance.AgeDetail = FMath::Clamp(AgeDetail, 0.0f, 1.0f);
	SetFaceControlInternal(ENiceInkFaceControl::AgeLines, Appearance.AgeDetail);
	BroadcastAndApply();
}

void UCharacterCustomizer::SetSkinUndertone(FLinearColor SkinUndertone)
{
	Appearance.SkinUndertone = SkinUndertone;
	BroadcastAndApply();
}

void UCharacterCustomizer::SetEyeColor(FLinearColor EyeColor)
{
	Appearance.EyeColor = EyeColor;
	BroadcastAndApply();
}

void UCharacterCustomizer::SetHairStyle(int32 HairStyleIndex)
{
	Appearance.HairStyleIndex = FMath::Max(0, HairStyleIndex);
	BroadcastAndApply();
}

void UCharacterCustomizer::SetHairColor(FLinearColor HairColor)
{
	Appearance.HairColor = HairColor;
	BroadcastAndApply();
}

void UCharacterCustomizer::SetBrowStyle(int32 BrowStyleIndex)
{
	Appearance.BrowStyleIndex = FMath::Max(0, BrowStyleIndex);
	BroadcastAndApply();
}

void UCharacterCustomizer::SetBrowColor(FLinearColor BrowColor)
{
	Appearance.BrowColor = BrowColor;
	BroadcastAndApply();
}

void UCharacterCustomizer::SetFacialHairStyle(int32 FacialHairStyleIndex)
{
	Appearance.FacialHairStyleIndex = FMath::Max(0, FacialHairStyleIndex);
	BroadcastAndApply();
}

void UCharacterCustomizer::SetFacialHairColor(FLinearColor FacialHairColor)
{
	Appearance.FacialHairColor = FacialHairColor;
	BroadcastAndApply();
}

void UCharacterCustomizer::SetMakeup(int32 MakeupStyleIndex, float MakeupIntensity)
{
	Appearance.MakeupStyleIndex = FMath::Max(0, MakeupStyleIndex);
	Appearance.MakeupIntensity = FMath::Clamp(MakeupIntensity, 0.0f, 1.0f);
	BroadcastAndApply();
}

void UCharacterCustomizer::ConfirmAppearance()
{
	RebuildMorphTargetsFromControls();
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
			DynamicMaterial->SetVectorParameterValue(TEXT("SkinUndertone"), Appearance.SkinUndertone);
			DynamicMaterial->SetVectorParameterValue(TEXT("EyeColor"), Appearance.EyeColor);
			DynamicMaterial->SetVectorParameterValue(TEXT("HairColor"), Appearance.HairColor);
			DynamicMaterial->SetVectorParameterValue(TEXT("BrowColor"), Appearance.BrowColor);
			DynamicMaterial->SetVectorParameterValue(TEXT("FacialHairColor"), Appearance.FacialHairColor);
			DynamicMaterial->SetScalarParameterValue(TEXT("FreckleIntensity"), Appearance.FreckleIntensity);
			DynamicMaterial->SetScalarParameterValue(TEXT("BlemishIntensity"), Appearance.BlemishIntensity);
			DynamicMaterial->SetScalarParameterValue(TEXT("ScarIntensity"), Appearance.ScarIntensity);
			DynamicMaterial->SetScalarParameterValue(TEXT("AgeDetail"), Appearance.AgeDetail);
			DynamicMaterial->SetScalarParameterValue(TEXT("MakeupIntensity"), Appearance.MakeupIntensity);
			DynamicMaterial->SetScalarParameterValue(TEXT("SkinRoughness"), Appearance.SkinRoughness);
		}
	}
}

FName UCharacterCustomizer::GetFaceControlName(ENiceInkFaceControl Control)
{
	switch (Control)
	{
	case ENiceInkFaceControl::HeadWidth: return TEXT("HeadWidth");
	case ENiceInkFaceControl::HeadHeight: return TEXT("HeadHeight");
	case ENiceInkFaceControl::FaceRoundness: return TEXT("FaceRoundness");
	case ENiceInkFaceControl::ForeheadHeight: return TEXT("ForeheadHeight");
	case ENiceInkFaceControl::BrowHeight: return TEXT("BrowHeight");
	case ENiceInkFaceControl::BrowAngle: return TEXT("BrowAngle");
	case ENiceInkFaceControl::EyeSize: return TEXT("EyeSize");
	case ENiceInkFaceControl::EyeSpacing: return TEXT("EyeSpacing");
	case ENiceInkFaceControl::EyeDepth: return TEXT("EyeDepth");
	case ENiceInkFaceControl::EyeAngle: return TEXT("EyeAngle");
	case ENiceInkFaceControl::UpperEyelid: return TEXT("UpperEyelid");
	case ENiceInkFaceControl::LowerEyelid: return TEXT("LowerEyelid");
	case ENiceInkFaceControl::NoseBridgeHeight: return TEXT("NoseBridgeHeight");
	case ENiceInkFaceControl::NoseBridgeWidth: return TEXT("NoseBridgeWidth");
	case ENiceInkFaceControl::NoseTipSize: return TEXT("NoseTipSize");
	case ENiceInkFaceControl::NoseTipAngle: return TEXT("NoseTipAngle");
	case ENiceInkFaceControl::NostrilWidth: return TEXT("NostrilWidth");
	case ENiceInkFaceControl::CheekboneHeight: return TEXT("CheekboneHeight");
	case ENiceInkFaceControl::CheekboneWidth: return TEXT("CheekboneWidth");
	case ENiceInkFaceControl::CheekFullness: return TEXT("CheekFullness");
	case ENiceInkFaceControl::MouthWidth: return TEXT("MouthWidth");
	case ENiceInkFaceControl::UpperLipFullness: return TEXT("UpperLipFullness");
	case ENiceInkFaceControl::LowerLipFullness: return TEXT("LowerLipFullness");
	case ENiceInkFaceControl::MouthCornerHeight: return TEXT("MouthCornerHeight");
	case ENiceInkFaceControl::ChinWidth: return TEXT("ChinWidth");
	case ENiceInkFaceControl::ChinHeight: return TEXT("ChinHeight");
	case ENiceInkFaceControl::ChinProjection: return TEXT("ChinProjection");
	case ENiceInkFaceControl::JawWidth: return TEXT("JawWidth");
	case ENiceInkFaceControl::JawAngle: return TEXT("JawAngle");
	case ENiceInkFaceControl::JawForward: return TEXT("JawForward");
	case ENiceInkFaceControl::EarSize: return TEXT("EarSize");
	case ENiceInkFaceControl::EarAngle: return TEXT("EarAngle");
	case ENiceInkFaceControl::NeckThickness: return TEXT("NeckThickness");
	case ENiceInkFaceControl::AgeLines: return TEXT("AgeLines");
	case ENiceInkFaceControl::FaceAsymmetry: return TEXT("FaceAsymmetry");
	default: return NAME_None;
	}
}

FName UCharacterCustomizer::GetBodyControlName(ENiceInkBodyControl Control)
{
	switch (Control)
	{
	case ENiceInkBodyControl::Height: return TEXT("Height");
	case ENiceInkBodyControl::ShoulderWidth: return TEXT("ShoulderWidth");
	case ENiceInkBodyControl::ChestSize: return TEXT("ChestSize");
	case ENiceInkBodyControl::WaistSize: return TEXT("WaistSize");
	case ENiceInkBodyControl::HipWidth: return TEXT("HipWidth");
	case ENiceInkBodyControl::ArmMuscle: return TEXT("ArmMuscle");
	case ENiceInkBodyControl::ArmLength: return TEXT("ArmLength");
	case ENiceInkBodyControl::LegMuscle: return TEXT("LegMuscle");
	case ENiceInkBodyControl::LegLength: return TEXT("LegLength");
	case ENiceInkBodyControl::BodyFat: return TEXT("BodyFat");
	case ENiceInkBodyControl::MuscleMass: return TEXT("MuscleMass");
	case ENiceInkBodyControl::Posture: return TEXT("Posture");
	default: return NAME_None;
	}
}

TArray<FName> UCharacterCustomizer::GetFaceControlNames() const
{
	return {
		GetFaceControlName(ENiceInkFaceControl::HeadWidth),
		GetFaceControlName(ENiceInkFaceControl::HeadHeight),
		GetFaceControlName(ENiceInkFaceControl::FaceRoundness),
		GetFaceControlName(ENiceInkFaceControl::ForeheadHeight),
		GetFaceControlName(ENiceInkFaceControl::BrowHeight),
		GetFaceControlName(ENiceInkFaceControl::BrowAngle),
		GetFaceControlName(ENiceInkFaceControl::EyeSize),
		GetFaceControlName(ENiceInkFaceControl::EyeSpacing),
		GetFaceControlName(ENiceInkFaceControl::EyeDepth),
		GetFaceControlName(ENiceInkFaceControl::EyeAngle),
		GetFaceControlName(ENiceInkFaceControl::UpperEyelid),
		GetFaceControlName(ENiceInkFaceControl::LowerEyelid),
		GetFaceControlName(ENiceInkFaceControl::NoseBridgeHeight),
		GetFaceControlName(ENiceInkFaceControl::NoseBridgeWidth),
		GetFaceControlName(ENiceInkFaceControl::NoseTipSize),
		GetFaceControlName(ENiceInkFaceControl::NoseTipAngle),
		GetFaceControlName(ENiceInkFaceControl::NostrilWidth),
		GetFaceControlName(ENiceInkFaceControl::CheekboneHeight),
		GetFaceControlName(ENiceInkFaceControl::CheekboneWidth),
		GetFaceControlName(ENiceInkFaceControl::CheekFullness),
		GetFaceControlName(ENiceInkFaceControl::MouthWidth),
		GetFaceControlName(ENiceInkFaceControl::UpperLipFullness),
		GetFaceControlName(ENiceInkFaceControl::LowerLipFullness),
		GetFaceControlName(ENiceInkFaceControl::MouthCornerHeight),
		GetFaceControlName(ENiceInkFaceControl::ChinWidth),
		GetFaceControlName(ENiceInkFaceControl::ChinHeight),
		GetFaceControlName(ENiceInkFaceControl::ChinProjection),
		GetFaceControlName(ENiceInkFaceControl::JawWidth),
		GetFaceControlName(ENiceInkFaceControl::JawAngle),
		GetFaceControlName(ENiceInkFaceControl::JawForward),
		GetFaceControlName(ENiceInkFaceControl::EarSize),
		GetFaceControlName(ENiceInkFaceControl::EarAngle),
		GetFaceControlName(ENiceInkFaceControl::NeckThickness),
		GetFaceControlName(ENiceInkFaceControl::AgeLines),
		GetFaceControlName(ENiceInkFaceControl::FaceAsymmetry)
	};
}

TArray<FName> UCharacterCustomizer::GetBodyControlNames() const
{
	return {
		GetBodyControlName(ENiceInkBodyControl::Height),
		GetBodyControlName(ENiceInkBodyControl::ShoulderWidth),
		GetBodyControlName(ENiceInkBodyControl::ChestSize),
		GetBodyControlName(ENiceInkBodyControl::WaistSize),
		GetBodyControlName(ENiceInkBodyControl::HipWidth),
		GetBodyControlName(ENiceInkBodyControl::ArmMuscle),
		GetBodyControlName(ENiceInkBodyControl::ArmLength),
		GetBodyControlName(ENiceInkBodyControl::LegMuscle),
		GetBodyControlName(ENiceInkBodyControl::LegLength),
		GetBodyControlName(ENiceInkBodyControl::BodyFat),
		GetBodyControlName(ENiceInkBodyControl::MuscleMass),
		GetBodyControlName(ENiceInkBodyControl::Posture)
	};
}

void UCharacterCustomizer::OnRep_Appearance()
{
	RebuildMorphTargetsFromControls();
	BroadcastAndApply();
}

void UCharacterCustomizer::BuildDefaultMorphBindings()
{
	FaceMorphBindings.Reset();
	for (const FName ControlName : GetFaceControlNames())
	{
		AddBinding(FaceMorphBindings, ControlName, ControlName);
	}

	// Helpful composite defaults for prototype meshes. Designers can replace these with actual MetaHuman FACS names.
	AddBinding(FaceMorphBindings, GetFaceControlName(ENiceInkFaceControl::FaceRoundness), GetFaceControlName(ENiceInkFaceControl::JawAngle), 0.35f);
	AddBinding(FaceMorphBindings, GetFaceControlName(ENiceInkFaceControl::AgeLines), TEXT("ForeheadWrinkles"), 0.8f);
	AddBinding(FaceMorphBindings, GetFaceControlName(ENiceInkFaceControl::AgeLines), TEXT("NasolabialFold"), 0.65f);

	BodyMorphBindings.Reset();
	for (const FName ControlName : GetBodyControlNames())
	{
		AddBinding(BodyMorphBindings, ControlName, ControlName);
	}
}

void UCharacterCustomizer::SetFaceControlInternal(ENiceInkFaceControl Control, float Value)
{
	const FName ControlName = GetFaceControlName(Control);
	UpsertNamedFloat(Appearance.FaceControls, ControlName, Value);
	ApplyBindingsForControl(ControlName, Appearance.FaceControls, FaceMorphBindings, Appearance.FaceMorphs);
}

void UCharacterCustomizer::SetBodyControlInternal(ENiceInkBodyControl Control, float Value)
{
	const FName ControlName = GetBodyControlName(Control);
	UpsertNamedFloat(Appearance.BodyControls, ControlName, Value);
	if (Control == ENiceInkBodyControl::Height)
	{
		Appearance.HeightScale = FMath::GetMappedRangeValueClamped(FVector2D(0.0f, 1.0f), FVector2D(0.75f, 1.25f), FMath::Clamp(Value, 0.0f, 1.0f));
	}
	ApplyBindingsForControl(ControlName, Appearance.BodyControls, BodyMorphBindings, Appearance.BodyMorphs);
}

void UCharacterCustomizer::ApplyBindingsForControl(const FName ControlName, const TArray<FNiceInkNamedFloat>& Controls, const TArray<FNiceInkMorphTargetBinding>& Bindings, TArray<FNiceInkNamedFloat>& Morphs)
{
	float ControlValue = 0.0f;
	if (!FindNamedFloat(Controls, ControlName, ControlValue))
	{
		return;
	}

	for (const FNiceInkMorphTargetBinding& Binding : Bindings)
	{
		if (Binding.ControlName != ControlName || Binding.MorphTargetName.IsNone())
		{
			continue;
		}

		const float SourceValue = Binding.bInvert ? 1.0f - ControlValue : ControlValue;
		const float MorphValue = FMath::Clamp(Binding.Bias + SourceValue * Binding.Multiplier, 0.0f, 1.0f);
		UpsertNamedFloat(Morphs, Binding.MorphTargetName, MorphValue);
	}
}

void UCharacterCustomizer::RebuildMorphTargetsFromControls()
{
	Appearance.FaceMorphs.Reset();
	for (const FNiceInkNamedFloat& Control : Appearance.FaceControls)
	{
		ApplyBindingsForControl(Control.Name, Appearance.FaceControls, FaceMorphBindings, Appearance.FaceMorphs);
	}

	Appearance.BodyMorphs.Reset();
	for (const FNiceInkNamedFloat& Control : Appearance.BodyControls)
	{
		ApplyBindingsForControl(Control.Name, Appearance.BodyControls, BodyMorphBindings, Appearance.BodyMorphs);
	}
}

void UCharacterCustomizer::BroadcastAndApply()
{
	ApplyAppearanceToMesh(nullptr);
	OnAppearanceChanged.Broadcast();
}

// --- Selfie Pipeline Integration ---

void UCharacterCustomizer::ApplySelfieResult(const FSelfieResult& SelfieResult)
{
	if (!SelfieResult.bSuccess)
	{
		return;
	}

	ApplyDetectedSkinTone(SelfieResult.DetectedSkinTone);

	if (SelfieResult.FaceTexture)
	{
		ApplyFaceTexture(SelfieResult.FaceTexture);
	}

	if (SelfieResult.bHairDetected)
	{
		ApplyHairColor(SelfieResult.HairColor);

		if (SelfieResult.MatchedHairStyleIndex >= 0)
		{
			ApplyHairStyle(SelfieResult.MatchedHairStyleIndex);
		}
	}

	BroadcastAndApply();
}

void UCharacterCustomizer::ApplyFaceTexture(UTexture2D* FaceTexture)
{
	CurrentFaceTexture = FaceTexture;

	USkeletalMeshComponent* MeshComponent = GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	if (!MeshComponent || !FaceTexture)
	{
		return;
	}

	for (int32 Index = 0; Index < MeshComponent->GetNumMaterials(); ++Index)
	{
		UMaterialInstanceDynamic* DynMat = Cast<UMaterialInstanceDynamic>(MeshComponent->GetMaterial(Index));
		if (!DynMat)
		{
			DynMat = MeshComponent->CreateAndSetMaterialInstanceDynamic(Index);
		}
		if (DynMat)
		{
			DynMat->SetTextureParameterValue(FaceTextureParam, FaceTexture);
		}
	}
}

void UCharacterCustomizer::ApplyHairColor(const FNiceInkHairColorData& HairColorData)
{
	CurrentHairColor = HairColorData;
	Appearance.HairColor = HairColorData.BaseColor;

	// Groom material parameters are set on the groom component's material, not the body mesh.
	// The groom material needs these parameters:
	//   HairBaseColor, HairRootColor, HairTipColor, HairRootAmount,
	//   HairHighlightColor, HairHighlightRatio, HairColorMode
	// For now, also set on body mesh materials for any hair-accepting material slots.

	USkeletalMeshComponent* MeshComponent = GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	if (!MeshComponent)
	{
		return;
	}

	for (int32 Index = 0; Index < MeshComponent->GetNumMaterials(); ++Index)
	{
		UMaterialInstanceDynamic* DynMat = Cast<UMaterialInstanceDynamic>(MeshComponent->GetMaterial(Index));
		if (!DynMat)
		{
			DynMat = MeshComponent->CreateAndSetMaterialInstanceDynamic(Index);
		}
		if (!DynMat)
		{
			continue;
		}

		DynMat->SetVectorParameterValue(HairBaseColorParam, HairColorData.BaseColor);
		DynMat->SetScalarParameterValue(HairColorModeParam, static_cast<float>(HairColorData.Mode));

		switch (HairColorData.Mode)
		{
		case ENiceInkHairColorMode::Ombre:
			DynMat->SetVectorParameterValue(HairRootColorParam, HairColorData.RootColor);
			DynMat->SetVectorParameterValue(HairTipColorParam, HairColorData.TipColor);
			DynMat->SetScalarParameterValue(HairRootAmountParam, HairColorData.RootAmount);
			break;
		case ENiceInkHairColorMode::Highlights:
			DynMat->SetVectorParameterValue(HairHighlightColorParam, HairColorData.HighlightColor);
			DynMat->SetScalarParameterValue(HairHighlightRatioParam, HairColorData.HighlightRatio);
			break;
		default:
			break;
		}
	}
}

void UCharacterCustomizer::ApplyHairStyle(int32 HairStyleIndex)
{
	CurrentHairStyleIndex = HairStyleIndex;
	Appearance.HairStyleIndex = HairStyleIndex;

	const FHairStyleEntry* Entry = UHairStyleDatabase::FindByIndex(HairStyleIndex);
	if (!Entry)
	{
		return;
	}

	// Groom attachment: load the groom asset and attach to the head bone.
	// The GroomAssetPath on HairStyleEntry must be configured per-project.
	if (!Entry->GroomAssetPath.IsNull())
	{
		UE_LOG(LogTemp, Log, TEXT("CharacterCustomizer: Selected hair style %d (%s), groom: %s"), HairStyleIndex, *Entry->DisplayName, *Entry->GroomAssetPath.ToString());
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("CharacterCustomizer: Selected hair style %d (%s), no groom asset assigned"), HairStyleIndex, *Entry->DisplayName);
	}
}

void UCharacterCustomizer::ApplyDetectedSkinTone(FLinearColor SkinTone)
{
	SetSkinTone(SkinTone);

	// Derive a plausible undertone from the detected skin tone
	const FLinearColor Undertone(SkinTone.R * 1.08f, SkinTone.G * 0.95f, SkinTone.B * 0.88f, 1.0f);
	SetSkinUndertone(Undertone);
}
