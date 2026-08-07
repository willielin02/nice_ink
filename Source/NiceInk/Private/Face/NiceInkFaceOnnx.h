// 內建臉管線（SPEC #52 定案①、SHIP_PLAN C1）：NNE ONNX 推理殼。
// 模型＝Content/FaceBakery/models/*.onnx（raw 檔 NonUFS 入包，非 uasset——
// UNNEModelData::Init 在 runtime 從 bytes 直建，GetModelData 現場叫 ORT 編譯）。
#pragma once

#include "CoreMinimal.h"
#include "NNERuntimeCPU.h"
#include "UObject/StrongObjectPtr.h"

class UNNEModelData;

/** 一顆 ONNX 模型的載入＋執行。UNNEModelData 的 NewObject 必須在 game thread
 *（CreateFromFile 內部處理跳線）；CreateModelCPU/RunSync 都在呼叫端執行緒跑。 */
class FNiFaceOnnxModel
{
public:
	/** 任意執行緒可呼叫：讀檔→（跳 game thread）建 UNNEModelData→本執行緒編譯 ORT session。
	 *  失敗回 nullptr、OutError 帶原因。 */
	static TUniquePtr<FNiFaceOnnxModel> CreateFromFile(const FString& OnnxPath, FString& OutError);

	/** 單輸入單/多輸出便利執行。InShape 例 {1,3,512,512}；Outputs 依模型輸出序，
	 *  由本函式配置大小。失敗回 false。 */
	bool Run(TConstArrayView<float> Input, TConstArrayView<uint32> InShape,
	         TArray<TArray<float>>& Outputs, FString& OutError);

	/** 多輸入版（LaMa：image+mask）。 */
	bool RunMulti(TConstArrayView<TConstArrayView<float>> Inputs,
	              TConstArrayView<TConstArrayView<uint32>> InShapes,
	              TArray<TArray<float>>& Outputs, FString& OutError);

	~FNiFaceOnnxModel();

private:
	FNiFaceOnnxModel() = default;

	TStrongObjectPtr<UNNEModelData> ModelData;
	TSharedPtr<UE::NNE::IModelCPU> Model;
	TSharedPtr<UE::NNE::IModelInstanceCPU> Instance;
};
