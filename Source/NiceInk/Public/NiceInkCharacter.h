#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "NiceInkCharacter.generated.h"

class UCameraComponent;
class UInkBodyComponent;
class UInkCanvasComponent;

// 玩家角色：第一人稱走動的光頭黑道老大。
// 身體＝UInkBodyComponent（char17 靜態網格＋臉貼圖＋膚色＋眼球禁畫遮罩），
// 畫布＝UInkCanvasComponent（麥克筆／刺青向量筆劃）。
//
// 網路模型：筆劃 server 權威——作畫者送 Server RPC，伺服器驗證
// （相位／身分／目標）後由「被畫的角色」multicast 重播到所有端；
// 向量資料在每端獨立重建 RT。規則操作（碳黑轉換、洗墨）同路徑。
//
// 輸入為輪詢制（不依賴輸入綁定資產）：WASD 走動、滑鼠視角、
// 左鍵作畫、1-0 選色。受害者沉睡時輸入被鎖，按 WASD＝現身（定案 #17）。
UCLASS()
class NICEINK_API ANiceInkCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ANiceInkCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Nice Ink")
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Nice Ink")
	TObjectPtr<UInkBodyComponent> Body;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Nice Ink")
	TObjectPtr<UInkCanvasComponent> InkCanvas;

	// 麥克筆觸及距離（公分）。刻意短——近臉作畫原則：要畫就得湊近。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Paint", meta = (ClampMin = "50", ClampMax = "500"))
	float PaintReach = 140.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Input", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float LookSensitivity = 1.6f;

	UPROPERTY(BlueprintReadOnly, Category = "Nice Ink|Paint")
	int32 SelectedColorIndex = 0;

	// 沉睡中（受害者入座～現身之間）。眼睛閉上、移動鎖定。
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Asleep, Category = "Nice Ink")
	bool bAsleep = false;

	UFUNCTION(BlueprintPure, Category = "Nice Ink")
	FLinearColor GetCurrentColor() const;

	UFUNCTION(BlueprintPure, Category = "Nice Ink")
	int32 GetInkAuthorId() const;

	// --- 伺服器端流程控制（GameMode 呼叫） ---

	// 入睡：鎖移動、閉眼、身體躺到指定位置（仰躺大字，定案 #18）。
	// 甦醒現身：站回座位、睜眼、恢復移動。
	void ServerSetAsleep(bool bNewAsleep, const FTransform& LieTransform);

	// --- 畫墨 RPC（作畫者 → 伺服器） ---

	UFUNCTION(Server, Reliable)
	void ServerPaintBegin(ANiceInkCharacter* Target, int32 ColorIndex, FVector2D UV);

	UFUNCTION(Server, Reliable)
	void ServerPaintPoints(const TArray<FVector2D>& UVs);

	UFUNCTION(Server, Reliable)
	void ServerPaintEnd();

	// --- 畫墨重播（被畫角色 → 所有端） ---

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPaintBegin(int32 AuthorId, FLinearColor Color, FVector2D UV);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPaintPoints(int32 AuthorId, const TArray<FVector2D>& UVs);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPaintEnd(int32 AuthorId);

	// --- 規則操作重播（GameMode 經由受害者角色廣播） ---

	UFUNCTION(NetMulticast, Reliable)
	void MulticastConvertWorkToCarbon(int32 WorkId);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastWashAllMarker();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastLockWorkPermanent(int32 WorkId);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastSetRoundIndex(int32 NewRoundIndex);

	// --- 受害者流程 RPC ---

	// 沉睡中按 WASD＝請求現身（M2 起由甦醒小遊戲第三次成功解鎖；M1 直接放行）
	UFUNCTION(Server, Reliable)
	void ServerRequestEmerge();

	UFUNCTION(Server, Reliable)
	void ServerSubmitAccusation(int32 WorkId, int32 AccusedPlayerId);

	// --- 除錯 exec（PIE 主控台；轉發到伺服器） ---

	UFUNCTION(Exec)
	void NiStart();

	UFUNCTION(Exec)
	void NiEmerge();

	// 指認巡禮清單中第 WorkNumber 幅（1 起算）的作者為第 SeatIndex 席玩家
	UFUNCTION(Exec)
	void NiAccuse(int32 WorkNumber, int32 SeatIndex);

	UFUNCTION(Server, Reliable)
	void ServerRequestStartMatch();

	static ANiceInkCharacter* FindByPlayerId(UWorld* World, int32 PlayerId);

private:
	float CameraPitch = 0.0f;
	int32 AppliedAvatarIndex = INDEX_NONE;

	// 作畫中（本地端）
	bool bPainting = false;
	TWeakObjectPtr<ANiceInkCharacter> PaintTarget;
	TArray<FVector2D> PendingPoints;
	float PointFlushTimer = 0.0f;
	bool bEmergeRequested = false;

	// 伺服器端：此玩家目前畫在誰身上（Points/End RPC 的路由目標）
	TWeakObjectPtr<ANiceInkCharacter> ServerPaintTarget;

	// 站姿席位（入睡時記下，現身時站回來）
	FTransform SeatTransform;

	UFUNCTION()
	void OnRep_Asleep();

	void EnsureAvatarApplied();
	void PollLook(APlayerController* PC, float DeltaSeconds);
	void PollMove(APlayerController* PC);
	void PollPalette(APlayerController* PC);
	void PollPaint(APlayerController* PC, float DeltaSeconds);
	void StopPaintingLocal();
	ANiceInkCharacter* TraceForBody(FVector2D& OutUV) const;
	void ApplySleepVisual();
};
