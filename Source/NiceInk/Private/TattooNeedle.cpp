#include "TattooNeedle.h"

FNeedleConfig UTattooNeedle::MakeDefaultConfig(ENiceInkNeedleType Type)
{
	FNeedleConfig Config;
	Config.Type = Type;

	switch (Type)
	{
	case ENiceInkNeedleType::RoundLiner:
		Config.StrikesPerSecond = 130.0f;
		Config.UvRadius = 0.005f;
		Config.DotsPerStrike = 1;
		Config.Opacity = 0.95f;
		Config.PatternScale = FVector2D(1.0f, 1.0f);
		break;
	case ENiceInkNeedleType::RoundShader:
		Config.StrikesPerSecond = 85.0f;
		Config.UvRadius = 0.011f;
		Config.DotsPerStrike = 5;
		Config.Opacity = 0.55f;
		Config.PatternScale = FVector2D(1.0f, 1.0f);
		break;
	case ENiceInkNeedleType::Magnum:
		Config.StrikesPerSecond = 65.0f;
		Config.UvRadius = 0.015f;
		Config.DotsPerStrike = 9;
		Config.Opacity = 0.7f;
		Config.PatternScale = FVector2D(2.2f, 0.55f);
		break;
	case ENiceInkNeedleType::CurvedMagnum:
		Config.StrikesPerSecond = 60.0f;
		Config.UvRadius = 0.018f;
		Config.DotsPerStrike = 11;
		Config.Opacity = 0.5f;
		Config.PatternScale = FVector2D(2.0f, 0.75f);
		break;
	}

	return Config;
}
