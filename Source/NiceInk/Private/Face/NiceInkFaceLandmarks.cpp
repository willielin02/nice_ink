#include "NiceInkFaceLandmarks.h"

#include "NiceInkFaceOnnx.h"

DEFINE_LOG_CATEGORY_STATIC(LogNiFaceLm, Log, All);

namespace
{
	constexpr int32 DET_SIDE = 128;
	constexpr int32 LM_SIDE = 256;
	constexpr int32 NUM_ANCHORS = 896;
	constexpr int32 NUM_LM = 478;
	constexpr float ROI_SCALE = 1.5f;
	constexpr float MIN_SCORE = 0.5f;
	constexpr float NMS_THRESH = 0.3f;

	// SSD anchors（BlazeFace short-range 128、fixed_anchor_size）：
	// 16×16 格×2＋8×8 格×6＝896（與 python gen_anchors 同序）
	void GenAnchors(TArray<cv::Point2f>& Out)
	{
		Out.Reset(NUM_ANCHORS);
		const int32 Layers[2][2] = { {16, 2}, {8, 6} };
		for (const auto& L : Layers)
		{
			const int32 Fm = L[0], PerCell = L[1];
			for (int32 y = 0; y < Fm; ++y)
				for (int32 x = 0; x < Fm; ++x)
					for (int32 k = 0; k < PerCell; ++k)
						Out.Add(cv::Point2f((x + 0.5f) / Fm, (y + 0.5f) / Fm));
		}
	}

	struct FDet
	{
		cv::Vec4f Box;          // x, y, w, h（normalized，letterbox 方形域）
		cv::Point2f Kp[6];
		float Score = 0.f;
	};
}

bool FNiFaceLandmarks::Detect(const cv::Mat& Bgr, cv::Mat& OutPts, FString& OutError)
{
	const int32 H = Bgr.rows, W = Bgr.cols;

	// ---- 偵測器：letterbox 到方形→128²→[-1,1] ----
	const int32 Side = std::max(H, W);
	const int32 PadX = (Side - W) / 2, PadY = (Side - H) / 2;
	cv::Mat Sq;
	cv::copyMakeBorder(Bgr, Sq, PadY, Side - H - PadY, PadX, Side - W - PadX,
		cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
	cv::Mat Small;
	cv::resize(Sq, Small, cv::Size(DET_SIDE, DET_SIDE), 0, 0, cv::INTER_LINEAR);
	cv::Mat Rgb;
	cv::cvtColor(Small, Rgb, cv::COLOR_BGR2RGB);
	Rgb.convertTo(Rgb, CV_32FC3, 1.0 / 127.5, -1.0);

	TArray<TArray<float>> DetOut;
	{
		TConstArrayView<float> In(reinterpret_cast<const float*>(Rgb.ptr()), DET_SIDE * DET_SIDE * 3);
		const uint32 Shape[4] = { 1, DET_SIDE, DET_SIDE, 3 };
		if (!Detector->Run(In, MakeArrayView(Shape, 4), DetOut, OutError))
		{
			return false;
		}
	}
	// 輸出依模型序：regressors (1,896,16)、classificators (1,896,1)——用長度辨識
	const TArray<float>* Reg = nullptr;
	const TArray<float>* Cls = nullptr;
	for (const TArray<float>& O : DetOut)
	{
		if (O.Num() == NUM_ANCHORS * 16) { Reg = &O; }
		else if (O.Num() == NUM_ANCHORS) { Cls = &O; }
	}
	if (!Reg || !Cls)
	{
		OutError = TEXT("detector output shapes unexpected");
		return false;
	}

	static TArray<cv::Point2f> Anchors;
	if (Anchors.Num() == 0) { GenAnchors(Anchors); }

	// score→sigmoid→過閾＋解碼
	TArray<FDet> Cands;
	for (int32 i = 0; i < NUM_ANCHORS; ++i)
	{
		const float Logit = std::clamp((*Cls)[i], -100.f, 100.f);
		const float Score = 1.f / (1.f + std::exp(-Logit));
		if (Score < MIN_SCORE) { continue; }
		const float* R = &(*Reg)[i * 16];
		FDet D;
		const float Cx = R[0] / DET_SIDE + Anchors[i].x;
		const float Cy = R[1] / DET_SIDE + Anchors[i].y;
		const float Bw = R[2] / DET_SIDE;
		const float Bh = R[3] / DET_SIDE;
		D.Box = cv::Vec4f(Cx - Bw / 2, Cy - Bh / 2, Bw, Bh);
		for (int32 k = 0; k < 6; ++k)
		{
			D.Kp[k] = cv::Point2f(R[4 + 2 * k] / DET_SIDE + Anchors[i].x,
			                      R[5 + 2 * k] / DET_SIDE + Anchors[i].y);
		}
		D.Score = Score;
		Cands.Add(D);
	}
	if (Cands.Num() == 0)
	{
		float MaxLogit = -1e9f;
		int32 MaxIdx = -1;
		for (int32 i = 0; i < NUM_ANCHORS; ++i)
		{
			if ((*Cls)[i] > MaxLogit) { MaxLogit = (*Cls)[i]; MaxIdx = i; }
		}
		UE_LOG(LogNiFaceLm, Warning, TEXT("NiFaceLm: no candidates; max logit %.3f at anchor %d (reg[0..3]=%.2f %.2f %.2f %.2f)"),
			MaxLogit, MaxIdx, (*Reg)[MaxIdx * 16], (*Reg)[MaxIdx * 16 + 1], (*Reg)[MaxIdx * 16 + 2], (*Reg)[MaxIdx * 16 + 3]);
		OutError = TEXT("no face detected");
		return false;
	}

	// 加權 NMS（與 python 同構：分數排序、cluster 內按分數加權平均）
	TArray<int32> Order;
	for (int32 i = 0; i < Cands.Num(); ++i) { Order.Add(i); }
	Order.Sort([&](int32 A, int32 B) { return Cands[A].Score > Cands[B].Score; });
	TArray<bool> Used;
	Used.Init(false, Cands.Num());
	FDet Best;
	float BestScore = -1.f;
	for (int32 oi = 0; oi < Order.Num(); ++oi)
	{
		const int32 i = Order[oi];
		if (Used[i]) { continue; }
		const cv::Vec4f& A = Cands[i].Box;
		TArray<int32> Cluster;
		Cluster.Add(i);
		for (int32 oj = 0; oj < Order.Num(); ++oj)
		{
			const int32 j = Order[oj];
			if (j == i || Used[j]) { continue; }
			const cv::Vec4f& B = Cands[j].Box;
			const float Ix = std::max(0.f, std::min(A[0] + A[2], B[0] + B[2]) - std::max(A[0], B[0]));
			const float Iy = std::max(0.f, std::min(A[1] + A[3], B[1] + B[3]) - std::max(A[1], B[1]));
			const float Inter = Ix * Iy;
			const float Union = A[2] * A[3] + B[2] * B[3] - Inter;
			if (Union > 0.f && Inter / Union > NMS_THRESH) { Cluster.Add(j); }
		}
		float WSum = 0.f, MaxS = 0.f;
		cv::Vec4f BBox(0, 0, 0, 0);
		cv::Point2f BKp[6] = {};
		for (int32 j : Cluster)
		{
			Used[j] = true;
			const float S = Cands[j].Score;
			WSum += S;
			MaxS = std::max(MaxS, S);
			for (int32 c = 0; c < 4; ++c) { BBox[c] += Cands[j].Box[c] * S; }
			for (int32 k = 0; k < 6; ++k) { BKp[k] += Cands[j].Kp[k] * S; }
		}
		FDet Blend;
		for (int32 c = 0; c < 4; ++c) { Blend.Box[c] = BBox[c] / WSum; }
		for (int32 k = 0; k < 6; ++k) { Blend.Kp[k] = BKp[k] * (1.f / WSum); }
		Blend.Score = MaxS;
		if (Blend.Score > BestScore) { BestScore = Blend.Score; Best = Blend; }
	}

	// un-letterbox（方形 normalized→影像 normalized）
	auto UnpadX = [&](float x) { return (x * Side - PadX) / W; };
	auto UnpadY = [&](float y) { return (y * Side - PadY) / H; };
	const float BoxX = UnpadX(Best.Box[0]);
	const float BoxY = UnpadY(Best.Box[1]);
	const float BoxW = Best.Box[2] * Side / W;
	const float BoxH = Best.Box[3] * Side / H;

	// ---- ROI：中心/尺寸＝box、旋轉＝兩眼 keypoint、scale 1.5、長邊方形 ----
	const double CxPx = (BoxX + BoxW / 2) * W;
	const double CyPx = (BoxY + BoxH / 2) * H;
	const double BwPx = BoxW * W;
	const double BhPx = BoxH * H;
	const double E0x = UnpadX(Best.Kp[0].x) * W, E0y = UnpadY(Best.Kp[0].y) * H;
	const double E1x = UnpadX(Best.Kp[1].x) * W, E1y = UnpadY(Best.Kp[1].y) * H;
	double Rot = -std::atan2(-(E1y - E0y), E1x - E0x);
	Rot -= 2 * CV_PI * std::floor((Rot + CV_PI) / (2 * CV_PI));
	const double RoiSide = std::max(BwPx, BhPx) * ROI_SCALE;

	// ---- 裁切（旋轉方形→256²）＋特徵點 ----
	const double Cos = std::cos(Rot), Sin = std::sin(Rot);
	const double S = RoiSide / LM_SIDE;
	const double C = LM_SIDE / 2.0;
	cv::Mat M = (cv::Mat_<double>(2, 3) <<
		Cos * S, -Sin * S, CxPx - Cos * S * C + Sin * S * C,
		Sin * S, Cos * S, CyPx - Sin * S * C - Cos * S * C);
	cv::Mat Crop;
	cv::warpAffine(Bgr, Crop, M, cv::Size(LM_SIDE, LM_SIDE),
		cv::INTER_LINEAR | cv::WARP_INVERSE_MAP, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
	cv::Mat CropRgb;
	cv::cvtColor(Crop, CropRgb, cv::COLOR_BGR2RGB);
	CropRgb.convertTo(CropRgb, CV_32FC3, 1.0 / 255.0);

	TArray<TArray<float>> LmOut;
	{
		TConstArrayView<float> In(reinterpret_cast<const float*>(CropRgb.ptr()), LM_SIDE * LM_SIDE * 3);
		const uint32 Shape[4] = { 1, LM_SIDE, LM_SIDE, 3 };
		if (!Landmarker->Run(In, MakeArrayView(Shape, 4), LmOut, OutError))
		{
			return false;
		}
	}
	const TArray<float>* Lm = nullptr;
	for (const TArray<float>& O : LmOut)
	{
		if (O.Num() == NUM_LM * 3) { Lm = &O; }
	}
	if (!Lm)
	{
		OutError = TEXT("landmark output shape unexpected");
		return false;
	}

	// crop 像素域→原圖像素域（同一個 M 正向）
	OutPts.create(NUM_LM, 2, CV_64F);
	const double* Md = M.ptr<double>();
	for (int32 i = 0; i < NUM_LM; ++i)
	{
		const double U = (*Lm)[i * 3 + 0];
		const double V = (*Lm)[i * 3 + 1];
		OutPts.at<double>(i, 0) = Md[0] * U + Md[1] * V + Md[2];
		OutPts.at<double>(i, 1) = Md[3] * U + Md[4] * V + Md[5];
	}
	return true;
}
