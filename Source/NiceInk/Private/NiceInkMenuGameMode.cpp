#include "NiceInkMenuGameMode.h"

#include "Engine/World.h"
#include "NiceInkMenuHUD.h"
#include "NiceInkMenuPlayerController.h"
#include "NiceInkMenuStage.h"

ANiceInkMenuGameMode::ANiceInkMenuGameMode()
{
	HUDClass = ANiceInkMenuHUD::StaticClass();
	PlayerControllerClass = ANiceInkMenuPlayerController::StaticClass();
	DefaultPawnClass = nullptr;
	bStartPlayersAsSpectators = true;
}

void ANiceInkMenuGameMode::BeginPlay()
{
	Super::BeginPlay();

	// 選單舞台（全程式生成、零關卡資產）：毛玻璃卡片後面＝力士跟 BGM 跳舞
	if (UWorld* World = GetWorld())
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		World->SpawnActor<ANiceInkMenuStage>(ANiceInkMenuStage::StaticClass(),
			FVector::ZeroVector, FRotator::ZeroRotator, Params);
	}
}
