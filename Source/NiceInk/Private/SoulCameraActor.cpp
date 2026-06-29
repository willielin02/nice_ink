#include "SoulCameraActor.h"

#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"

ASoulCameraActor::ASoulCameraActor()
{
	PrimaryActorTick.bCanEverTick = true;
	CameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("SoulCamera"));
	SetRootComponent(CameraComponent);
	CameraComponent->SetFieldOfView(75.0f);
}

void ASoulCameraActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!IsValid(FollowTarget))
	{
		return;
	}

	const FVector DesiredLocation = FollowTarget->GetActorLocation() + ViewOffset;
	const FVector NewLocation = FMath::VInterpTo(GetActorLocation(), DesiredLocation, DeltaSeconds, FollowSpeed);
	SetActorLocation(NewLocation);

	const FRotator LookAt = (FollowTarget->GetActorLocation() - NewLocation).Rotation();
	SetActorRotation(FMath::RInterpTo(GetActorRotation(), LookAt, DeltaSeconds, FollowSpeed));
}

void ASoulCameraActor::ActivateSoulView(APlayerController* PlayerController, AActor* VictimActor)
{
	FollowTarget = VictimActor;
	if (PlayerController)
	{
		PlayerController->SetViewTargetWithBlend(this, 0.35f);
	}
}

void ASoulCameraActor::ClearSoulView(APlayerController* PlayerController)
{
	if (PlayerController && FollowTarget)
	{
		PlayerController->SetViewTargetWithBlend(FollowTarget, 0.35f);
	}
	FollowTarget = nullptr;
}
