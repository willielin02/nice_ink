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
		// 跟隨已配置的服務：NULL=LAN 房、EOS=網路房（與主選單 bUseLan 同一條規則）
		Sessions->HostSession(/*bLan=*/!UNiceInkSessionSubsystem::IsOnlineServiceConfigured());
	}
}

void ANiceInkMenuPlayerController::NiMenuJoin()
{
	if (UNiceInkSessionSubsystem* Sessions = GetGameInstance() ? GetGameInstance()->GetSubsystem<UNiceInkSessionSubsystem>() : nullptr)
	{
		Sessions->JoinFirstFoundSession(/*bLan=*/!UNiceInkSessionSubsystem::IsOnlineServiceConfigured());
	}
}
