#include "NiceInkTypes.h"

namespace
{
	// 膚色 = Tools/FacePipeline/out/players/<key>/skin_color.json 的 linear_rgb 量測值
	const FNiceInkAvatarDef GAvatars[] = {
		{ TEXT("7AF4"),
		  TEXT("/Game/Characters/Faces/T_Face_7AF4.T_Face_7AF4"),
		  TEXT("/Game/Characters/Faces/T_Face_7AF4_Closed.T_Face_7AF4_Closed"),
		  TEXT("/Game/Characters/Faces/T_EyeMaskInk_7AF4.T_EyeMaskInk_7AF4"),
		  FLinearColor(0.396755f, 0.219526f, 0.132868f) },
		{ TEXT("cvd"),
		  TEXT("/Game/Characters/Faces/T_Face_cvd.T_Face_cvd"),
		  TEXT("/Game/Characters/Faces/T_Face_cvd_Closed.T_Face_cvd_Closed"),
		  TEXT("/Game/Characters/Faces/T_EyeMaskInk_cvd.T_EyeMaskInk_cvd"),
		  FLinearColor(0.401978f, 0.187821f, 0.155926f) },
		{ TEXT("caseoh"),
		  TEXT("/Game/Characters/Faces/T_Face_caseoh.T_Face_caseoh"),
		  TEXT("/Game/Characters/Faces/T_Face_caseoh_Closed.T_Face_caseoh_Closed"),
		  TEXT("/Game/Characters/Faces/T_EyeMaskInk_caseoh.T_EyeMaskInk_caseoh"),
		  FLinearColor(0.361307f, 0.144128f, 0.099899f) },
		{ TEXT("ibai"),
		  TEXT("/Game/Characters/Faces/T_Face_ibai.T_Face_ibai"),
		  TEXT("/Game/Characters/Faces/T_Face_ibai_Closed.T_Face_ibai_Closed"),
		  TEXT("/Game/Characters/Faces/T_EyeMaskInk_ibai.T_EyeMaskInk_ibai"),
		  FLinearColor(0.456411f, 0.205079f, 0.174647f) },
		{ TEXT("img1"),
		  TEXT("/Game/Characters/Faces/T_Face_img1.T_Face_img1"),
		  TEXT("/Game/Characters/Faces/T_Face_img1_Closed.T_Face_img1_Closed"),
		  TEXT("/Game/Characters/Faces/T_EyeMaskInk_img1.T_EyeMaskInk_img1"),
		  FLinearColor(0.40724f, 0.191202f, 0.130136f) },
		{ TEXT("img0"),
		  TEXT("/Game/Characters/Faces/T_Face_img0.T_Face_img0"),
		  TEXT("/Game/Characters/Faces/T_Face_img0_Closed.T_Face_img0_Closed"),
		  TEXT("/Game/Characters/Faces/T_EyeMaskInk_img0.T_EyeMaskInk_img0"),
		  FLinearColor(0.266356f, 0.104616f, 0.078187f) },
	};

	const FLinearColor GPalette[] = {
		FLinearColor(0.02f, 0.02f, 0.02f), // 黑
		FLinearColor(0.95f, 0.95f, 0.95f), // 白
		FLinearColor(0.78f, 0.05f, 0.05f), // 紅
		FLinearColor(0.9f, 0.4f, 0.05f),   // 橙
		FLinearColor(0.92f, 0.85f, 0.05f), // 黃
		FLinearColor(0.06f, 0.55f, 0.1f),  // 綠
		FLinearColor(0.05f, 0.2f, 0.8f),   // 藍
		FLinearColor(0.4f, 0.08f, 0.65f),  // 紫
		FLinearColor(0.95f, 0.4f, 0.65f),  // 粉
		FLinearColor(0.4f, 0.22f, 0.08f),  // 棕
	};
}

int32 FNiceInkAvatars::Num()
{
	return UE_ARRAY_COUNT(GAvatars);
}

const FNiceInkAvatarDef& FNiceInkAvatars::Get(int32 Index)
{
	return GAvatars[FMath::Clamp(Index, 0, Num() - 1)];
}

int32 FNiceInkPalette::Num()
{
	return UE_ARRAY_COUNT(GPalette);
}

FLinearColor FNiceInkPalette::Get(int32 Index)
{
	return GPalette[FMath::Clamp(Index, 0, Num() - 1)];
}
