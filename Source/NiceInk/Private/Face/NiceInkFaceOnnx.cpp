#include "NiceInkFaceOnnx.h"

#include "Async/Async.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NNE.h"
#include "NNEModelData.h"

DEFINE_LOG_CATEGORY_STATIC(LogNiFaceOnnx, Log, All);

TUniquePtr<FNiFaceOnnxModel> FNiFaceOnnxModel::CreateFromFile(const FString& OnnxPath, FString& OutError)
{
	TArray64<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *OnnxPath))
	{
		OutError = FString::Printf(TEXT("cannot read %s"), *OnnxPath);
		return nullptr;
	}

	// NewObject 只能在 game thread；Init 只是拷 bytes，一併做掉再回工作執行緒。
	TStrongObjectPtr<UNNEModelData> Data;
	if (IsInGameThread())
	{
		UNNEModelData* Obj = NewObject<UNNEModelData>();
		Obj->Init(TEXT("onnx"), TConstArrayView64<uint8>(Bytes.GetData(), Bytes.Num()));
		Data = TStrongObjectPtr<UNNEModelData>(Obj);
	}
	else
	{
		FEvent* DoneEvent = FPlatformProcess::GetSynchEventFromPool(true);
		AsyncTask(ENamedThreads::GameThread, [&Data, &Bytes, DoneEvent]()
		{
			UNNEModelData* Obj = NewObject<UNNEModelData>();
			Obj->Init(TEXT("onnx"), TConstArrayView64<uint8>(Bytes.GetData(), Bytes.Num()));
			Data = TStrongObjectPtr<UNNEModelData>(Obj);
			DoneEvent->Trigger();
		});
		DoneEvent->Wait();
		FPlatformProcess::ReturnSynchEventToPool(DoneEvent);
	}

	TWeakInterfacePtr<INNERuntimeCPU> Runtime = UE::NNE::GetRuntime<INNERuntimeCPU>(TEXT("NNERuntimeORTCpu"));
	if (!Runtime.IsValid())
	{
		OutError = TEXT("NNERuntimeORTCpu not available (plugin disabled?)");
		return nullptr;
	}

	TSharedPtr<UE::NNE::IModelCPU> Model = Runtime->CreateModelCPU(Data.Get());
	if (!Model.IsValid())
	{
		OutError = FString::Printf(TEXT("CreateModelCPU failed for %s"), *FPaths::GetCleanFilename(OnnxPath));
		return nullptr;
	}
	TSharedPtr<UE::NNE::IModelInstanceCPU> Instance = Model->CreateModelInstanceCPU();
	if (!Instance.IsValid())
	{
		OutError = FString::Printf(TEXT("CreateModelInstanceCPU failed for %s"), *FPaths::GetCleanFilename(OnnxPath));
		return nullptr;
	}

	TUniquePtr<FNiFaceOnnxModel> Out = TUniquePtr<FNiFaceOnnxModel>(new FNiFaceOnnxModel());
	Out->ModelData = MoveTemp(Data);
	Out->Model = Model;
	Out->Instance = Instance;
	UE_LOG(LogNiFaceOnnx, Log, TEXT("NiFace: model ready %s (%.1f MB)"),
		*FPaths::GetCleanFilename(OnnxPath), Bytes.Num() / 1e6);
	return Out;
}

FNiFaceOnnxModel::~FNiFaceOnnxModel()
{
	// TStrongObjectPtr 解引用必須在 game thread 釋放才安全嗎？——StrongObjectPtr 的
	// 釋放走 FGCObject 解錨，任意執行緒 OK（引擎自鎖）。Instance/Model 純 native。
}

bool FNiFaceOnnxModel::Run(TConstArrayView<float> Input, TConstArrayView<uint32> InShape,
                           TArray<TArray<float>>& Outputs, FString& OutError)
{
	TConstArrayView<float> Ins[1] = { Input };
	TConstArrayView<uint32> Shapes[1] = { InShape };
	return RunMulti(MakeArrayView(Ins, 1), MakeArrayView(Shapes, 1), Outputs, OutError);
}

bool FNiFaceOnnxModel::RunMulti(TConstArrayView<TConstArrayView<float>> Inputs,
                                TConstArrayView<TConstArrayView<uint32>> InShapes,
                                TArray<TArray<float>>& Outputs, FString& OutError)
{
	TArray<UE::NNE::FTensorShape> Shapes;
	for (TConstArrayView<uint32> S : InShapes)
	{
		Shapes.Add(UE::NNE::FTensorShape::Make(S));
	}
	if (Instance->SetInputTensorShapes(Shapes) != UE::NNE::EResultStatus::Ok)
	{
		OutError = TEXT("SetInputTensorShapes failed");
		return false;
	}

	TArray<UE::NNE::FTensorBindingCPU> InBind;
	for (TConstArrayView<float> In : Inputs)
	{
		InBind.Add({ const_cast<float*>(In.GetData()), In.Num() * sizeof(float) });
	}

	// 有些模型（tf2onnx 轉出的動態批次）SetInputTensorShapes 後不解析輸出形狀
	// ——fallback 用符號形狀推導（負維＝動態、當 1：本管線批次恆 1）
	TArray<uint64> Volumes;
	TConstArrayView<UE::NNE::FTensorShape> OutShapes = Instance->GetOutputTensorShapes();
	if (OutShapes.Num() > 0)
	{
		for (const UE::NNE::FTensorShape& S : OutShapes)
		{
			Volumes.Add(S.Volume());
		}
	}
	else
	{
		for (const UE::NNE::FTensorDesc& D : Instance->GetOutputTensorDescs())
		{
			uint64 V = 1;
			for (int32 Dim : D.GetShape().GetData())
			{
				V *= (Dim > 0) ? Dim : 1;
			}
			Volumes.Add(V);
		}
	}
	if (Volumes.Num() == 0)
	{
		OutError = TEXT("no output tensor descs");
		return false;
	}
	Outputs.SetNum(Volumes.Num());
	TArray<UE::NNE::FTensorBindingCPU> OutBind;
	for (int32 i = 0; i < Volumes.Num(); ++i)
	{
		Outputs[i].SetNumUninitialized(Volumes[i]);
		OutBind.Add({ Outputs[i].GetData(), Outputs[i].Num() * sizeof(float) });
	}

	if (Instance->RunSync(InBind, OutBind) != UE::NNE::EResultStatus::Ok)
	{
		OutError = TEXT("RunSync failed");
		return false;
	}
	return true;
}
