#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NiceInkPortraitBooth.generated.h"

class ANiceInkCharacter;
class UBoxComponent;
class UPointLightComponent;
class USceneCaptureComponent2D;
class UTexture2D;
class UTextureRenderTarget2D;

// 頭像亭（2026-08-12）：把「角色的頭本人」烘成正面肖像 icon——含丁髷、
// 膚色、臉貼圖，頭滿框、黑底（與全遊戲黑舞台同語言）。取代臉貼圖 UV 裁切
//（膚色豆腐）。用途：局內 DrawFaceTok 身分辨識＋個人檔案臉庫縮圖。
// 實作＝隱形攝影棚：藏在世界下方；替身與亭燈全走光照通道 2（世界光不進、
// 亭光不出）＝任何關卡烘出來都一致；每張肖像＝獨立小 RT 常駐快取（快取鍵
// ＝呼叫端組的字串：lib_<id>／seat<N>_v<rev>／roster<idx>）。
UCLASS()
class NICEINK_API ANiceInkPortraitBooth : public AActor
{
	GENERATED_BODY()

public:
	ANiceInkPortraitBooth();

	virtual void Tick(float DeltaSeconds) override;

	// 找或生（每世界一座）
	static ANiceInkPortraitBooth* Get(const UObject* Ctx);

	// 以臉工件烘肖像（快取鍵＝Key；未就緒/失敗＝nullptr，呼叫端保留舊路墊檔）。
	// Closed/Mask 可傳 nullptr（肖像睜眼、無墨＝兩者不影響畫面，內部以 Open 補位）
	UTexture* GetPortraitKeyed(const FString& Key, UTexture2D* Open, UTexture2D* Closed,
		UTexture2D* Mask, const FLinearColor& Tone);

	// 名冊臉肖像（快取鍵＝roster<idx>；貼圖自名冊路徑載入）
	UTexture* GetPortraitRoster(int32 AvatarIdx);

	// --- 取景旋鈕（2026-08-12 user 定案：正交＋頭部精準裁切＋透明背景
	// ＝icon 就是頭的形狀，不是方形照片）---
	UPROPERTY(EditAnywhere, Category = "Nice Ink|Portrait")
	float AimZCm = 64.0f;         // 瞄準點＝actor 中心上方（頭的高度）

	UPROPERTY(EditAnywhere, Category = "Nice Ink|Portrait")
	float OrthoWidthCm = 46.0f;   // 正交取景寬≈頭寬＋餘裕（肩膀擠出框＝裁切恰好頭部）

	UPROPERTY(EditAnywhere, Category = "Nice Ink|Portrait")
	float KeyIntensity = 7.0f;    // 亭燈（量測定值：12 時臉 29.7% 撞頂 255）

	UPROPERTY(EditAnywhere, Category = "Nice Ink|Portrait")
	float FillIntensity = 5.0f;   // 下前補光（下巴底朝下面兩頭不著光＝黑影帶）

	UPROPERTY(EditAnywhere, Category = "Nice Ink|Portrait")
	float RimIntensity = 20.0f;   // 背光（輪廓分離）

	UPROPERTY(EditAnywhere, Category = "Nice Ink|Portrait")
	float ColorGain = 0.62f;      // HDR→sRGB 的手動增益（量測迭代收斂：0.7 仍 6.6% 撞頂）

	// 深度遮罩上下分域（單一閾值血價：16=髷被砍、26=肩楔活著——髷與肩的
	// 深度重疊，但高度不重疊：肩不可能在頭頂）：上域寬鬆保髷、下域緊殺肩
	UPROPERTY(EditAnywhere, Category = "Nice Ink|Portrait")
	float DepthKeepUpperCm = 34.0f;

	UPROPERTY(EditAnywhere, Category = "Nice Ink|Portrait")
	float DepthKeepLowerCm = 14.0f;

	UPROPERTY(EditAnywhere, Category = "Nice Ink|Portrait")
	float DepthSplitFrac = 0.58f; // 分域線＝頭直跨的百分比（上=髷域、下=肩域）

	UPROPERTY(EditAnywhere, Category = "Nice Ink|Portrait")
	int32 PortraitSize = 256;

private:
	UPROPERTY() TObjectPtr<UBoxComponent> Floor;                  // 替身落腳（世界下方無地板）
	UPROPERTY() TObjectPtr<ANiceInkCharacter> Dummy;
	UPROPERTY() TObjectPtr<UPointLightComponent> KeyLight;
	UPROPERTY() TObjectPtr<UPointLightComponent> FillLight;
	UPROPERTY() TObjectPtr<UPointLightComponent> RimLight;
	UPROPERTY() TObjectPtr<USceneCaptureComponent2D> Capture;
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> ScratchRT; // 捕捉暫存（成品=裁切後 UTexture2D）
	UPROPERTY() TMap<FString, TObjectPtr<UTexture2D>> Cache;
	// 名冊貼圖載入快取（LoadObject 結果保 GC；名冊只有六人＝小）
	UPROPERTY() TMap<int32, TObjectPtr<UTexture2D>> RosterOpen;

	int32 TicksAlive = 0; // 姿勢系統跑穩前不出片（前幾拍＝參考姿勢）

	void EnsureDummy();
	UTexture2D* CaptureNow(); // 正交捕捉→CPU alpha 裁切→透明背景頭形貼圖
};
