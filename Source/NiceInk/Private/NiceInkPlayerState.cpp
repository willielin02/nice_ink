#include "NiceInkPlayerState.h"

#include "Net/UnrealNetwork.h"

ANiceInkPlayerState::ANiceInkPlayerState()
{
	bReplicates = true;
	// 引擎預設 1Hz＝罰酒數/現金/座位變化最多慢一整秒（07-26 遲鈍根治）
	SetNetUpdateFrequency(10.0f);
}

void ANiceInkPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ANiceInkPlayerState, SeatIndex);
	DOREPLIFETIME(ANiceInkPlayerState, AvatarIndex);
	DOREPLIFETIME(ANiceInkPlayerState, bIsRoomHost);
	DOREPLIFETIME(ANiceInkPlayerState, PenaltyCups);
	DOREPLIFETIME(ANiceInkPlayerState, Cash);
}
