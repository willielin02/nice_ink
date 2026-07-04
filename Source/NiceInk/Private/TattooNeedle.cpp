#include "TattooNeedle.h"

FMarkerConfig UTattooMarker::MakeDefaultConfig(ENiceInkMarkerType Type)
{
	FMarkerConfig Config;
	Config.Type = Type;

	switch (Type)
	{
	case ENiceInkMarkerType::FineMarker:
		Config.StrokesPerSecond = 130.0f;
		Config.UvRadius = 0.005f;
		Config.DotsPerStroke = 1;
		Config.Opacity = 0.95f;
		Config.PatternScale = FVector2D(1.0f, 1.0f);
		break;
	case ENiceInkMarkerType::ThickMarker:
		Config.StrokesPerSecond = 85.0f;
		Config.UvRadius = 0.011f;
		Config.DotsPerStroke = 5;
		Config.Opacity = 0.55f;
		Config.PatternScale = FVector2D(1.0f, 1.0f);
		break;
	case ENiceInkMarkerType::BrushTip:
		Config.StrokesPerSecond = 65.0f;
		Config.UvRadius = 0.015f;
		Config.DotsPerStroke = 9;
		Config.Opacity = 0.7f;
		Config.PatternScale = FVector2D(2.2f, 0.55f);
		break;
	}

	return Config;
}
