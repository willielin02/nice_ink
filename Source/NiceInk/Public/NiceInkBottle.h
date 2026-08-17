#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NiceInkBottle.generated.h"

class UStaticMeshComponent;

// 開場儀式的酒瓶（2026-08-16）。SPEC「道場中央轉一個酒瓶，轉到誰誰喝——純儀式，零規則」。
//
// **先抽後演**（user 定案）：伺服器先均勻抽人，再把「起點角/終點角/開轉時間戳」複製下去，
// 每端各自用同一組參數播減速曲線 ⇒ 每台機器算出的終角逐位相同、畫面與宣判永不矛盾。
// 位置與角度**不複製**（複製會帶 33ms 量化階梯與抖動）——全部是 GameState 參數的純函式。
//
// 資產（Tools/AssetPrep/build_bottle_fbx.py）：/Game/Props/SM_Bottle，橫躺、
// 原點＝旋轉樞軸（長軸質心、底面 z=0）、**+X＝瓶口方向** ⇒ 瓶口指向 = actor yaw。
UCLASS()
class NICEINK_API ANiceInkBottle : public AActor
{
	GENERATED_BODY()

public:
	ANiceInkBottle();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// 佔位材質基底（＝實體筆同款 BasicShapeMaterial；逐槽上 Color MID）
	UPROPERTY()
	TObjectPtr<class UMaterialInterface> PropMaterial;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bottle")
	TObjectPtr<UStaticMeshComponent> Mesh;

	// 瓶口相對樞軸的前伸量（cm；資產實測 14.1）——手要抓的是瓶頸不是樞軸
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bottle")
	float NeckOffsetCm = 11.0f;

	// 躺在地上時的靜止位置（伺服器生成時寫入；脫手落地後回歸此處）
	UPROPERTY(Replicated)
	FVector RestLocation = FVector::ZeroVector;

	// 被誰握著（Collapse 前由儀式指定；nullptr＝在地上）。複製＝他端也看得到瓶在手上
	UPROPERTY(Replicated)
	TObjectPtr<AActor> HeldBy = nullptr;

	// 脫手拋物線（Collapse 起手）：起點/起始時間；bFalling=false 時無效
	UPROPERTY(Replicated)
	FVector DropFromLocation = FVector::ZeroVector;

	UPROPERTY(Replicated)
	float DropStartServerTime = 0.0f;

	UPROPERTY(Replicated)
	bool bFalling = false;

	// 世界瓶口位置（手 IK 的目標點）
	UFUNCTION(BlueprintPure, Category = "Bottle")
	FVector GetNeckWorldLocation() const;

	// 儀式呼叫（server）：開始被握 / 脫手落地
	void ServerSetHeld(AActor* Holder);
	void ServerDrop();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	// 轉瓶：ease-out quintic（角速度單調遞減到 0＝「自然地、慢慢地停」）
	static float SpinEase(float T);
};
