// FaceBakery Core：共用 helpers＋步驟 1~5b（分割/膚色/自然填充/鬍鬚/眉毛/LaMa）。
// 行為規格＝selfie_to_face_texture.py v7＋analyze_hair.beard_analysis＋lama_fill.py。
#include "NiceInkFaceBakeryImpl.h"

#include "Async/ParallelFor.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"
#include "NiceInkFaceOnnx.h"

DEFINE_LOG_CATEGORY_STATIC(LogNiFaceBake, Log, All);

namespace NiFace
{

// ---------------------------------------------------------------- image IO

bool LoadImage(const FString& Path, cv::Mat& Out, bool bGrayscale)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path))
	{
		return false;
	}
	IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const EImageFormat Format = Module.DetectImageFormat(Bytes.GetData(), Bytes.Num());
	if (Format == EImageFormat::Invalid)
	{
		return false;
	}
	TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(Format);
	if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Bytes.GetData(), Bytes.Num()))
	{
		return false;
	}
	TArray64<uint8> Raw;
	if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw))
	{
		return false;
	}
	const int32 W = Wrapper->GetWidth(), H = Wrapper->GetHeight();
	cv::Mat Bgra(H, W, CV_8UC4);
	FMemory::Memcpy(Bgra.ptr(), Raw.GetData(), (SIZE_T)W * H * 4);
	if (bGrayscale)
	{
		cv::cvtColor(Bgra, Out, cv::COLOR_BGRA2GRAY);
	}
	else
	{
		cv::cvtColor(Bgra, Out, cv::COLOR_BGRA2BGR);
	}
	return true;
}

bool SavePng(const cv::Mat& Img, const FString& Path)
{
	IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
	if (!Wrapper.IsValid())
	{
		return false;
	}
	bool bOk = false;
	if (Img.type() == CV_8U)
	{
		bOk = Wrapper->SetRaw(Img.ptr(), (int64)Img.rows * Img.cols, Img.cols, Img.rows, ERGBFormat::Gray, 8);
	}
	else if (Img.type() == CV_8UC3)
	{
		cv::Mat Bgra;
		cv::cvtColor(Img, Bgra, cv::COLOR_BGR2BGRA);
		bOk = Wrapper->SetRaw(Bgra.ptr(), (int64)Bgra.rows * Bgra.cols * 4, Bgra.cols, Bgra.rows, ERGBFormat::BGRA, 8);
	}
	else if (Img.type() == CV_8UC4)
	{
		bOk = Wrapper->SetRaw(Img.ptr(), (int64)Img.rows * Img.cols * 4, Img.cols, Img.rows, ERGBFormat::BGRA, 8);
	}
	if (!bOk)
	{
		return false;
	}
	const TArray64<uint8> Png = Wrapper->GetCompressed();
	return FFileHelper::SaveArrayToFile(TConstArrayView64<uint8>(Png.GetData(), Png.Num()), *Path);
}

void FCtx::Say(const FString& Line)
{
	const FString Stamped = FString::Printf(TEXT("[t+%.1fs] %s"),
		FPlatformTime::Seconds() - T0, *Line);
	Log.Add(Stamped);
	UE_LOG(LogNiFaceBake, Log, TEXT("NiFaceBake: %s"), *Stamped);
	if (Progress)
	{
		Progress(Line);
	}
}

// ---------------------------------------------------------------- helpers

cv::Mat BlurF(const cv::Mat& Src, double Sigma)
{
	// 自寫平行可分離高斯（UE 的 opencv_world455 無平行化——sigma 64 的 2048²
	// 模糊單線程要 10 秒級）。kernel/邊界與 cv::GaussianBlur(Size(0,0),sigma)
	// 同構：ksize=round(4σ)*2+1|1（CV_32F 域）、BORDER_REFLECT_101。
	check(Src.depth() == CV_32F && (Src.channels() == 1 || Src.channels() == 3));
	const int32 KSize = ((int32)std::lround(Sigma * 4.0) * 2 + 1) | 1;
	cv::Mat K = cv::getGaussianKernel(KSize, Sigma, CV_64F);
	TArray<float> Kf;
	for (int32 i = 0; i < KSize; ++i) { Kf.Add((float)K.at<double>(i)); }
	const int32 R = KSize / 2;
	const int32 H = Src.rows, W = Src.cols, C = Src.channels();
	const int32 WC = W * C;

	auto Reflect = [](int32 I, int32 N)
	{
		// BORDER_REFLECT_101：-1→1、N→N-2
		while (I < 0 || I >= N)
		{
			if (I < 0) { I = -I; }
			if (I >= N) { I = 2 * N - 2 - I; }
		}
		return I;
	};

	cv::Mat Tmp(H, W, Src.type());
	ParallelFor(H, [&](int32 y)
	{
		const float* S = Src.ptr<float>(y);
		float* T = Tmp.ptr<float>(y);
		for (int32 x = 0; x < W; ++x)
		{
			float Acc[3] = { 0, 0, 0 };
			for (int32 k = -R; k <= R; ++k)
			{
				const int32 Xs = Reflect(x + k, W);
				const float Wt = Kf[k + R];
				for (int32 c = 0; c < C; ++c)
				{
					Acc[c] += S[Xs * C + c] * Wt;
				}
			}
			for (int32 c = 0; c < C; ++c) { T[x * C + c] = Acc[c]; }
		}
	});
	cv::Mat Out(H, W, Src.type());
	ParallelFor(H, [&](int32 y)
	{
		float* O = Out.ptr<float>(y);
		FMemory::Memzero(O, sizeof(float) * WC);
		for (int32 k = -R; k <= R; ++k)
		{
			const float* T = Tmp.ptr<float>(Reflect(y + k, H));
			const float Wt = Kf[k + R];
			for (int32 i = 0; i < WC; ++i)
			{
				O[i] += T[i] * Wt;
			}
		}
	});
	return Out;
}

cv::Mat NormalizedConv(const cv::Mat& ImgF, const cv::Mat& MaskF, double Sigma)
{
	// blur(img*mask)/blur(mask)：mask 域內的鄰域平均
	cv::Mat Weighted;
	if (ImgF.channels() == 3)
	{
		cv::Mat M3;
		cv::Mat Ch[3] = { MaskF, MaskF, MaskF };
		cv::merge(Ch, 3, M3);
		cv::multiply(ImgF, M3, Weighted);
	}
	else
	{
		cv::multiply(ImgF, MaskF, Weighted);
	}
	cv::Mat Num = BlurF(Weighted, Sigma);
	cv::Mat Den = BlurF(MaskF, Sigma);
	cv::max(Den, 1e-6, Den);
	cv::Mat Out(ImgF.size(), ImgF.type());
	if (ImgF.channels() == 3)
	{
		for (int32 y = 0; y < Out.rows; ++y)
		{
			const float* N = Num.ptr<float>(y);
			const float* D = Den.ptr<float>(y);
			float* O = Out.ptr<float>(y);
			for (int32 x = 0; x < Out.cols; ++x)
			{
				O[3 * x] = N[3 * x] / D[x];
				O[3 * x + 1] = N[3 * x + 1] / D[x];
				O[3 * x + 2] = N[3 * x + 2] / D[x];
			}
		}
	}
	else
	{
		cv::divide(Num, Den, Out);
	}
	return Out;
}

void NearestExtend(const cv::Mat& Values3F, const cv::Mat& SrcMaskU8,
                   cv::Mat& OutExtended, cv::Mat& OutDist)
{
	// python：inv=(src==0)；distanceTransformWithLabels(DIST_LABEL_PIXEL)＋LUT
	const int32 H = Values3F.rows, W = Values3F.cols;
	cv::Mat Inv(H, W, CV_8U);
	for (int32 y = 0; y < H; ++y)
	{
		const uint8* S = SrcMaskU8.ptr<uint8>(y);
		uint8* I = Inv.ptr<uint8>(y);
		for (int32 x = 0; x < W; ++x) { I[x] = (S[x] == 0) ? 1 : 0; }
	}
	cv::Mat Labels;
	cv::distanceTransform(Inv, OutDist, Labels, cv::DIST_L2, 5, cv::DIST_LABEL_PIXEL);
	// label→源像素 flat index
	int32 MaxLabel = 0;
	for (int32 y = 0; y < H; ++y)
	{
		const int32* L = Labels.ptr<int32>(y);
		for (int32 x = 0; x < W; ++x) { MaxLabel = std::max(MaxLabel, L[x]); }
	}
	TArray<int64> Lut;
	Lut.Init(0, MaxLabel + 1);
	for (int32 y = 0; y < H; ++y)
	{
		const uint8* I = Inv.ptr<uint8>(y);
		const int32* L = Labels.ptr<int32>(y);
		for (int32 x = 0; x < W; ++x)
		{
			if (I[x] == 0) { Lut[L[x]] = (int64)y * W + x; }
		}
	}
	OutExtended.create(H, W, CV_32FC3);
	for (int32 y = 0; y < H; ++y)
	{
		const int32* L = Labels.ptr<int32>(y);
		cv::Vec3f* O = OutExtended.ptr<cv::Vec3f>(y);
		for (int32 x = 0; x < W; ++x)
		{
			const int64 Flat = Lut[L[x]];
			O[x] = Values3F.at<cv::Vec3f>((int32)(Flat / W), (int32)(Flat % W));
		}
	}
}

cv::Mat LabelMask(const cv::Mat& Parsing, const TArray<int32>& InLabels)
{
	cv::Mat Out(Parsing.size(), CV_8U, cv::Scalar(0));
	for (int32 y = 0; y < Parsing.rows; ++y)
	{
		const uint8* P = Parsing.ptr<uint8>(y);
		uint8* O = Out.ptr<uint8>(y);
		for (int32 x = 0; x < Parsing.cols; ++x)
		{
			for (int32 L : InLabels)
			{
				if (P[x] == L) { O[x] = 255; break; }
			}
		}
	}
	return Out;
}

cv::Mat ResizeNearest(const cv::Mat& M512, int32 W, int32 H)
{
	cv::Mat Out;
	cv::resize(M512, Out, cv::Size(W, H), 0, 0, cv::INTER_NEAREST);
	return Out;
}

static double MedianOf(TArray<double>& V)
{
	if (V.Num() == 0) { return 0.0; }
	// np.median：偶數取中間兩值平均
	TArray<double> S = V;
	S.Sort();
	const int32 N = S.Num();
	return (N % 2 == 1) ? S[N / 2] : 0.5 * (S[N / 2 - 1] + S[N / 2]);
}

cv::Vec3d MedianBgr(const cv::Mat& Bgr, const cv::Mat& MaskBool)
{
	TArray<double> C0, C1, C2;
	for (int32 y = 0; y < Bgr.rows; ++y)
	{
		const uint8* M = MaskBool.ptr<uint8>(y);
		const cv::Vec3b* P = Bgr.ptr<cv::Vec3b>(y);
		for (int32 x = 0; x < Bgr.cols; ++x)
		{
			if (M[x])
			{
				C0.Add(P[x][0]);
				C1.Add(P[x][1]);
				C2.Add(P[x][2]);
			}
		}
	}
	return cv::Vec3d(MedianOf(C0), MedianOf(C1), MedianOf(C2));
}

double MedianGray(const cv::Mat& Gray, const cv::Mat& MaskBool)
{
	TArray<double> V;
	for (int32 y = 0; y < Gray.rows; ++y)
	{
		const uint8* M = MaskBool.ptr<uint8>(y);
		const uint8* G = Gray.ptr<uint8>(y);
		for (int32 x = 0; x < Gray.cols; ++x)
		{
			if (M[x]) { V.Add(G[x]); }
		}
	}
	return MedianOf(V);
}

double Percentile(TArray<float>& Vals, double P)
{
	if (Vals.Num() == 0) { return 0.0; }
	Vals.Sort();
	const double Idx = P / 100.0 * (Vals.Num() - 1);
	const int32 Lo = (int32)std::floor(Idx);
	const int32 Hi = std::min(Lo + 1, Vals.Num() - 1);
	const double T = Idx - Lo;
	return Vals[Lo] * (1.0 - T) + Vals[Hi] * T;
}

cv::Vec3f BgrToLabF(const cv::Vec3b& Bgr)
{
	cv::Mat One(1, 1, CV_8UC3, cv::Scalar(Bgr[0], Bgr[1], Bgr[2]));
	cv::Mat Lab;
	cv::cvtColor(One, Lab, cv::COLOR_BGR2Lab);
	const cv::Vec3b L = Lab.at<cv::Vec3b>(0, 0);
	return cv::Vec3f(L[0], L[1], L[2]);
}

FGrainRng::FGrainRng(uint64 Seed)
	: State(Seed * 6364136223846793005ULL + 1442695040888963407ULL)
{
}

uint32 FGrainRng::NextU32()
{
	// PCG32（O'Neill）
	const uint64 Old = State;
	State = Old * 6364136223846793005ULL + 1442695040888963407ULL;
	const uint32 XorShifted = (uint32)(((Old >> 18u) ^ Old) >> 27u);
	const uint32 Rot = (uint32)(Old >> 59u);
	return (XorShifted >> Rot) | (XorShifted << ((32u - Rot) & 31u));
}

float FGrainRng::Gauss()
{
	if (bHasSpare)
	{
		bHasSpare = false;
		return Spare;
	}
	// Box-Muller
	float U1, U2;
	do
	{
		U1 = (NextU32() >> 8) * (1.0f / 16777216.0f);
	} while (U1 <= 1e-12f);
	U2 = (NextU32() >> 8) * (1.0f / 16777216.0f);
	const float R = std::sqrt(-2.0f * std::log(U1));
	Spare = R * std::sin(2.0f * (float)CV_PI * U2);
	bHasSpare = true;
	return R * std::cos(2.0f * (float)CV_PI * U2);
}

cv::Mat FGrainRng::Field(int32 H, int32 W, double Std)
{
	cv::Mat Out(H, W, CV_32F);
	for (int32 y = 0; y < H; ++y)
	{
		float* O = Out.ptr<float>(y);
		for (int32 x = 0; x < W; ++x) { O[x] = Gauss() * (float)Std; }
	}
	return Out;
}

// ---------------------------------------------------------------- BiSeNet

bool ParseSelfie(FCtx& C, const cv::Mat& SelfieBgr, cv::Mat& OutParsing, FString& Err)
{
	// python 用 PIL Resize（抗鋸齒雙線性）；cv 對應：縮小=INTER_AREA、放大=INTER_LINEAR
	cv::Mat Img;
	const int32 Interp = (SelfieBgr.cols > 512 || SelfieBgr.rows > 512) ? cv::INTER_AREA : cv::INTER_LINEAR;
	cv::resize(SelfieBgr, Img, cv::Size(512, 512), 0, 0, Interp);
	cv::Mat Rgb;
	cv::cvtColor(Img, Rgb, cv::COLOR_BGR2RGB);
	Rgb.convertTo(Rgb, CV_32FC3, 1.0 / 255.0);

	// (x-mean)/std → NCHW
	const float Mean[3] = { 0.485f, 0.456f, 0.406f };
	const float Std[3] = { 0.229f, 0.224f, 0.225f };
	TArray<float> Input;
	Input.SetNumUninitialized(3 * 512 * 512);
	for (int32 ch = 0; ch < 3; ++ch)
	{
		float* Dst = Input.GetData() + ch * 512 * 512;
		for (int32 y = 0; y < 512; ++y)
		{
			const float* Row = Rgb.ptr<float>(y);
			for (int32 x = 0; x < 512; ++x)
			{
				Dst[y * 512 + x] = (Row[3 * x + ch] - Mean[ch]) / Std[ch];
			}
		}
	}

	TArray<TArray<float>> Outs;
	const uint32 Shape[4] = { 1, 3, 512, 512 };
	if (!C.Bisenet->Run(Input, MakeArrayView(Shape, 4), Outs, Err))
	{
		return false;
	}
	const TArray<float>* Logits = nullptr;
	for (const TArray<float>& O : Outs)
	{
		if (O.Num() == 19 * 512 * 512) { Logits = &O; }
	}
	if (!Logits)
	{
		Err = TEXT("bisenet output shape unexpected");
		return false;
	}

	OutParsing.create(512, 512, CV_8U);
	for (int32 y = 0; y < 512; ++y)
	{
		uint8* O = OutParsing.ptr<uint8>(y);
		for (int32 x = 0; x < 512; ++x)
		{
			int32 Best = 0;
			float BestV = (*Logits)[(int64)0 * 512 * 512 + y * 512 + x];
			for (int32 cl = 1; cl < 19; ++cl)
			{
				const float V = (*Logits)[(int64)cl * 512 * 512 + y * 512 + x];
				if (V > BestV) { BestV = V; Best = cl; }
			}
			O[x] = (uint8)Best;
		}
	}
	return true;
}

cv::Mat BuildHeadMask(const cv::Mat& Parsing, int32 W, int32 H, double DeflatePx,
                      const cv::Mat& FaceContour)
{
	cv::Mat M512 = LabelMask(Parsing, HEAD_LABELS);
	cv::morphologyEx(M512, M512, cv::MORPH_CLOSE, cv::Mat::ones(3, 3, CV_8U));
	cv::Mat Mask = ResizeNearest(M512, W, H);

	// 多人照：留與 landmark 臉重疊最大的連通元件，不是最大的
	cv::Mat Labels, Stats, Centroids;
	const int32 NLabels = cv::connectedComponentsWithStats(Mask, Labels, Stats, Centroids);
	if (NLabels > 2)
	{
		cv::Mat Poly(H, W, CV_8U, cv::Scalar(0));
		TArray<cv::Point> Pts;
		for (int32 i = 0; i < FaceContour.rows; ++i)
		{
			Pts.Add(cv::Point((int32)FaceContour.at<double>(i, 0), (int32)FaceContour.at<double>(i, 1)));
		}
		cv::fillPoly(Poly, std::vector<std::vector<cv::Point>>{ std::vector<cv::Point>(Pts.GetData(), Pts.GetData() + Pts.Num()) }, cv::Scalar(1));
		TArray<int64> Overlap;
		Overlap.Init(0, NLabels);
		for (int32 y = 0; y < H; ++y)
		{
			const int32* L = Labels.ptr<int32>(y);
			const uint8* P = Poly.ptr<uint8>(y);
			for (int32 x = 0; x < W; ++x)
			{
				if (P[x] && L[x] > 0) { ++Overlap[L[x]]; }
			}
		}
		int32 BestIdx = 0;
		int64 BestOv = 0;
		for (int32 i = 1; i < NLabels; ++i)
		{
			if (Overlap[i] > BestOv) { BestOv = Overlap[i]; BestIdx = i; }
		}
		if (BestOv == 0)
		{
			int32 BestArea = 0;
			for (int32 i = 1; i < NLabels; ++i)
			{
				const int32 Area = Stats.at<int32>(i, cv::CC_STAT_AREA);
				if (Area > BestArea) { BestArea = Area; BestIdx = i; }
			}
		}
		for (int32 y = 0; y < H; ++y)
		{
			const int32* L = Labels.ptr<int32>(y);
			uint8* MRow = Mask.ptr<uint8>(y);
			for (int32 x = 0; x < W; ++x) { MRow[x] = (L[x] == BestIdx) ? 255 : 0; }
		}
	}

	const double Upscale = std::max(H, W) / 512.0;
	const int32 Iters = std::max({ (int32)std::lround(DeflatePx),
		(int32)std::ceil(MASK_DEFLATE_MIN_512 * Upscale), 1 });
	cv::erode(Mask, Mask, cv::Mat::ones(3, 3, CV_8U), cv::Point(-1, -1), Iters);
	return Mask;
}

cv::Vec3b ExtractSkinColor(const cv::Mat& Parsing, const cv::Mat& SelfieBgr)
{
	cv::Mat Skin512 = LabelMask(Parsing, { SKIN_LABEL });
	cv::erode(Skin512, Skin512, cv::Mat::ones(3, 3, CV_8U), cv::Point(-1, -1), 2);
	cv::Mat Skin = ResizeNearest(Skin512, SelfieBgr.cols, SelfieBgr.rows);
	if (cv::countNonZero(Skin) == 0)
	{
		return cv::Vec3b(120, 140, 180);
	}
	const cv::Vec3d M = MedianBgr(SelfieBgr, Skin);
	return cv::Vec3b((uint8)M[0], (uint8)M[1], (uint8)M[2]);
}

bool ExtractHairColor(const cv::Mat& Parsing, const cv::Mat& SelfieBgr, cv::Vec3b& OutBgr)
{
	cv::Mat Hair512 = LabelMask(Parsing, { HAIR_LABEL });
	cv::erode(Hair512, Hair512, cv::Mat::ones(3, 3, CV_8U), cv::Point(-1, -1), 2);
	cv::Mat Hair = ResizeNearest(Hair512, SelfieBgr.cols, SelfieBgr.rows);
	const int32 Count = cv::countNonZero(Hair);
	if (Count < MIN_HAIR_FRACTION * SelfieBgr.rows * SelfieBgr.cols)
	{
		return false;
	}
	const cv::Vec3d M = MedianBgr(SelfieBgr, Hair);
	OutBgr = cv::Vec3b((uint8)M[0], (uint8)M[1], (uint8)M[2]);
	return true;
}

// ---------------------------------------------------------------- fills

cv::Mat NaturalSkinFill(const cv::Mat& SelfieBgr, const cv::Mat& HeadMask,
                        const cv::Vec3b& SkinColor, double FaceWidth,
                        const cv::Mat& SampleExclude)
{
	const int32 H = SelfieBgr.rows, W = SelfieBgr.cols;
	const int32 Inset = std::max(1, (int32)std::lround(FaceWidth * FILL_SOURCE_INSET_RATIO));
	cv::Mat SrcMask;
	cv::erode(HeadMask, SrcMask, cv::Mat::ones(3, 3, CV_8U), cv::Point(-1, -1), Inset);
	if (!SampleExclude.empty())
	{
		for (int32 y = 0; y < H; ++y)
		{
			const uint8* E = SampleExclude.ptr<uint8>(y);
			uint8* S = SrcMask.ptr<uint8>(y);
			for (int32 x = 0; x < W; ++x)
			{
				if (E[x]) { S[x] = 0; }
			}
		}
	}
	if (cv::countNonZero(SrcMask) == 0)
	{
		SrcMask = HeadMask.clone();
	}

	// 遮罩域鄰域平均（normalized convolution）
	const double SigmaNc = std::max(2.0, FaceWidth * 0.02);
	cv::Mat MF(H, W, CV_32F);
	for (int32 y = 0; y < H; ++y)
	{
		const uint8* S = SrcMask.ptr<uint8>(y);
		float* M = MF.ptr<float>(y);
		for (int32 x = 0; x < W; ++x) { M[x] = S[x] ? 1.f : 0.f; }
	}
	cv::Mat SelfieF;
	SelfieBgr.convertTo(SelfieF, CV_32FC3);
	cv::Mat InteriorAvg = NormalizedConv(SelfieF, MF, SigmaNc);

	// 外推＋距離
	cv::Mat Extended, Dist;
	NearestExtend(InteriorAvg, SrcMask, Extended, Dist);

	Extended = BlurF(Extended, std::max(2.0, FaceWidth * FILL_BLUR_RATIO));
	const double BlendDist = std::max(1.0, FaceWidth * FILL_BLEND_DIST_RATIO);
	cv::Mat Fill(H, W, CV_32FC3);
	const cv::Vec3f SkinF((float)SkinColor[0], (float)SkinColor[1], (float)SkinColor[2]);
	for (int32 y = 0; y < H; ++y)
	{
		const float* D = Dist.ptr<float>(y);
		const cv::Vec3f* E = Extended.ptr<cv::Vec3f>(y);
		cv::Vec3f* F = Fill.ptr<cv::Vec3f>(y);
		for (int32 x = 0; x < W; ++x)
		{
			const float T = (float)(std::clamp(D[x] / BlendDist, 0.0, 1.0) * FILL_MEDIAN_PULL);
			F[x] = E[x] * (1.f - T) + SkinF * T;
		}
	}

	// 軟遮罩跨帶混合＋顆粒
	cv::Mat HeadF(H, W, CV_32F);
	for (int32 y = 0; y < H; ++y)
	{
		const uint8* S = HeadMask.ptr<uint8>(y);
		float* M = HeadF.ptr<float>(y);
		for (int32 x = 0; x < W; ++x) { M[x] = S[x] ? 1.f : 0.f; }
	}
	cv::Mat MSoft = BlurF(HeadF, std::max(2.0, FaceWidth * 0.008));
	FGrainRng Rng(7);
	cv::Mat Grain = Rng.Field(H, W, FILL_GRAIN_STD);
	cv::Mat Out(H, W, CV_8UC3);
	for (int32 y = 0; y < H; ++y)
	{
		const float* MS = MSoft.ptr<float>(y);
		const float* G = Grain.ptr<float>(y);
		const cv::Vec3f* SF = SelfieF.ptr<cv::Vec3f>(y);
		const cv::Vec3f* F = Fill.ptr<cv::Vec3f>(y);
		cv::Vec3b* O = Out.ptr<cv::Vec3b>(y);
		for (int32 x = 0; x < W; ++x)
		{
			for (int32 c = 0; c < 3; ++c)
			{
				const float FillC = std::clamp(F[x][c], 0.f, 255.f);
				float V = SF[x][c] * MS[x] + FillC * (1.f - MS[x]);
				V += G[x] * (1.f - MS[x]);
				O[x][c] = (uint8)std::clamp(V, 0.f, 255.f);
			}
		}
	}
	return Out;
}

cv::Mat LipsGuardMask(const cv::Mat& Parsing, int32 W, int32 H, double FaceWidth)
{
	cv::Mat Lips512 = LabelMask(Parsing, LIP_LABELS);
	cv::Mat Lips = ResizeNearest(Lips512, W, H);
	const int32 Guard = std::max(1, (int32)std::lround(FaceWidth * LIP_GUARD_RATIO));
	cv::dilate(Lips, Lips, cv::Mat::ones(3, 3, CV_8U), cv::Point(-1, -1), Guard);
	return Lips; // >0 即 guard
}

FBrowStatus BrowCoverStatus(const cv::Mat& Parsing, const cv::Mat& AllLm,
                            int32 W, int32 H, double FaceWidth)
{
	cv::Mat Hair512 = LabelMask(Parsing, { HAIR_LABEL });
	cv::Mat Hair = ResizeNearest(Hair512, W, H);
	const int32 Pad = std::max(2, (int32)std::lround(FaceWidth * 0.02));
	FBrowStatus Out;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const TArray<int32>& Idx = (Side == 0) ? BROW_L_LM : BROW_R_LM;
		double MinX = 1e18, MaxX = -1e18, MinY = 1e18, MaxY = -1e18;
		for (int32 I : Idx)
		{
			MinX = std::min(MinX, AllLm.at<double>(I, 0));
			MaxX = std::max(MaxX, AllLm.at<double>(I, 0));
			MinY = std::min(MinY, AllLm.at<double>(I, 1));
			MaxY = std::max(MaxY, AllLm.at<double>(I, 1));
		}
		const int32 X0 = std::max((int32)MinX - Pad, 0);
		const int32 X1 = std::min((int32)MaxX + Pad, W - 1);
		const int32 Y0 = std::max((int32)MinY - Pad, 0);
		const int32 Y1 = std::min((int32)MaxY + Pad, H - 1);
		int64 NHair = 0, NAll = 0;
		for (int32 y = Y0; y <= Y1; ++y)
		{
			const uint8* HRow = Hair.ptr<uint8>(y);
			for (int32 x = X0; x <= X1; ++x)
			{
				++NAll;
				if (HRow[x]) { ++NHair; }
			}
		}
		FBrowSide& S = (Side == 0) ? Out.L : Out.R;
		S.HairFraction = NAll ? (double)NHair / NAll : 0.0;
		S.bCovered = S.HairFraction > BROW_COVER_FRACTION;
		S.Box[0] = X0; S.Box[1] = Y0; S.Box[2] = X1; S.Box[3] = Y1;
	}
	Out.bCovered = Out.L.bCovered || Out.R.bCovered;
	return Out;
}

cv::Mat BeardTerritoryMask(const cv::Mat& AllLm, int32 W, int32 H, double FaceWidth)
{
	auto Lm = [&](int32 I) { return cv::Point2d(AllLm.at<double>(I, 0), AllLm.at<double>(I, 1)); };
	const cv::Point2d EarL = Lm(132), EarR = Lm(361), MouthL = Lm(61), MouthR = Lm(291);
	std::vector<cv::Point> Poly = {
		cv::Point((int32)EarL.x, (int32)EarL.y),
		cv::Point((int32)MouthL.x, (int32)MouthL.y),
		cv::Point((int32)MouthR.x, (int32)MouthR.y),
		cv::Point((int32)EarR.x, (int32)EarR.y),
		cv::Point((int32)std::min(EarR.x + 0.15 * FaceWidth, (double)W - 1), H - 1),
		cv::Point((int32)std::max(EarL.x - 0.15 * FaceWidth, 0.0), H - 1),
	};
	cv::Mat T(H, W, CV_8U, cv::Scalar(0));
	cv::fillPoly(T, std::vector<std::vector<cv::Point>>{ Poly }, cv::Scalar(255));
	return T;
}

FBeardMeta BeardAnalysis(const cv::Mat& Selfie, const cv::Mat& Parsing, const cv::Mat& AllLm,
                         const cv::Vec3b& SkinBgr, bool bHasHair, const cv::Vec3b& HairBgr,
                         double FaceWidth, cv::Mat& OutBeardMask)
{
	const int32 H = Selfie.rows, W = Selfie.cols;
	FBeardMeta Meta;
	OutBeardMask = cv::Mat(H, W, CV_8U, cv::Scalar(0));

	auto Lm = [&](int32 I) { return cv::Point2d(AllLm.at<double>(I, 0), AllLm.at<double>(I, 1)); };
	const double LipY = Lm(13).y;
	const cv::Point2d Chin = Lm(152), JawL = Lm(172), JawR = Lm(397);

	cv::Mat OvalMask(H, W, CV_8U, cv::Scalar(0));
	{
		std::vector<cv::Point> Poly;
		for (int32 I : FACE_OVAL_ORDER)
		{
			Poly.emplace_back((int32)AllLm.at<double>(I, 0), (int32)AllLm.at<double>(I, 1));
		}
		cv::fillPoly(OvalMask, std::vector<std::vector<cv::Point>>{ Poly }, cv::Scalar(255));
	}
	cv::Mat Head = ResizeNearest(LabelMask(Parsing, { 1, 17 }), W, H);
	cv::Mat Lips = ResizeNearest(LabelMask(Parsing, LIP_LABELS), W, H);

	const int32 Bx0 = std::max((int32)(std::min(JawL.x, JawR.x) + 0.10 * FaceWidth), 0);
	const int32 Bx1 = std::min((int32)(std::max(JawL.x, JawR.x) - 0.10 * FaceWidth), W - 1);
	const int32 By1 = std::min((int32)(Chin.y + 0.6 * FaceWidth), H - 1);

	cv::Mat Region(H, W, CV_8U, cv::Scalar(0));
	int64 NRegion = 0;
	for (int32 y = 0; y < H; ++y)
	{
		const uint8* Ov = OvalMask.ptr<uint8>(y);
		const uint8* Hd = Head.ptr<uint8>(y);
		const uint8* Lp = Lips.ptr<uint8>(y);
		uint8* R = Region.ptr<uint8>(y);
		for (int32 x = 0; x < W; ++x)
		{
			bool In = (Ov[x] > 0) && (y > LipY - 0.05 * FaceWidth);
			if (!In && y >= (int32)Chin.y && y < By1 && x >= Bx0 && x < Bx1) { In = true; }
			if (In && Hd[x] > 0 && Lp[x] == 0)
			{
				R[x] = 1;
				++NRegion;
			}
		}
	}
	if (!bHasHair || NRegion == 0)
	{
		return Meta;
	}

	// chroma 加權 Lab 距離（L×0.3）
	constexpr float LW = 0.3f;
	cv::Mat Lab;
	cv::cvtColor(Selfie, Lab, cv::COLOR_BGR2Lab);
	cv::Vec3f SkinLab = BgrToLabF(SkinBgr);
	cv::Vec3f HairLab = BgrToLabF(HairBgr);
	SkinLab[0] *= LW;
	HairLab[0] *= LW;
	cv::Mat Beard(H, W, CV_8U, cv::Scalar(0));
	for (int32 y = 0; y < H; ++y)
	{
		const uint8* R = Region.ptr<uint8>(y);
		const cv::Vec3b* L = Lab.ptr<cv::Vec3b>(y);
		uint8* B = Beard.ptr<uint8>(y);
		for (int32 x = 0; x < W; ++x)
		{
			if (!R[x]) { continue; }
			const cv::Vec3f P(L[x][0] * LW, (float)L[x][1], (float)L[x][2]);
			const float DSkin = (float)cv::norm(P - SkinLab);
			const float DHair = (float)cv::norm(P - HairLab);
			if (DHair < DSkin) { B[x] = 255; }
		}
	}
	cv::morphologyEx(Beard, Beard, cv::MORPH_OPEN, cv::Mat::ones(5, 5, CV_8U));

	int64 NBeard = 0;
	int32 MaxY = -1;
	for (int32 y = 0; y < H; ++y)
	{
		const uint8* B = Beard.ptr<uint8>(y);
		for (int32 x = 0; x < W; ++x)
		{
			if (B[x]) { ++NBeard; MaxY = y; }
		}
	}
	Meta.Coverage = (double)NBeard / std::max<int64>(NRegion, 1);
	Meta.Drape = (MaxY >= 0) ? std::max((MaxY - Chin.y) / FaceWidth, 0.0) : 0.0;
	Meta.Pixels = (int32)NBeard;
	if (NBeard > 500)
	{
		const cv::Vec3d M = MedianBgr(Selfie, Beard);
		Meta.ColorBgr = M;
		Meta.bHasColor = true;
	}
	OutBeardMask = Beard;
	return Meta;
}

// ---------------------------------------------------------------- LaMa

bool LamaInpaint(FCtx& C, cv::Mat& Bgr, const cv::Mat& HoleBool, FString& Err)
{
	// lama_fill.py 移植：hole bbox＋context 視窗→512²→LaMa→羽化貼回
	if (HoleBool.empty() || cv::countNonZero(HoleBool) == 0)
	{
		return true;
	}
	const int32 H = Bgr.rows, W = Bgr.cols;
	int32 Y0 = H, Y1 = -1, X0 = W, X1 = -1;
	for (int32 y = 0; y < H; ++y)
	{
		const uint8* Ho = HoleBool.ptr<uint8>(y);
		for (int32 x = 0; x < W; ++x)
		{
			if (Ho[x])
			{
				Y0 = std::min(Y0, y); Y1 = std::max(Y1, y);
				X0 = std::min(X0, x); X1 = std::max(X1, x);
			}
		}
	}
	const int32 Cy = (Y0 + Y1) / 2, Cx = (X0 + X1) / 2;
	int32 Half = (int32)(std::max(Y1 - Y0, X1 - X0) * (1 + 0.6) / 2) + 16;
	Half = std::max(Half, 96);

	auto ClampAxis = [Half](int32 Ce, int32 Size, int32& A, int32& B)
	{
		A = Ce - Half;
		B = Ce + Half;
		if (A < 0) { B += -A; A = 0; }
		if (B > Size) { A -= B - Size; B = Size; }
		A = std::max(A, 0);
	};
	int32 Ya, Yb, Xa, Xb;
	ClampAxis(Cy, H, Ya, Yb);
	ClampAxis(Cx, W, Xa, Xb);
	cv::Mat Crop = Bgr(cv::Rect(Xa, Ya, Xb - Xa, Yb - Ya)).clone();
	cv::Mat MCrop(Crop.size(), CV_8U, cv::Scalar(0));
	for (int32 y = Ya; y < Yb; ++y)
	{
		const uint8* Ho = HoleBool.ptr<uint8>(y);
		uint8* M = MCrop.ptr<uint8>(y - Ya);
		for (int32 x = Xa; x < Xb; ++x) { M[x - Xa] = Ho[x] ? 255 : 0; }
	}
	const int32 Ch = Crop.rows, Cw = Crop.cols;
	const int32 PadY = std::max(0, Cw - Ch), PadX = std::max(0, Ch - Cw);
	if (PadY || PadX)
	{
		cv::copyMakeBorder(Crop, Crop, 0, PadY, 0, PadX, cv::BORDER_REFLECT);
		cv::copyMakeBorder(MCrop, MCrop, 0, PadY, 0, PadX, cv::BORDER_CONSTANT, cv::Scalar(0));
	}

	cv::Mat Img512, M512;
	cv::resize(Crop, Img512, cv::Size(512, 512), 0, 0, cv::INTER_AREA);
	cv::resize(MCrop, M512, cv::Size(512, 512), 0, 0, cv::INTER_NEAREST);
	cv::dilate(M512, M512, cv::Mat::ones(5, 5, CV_8U));

	// NCHW RGB [0,1]＋mask 1×1×512×512
	TArray<float> ImgIn, MaskIn;
	ImgIn.SetNumUninitialized(3 * 512 * 512);
	MaskIn.SetNumUninitialized(512 * 512);
	for (int32 y = 0; y < 512; ++y)
	{
		const cv::Vec3b* R = Img512.ptr<cv::Vec3b>(y);
		const uint8* M = M512.ptr<uint8>(y);
		for (int32 x = 0; x < 512; ++x)
		{
			ImgIn[(int64)0 * 512 * 512 + y * 512 + x] = R[x][2] / 255.f; // R
			ImgIn[(int64)1 * 512 * 512 + y * 512 + x] = R[x][1] / 255.f; // G
			ImgIn[(int64)2 * 512 * 512 + y * 512 + x] = R[x][0] / 255.f; // B
			MaskIn[y * 512 + x] = (M[x] > 127) ? 1.f : 0.f;
		}
	}
	TArray<TArray<float>> Outs;
	{
		TConstArrayView<float> Ins[2] = { ImgIn, MaskIn };
		const uint32 S0[4] = { 1, 3, 512, 512 };
		const uint32 S1[4] = { 1, 1, 512, 512 };
		TConstArrayView<uint32> Shapes[2] = { MakeArrayView(S0, 4), MakeArrayView(S1, 4) };
		if (!C.Lama->RunMulti(MakeArrayView(Ins, 2), MakeArrayView(Shapes, 2), Outs, Err))
		{
			return false;
		}
	}
	const TArray<float>* O = nullptr;
	for (const TArray<float>& X : Outs)
	{
		if (X.Num() == 3 * 512 * 512) { O = &X; }
	}
	if (!O)
	{
		Err = TEXT("lama output shape unexpected");
		return false;
	}
	// 有的匯出 0..1、有的 0..255——首跑偵測（同 python _out_is_unit）
	float MaxV = 0.f;
	for (int32 i = 0; i < O->Num(); i += 997) { MaxV = std::max(MaxV, (*O)[i]); }
	const float Scale = (MaxV <= 1.5f) ? 255.f : 1.f;
	cv::Mat Out512(512, 512, CV_8UC3);
	for (int32 y = 0; y < 512; ++y)
	{
		cv::Vec3b* R = Out512.ptr<cv::Vec3b>(y);
		for (int32 x = 0; x < 512; ++x)
		{
			const float Rr = (*O)[(int64)0 * 512 * 512 + y * 512 + x] * Scale;
			const float Gg = (*O)[(int64)1 * 512 * 512 + y * 512 + x] * Scale;
			const float Bb = (*O)[(int64)2 * 512 * 512 + y * 512 + x] * Scale;
			R[x] = cv::Vec3b((uint8)std::clamp(Bb, 0.f, 255.f), (uint8)std::clamp(Gg, 0.f, 255.f),
			                 (uint8)std::clamp(Rr, 0.f, 255.f));
		}
	}

	cv::Mat OutCrop;
	cv::resize(Out512, OutCrop, Crop.size(), 0, 0, cv::INTER_CUBIC);
	OutCrop = OutCrop(cv::Rect(0, 0, Cw, Ch));

	// 羽化貼回（只有洞附近變動）
	cv::Mat HoleF(Ch, Cw, CV_32F);
	for (int32 y = 0; y < Ch; ++y)
	{
		const uint8* M = MCrop.ptr<uint8>(y);
		float* F = HoleF.ptr<float>(y);
		for (int32 x = 0; x < Cw; ++x) { F[x] = (M[x] > 127) ? 1.f : 0.f; }
	}
	cv::Mat Feather = BlurF(HoleF, 3.0);
	for (int32 y = 0; y < Ch; ++y)
	{
		const float* F = Feather.ptr<float>(y);
		const cv::Vec3b* N = OutCrop.ptr<cv::Vec3b>(y);
		cv::Vec3b* D = Bgr.ptr<cv::Vec3b>(Ya + y);
		for (int32 x = 0; x < Cw; ++x)
		{
			const float A = F[x];
			if (A <= 0.f) { continue; }
			for (int32 c = 0; c < 3; ++c)
			{
				const float V = D[Xa + x][c] * (1.f - A) + N[x][c] * A;
				D[Xa + x][c] = (uint8)std::clamp(V, 0.f, 255.f);
			}
		}
	}
	return true;
}

} // namespace NiFace
