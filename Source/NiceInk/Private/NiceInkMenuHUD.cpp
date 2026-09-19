#include "NiceInkMenuHUD.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "NiceInkUiTokens.h"
#include "Engine/Font.h"
#include "Engine/GameViewportClient.h"
#include "NiceInkGameInstance.h"
#include "NiceInkHUD.h"
#include "NiceInkMenuWidget.h"
#include "SNiHud.h"   // NiSlate::LoadKeycapTex（一顆鍵一張的鍵帽）

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
		LogoTex = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/T_UI_Logo.T_UI_Logo"));
		KeycapTex = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/T_UI_Keycap.T_UI_Keycap"));
		Menu = SNew(SNiMenu).OwnerPC(PlayerOwner).Font(MenuFont).LogoTex(LogoTex).KeycapTex(KeycapTex).MenuHud(this);
		GEngine->GameViewport->AddViewportWidgetContent(Menu.ToSharedRef(), /*ZOrder=*/10);
	}
}

UTexture2D* ANiceInkMenuHUD::GetKeyTex(const FString& Key)
{
	const FName CacheKey(*Key);
	if (TObjectPtr<UTexture2D>* Found = KeyTexCache.Find(CacheKey))
	{
		return *Found;
	}
	UTexture2D* Tex = NiSlate::LoadKeycapTex(Key);
	KeyTexCache.Add(CacheKey, Tex);
	return Tex;
}

void ANiceInkMenuHUD::EnsureScrimTex()
{
	if (ScrimTex)
	{
		return;
	}
	// 一張貼圖、一次 DrawTile（與局內 ScrimTex 同一個做法：疊 N 個 DrawRect 曾被
	// robo 的 cruise tipSpd 契約抓到成本迴歸——選單雖沒有那條契約，做法統一）。
	constexpr int32 N = 64;
	constexpr float Hold = 0.45f;   // 內容欄本體（標題／按鈕／chip）在這一段之內
	UTexture2D* Tex = UTexture2D::CreateTransient(N, 1, PF_B8G8R8A8);
	Tex->SRGB = false;
	Tex->NeverStream = true;
	Tex->Filter = TF_Bilinear;
	Tex->AddressX = TA_Clamp;
	Tex->AddressY = TA_Clamp;
	FTexture2DMipMap& Mip = Tex->GetPlatformData()->Mips[0];
	uint8* Data = static_cast<uint8*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	for (int32 x = 0; x < N; ++x)
	{
		const float T = x / static_cast<float>(N - 1);
		float A = 1.0f;
		if (T > Hold)
		{
			const float U = FMath::Clamp((T - Hold) / (1.0f - Hold), 0.0f, 1.0f);
			A = 1.0f - (U * U * (3.0f - 2.0f * U));
		}
		uint8* Px4 = Data + x * 4;
		Px4[0] = 255; Px4[1] = 255; Px4[2] = 255; // BGRA、白底吃頂點 tint
		Px4[3] = static_cast<uint8>(FMath::Clamp(A, 0.0f, 1.0f) * 255.0f + 0.5f);
	}
	Mip.BulkData.Unlock();
	Tex->UpdateResource();
	ScrimTex = Tex;

	// 垂直版（上／下暗角）：純 smoothstep 1→0，不留平頂
	UTexture2D* VTex = UTexture2D::CreateTransient(1, N, PF_B8G8R8A8);
	VTex->SRGB = false;
	VTex->NeverStream = true;
	VTex->Filter = TF_Bilinear;
	VTex->AddressX = TA_Clamp;
	VTex->AddressY = TA_Clamp;
	FTexture2DMipMap& VMip = VTex->GetPlatformData()->Mips[0];
	uint8* VData = static_cast<uint8*>(VMip.BulkData.Lock(LOCK_READ_WRITE));
	for (int32 y = 0; y < N; ++y)
	{
		const float U = y / static_cast<float>(N - 1);
		const float A = 1.0f - (U * U * (3.0f - 2.0f * U));
		uint8* Px4 = VData + y * 4;
		Px4[0] = 255; Px4[1] = 255; Px4[2] = 255;
		Px4[3] = static_cast<uint8>(FMath::Clamp(A, 0.0f, 1.0f) * 255.0f + 0.5f);
	}
	VMip.BulkData.Unlock();
	VTex->UpdateResource();
	VScrimTex = VTex;
}

void ANiceInkMenuHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}
	EnsureScrimTex();
	if (!ScrimTex)
	{
		return;
	}
	FLinearColor C = NiHudColor::Black;
	C.A = ScrimAlpha;
	FCanvasTileItem Tile(FVector2D(0.0f, 0.0f), ScrimTex->GetResource(),
		FVector2D(Canvas->ClipX * ScrimWidthFrac, Canvas->ClipY),
		FVector2D(0.0f, 0.0f), FVector2D(1.0f, 1.0f), C);
	Tile.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Tile);

	// 暗角（十修）：上／下／右三邊再壓一層——整張畫面暗一階、主角留在亮處，
	// 大廠主選單的背景從來不是原樣的世界。
	if (VScrimTex)
	{
		const float W = Canvas->ClipX;
		const float H = Canvas->ClipY;
		FLinearColor Top = NiHudColor::Black;  Top.A = 0.45f;
		FCanvasTileItem TopTile(FVector2D(0.0f, 0.0f), VScrimTex->GetResource(),
			FVector2D(W, H * 0.18f), FVector2D(0.0f, 0.0f), FVector2D(1.0f, 1.0f), Top);
		TopTile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(TopTile);
		FLinearColor Bottom = NiHudColor::Black;  Bottom.A = 0.62f;
		FCanvasTileItem BottomTile(FVector2D(0.0f, H * 0.76f), VScrimTex->GetResource(),
			FVector2D(W, H * 0.24f), FVector2D(0.0f, 1.0f), FVector2D(1.0f, 0.0f), Bottom);
		BottomTile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(BottomTile);
		FLinearColor Right = NiHudColor::Black;  Right.A = 0.45f;
		FCanvasTileItem RightTile(FVector2D(W * 0.86f, 0.0f), ScrimTex->GetResource(),
			FVector2D(W * 0.14f, H), FVector2D(1.0f, 0.0f), FVector2D(0.0f, 1.0f), Right);
		RightTile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(RightTile);
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

void ANiceInkMenuHUD::RoboOpenJoinLangPicker()
{
	if (Menu.IsValid())
	{
		Menu->RoboOpenJoinLangPicker();
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

void ANiceInkMenuHUD::RoboOpenSettingsPage()
{
	if (Menu.IsValid())
	{
		Menu->OpenSettingsPage();
	}
}

void ANiceInkMenuHUD::RoboOpenHostPage()
{
	if (Menu.IsValid())
	{
		Menu->OpenHostPage();
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
		Menu = SNew(SNiMenu).OwnerPC(PlayerOwner).Font(MenuFont).LogoTex(LogoTex).KeycapTex(KeycapTex).MenuHud(this);
		if (bOpenSettings)
		{
			Menu->OpenSettingsPage();
		}
		GEngine->GameViewport->AddViewportWidgetContent(Menu.ToSharedRef(), /*ZOrder=*/10);
	}));
}

void ANiceInkMenuHUD::RoboGoBack()
{
	if (Menu.IsValid())
	{
		Menu->RoboBack();
	}
}


bool ANiceInkMenuHUD::IsProfileShowcase() const
{
	return Menu.IsValid() && Menu->IsProfilePageOpen();
}
