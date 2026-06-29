#include "NiceInkPlayerState.h"

#include "Net/UnrealNetwork.h"

ANiceInkPlayerState::ANiceInkPlayerState()
{
	bReplicates = true;
}

void ANiceInkPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ANiceInkPlayerState, Appearance);
	DOREPLIFETIME(ANiceInkPlayerState, CorrectGuesses);
	DOREPLIFETIME(ANiceInkPlayerState, TimesTattooed);
	DOREPLIFETIME(ANiceInkPlayerState, TimesArtist);
}

void ANiceInkPlayerState::SetAppearance(const FCharacterAppearance& NewAppearance)
{
	Appearance = NewAppearance;
	OnRep_Appearance();
}

void ANiceInkPlayerState::AddCorrectGuess()
{
	++CorrectGuesses;
}

void ANiceInkPlayerState::AddTattooed()
{
	++TimesTattooed;
}

void ANiceInkPlayerState::AddArtistRound()
{
	++TimesArtist;
}

void ANiceInkPlayerState::OnRep_Appearance()
{
}
