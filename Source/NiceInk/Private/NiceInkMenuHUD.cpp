#include "NiceInkMenuHUD.h"

#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/GameViewportClient.h"
#include "NiceInkGameInstance.h"
#include "NiceInkHUD.h"
#include "NiceInkMenuWidget.h"

UFont* ANiceInkMenuHUD::BuildMenuFont()
{
	// 六文字系統矩陣＝與局內 HUD 共用同一座（2026-08-07 抽共用；本體在
	// ANiceInkHUD::BuildCompositeUiFont——名字回歸局內後兩邊都要 13 語全覆蓋）
	return ANiceInkHUD::BuildCompositeUiFont(this, TEXT("NiMenuFont"));
}

void ANiceInkMenuHUD::BeginPlay()
{
	Super::BeginPlay();

	// BGM 喚起（舊版繼承 ANiceInkHUD 順帶做掉；改繼承 AHUD 後要自己叫——
	// 斷樂＝舞台編舞的時間零點也沒了）
	if (UNiceInkGameInstance* Inst = UNiceInkGameInstance::Get(this))
	{
		Inst->EnsureBgmPlaying(GetWorld());
	}

	if (GEngine && GEngine->GameViewport && PlayerOwner)
	{
		MenuFont = BuildMenuFont();
		Menu = SNew(SNiMenu).OwnerPC(PlayerOwner).Font(MenuFont);
		GEngine->GameViewport->AddViewportWidgetContent(Menu.ToSharedRef(), /*ZOrder=*/10);
	}
}

void ANiceInkMenuHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Menu.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Menu.ToSharedRef());
	}
	Menu.Reset();
	Super::EndPlay(EndPlayReason);
}

void ANiceInkMenuHUD::RoboOpenJoinPage(const FString& PrefillCode)
{
	if (Menu.IsValid())
	{
		Menu->OpenJoinPage(PrefillCode);
	}
}

void ANiceInkMenuHUD::RoboShowFontSample(const FString& Sample)
{
	if (Menu.IsValid())
	{
		Menu->ShowFontSample(Sample);
	}
}

void ANiceInkMenuHUD::RoboOpenLanguagePage()
{
	if (Menu.IsValid())
	{
		Menu->OpenLanguagePage();
	}
}

void ANiceInkMenuHUD::RoboOpenProfilePage()
{
	if (Menu.IsValid())
	{
		Menu->OpenProfilePage();
	}
}

void ANiceInkMenuHUD::RecreateMenu(bool bOpenSettings)
{
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, bOpenSettings]()
	{
		if (!GEngine || !GEngine->GameViewport || !PlayerOwner)
		{
			return;
		}
		if (Menu.IsValid())
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(Menu.ToSharedRef());
		}
		Menu = SNew(SNiMenu).OwnerPC(PlayerOwner).Font(MenuFont);
		if (bOpenSettings)
		{
			Menu->OpenSettingsPage();
		}
		GEngine->GameViewport->AddViewportWidgetContent(Menu.ToSharedRef(), /*ZOrder=*/10);
	}));
}
