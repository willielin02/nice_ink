// MediaPipe FaceLandmarker 的 C++/ONNX 重現（M2）。行為規格＝Tools/FacePipeline/
// mp_onnx_landmarks.py（python 對賬儀器）——兩邊必須逐步一致。
// 模型＝task 解包同權重：face_detector.onnx（BlazeFace 128²/896 anchor）＋
// face_landmarks.onnx（256²/478 點）。索引語義與現行管線 100% 相同。
#pragma once

#include "CoreMinimal.h"
#include "NiceInkFaceCv.h"

class FNiFaceOnnxModel;

class FNiFaceLandmarks
{
public:
	FNiFaceLandmarks(FNiFaceOnnxModel* InDetector, FNiFaceOnnxModel* InLandmarker)
		: Detector(InDetector), Landmarker(InLandmarker) {}

	/** 全鏈：偵測→ROI→裁切→特徵點→映回原圖像素座標。無臉回 false。
	 *  OutPts＝478×2（double，像素）。 */
	bool Detect(const cv::Mat& Bgr, cv::Mat& OutPts, FString& OutError);

private:
	FNiFaceOnnxModel* Detector = nullptr;
	FNiFaceOnnxModel* Landmarker = nullptr;
};
