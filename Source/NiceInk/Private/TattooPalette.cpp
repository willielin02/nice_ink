#include "TattooPalette.h"

FLinearColor UTattooPalette::GetColor(int32 Index) const
{
	return Colors.IsValidIndex(Index) ? Colors[Index] : FLinearColor::Black;
}
