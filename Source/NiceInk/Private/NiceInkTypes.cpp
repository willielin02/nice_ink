#include "NiceInkTypes.h"

bool FTattooStroke::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	Ar << UV.X;
	Ar << UV.Y;
	Ar << Color.R;
	Ar << Color.G;
	Ar << Color.B;
	Ar << Color.A;

	uint8 NeedleValue = static_cast<uint8>(NeedleType);
	Ar << NeedleValue;
	if (Ar.IsLoading())
	{
		NeedleType = static_cast<ENiceInkNeedleType>(NeedleValue);
	}

	Ar << Pressure;
	Ar << Radius;
	Ar << Timestamp;
	Ar << ArtistPlayerId;
	Ar << TargetPlayerId;

	bOutSuccess = true;
	return true;
}

bool FTattooStroke::operator==(const FTattooStroke& Other) const
{
	return UV.Equals(Other.UV)
		&& Color.Equals(Other.Color)
		&& NeedleType == Other.NeedleType
		&& FMath::IsNearlyEqual(Pressure, Other.Pressure)
		&& FMath::IsNearlyEqual(Radius, Other.Radius)
		&& FMath::IsNearlyEqual(Timestamp, Other.Timestamp)
		&& ArtistPlayerId == Other.ArtistPlayerId
		&& TargetPlayerId == Other.TargetPlayerId;
}

void FTattooStrokeHistory::AddStroke(const FTattooStroke& Stroke)
{
	FTattooStrokeItem& NewItem = Items.AddDefaulted_GetRef();
	NewItem.Stroke = Stroke;
	MarkItemDirty(NewItem);
}
