#include "InkBodyActor.h"

#include "Engine/StaticMesh.h"
#include "InkBodyComponent.h"
#include "InkCanvasComponent.h"
#include "UObject/ConstructorHelpers.h"

AInkBodyActor::AInkBodyActor()
{
	PrimaryActorTick.bCanEverTick = false;

	BodyMesh = CreateDefaultSubobject<UInkBodyComponent>(TEXT("BodyMesh"));
	SetRootComponent(BodyMesh);

	// 預設給一顆球，確保 actor 放進關卡就能畫；正式身體網格在編輯器指定。
	// 注意：不設縮放——身體網格以真實尺寸（174cm）匯入
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereAsset(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereAsset.Succeeded())
	{
		BodyMesh->SetStaticMesh(SphereAsset.Object);
	}

	InkCanvas = CreateDefaultSubobject<UInkCanvasComponent>(TEXT("InkCanvas"));
}

void AInkBodyActor::BeginPlay()
{
	Super::BeginPlay();

	if (BodyMesh)
	{
		BodyMesh->BodyMaterial = BodyMaterial;
		BodyMesh->FaceOpenTexture = FaceTexture;
		BodyMesh->EyeMaskTexture = EyeMaskTexture;
		BodyMesh->SkinTone = SkinTone;
		BodyMesh->BindCanvas(InkCanvas);
	}
}

bool AInkBodyActor::ResolveBodyUV(const FVector& WorldPosition, FVector2D& OutUV)
{
	return BodyMesh ? BodyMesh->ResolveBodyUV(WorldPosition, OutUV) : false;
}
