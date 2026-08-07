// FaceBakery 內部實作共用（Core=步驟1~5b、Warp=步驟6~12＋intake 工件）。
// 常數/函式名對齊 selfie_to_face_texture.py，方便逐行 diff 對賬。
#pragma once

#include "CoreMinimal.h"
#include "NiceInkFaceCv.h"

class FNiFaceOnnxModel;
class FNiFaceLandmarks;

namespace NiFace
{
	// ---- selfie_to_face_texture.py 常數（v7 現行組態） ----
	constexpr int32 TEX_SIZE = 2048;
	constexpr int32 MAX_SELFIE_SIDE = 1600;
	constexpr int32 CANVAS_SAFETY = 48;
	constexpr int32 N_CONTOUR_PTS = 64;
	constexpr int32 N_TOP_ARC_PTS = 36;
	constexpr double MASK_DEFLATE_RATIO = 0.03;
	constexpr int32 MASK_DEFLATE_MIN_512 = 2;
	constexpr double SELFIE_INFLATE_RATIO = 0.09;
	constexpr double FILL_SOURCE_INSET_RATIO = 0.015;
	constexpr double FILL_BLUR_RATIO = 0.04;
	constexpr double FILL_BLEND_DIST_RATIO = 0.35;
	constexpr double FILL_MEDIAN_PULL = 0.3;
	constexpr double FILL_GRAIN_STD = 7.0;
	constexpr int32 FADE_DIST = 25;
	constexpr int32 FADE_TRANSITION = 30;
	constexpr int32 EDGE_BLEND_DIST = 30;
	constexpr double ROT_COMPENSATE_MAX = 15.0;
	constexpr double ROLL_CORRECT_MIN_DEG = 2.0;
	constexpr double ROLL_CORRECT_MAX_DEG = 30.0;
	constexpr double EXPOSURE_TARGET_GRAY = 132.0;
	constexpr double EXPOSURE_BAND_LO = 115.0, EXPOSURE_BAND_HI = 152.0;
	constexpr double EXPOSURE_GAIN_LO = 0.55, EXPOSURE_GAIN_HI = 2.0;
	constexpr double FILLSMOOTH_SIGMA = 8.0;
	constexpr double FILLSMOOTH_RAMP_PX = 16.0;
	constexpr double FLATFIELD_K = 0.9;
	constexpr double FLATFIELD_SIGMA = 64.0;
	constexpr double FLATFIELD_RATIO_LO = 0.55, FLATFIELD_RATIO_HI = 1.9;
	constexpr double FLATFIELD_SMOOTH = 12.0;
	constexpr double BEARD_REMOVE_MIN_COVERAGE = 0.10;
	constexpr double BEARD_DILATE_RATIO = 0.012;
	constexpr double LIP_GUARD_RATIO = 0.05;
	constexpr double BROW_COVER_FRACTION = 0.5;
	constexpr double BROW_MIRROR_MIN = 0.25;
	constexpr double BROW_MIRROR_MARGIN = 0.10;
	constexpr double MIN_HAIR_FRACTION = 0.005;
	constexpr double EYELID_DILATE_RATIO = 0.16;
	constexpr double EYELID_LINE_W_RATIO = 0.05;
	constexpr double EYELID_LINE_DARKEN = 0.45;
	constexpr double EYELID_LINE_ALPHA = 0.85;
	constexpr double EYELID_SHADOW_ALPHA = 0.20;
	constexpr double EYELID_SHADOW_SIGMA_RATIO = 0.10;
	constexpr int32 POISSON_SOLVE_RES = 256;
	constexpr int32 POISSON_EDGE_ERODE = 2;
	constexpr double EDGE_SNAP_PX = 5.0;
	constexpr double MAX_CENTER_SHIFT = 80.0;

	// BiSeNet 標籤
	inline const TArray<int32> HEAD_LABELS = { 1, 2, 3, 4, 5, 6, 10, 11, 12, 13 };
	inline const TArray<int32> LIP_LABELS = { 11, 12, 13 };
	constexpr int32 SKIN_LABEL = 1;
	constexpr int32 HAIR_LABEL = 17;

	// MediaPipe 索引
	inline const TArray<int32> FACE_OVAL_ORDER = {
		10, 338, 297, 332, 284, 251, 389, 356, 454, 323, 361, 288,
		397, 365, 379, 378, 400, 377, 152, 148, 176, 149, 150, 136,
		172, 58, 132, 93, 234, 127, 162, 21, 54, 103, 67, 109 };
	inline const TArray<int32> BROW_L_LM = { 70, 63, 105, 66, 107 };
	inline const TArray<int32> BROW_R_LM = { 336, 296, 334, 293, 300 };
	inline const TArray<int32> EYE_L_RING = { 33, 7, 163, 144, 145, 153, 154, 155, 133,
		173, 157, 158, 159, 160, 161, 246 };
	inline const TArray<int32> EYE_R_RING = { 263, 249, 390, 373, 374, 380, 381, 382, 362,
		398, 384, 385, 386, 387, 388, 466 };
	inline const int32 SYM_PAIRS[5][2] = { {33, 263}, {133, 362}, {61, 291}, {70, 300}, {50, 280} };
	inline const TArray<int32> MIDLINE = { 168, 1, 13, 152 };

	// ---- 執行 context ----
	struct FCtx
	{
		FString DataDir;                 // Content/FaceBakery/data
		FNiFaceOnnxModel* Bisenet = nullptr;
		FNiFaceOnnxModel* Lama = nullptr;
		FNiFaceLandmarks* Landmarks = nullptr;
		TFunction<void(const FString&)> Progress;
		TArray<FString> Log;             // intake_log.txt 行
		bool bDump = false;
		FString DumpDir;
		double T0 = 0.0;                 // RunIntake 起點（Say 的耗時戳）

		void Say(const FString& Line);   // Log + UE_LOG + Progress
	};

	struct FBrowSide
	{
		double HairFraction = 0.0;
		bool bCovered = false;
		int32 Box[4] = { 0, 0, 0, 0 };   // x0, y0, x1, y1
	};
	struct FBrowStatus
	{
		FBrowSide L, R;
		bool bCovered = false;
	};

	struct FBeardMeta
	{
		double Coverage = 0.0;
		double Drape = 0.0;
		bool bHasColor = false;
		cv::Vec3d ColorBgr;
		int32 Pixels = 0;
	};

	struct FBakeResult
	{
		cv::Mat FaceOpen;    // 2048² BGRA u8
		cv::Mat FaceClosed;  // 2048² BGRA u8
		cv::Mat EyeMask;     // 2048² u8（FaceUV 眼開口）
		cv::Vec3b SkinColor; // BGR
	};

	// ---- 影像 IO（Core.cpp；UE ImageWrapper——opencv_world455 的 imgcodecs 沒帶
	// jpeg 編解碼、且 fopen 不吃 unicode 路徑）----
	bool LoadImage(const FString& Path, cv::Mat& Out, bool bGrayscale);
	bool SavePng(const cv::Mat& Img, const FString& Path); // CV_8U / 8UC3 / 8UC4

	// ---- 共用 helpers（Core.cpp） ----
	cv::Mat BlurF(const cv::Mat& Src, double Sigma);                       // GaussianBlur Size(0,0)
	cv::Mat NormalizedConv(const cv::Mat& ImgF, const cv::Mat& MaskF, double Sigma);
	void NearestExtend(const cv::Mat& Values3F, const cv::Mat& SrcMaskU8,
	                   cv::Mat& OutExtended, cv::Mat& OutDist);            // 最近源像素外推
	cv::Mat LabelMask(const cv::Mat& Parsing, const TArray<int32>& Labels); // 512² u8*255
	cv::Mat ResizeNearest(const cv::Mat& M512, int32 W, int32 H);
	cv::Vec3d MedianBgr(const cv::Mat& Bgr, const cv::Mat& MaskBool);      // per-channel median
	double MedianGray(const cv::Mat& Gray, const cv::Mat& MaskBool);
	double Percentile(TArray<float>& Vals, double P);                      // 就地 nth_element（np.percentile 線性插值）
	cv::Vec3f BgrToLabF(const cv::Vec3b& Bgr);                             // u8 LAB 域 float

	/** 決定性高斯場（PCG32＋Box-Muller、seed 固定）——numpy 序列不同、統計等價。 */
	struct FGrainRng
	{
		uint64 State;
		explicit FGrainRng(uint64 Seed);
		float Gauss();                    // N(0,1)
		cv::Mat Field(int32 H, int32 W, double Std); // CV_32F H×W
	private:
		uint32 NextU32();
		bool bHasSpare = false;
		float Spare = 0.f;
	};

	// ---- 步驟函式（Core.cpp） ----
	bool ParseSelfie(FCtx& C, const cv::Mat& SelfieBgr, cv::Mat& OutParsing, FString& Err);
	cv::Mat BuildHeadMask(const cv::Mat& Parsing, int32 W, int32 H, double DeflatePx,
	                      const cv::Mat& FaceContour);
	cv::Vec3b ExtractSkinColor(const cv::Mat& Parsing, const cv::Mat& SelfieBgr);
	bool ExtractHairColor(const cv::Mat& Parsing, const cv::Mat& SelfieBgr, cv::Vec3b& OutBgr);
	cv::Mat NaturalSkinFill(const cv::Mat& SelfieBgr, const cv::Mat& HeadMask,
	                        const cv::Vec3b& SkinColor, double FaceWidth,
	                        const cv::Mat& SampleExclude);
	cv::Mat LipsGuardMask(const cv::Mat& Parsing, int32 W, int32 H, double FaceWidth);
	FBrowStatus BrowCoverStatus(const cv::Mat& Parsing, const cv::Mat& AllLm,
	                            int32 W, int32 H, double FaceWidth);
	cv::Mat BeardTerritoryMask(const cv::Mat& AllLm, int32 W, int32 H, double FaceWidth);
	FBeardMeta BeardAnalysis(const cv::Mat& Selfie, const cv::Mat& Parsing, const cv::Mat& AllLm,
	                         const cv::Vec3b& SkinBgr, bool bHasHair, const cv::Vec3b& HairBgr,
	                         double FaceWidth, cv::Mat& OutBeardMask);
	bool LamaInpaint(FCtx& C, cv::Mat& Bgr, const cv::Mat& HoleBool, FString& Err);

	// ---- 步驟函式（Warp.cpp） ----
	bool SelfieToFaceTexture(FCtx& C, const cv::Mat& SelfieIn, FBakeResult& Out, FString& Err);
}
