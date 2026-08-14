// FaceBakery 入口：模型載入＋intake 編排（intake_selfie.py 的 C++ 移植）
// ＋NiFaceBake console 對賬鉤（robo/金樣本 diff 用）。
#include "NiceInkFaceBakery.h"

#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NiceInkFaceBakeryImpl.h"
#include "NiceInkFaceLandmarks.h"
#include "NiceInkFaceOnnx.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogNiFaceBakery, Log, All);

namespace NiFace
{

static FString BakeryRoot()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("FaceBakery"));
}

// intake_selfie.bake_sumo_ink_mask：FaceUV 眼罩→UV0＋靜態禁畫（髮+褌）聯集
static bool BakeSumoInkMask(FCtx& C, const cv::Mat& EyeMask, cv::Mat& OutAllowed, FString& Err)
{
	const int32 SrcSize = EyeMask.rows;   // 2048（FaceUV）
	const int32 Size = TEX_SIZE;          // 2048（UV0）

	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *(C.DataDir / TEXT("sumo_ink_uv_map.json"))))
	{
		Err = TEXT("sumo_ink_uv_map.json missing");
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		Err = TEXT("sumo_ink_uv_map.json parse failed");
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Tris = nullptr;
	if (!Root->TryGetArrayField(TEXT("tris"), Tris) || !Tris)
	{
		Err = TEXT("sumo_ink_uv_map.json no tris");
		return false;
	}

	cv::Mat Hair, Fund;
	if (!LoadImage(C.DataDir / TEXT("hair_mask.png"), Hair, /*bGrayscale=*/true) ||
		!LoadImage(C.DataDir / TEXT("fundoshi_mask_sharp.png"), Fund, /*bGrayscale=*/true))
	{
		Err = TEXT("hair_mask.png / fundoshi_mask_sharp.png missing");
		return false;
	}
	cv::Mat HairR, FundR;
	cv::resize(Hair, HairR, cv::Size(Size, Size), 0, 0, cv::INTER_LINEAR);
	cv::resize(Fund, FundR, cv::Size(Size, Size), 0, 0, cv::INTER_AREA);
	cv::Mat StaticAllowed(Size, Size, CV_8U);
	for (int32 y = 0; y < Size; ++y)
	{
		const uint8* Hh = HairR.ptr<uint8>(y);
		const uint8* Ff = FundR.ptr<uint8>(y);
		uint8* S = StaticAllowed.ptr<uint8>(y);
		for (int32 x = 0; x < Size; ++x) { S[x] = 255 - std::max(Hh[x], Ff[x]); }
	}

	cv::Mat Acc(Size, Size, CV_8U, cv::Scalar(0));
	double EyeMax;
	cv::minMaxLoc(EyeMask, nullptr, &EyeMax);
	if (EyeMax > 0)
	{
		for (const TSharedPtr<FJsonValue>& RowV : *Tris)
		{
			const TArray<TSharedPtr<FJsonValue>>& Row = RowV->AsArray();
			cv::Point2f Dst[3], Src[3];
			for (int32 k = 0; k < 3; ++k)
			{
				const double U0 = Row[k * 2]->AsNumber(), V0 = Row[k * 2 + 1]->AsNumber();
				const double U1 = Row[6 + k * 2]->AsNumber(), V1 = Row[6 + k * 2 + 1]->AsNumber();
				Dst[k] = cv::Point2f((float)(U0 * Size), (float)((1.0 - V0) * Size));
				Src[k] = cv::Point2f((float)(U1 * SrcSize), (float)((1.0 - V1) * SrcSize));
			}
			const cv::Point2f E1 = Dst[1] - Dst[0], E2 = Dst[2] - Dst[0];
			if (std::abs(E1.x * E2.y - E1.y * E2.x) < 1e-3)
			{
				continue;
			}
			int32 Sx0 = (int32)std::floor(std::min({ Src[0].x, Src[1].x, Src[2].x }));
			int32 Sy0 = (int32)std::floor(std::min({ Src[0].y, Src[1].y, Src[2].y }));
			int32 Sx1 = (int32)std::ceil(std::max({ Src[0].x, Src[1].x, Src[2].x }));
			int32 Sy1 = (int32)std::ceil(std::max({ Src[0].y, Src[1].y, Src[2].y }));
			Sx0 = std::max(Sx0, 0);
			Sy0 = std::max(Sy0, 0);
			Sx1 = std::min(Sx1, SrcSize);
			Sy1 = std::min(Sy1, SrcSize);
			if (Sx1 <= Sx0 || Sy1 <= Sy0)
			{
				continue;
			}
			double SubMax;
			cv::minMaxLoc(EyeMask(cv::Rect(Sx0, Sy0, Sx1 - Sx0, Sy1 - Sy0)), nullptr, &SubMax);
			if (SubMax == 0)
			{
				continue;
			}
			// dst bbox ROI 化的整圖 warp（結果等值、省 900 次全幅 warp）
			int32 Dx0 = std::max((int32)std::floor(std::min({ Dst[0].x, Dst[1].x, Dst[2].x })) - 1, 0);
			int32 Dy0 = std::max((int32)std::floor(std::min({ Dst[0].y, Dst[1].y, Dst[2].y })) - 1, 0);
			int32 Dx1 = std::min((int32)std::ceil(std::max({ Dst[0].x, Dst[1].x, Dst[2].x })) + 1, Size);
			int32 Dy1 = std::min((int32)std::ceil(std::max({ Dst[0].y, Dst[1].y, Dst[2].y })) + 1, Size);
			if (Dx1 <= Dx0 || Dy1 <= Dy0)
			{
				continue;
			}
			cv::Mat M = cv::getAffineTransform(Src, Dst);
			M.at<double>(0, 2) -= Dx0;
			M.at<double>(1, 2) -= Dy0;
			cv::Mat WarpedRoi;
			cv::warpAffine(EyeMask, WarpedRoi, M, cv::Size(Dx1 - Dx0, Dy1 - Dy0), cv::INTER_LINEAR);
			cv::Mat TriMask(WarpedRoi.size(), CV_8U, cv::Scalar(0));
			cv::Point Poly[3];
			for (int32 k = 0; k < 3; ++k)
			{
				Poly[k] = cv::Point((int32)std::lround(Dst[k].x) - Dx0, (int32)std::lround(Dst[k].y) - Dy0);
			}
			cv::fillConvexPoly(TriMask, Poly, 3, cv::Scalar(255));
			cv::Mat Roi = Acc(cv::Rect(Dx0, Dy0, Dx1 - Dx0, Dy1 - Dy0));
			for (int32 y = 0; y < WarpedRoi.rows; ++y)
			{
				const uint8* Wr = WarpedRoi.ptr<uint8>(y);
				const uint8* Tm = TriMask.ptr<uint8>(y);
				uint8* R = Roi.ptr<uint8>(y);
				for (int32 x = 0; x < WarpedRoi.cols; ++x)
				{
					R[x] = std::max(R[x], (uint8)(Wr[x] & Tm[x]));
				}
			}
		}
	}
	OutAllowed.create(Size, Size, CV_8U);
	for (int32 y = 0; y < Size; ++y)
	{
		const uint8* A = Acc.ptr<uint8>(y);
		const uint8* S = StaticAllowed.ptr<uint8>(y);
		uint8* O = OutAllowed.ptr<uint8>(y);
		for (int32 x = 0; x < Size; ++x) { O[x] = std::min((uint8)(255 - A[x]), S[x]); }
	}
	return true;
}

static double SrgbToLinear(double C01)
{
	return C01 <= 0.04045 ? C01 / 12.92 : std::pow((C01 + 0.055) / 1.055, 2.4);
}

} // namespace NiFace

// ---------------------------------------------------------------- public API

bool FNiFaceBakery::IsAvailable()
{
	const FString Root = NiFace::BakeryRoot();
	const TCHAR* Needed[] = {
		TEXT("models/bisenet_512.onnx"), TEXT("models/lama_fp32.onnx"),
		TEXT("models/face_detector.onnx"), TEXT("models/face_landmarks.onnx"),
		TEXT("data/faceuv_mask_coverage.png"), TEXT("data/faceuv_expansion_data.json"),
		TEXT("data/sumo_ink_uv_map.json"), TEXT("data/hair_mask.png"),
		TEXT("data/fundoshi_mask_sharp.png") };
	for (const TCHAR* N : Needed)
	{
		if (!FPaths::FileExists(Root / N))
		{
			return false;
		}
	}
	return true;
}

// 模型常駐（SHIP_PLAN C1 紅利：NNE 資產載一次＝冷啟大頭只付首次；LaMa 的
// ORT session 建立實測 ~60s）。RunIntake（首用）與 WarmupModels（選單預熱）
// 共用同一鎖同一快取：預熱撞上上傳＝上傳等鎖後接暖模型，永不重載。
namespace
{
struct FModelCache
{
	FCriticalSection Mutex;
	TUniquePtr<FNiFaceOnnxModel> Bisenet, Det, Lm, Lama;
};
FModelCache GModelCache;

// 呼叫端須持 GModelCache.Mutex
bool EnsureModelsLocked(FString& OutErr)
{
	if (GModelCache.Bisenet && GModelCache.Det && GModelCache.Lm && GModelCache.Lama)
	{
		return true;
	}
	const FString Models = NiFace::BakeryRoot() / TEXT("models");
	GModelCache.Bisenet = FNiFaceOnnxModel::CreateFromFile(Models / TEXT("bisenet_512.onnx"), OutErr);
	if (!GModelCache.Bisenet) { return false; }
	GModelCache.Det = FNiFaceOnnxModel::CreateFromFile(Models / TEXT("face_detector.onnx"), OutErr);
	if (!GModelCache.Det) { return false; }
	GModelCache.Lm = FNiFaceOnnxModel::CreateFromFile(Models / TEXT("face_landmarks.onnx"), OutErr);
	if (!GModelCache.Lm) { return false; }
	GModelCache.Lama = FNiFaceOnnxModel::CreateFromFile(Models / TEXT("lama_fp32.onnx"), OutErr);
	return GModelCache.Lama.IsValid();
}
} // namespace

bool FNiFaceBakery::WarmupModels(FString& OutError)
{
	FScopeLock Lock(&GModelCache.Mutex);
	return EnsureModelsLocked(OutError);
}

bool FNiFaceBakery::RunIntake(const FString& SelfiePath, const FString& OutDir,
                              FString& OutError, const FOptions& Options)
{
	const double T0 = FPlatformTime::Seconds();
	IFileManager::Get().MakeDirectory(*OutDir, /*Tree=*/true);

	NiFace::FCtx C;
	C.DataDir = NiFace::BakeryRoot() / TEXT("data");
	C.Progress = Options.Progress;
	C.bDump = Options.bDumpIntermediates;
	C.DumpDir = OutDir;
	C.T0 = T0;
	C.Log.Add(FString::Printf(TEXT("native bakery: %s -> %s"), *SelfiePath, *OutDir));

	auto Fail = [&](const FString& Why)
	{
		OutError = Why;
		C.Log.Add(FString::Printf(TEXT("FAILED: %s"), *Why));
		FFileHelper::SaveStringToFile(FString::Join(C.Log, TEXT("\n")) + TEXT("\n"),
			*(OutDir / TEXT("intake_log.txt")));
		UE_LOG(LogNiFaceBakery, Warning, TEXT("NiFaceBake FAILED: %s"), *Why);
		return false;
	};

	// 讀自拍（UE ImageWrapper＝unicode 路徑安全＋jpeg 支援；
	// 記帳：cv2.imread 會套 EXIF 方向、ImageWrapper 不套——手機直幅照差異點）
	cv::Mat Selfie;
	if (!NiFace::LoadImage(SelfiePath, Selfie, /*bGrayscale=*/false))
	{
		return Fail(FString::Printf(TEXT("ERROR: Cannot read %s"), *SelfiePath));
	}

	// 模型常駐快取（見 GModelCache）：整趟 RunIntake 互斥＝單工
	FScopeLock Lock(&GModelCache.Mutex);
	FString Err;
	if (!GModelCache.Lama)
	{
		C.Say(TEXT("loading models (bisenet/lama/detector/landmarks)..."));
	}
	if (!EnsureModelsLocked(Err))
	{
		return Fail(Err);
	}
	FNiFaceLandmarks Landmarks(GModelCache.Det.Get(), GModelCache.Lm.Get());
	C.Bisenet = GModelCache.Bisenet.Get();
	C.Lama = GModelCache.Lama.Get();
	C.Landmarks = &Landmarks;

	// 主管線
	NiFace::FBakeResult Result;
	if (!NiFace::SelfieToFaceTexture(C, Selfie, Result, Err))
	{
		return Fail(Err);
	}

	// 眼罩→sumo UV0（含靜態禁畫預乘）
	C.Say(TEXT("baking sumo ink mask..."));
	cv::Mat Allowed;
	if (!NiFace::BakeSumoInkMask(C, Result.EyeMask, Allowed, Err))
	{
		return Fail(Err);
	}

	// 成品落盤（同 intake_selfie.py 四工件＋thumb）
	C.Say(TEXT("writing artifacts..."));
	if (!NiFace::SavePng(Result.FaceOpen, OutDir / TEXT("face_open.png")) ||
		!NiFace::SavePng(Result.FaceClosed, OutDir / TEXT("face_closed.png")) ||
		!NiFace::SavePng(Allowed, OutDir / TEXT("eye_mask_ink.png")))
	{
		return Fail(TEXT("artifact write failed"));
	}
	if (C.bDump)
	{
		NiFace::SavePng(Result.EyeMask, OutDir / TEXT("eye_mask.png"));
	}

	// skin_color.json
	{
		const int32 R = Result.SkinColor[2], G = Result.SkinColor[1], B = Result.SkinColor[0];
		const FString JsonStr = FString::Printf(
			TEXT("{\n  \"srgb_rgb_255\": [%d, %d, %d],\n  \"srgb_hex\": \"#%02x%02x%02x\",\n  \"linear_rgb\": [%.6f, %.6f, %.6f]\n}"),
			R, G, B, R, G, B,
			NiFace::SrgbToLinear(R / 255.0), NiFace::SrgbToLinear(G / 255.0), NiFace::SrgbToLinear(B / 255.0));
		if (!FFileHelper::SaveStringToFile(JsonStr, *(OutDir / TEXT("skin_color.json"))))
		{
			return Fail(TEXT("skin_color.json write failed"));
		}
	}

	// 縮圖（臉庫列表；HUD FaceTok 裁切框 (0.30,0.22)+(0.40,0.40)、膚色打底）
	{
		const int32 Hh = Result.FaceOpen.rows, Ww = Result.FaceOpen.cols;
		const cv::Rect CropR((int32)(0.30 * Ww), (int32)(0.22 * Hh),
			(int32)(0.70 * Ww) - (int32)(0.30 * Ww), (int32)(0.62 * Hh) - (int32)(0.22 * Hh));
		cv::Mat Crop = Result.FaceOpen(CropR);
		cv::Mat Comp(Crop.size(), CV_8UC3);
		for (int32 y = 0; y < Crop.rows; ++y)
		{
			const cv::Vec4b* P = Crop.ptr<cv::Vec4b>(y);
			cv::Vec3b* O = Comp.ptr<cv::Vec3b>(y);
			for (int32 x = 0; x < Crop.cols; ++x)
			{
				const float A = P[x][3] / 255.f;
				for (int32 c = 0; c < 3; ++c)
				{
					O[x][c] = (uint8)std::clamp(P[x][c] * A + Result.SkinColor[c] * (1.f - A), 0.f, 255.f);
				}
			}
		}
		cv::Mat Thumb;
		cv::resize(Comp, Thumb, cv::Size(256, 256), 0, 0, cv::INTER_AREA);
		NiFace::SavePng(Thumb, OutDir / TEXT("thumb.png"));
	}

	C.Log.Add(FString::Printf(TEXT("elapsed %.1fs"), FPlatformTime::Seconds() - T0));
	C.Log.Add(TEXT("DONE_INTAKE"));
	FFileHelper::SaveStringToFile(FString::Join(C.Log, TEXT("\n")) + TEXT("\n"),
		*(OutDir / TEXT("intake_log.txt")));
	UE_LOG(LogNiFaceBakery, Log, TEXT("NiFaceBake DONE in %.1fs -> %s"),
		FPlatformTime::Seconds() - T0, *OutDir);
	return true;
}

// ---------------------------------------------------------------- robo hook
// NiFaceBake <selfie> <outdir> [dump]：對賬儀器（-game/-ExecCmds 可呼）。
// 完成後寫 <outdir>/BAKE_RESULT.txt（DONE/FAIL 行）供背景迴圈等待。
static FAutoConsoleCommand GNiFaceBakeCmd(
	TEXT("NiFaceBake"),
	TEXT("Run the native face bakery: NiFaceBake <selfie> <outdir> [dump]"),
	FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
	{
		if (Args.Num() < 2)
		{
			UE_LOG(LogNiFaceBakery, Warning, TEXT("usage: NiFaceBake <selfie> <outdir> [dump]"));
			return;
		}
		const FString Selfie = Args[0];
		const FString OutDir = Args[1];
		FNiFaceBakery::FOptions Opt;
		Opt.bDumpIntermediates = Args.Num() > 2 && Args[2] == TEXT("dump");
		Async(EAsyncExecution::Thread, [Selfie, OutDir, Opt]()
		{
			FString Err;
			const bool bOk = FNiFaceBakery::RunIntake(Selfie, OutDir, Err, Opt);
			FFileHelper::SaveStringToFile(bOk ? TEXT("DONE\n") : FString::Printf(TEXT("FAIL %s\n"), *Err),
				*(OutDir / TEXT("BAKE_RESULT.txt")));
		});
	}));
