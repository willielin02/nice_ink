#include "NiceInkTypes.h"

bool FTattooStroke::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	Ar << UV.X;
	Ar << UV.Y;
	Ar << Color.R;
	Ar << Color.G;
	Ar << Color.B;
	Ar << Color.A;

	uint8 MarkerValue = static_cast<uint8>(MarkerType);
	Ar << MarkerValue;
	if (Ar.IsLoading())
	{
		MarkerType = static_cast<ENiceInkMarkerType>(MarkerValue);
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
		&& MarkerType == Other.MarkerType
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
