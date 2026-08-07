// FaceBakery Warp：步驟 6~12（輪廓/TPS/對稱/平場/邊緣收斂/Poisson 膜/眼閉變體）
// ＋SelfieToFaceTexture 總編排。行為規格＝selfie_to_face_texture.py v7 main()。
#include "NiceInkFaceBakeryImpl.h"

#include "Algo/BinarySearch.h"
#include "Algo/Reverse.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NiceInkFaceLandmarks.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogNiFaceWarp, Log, All);

namespace NiFace
{

// ---------------------------------------------------------------- contours

static double ShoelaceArea(const cv::Mat& C)
{
	const int32 N = C.rows;
	double Sum = 0.0;
	for (int32 i = 0; i < N; ++i)
	{
		const int32 j = (i + 1) % N;
		Sum += C.at<double>(i, 0) * C.at<double>(j, 1) - C.at<double>(j, 0) * C.at<double>(i, 1);
	}
	return Sum;
}

static cv::Mat EnsureClockwise(const cv::Mat& C)
{
	if (ShoelaceArea(C) < 0)
	{
		cv::Mat Out(C.rows, 2, CV_64F);
		for (int32 i = 0; i < C.rows; ++i)
		{
			Out.at<double>(i, 0) = C.at<double>(C.rows - 1 - i, 0);
			Out.at<double>(i, 1) = C.at<double>(C.rows - 1 - i, 1);
		}
		return Out;
	}
	return C.clone();
}

struct FEllipse
{
	double Cx, Cy, A, B, AngleDeg;
};

static bool BuildSelfieContour(const cv::Mat& AllLm, cv::Mat& OutContour, FEllipse& OutEllipse)
{
	const int32 N = FACE_OVAL_ORDER.Num();
	std::vector<cv::Point2f> OvalF;
	cv::Mat Oval(N, 2, CV_64F);
	for (int32 i = 0; i < N; ++i)
	{
		const double X = AllLm.at<double>(FACE_OVAL_ORDER[i], 0);
		const double Y = AllLm.at<double>(FACE_OVAL_ORDER[i], 1);
		Oval.at<double>(i, 0) = X;
		Oval.at<double>(i, 1) = Y;
		OvalF.emplace_back((float)X, (float)Y);
	}
	const cv::RotatedRect E = cv::fitEllipse(OvalF);
	OutEllipse = { E.center.x, E.center.y, E.size.width / 2.0, E.size.height / 2.0, E.angle };
	const double Ar = OutEllipse.AngleDeg * CV_PI / 180.0;
	const double CosR = std::cos(Ar), SinR = std::sin(Ar);

	TArray<double> Xs;
	for (int32 i = 0; i < N; ++i) { Xs.Add(Oval.at<double>(i, 0)); }
	TArray<double> XSort = Xs;
	XSort.Sort();
	const double XMed = (N % 2 == 1) ? XSort[N / 2] : 0.5 * (XSort[N / 2 - 1] + XSort[N / 2]);

	int32 LeftTop = -1, RightTop = -1;
	double LeftTopY = 1e18, RightTopY = 1e18;
	for (int32 i = 0; i < N; ++i)
	{
		const double Y = Oval.at<double>(i, 1);
		if (Xs[i] < XMed)
		{
			if (Y < LeftTopY) { LeftTopY = Y; LeftTop = i; }
		}
		else
		{
			if (Y < RightTopY) { RightTopY = Y; RightTop = i; }
		}
	}
	if (LeftTop < 0 || RightTop < 0)
	{
		return false;
	}

	TArray<cv::Point2d> Bottom;
	{
		int32 i = RightTop;
		while (true)
		{
			Bottom.Add(cv::Point2d(Oval.at<double>(i, 0), Oval.at<double>(i, 1)));
			if (i == LeftTop && Bottom.Num() > 1) { break; }
			i = (i + 1) % N;
		}
	}

	auto ParamAngle = [&](const cv::Point2d& P)
	{
		const double Dx = P.x - OutEllipse.Cx, Dy = P.y - OutEllipse.Cy;
		const double Lx = Dx * CosR + Dy * SinR;
		const double Ly = -Dx * SinR + Dy * CosR;
		return std::atan2(Ly / OutEllipse.B, Lx / OutEllipse.A);
	};
	auto EllipsePt = [&](double T)
	{
		const double Lx = OutEllipse.A * std::cos(T), Ly = OutEllipse.B * std::sin(T);
		return cv::Point2d(OutEllipse.Cx + Lx * CosR - Ly * SinR,
		                   OutEllipse.Cy + Lx * SinR + Ly * CosR);
	};
	const double TL = ParamAngle(cv::Point2d(Oval.at<double>(LeftTop, 0), Oval.at<double>(LeftTop, 1)));
	const double TR = ParamAngle(cv::Point2d(Oval.at<double>(RightTop, 0), Oval.at<double>(RightTop, 1)));

	auto ArcPts = [&](double T0, double T1)
	{
		TArray<cv::Point2d> Out;
		for (int32 i = 0; i < N_TOP_ARC_PTS; ++i)
		{
			const double T = T0 + (T1 - T0) * i / (N_TOP_ARC_PTS - 1);
			Out.Add(EllipsePt(T));
		}
		return Out;
	};
	TArray<cv::Point2d> PtsA, PtsB;
	if (TL <= TR)
	{
		PtsA = ArcPts(TL, TR);
		PtsB = ArcPts(TL, TR - 2 * CV_PI);
	}
	else
	{
		PtsA = ArcPts(TL, TR + 2 * CV_PI);
		PtsB = ArcPts(TL, TR);
	}
	double MeanA = 0, MeanB = 0;
	for (const cv::Point2d& P : PtsA) { MeanA += P.y; }
	for (const cv::Point2d& P : PtsB) { MeanB += P.y; }
	const TArray<cv::Point2d>& Top = (MeanA < MeanB) ? PtsA : PtsB;

	cv::Mat C(Bottom.Num() + Top.Num(), 2, CV_64F);
	for (int32 i = 0; i < Bottom.Num(); ++i)
	{
		C.at<double>(i, 0) = Bottom[i].x;
		C.at<double>(i, 1) = Bottom[i].y;
	}
	for (int32 i = 0; i < Top.Num(); ++i)
	{
		C.at<double>(Bottom.Num() + i, 0) = Top[i].x;
		C.at<double>(Bottom.Num() + i, 1) = Top[i].y;
	}
	OutContour = EnsureClockwise(C);
	return true;
}

static bool GetFaceUvContour(const FString& MaskPath, cv::Mat& OutContour, cv::Mat& OutMask)
{
	cv::Mat Raw;
	if (!LoadImage(MaskPath, Raw, /*bGrayscale=*/true))
	{
		return false;
	}
	cv::Mat Binary;
	cv::threshold(Raw, Binary, 127, 255, cv::THRESH_BINARY);
	cv::Mat Labels, Stats, Centroids;
	const int32 NLabels = cv::connectedComponentsWithStats(Binary, Labels, Stats, Centroids);
	if (NLabels > 2)
	{
		int32 Best = 1, BestArea = 0;
		for (int32 i = 1; i < NLabels; ++i)
		{
			const int32 Area = Stats.at<int32>(i, cv::CC_STAT_AREA);
			if (Area > BestArea) { BestArea = Area; Best = i; }
		}
		for (int32 y = 0; y < Binary.rows; ++y)
		{
			const int32* L = Labels.ptr<int32>(y);
			uint8* B = Binary.ptr<uint8>(y);
			for (int32 x = 0; x < Binary.cols; ++x) { B[x] = (L[x] == Best) ? 255 : 0; }
		}
	}
	std::vector<std::vector<cv::Point>> Contours;
	cv::findContours(Binary, Contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
	if (Contours.empty())
	{
		return false;
	}
	size_t Best = 0;
	double BestArea = -1;
	for (size_t i = 0; i < Contours.size(); ++i)
	{
		const double A = cv::contourArea(Contours[i]);
		if (A > BestArea) { BestArea = A; Best = i; }
	}
	cv::Mat C((int32)Contours[Best].size(), 2, CV_64F);
	for (int32 i = 0; i < C.rows; ++i)
	{
		C.at<double>(i, 0) = Contours[Best][i].x;
		C.at<double>(i, 1) = Contours[Best][i].y;
	}
	OutContour = EnsureClockwise(C);
	OutMask = Binary;
	return true;
}

struct FExpansion
{
	cv::Mat OldTargets, NewTargets, AnchorOldPx, AnchorNewPx; // Nx2 f64
};

static bool LoadExpansion(const FString& JsonPath, FExpansion& Out)
{
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *JsonPath))
	{
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return false;
	}
	auto ReadPts = [&](const TCHAR* Key, cv::Mat& M)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Root->TryGetArrayField(Key, Arr) || !Arr)
		{
			return false;
		}
		M.create(Arr->Num(), 2, CV_64F);
		for (int32 i = 0; i < Arr->Num(); ++i)
		{
			const TArray<TSharedPtr<FJsonValue>>& Row = (*Arr)[i]->AsArray();
			M.at<double>(i, 0) = Row[0]->AsNumber();
			M.at<double>(i, 1) = Row[1]->AsNumber();
		}
		return true;
	};
	return ReadPts(TEXT("old_targets"), Out.OldTargets) && ReadPts(TEXT("new_targets"), Out.NewTargets)
		&& ReadPts(TEXT("anchor_old_px"), Out.AnchorOldPx) && ReadPts(TEXT("anchor_new_px"), Out.AnchorNewPx);
}

static cv::Mat ResampleContour(const cv::Mat& Contour, int32 NPts)
{
	const int32 N = Contour.rows;
	int32 TopIdx = 0;
	double TopY = 1e18;
	for (int32 i = 0; i < N; ++i)
	{
		if (Contour.at<double>(i, 1) < TopY) { TopY = Contour.at<double>(i, 1); TopIdx = i; }
	}
	// roll 到頂點開頭＋閉合
	TArray<cv::Point2d> Closed;
	for (int32 i = 0; i < N; ++i)
	{
		const int32 j = (TopIdx + i) % N;
		Closed.Add(cv::Point2d(Contour.at<double>(j, 0), Contour.at<double>(j, 1)));
	}
	const cv::Point2d First = Closed[0]; // 不能 Add 自身元素（擴容別名斷言）
	Closed.Add(First);

	TArray<double> Cum;
	Cum.Add(0);
	for (int32 i = 1; i < Closed.Num(); ++i)
	{
		Cum.Add(Cum[i - 1] + cv::norm(Closed[i] - Closed[i - 1]));
	}
	const double Total = Cum.Last();
	cv::Mat Out(NPts, 2, CV_64F);
	for (int32 i = 0; i < NPts; ++i)
	{
		const double D = Total * i / NPts;
		// searchsorted right - 1
		int32 Idx = Algo::UpperBound(Cum, D) - 1;
		Idx = std::min(Idx, N - 1);
		const double Seg = Cum[Idx + 1] - Cum[Idx];
		const double T = (Seg < 1e-8) ? 0.0 : (D - Cum[Idx]) / Seg;
		Out.at<double>(i, 0) = Closed[Idx].x * (1 - T) + Closed[Idx + 1].x * T;
		Out.at<double>(i, 1) = Closed[Idx].y * (1 - T) + Closed[Idx + 1].y * T;
	}
	return Out;
}

static cv::Mat InflateContour(const cv::Mat& Pts, double Margin)
{
	const int32 N = Pts.rows;
	cv::Mat Out = Pts.clone();
	for (int32 i = 0; i < N; ++i)
	{
		const int32 Ip = (i + 1) % N, Im = (i - 1 + N) % N;
		const double Tx = Pts.at<double>(Ip, 0) - Pts.at<double>(Im, 0);
		const double Ty = Pts.at<double>(Ip, 1) - Pts.at<double>(Im, 1);
		double Nx = Ty, Ny = -Tx;
		const double L = std::sqrt(Nx * Nx + Ny * Ny);
		if (L > 1e-8) { Nx /= L; Ny /= L; }
		Out.at<double>(i, 0) = Pts.at<double>(i, 0) + Nx * Margin;
		Out.at<double>(i, 1) = Pts.at<double>(i, 1) + Ny * Margin;
	}
	return Out;
}

// ---------------------------------------------------------------- warp

static cv::Mat MakeCanvas(const cv::Mat& Image, const cv::Vec3b& Fill, const cv::Mat& SrcPts,
                          cv::Mat& OutAdjSrc, cv::Point2d& OutOffset)
{
	const int32 H = Image.rows, W = Image.cols;
	cv::Mat Canvas(TEX_SIZE, TEX_SIZE, CV_8UC3, cv::Scalar(Fill[0], Fill[1], Fill[2]));
	const int32 Oy = (TEX_SIZE - H) / 2, Ox = (TEX_SIZE - W) / 2;
	Image.copyTo(Canvas(cv::Rect(Ox, Oy, W, H)));
	OutAdjSrc = SrcPts.clone();
	for (int32 i = 0; i < OutAdjSrc.rows; ++i)
	{
		OutAdjSrc.at<double>(i, 0) = std::clamp(OutAdjSrc.at<double>(i, 0) + Ox,
			(double)CANVAS_SAFETY, (double)(TEX_SIZE - CANVAS_SAFETY));
		OutAdjSrc.at<double>(i, 1) = std::clamp(OutAdjSrc.at<double>(i, 1) + Oy,
			(double)CANVAS_SAFETY, (double)(TEX_SIZE - CANVAS_SAFETY));
	}
	OutOffset = cv::Point2d(Ox, Oy);
	return Canvas;
}

static cv::Mat PtsToShape(const cv::Mat& Pts64)
{
	cv::Mat Out(1, Pts64.rows, CV_32FC2);
	for (int32 i = 0; i < Pts64.rows; ++i)
	{
		Out.at<cv::Vec2f>(0, i) = cv::Vec2f((float)Pts64.at<double>(i, 0), (float)Pts64.at<double>(i, 1));
	}
	return Out;
}

static cv::Ptr<cv::ThinPlateSplineShapeTransformer> EstimateTps(const cv::Mat& DstPts, const cv::Mat& SrcPts)
{
	cv::Ptr<cv::ThinPlateSplineShapeTransformer> Tps = cv::createThinPlateSplineShapeTransformer();
	std::vector<cv::DMatch> Matches;
	for (int32 i = 0; i < DstPts.rows; ++i)
	{
		Matches.emplace_back(i, i, 0.f);
	}
	Tps->estimateTransformation(PtsToShape(DstPts), PtsToShape(SrcPts), Matches);
	return Tps;
}

static cv::Mat ApplyTps(const cv::Ptr<cv::ThinPlateSplineShapeTransformer>& Tps, const cv::Mat& Pts64)
{
	cv::Mat Out;
	Tps->applyTransformation(PtsToShape(Pts64), Out);
	cv::Mat R(Pts64.rows, 2, CV_64F);
	for (int32 i = 0; i < Pts64.rows; ++i)
	{
		const cv::Vec2f V = Out.at<cv::Vec2f>(0, i);
		R.at<double>(i, 0) = V[0];
		R.at<double>(i, 1) = V[1];
	}
	return R;
}

static cv::Mat RotatePts(const cv::Mat& Pts, const cv::Point2d& Center, double Deg)
{
	const double R = Deg * CV_PI / 180.0;
	const double C = std::cos(R), S = std::sin(R);
	cv::Mat Out(Pts.rows, 2, CV_64F);
	for (int32 i = 0; i < Pts.rows; ++i)
	{
		const double Qx = Pts.at<double>(i, 0) - Center.x;
		const double Qy = Pts.at<double>(i, 1) - Center.y;
		Out.at<double>(i, 0) = Qx * C - Qy * S + Center.x;
		Out.at<double>(i, 1) = Qx * S + Qy * C + Center.y;
	}
	return Out;
}

static void AlignSymmetry(FCtx& C, cv::Mat& DstAll, const cv::Mat& SrcAll, const cv::Mat& LmCanvas,
                          double IslandCenterX)
{
	cv::Ptr<cv::ThinPlateSplineShapeTransformer> Fwd = EstimateTps(SrcAll, DstAll); // canvas -> texture
	const cv::Mat Pts = ApplyTps(Fwd, LmCanvas);
	const int32 NPairs = 5;
	cv::Mat AxisPts(NPairs + MIDLINE.Num(), 2, CV_64F);
	for (int32 i = 0; i < NPairs; ++i)
	{
		AxisPts.at<double>(i, 0) = (Pts.at<double>(i, 0) + Pts.at<double>(NPairs + i, 0)) / 2;
		AxisPts.at<double>(i, 1) = (Pts.at<double>(i, 1) + Pts.at<double>(NPairs + i, 1)) / 2;
	}
	for (int32 i = 0; i < MIDLINE.Num(); ++i)
	{
		AxisPts.at<double>(NPairs + i, 0) = Pts.at<double>(2 * NPairs + i, 0);
		AxisPts.at<double>(NPairs + i, 1) = Pts.at<double>(2 * NPairs + i, 1);
	}

	cv::Point2d Mean(0, 0);
	for (int32 i = 0; i < AxisPts.rows; ++i)
	{
		Mean.x += AxisPts.at<double>(i, 0);
		Mean.y += AxisPts.at<double>(i, 1);
	}
	Mean *= 1.0 / AxisPts.rows;
	cv::Mat Centered(AxisPts.rows, 2, CV_64F);
	for (int32 i = 0; i < AxisPts.rows; ++i)
	{
		Centered.at<double>(i, 0) = AxisPts.at<double>(i, 0) - Mean.x;
		Centered.at<double>(i, 1) = AxisPts.at<double>(i, 1) - Mean.y;
	}
	cv::Mat Wv, U, Vt;
	cv::SVD::compute(Centered, Wv, U, Vt);
	double D0 = Vt.at<double>(0, 0), D1 = Vt.at<double>(0, 1);
	if (D1 < 0) { D0 = -D0; D1 = -D1; }
	const double Theta = std::atan2(D0, D1) * 180.0 / CV_PI;
	if (std::abs(Theta) > ROT_COMPENSATE_MAX)
	{
		C.Say(FString::Printf(TEXT("WARNING: symmetry-axis tilt %+.1f deg exceeds cap, skipping"), Theta));
		return;
	}
	cv::Point2d Center(0, 0);
	for (int32 i = 0; i < DstAll.rows; ++i)
	{
		Center.x += DstAll.at<double>(i, 0);
		Center.y += DstAll.at<double>(i, 1);
	}
	Center *= 1.0 / DstAll.rows;
	cv::Mat Dst2 = RotatePts(DstAll, Center, Theta);
	cv::Mat MeanM(1, 2, CV_64F);
	MeanM.at<double>(0, 0) = Mean.x;
	MeanM.at<double>(0, 1) = Mean.y;
	const cv::Mat MeanRot = RotatePts(MeanM, Center, Theta);
	const double Dx = std::clamp(IslandCenterX - MeanRot.at<double>(0, 0), -MAX_CENTER_SHIFT, MAX_CENTER_SHIFT);
	for (int32 i = 0; i < Dst2.rows; ++i)
	{
		Dst2.at<double>(i, 0) += Dx;
	}
	DstAll = Dst2;
	C.Say(FString::Printf(TEXT("symmetry aligned: rot %+.2f deg, shift %+.1fpx"), Theta, Dx));
}

// warpImage 內部＝「對每個輸出像素求 T(x)→remap INTER_LINEAR/BORDER_CONSTANT」；
// 建一次 map 後 remap ×5（本體＋4 張 mask）——省掉 4 次 O(像素×控制點) 重算。
static cv::Mat BuildTpsMap(const cv::Ptr<cv::ThinPlateSplineShapeTransformer>& Tps)
{
	cv::Mat Grid(1, TEX_SIZE * TEX_SIZE, CV_32FC2);
	cv::Vec2f* G = Grid.ptr<cv::Vec2f>(0);
	for (int32 y = 0; y < TEX_SIZE; ++y)
	{
		for (int32 x = 0; x < TEX_SIZE; ++x)
		{
			G[y * TEX_SIZE + x] = cv::Vec2f((float)x, (float)y);
		}
	}
	cv::Mat Out;
	Tps->applyTransformation(Grid, Out);
	cv::Mat Map(TEX_SIZE, TEX_SIZE, CV_32FC2);
	FMemory::Memcpy(Map.ptr(), Out.ptr(), sizeof(float) * 2 * TEX_SIZE * TEX_SIZE);
	return Map;
}

static cv::Mat RemapExtra(const cv::Mat& MapXY, const cv::Mat& Extra)
{
	const int32 H = Extra.rows, W = Extra.cols;
	cv::Mat Ec(TEX_SIZE, TEX_SIZE, CV_8U, cv::Scalar(0));
	const int32 Oy = (TEX_SIZE - H) / 2, Ox = (TEX_SIZE - W) / 2;
	Extra.copyTo(Ec(cv::Rect(Ox, Oy, W, H)));
	cv::Mat Out;
	cv::remap(Ec, Out, MapXY, cv::noArray(), cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0));
	return Out;
}

// ---------------------------------------------------------------- 8d flat-field

static cv::Mat SkinFlatfield(FCtx& C, const cv::Mat& Warped, const cv::Mat& UvMask,
                             const cv::Mat& ContentMask, const cv::Mat& SkinTex, const cv::Vec3b& SkinColor)
{
	const int32 N = TEX_SIZE;
	cv::Mat Inside(N, N, CV_8U), Content(N, N, CV_8U), Probe(N, N, CV_8U);
	int64 NProbe = 0;
	for (int32 y = 0; y < N; ++y)
	{
		const uint8* Uv = UvMask.ptr<uint8>(y);
		const uint8* Cm = ContentMask.ptr<uint8>(y);
		const uint8* St = SkinTex.ptr<uint8>(y);
		uint8* I = Inside.ptr<uint8>(y);
		uint8* Co = Content.ptr<uint8>(y);
		uint8* P = Probe.ptr<uint8>(y);
		for (int32 x = 0; x < N; ++x)
		{
			I[x] = Uv[x] > 0;
			Co[x] = Cm[x] > 127;
			P[x] = (St[x] > 127 && Co[x]) ? 1 : 0;
			NProbe += P[x];
		}
	}
	cv::Mat LabImg;
	cv::cvtColor(Warped, LabImg, cv::COLOR_BGR2Lab);
	const cv::Vec3f LabT = BgrToLabF(SkinColor);
	auto CoreDe = [&](const cv::Mat& Img)
	{
		cv::Mat Lab2;
		cv::cvtColor(Img, Lab2, cv::COLOR_BGR2Lab);
		double S0 = 0, S1 = 0, S2 = 0;
		int64 Cnt = 0;
		for (int32 y = 0; y < N; ++y)
		{
			const uint8* P = Probe.ptr<uint8>(y);
			const cv::Vec3b* L = Lab2.ptr<cv::Vec3b>(y);
			for (int32 x = 0; x < N; ++x)
			{
				if (P[x]) { S0 += L[x][0]; S1 += L[x][1]; S2 += L[x][2]; ++Cnt; }
			}
		}
		if (!Cnt) { return 0.0; }
		const double D0 = S0 / Cnt - LabT[0], D1 = S1 / Cnt - LabT[1], D2 = S2 / Cnt - LabT[2];
		return std::sqrt(D0 * D0 + D1 * D1 + D2 * D2);
	};
	const double DeBefore = CoreDe(Warped);
	if (NProbe < 5000)
	{
		C.Say(FString::Printf(TEXT("face-core dE vs body: %.1f -> %.1f (flat-field skipped)"), DeBefore, DeBefore));
		return Warped.clone();
	}

	// linear 域比值場
	cv::Mat Lin(N, N, CV_32FC3);
	for (int32 y = 0; y < N; ++y)
	{
		const cv::Vec3b* Wr = Warped.ptr<cv::Vec3b>(y);
		cv::Vec3f* L = Lin.ptr<cv::Vec3f>(y);
		for (int32 x = 0; x < N; ++x)
		{
			for (int32 c = 0; c < 3; ++c) { L[x][c] = std::pow(Wr[x][c] / 255.f, 2.2f); }
		}
	}
	cv::Vec3f TLin;
	for (int32 c = 0; c < 3; ++c) { TLin[c] = std::pow(SkinColor[c] / 255.f, 2.2f); }

	cv::Mat MF(N, N, CV_32F);
	for (int32 y = 0; y < N; ++y)
	{
		const uint8* P = Probe.ptr<uint8>(y);
		float* M = MF.ptr<float>(y);
		for (int32 x = 0; x < N; ++x) { M[x] = P[x] ? 1.f : 0.f; }
	}
	cv::Mat M3;
	{
		cv::Mat Ch[3] = { MF, MF, MF };
		cv::merge(Ch, 3, M3);
	}
	cv::Mat Num = BlurF(Lin.mul(M3), FLATFIELD_SIGMA);
	cv::Mat Den = BlurF(MF, FLATFIELD_SIGMA);
	cv::Mat Num2 = BlurF(Num, FLATFIELD_SIGMA * 2);
	cv::Mat Den2 = BlurF(Den, FLATFIELD_SIGMA * 2);

	cv::Mat Ratio(N, N, CV_32FC3);
	for (int32 y = 0; y < N; ++y)
	{
		const cv::Vec3f* Nu = Num.ptr<cv::Vec3f>(y);
		const float* De = Den.ptr<float>(y);
		const cv::Vec3f* Nu2 = Num2.ptr<cv::Vec3f>(y);
		const float* De2 = Den2.ptr<float>(y);
		cv::Vec3f* R = Ratio.ptr<cv::Vec3f>(y);
		for (int32 x = 0; x < N; ++x)
		{
			cv::Vec3f L;
			if (De[x] < 0.02f)
			{
				for (int32 c = 0; c < 3; ++c) { L[c] = Nu2[x][c] / std::max(De2[x], 1e-5f); }
			}
			else
			{
				for (int32 c = 0; c < 3; ++c) { L[c] = Nu[x][c] / std::max(De[x], 1e-5f); }
			}
			for (int32 c = 0; c < 3; ++c)
			{
				const float V = std::pow(TLin[c] / std::max(L[c], 1e-4f), (float)FLATFIELD_K);
				R[x][c] = std::clamp(V, (float)FLATFIELD_RATIO_LO, (float)FLATFIELD_RATIO_HI);
			}
		}
	}
	Ratio = BlurF(Ratio, FLATFIELD_SMOOTH);

	// probe 均值重定心（mean vs median 偏亮補償）
	cv::Vec3d AppliedMean(0, 0, 0);
	int64 Cnt = 0;
	for (int32 y = 0; y < N; ++y)
	{
		const uint8* P = Probe.ptr<uint8>(y);
		const cv::Vec3f* L = Lin.ptr<cv::Vec3f>(y);
		const cv::Vec3f* R = Ratio.ptr<cv::Vec3f>(y);
		for (int32 x = 0; x < N; ++x)
		{
			if (P[x])
			{
				for (int32 c = 0; c < 3; ++c) { AppliedMean[c] += (double)L[x][c] * R[x][c]; }
				++Cnt;
			}
		}
	}
	cv::Vec3f Corr;
	for (int32 c = 0; c < 3; ++c)
	{
		Corr[c] = (float)std::clamp(TLin[c] / std::max(AppliedMean[c] / Cnt, 1e-5), 0.8, 1.25);
	}

	cv::Mat Out(N, N, CV_8UC3);
	TArray<float> AbsLogRatio;
	for (int32 y = 0; y < N; ++y)
	{
		const uint8* I = Inside.ptr<uint8>(y);
		const cv::Vec3f* L = Lin.ptr<cv::Vec3f>(y);
		const cv::Vec3f* R = Ratio.ptr<cv::Vec3f>(y);
		const cv::Vec3b* Wr = Warped.ptr<cv::Vec3b>(y);
		cv::Vec3b* O = Out.ptr<cv::Vec3b>(y);
		for (int32 x = 0; x < N; ++x)
		{
			if (I[x])
			{
				for (int32 c = 0; c < 3; ++c)
				{
					const float RC = R[x][c] * Corr[c];
					const float V = std::pow(std::clamp(L[x][c] * RC, 0.f, 1.f), 1.f / 2.2f) * 255.f;
					O[x][c] = (uint8)std::clamp(V, 0.f, 255.f);
					AbsLogRatio.Add(std::abs(std::log(RC)));
				}
			}
			else
			{
				O[x] = Wr[x];
			}
		}
	}
	const double DeAfter = CoreDe(Out);
	const double P95 = Percentile(AbsLogRatio, 95.0);
	C.Say(FString::Printf(TEXT("face-core dE vs body: %.1f -> %.1f | lighting |log-ratio| p95 %.3f"),
		DeBefore, DeAfter, P95));
	return Out;
}

// ---------------------------------------------------------------- 10b poisson

static cv::Mat PoissonMembraneToSkin(FCtx& C, const cv::Mat& Warped, const cv::Mat& UvMask,
                                     const cv::Mat& ContentMask, const cv::Vec3b& SkinColor)
{
	const int32 N = TEX_SIZE;
	cv::Mat InsideFull(N, N, CV_8U);
	for (int32 y = 0; y < N; ++y)
	{
		const uint8* Uv = UvMask.ptr<uint8>(y);
		uint8* I = InsideFull.ptr<uint8>(y);
		for (int32 x = 0; x < N; ++x) { I[x] = Uv[x] > 0 ? 1 : 0; }
	}
	const cv::Vec3f T((float)SkinColor[0], (float)SkinColor[1], (float)SkinColor[2]);
	cv::Mat Er;
	cv::erode(InsideFull, Er, cv::Mat::ones(3, 3, CV_8U), cv::Point(-1, -1), POISSON_EDGE_ERODE);
	cv::Mat RingFull(N, N, CV_8U, cv::Scalar(0));
	cv::Mat OffFull(N, N, CV_32FC3, cv::Scalar(0, 0, 0));
	for (int32 y = 0; y < N; ++y)
	{
		const uint8* I = InsideFull.ptr<uint8>(y);
		const uint8* E = Er.ptr<uint8>(y);
		const uint8* Cm = ContentMask.ptr<uint8>(y);
		const cv::Vec3b* Wr = Warped.ptr<cv::Vec3b>(y);
		uint8* R = RingFull.ptr<uint8>(y);
		cv::Vec3f* O = OffFull.ptr<cv::Vec3f>(y);
		for (int32 x = 0; x < N; ++x)
		{
			if (I[x] && !E[x] && Cm[x] <= 127)
			{
				R[x] = 1;
				O[x] = cv::Vec3f(T[0] - Wr[x][0], T[1] - Wr[x][1], T[2] - Wr[x][2]);
			}
		}
	}

	cv::Mat RingFullF(N, N, CV_32F);
	for (int32 y = 0; y < N; ++y)
	{
		const uint8* R = RingFull.ptr<uint8>(y);
		float* F = RingFullF.ptr<float>(y);
		for (int32 x = 0; x < N; ++x) { F[x] = R[x] ? 1.f : 0.f; }
	}

	cv::Mat Mem; // s×s×3
	const int32 Scales[4] = { 32, 64, 128, POISSON_SOLVE_RES };
	for (int32 si = 0; si < 4; ++si)
	{
		const int32 S = Scales[si];
		cv::Mat InsideS;
		cv::resize(InsideFull, InsideS, cv::Size(S, S), 0, 0, cv::INTER_NEAREST);
		cv::Mat ErS;
		cv::erode(InsideS, ErS, cv::Mat::ones(3, 3, CV_8U));
		cv::Mat RingS(S, S, CV_8U);
		for (int32 y = 0; y < S; ++y)
		{
			const uint8* I = InsideS.ptr<uint8>(y);
			const uint8* E = ErS.ptr<uint8>(y);
			uint8* R = RingS.ptr<uint8>(y);
			for (int32 x = 0; x < S; ++x) { R[x] = (I[x] && !E[x]) ? 1 : 0; }
		}
		cv::Mat OffWeighted(N, N, CV_32FC3);
		{
			cv::Mat Ch[3] = { RingFullF, RingFullF, RingFullF };
			cv::Mat R3;
			cv::merge(Ch, 3, R3);
			OffWeighted = OffFull.mul(R3);
		}
		cv::Mat NumS, DenS;
		cv::resize(OffWeighted, NumS, cv::Size(S, S), 0, 0, cv::INTER_AREA);
		cv::resize(RingFullF, DenS, cv::Size(S, S), 0, 0, cv::INTER_AREA);
		for (int32 It = 0; It < 12; ++It)
		{
			float MinDen = 1e9f;
			for (int32 y = 0; y < S; ++y)
			{
				const uint8* R = RingS.ptr<uint8>(y);
				const float* D = DenS.ptr<float>(y);
				for (int32 x = 0; x < S; ++x)
				{
					if (R[x]) { MinDen = std::min(MinDen, D[x]); }
				}
			}
			if (MinDen > 1e-4f) { break; }
			NumS = BlurF(NumS, 2.0);
			DenS = BlurF(DenS, 2.0);
		}
		cv::Mat BVal(S, S, CV_32FC3);
		for (int32 y = 0; y < S; ++y)
		{
			const cv::Vec3f* Nu = NumS.ptr<cv::Vec3f>(y);
			const float* De = DenS.ptr<float>(y);
			cv::Vec3f* B = BVal.ptr<cv::Vec3f>(y);
			for (int32 x = 0; x < S; ++x)
			{
				for (int32 c = 0; c < 3; ++c) { B[x][c] = Nu[x][c] / std::max(De[x], 1e-6f); }
			}
		}
		if (Mem.empty())
		{
			Mem = cv::Mat(S, S, CV_32FC3, cv::Scalar(0, 0, 0));
		}
		else
		{
			cv::resize(Mem, Mem, cv::Size(S, S), 0, 0, cv::INTER_LINEAR);
		}

		const int32 Iters = (S <= 64) ? 600 : 400;
		cv::Mat Avg(S, S, CV_32FC3);
		for (int32 It = 0; It < Iters; ++It)
		{
			// np.roll＝環繞位移
			for (int32 y = 0; y < S; ++y)
			{
				const cv::Vec3f* Up = Mem.ptr<cv::Vec3f>((y - 1 + S) % S);
				const cv::Vec3f* Dn = Mem.ptr<cv::Vec3f>((y + 1) % S);
				const cv::Vec3f* Row = Mem.ptr<cv::Vec3f>(y);
				cv::Vec3f* A = Avg.ptr<cv::Vec3f>(y);
				for (int32 x = 0; x < S; ++x)
				{
					const cv::Vec3f L = Row[(x - 1 + S) % S];
					const cv::Vec3f R = Row[(x + 1) % S];
					A[x] = (Up[x] + Dn[x] + L + R) * 0.25f;
				}
			}
			for (int32 y = 0; y < S; ++y)
			{
				const uint8* I = InsideS.ptr<uint8>(y);
				const uint8* R = RingS.ptr<uint8>(y);
				const cv::Vec3f* A = Avg.ptr<cv::Vec3f>(y);
				const cv::Vec3f* B = BVal.ptr<cv::Vec3f>(y);
				cv::Vec3f* M = Mem.ptr<cv::Vec3f>(y);
				for (int32 x = 0; x < S; ++x)
				{
					if (R[x]) { M[x] = B[x]; }
					else if (I[x]) { M[x] = A[x]; }
				}
			}
		}
		// 邊外延伸（下一階升採樣不與 0 稀釋）
		cv::Mat InsideSF(S, S, CV_32F);
		for (int32 y = 0; y < S; ++y)
		{
			const uint8* I = InsideS.ptr<uint8>(y);
			float* F = InsideSF.ptr<float>(y);
			for (int32 x = 0; x < S; ++x) { F[x] = I[x] ? 1.f : 0.f; }
		}
		for (int32 It = 0; It < 6; ++It)
		{
			cv::Mat I3;
			{
				cv::Mat Ch[3] = { InsideSF, InsideSF, InsideSF };
				cv::merge(Ch, 3, I3);
			}
			cv::Mat Mn = BlurF(Mem.mul(I3), 2.0);
			cv::Mat Wn = BlurF(InsideSF, 2.0);
			for (int32 y = 0; y < S; ++y)
			{
				const uint8* I = InsideS.ptr<uint8>(y);
				const cv::Vec3f* MnR = Mn.ptr<cv::Vec3f>(y);
				const float* WnR = Wn.ptr<float>(y);
				cv::Vec3f* M = Mem.ptr<cv::Vec3f>(y);
				for (int32 x = 0; x < S; ++x)
				{
					if (!I[x])
					{
						for (int32 c = 0; c < 3; ++c) { M[x][c] = MnR[x][c] / std::max(WnR[x], 1e-6f); }
					}
				}
			}
		}
	}

	cv::Mat MemFull;
	cv::resize(Mem, MemFull, cv::Size(N, N), 0, 0, cv::INTER_LINEAR);

	cv::Mat DistIn;
	cv::distanceTransform(InsideFull, DistIn, cv::DIST_L2, 5);
	cv::Mat Out(N, N, CV_8UC3);
	double MemAbsMax = 0.0;
	for (int32 y = 0; y < N; ++y)
	{
		const uint8* I = InsideFull.ptr<uint8>(y);
		const float* D = DistIn.ptr<float>(y);
		const cv::Vec3f* M = MemFull.ptr<cv::Vec3f>(y);
		const cv::Vec3b* Wr = Warped.ptr<cv::Vec3b>(y);
		cv::Vec3b* O = Out.ptr<cv::Vec3b>(y);
		for (int32 x = 0; x < N; ++x)
		{
			float Sw = std::clamp(1.f - D[x] / (float)EDGE_SNAP_PX, 0.f, 1.f);
			Sw *= I[x] ? 1.f : 0.f;
			for (int32 c = 0; c < 3; ++c)
			{
				MemAbsMax = std::max(MemAbsMax, (double)std::abs(M[x][c]));
				float V = Wr[x][c] + (I[x] ? M[x][c] : 0.f);
				V = V * (1.f - Sw) + T[c] * Sw;
				O[x][c] = (uint8)std::clamp(V, 0.f, 255.f);
			}
		}
	}

	// island-edge QA（user 可見的臉/身交界指標）
	cv::Mat LabOut;
	cv::cvtColor(Out, LabOut, cv::COLOR_BGR2Lab);
	const cv::Vec3f LabT = BgrToLabF(SkinColor);
	TArray<float> DE;
	for (int32 y = 0; y < N; ++y)
	{
		const uint8* I = InsideFull.ptr<uint8>(y);
		const float* D = DistIn.ptr<float>(y);
		const cv::Vec3b* L = LabOut.ptr<cv::Vec3b>(y);
		for (int32 x = 0; x < N; ++x)
		{
			if (I[x] && D[x] <= 4.f)
			{
				const float D0 = L[x][0] - LabT[0], D1 = L[x][1] - LabT[1], D2 = L[x][2] - LabT[2];
				DE.Add(std::sqrt(D0 * D0 + D1 * D1 + D2 * D2));
			}
		}
	}
	double Mean = 0;
	for (float V : DE) { Mean += V; }
	Mean = DE.Num() ? Mean / DE.Num() : 0;
	const double P95 = Percentile(DE, 95.0);
	C.Say(FString::Printf(TEXT("island-edge dE vs body: mean %.2f p95 %.2f | membrane |max| %.1f"),
		Mean, P95, MemAbsMax));
	return Out;
}

// ---------------------------------------------------------------- eyes closed

static cv::Mat EyesClosedVariant(FCtx& C, const cv::Mat& Warped, const TArray<cv::Mat>& EyeMasks, int32& OutDone)
{
	cv::Mat Out = Warped.clone();
	OutDone = 0;
	for (const cv::Mat& Em : EyeMasks)
	{
		cv::Mat M(Em.size(), CV_8U);
		int64 Sum = 0;
		int32 X0 = Em.cols, X1 = -1;
		for (int32 y = 0; y < Em.rows; ++y)
		{
			const uint8* E = Em.ptr<uint8>(y);
			uint8* Mr = M.ptr<uint8>(y);
			for (int32 x = 0; x < Em.cols; ++x)
			{
				Mr[x] = (E[x] > 127) ? 1 : 0;
				if (Mr[x])
				{
					++Sum;
					X0 = std::min(X0, x);
					X1 = std::max(X1, x);
				}
			}
		}
		if (Sum < 40)
		{
			C.Say(TEXT("WARNING: eye mask too small after warp, eye skipped"));
			continue;
		}
		const int32 EyeW = std::max(X1 - X0, 8);
		const int32 Grow = std::max(2, (int32)std::lround(EyeW * EYELID_DILATE_RATIO));
		cv::Mat Md;
		cv::dilate(M, Md, cv::Mat::ones(3, 3, CV_8U), cv::Point(-1, -1), Grow);
		cv::Mat Md255;
		Md.convertTo(Md255, CV_8U, 255);
		cv::Mat Filled;
		cv::inpaint(Out, Md255, Filled, 5, cv::INPAINT_TELEA);

		// TELEA 補丁低通＋羽化＋再顆粒
		cv::Mat FilledF;
		Filled.convertTo(FilledF, CV_32FC3);
		cv::Mat Sm = BlurF(FilledF, std::max(2.0, EyeW * 0.05));
		cv::Mat MdF(Md.size(), CV_32F);
		for (int32 y = 0; y < Md.rows; ++y)
		{
			const uint8* Mr = Md.ptr<uint8>(y);
			float* F = MdF.ptr<float>(y);
			for (int32 x = 0; x < Md.cols; ++x) { F[x] = Mr[x] ? 1.f : 0.f; }
		}
		cv::Mat AF = BlurF(MdF, std::max(2.0, EyeW * 0.06));
		FGrainRng Rng(7);
		cv::Mat G = Rng.Field(M.rows, M.cols, 2.5);
		for (int32 y = 0; y < Out.rows; ++y)
		{
			const float* A = AF.ptr<float>(y);
			const float* Gr = G.ptr<float>(y);
			const cv::Vec3f* S = Sm.ptr<cv::Vec3f>(y);
			cv::Vec3b* O = Out.ptr<cv::Vec3b>(y);
			for (int32 x = 0; x < Out.cols; ++x)
			{
				const float Af = std::clamp(A[x], 0.f, 1.f);
				if (Af <= 0.f) { continue; }
				for (int32 c = 0; c < 3; ++c)
				{
					const float V = O[x][c] * (1.f - Af) + (S[x][c] + Gr[x]) * Af;
					O[x][c] = (uint8)std::clamp(V, 0.f, 255.f);
				}
			}
		}

		// 閉眼睫毛線＝眼開口下緣的二次擬合
		TArray<double> Cols, Ys;
		for (int32 x = X0; x <= X1; ++x)
		{
			int32 MaxRow = -1;
			for (int32 y = 0; y < M.rows; ++y)
			{
				if (M.at<uint8>(y, x)) { MaxRow = y; }
			}
			if (MaxRow >= 0)
			{
				Cols.Add(x);
				Ys.Add(MaxRow);
			}
		}
		if (Cols.Num() < 3) { continue; }
		// polyfit deg2（正規方程、double）
		double Sx[5] = { 0, 0, 0, 0, 0 }, Sy[3] = { 0, 0, 0 };
		for (int32 i = 0; i < Cols.Num(); ++i)
		{
			const double X = Cols[i], Y = Ys[i];
			double P = 1;
			for (int32 k = 0; k < 5; ++k) { Sx[k] += P; P *= X; }
			P = 1;
			for (int32 k = 0; k < 3; ++k) { Sy[k] += Y * P; P *= X; }
		}
		cv::Mat A2 = (cv::Mat_<double>(3, 3) << Sx[4], Sx[3], Sx[2], Sx[3], Sx[2], Sx[1], Sx[2], Sx[1], Sx[0]);
		cv::Mat B2 = (cv::Mat_<double>(3, 1) << Sy[2], Sy[1], Sy[0]);
		cv::Mat Coef;
		cv::solve(A2, B2, Coef, cv::DECOMP_SVD);
		std::vector<cv::Point> Pts;
		for (int32 x = X0; x <= X1; ++x)
		{
			const double Y = Coef.at<double>(0) * x * x + Coef.at<double>(1) * x + Coef.at<double>(2);
			Pts.emplace_back(x, (int32)std::lround(Y));
		}

		const int32 Thick = std::max(2, (int32)std::lround(EyeW * EYELID_LINE_W_RATIO));
		cv::Mat LineM(M.size(), CV_32F, cv::Scalar(0));
		cv::polylines(LineM, std::vector<std::vector<cv::Point>>{ Pts }, false, cv::Scalar(1.0), Thick, cv::LINE_AA);

		// 皺褶陰影（寬＋淡）→睫毛線（細＋深）
		cv::Mat Shadow = BlurF(LineM, std::max(2.0, EyeW * EYELID_SHADOW_SIGMA_RATIO));
		double ShMax;
		cv::minMaxLoc(Shadow, nullptr, &ShMax);
		for (int32 y = 0; y < Out.rows; ++y)
		{
			const float* Sh = Shadow.ptr<float>(y);
			cv::Vec3b* O = Out.ptr<cv::Vec3b>(y);
			for (int32 x = 0; x < Out.cols; ++x)
			{
				const float SNorm = Sh[x] / std::max((float)ShMax, 1e-6f);
				if (SNorm <= 0.f) { continue; }
				for (int32 c = 0; c < 3; ++c)
				{
					const float V = O[x][c] * (1.f - (float)EYELID_SHADOW_ALPHA * SNorm);
					O[x][c] = (uint8)std::clamp(V, 0.f, 255.f);
				}
			}
		}

		cv::Mat Ring;
		cv::dilate(Md, Ring, cv::Mat::ones(3, 3, CV_8U), cv::Point(-1, -1), 3);
		cv::Mat RingOnly(Ring.size(), CV_8U);
		for (int32 y = 0; y < Ring.rows; ++y)
		{
			const uint8* R = Ring.ptr<uint8>(y);
			const uint8* Mm = Md.ptr<uint8>(y);
			uint8* Ro = RingOnly.ptr<uint8>(y);
			for (int32 x = 0; x < Ring.cols; ++x) { Ro[x] = (R[x] && !Mm[x]) ? 1 : 0; }
		}
		cv::Vec3d LidTone = cv::countNonZero(RingOnly) ? MedianBgr(Out, RingOnly) : cv::Vec3d(120, 140, 170);
		const cv::Vec3d LineCol = LidTone * EYELID_LINE_DARKEN;
		cv::Mat Aa = BlurF(LineM, 1.2);
		double AaMax;
		cv::minMaxLoc(Aa, nullptr, &AaMax);
		for (int32 y = 0; y < Out.rows; ++y)
		{
			const float* A = Aa.ptr<float>(y);
			cv::Vec3b* O = Out.ptr<cv::Vec3b>(y);
			for (int32 x = 0; x < Out.cols; ++x)
			{
				const float Al = std::clamp(A[x] / std::max((float)AaMax, 1e-6f), 0.f, 1.f) * (float)EYELID_LINE_ALPHA;
				if (Al <= 0.f) { continue; }
				for (int32 c = 0; c < 3; ++c)
				{
					const float V = O[x][c] * (1.f - Al) + (float)LineCol[c] * Al;
					O[x][c] = (uint8)std::clamp(V, 0.f, 255.f);
				}
			}
		}
		++OutDone;
	}
	return Out;
}

// ---------------------------------------------------------------- main pipeline

bool SelfieToFaceTexture(FCtx& C, const cv::Mat& SelfieIn, FBakeResult& Out, FString& Err)
{
	// 2. 讀入＋縮到 1600
	cv::Mat Selfie = SelfieIn;
	int32 H = Selfie.rows, W = Selfie.cols;
	if (std::max(H, W) > MAX_SELFIE_SIDE)
	{
		const double S = (double)MAX_SELFIE_SIDE / std::max(H, W);
		cv::resize(Selfie, Selfie, cv::Size((int32)(W * S), (int32)(H * S)), 0, 0, cv::INTER_AREA);
		H = Selfie.rows;
		W = Selfie.cols;
		C.Say(FString::Printf(TEXT("downscaled to %dx%d"), W, H));
	}

	// 3. 特徵點＋roll 校正
	C.Say(TEXT("3. Detecting face landmarks..."));
	cv::Mat AllLm;
	if (!C.Landmarks->Detect(Selfie, AllLm, Err))
	{
		Err = FString::Printf(TEXT("ERROR: No face detected (%s)"), *Err);
		return false;
	}
	{
		const double RollDeg = std::atan2(AllLm.at<double>(263, 1) - AllLm.at<double>(33, 1),
			AllLm.at<double>(263, 0) - AllLm.at<double>(33, 0)) * 180.0 / CV_PI;
		if (std::abs(RollDeg) >= ROLL_CORRECT_MIN_DEG && std::abs(RollDeg) <= ROLL_CORRECT_MAX_DEG)
		{
			cv::Point2d Center(0, 0);
			for (int32 I : FACE_OVAL_ORDER)
			{
				Center.x += AllLm.at<double>(I, 0);
				Center.y += AllLm.at<double>(I, 1);
			}
			Center *= 1.0 / FACE_OVAL_ORDER.Num();
			const cv::Mat R = cv::getRotationMatrix2D(cv::Point2f((float)Center.x, (float)Center.y), RollDeg, 1.0);
			cv::warpAffine(Selfie, Selfie, R, Selfie.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
			if (!C.Landmarks->Detect(Selfie, AllLm, Err))
			{
				Err = FString::Printf(TEXT("ERROR: No face detected after roll (%s)"), *Err);
				return false;
			}
			C.Say(FString::Printf(TEXT("roll corrected: %+.1f deg"), RollDeg));
		}
	}
	cv::Mat SelfieContour;
	FEllipse Ellipse;
	if (!BuildSelfieContour(AllLm, SelfieContour, Ellipse))
	{
		Err = TEXT("contour build failed");
		return false;
	}
	cv::Mat SelfieSampled = ResampleContour(SelfieContour, N_CONTOUR_PTS);
	const double FaceWidth = Ellipse.A * 2.0; // ellipse_params[1][0] = w_diam
	const double DeflatePx = FaceWidth * MASK_DEFLATE_RATIO;
	const double InflatePx = FaceWidth * SELFIE_INFLATE_RATIO;
	C.Say(FString::Printf(TEXT("face width: %.0fpx, mask deflate: %.1fpx, contour inflate: %.1fpx"),
		FaceWidth, DeflatePx, InflatePx));

	// 4. BiSeNet
	C.Say(TEXT("4. BiSeNet head segmentation..."));
	cv::Mat Parsing;
	if (!ParseSelfie(C, Selfie, Parsing, Err))
	{
		return false;
	}

	// v7 曝光正規化＋白平衡防護
	{
		cv::Mat SkinE = ResizeNearest(LabelMask(Parsing, { SKIN_LABEL }), W, H);
		if (cv::countNonZero(SkinE) > 0)
		{
			cv::Vec3d Gains(1, 1, 1);
			const cv::Vec3d Med = MedianBgr(Selfie, SkinE);
			if (Med[0] > 0.95 * Med[1])
			{
				const double Wb = std::clamp(std::pow(0.93 * Med[1] / std::max(Med[0], 1.0), 2.2), 0.5, 1.0);
				Gains[0] = Wb;
				C.Say(FString::Printf(TEXT("white-balance guard: skin B%.0f > G%.0f (magenta cast), B gain %.2f"),
					Med[0], Med[1], Wb));
			}
			cv::Mat Gray;
			cv::cvtColor(Selfie, Gray, cv::COLOR_BGR2GRAY);
			const double GrayMed = MedianGray(Gray, SkinE);
			if (!(EXPOSURE_BAND_LO <= GrayMed && GrayMed <= EXPOSURE_BAND_HI))
			{
				const double GLin = std::clamp(std::pow(EXPOSURE_TARGET_GRAY / 255.0, 2.2)
					/ std::max(std::pow(GrayMed / 255.0, 2.2), 1e-4), EXPOSURE_GAIN_LO, EXPOSURE_GAIN_HI);
				Gains *= GLin;
				C.Say(FString::Printf(TEXT("exposure normalized: skin gray %.0f -> %.0f (linear gain %.2f)"),
					GrayMed, EXPOSURE_TARGET_GRAY, GLin));
			}
			if (std::abs(Gains[0] - 1) > 0.02 || std::abs(Gains[1] - 1) > 0.02 || std::abs(Gains[2] - 1) > 0.02)
			{
				for (int32 y = 0; y < H; ++y)
				{
					cv::Vec3b* P = Selfie.ptr<cv::Vec3b>(y);
					for (int32 x = 0; x < W; ++x)
					{
						for (int32 c = 0; c < 3; ++c)
						{
							const double Lin = std::pow(P[x][c] / 255.0, 2.2) * Gains[c];
							P[x][c] = (uint8)std::clamp(std::pow(std::clamp(Lin, 0.0, 1.0), 1 / 2.2) * 255.0, 0.0, 255.0);
						}
					}
				}
				if (!ParseSelfie(C, Selfie, Parsing, Err))
				{
					return false;
				}
			}
		}
	}

	cv::Mat HeadMask = BuildHeadMask(Parsing, W, H, DeflatePx, SelfieContour);
	const int32 HeadArea = cv::countNonZero(HeadMask);
	if (HeadArea < 0.01 * H * W)
	{
		Err = TEXT("ERROR: Head mask too small, segmentation failed");
		return false;
	}
	cv::Vec3b SkinColor = ExtractSkinColor(Parsing, Selfie);
	cv::Vec3b HairColor;
	const bool bHasHair = ExtractHairColor(Parsing, Selfie, HairColor);
	C.Say(FString::Printf(TEXT("head mask: %d px (%.1f%%); hair: %s"), HeadArea,
		100.0 * HeadArea / (H * W), bHasHair ? *FString::Printf(TEXT("BGR(%d,%d,%d)"),
			HairColor[0], HairColor[1], HairColor[2]) : TEXT("none (bald)")));

	// 4b. 鬍鬚＋眉毛
	C.Say(TEXT("4b. Selfie analysis: beard detection + brow status..."));
	cv::Mat LipsGuard = LipsGuardMask(Parsing, W, H, FaceWidth);
	FBrowStatus Brows = BrowCoverStatus(Parsing, AllLm, W, H, FaceWidth);
	cv::Mat BeardPx;
	FBeardMeta BeardMeta = BeardAnalysis(Selfie, Parsing, AllLm, SkinColor, bHasHair, HairColor,
		FaceWidth, BeardPx);
	const bool bBearded = BeardMeta.Coverage >= BEARD_REMOVE_MIN_COVERAGE;
	cv::Mat BeardDet; // bool（空=無）
	if (bBearded && cv::countNonZero(BeardPx) > 0)
	{
		const int32 Grow = std::max(1, (int32)std::lround(FaceWidth * BEARD_DILATE_RATIO));
		cv::dilate(BeardPx, BeardDet, cv::Mat::ones(3, 3, CV_8U), cv::Point(-1, -1), Grow);
		// 乾淨膚色（鬍偵測抓的正是最暗鬍像素）
		cv::Mat SkinFull = ResizeNearest(LabelMask(Parsing, { SKIN_LABEL }), W, H);
		cv::Mat SkinNoBeard(H, W, CV_8U, cv::Scalar(0));
		int64 NPx = 0;
		for (int32 y = 0; y < H; ++y)
		{
			const uint8* S = SkinFull.ptr<uint8>(y);
			const uint8* B = BeardDet.ptr<uint8>(y);
			uint8* O = SkinNoBeard.ptr<uint8>(y);
			for (int32 x = 0; x < W; ++x)
			{
				if (S[x] && !B[x]) { O[x] = 1; ++NPx; }
			}
		}
		if (NPx > 0)
		{
			const cv::Vec3d M = MedianBgr(Selfie, SkinNoBeard);
			SkinColor = cv::Vec3b((uint8)M[0], (uint8)M[1], (uint8)M[2]);
		}
		// BEARD_KEEP_IN_TEXTURE：BiSeNet 標成 HAIR 的密鬍像素接回 head mask
		cv::Mat HairK = ResizeNearest(LabelMask(Parsing, { HAIR_LABEL }), W, H);
		int64 NGraft = 0;
		for (int32 y = 0; y < H; ++y)
		{
			const uint8* B = BeardDet.ptr<uint8>(y);
			const uint8* Hk = HairK.ptr<uint8>(y);
			uint8* Hm = HeadMask.ptr<uint8>(y);
			for (int32 x = 0; x < W; ++x)
			{
				if (B[x] && Hk[x]) { Hm[x] = 255; ++NGraft; }
			}
		}
		C.Say(FString::Printf(TEXT("beard kept in texture: %lld hair-labeled beard px grafted"), NGraft));
	}
	C.Say(FString::Printf(TEXT("beard coverage %.2f (kept in texture); brows covered: L=%d R=%d (frac L=%.3f R=%.3f)"),
		BeardMeta.Coverage, Brows.L.bCovered ? 1 : 0, Brows.R.bCovered ? 1 : 0,
		Brows.L.HairFraction, Brows.R.HairFraction));

	// 5. 自然膚色填充
	C.Say(TEXT("5. Natural skin fill outside head mask..."));
	cv::Mat SelfieFilled = NaturalSkinFill(Selfie, HeadMask, SkinColor, FaceWidth, LipsGuard);

	// 5b.（鬍區重生分支＝死路；brow 修復＋鏡射＋縫環照跑）
	C.Say(TEXT("5b. LaMa passes on the filled image..."));
	cv::Mat Gray;
	cv::cvtColor(SelfieFilled, Gray, cv::COLOR_BGR2GRAY);
	cv::Mat HairFull = ResizeNearest(LabelMask(Parsing, { HAIR_LABEL }), W, H);
	cv::Mat SkinFull = ResizeNearest(LabelMask(Parsing, { SKIN_LABEL }), W, H);
	double SkinLum = 128.0;
	{
		cv::Mat SkinBool(H, W, CV_8U);
		for (int32 y = 0; y < H; ++y)
		{
			const uint8* S = SkinFull.ptr<uint8>(y);
			uint8* B = SkinBool.ptr<uint8>(y);
			for (int32 x = 0; x < W; ++x) { B[x] = S[x] ? 1 : 0; }
		}
		if (cv::countNonZero(SkinBool))
		{
			SkinLum = MedianGray(Gray, SkinBool);
		}
	}
	cv::Mat BrowHoles(H, W, CV_8U, cv::Scalar(0));
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const FBrowSide& S = (Side == 0) ? Brows.L : Brows.R;
		if (S.bCovered) { continue; }
		const int32 Xm = (int32)(0.04 * FaceWidth);
		const int32 Bx0 = std::max(S.Box[0] - Xm, 0), Bx1 = std::min(S.Box[2] + Xm, W - 1);
		for (int32 y = S.Box[1]; y <= S.Box[3]; ++y)
		{
			const uint8* Hf = HairFull.ptr<uint8>(y);
			uint8* B = BrowHoles.ptr<uint8>(y);
			for (int32 x = Bx0; x <= Bx1; ++x)
			{
				if (Hf[x]) { B[x] = 1; }
			}
		}
		const int32 Sy0 = std::max(S.Box[1] - (int32)(0.20 * FaceWidth), 0);
		for (int32 y = Sy0; y < S.Box[1]; ++y)
		{
			const uint8* Hf = HairFull.ptr<uint8>(y);
			const uint8* Sf = SkinFull.ptr<uint8>(y);
			const uint8* G = Gray.ptr<uint8>(y);
			uint8* B = BrowHoles.ptr<uint8>(y);
			for (int32 x = Bx0; x <= Bx1; ++x)
			{
				if (Hf[x] || (Sf[x] && G[x] < 0.80 * SkinLum)) { B[x] = 1; }
			}
		}
	}
	if (cv::countNonZero(BrowHoles))
	{
		cv::dilate(BrowHoles, BrowHoles, cv::Mat::ones(3, 3, CV_8U), cv::Point(-1, -1), 2);
		if (!LamaInpaint(C, SelfieFilled, BrowHoles, Err))
		{
			return false;
		}
		C.Say(FString::Printf(TEXT("brow repair: %d px regenerated (skin_lum %.0f)"),
			cv::countNonZero(BrowHoles), SkinLum));
	}

	// 眉毛對稱補全
	{
		const double Fl = Brows.L.HairFraction, Fr = Brows.R.HairFraction;
		int32 Donor = -1, Recip = -1; // 0=L 1=R
		if (!Brows.bCovered)
		{
			if (Fr >= BROW_MIRROR_MIN && Fl <= Fr - BROW_MIRROR_MARGIN) { Donor = 0; Recip = 1; }
			else if (Fl >= BROW_MIRROR_MIN && Fr <= Fl - BROW_MIRROR_MARGIN) { Donor = 1; Recip = 0; }
		}
		if (Donor >= 0)
		{
			const TArray<int32>& DIdx = (Donor == 0) ? BROW_L_LM : BROW_R_LM;
			TArray<int32> RIdx = (Recip == 0) ? BROW_L_LM : BROW_R_LM;
			Algo::Reverse(RIdx);
			const int32 DEyes[2] = { Donor == 0 ? 33 : 263, Donor == 0 ? 133 : 362 };
			const int32 REyes[2] = { Recip == 0 ? 33 : 263, Recip == 0 ? 133 : 362 };
			std::vector<cv::Point2f> Src, Dst;
			for (int32 I : DIdx) { Src.emplace_back((float)AllLm.at<double>(I, 0), (float)AllLm.at<double>(I, 1)); }
			for (int32 I : RIdx) { Dst.emplace_back((float)AllLm.at<double>(I, 0), (float)AllLm.at<double>(I, 1)); }
			for (int32 k = 0; k < 2; ++k)
			{
				Src.emplace_back((float)AllLm.at<double>(DEyes[k], 0), (float)AllLm.at<double>(DEyes[k], 1));
				Dst.emplace_back((float)AllLm.at<double>(REyes[k], 0), (float)AllLm.at<double>(REyes[k], 1));
			}
			cv::theRNG().state = 0xffffffff; // estimateAffine2D RANSAC 決定性
			cv::Mat A = cv::estimateAffine2D(Src, Dst);
			if (!A.empty())
			{
				const FBrowSide& DS = (Donor == 0) ? Brows.L : Brows.R;
				const int32 By1e = std::min(DS.Box[3] + (int32)(0.05 * FaceWidth), H - 1);
				cv::Mat G2;
				cv::cvtColor(SelfieFilled, G2, cv::COLOR_BGR2GRAY);
				cv::Mat Stroke(H, W, CV_8U, cv::Scalar(0));
				for (int32 y = DS.Box[1]; y <= By1e; ++y)
				{
					const uint8* G = G2.ptr<uint8>(y);
					uint8* St = Stroke.ptr<uint8>(y);
					for (int32 x = DS.Box[0]; x <= DS.Box[2]; ++x)
					{
						if (G[x] < 0.85 * SkinLum) { St[x] = 1; }
					}
				}
				cv::morphologyEx(Stroke, Stroke, cv::MORPH_CLOSE, cv::Mat::ones(5, 5, CV_8U));
				cv::dilate(Stroke, Stroke, cv::Mat::ones(3, 3, CV_8U), cv::Point(-1, -1),
					std::max(1, (int32)(0.006 * FaceWidth)));
				const int32 NStroke = cv::countNonZero(Stroke);
				if (NStroke < 100)
				{
					C.Say(FString::Printf(TEXT("brow mirror skipped: donor stroke too faint (%d px)"), NStroke));
				}
				else
				{
					cv::Mat StrokeF(H, W, CV_32F);
					for (int32 y = 0; y < H; ++y)
					{
						const uint8* St = Stroke.ptr<uint8>(y);
						float* F = StrokeF.ptr<float>(y);
						for (int32 x = 0; x < W; ++x) { F[x] = St[x] ? 1.f : 0.f; }
					}
					cv::Mat Dm = BlurF(StrokeF, 0.008 * FaceWidth);
					cv::Mat DonorImg, DonorM;
					cv::warpAffine(SelfieFilled, DonorImg, A, cv::Size(W, H), cv::INTER_LINEAR);
					cv::warpAffine(Dm, DonorM, A, cv::Size(W, H));
					cv::Mat Melt(H, W, CV_8U, cv::Scalar(0));
					int64 NMelt = 0;
					for (int32 y = 0; y < H; ++y)
					{
						const float* Mo = DonorM.ptr<float>(y);
						const cv::Vec3b* Di = DonorImg.ptr<cv::Vec3b>(y);
						cv::Vec3b* O = SelfieFilled.ptr<cv::Vec3b>(y);
						uint8* Me = Melt.ptr<uint8>(y);
						for (int32 x = 0; x < W; ++x)
						{
							const float Mv = std::clamp(Mo[x], 0.f, 1.f);
							if (Mv > 0.f)
							{
								for (int32 c = 0; c < 3; ++c)
								{
									O[x][c] = (uint8)std::clamp(Di[x][c] * Mv + O[x][c] * (1.f - Mv), 0.f, 255.f);
								}
							}
							if (Mv > 0.05f && Mv < 0.6f) { Me[x] = 1; ++NMelt; }
						}
					}
					if (!LamaInpaint(C, SelfieFilled, Melt, Err))
					{
						return false;
					}
					C.Say(FString::Printf(TEXT("brow mirrored %s->%s stroke only (occ %.2f/%.2f, %d px, melt %lld px)"),
						Donor == 0 ? TEXT("L") : TEXT("R"), Recip == 0 ? TEXT("L") : TEXT("R"), Fl, Fr, NStroke, NMelt));
				}
			}
		}
	}

	// 縫環重生（剪影陰影帶）＋鬍領地豁免
	{
		const int32 Rr = std::max(3, (int32)std::lround(FaceWidth * 0.02));
		cv::Mat Hb(H, W, CV_8U);
		for (int32 y = 0; y < H; ++y)
		{
			const uint8* Hm = HeadMask.ptr<uint8>(y);
			uint8* B = Hb.ptr<uint8>(y);
			for (int32 x = 0; x < W; ++x) { B[x] = Hm[x] ? 1 : 0; }
		}
		cv::Mat Dil, Ero;
		cv::dilate(Hb, Dil, cv::Mat::ones(3, 3, CV_8U), cv::Point(-1, -1), Rr);
		cv::erode(Hb, Ero, cv::Mat::ones(3, 3, CV_8U), cv::Point(-1, -1), Rr);
		cv::Mat Ring(H, W, CV_8U);
		for (int32 y = 0; y < H; ++y)
		{
			const uint8* D = Dil.ptr<uint8>(y);
			const uint8* E = Ero.ptr<uint8>(y);
			uint8* R = Ring.ptr<uint8>(y);
			for (int32 x = 0; x < W; ++x) { R[x] = (D[x] && !E[x]) ? 1 : 0; }
		}
		if (bBearded)
		{
			const cv::Mat Terr = BeardTerritoryMask(AllLm, W, H, FaceWidth);
			const int32 NBefore = cv::countNonZero(Ring);
			for (int32 y = 0; y < H; ++y)
			{
				const uint8* T = Terr.ptr<uint8>(y);
				uint8* R = Ring.ptr<uint8>(y);
				for (int32 x = 0; x < W; ++x)
				{
					if (T[x]) { R[x] = 0; }
				}
			}
			C.Say(FString::Printf(TEXT("seam ring exempts beard territory (%d px spared)"),
				NBefore - cv::countNonZero(Ring)));
		}
		if (!LamaInpaint(C, SelfieFilled, Ring, Err))
		{
			return false;
		}
		C.Say(FString::Printf(TEXT("seam ring regenerated: %d px"), cv::countNonZero(Ring)));
	}

	if (C.bDump)
	{
		SavePng(SelfieFilled, C.DumpDir / TEXT("selfie_filled.png"));
	}

	// 6. FaceUV 輪廓（擴張島模式）
	C.Say(TEXT("6. Extracting FaceUV contour..."));
	FExpansion Exp;
	if (!LoadExpansion(C.DataDir / TEXT("faceuv_expansion_data.json"), Exp))
	{
		Err = TEXT("faceuv_expansion_data.json missing/bad");
		return false;
	}
	cv::Mat UvContour, UvMask;
	if (!GetFaceUvContour(C.DataDir / TEXT("faceuv_mask_coverage.png"), UvContour, UvMask))
	{
		Err = TEXT("faceuv_mask_coverage.png missing/bad");
		return false;
	}

	// 8. TPS（膨脹源→擴張島＋凍結錨）
	C.Say(TEXT("8. TPS warp (inflated source -> full FaceUV)..."));
	cv::Mat SrcInflated = InflateContour(SelfieSampled, InflatePx);
	TArray<int32> LmIdx;
	for (int32 i = 0; i < 5; ++i) { LmIdx.Add(SYM_PAIRS[i][0]); }
	for (int32 i = 0; i < 5; ++i) { LmIdx.Add(SYM_PAIRS[i][1]); }
	LmIdx.Append(MIDLINE);
	cv::Mat LmPts(LmIdx.Num(), 2, CV_64F);
	for (int32 i = 0; i < LmIdx.Num(); ++i)
	{
		LmPts.at<double>(i, 0) = AllLm.at<double>(LmIdx[i], 0);
		LmPts.at<double>(i, 1) = AllLm.at<double>(LmIdx[i], 1);
	}
	int32 MaskX0 = TEX_SIZE, MaskX1 = -1, MaskY0 = TEX_SIZE, MaskY1 = -1;
	for (int32 y = 0; y < TEX_SIZE; ++y)
	{
		const uint8* U = UvMask.ptr<uint8>(y);
		for (int32 x = 0; x < TEX_SIZE; ++x)
		{
			if (U[x])
			{
				MaskX0 = std::min(MaskX0, x);
				MaskX1 = std::max(MaskX1, x);
				MaskY0 = std::min(MaskY0, y);
				MaskY1 = std::max(MaskY1, y);
			}
		}
	}
	const double IslandCenterX = (MaskX0 + MaskX1) / 2.0;

	cv::Mat SkinSrc = ResizeNearest(LabelMask(Parsing, { SKIN_LABEL }), W, H);
	if (!BeardDet.empty())
	{
		for (int32 y = 0; y < H; ++y)
		{
			const uint8* B = BeardDet.ptr<uint8>(y);
			uint8* S = SkinSrc.ptr<uint8>(y);
			for (int32 x = 0; x < W; ++x)
			{
				if (B[x]) { S[x] = 0; }
			}
		}
	}
	cv::Mat EyeLSrc(H, W, CV_8U, cv::Scalar(0)), EyeRSrc(H, W, CV_8U, cv::Scalar(0));
	{
		std::vector<cv::Point> L, R;
		for (int32 I : EYE_L_RING)
		{
			L.emplace_back((int32)std::lround(AllLm.at<double>(I, 0)), (int32)std::lround(AllLm.at<double>(I, 1)));
		}
		for (int32 I : EYE_R_RING)
		{
			R.emplace_back((int32)std::lround(AllLm.at<double>(I, 0)), (int32)std::lround(AllLm.at<double>(I, 1)));
		}
		cv::fillPoly(EyeLSrc, std::vector<std::vector<cv::Point>>{ L }, cv::Scalar(255));
		cv::fillPoly(EyeRSrc, std::vector<std::vector<cv::Point>>{ R }, cv::Scalar(255));
	}

	// tps_warp_expanded
	cv::Mat AdjSrc;
	cv::Point2d Offset;
	cv::Mat Canvas = MakeCanvas(SelfieFilled, SkinColor, SrcInflated, AdjSrc, Offset);
	{
		cv::Ptr<cv::ThinPlateSplineShapeTransformer> TOld = EstimateTps(Exp.OldTargets, AdjSrc);
		const cv::Mat Chk = ApplyTps(TOld, Exp.OldTargets);
		double MaxErr = 0;
		for (int32 i = 0; i < Chk.rows; ++i)
		{
			MaxErr = std::max(MaxErr, std::abs(Chk.at<double>(i, 0) - AdjSrc.at<double>(i, 0)));
			MaxErr = std::max(MaxErr, std::abs(Chk.at<double>(i, 1) - AdjSrc.at<double>(i, 1)));
		}
		if (MaxErr > 1.0)
		{
			C.Say(FString::Printf(TEXT("WARNING: TPS direction self-check failed (err %.2fpx)"), MaxErr));
		}
		cv::Mat AnchorSrc = ApplyTps(TOld, Exp.AnchorOldPx);
		for (int32 i = 0; i < AnchorSrc.rows; ++i)
		{
			AnchorSrc.at<double>(i, 0) = std::clamp(AnchorSrc.at<double>(i, 0), (double)CANVAS_SAFETY, (double)(TEX_SIZE - CANVAS_SAFETY));
			AnchorSrc.at<double>(i, 1) = std::clamp(AnchorSrc.at<double>(i, 1), (double)CANVAS_SAFETY, (double)(TEX_SIZE - CANVAS_SAFETY));
		}
		cv::Mat DstAll(Exp.NewTargets.rows + Exp.AnchorNewPx.rows, 2, CV_64F);
		Exp.NewTargets.copyTo(DstAll(cv::Rect(0, 0, 2, Exp.NewTargets.rows)));
		Exp.AnchorNewPx.copyTo(DstAll(cv::Rect(0, Exp.NewTargets.rows, 2, Exp.AnchorNewPx.rows)));
		cv::Mat SrcAll(AdjSrc.rows + AnchorSrc.rows, 2, CV_64F);
		AdjSrc.copyTo(SrcAll(cv::Rect(0, 0, 2, AdjSrc.rows)));
		AnchorSrc.copyTo(SrcAll(cv::Rect(0, AdjSrc.rows, 2, AnchorSrc.rows)));

		cv::Mat LmCanvas = LmPts.clone();
		for (int32 i = 0; i < LmCanvas.rows; ++i)
		{
			LmCanvas.at<double>(i, 0) += Offset.x;
			LmCanvas.at<double>(i, 1) += Offset.y;
		}
		AlignSymmetry(C, DstAll, SrcAll, LmCanvas, IslandCenterX);
		cv::Ptr<cv::ThinPlateSplineShapeTransformer> Tps = EstimateTps(DstAll, SrcAll);
		const cv::Mat MapXY = BuildTpsMap(Tps);
		cv::Mat Warped;
		cv::remap(Canvas, Warped, MapXY, cv::noArray(), cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));

		cv::Mat ContentMask = RemapExtra(MapXY, HeadMask);
		cv::Mat SkinTex = RemapExtra(MapXY, SkinSrc);
		cv::Mat EyeLW = RemapExtra(MapXY, EyeLSrc);
		cv::Mat EyeRW = RemapExtra(MapXY, EyeRSrc);

		if (C.bDump)
		{
			SavePng(Warped, C.DumpDir / TEXT("warped_raw.png"));
		}

		// 8c. 填充域平滑
		C.Say(TEXT("8c. Fill-zone smoothing..."));
		{
			cv::Mat WarpedF;
			Warped.convertTo(WarpedF, CV_32FC3);
			cv::Mat Sm = BlurF(WarpedF, FILLSMOOTH_SIGMA);
			cv::Mat NotContent(TEX_SIZE, TEX_SIZE, CV_8U);
			for (int32 y = 0; y < TEX_SIZE; ++y)
			{
				const uint8* Cm = ContentMask.ptr<uint8>(y);
				uint8* Nc = NotContent.ptr<uint8>(y);
				for (int32 x = 0; x < TEX_SIZE; ++x) { Nc[x] = (Cm[x] > 127) ? 0 : 1; }
			}
			cv::Mat DOut;
			cv::distanceTransform(NotContent, DOut, cv::DIST_L2, 5);
			double HfSum = 0;
			int64 HfN = 0;
			cv::Mat Blur4 = BlurF(WarpedF, 4.0);
			for (int32 y = 0; y < TEX_SIZE; ++y)
			{
				const float* D = DOut.ptr<float>(y);
				const cv::Vec3f* S = Sm.ptr<cv::Vec3f>(y);
				const cv::Vec3f* WF = WarpedF.ptr<cv::Vec3f>(y);
				const uint8* Nc = NotContent.ptr<uint8>(y);
				const uint8* Uv = UvMask.ptr<uint8>(y);
				const cv::Vec3f* B4 = Blur4.ptr<cv::Vec3f>(y);
				cv::Vec3b* O = Warped.ptr<cv::Vec3b>(y);
				for (int32 x = 0; x < TEX_SIZE; ++x)
				{
					const float T = std::clamp((float)(D[x] / FILLSMOOTH_RAMP_PX), 0.f, 1.f);
					const float Wt = T * T * (3.f - 2.f * T);
					for (int32 c = 0; c < 3; ++c)
					{
						const float V = WF[x][c] * (1.f - Wt) + S[x][c] * Wt;
						O[x][c] = (uint8)std::clamp(V, 0.f, 255.f);
						if (Nc[x] && Uv[x])
						{
							HfSum += std::abs(V - B4[x][c]);
						}
					}
					if (Nc[x] && Uv[x]) { ++HfN; }
				}
			}
			// 注意：python 的 hf 統計在混合後重算；此處近似（診斷用）
			C.Say(FString::Printf(TEXT("fill-zone high-freq energy ~ %.2f"), HfN ? HfSum / (HfN * 3) : 0.0));
		}

		// 8d. flat-field
		C.Say(TEXT("8d. Skin-probe flat-field..."));
		Warped = SkinFlatfield(C, Warped, UvMask, ContentMask, SkinTex, SkinColor);

		// 9. alpha
		C.Say(TEXT("9. Alpha from feathered FaceUV mask..."));
		cv::Mat UvF(TEX_SIZE, TEX_SIZE, CV_32F);
		for (int32 y = 0; y < TEX_SIZE; ++y)
		{
			const uint8* U = UvMask.ptr<uint8>(y);
			float* F = UvF.ptr<float>(y);
			for (int32 x = 0; x < TEX_SIZE; ++x) { F[x] = U[x]; }
		}
		cv::Mat MaskBlur = BlurF(UvF, 5.0);
		cv::Mat Alpha(TEX_SIZE, TEX_SIZE, CV_8U);
		for (int32 y = 0; y < TEX_SIZE; ++y)
		{
			const float* Mb = MaskBlur.ptr<float>(y);
			uint8* A = Alpha.ptr<uint8>(y);
			for (int32 x = 0; x < TEX_SIZE; ++x) { A[x] = (uint8)std::clamp(Mb[x], 0.f, 255.f); }
		}

		// 10. 邊緣 RGB 收斂＋下半 alpha 淡出
		C.Say(TEXT("10. Edge RGB convergence + bottom-half alpha fade..."));
		cv::Mat Dist;
		cv::distanceTransform(UvMask, Dist, cv::DIST_L2, 5);
		{
			cv::Mat SrcM(TEX_SIZE, TEX_SIZE, CV_8U);
			cv::Mat SrcF(TEX_SIZE, TEX_SIZE, CV_32F);
			for (int32 y = 0; y < TEX_SIZE; ++y)
			{
				const uint8* U = UvMask.ptr<uint8>(y);
				const float* D = Dist.ptr<float>(y);
				uint8* S = SrcM.ptr<uint8>(y);
				float* F = SrcF.ptr<float>(y);
				for (int32 x = 0; x < TEX_SIZE; ++x)
				{
					S[x] = (U[x] > 0 && D[x] > EDGE_BLEND_DIST) ? 255 : 0;
					F[x] = S[x] ? 1.f : 0.f;
				}
			}
			cv::Mat WarpedF;
			Warped.convertTo(WarpedF, CV_32FC3);
			cv::Mat InteriorAvg = NormalizedConv(WarpedF, SrcF, 12.0);
			cv::Mat LocalTarget, DistDummy;
			NearestExtend(InteriorAvg, SrcM, LocalTarget, DistDummy);
			LocalTarget = BlurF(LocalTarget, 6.0);
			for (int32 y = 0; y < TEX_SIZE; ++y)
			{
				const float* D = Dist.ptr<float>(y);
				const cv::Vec3f* Lt = LocalTarget.ptr<cv::Vec3f>(y);
				cv::Vec3b* O = Warped.ptr<cv::Vec3b>(y);
				for (int32 x = 0; x < TEX_SIZE; ++x)
				{
					const float Ew = std::clamp(1.f - D[x] / EDGE_BLEND_DIST, 0.f, 1.f);
					if (Ew <= 0.f) { continue; }
					for (int32 c = 0; c < 3; ++c)
					{
						O[x][c] = (uint8)std::clamp(O[x][c] * (1.f - Ew) + Lt[x][c] * Ew, 0.f, 255.f);
					}
				}
			}
		}
		{
			const int32 YCenter = (MaskY0 + MaskY1) / 2;
			const int32 YFadeStart = YCenter - FADE_TRANSITION / 2;
			for (int32 y = 0; y < TEX_SIZE; ++y)
			{
				const float Bw = std::clamp((float)(y - YFadeStart) / FADE_TRANSITION, 0.f, 1.f);
				const float* D = Dist.ptr<float>(y);
				const uint8* U = UvMask.ptr<uint8>(y);
				uint8* A = Alpha.ptr<uint8>(y);
				for (int32 x = 0; x < TEX_SIZE; ++x)
				{
					if (!U[x])
					{
						A[x] = 0;
						continue;
					}
					const float Ef = std::clamp(D[x] / FADE_DIST, 0.f, 1.f);
					const float Fa = 1.f - Bw * (1.f - Ef);
					A[x] = (uint8)std::clamp(A[x] * Fa, 0.f, 255.f);
				}
			}
			int64 Filled = 0;
			for (int32 y = 0; y < TEX_SIZE; ++y)
			{
				const uint8* A = Alpha.ptr<uint8>(y);
				for (int32 x = 0; x < TEX_SIZE; ++x)
				{
					if (A[x]) { ++Filled; }
				}
			}
			C.Say(FString::Printf(TEXT("coverage: %lld px (%.1f%%)"), Filled,
				100.0 * Filled / ((double)TEX_SIZE * TEX_SIZE)));
		}

		// 10b. Poisson 膜
		C.Say(TEXT("10b. Poisson membrane to SkinColor..."));
		Warped = PoissonMembraneToSkin(C, Warped, UvMask, ContentMask, SkinColor);

		// 11b. 眼閉變體
		C.Say(TEXT("11b. Eyes-closed variant..."));
		int32 NEyes = 0;
		TArray<cv::Mat> EyeMasks;
		EyeMasks.Add(EyeLW);
		EyeMasks.Add(EyeRW);
		cv::Mat Closed = EyesClosedVariant(C, Warped, EyeMasks, NEyes);
		C.Say(FString::Printf(TEXT("%d/2 eyes closed"), NEyes));

		// 12. 輸出
		C.Say(TEXT("12. Assembling outputs..."));
		cv::Mat EyeMask(TEX_SIZE, TEX_SIZE, CV_8U);
		for (int32 y = 0; y < TEX_SIZE; ++y)
		{
			const uint8* L = EyeLW.ptr<uint8>(y);
			const uint8* R = EyeRW.ptr<uint8>(y);
			uint8* E = EyeMask.ptr<uint8>(y);
			for (int32 x = 0; x < TEX_SIZE; ++x) { E[x] = std::max(L[x], R[x]); }
		}
		auto ToRgba = [&Alpha](const cv::Mat& Bgr)
		{
			cv::Mat Rgba(TEX_SIZE, TEX_SIZE, CV_8UC4);
			cv::Mat Ch[4];
			cv::split(Bgr, Ch);
			Ch[3] = Alpha;
			cv::merge(Ch, 4, Rgba);
			return Rgba;
		};
		Out.FaceOpen = ToRgba(Warped);
		Out.FaceClosed = ToRgba(Closed);
		Out.EyeMask = EyeMask;
		Out.SkinColor = SkinColor;
	}
	return true;
}

} // namespace NiFace
