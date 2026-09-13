#include "NiceInkPlayerState.h"

#include "Net/UnrealNetwork.h"

ANiceInkPlayerState::ANiceInkPlayerState()
{
	bReplicates = true;
	// 引擎預設 1Hz＝罰酒數/現金/座位變化最多慢一整秒（07-26 遲鈍根治）
	SetNetUpdateFrequency(10.0f);
}

void ANiceInkPlayerState::CopyProperties(APlayerState* PlayerState)
{
	Super::CopyProperties(PlayerState);
	if (ANiceInkPlayerState* P = Cast<ANiceInkPlayerState>(PlayerState))
	{
		// 身分＝席位、名冊臉、房主、罰酒、現金——同一個人回來還是同一席、同一張臉。
		// 刻意**不**抄：bAssetsRestored（新 pawn 要重新還原刺青）、bPersonaVerified（重上行重驗）、
		// bFaceNone（重進的人會重新上傳 blob，觀看端要重新等；等不到再由 10s 保底重標）。
		P->SeatIndex = SeatIndex;
		P->AvatarIndex = AvatarIndex;
		P->DesiredAvatarIndex = DesiredAvatarIndex;
		P->bIsRoomHost = bIsRoomHost;
		P->PenaltyCups = PenaltyCups;
		P->Cash = Cash;
	}
}

void ANiceInkPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ANiceInkPlayerState, SeatIndex);
	DOREPLIFETIME(ANiceInkPlayerState, AvatarIndex);
	DOREPLIFETIME(ANiceInkPlayerState, bIsRoomHost);
	DOREPLIFETIME(ANiceInkPlayerState, bFaceNone);
	DOREPLIFETIME(ANiceInkPlayerState, PenaltyCups);
	DOREPLIFETIME(ANiceInkPlayerState, Cash);
}
