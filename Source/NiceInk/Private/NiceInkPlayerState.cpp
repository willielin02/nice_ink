#include "NiceInkPlayerState.h"

#include "Net/UnrealNetwork.h"

ANiceInkPlayerState::ANiceInkPlayerState()
{
	bReplicates = true;
}

void ANiceInkPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ANiceInkPlayerState, SeatIndex);
	DOREPLIFETIME(ANiceInkPlayerState, AvatarIndex);
	DOREPLIFETIME(ANiceInkPlayerState, PenaltyCups);
	DOREPLIFETIME(ANiceInkPlayerState, Cash);
}
