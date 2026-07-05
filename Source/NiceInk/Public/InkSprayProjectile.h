#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InkTypes.h"
#include "InkSprayProjectile.generated.h"

class UProjectileMovementComponent;
class USphereComponent;
class UStaticMeshComponent;

// 沉睡者的噴射物（噴嚏／尿／屎）。server 生成、全房可見的拋物線小球；
// 命中作畫者＝該回合致盲＋身上留噴漬（證據標記）。命中回饋不回傳給噴的人
//（SPEC：無即時命中回饋——結果醒來用眼睛驗收）。
UCLASS()
class NICEINK_API AInkSprayProjectile : public AActor
{
	GENERATED_BODY()

public:
	AInkSprayProjectile();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spray")
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spray")
	TObjectPtr<UStaticMeshComponent> Blob;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spray")
	TObjectPtr<UProjectileMovementComponent> Movement;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Spray")
	EInkEvidenceType SprayType = EInkEvidenceType::Sneeze;

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UFUNCTION()
	void OnBlobHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	void ApplyBlobColor();
};
