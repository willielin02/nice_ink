// 昭和映像管電視（2026-08-27 開場動畫道具）。
// 全程式生成：櫃體＝SM_TvRadiola、螢幕內容＝逐幀畫（零影片資產、零授權風險）。
//
// **兩個解析度**（2026-08-28 user 定案「低解析度、像是真的舊電視的畫面」）：
//   ① 影像＝128×96 的 CPU 影格緩衝（NiceInkTvBroadcast）→ TF_Nearest 貼圖 →
//      單一 DrawTile 放大 4× 進 RT ⇒ **硬邊方塊像素**。直接畫在 512 上再假裝低解析
//      永遠會露餡（線條會是平滑的）。
//   ② 掃描線／暗角／關機白線畫在 512 這一層＝比影像細一階，兩層疊起來才是映像管。
// 節目內容＝日本傳統祭典特輯（夜祭→太鼓→神輿→極道の主役登場→振り返り），
// 敘事作用＝力士羨慕他們的刺青，覺得刺上一樣的就能一樣帥。
// 視覺＝(step, t) 的純函式（各端自算、無複製欄位）；開場後留在場上＝道場家具。
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NiceInkTvSet.generated.h"

UCLASS()
class NICEINK_API ANiceInkTvSet : public AActor
{
	GENERATED_BODY()

public:
	ANiceInkTvSet();
	virtual void Tick(float DeltaSeconds) override;

	// 儀器（2026-08-28）：把 5 分鏡 × 6 時點的影格排成一張 contact sheet 存 PNG。
	// **不需要 PIE**（RenderFrame 是純 CPU 函式）⇒ 一次性 headless session 就能自查，
	// 低解析度美術的迭代閘門靠它，不然每改一格都要跑一輪 PIE。
	//   UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript=Tools/RoboTest/robo_tv_filmsheet.py
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Tv")
	static void DumpFilmContactSheet(const FString& OutPath, int32 UpScale = 2);

	// 儀器其二（2026-08-28）：**逐格傾印**——每個分鏡 PerShot 張，各自存成 PNG。
	// contact sheet 是拿來一眼掃全片的；這支是拿來趴在上面看單格的（預設 6× 最近鄰放大
	// ＝每個影格像素一塊乾淨的 6×6，數得出來）。
	// 秒數用**真實鏡長**（IntroNotice 的比率表 × NoticeSeconds），所以傾印出來的
	// 就是實際會播的那一格，不是另一套時間軸的近似。
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Tv")
	static void DumpFilmFrames(const FString& OutDir, int32 UpScale = 6, int32 PerShot = 9,
		float NoticeSeconds = 10.4f);

protected:
	virtual void BeginPlay() override;

	// 2026-08-28 換裝：方塊拼裝退役，櫃體＝SM_TvRadiola
	//（"Radiola from Matrix" by Sirenko，CC-BY-4.0，信用名單必列；
	// SourceAssets/Television_Sirenko/ATTRIBUTION.txt）。
	// RT 節目畫在模型自己的玻璃槽（'TvGlass'，圓角內凹 52.5×44.7cm）——
	// 浮貼平板退役（user 抓實：平板把凸框整片蓋成螢幕）。
	UPROPERTY(VisibleAnywhere, Category = "Nice Ink|Tv")
	TObjectPtr<class UStaticMeshComponent> CabinetMesh;

	// 螢幕 RT（512×384＝4:3 映像管）；UPROPERTY＝GC 錨（Slate/MID 不保 GC 鐵坑同族）
	UPROPERTY()
	TObjectPtr<class UCanvasRenderTarget2D> ScreenRT;

	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> ScreenMid;

	// 低解析度影格（128×96、TF_Nearest）＝「舊電視」的承重載體
	UPROPERTY()
	TObjectPtr<class UTexture2D> FilmTex;

	UFUNCTION()
	void DrawScreen(class UCanvas* Canvas, int32 Width, int32 Height);

private:
	// 這一幀的螢幕狀態（Tick 從 GameState 導出、DrawScreen 消費）
	float ScreenOn01 = 1.0f;      // 0=黑；1=播放中
	float Collapse01 = 0.0f;      // 關機白線動畫進度
	int32 FilmShot = 0;           // 節目分鏡（NiceInkTvFilm::EShot）
	float FilmU = 0.0f;           // 鏡內進度（戲劇進度，隨鏡長伸縮）
	float FilmSeconds = 0.0f;     // 鏡頭開始起算的實秒（物理速度，不隨鏡長變）
	int32 FilmFrameNo = 0;        // 12Hz 影格序號（噪點/抖動種子）
	double LastDrawTime = -1.0;   // 12Hz 節流（映像管更新率，也省 GPU）

	TArray<FColor> FilmBuf;       // CPU 影格緩衝（FilmW×FilmH）

	void EnsureFilmTexture();
	void UploadFilmFrame();       // 畫一格 → 上傳到 FilmTex（GPU 端本幀之後即可取樣）
	void DrawFilmTile(class UCanvas* Canvas, float X, float Y, float W, float H, float Bright) const;
	void DrawScanlines(class UCanvas* Canvas, int32 W, int32 H) const;
	void DrawGlassBase(class UCanvas* Canvas, int32 W, int32 H) const;
	void DrawTubeVignette(class UCanvas* Canvas, int32 W, int32 H) const;
};
