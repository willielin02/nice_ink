#include "NiceInkMenuPlayerController.h"

#include "NiceInkSessionSubsystem.h"

ANiceInkMenuPlayerController::ANiceInkMenuPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = false;
	bEnableMouseOverEvents = false;
}

void ANiceInkMenuPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// 游標常駐可見：按住左鍵拖曳時不得沒收游標（沒有 UMG，Slate 不替我們管）
	FInputModeGameAndUI Mode;
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Mode.SetHideCursorDuringCapture(false);
	SetInputMode(Mode);
}

void ANiceInkMenuPlayerController::NiMenuHost()
{
	if (UNiceInkSessionSubsystem* Sessions = GetGameInstance() ? GetGameInstance()->GetSubsystem<UNiceInkSessionSubsystem>() : nullptr)
	{
		Sessions->HostSession(/*bLan=*/true);
	}
}

void ANiceInkMenuPlayerController::NiMenuJoin()
{
	if (UNiceInkSessionSubsystem* Sessions = GetGameInstance() ? GetGameInstance()->GetSubsystem<UNiceInkSessionSubsystem>() : nullptr)
	{
		Sessions->JoinFirstFoundSession(/*bLan=*/true);
	}
}
