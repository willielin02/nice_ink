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

	// Crayola 官方十色照抄（2026-07-24 user 定案「直接抄他們的」→引擎雙膚色
	// 截圖驗收「過」）：權威依據=1903 八色盒＋1926 併購 Munsell 蠟筆線（色彩
	// 科學譜系）＋百年消費者校準；值=官方公布 sRGB 轉線性。選色史（三輪公式
	// 落選帳＋膚色可讀剖面）在 Docs/DIRECT_DRAW_PLAN.md 與 Saved/palette_*.json。
	const FLinearColor GPalette[] = {
		FLinearColor(0.0168f, 0.0168f, 0.0168f), // 黑 Black #232323
		FLinearColor(0.8469f, 0.8469f, 0.8469f), // 白 White #EDEDED
		FLinearColor(0.8550f, 0.0144f, 0.0742f), // 紅 Red #EE204D
		FLinearColor(1.0000f, 0.1779f, 0.0395f), // 橙 Orange #FF7538
		FLinearColor(0.9734f, 0.8070f, 0.2270f), // 黃 Yellow #FCE883
		FLinearColor(0.0116f, 0.4125f, 0.1878f), // 綠 Green #1CAC78
		FLinearColor(0.0137f, 0.1779f, 0.9911f), // 藍 Blue #1F75FE
		FLinearColor(0.2874f, 0.1559f, 0.4233f), // 紫 Violet #926EAE
		FLinearColor(1.0000f, 0.4020f, 0.6038f), // 粉 Carnation Pink #FFAACC
		FLinearColor(0.4564f, 0.1356f, 0.0742f), // 棕 Brown #B4674D
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
