// 由 Tools/AssetPrep/export_draw_pose.py 生成——手改無效，重跑腳本。
// 蹲踞作畫基底姿勢（SourceAssets/sumo_retopo_base16.blend 的 rest＝user 手擺蹲姿）。
// 每骨＝絕對 ComponentSpace transform（UE 空間、cm）；執行期依序（root→leaf）寫入
// BowBody 即得蹲姿。絕對目標＝與骨骼局部軸慣例無關。
#pragma once

#include "CoreMinimal.h"

struct FDrawPoseBoneCS
{
	const TCHAR* BoneName;
	FQuat Q;    // CS 旋轉
	FVector T;  // CS 位置（cm）
};

inline const FDrawPoseBoneCS GDrawPoseCS[] = {
	{ TEXT("Hips"), FQuat(-0.705520f, 0.000001f, -0.000001f, 0.708690f), FVector(-0.000f, -29.674f, 65.686f) },
	{ TEXT("LeftUpLeg"), FQuat(0.350726f, 0.176128f, 0.834439f, 0.386888f), FVector(33.556f, -13.108f, 71.813f) },
	{ TEXT("LeftLeg"), FQuat(0.431804f, 0.762960f, 0.206997f, 0.434269f), FVector(58.675f, 17.614f, 44.616f) },
	{ TEXT("LeftFoot"), FQuat(0.139782f, 0.032360f, 0.962449f, 0.230448f), FVector(33.499f, -10.835f, 8.312f) },
	{ TEXT("LeftToeBase"), FQuat(0.990889f, -0.119158f, 0.007497f, 0.062323f), FVector(45.720f, 14.242f, 4.748f) },
	{ TEXT("Spine"), FQuat(-0.630241f, -0.000000f, -0.000000f, 0.776400f), FVector(-0.000f, -29.567f, 76.276f) },
	{ TEXT("Spine1"), FQuat(-0.772325f, -0.000000f, 0.000000f, 0.635228f), FVector(-0.000f, -33.428f, 94.656f) },
	{ TEXT("Neck"), FQuat(-0.982263f, -0.000000f, 0.000000f, 0.187508f), FVector(-0.000f, -13.180f, 143.167f) },
	{ TEXT("Head"), FQuat(-0.890511f, -0.000000f, 0.000000f, 0.454961f), FVector(-0.000f, -2.714f, 147.314f) },
	{ TEXT("LeftShoulder"), FQuat(0.166054f, 0.152903f, 0.721643f, 0.654430f), FVector(9.000f, -14.405f, 144.677f) },
	{ TEXT("LeftArm"), FQuat(0.189952f, 0.208421f, 0.665394f, 0.691180f), FVector(37.343f, -11.339f, 130.786f) },
	{ TEXT("LeftForeArm"), FQuat(-0.338553f, 0.434478f, 0.828874f, 0.097871f), FVector(72.921f, -13.131f, 107.934f) },
	{ TEXT("LeftHand"), FQuat(-0.194514f, 0.313273f, 0.923710f, 0.103848f), FVector(89.204f, 8.391f, 84.604f) },
	{ TEXT("LeftHandIndex1"), FQuat(0.784369f, -0.369550f, -0.360702f, 0.343645f), FVector(87.348f, 24.468f, 79.138f) },
	{ TEXT("LeftHandIndex2"), FQuat(0.279011f, -0.391974f, -0.562281f, 0.672569f), FVector(88.990f, 26.895f, 75.152f) },
	{ TEXT("LeftHandProp"), FQuat(0.798204f, 0.312298f, -0.487539f, 0.166274f), FVector(93.672f, 12.090f, 71.475f) },
	{ TEXT("LeftHandThumb1"), FQuat(0.858024f, 0.275359f, -0.326615f, 0.285123f), FVector(83.690f, 16.934f, 83.606f) },
	{ TEXT("LeftHandThumb2"), FQuat(0.895188f, 0.016933f, -0.137728f, 0.423536f), FVector(78.502f, 22.335f, 81.169f) },
	{ TEXT("RightShoulder"), FQuat(0.166054f, -0.152903f, -0.721643f, 0.654430f), FVector(-9.000f, -14.405f, 144.677f) },
	{ TEXT("RightArm"), FQuat(0.189952f, -0.208421f, -0.665394f, 0.691180f), FVector(-37.343f, -11.339f, 130.786f) },
	{ TEXT("RightForeArm"), FQuat(-0.338553f, -0.434478f, -0.828874f, 0.097871f), FVector(-72.921f, -13.131f, 107.934f) },
	{ TEXT("RightHand"), FQuat(-0.194515f, -0.313273f, -0.923710f, 0.103848f), FVector(-89.204f, 8.391f, 84.604f) },
	{ TEXT("RightHandIndex1"), FQuat(0.784369f, 0.369550f, 0.360702f, 0.343645f), FVector(-87.348f, 24.468f, 79.138f) },
	{ TEXT("RightHandIndex2"), FQuat(0.279011f, 0.391974f, 0.562281f, 0.672569f), FVector(-88.990f, 26.895f, 75.152f) },
	{ TEXT("RightHandProp"), FQuat(0.798204f, -0.312298f, 0.487539f, 0.166274f), FVector(-93.672f, 12.090f, 71.475f) },
	{ TEXT("RightHandThumb1"), FQuat(0.858024f, -0.275359f, 0.326615f, 0.285123f), FVector(-83.690f, 16.933f, 83.606f) },
	{ TEXT("RightHandThumb2"), FQuat(0.895188f, -0.016933f, 0.137728f, 0.423536f), FVector(-78.502f, 22.335f, 81.169f) },
	{ TEXT("RightUpLeg"), FQuat(0.350726f, -0.176128f, -0.834439f, 0.386888f), FVector(-33.556f, -13.108f, 71.813f) },
	{ TEXT("RightLeg"), FQuat(0.431804f, -0.762960f, -0.206997f, 0.434269f), FVector(-58.675f, 17.614f, 44.616f) },
	{ TEXT("RightFoot"), FQuat(0.139782f, -0.032360f, -0.962449f, 0.230448f), FVector(-33.499f, -10.835f, 8.312f) },
	{ TEXT("RightToeBase"), FQuat(0.990889f, 0.119158f, -0.007497f, 0.062323f), FVector(-45.720f, 14.242f, 4.748f) },
};
