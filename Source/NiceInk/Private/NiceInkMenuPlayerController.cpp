#include "NiceInkMenuPlayerController.h"

#include "NiceInkGameInstance.h"
#include "NiceInkMenuHUD.h"
#include "NiceInkSessionSubsystem.h"
#include "TimerManager.h"

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

// --- 房間碼自動化（timer-deferred：-ExecCmds 在首幀觸發，讓子系統/HUD 完全就緒）---

void ANiceInkMenuPlayerController::NiMenuAutoHost(int32 bPublic)
{
	FTimerHandle Unused;
	GetWorldTimerManager().SetTimer(Unused, FTimerDelegate::CreateWeakLambda(this, [this, bPublic]()
	{
		if (UNiceInkSessionSubsystem* Sessions = GetGameInstance() ? GetGameInstance()->GetSubsystem<UNiceInkSessionSubsystem>() : nullptr)
		{
			Sessions->HostSession(/*bLan=*/!UNiceInkSessionSubsystem::IsOnlineServiceConfigured(), bPublic != 0);
		}
	}), 1.0f, false);
}

void ANiceInkMenuPlayerController::NiMenuShowJoin(const FString& Code)
{
	FTimerHandle Unused;
	GetWorldTimerManager().SetTimer(Unused, FTimerDelegate::CreateWeakLambda(this, [this, Code]()
	{
		if (ANiceInkMenuHUD* MenuHud = Cast<ANiceInkMenuHUD>(GetHUD()))
		{
			MenuHud->RoboOpenJoinPage(Code);
		}
	}), 1.0f, false);
}

void ANiceInkMenuPlayerController::NiMenuFontSample()
{
	FTimerHandle Unused;
	GetWorldTimerManager().SetTimer(Unused, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (ANiceInkMenuHUD* MenuHud = Cast<ANiceInkMenuHUD>(GetHUD()))
		{
			// 墨 | 相撲の墨 | 繁體古風 | 简体宋体 | 한국어 | Русский | العربية | İstanbul ğş
			//（\u 逃逸＝不依賴原始檔編碼；en 文化下繁簡走 Zen/fallback、zh 文化下走各自面）
			const FString Sample =
				TEXT("Ink 墨 | 相撲の墨 | 繁體古風 | ")
				TEXT("简体宋体 | 한국어 | ")
				TEXT("Русский | ")
				TEXT("العربية | İstanbul ğş");
			MenuHud->RoboShowFontSample(Sample);
		}
	}), 1.0f, false);
}

void ANiceInkMenuPlayerController::NiMenuLang(int32 LangIndex)
{
	FTimerHandle Unused;
	GetWorldTimerManager().SetTimer(Unused, FTimerDelegate::CreateWeakLambda(this, [this, LangIndex]()
	{
		if (UNiceInkGameInstance* GI = Cast<UNiceInkGameInstance>(GetGameInstance()))
		{
			GI->ApplyLanguage(LangIndex);
		}
		if (ANiceInkMenuHUD* MenuHud = Cast<ANiceInkMenuHUD>(GetHUD()))
		{
			MenuHud->RecreateMenu(/*bOpenSettings=*/false);
		}
	}), 1.0f, false);
}

void ANiceInkMenuPlayerController::NiMenuShowLang()
{
	FTimerHandle Unused;
	GetWorldTimerManager().SetTimer(Unused, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (ANiceInkMenuHUD* MenuHud = Cast<ANiceInkMenuHUD>(GetHUD()))
		{
			MenuHud->RoboOpenLanguagePage();
		}
	}), 1.0f, false);
}

void ANiceInkMenuPlayerController::NiMenuJoinCode(const FString& Code)
{
	FTimerHandle Unused;
	GetWorldTimerManager().SetTimer(Unused, FTimerDelegate::CreateWeakLambda(this, [this, Code]()
	{
		if (ANiceInkMenuHUD* MenuHud = Cast<ANiceInkMenuHUD>(GetHUD()))
		{
			MenuHud->RoboOpenJoinPage(Code); // 畫面同步走加入頁（截圖可讀）
		}
		if (UNiceInkSessionSubsystem* Sessions = GetGameInstance() ? GetGameInstance()->GetSubsystem<UNiceInkSessionSubsystem>() : nullptr)
		{
			Sessions->JoinRoomByCode(Code, /*bLan=*/!UNiceInkSessionSubsystem::IsOnlineServiceConfigured());
		}
	}), 1.0f, false);
}
