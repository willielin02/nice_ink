#include "NiceInkAudio.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "NiceInkCharacter.h"
#include "NiceInkGameInstance.h"
#include "Sound/SoundBase.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	const TCHAR* SoundPath(ENiSound Sound)
	{
		switch (Sound)
		{
		case ENiSound::UiClick:       return TEXT("/Game/Audio/ui_click.ui_click");
		case ENiSound::StrokeStart:   return TEXT("/Game/Audio/stroke_start.stroke_start");
		case ENiSound::BottleSpin:    return TEXT("/Game/Audio/bottle_spin.bottle_spin");
		case ENiSound::DrinkGulp:     return TEXT("/Game/Audio/drink_gulp.drink_gulp");
		case ENiSound::SpraySplat:    return TEXT("/Game/Audio/spray_splat.spray_splat");
		case ENiSound::TourChime:     return TEXT("/Game/Audio/tour_chime.tour_chime");
		case ENiSound::CarbonStamp:   return TEXT("/Game/Audio/carbon_stamp.carbon_stamp");
		case ENiSound::FinaleGong:    return TEXT("/Game/Audio/finale_gong.finale_gong");
		case ENiSound::AccuseCorrect: return TEXT("/Game/Audio/accuse_correct.accuse_correct");
		case ENiSound::AccuseWrong:   return TEXT("/Game/Audio/accuse_wrong.accuse_wrong");
		default:                      return TEXT("/Game/Audio/kick_thud.kick_thud");
		}
	}

	USoundBase* GetSound(ENiSound Sound)
	{
		// TStrongObjectPtr 快取：LoadObject 一次、GC 不回收
		static TMap<ENiSound, TStrongObjectPtr<USoundBase>> Cache;
		if (const TStrongObjectPtr<USoundBase>* Found = Cache.Find(Sound))
		{
			return Found->Get();
		}
		USoundBase* Loaded = LoadObject<USoundBase>(nullptr, SoundPath(Sound));
		Cache.Add(Sound, TStrongObjectPtr<USoundBase>(Loaded));
		return Loaded;
	}
}

void NiAudio::Play(const UObject* WorldContext, ENiSound Sound, float VolumeScale)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	float Volume = VolumeScale;
	if (const UNiceInkGameInstance* GI = UNiceInkGameInstance::Get(WorldContext))
	{
		Volume *= FMath::Clamp(GI->MasterVolume, 0.0f, 1.0f);
	}
	if (Volume <= 0.005f)
	{
		return;
	}

	// 閉眼沉睡者：遊戲音全滅（情報遮蔽），只留 UI 點擊回饋
	if (Sound != ENiSound::UiClick)
	{
		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			const ANiceInkCharacter* Me = Cast<ANiceInkCharacter>(PC->GetPawn());
			if (Me && Me->bAsleep && !Me->bEyesOpen)
			{
				return;
			}
		}
	}

	if (USoundBase* Asset = GetSound(Sound))
	{
		UGameplayStatics::PlaySound2D(World, Asset, Volume);
	}
}
