#include "TattooSubsystem.h"

void UTattooSubsystem::RecordStroke(AActor* TargetActor, const FTattooStroke& Stroke)
{
	if (!IsValid(TargetActor))
	{
		return;
	}

	StrokeHistoryByActor.FindOrAdd(TargetActor).Add(Stroke);
}

TArray<FTattooStroke> UTattooSubsystem::GetStrokeHistory(AActor* TargetActor) const
{
	if (!IsValid(TargetActor))
	{
		return {};
	}

	if (const TArray<FTattooStroke>* History = StrokeHistoryByActor.Find(TargetActor))
	{
		return *History;
	}

	return {};
}

void UTattooSubsystem::ClearStrokeHistory(AActor* TargetActor)
{
	if (IsValid(TargetActor))
	{
		StrokeHistoryByActor.Remove(TargetActor);
	}
}
