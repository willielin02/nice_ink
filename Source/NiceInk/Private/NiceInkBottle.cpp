#include "NiceInkBottle.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "NiceInkCharacter.h"
#include "NiceInkGameState.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

ANiceInkBottle::ANiceInkBottle()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	// 位置/角度全部由 GameState 參數純函式導出 ⇒ **不複製移動**
	//（複製會帶 33ms 量化階梯＝轉瓶跳格；純函式零成本零抖動、各端逐位相同）
	SetReplicateMovement(false);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	// 純演出道具：不擋人、不擋游標射線（作畫的 trace 要能穿過去打到皮膚）
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetGenerateOverlapEvents(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> BottleAsset(
		TEXT("/Game/Props/SM_Bottle.SM_Bottle"));
	if (BottleAsset.Succeeded())
	{
		Mesh->SetStaticMesh(BottleAsset.Object);
	}

	// 材質：照抄實體筆的既定做法（BasicShapeMaterial＋Color 參數的 MID）。
	// FBX 帶不進 glb 的 PBR 材質（只有槽名），不上色的結果＝場中央一個灰塊
	//（08-16 截圖自查抓到）。槽序＝匯入實測 paper/glass/whiskey/viko。
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BaseMat(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (BaseMat.Succeeded())
	{
		PropMaterial = BaseMat.Object;
	}
}

void ANiceInkBottle::BeginPlay()
{
	Super::BeginPlay();
	if (!PropMaterial || !Mesh || !Mesh->GetStaticMesh())
	{
		return;
	}
	// 佔位配色（待 viewport：換色＝改這四行；換網格＝改上面的資產路徑）
	static const TMap<FName, FLinearColor> SlotColors = {
		{ FName(TEXT("paper")),   FLinearColor(0.78f, 0.72f, 0.58f) }, // 標籤：和紙色
		{ FName(TEXT("glass")),   FLinearColor(0.06f, 0.11f, 0.07f) }, // 瓶身：深綠玻璃
		{ FName(TEXT("whiskey")), FLinearColor(0.35f, 0.16f, 0.04f) }, // 瓶內酒：琥珀
		{ FName(TEXT("viko")),    FLinearColor(0.05f, 0.05f, 0.06f) }, // 瓶蓋：近黑
	};
	const TArray<FStaticMaterial>& Mats = Mesh->GetStaticMesh()->GetStaticMaterials();
	for (int32 i = 0; i < Mats.Num(); ++i)
	{
		if (UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(PropMaterial, this))
		{
			const FLinearColor* C = SlotColors.Find(Mats[i].MaterialSlotName);
			MID->SetVectorParameterValue(TEXT("Color"),
				C ? *C : FLinearColor(0.10f, 0.10f, 0.11f));
			Mesh->SetMaterial(i, MID);
		}
	}
}

void ANiceInkBottle::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ANiceInkBottle, RestLocation);
	DOREPLIFETIME(ANiceInkBottle, HeldBy);
	DOREPLIFETIME(ANiceInkBottle, DropFromLocation);
	DOREPLIFETIME(ANiceInkBottle, DropStartServerTime);
	DOREPLIFETIME(ANiceInkBottle, bFalling);
}

float ANiceInkBottle::SpinEase(float T)
{
	// ease-out quintic：角速度 ∝ (1-t)^4 單調遞減到 0＝「自然地、慢慢地停」。
	// 五次方比三次方尾巴更長＝最後一小段慢爬（真物理瓶子的讀感）。
	const float U = 1.0f - FMath::Clamp(T, 0.0f, 1.0f);
	return 1.0f - U * U * U * U * U;
}

FVector ANiceInkBottle::GetNeckWorldLocation() const
{
	return GetActorLocation() + GetActorRotation().Quaternion().GetAxisX() * NeckOffsetCm;
}

void ANiceInkBottle::ServerSetHeld(AActor* Holder)
{
	if (!HasAuthority())
	{
		return;
	}
	HeldBy = Holder;
	bFalling = false;
}

void ANiceInkBottle::ServerDrop()
{
	if (!HasAuthority() || !GetWorld())
	{
		return;
	}
	HeldBy = nullptr;
	DropFromLocation = GetActorLocation();
	DropStartServerTime = GetWorld()->GetTimeSeconds();
	bFalling = true;
}

void ANiceInkBottle::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const UWorld* World = GetWorld();
	const ANiceInkGameState* GS = World ? World->GetGameState<ANiceInkGameState>() : nullptr;
	if (!GS)
	{
		return;
	}

	// ① 脫手落地：拋物線（不進物理引擎＝跨端決定性），落地後歸位躺平。
	// **先於握持分支**：ServerDrop 清 HeldBy 與設 bFalling 是兩個複製欄位，
	// 到達順序不保證——落地優先＝順序無關。
	if (bFalling)
	{
		constexpr float FallSeconds = 0.55f;
		const float T = FMath::Clamp(
			(static_cast<float>(World->GetTimeSeconds()) - DropStartServerTime) / FallSeconds, 0.0f, 1.0f);
		const FVector Ground = RestLocation;
		// 水平線性、垂直重力型（t²）＝真掉落讀感
		FVector P = FMath::Lerp(DropFromLocation, Ground, T);
		P.Z = FMath::Lerp(DropFromLocation.Z, Ground.Z, T * T);
		SetActorLocation(P);
		// 落地過程中滾平（roll 歸零、保持當下 yaw）
		const FRotator R = GetActorRotation();
		SetActorRotation(FRotator(FMath::Lerp(R.Pitch, 0.0f, T), R.Yaw, FMath::Lerp(R.Roll, 0.0f, T)));
		if (T >= 1.0f && HasAuthority())
		{
			bFalling = false;
		}
		return;
	}

	// ② 握在手上：整個變換跟手（由持有者的姿勢層算——瓶子本身不做任何解算）
	if (const ANiceInkCharacter* Holder = Cast<ANiceInkCharacter>(HeldBy))
	{
		FTransform BottleT;
		if (Holder->GetCeremonyBottleTransform(BottleT))
		{
			SetActorLocationAndRotation(BottleT.GetLocation(), BottleT.GetRotation());
			return;
		}
		return; // 尚未捕捉相對變換：原地不動（下一幀就接上，零跳變）
	}

	// ③ 轉瓶／靜止：位置恆在圈心，yaw＝起點→終點的減速曲線（純函式）
	SetActorLocation(RestLocation);
	float Yaw = GS->BottleEndYaw;
	if (GS->CeremonyStep == ENiCeremonyStep::Spin)
	{
		Yaw = FMath::Lerp(GS->BottleStartYaw, GS->BottleEndYaw, SpinEase(GS->GetCeremonyAlpha()));
	}
	SetActorRotation(FRotator(0.0f, Yaw, 0.0f));
}
