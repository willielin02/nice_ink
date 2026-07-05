#pragma once

#include "CoreMinimal.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "NiceInkSessionSubsystem.generated.h"

class FOnlineSessionSearch;

// 連線房間管理：建房（listen server）／搜房／加入。
// 走 Online Subsystem 抽象層——DefaultPlatformService=NULL 時是 LAN 房，
// 填好 EOS 憑證並切換後（見 Docs/EOS_SETUP.md）同一套程式碼變成網路房。
UCLASS()
class NICEINK_API UNiceInkSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// 建房並以 listen server 載入桑拿房
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Session")
	void HostSession(bool bLan = true);

	// 搜到的第一個房直接加入（朋友房夠用；房間瀏覽器是之後的事）
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Session")
	void JoinFirstFoundSession(bool bLan = true);

	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Session")
	void DestroySession();

private:
	TSharedPtr<FOnlineSessionSearch> SessionSearch;

	FDelegateHandle CreateHandle;
	FDelegateHandle FindHandle;
	FDelegateHandle JoinHandle;

	IOnlineSessionPtr GetSessionInterface() const;

	void OnCreateSessionComplete(FName SessionName, bool bWasSuccessful);
	void OnFindSessionsComplete(bool bWasSuccessful);
	void OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
};
