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
class UNeckStretchComponent;
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

	// 轆轤首伸縮脖（2026-07-16 user 定案）：頭身沿手標 seam 真切開，銜接曲面每幀重新
	// 解出（無材料身分＝素色下無扭曲概念）。掛在 BowBody 下、擺骨完成後顯式更新。
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Nice Ink")
	TObjectPtr<UNeckStretchComponent> NeckStretch;


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
	// 臉指向制（2026-07-16 三改版 user 定案：一維軌道「簡單但難用」→ 2-DOF 直接指向）：
	// 睜眼＝滑鼠 X/Y 直接操縱「臉指向」的方位/俯仰（FPS 肌肉記憶：滑鼠往哪撥臉朝哪）；
	// 頭部姿勢＝指向的純函數（yaw∘pitch 構造、零 roll 框架），玩家仍不持有姿勢自由度——
	// 俯仰域鉗在量測表（每方位最大俯角）、抬升＝頭總旋轉角的純函數（平台制）。
	// 相機嚴格＝眉心＋臉朝向——「看得到的」≡「臉表達的」≡「旁人讀到的破綻」，
	// 且指向語義比軌道制更強（把人轉到畫面中央＝刻意動作＝抓現行訊號更鋒利）。
	// 網格層：頭身沿 user 手標 cut_seam_head 真切開；銜接＝UNeckStretch 每幀生成。
	// 閉眼＝方向鍵盲瞄照舊（滑鼠仍屬迷宮、骨頭不動、Q 噴射讀相機 yaw）。

	// 閉眼盲瞄低頭上限（僅閉眼瞄準域；睜眼軌道無此參數）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|SleepLook", meta = (ClampMin = "0", ClampMax = "150"))
	float SleepBendMaxDeg = 110.0f;

	// 按住連發的角速度（度/秒）；單擊固定 1°（閉眼盲瞄專用）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|SleepLook", meta = (ClampMin = "10", ClampMax = "360"))
	float SleepHeadTurnRate = 75.0f;

	// 睜眼裝睡 FOV（站姿預設 90）——72＝對視保證強化（user 定案 2026-07-16）：
	// 水平半角 36°/垂直 ~22°，餘光域比舊 102 砍三成——「看誰」≈「臉對準誰」，
	// 且找人要多掃視＝轉頭破綻更多。代價（記帳）：廣角壓迫感淡了、貼臉作畫者不再被
	// 邊緣放大。覆蓋保證以 72 錐重驗（v9 掃描）。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|SleepLook", meta = (ClampMin = "55", ClampMax = "120"))
	float SleepWakeFov = 72.0f;

	// 抬升整體倍率（規則本體＝量測定案的平台制，見 SleepAimLiftCm；
	// 此倍率只留給 viewport 口味微調，1.0＝量測原值）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|SleepLook", meta = (ClampMin = "0", ClampMax = "2"))
	float SleepOrbitLiftScale = 1.0f;

	// 抬升（cm）＝純函數（量測定案 2026-07-16 v8）：46×max(ss(俯仰/12°), ss((總旋轉−8°)/14°))
	// ——俯仰立即墊高（下巴一低就要離胸）、純 yaw 給 8° 寬限（枕上小轉頭不升電梯）、
	// 之後恆高（平台制＝掃視時脖長恆定）。Blender 全網格 BVH 全域 (az,tilt) 網格驗證零洞。
	float SleepAimLiftCm(float TiltDeg, float TotalRotDeg) const;

	// 每方位最大俯角（度）＝合法域鉗位（量測烘入：零穿透＋地板＋環間淨空；
	// 腳側 ~100°（下巴/胸極限）、側/頭側 105–130°）。趴姿共用近似（記帳）。
	static float SleepAimMaxTiltDeg(float AzDeg);



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

	// 睡姿頭部姿態＝臉指向（方位＋俯仰，複製給其他端擺骨；閉眼不送＝盲瞄不洩漏）。
	// 各端用同一套純函數（yaw∘pitch＋抬升規則）求值＝畫面必然一致；抬升不複製。
	// 方位語義：180=腳側、0=頭頂側（與量測掃描一致）；俯仰：0=朝天、越大越壓向水平線下。
	UPROPERTY(BlueprintReadWrite, Replicated, Category = "Nice Ink")
	float SleepAimAzDeg = 180.0f;

	UPROPERTY(BlueprintReadWrite, Replicated, Category = "Nice Ink")
	float SleepAimTiltDeg = 0.0f;

	UFUNCTION(Server, Unreliable)
	void ServerUpdateSleepAim(float AzDeg, float TiltDeg);

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

	// 入睡/甦醒傳送的本端落地（2026-07-16 bug：server 對 autonomous proxy 的
	// SetActorTransform 只有位置會被移動修正推回、yaw 是客戶端權威永不修正——
	// 受害者自己畫面上的身體/相機保持入睡前朝向＝整個世界讀感旋轉、目光契約跨端破裂。
	// 修=owning client 本地執行同一個傳送，之後上報移動自然帶新朝向）
	UFUNCTION(Client, Reliable)
	void ClientSyncPoseTransform(const FTransform& NewTransform);

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

	// robo：模擬沉睡受害者的頭控（本地受害者於下一 tick 消化；滑鼠不可注入）。
	// 睜眼＝(Yaw,Pitch) 設臉指向 (az,tilt)（走真實 ServerUpdateSleepAim 鏈、tilt 過鉗位）；
	// 閉眼＝(Twist,Bend) 直設盲瞄狀態（骨不動，驗噴射 yaw）。
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

	// --- 睡姿替身（頭部轉動破綻，2026-07-15；臉指向制 2026-07-16 三改版）---
	// 單一頻道：頭部姿勢＝臉指向(az,tilt) 的純函數，相機剛體錨在眉心、朝向＝臉朝向。
	// 你看到哪裡＝你的臉指向哪裡＝旁人讀到的破綻，三者恆等式由構造保證。
	// 一具替身全員可見（含本人）。靜態 Body 只藏不關碰撞——畫墨/噴射 UV 解算照打。
	bool EnsureBowBodyAsset() { return EnsurePoseableAsset(BowBody); }
	bool EnsurePoseableAsset(UPoseableMeshComponent* Poseable); // SK 惰性載入＋皮膚 MID 共享
	void UpdateSleepBodyDouble(float DeltaSeconds); // 每 tick（所有端）：替身開關＋指向擺骨＋本人相機
	bool bSleepDoubleActive = false;
	float SleepLookSendAccum = 0.0f;    // 姿態上報節流（本人端）
	float LastSentAimAz = 180.0f;
	float LastSentAimTilt = 0.0f;
	float SleepAimAzLocal = 180.0f;     // 本人端臉指向方位（睜眼滑鼠 X 累積；180=腳側）
	float SleepAimTiltLocal = 0.0f;     // 本人端臉指向俯仰（滑鼠 Y；0=朝天，鉗於量測表）
	float SleepTwistLocal = 0.0f;       // 閉眼盲瞄扭轉（方向鍵；骨不動只轉隱形相機）
	float SleepBendLocal = 0.0f;        // 閉眼盲瞄低頭（[0,上限]）
	bool bHasPendingDebugSleepLook = false; // DebugRoboSleepLook 待消化（本地受害者 tick）
	FVector2D PendingDebugSleepLook = FVector2D::ZeroVector;

	bool bWakeGazeActive = false;       // 睜眼指向啟用（FOV 切廣角）

	// 他端指向顯示平滑（複製 20Hz 節流+無插值＝每包跳格抽動；
	// 純視覺追趕，本人端零延遲不經此路）
	float RemoteAimAzDeg = 180.0f;
	float RemoteAimTiltDeg = 0.0f;
	bool bRemoteOrbitSnap = true;

	// 方向鍵盲瞄輪詢（閉眼）＋睜眼滑鼠 X/Y→(az,tilt)；姿態上報節流
	void PollSleepHead(APlayerController* PC, float DeltaSeconds);
	float SleepKeyHeldTime[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; // L/R/U/D 按住時間

	void ActivateWakeGaze(const FTransform& CompT);
	void DeactivateWakeGaze();

	// 睡姿替身姿勢快取（止血：姿態沒變不寫骨——每 tick 歸零重擺=假移動=動態模糊糊臉）
	bool bSleepPoseDirty = true;
	float LastPoseAz = 1e9f;
	float LastPoseTilt = 1e9f;
	bool bLastPoseEyes = false;
	FTransform SleepNeckRefCS;          // 替身啟用時捕捉的參考姿勢（分析式擺骨用）
	FTransform SleepHeadRefCS;
	bool bSleepRefCaptured = false;
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
