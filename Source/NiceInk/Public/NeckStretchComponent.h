// 轆轤首伸縮脖（2026-07-16 user 定案；分界線 2026-07-17 改 cut_seam_head3）：頭與身體沿手標切縫真切開後，
// 兩個邊界環之間的銜接曲面「每一幀重新解出來」——它沒有材料身分、沒有上一幀的自己，
// 素色皮膚之下「扭曲」是無意義概念。頭殼/身殼（含臉、刺青、筆跡）永遠剛體。
//
// 每幀解法（φ 與骨骼姿勢的純函數＝跨端決定性，零複製欄位）：
//   1. 邊界環 mini-skin（NeckSeamData 烘入的 rest 位置＋權重 × 當前骨骼）＝兩條真實邊界曲線
//   2. Hermite 中線（端切向＝邊界環 Newell 法線方向、幅度∝弦長）
//   3. 沿線 rotation-minimizing frames（double reflection——由當前曲線形狀決定、無歷史）
//   4. 頭側環按 RMF 角座標重取樣（零剪切的對應——「扭轉」由此在構造上不存在）
//   5. 環形狀＝身側輪廓→頭側輪廓混合 × taper（中段 0.88，user 定值）
//   6. 端排頂點＝邊界環原點原樣（視覺水密）；端排法線＝縫區移植法線（光影無縫）
//   7. 端排頂點色＝圖集實際端色（後頸髮際無色階）、中段淡入中性膚
// 收合（頭安座、弦長 < 門檻）＝整件隱藏——作畫階段它物理上不存在（非畫布=結構必然）。
#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "NeckStretchComponent.generated.h"

class UPoseableMeshComponent;
class UMaterialInstanceDynamic;

UCLASS(ClassGroup = (NiceInk), meta = (BlueprintSpawnableComponent))
class UNeckStretchComponent : public UProceduralMeshComponent
{
	GENERATED_BODY()

public:
	UNeckStretchComponent(const FObjectInitializer& ObjectInitializer);

	// 綁定資料源（BowBody 睡姿替身/彎腰共用的 poseable）＋抄身體 MID 的膚色參數
	void InitFromSource(UPoseableMeshComponent* InSource, UMaterialInterface* BodyMaterial);

	// 每 tick 由角色在「所有擺骨完成後」呼叫（順序顯式＝不吃 tick 順序運氣）
	void UpdateNeck();

	// 中段整體收細（user 2026-07-16 改定 1.0＝不內縮；兩端恆 1.0 貼合邊界環。
	// 深彎自適應收細已停用——v8 環間淨空抬升表把弧長拉夠了，傾印量測回驗過）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Neck", meta = (ClampMin = "0.5", ClampMax = "1.0"))
	float NeckTaperMid = 1.0f;

	// 環向形狀曲線（user 2026-07-16 定值）：後頸側外凸、下巴側內收、兩側 1.0，
	// 沿環向平滑過渡；兩端排恆 1.0、中段以 sin² 包絡全額生效。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Neck", meta = (ClampMin = "1.0", ClampMax = "1.6"))
	float NeckNapeBulge = 1.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Neck", meta = (ClampMin = "0.6", ClampMax = "1.0"))
	float NeckChinTuck = 0.88f;

	// Hermite 端切向幅度（×弦長）。深彎需要弧長 ≳ 彎角×管半徑（摺疊判據），
	// 0.75=弧長 ~34cm（100° 彎的成立條件之一；行框架已改 slerp 均攤＝過衝不再繞到臉前，
	// 且本人 OwnerNoSee）。viewport 口味域
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Neck", meta = (ClampMin = "0.2", ClampMax = "1.5"))
	float NeckTangentK = 0.75f;

	// 弦長低於此值＝頭安座＝整件隱藏（rest 收合）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Neck", meta = (ClampMin = "0.1", ClampMax = "10"))
	float NeckHideChordCm = 0.15f; // 08-15 1.5→0.15：站立俯仰 ±5~9° 時 chord 0.3~0.5＋楔縫已張卻被判安座＝穿膜（probe 掃描實錘）；真安座 chord 逐位=0.0

	// 沿長度的環數（含兩端排）。20＝深彎每行 ~5° 環面轉角（曲率取樣，成本可忽略）
	UPROPERTY(EditAnywhere, Category = "Nice Ink|Neck", meta = (ClampMin = "6", ClampMax = "32"))
	int32 NeckRows = 20;

	// robo 探針：管幾何現場數字（chord/環中心世界座標/bounds）
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Neck")
	FString GetDebugSummary() const;
	void ForceRebuild(); // 清跳過快取＝下一 UpdateNeck 必重建（actor 現身邊緣用）
	// 身體 MID 晚綁（client 上 InitFromSource 時 MID 常未建）：角色每 tick 指標比對推入
	void SetBodyMaterialRef(UMaterialInstanceDynamic* BodyMid);
	bool bNeckStretchEnabled = true; // 08-15 雙版制：縫合版網格上停用（脖子=蒙皮本體）
	// 膚色同步：身體 MID 參照＋上次同步的 SkinTone（變了整組 uniform 重抄）
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BodyMidRef;
	FLinearColor LastSyncedTone = FLinearColor::Black;

	// robo 探針：傾印當前管幾何（CSV：row,col,pos,normal）——平滑度調查用
	// （非 const：引擎 GetProcMeshSection 只有非 const 版）
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Neck")
	bool DumpNeckMesh(const FString& Path);

private:
	UPROPERTY()
	TObjectPtr<UPoseableMeshComponent> Source;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> NeckMid;

	// 骨骼 mini-skin 快取（ref pose CS 反矩陣；資產常數）
	TArray<FName> SkinBoneNames;
	TArray<FTransform> RefInvCS;
	bool bReady = false;

	// 端色（FColor 編碼：rgb=linear/2、a=hairW）
	TArray<FColor> BodyRingColor;
	TArray<FColor> HeadRingColor;

	// 頭環 rest 平面內的臉方向（環向形狀曲線的座標基準——user 定案：形狀以「頭部」
	// 為錨，凸起/收口跟著頭轉；每列係數每幀由頭端零扭轉對應方向對此取點積）
	FVector HeadFaceInPlane = FVector::YAxisVector;

	// 頭側環 rest 參數化（在環自身平面上＝星形保證角度單調；運行時重取樣只做
	// 「RMF 端框方向→頭平面角度」投影映射——傾斜端框直接取角會摺疊，robo 實錘摺痕）
	FVector HeadC0 = FVector::ZeroVector;   // rest 環心
	FVector HeadN0 = FVector::ZeroVector;   // rest 環平面法線
	FVector HeadE1 = FVector::ZeroVector;   // 平面內正交基
	FVector HeadE2 = FVector::ZeroVector;
	float HeadRingRadius = 17.8f;           // rest 環平均半徑（隱藏判定的縫寬估計）
	TArray<float> HeadAng0;                  // 每頂點 rest 角（沿環序）
	bool bHeadAngAscending = true;
	int32 HeadBoneSlot = 0;                  // SkinBoneNames 中 Head 的索引

	// 變化偵測（姿勢沒動不重算/不重傳 GPU）
	FVector LastHeadCenter = FVector(FLT_MAX);
	FQuat LastHeadQ = FQuat::Identity;
	bool bSectionCreated = false;

	// 探針快取（UpdateNeck 每次重算時寫入）
	FVector DbgBc = FVector::ZeroVector;
	FVector DbgHc = FVector::ZeroVector;
	float DbgChord = 0.0f;
};
