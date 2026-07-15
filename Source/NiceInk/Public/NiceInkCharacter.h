#pragma once

#include "CoreMinimal.h"
#include "DreamMaze.h"
#include "Engine/NetSerialization.h"
#include "GameFramework/Character.h"
#include "InkTypes.h"
#include "NiceInkCharacter.generated.h"

class ACameraActor;
class UCameraComponent;
class UDreamMazeComponent;
class UInkBodyComponent;
class UInkCanvasComponent;
class UPoseableMeshComponent;
class USkeletalMesh;

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

	// 醉夢圓形迷宮（SPEC v3.3 甦醒小遊戲）：受害者 client 本地模擬＋繪製
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Nice Ink")
	TObjectPtr<UDreamMazeComponent> DreamMaze;

	// 站姿／仰躺大字睡姿網格（同 UV 圖集，切換不影響墨水 RT）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink")
	TObjectPtr<UStaticMesh> StandMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink")
	TObjectPtr<UStaticMesh> SleepMesh;

	// 貼臉鎖定時的可擺骨身體（程式化硬彎腰——SPEC 定案 #23）。
	// 骨骼資產缺席時退回站姿靜態網格（姿勢不演，機制照跑）。
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Nice Ink")
	TObjectPtr<UPoseableMeshComponent> BowBody;


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink")
	TObjectPtr<USkeletalMesh> BowMesh;

	// 實體麥克筆：筆尖永遠在墨水落下的位置（畫布狀態即真相、全端一致、零 offset）
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Nice Ink")
	TObjectPtr<UStaticMeshComponent> PenMesh;

	// 麥克筆觸及距離（公分）。刻意短——近臉作畫原則：要畫就得湊近。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Paint", meta = (ClampMin = "50", ClampMax = "500"))
	float PaintReach = 140.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Input", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float LookSensitivity = 1.6f;

	UPROPERTY(BlueprintReadOnly, Category = "Nice Ink|Paint")
	int32 SelectedColorIndex = 0;

	// 沉睡中（受害者入座～現身之間）。移動鎖定；閉眼與否看 bEyesOpen。
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Asleep, Category = "Nice Ink")
	bool bAsleep = false;

	// --- 甦醒轉頭參數（playtest 域）---
	// 操作制（2026-07-15 user 終版定案）：方向鍵分軸控制——左右＝扭轉、下＝低頭、
	// 上＝撤回低頭（不仰頭）；單擊 1°、按住連發；滑鼠永久屬於迷宮游標。
	// 貓頭鷹扭轉上限：順/逆各 270°（user 定值：再多脖子形變撐不住）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|SleepLook", meta = (ClampMin = "90", ClampMax = "360"))
	float SleepTwistMaxDeg = 270.0f;

	// 低頭上限（0=安睡朝向；域 [0,上限]＝天生不可能向後仰進枕頭）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|SleepLook", meta = (ClampMin = "0", ClampMax = "150"))
	float SleepBendMaxDeg = 110.0f;

	// 按住連發的角速度（度/秒）；單擊固定 1°
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|SleepLook", meta = (ClampMin = "10", ClampMax = "360"))
	float SleepHeadTurnRate = 75.0f;

	// 頸骨前/後彎上限：頸骨支點在脖根＝彎多少頭就沿弧抬多高（伸脖本體）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|SleepLook", meta = (ClampMin = "0", ClampMax = "80"))
	float SleepNeckBendCapDeg = 50.0f;

	// 頸骨的扭轉分擔比：把貓頭鷹的皮膚剪切攤開在整段脖子（0=全在頭頸交界）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|SleepLook", meta = (ClampMin = "0", ClampMax = "0.5"))
	float SleepNeckTwistShare = 0.25f;

	// 伸脖額外量（cm，隨彎角比例給）：轆轤首檔（2026-07-16 user 解禁拉伸量：
	// 「我想要玩家在醒來後的頭可以拉長」）——48cm＝相機站上肚頂（z85）之上、
	// 視線過水平線俯視自己的肚皮與腳邊；力學模型維持脖根捲曲弧（user 定案），
	// 軀幹凍結＝畫布不動＝shift 偵測經濟不破。穿膜由姿勢層眉護束飽和擋死。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|SleepLook", meta = (ClampMin = "0", ClampMax = "80"))
	float SleepNeckMaxStretch = 72.0f;



	// 翻身（2026-07-15 user 定案）：非 ragdoll——作畫者之一提出、其餘作畫者
	// 全數同意後執行；正面/背面均為固定姿勢。醒來（現身）自動回正面。
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_FaceDown, Category = "Nice Ink")
	bool bBodyFaceDown = false;

	// --- 貼臉鎖定（SPEC v3.1 定案 #19/#23）---

	// 鎖定中：畫面固定、游標作畫、身體硬彎腰貼臉
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Lean, Category = "Nice Ink|Lean")
	bool bLeanLocked = false;

	// 鎖定點（被畫身體表面）與該處法線
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink|Lean")
	FVector_NetQuantize LeanPoint;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink|Lean")
	FVector_NetQuantizeNormal LeanNormal;

	// 被畫的身體（受害者；終局＝輸家）
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink|Lean")
	TObjectPtr<ANiceInkCharacter> LeanTarget;

	// 偷瞄中（按住 Shift）：頭頸硬轉向受害者——全房可見的緊張
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Peeking, Category = "Nice Ink|Lean")
	bool bPeeking = false;

	// 無聲甦醒（定案 #8）：走出迷宮出口後睜眼。零系統提示——
	// 其他玩家能觀察到的破綻＝睜眼貼圖＋頭部轉動（睡姿替身驅動，2026-07-15）。
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_EyesOpen, Category = "Nice Ink")
	bool bEyesOpen = false;

	// 睡姿頭部姿態（複製給其他端擺骨；閉眼不送＝盲瞄不洩漏）。
	// Twist＝繞脊椎軸的貓頭鷹扭轉（±SleepTwistMaxDeg）；Bend＝低頭量（≥0，繞當前臉的耳軸）。
	// 兩者完整決定臉方向；輸入即狀態（方向鍵分軸）——無任何方向反解/分支/纏繞機器。
	UPROPERTY(BlueprintReadWrite, Replicated, Category = "Nice Ink")
	float SleepTwistDeg = 0.0f;

	UPROPERTY(BlueprintReadWrite, Replicated, Category = "Nice Ink")
	float SleepBendDeg = 0.0f;

	UFUNCTION(Server, Unreliable)
	void ServerUpdateSleepLook(float TwistDeg, float BendDownDeg);

	// 反制工具庫存（迷宮存檔點發放；睜眼即過期——SPEC 定案 #6/#7）。
	// 只複製給本人：作畫者不該從網路層讀到「受害者拿到技能了」。
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	int32 SprayCharges = 0;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	int32 KickCharges = 0;

	// 沉睡者選定的噴射出發點（1=鼻／2=陰部／3=肛門；SPEC 定案 #6）
	UPROPERTY(BlueprintReadOnly, Category = "Nice Ink")
	EInkEvidenceType SelectedSprayOrigin = EInkEvidenceType::Sneeze;

	// 被噴致盲（該回合內；指認結算時清除）。致盲色＝噴射物種類。
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Blinded, Category = "Nice Ink")
	bool bBlinded = false;

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Nice Ink")
	EInkEvidenceType BlindType = EInkEvidenceType::Sneeze;

	UFUNCTION(BlueprintPure, Category = "Nice Ink")
	FLinearColor GetCurrentColor() const;

	UFUNCTION(BlueprintPure, Category = "Nice Ink")
	int32 GetInkAuthorId() const;

	// --- 伺服器端流程控制（GameMode 呼叫） ---

	// 入睡：鎖移動、閉眼、身體躺到指定位置（仰躺大字，定案 #18）。
	// 甦醒現身：站回座位、睜眼、恢復移動。
	void ServerSetAsleep(bool bNewAsleep, const FTransform& LieTransform);

	// --- 貼臉鎖定 RPC ---

	UFUNCTION(Server, Reliable)
	void ServerEnterLean(ANiceInkCharacter* Target, FVector_NetQuantize Point, FVector_NetQuantizeNormal Normal);

	UFUNCTION(Server, Reliable)
	void ServerExitLean();

	UFUNCTION(Server, Reliable)
	void ServerSetPeeking(bool bNewPeeking);

	// 伺服器端強制起身（被踹飛、相位切換）
	void ForceExitLean();

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

	// 沉睡中按 WASD＝請求現身（server 驗證已無聲甦醒＝bEyesOpen）
	UFUNCTION(Server, Reliable)
	void ServerRequestEmerge();

	// --- 翻身提案（作畫者 → 伺服器；GameMode 計票） ---

	UFUNCTION(Server, Reliable)
	void ServerProposeFlip();

	UFUNCTION(Server, Reliable)
	void ServerAgreeFlip();

	// 伺服器端直接執行翻身（GameMode 票數到齊時呼叫；現身時回正）
	void ServerSetFaceDown(bool bNewFaceDown);

	// 本地：這輪提案我是否已表態（HUD 顯示「等待中」）；提案編號變更時重置
	int32 FlipAgreedProposalSerial = INDEX_NONE;

	// --- 醉夢圓形迷宮 RPC（SPEC v3.3 定案 #30/#31）---
	// 信任模型沿用既有：client 判定、server 驗身分/相位/上限（party game 取捨）。
	// 事件只走 victim↔server↔killer 三點——其餘玩家一無所知是規則本體。

	// 回合開始（EnterSeating）：server 發種子＋難度檔＋兇手名單，受害者端決定性重建迷宮
	UFUNCTION(Client, Reliable)
	void ClientStartMaze(int32 Seed, const FDreamMazeParams& Params, const TArray<int32>& TrapOwnerIds);

	// 受害者踩中陷阱（client 偵測）→ server 驗證後只通知兇手開轉盤
	UFUNCTION(Server, Reliable)
	void ServerMazeTrapHit(int32 KillerPlayerId);

	// 兇手端：開 5 秒轉盤（滾輪選 −360~360；逾時自動送出當前值，預設 0）
	UFUNCTION(Client, Reliable)
	void ClientOpenTrapDial(float DialSeconds);

	UFUNCTION(Server, Reliable)
	void ServerSubmitTrapDial(float AngleDeg);

	// 受害者端：套用旋轉（含 0 度——動畫照播、不可分辨）
	UFUNCTION(Client, Reliable)
	void ClientApplyMazeRotation(float AngleDeg);

	// 存檔點：經過＝存檔＋獲得技能（server 每回合每點只授一次）。0=噴射 1=拳腳
	UFUNCTION(Server, Reliable)
	void ServerMazeCheckpointReached(uint8 CheckpointType);

	// 走出出口＝無聲甦醒（睜眼、零提示；等同舊小遊戲第三次成功的語意）
	UFUNCTION(Server, Reliable)
	void ServerMazeExited();

	// --- 兇手轉盤本地狀態（HUD 讀取；robo 可直寫 TrapDialAngleDeg） ---

	UPROPERTY(BlueprintReadWrite, Transient, Category = "Nice Ink|Maze")
	bool bTrapDialActive = false;

	UPROPERTY(BlueprintReadWrite, Transient, Category = "Nice Ink|Maze")
	float TrapDialAngleDeg = 0.0f;

	float TrapDialEndTime = 0.0f;   // client world time
	float TrapDialDuration = 5.0f;

	// --- 沉睡者反制（SPEC 定案 #6/#7） ---

	// 噴射：以選定出發點朝世界 yaw 方向丟出投射物（醒來前任意時刻；無命中回饋）
	UFUNCTION(Server, Reliable)
	void ServerSpray(EInkEvidenceType Origin, float AimYawWorld);

	// 拳腳：朝世界 yaw 方向掃掠；命中＝瘀青＋彈飛
	UFUNCTION(Server, Reliable)
	void ServerKick(float AimYawWorld);

	// 命中者身上留證據標記（所有端重播進畫布）
	UFUNCTION(NetMulticast, Reliable)
	void MulticastAddEvidence(EInkEvidenceType Type, FVector2D UV, int32 Seed);

	// server 端套用致盲（僅屬性複寫；HUD 讀 bBlinded 蓋致盲遮罩）
	void ServerApplyBlind(EInkEvidenceType Type);

	// 回合結算清場：洗麥克筆與證據、解除致盲（GameMode 對全員廣播）
	UFUNCTION(NetMulticast, Reliable)
	void MulticastRoundCleanup();

	// --- 場間大廳（PostGame）---

	// 雷射自己身上最舊的碳黑刺青一級（自費；三級整幅清除；永久無效）
	UFUNCTION(Server, Reliable)
	void ServerRequestLaser();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastApplyLaser(int32 WorkId);

	// 跨場刺青還原（入場時 server 廣播存檔內容）
	UFUNCTION(NetMulticast, Reliable)
	void MulticastRestoreWork(FInkWork Work);

	UFUNCTION(Server, Reliable)
	void ServerSubmitAccusation(int32 WorkId, int32 AccusedPlayerId);

	// --- 指認 UI 狀態（受害者本地；HUD 讀取） ---

	// 目前預覽的傑作編號（1..N，巡禮順序）；數字鍵選擇，鏡頭跟著聚焦
	UPROPERTY(BlueprintReadOnly, Category = "Nice Ink|Accuse")
	int32 AccusePickNumber = 1;

	// 目前指向的嫌疑人游標（TAB 輪換；非受害者玩家依席位排序）
	UPROPERTY(BlueprintReadOnly, Category = "Nice Ink|Accuse")
	int32 AccuseSuspectCursor = 0;

	// 嫌疑人清單（席位排序、排除受害者）；回傳 PlayerState 供 HUD 顯示名字
	UFUNCTION(BlueprintPure, Category = "Nice Ink|Accuse")
	APlayerState* GetAccuseSuspect() const;

	// HUD 用：鎖定中的麥克筆游標位置（螢幕像素）
	FVector2D GetLeanCursorPx() const { return LeanCursorPx; }

	// 噴射命中彎腰身體時的證據落點（骨頭→身體圖集 UV 的粗錨定；
	// 無骨名（膠囊命中）時以命中高度粗分頭/軀幹/腿）
	bool GetEvidenceUVForHit(FName BoneName, const FVector& ImpactPoint, FVector2D& OutUV);

	// --- 除錯 exec（PIE 主控台；轉發到伺服器） ---

	UFUNCTION(Exec)
	void NiStart();

	// 建房／搜房加入（NULL subsystem＝LAN；EOS 憑證填好後＝網路房）
	UFUNCTION(Exec)
	void NiHost();

	UFUNCTION(Exec)
	void NiJoin();

	UFUNCTION(Exec)
	void NiEmerge();

	// 指認巡禮清單中第 WorkNumber 幅（1 起算）的作者為第 SeatIndex 席玩家
	UFUNCTION(Exec)
	void NiAccuse(int32 WorkNumber, int32 SeatIndex);

	// 迷宮生成器離線統計：NumSeeds 個種子的通關時間/繞路成本分布＋移動 fuzz 自測。
	// 調參前先開表（SPEC 待定 #2 的校準儀器）；結果進 log。
	UFUNCTION(Exec)
	void NiMazeStats(int32 NumSeeds, int32 Cup);

	// robo：模擬沉睡受害者轉頭（本地受害者於下一 tick 消化＝設相機視線＋走真實
	// ServerUpdateSleepLook RPC 鏈——頭轉破綻的端到端驗法；滑鼠不可注入）
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugRoboSleepLook(float Yaw, float Pitch);

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

	UFUNCTION()
	void OnRep_EyesOpen();

	UFUNCTION()
	void OnRep_Blinded();

	UFUNCTION()
	void OnRep_FaceDown();

	UFUNCTION()
	void OnRep_Lean();

	UFUNCTION()
	void OnRep_Peeking();

	// --- 貼臉鎖定內部 ---

	FVector2D LeanCursorPx = FVector2D::ZeroVector; // 虛擬麥克筆游標（螢幕像素）
	float LeanLockTime = 0.0f;                      // 鎖定起始（鏡頭到位前不落筆）
	bool bLeanCamActive = false;
	bool bPeekCamApplied = false;

	FVector2D LastCursorPx = FVector2D::ZeroVector; // 上一 tick 游標（螢幕細分用）

	void PollLeanEnter(APlayerController* PC);
	void PollLockedDraw(APlayerController* PC, float DeltaSeconds);

	// --- 睡姿替身（頭部轉動破綻，2026-07-15）---
	// lean-lock 同構：一具替身全員可見（含本人）、姿勢是唯一真相、
	// 本人相機放在眉間騎著頭骨（轉視野＝轉頭、視野恆與臉同向、自己的頭在鏡頭後）。
	// 靜態 Body 只藏不關碰撞——畫墨/噴射的 UV 解算照打靜態網格。
	bool EnsureBowBodyAsset() { return EnsurePoseableAsset(BowBody); }
	bool EnsurePoseableAsset(UPoseableMeshComponent* Poseable); // SK 惰性載入＋皮膚 MID 共享
	void UpdateSleepBodyDouble();       // 每 tick（所有端）：替身開關＋擺頭骨＋本人相機騎頭
	bool bSleepDoubleActive = false;
	float SleepLookSendAccum = 0.0f;    // 姿態上報節流（本人端）
	float LastSentSleepTwist = 0.0f;
	float LastSentSleepBend = 0.0f;
	float SleepTwistLocal = 0.0f;       // 本人端扭轉狀態（方向鍵直加、±上限 clamp）
	float SleepBendLocal = 0.0f;        // 本人端低頭狀態（[0,上限]）
	bool bHasPendingDebugSleepLook = false; // DebugRoboSleepLook 待消化（本地受害者 tick）
	FVector2D PendingDebugSleepLook = FVector2D::ZeroVector;

	// 方向鍵轉頭輪詢：單擊 1°、按住 0.25s 後以 SleepHeadTurnRate 連發、斜向可同按
	void PollSleepHead(APlayerController* PC, float DeltaSeconds);
	float SleepKeyHeldTime[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; // L/R/U/D 按住時間

	// 睡姿替身姿勢快取（止血：姿態沒變不寫骨——每 tick 歸零重擺=假移動=動態模糊糊臉）
	bool bSleepPoseDirty = true;
	float LastPoseTwist = 1e9f;
	float LastPoseBend = 1e9f;
	bool bLastPoseEyes = false;
	FTransform SleepNeckRefCS;          // 替身啟用時捕捉的參考姿勢（分析式擺骨用）
	FTransform SleepHeadRefCS;
	bool bSleepRefCaptured = false;

	// 穿膜鐵律的解算器：給定扭轉角與想要的低頭角，眉護束（罩住相機與臉前皮膚、
	// 與頭剛體共動的射線組）沿低頭弧線從 0 行進，回傳「不穿越自己軀幹表面／
	// 世界靜態物表面」的最大可行低頭角。共動＝對自己頭皮零相對位移＝永不誤擋；
	// 約束打在姿勢＝頭骨永遠不進身體＝第一/第三人稱由構造保證同一顆頭。
	float SolveSleepBendLimit(float TwistDeg, float DesiredBendDeg);
	FVector SleepGuardPointWS(float TwistRad, float BendDeg, const FTransform& CompT, const FVector& HeadLocalOffset) const;
	float SleepLimitCacheTwist = 1e9f;  // 解算快取（同輸入同身姿不重算——march 有成本）
	float SleepLimitCacheBend = 1e9f;
	FTransform SleepLimitCacheCompT;
	float SleepLimitCacheResult = 0.0f;
	bool ResolveCursorToTargetUV(APlayerController* PC, const FVector2D& ScreenPx, FVector2D& OutUV) const;
	void ApplyBowPose();     // 程式化硬彎腰（所有端；解算頭到落筆點、臉對準目標）
	void ResetBowPose();
	void UpdateLeanCamera(APlayerController* PC);
	void UpdatePenVisual();  // 實體筆對位（所有端）
	FVector GetLeanFaceTargetWorld() const; // 受害者頭部（偷瞄注視點）

	// 系統鏡頭（巡禮／指認預覽／結算聚焦）——各端本地生成、依複寫的 WorkId 對位
	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> CinematicCamera;

	int32 LastViewWorkId = INDEX_NONE;
	bool bWideViewActive = false;
	bool bViewOverridden = false;

	void UpdateCinematicCamera(APlayerController* PC);
	void ViewWork(APlayerController* PC, int32 WorkId);
	void ViewWide(APlayerController* PC);
	void ViewSelfThirdPerson(APlayerController* PC);
	void RestoreView(APlayerController* PC);
	ACameraActor* GetOrSpawnCinematicCamera();

	bool bThirdPersonActive = false;

	void PollLobby(APlayerController* PC);
	void PollAccusation(APlayerController* PC);
	void PollCounterplay(APlayerController* PC);
	void PollFlip(APlayerController* PC);
	void EnsureAvatarApplied();
	void PollLook(APlayerController* PC, float DeltaSeconds);
	void PollMove(APlayerController* PC);
	void PollTrapDial(APlayerController* PC);
	void PollPalette(APlayerController* PC);
	void StopPaintingLocal();
	void ApplySleepVisual();

	// 迷宮技能授予去重（server；每回合每存檔點只授一次，ServerSetAsleep(true) 重置）
	bool bMazeSprayGranted = false;
	bool bMazeKickGranted = false;
};
