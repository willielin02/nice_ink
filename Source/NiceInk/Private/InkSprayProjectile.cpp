#include "InkSprayProjectile.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "InkBodyComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "NiceInkCharacter.h"
#include "UObject/ConstructorHelpers.h"

AInkSprayProjectile::AInkSprayProjectile()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);
	InitialLifeSpan = 4.0f;

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(7.0f);
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Collision->SetCollisionObjectType(ECC_PhysicsBody);
	Collision->SetCollisionResponseToAllChannels(ECR_Block);
	Collision->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Collision->SetNotifyRigidBodyCollision(true);
	SetRootComponent(Collision);

	Blob = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Blob"));
	Blob->SetupAttachment(Collision);
	Blob->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Blob->SetRelativeScale3D(FVector(0.13f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereAsset(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereAsset.Succeeded())
	{
		Blob->SetStaticMesh(SphereAsset.Object);
	}

	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->InitialSpeed = 750.0f;
	Movement->MaxSpeed = 750.0f;
	Movement->ProjectileGravityScale = 0.55f;
	Movement->bRotationFollowsVelocity = true;
	Movement->bShouldBounce = false;
}

void AInkSprayProjectile::BeginPlay()
{
	Super::BeginPlay();

	ApplyBlobColor();

	// 不打噴自己的人（噴射從身體表面出發，否則出膛即自爆）
	if (APawn* Shooter = GetInstigator())
	{
		Collision->IgnoreActorWhenMoving(Shooter, true);
	}

	if (HasAuthority())
	{
		Collision->OnComponentHit.AddDynamic(this, &AInkSprayProjectile::OnBlobHit);
	}
}

void AInkSprayProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AInkSprayProjectile, SprayType);
}

void AInkSprayProjectile::ApplyBlobColor()
{
	FLinearColor Color;
	switch (SprayType)
	{
	case EInkEvidenceType::Sneeze: Color = FLinearColor(0.55f, 0.68f, 0.35f); break;
	case EInkEvidenceType::Piss:   Color = FLinearColor(0.85f, 0.72f, 0.12f); break;
	default:                       Color = FLinearColor(0.27f, 0.15f, 0.05f); break;
	}
	if (Blob)
	{
		if (UMaterialInstanceDynamic* MID = Blob->CreateAndSetMaterialInstanceDynamic(0))
		{
			MID->SetVectorParameterValue(TEXT("Color"), Color);
		}
	}
}

void AInkSprayProjectile::OnBlobHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (ANiceInkCharacter* Target = Cast<ANiceInkCharacter>(OtherActor))
	{
		if (Target != GetInstigator())
		{
			// 命中作畫者：噴漬標記＋該回合致盲。
			// 站姿＝精準解析；彎腰（poseable，姿勢偏離站姿）＝骨頭錨定粗落點。
			FVector2D UV;
			bool bResolved = !Target->bLeanLocked && Target->Body && Target->Body->ResolveBodyUV(Hit.ImpactPoint, UV, 45.0f);
			if (!bResolved)
			{
				bResolved = Target->GetEvidenceUVForHit(Hit.BoneName, Hit.ImpactPoint, UV);
			}
			if (bResolved)
			{
				Target->MulticastAddEvidence(SprayType, UV, FMath::Rand());
			}
			// 被噴不強制起身——半盲續畫走鐘自己的畫（SPEC v3.1 定案 #20）
			Target->ServerApplyBlind(SprayType);
		}
	}
	Destroy();
}
