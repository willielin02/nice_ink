#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SoulCameraActor.generated.h"

class UCameraComponent;

UCLASS()
class NICEINK_API ASoulCameraActor : public AActor
{
	GENERATED_BODY()

public:
	ASoulCameraActor();

	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Soul")
	TObjectPtr<UCameraComponent> CameraComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soul")
	FVector ViewOffset = FVector(-450.0f, 0.0f, 320.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soul", meta = (ClampMin = "1.0", ClampMax = "30.0"))
	float FollowSpeed = 8.0f;

	UFUNCTION(BlueprintCallable, Category = "Soul")
	void ActivateSoulView(APlayerController* PlayerController, AActor* VictimActor);

	UFUNCTION(BlueprintCallable, Category = "Soul")
	void ClearSoulView(APlayerController* PlayerController);

private:
	UPROPERTY()
	TObjectPtr<AActor> FollowTarget;
};
