#include "NiceInkMenuGameMode.h"

#include "NiceInkMenuHUD.h"
#include "NiceInkMenuPlayerController.h"

ANiceInkMenuGameMode::ANiceInkMenuGameMode()
{
	HUDClass = ANiceInkMenuHUD::StaticClass();
	PlayerControllerClass = ANiceInkMenuPlayerController::StaticClass();
	DefaultPawnClass = nullptr;
	bStartPlayersAsSpectators = true;
}
