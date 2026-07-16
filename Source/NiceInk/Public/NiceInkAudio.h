#pragma once

#include "CoreMinimal.h"

// 遊戲內全部 one-shot 音效的名冊（資產在 /Game/Audio，python 合成的最小功能音組）。
// 刻意沒有任何「甦醒」音——無聲甦醒零提示是 SPEC 定案 #8。
enum class ENiSound : uint8
{
	UiClick,
	StrokeStart,
	BottleSpin,
	DrinkGulp,
	SpraySplat,
	TourChime,
	CarbonStamp,
	FinaleGong,
	AccuseCorrect,
	AccuseWrong,
	KickThud
};

// 極簡 2D one-shot 音效層：音量＝GameInstance MasterVolume（設定選單即時生效）。
// 閉眼沉睡者全域靜音（UiClick 除外）——感官規格：沉睡者只聽得到房間語音，
// 筆劃聲/命中聲都是情報，一律不給（SPEC「無命中回饋」）。
namespace NiAudio
{
	NICEINK_API void Play(const UObject* WorldContext, ENiSound Sound, float VolumeScale = 1.0f);
}
