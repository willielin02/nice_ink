#include "SelfiePipeline.h"

#include "Engine/Texture2D.h"
#include "SelfieImageUtils.h"

USelfiePipeline::USelfiePipeline()
{
	// Default puppet face landmarks in UV space [0,1].
	// These are placeholder positions for a generic face UV layout.
	// Must be replaced with actual UV coordinates from the puppet mesh.
	PuppetFaceLandmarks.SetNum(68);

	// Approximate 68-point face landmark layout in normalized UV space.
	// Jaw contour (0-16)
	const float JawY = 0.75f;
	for (int32 I = 0; I <= 16; ++I)
	{
		const float T = static_cast<float>(I) / 16.0f;
		PuppetFaceLandmarks[I] = FVector2D(0.15f + T * 0.7f, 0.35f + FMath::Sin(T * PI) * 0.4f);
	}
	// Left eyebrow (17-21)
	for (int32 I = 0; I < 5; ++I)
	{
		PuppetFaceLandmarks[17 + I] = FVector2D(0.25f + I * 0.04f, 0.28f - FMath::Sin(I * 0.8f) * 0.02f);
	}
	// Right eyebrow (22-26)
	for (int32 I = 0; I < 5; ++I)
	{
		PuppetFaceLandmarks[22 + I] = FVector2D(0.55f + I * 0.04f, 0.28f - FMath::Sin(I * 0.8f) * 0.02f);
	}
	// Nose bridge (27-30)
	PuppetFaceLandmarks[27] = FVector2D(0.50f, 0.32f);
	PuppetFaceLandmarks[28] = FVector2D(0.50f, 0.38f);
	PuppetFaceLandmarks[29] = FVector2D(0.50f, 0.44f);
	PuppetFaceLandmarks[30] = FVector2D(0.50f, 0.48f);
	// Nose bottom (31-35)
	PuppetFaceLandmarks[31] = FVector2D(0.42f, 0.50f);
	PuppetFaceLandmarks[32] = FVector2D(0.45f, 0.51f);
	PuppetFaceLandmarks[33] = FVector2D(0.50f, 0.52f);
	PuppetFaceLandmarks[34] = FVector2D(0.55f, 0.51f);
	PuppetFaceLandmarks[35] = FVector2D(0.58f, 0.50f);
	// Left eye (36-41)
	PuppetFaceLandmarks[36] = FVector2D(0.30f, 0.35f);
	PuppetFaceLandmarks[37] = FVector2D(0.33f, 0.33f);
	PuppetFaceLandmarks[38] = FVector2D(0.37f, 0.33f);
	PuppetFaceLandmarks[39] = FVector2D(0.40f, 0.35f);
	PuppetFaceLandmarks[40] = FVector2D(0.37f, 0.37f);
	PuppetFaceLandmarks[41] = FVector2D(0.33f, 0.37f);
	// Right eye (42-47)
	PuppetFaceLandmarks[42] = FVector2D(0.60f, 0.35f);
	PuppetFaceLandmarks[43] = FVector2D(0.63f, 0.33f);
	PuppetFaceLandmarks[44] = FVector2D(0.67f, 0.33f);
	PuppetFaceLandmarks[45] = FVector2D(0.70f, 0.35f);
	PuppetFaceLandmarks[46] = FVector2D(0.67f, 0.37f);
	PuppetFaceLandmarks[47] = FVector2D(0.63f, 0.37f);
	// Outer mouth (48-59)
	PuppetFaceLandmarks[48] = FVector2D(0.38f, 0.60f);
	PuppetFaceLandmarks[49] = FVector2D(0.42f, 0.58f);
	PuppetFaceLandmarks[50] = FVector2D(0.46f, 0.57f);
	PuppetFaceLandmarks[51] = FVector2D(0.50f, 0.58f);
	PuppetFaceLandmarks[52] = FVector2D(0.54f, 0.57f);
	PuppetFaceLandmarks[53] = FVector2D(0.58f, 0.58f);
	PuppetFaceLandmarks[54] = FVector2D(0.62f, 0.60f);
	PuppetFaceLandmarks[55] = FVector2D(0.58f, 0.64f);
	PuppetFaceLandmarks[56] = FVector2D(0.54f, 0.66f);
	PuppetFaceLandmarks[57] = FVector2D(0.50f, 0.66f);
	PuppetFaceLandmarks[58] = FVector2D(0.46f, 0.66f);
	PuppetFaceLandmarks[59] = FVector2D(0.42f, 0.64f);
	// Inner mouth (60-67)
	PuppetFaceLandmarks[60] = FVector2D(0.40f, 0.60f);
	PuppetFaceLandmarks[61] = FVector2D(0.46f, 0.59f);
	PuppetFaceLandmarks[62] = FVector2D(0.50f, 0.59f);
	PuppetFaceLandmarks[63] = FVector2D(0.54f, 0.59f);
	PuppetFaceLandmarks[64] = FVector2D(0.60f, 0.60f);
	PuppetFaceLandmarks[65] = FVector2D(0.54f, 0.63f);
	PuppetFaceLandmarks[66] = FVector2D(0.50f, 0.64f);
	PuppetFaceLandmarks[67] = FVector2D(0.46f, 0.63f);
}

bool USelfiePipeline::LoadModels(const FString& ModelDirectory)
{
	// TODO: Load ONNX models via NNE (Neural Network Engine)
	// Expected files in ModelDirectory:
	//   3ddfa_v2.onnx     (~12MB)  - face detection + 3DMM + UV mapping
	//   bisenet.onnx      (~50MB)  - face semantic segmentation

	const FString FaceModelPath = FPaths::Combine(ModelDirectory, TEXT("3ddfa_v2.onnx"));
	const FString ParseModelPath = FPaths::Combine(ModelDirectory, TEXT("bisenet.onnx"));

	if (!FPaths::FileExists(FaceModelPath))
	{
		UE_LOG(LogTemp, Warning, TEXT("SelfiePipeline: 3DDFA_V2 model not found at %s"), *FaceModelPath);
	}
	if (!FPaths::FileExists(ParseModelPath))
	{
		UE_LOG(LogTemp, Warning, TEXT("SelfiePipeline: BiSeNet model not found at %s"), *ParseModelPath);
	}

	// NNE model loading would go here:
	// 1. UNNEModelData* ModelData = LoadObject<UNNEModelData>(...)
	// 2. Create runtime model from ModelData
	// 3. Bind input/output tensors

	bModelsLoaded = true;
	UE_LOG(LogTemp, Log, TEXT("SelfiePipeline: Models loaded (inference stubs active until ONNX integration)"));
	return true;
}

FSelfieResult USelfiePipeline::ProcessSelfie(UTexture2D* SelfiePhoto)
{
	FSelfieResult Result;

	if (!SelfiePhoto)
	{
		Result.ErrorMessage = TEXT("No selfie photo provided");
		OnSelfieError.Broadcast(Result.ErrorMessage);
		LastResult = Result;
		return Result;
	}

	TArray<FColor> Pixels;
	int32 Width = 0, Height = 0;
	if (!ExtractPixels(SelfiePhoto, Pixels, Width, Height))
	{
		Result.ErrorMessage = TEXT("Failed to read selfie pixels");
		OnSelfieError.Broadcast(Result.ErrorMessage);
		LastResult = Result;
		return Result;
	}

	// ① 3DDFA_V2: face detection + landmarks + head pose + UV mapping
	TArray<FVector2D> Landmarks;
	float Yaw = 0.0f;
	TArray<FColor> FrontalizedPixels;
	int32 FrontalW = 0, FrontalH = 0;

	if (!InferFaceDetection(Pixels, Width, Height, Landmarks, Yaw, FrontalizedPixels, FrontalW, FrontalH))
	{
		Result.ErrorMessage = TEXT("No face detected. Please upload a front-facing selfie.");
		OnSelfieError.Broadcast(Result.ErrorMessage);
		LastResult = Result;
		return Result;
	}

	Result.bFaceDetected = true;
	Result.HeadYaw = Yaw;
	Result.FaceLandmarks = Landmarks;

	constexpr float MaxAllowedYaw = 25.0f;
	if (FMath::Abs(Yaw) > MaxAllowedYaw)
	{
		Result.ErrorMessage = FString::Printf(TEXT("Head angle too large (%.1f deg). Please face the camera directly."), Yaw);
		OnSelfieError.Broadcast(Result.ErrorMessage);
		LastResult = Result;
		return Result;
	}

	// ② BiSeNet: face parsing (skin mask)
	TArray<uint8> Segmentation;
	if (!InferFaceParsing(Pixels, Width, Height, Segmentation))
	{
		Result.ErrorMessage = TEXT("Face parsing failed");
		OnSelfieError.Broadcast(Result.ErrorMessage);
		LastResult = Result;
		return Result;
	}

	TArray<uint8> SkinMask;
	SkinMask.SetNumZeroed(Width * Height);

	for (int32 I = 0; I < Segmentation.Num() && I < Width * Height; ++I)
	{
		if (Segmentation[I] == BISENET_SKIN || Segmentation[I] == BISENET_NOSE)
		{
			SkinMask[I] = 255;
		}
	}

	// ③ Face texture: affine warp frontalized UV onto puppet face UV
	if (FrontalizedPixels.Num() > 0 && Landmarks.Num() >= 68 && PuppetFaceLandmarks.Num() >= 68)
	{
		Result.FaceTexture = FSelfieImageUtils::GenerateFaceTexture(this, FrontalizedPixels, FrontalW, FrontalH, Landmarks, PuppetFaceLandmarks, FaceTextureResolution);
	}

	// ④ Skin color: median LAB from skin-masked pixels
	Result.DetectedSkinTone = FSelfieImageUtils::ExtractSkinTone(Pixels, SkinMask, Width, Height);

	Result.bSuccess = true;
	LastResult = Result;
	OnSelfieProcessed.Broadcast(Result);
	return Result;
}

bool USelfiePipeline::RunFaceDetection(UTexture2D* SelfiePhoto, TArray<FVector2D>& OutLandmarks, float& OutYaw, UTexture2D*& OutFrontalizedUV)
{
	TArray<FColor> Pixels;
	int32 W = 0, H = 0;
	if (!ExtractPixels(SelfiePhoto, Pixels, W, H))
	{
		return false;
	}

	TArray<FColor> FrontalPixels;
	int32 FW = 0, FH = 0;
	if (!InferFaceDetection(Pixels, W, H, OutLandmarks, OutYaw, FrontalPixels, FW, FH))
	{
		return false;
	}

	if (FrontalPixels.Num() > 0)
	{
		OutFrontalizedUV = UTexture2D::CreateTransient(FW, FH, PF_B8G8R8A8, TEXT("FrontalizedUV"));
		if (OutFrontalizedUV)
		{
			void* Data = OutFrontalizedUV->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
			FMemory::Memcpy(Data, FrontalPixels.GetData(), FrontalPixels.Num() * sizeof(FColor));
			OutFrontalizedUV->GetPlatformData()->Mips[0].BulkData.Unlock();
			OutFrontalizedUV->UpdateResource();
		}
	}

	return true;
}

bool USelfiePipeline::RunFaceParsing(UTexture2D* SelfiePhoto, TArray<uint8>& OutSkinMask)
{
	TArray<FColor> Pixels;
	int32 W = 0, H = 0;
	if (!ExtractPixels(SelfiePhoto, Pixels, W, H))
	{
		return false;
	}

	TArray<uint8> Segmentation;
	if (!InferFaceParsing(Pixels, W, H, Segmentation))
	{
		return false;
	}

	OutSkinMask.SetNumZeroed(W * H);
	for (int32 I = 0; I < Segmentation.Num() && I < W * H; ++I)
	{
		if (Segmentation[I] == BISENET_SKIN || Segmentation[I] == BISENET_NOSE)
		{
			OutSkinMask[I] = 255;
		}
	}

	return true;
}

FLinearColor USelfiePipeline::RunSkinColorExtraction(UTexture2D* SelfiePhoto, const TArray<uint8>& SkinMask)
{
	TArray<FColor> Pixels;
	int32 W = 0, H = 0;
	if (!ExtractPixels(SelfiePhoto, Pixels, W, H))
	{
		return FLinearColor(0.75f, 0.55f, 0.42f, 1.0f);
	}
	return FSelfieImageUtils::ExtractSkinTone(Pixels, SkinMask, W, H);
}

bool USelfiePipeline::ExtractPixels(UTexture2D* Texture, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight)
{
	if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.Num() == 0)
	{
		return false;
	}

	OutWidth = Texture->GetSizeX();
	OutHeight = Texture->GetSizeY();

	FByteBulkData& BulkData = Texture->GetPlatformData()->Mips[0].BulkData;
	const void* RawData = BulkData.LockReadOnly();
	if (!RawData)
	{
		return false;
	}

	const int32 NumPixels = OutWidth * OutHeight;
	OutPixels.SetNum(NumPixels);
	FMemory::Memcpy(OutPixels.GetData(), RawData, NumPixels * sizeof(FColor));
	BulkData.Unlock();

	return true;
}

// ===== NNE Model Inference Stubs =====
// These produce synthetic outputs for pipeline testing.
// Replace with actual NNE inference when ONNX models are integrated.

bool USelfiePipeline::InferFaceDetection(const TArray<FColor>& Pixels, int32 Width, int32 Height, TArray<FVector2D>& OutLandmarks, float& OutYaw, TArray<FColor>& OutFrontalizedPixels, int32& OutFrontalW, int32& OutFrontalH)
{
	// Stub: generate approximate landmarks based on image center
	OutLandmarks.SetNum(68);
	const float CX = Width * 0.5f;
	const float CY = Height * 0.45f;
	const float Scale = FMath::Min(Width, Height) * 0.35f;

	// Jaw (0-16)
	for (int32 I = 0; I <= 16; ++I)
	{
		const float Angle = PI * 0.15f + (PI * 0.7f) * I / 16.0f;
		OutLandmarks[I] = FVector2D(CX + FMath::Cos(Angle) * Scale, CY + FMath::Sin(Angle) * Scale * 1.2f);
	}
	// Eyebrows (17-26)
	for (int32 I = 0; I < 5; ++I)
	{
		OutLandmarks[17 + I] = FVector2D(CX - Scale * 0.35f + I * Scale * 0.12f, CY - Scale * 0.35f);
		OutLandmarks[22 + I] = FVector2D(CX + Scale * 0.1f + I * Scale * 0.12f, CY - Scale * 0.35f);
	}
	// Nose (27-35)
	OutLandmarks[27] = FVector2D(CX, CY - Scale * 0.15f);
	OutLandmarks[28] = FVector2D(CX, CY - Scale * 0.05f);
	OutLandmarks[29] = FVector2D(CX, CY + Scale * 0.05f);
	OutLandmarks[30] = FVector2D(CX, CY + Scale * 0.15f);
	for (int32 I = 0; I < 5; ++I)
	{
		OutLandmarks[31 + I] = FVector2D(CX - Scale * 0.12f + I * Scale * 0.06f, CY + Scale * 0.18f);
	}
	// Eyes (36-47)
	for (int32 I = 0; I < 6; ++I)
	{
		const float Angle = I * TWO_PI / 6.0f;
		OutLandmarks[36 + I] = FVector2D(CX - Scale * 0.22f + FMath::Cos(Angle) * Scale * 0.08f, CY - Scale * 0.2f + FMath::Sin(Angle) * Scale * 0.04f);
		OutLandmarks[42 + I] = FVector2D(CX + Scale * 0.22f + FMath::Cos(Angle) * Scale * 0.08f, CY - Scale * 0.2f + FMath::Sin(Angle) * Scale * 0.04f);
	}
	// Mouth (48-67)
	for (int32 I = 0; I < 12; ++I)
	{
		const float Angle = I * TWO_PI / 12.0f;
		OutLandmarks[48 + I] = FVector2D(CX + FMath::Cos(Angle) * Scale * 0.18f, CY + Scale * 0.4f + FMath::Sin(Angle) * Scale * 0.08f);
	}
	for (int32 I = 0; I < 8; ++I)
	{
		const float Angle = I * TWO_PI / 8.0f;
		OutLandmarks[60 + I] = FVector2D(CX + FMath::Cos(Angle) * Scale * 0.12f, CY + Scale * 0.4f + FMath::Sin(Angle) * Scale * 0.05f);
	}

	OutYaw = 0.0f;

	// Stub: copy original pixels as "frontalized" output
	OutFrontalizedPixels = Pixels;
	OutFrontalW = Width;
	OutFrontalH = Height;

	return true;
}

bool USelfiePipeline::InferFaceParsing(const TArray<FColor>& Pixels, int32 Width, int32 Height, TArray<uint8>& OutSegmentation)
{
	// Stub: generate approximate segmentation using simple heuristics
	OutSegmentation.SetNumZeroed(Width * Height);

	const float CX = Width * 0.5f;
	const float CY = Height * 0.45f;
	const float FaceRadius = FMath::Min(Width, Height) * 0.3f;

	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const float DX = X - CX;
			const float DY = Y - CY;
			const float Dist = FMath::Sqrt(DX * DX + DY * DY);
			const int32 Idx = Y * Width + X;

			if (Dist < FaceRadius * 0.85f && DY > -FaceRadius * 0.4f)
			{
				OutSegmentation[Idx] = BISENET_SKIN;
			}
		}
	}

	return true;
}

