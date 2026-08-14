// 內建臉管線（SPEC #52 定案①、SHIP_PLAN C1 M3~M5）：selfie_to_face_texture.py v7
// ＋intake_selfie.py 的 C++ 移植。python 版＝行為規格與對照組（venv 保留）；
// 每一步的常數/順序/演算法與 python 逐行對齊，方便 diff 對賬。
//
// 涵蓋範圍（與 python 現行組態一致）：BEARD_KEEP_IN_TEXTURE=True（鬍鬚留貼圖、
// LaMa 鬍區重生分支＝死路不移植）、DIRECT_MODE=False、擴張島模式（legacy 單
// mask 模式不移植）、FLATTEN_RAMP_PX=0（v5 flatten＝停用不移植）、seam QA 儀器
// 不移植（診斷性輸出，poisson 段的 edge dE 統計保留進 log）。
// 顆粒噪聲（fill grain/眼閉 regrain）＝自帶 PCG32 高斯：與 numpy 序列不同、
// 統計等價（std 相同）——對賬時此層以統計比對。
#pragma once

#include "CoreMinimal.h"

/** 一次自拍 intake 的完整執行（背景執行緒呼叫）。
 *  產物寫進 OutDir：face_open.png / face_closed.png / eye_mask_ink.png /
 *  skin_color.json / thumb.png / intake_log.txt——與 intake_selfie.py 同一合約。 */
class FNiFaceBakery
{
public:
	struct FOptions
	{
		/** 額外 dump 中間產物（landmarks.json/parsing.png/head_mask.png/
		 *  selfie_filled.png/warped_raw.png）到 OutDir——對賬儀器用。 */
		bool bDumpIntermediates = false;
		/** 進度回報（任意執行緒呼叫；帶目前步驟名）。 */
		TFunction<void(const FString&)> Progress;
	};

	/** 模型/資料根目錄有效性（決定 Persona 走內建或 venv 對照組）。 */
	static bool IsAvailable();

	/** 背景預熱模型快取（選單待機時呼叫，背景執行緒）：LaMa 的 ORT session
	 *  建立 ~60s＝首次上傳的冷啟大頭——先建好＝首上傳直接走暖路。與
	 *  RunIntake 同鎖同快取：上傳撞上預熱只會等鎖、永不重載。 */
	static bool WarmupModels(FString& OutError);

	/** 跑完整 intake。回 false 時 OutError 帶原因（同 python 的 stderr 語義），
	 *  intake_log.txt 一樣會寫（驗屍管道）。 */
	static bool RunIntake(const FString& SelfiePath, const FString& OutDir,
	                      FString& OutError, const FOptions& Options = FOptions());
};
