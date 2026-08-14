#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameSession.h"
#include "NiceInkGameSession.generated.h"

// 引擎 AGameMode 的 match 生命週期≠本遊戲的局：大廳/場間都活在「已開場」的
// 引擎 match 裡——預設 AGameSession 於開場即 StartSession→session 進 InProgress，
// 配上 bAllowJoinInProgress=false＝主機對 LAN 搜房查詢無聲拒答（2026-08-14
// verbose log 活體定罪：查詢抵達主機、主機零回應；IsSessionJoinable 靜默假）。
// 兩個 handler 改 no-op（引擎版只做 Start/EndSession＋遠端通知＋stat 擷取），
// session 狀態改由 GameMode 在真實開局/收局點驅動（SetSessionInProgress）。
UCLASS()
class NICEINK_API ANiceInkGameSession : public AGameSession
{
	GENERATED_BODY()

public:
	virtual void HandleMatchHasStarted() override {}
	virtual void HandleMatchHasEnded() override {}
};
