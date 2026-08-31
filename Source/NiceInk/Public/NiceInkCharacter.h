#pragma once

#include "CoreMinimal.h"
#include "DreamMaze.h"
#include "DreamTrace.h"
#include "Engine/NetSerialization.h"
#include "GameFramework/Character.h"
#include "InkTypes.h"
#include "NiceInkTypes.h"
#include "NiceInkCharacter.generated.h"

class ACameraActor;
class UCameraComponent;
class UDreamMazeComponent;
class UInkBodyComponent;
class UInkCanvasComponent;
class UMaterialInterface;
class UNeckStretchComponent;
class UPoseableMeshComponent;
class USkeletalMesh;
struct FNetViewer;
struct FReferenceSkeleton;

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

	// 防作弊 P0-2（2026-08-31；帳本=Docs/ANTICHEAT_PLAN.md §3）：受害者閉眼沉睡期間，
	// 其他角色對他的連線凍結複製——channel 不關（作品/貼圖不毀、醒來屬性自動收斂），
	// 只斷屬性流。誠實客戶端本來就黑屏；這裡斷的是改裝客戶端看穿黑屏能讀到的
	// 位置/aim/lean（「誰站在我身邊」＝作者推理直送）。
	virtual bool IsReplicationPausedForConnection(const FNetViewer& ConnectionOwnerNetViewer) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Nice Ink")
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Nice Ink")
	TObjectPtr<UInkBodyComponent> Body;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Nice Ink")
	TObjectPtr<UInkCanvasComponent> InkCanvas;

	// 醉夢圓形迷宮（SPEC v3.3 甦醒小遊戲；v4.0 退役封存——描圖取代、元件保留不啟動）
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Nice Ink")
	TObjectPtr<UDreamMazeComponent> DreamMaze;

	// 醉夢描圖（SPEC v4.0 定案 #49 甦醒小遊戲）：受害者 client 本地模擬＋繪製
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Nice Ink")
	TObjectPtr<class UDreamTraceComponent> DreamTrace;

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

	// 作畫中 FOV（直接畫制：眼錨定相機＋窄視野。36＝user 二調定案 2026-07-20
	//（60 仍太廣）——「投入是要付代價才能脫離的狀態」：俯身入畫後世界只剩眼前皮膚，
	// 偷瞄=抬同一顆頭=少畫一筆，「畫到忘我被盯很久」的脆弱窗由窄視野製造）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Paint", meta = (ClampMin = "24", ClampMax = "110"))
	float LeanLockedFov = 36.0f;

	// 鎖定作畫的滑鼠增益口味旋鈕（07-24 操作優化）。實際增益＝LookSensitivity ×
	// FOV 比例縮放 × 本值——開鏡定律：鎖定 FOV 36 對站姿 90，同一角速度的螢幕
	// 投影速度差 tan(45°)/tan(18°)≈3.1×，不縮放＝進鎖游標三倍速（打霧打出去／
	// 割線瞄不準的主因）。1.0＝剛好抵銷放大（游標螢幕速度回到站姿肌肉記憶）。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Paint", meta = (ClampMin = "0.2", ClampMax = "3"))
	float DrawSensitivity = 1.0f;

	// 鎖定作畫滑鼠增益（度/單位）＝EffectiveLookSensitivity × FOV 縮放 × DrawSensitivity
	float DrawAimSensitivity() const;

	// One Euro 濾波旋鈕（筆即游標，2026-07-20 定案）：aim→姿勢/筆/墨的速度自適應濾波。
	// 靜止＝截止壓到 MinCutoff（強濾手抖）；快掃＝截止隨速度拉高（近零滯後）——
	// 取代固定係數顯示平滑（100ms 慣性＝實體筆 lag 的真兇）。相機恆用生訊號。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Paint", meta = (ClampMin = "0.1", ClampMax = "10"))
	float DrawAimFilterMinCutoffHz = 1.0f;

	// One Euro 總開關（2026-08-25 延遲戰役；user 指令「把所有延遲優化到最低，
	// 也要注意玩家的操作到真實筆移動之間的延遲」）——**預設關**。
	// 帳：fc = MinCutoff + Beta·|角速| ⇒ 30°/s 時 fc≈1.9Hz、τ≈84ms；100°/s 時
	// τ≈40ms。稿筆 08-02 就是為了這段滯後（「有人在干擾滑鼠」）拆掉的；同一個
	// 論證原封不動適用於機器工具：
	//   ・Liner：濾波源本來就是**針 aim**，而針已經被 v_max 守恆式限速＝天生平滑
	//     ——再過一層低通只剩滯後（原註解自己寫「追趕本身已是平滑器」）。
	//   ・Shader：手擁有速度，濾波滯後被手直接感覺到，收益＝抑制 ~0.5mm 手抖
	//     （對公尺級身體姿勢不可見，08-02 已量）。
	// 前提條件（同批一起做）：引擎 bEnableMouseSmoothing 關掉——否則「滑鼠不動
	// ⇒ delta=0 ⇒ aim 靜止」這條構造保證是假的（引擎會在停手後繼續合成位移）。
	// 旋鈕留著＝手感有異議時一鍵回舊行為（robo 亦可 A/B）。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Paint")
	bool bDrawAimFilterEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Paint", meta = (ClampMin = "0", ClampMax = "0.5"))
	float DrawAimFilterBeta = 0.03f;

	// HUD（筆即游標）：筆尖世界位置（接觸墨點的螢幕投影錨）；回傳=鎖定中且筆有效
	bool GetPenTipWorldForHud(FVector& OutTip) const
	{
		OutTip = PenTipWorld;
		return bLeanLocked && bPenStateValid;
	}

	bool IsDrawTipReachable() const { return bDrawTipReachable; }

	// 搆不到已持續秒數（有目標但筆搆不著才累積；看向房間不算）——HUD 提示閘
	float GetDrawUnreachableSeconds() const { return DrawUnreachSecs; }

	// 本 tick 游標點可畫嗎（＝墨閘/收筆用的同一個裁決）——HUD 筆尖 ✕ 提示用。
	// 皮膚紗在稜線掠射角被透視壓成看不見的細縫（07-29 user 抓「沒遮蓋卻畫不上」）
	// ＝區域標記的先天盲區；筆尖級提示直接讀裁決、任何角度都準。
	bool IsCursorDrawable() const { return bCursorDrawable; }
	bool HasDrawTarget() const { return bDrawTargetValid; }


	// 作畫者 ghost 材質（直接畫制：畫畫時除自己與沉睡者外，其餘人半透明＋可穿過）
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> GhostMaterial;

	// 麥克筆筆身材質基底（SM_Marker 幾何 only 匯入；引擎基本材質 MID 上深色）
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> PenBodyMaterial;

	// PenMesh 用的是真麥克筆資產（尖端在原點、筆身 +Z、實尺寸）還是引擎圓柱退路
	bool bPenIsMarkerAsset = false;

	// 刺青機制（2026-07-21 伸縮針制；07-22 貼圖版＋伸縮分帳制 user 定案「伸長量
	// 一半給針、一半給握管」）：三件套（build_tattoo_machine_tex.py 生成）——
	// PenMesh=SM_TattooMachine（機械體、pivot=握管頂接點、+Z=離皮膚向、-Y=骨架側
	// 〔FBX 匯入 Y 翻轉實測〕）＋GripMesh=SM_TattooGrip（頂在原點、沿 -Z、單位長
	// 1cm ⇒ scale.Z=握管長 cm）＋NeedleMesh=SM_TattooNeedle（同慣例）。
	// LMB=伸出（針伸出=墨流出的完美因果）、放開=收樁；伸長 e=出針深度-標稱 →
	// 握管拉長 e/2（出針口前移）＋針蓋 e/2。三資產任一缺席＝整套退麥克筆/圓柱。
	bool bPenIsMachineAsset = false;
	bool bNeedleIsAsset = false;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> GripMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> NeedleMesh;

	// 打稿麥克筆（07-25 打稿制）：Stencil 工具時的手持模型（SM_Marker、pivot=筆尖、
	// 筆身 +Z）——與刺青機三件套互斥顯示；本人 FP 直接看 3D 筆（細長不遮畫布，
	// 07-18 直接畫制原味），不走 2D viewmodel
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> MarkerPen;

	// 麥克筆顯示層平滑（純化妝——機器 FP 走 2D 貼圖天然零抖、麥克筆是 FP 可見 3D 件：
	// 骨骼解算噪聲被 13cm 筆桿放大成搖擺）。筆尖=trace 真實命中點不經平滑（墨零延遲）；
	// 只平滑筆身朝向＋懸筆時的位置
	FQuat MarkerPenSmoothedQ = FQuat::Identity;
	FVector MarkerPenSmoothedTip = FVector::ZeroVector;
	bool bMarkerPenSmoothValid = false;

	// 標稱針長＝非觸發時出針口懸在皮膚上方的間隙；解算的「虛擬筆尖」=出針口+此值
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Paint", meta = (ClampMin = "1", ClampMax = "10"))
	float PenNeedleNominalCm = 4.0f;

	// 收針樁長（遮出針口開口＝「收」態讀感）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Paint", meta = (ClampMin = "0", ClampMax = "3"))
	float PenNeedleStubCm = 0.6f;

	// 握管基準長（=資產自然長；build 腳本印出的 PenGripBaseLenCm——資產重生時同步）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Paint", meta = (ClampMin = "5", ClampMax = "80"))
	float PenGripBaseLenCm = 23.17f;

	// --- 刺青手感（2026-07-22 user 定案：高頻點狀出墨＋機器恆速；07-24 皮繩追趕制
	// 取代方向拉桿——「割線難操作」優化）---
	// 本質不變＝線的「速率」所有權歸機器：墨的釋出＝離散扎針（TattooDotHz）、
	// 針速上限由守恆式保證：
	//   v_max = TattooSpacingK × TattooNibDiameterCm × TattooDotHz
	//（最高速下相鄰針心距 = k×筆寬；k=0.5＝間距=半徑＝數位筆刷實線標準）。
	// 07-24 改制＝「路徑」所有權還給手：LMB 按住時滑鼠照常指哪（游標=手的意圖、
	// 相機照常跟生 aim），針沿皮膚以 v_max 上限**追趕游標**（lazy-mouse／拉繩穩定器
	// 的業界標準結構）——手停=針停、曲率=手畫、小圓畫得出來；模式切換（滑鼠變拉桿）
	// 退役。游標最多跑在針前「皮繩長」，超出被鉗回（針釘邊緣/慢針時游標拉不動=
	// 皮繩張力體感）。守恆式/點距/流量/令牌桶全部原樣。
	// 嫌慢只准動 f（TattooDotHz）——動 k>0.7＝針珠讀成虛線＝自毀機制目的。

	// 出墨頻率（Hz）：最高速下的針節拍。每針＝皮膚上一顆固定筆寬的點。
	// 實作=距離節拍（針距 k×筆寬恆定、頻率隨筆速湧現、天花板 f×1.5）——
	// 最高速巡航時恰為此頻率；慢速/掠射自動變疏拍不變距（實線永遠成立）。
	// 12Hz＝user 定值（07-22 二調「太慢」：5→12 ⇒ v_max≈2.34cm/s；速度只准動這顆）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Paint", meta = (ClampMin = "1", ClampMax = "30"))
	float TattooDotHz = 12.0f;

	// 最高速下「相鄰針心距／筆寬」比（實線鐵律 ≤0.7）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Paint", meta = (ClampMin = "0.2", ClampMax = "0.7"))
	float TattooSpacingK = 0.5f;

	// 筆寬實體直徑 cm（＝MarkerUvRadius 0.000452 × 2048px ÷ 0.617px/mm≈3.0mm，
	// 08-02 user 定值；圖集重生／筆寬改動時同步——這是速率換算的錨，不是視覺旋鈕。
	// 細針=慢針：v_max=k×筆寬×f=0.5×0.30×12=1.8cm/s（原 2.34）——嫌慢只准動 f）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Paint", meta = (ClampMin = "0.1", ClampMax = "2"))
	float TattooNibDiameterCm = 0.30f;

	// 皮繩長（cm，皮膚面距離）：游標最多跑在針前這麼遠，超出＝游標被鉗回。
	// 短繩=貼手（針幾乎恆在螢幕中心）、長繩=平滑強但針尾隨感重
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Paint", meta = (ClampMin = "0.5", ClampMax = "8"))
	float TattooChaseLeashCm = 2.5f;

	// 追趕死區（cm）：針距游標小於此＝針已到手＝停針原地扎（點刺；原地冪等由
	// 距離節拍構造保證）。要蓋住 One Euro 靜止殘抖＋trace 量化，不能為 0
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Paint", meta = (ClampMin = "0.02", ClampMax = "1"))
	float TattooChaseStopCm = 0.08f;

	// 沿稿吸附半徑（cm）：Liner 落針時針下 SnapCm 內有稿線＝機器沿稿自動走（07-25
	// 打稿制）；壓在空皮膚上＝照舊自由巡航。UV 空間近似量距（跨縫稿線在縫上斷開
	// ＝誠實限制，放開重壓另一側接續）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Paint", meta = (ClampMin = "0.3", ClampMax = "5"))
	float TattooStencilSnapCm = 1.5f;

	// --- 稿筆自由滑鼠制（07-31 五版 user 逐字定案「放滑鼠自由，僅接收資訊而不
	// 控制滑鼠的走向」）---
	// 滑鼠→aim 純積分（零投影/吸附/否決；凍結相機下角度≡螢幕位置=小畫家的
	// 螢幕恆定增益）；皮膚點 P/筆/墨=每 tick 讀出來的導出量、打不到=收筆（純回饋）。
	// 注視(gaze)=aim 的惰性追隨只餵臉（他端經 DrawAimAzDeg 複製通道收到的就是 gaze）；
	// 相機=凍結＋邊緣推擠（見 StencilCamEdgeFrac）。
	// HUD 出剪影退路的筆錨定深度（cm）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Paint", meta = (ClampMin = "20", ClampMax = "150"))
	float DrawCursorRefDistCm = 60.0f;

	// 注視追隨時間常數（秒）：臉落後游標的惰性（第三人稱「臉追著筆走」讀感）——
	// 0=硬跟、大=懶。（相機自 07-31 二版起不吃 gaze——見 StencilCamEdgeFrac）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Paint", meta = (ClampMin = "0.0", ClampMax = "1.5"))
	float DrawGazeTauS = 0.22f;

	// 稿筆游標增益（口味旋鈕，疊在開鏡縮放上、只吃稿筆）。預設 1.0=不動手感
	//（08-01 教訓：user「太敏感」指的是邊緣推擠早觸發、不是游標速度——0.4 版
	// 誤修已還原；速度是 user 的口味域，沒點名就不動）。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Paint", meta = (ClampMin = "0.1", ClampMax = "1.5"))
	float StencilCursorGain = 1.0f;

	// 稿筆相機邊緣帶：六版 user 定案「只有把筆移到最邊緣還持續往外移動才改變
	// 視野方向；碰到邊緣後還持續位移多少才給多少位移；左鍵按下後視野不位移」
	// ＝邊緣推擠制。判定主詞=可見筆尖投影到本視窗像素（四修）；邊條寬=四邊
	// 統一像素「視窗短邊半長×(1-此值)」（五修：各軸自算百分比在寬螢幕=左右帶
	// 比上下寬近一倍）。0.92≈720 高視窗上離邊框 29px；調小=帶更寬、更早讓位
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Paint", meta = (ClampMin = "0.5", ClampMax = "1.0"))
	float StencilCamEdgeFrac = 0.92f;

	// 稿筆拉繩穩定器（08-02，Lazy Mouse／拉繩模式）：按住 LMB 時墨尖 B 被一條
	// 定長繩拖著追游標 P——d≤L 繩鬆不動、d>L 沿 B→P 前進 (d−L)。手擁有速度
	//（每 tick 只走手多拉出的距離；鬆繩存量放開時由收筆補完吐出=位移全記帳）、
	// 路徑=追逐曲線（垂直於行進向的抖動被 L 按比例壓掉=空間低通）。游標/✕/
	// 小點/邊緣推擠照舊吃生滑鼠——繩子只拉墨與 2D 筆（回饋面）。
	// 08-02 user 驗收「只感覺鈍」＝預設 0（關閉）：手抖只有筆寬 3.9mm 的一成、
	// 上游 One Euro 已濾——平滑收益不可感、滯後可感。旋鈕留（細線需求再開）。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Paint", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float StencilLazyRadiusCm = 0.0f;


	// 導引預測路徑前瞻距離（cm，皮膚弧長）——業界式導引（07-22 二改；五修 user
	//「還是太短」8→16 ≈ 7s 路程）：與速度同域，調速時導引自動等比
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Paint", meta = (ClampMin = "2", ClampMax = "40"))
	float TattooGuideLookaheadCm = 16.0f;

	// 導引顯示門檻（cm）——針落後游標超過此距離才畫行進蟻（導引=針→游標
	// 的待走路徑；貼手時針就在游標上、虛線=噪音）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Paint", meta = (ClampMin = "0.05", ClampMax = "3"))
	float TattooGuideShowCm = 0.25f;

	// 導引取樣步長（cm）——沿皮膚曲面每步一個世界點（線=貼膚曲線；
	// 0.75×21 步＝前瞻拉長後 trace 成本持平）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Paint", meta = (ClampMin = "0.1", ClampMax = "2"))
	float TattooGuideStepCm = 0.75f;

	// 浮雕跳段容差（cm）——「小跳=浮雕、大跳=真邊緣」的分界（07-22 五修 user
	//「乳頭等凹凸處非常難畫」）：射線掃過乳頭/肚臍/下顎時 P 會跳過凸起後方的
	// 自遮盲帶（1~3cm），首版一律判邊緣釘住＝浮雕變牆。≤此值＝騎過凸起（盲帶
	// 誠實留白——看不到的地方本來就畫不到）；>此值或射線離體＝真邊緣照舊釘住
	//（跨肢誤連線防護保留）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Paint", meta = (ClampMin = "0.5", ClampMax = "8"))
	float TattooReliefJumpCm = 3.0f;

	// HUD：巡航導引預測路徑——把巡航步進器往前模擬（本地複本、不動巡航狀態），
	// 沿皮膚取樣世界點；線停在剪影邊緣＝「針會在這裡釘住」的預告。回傳點數
	//（0=沒在巡航輸入態或 aim 不在皮膚上）。
	int32 BuildTattooGuidePath(TArray<FVector>& OutPoints) const;

	// --- Shader 打霧針參數（07-23 四版＝自由揮掃噴槍制，user 定案「動才出墨」）---
	// 割線與打霧＝速度所有權的鏡像：割線=機器擁有速度（巡航、穩、實線）；
	// 打霧=手擁有速度（自由揮掃、快、有節奏），機器改為擁有「單趟的薄」——
	// **距離節拍軟霧**（十版）：沿路徑每 ShaderStampSpacingCm 一枚低濃度軟章、
	// 疊加成連續灰場（單趟中心 ~25-30% 灰、疊趟平滑變深）；任何手速單趟均勻。
	// 停針=零出墨（現實鐵則「手永遠在動」+殺定點掛機）。
	// 內容經濟：覆蓋率由墨點計費定價（每章=固定墨費）。SPEC 對齊 user 統一處理。

	// 沉積流量天花板（Hz）：×1.5=每秒最多沉積的排數——**純防外掛，不是經濟閘**。
	// 2000（九版）：天花板 3000 排/s ⇒ 出縫門檻 600cm/s ≈ 3000°/s aim——真人手速
	// 域（300~1000°/s=180~590cm/s=900~3000 排/s）全在門檻下=任何手速連續。
	// 八版 200 把門檻壓在 60cm/s=真人域正中間被腰斬=viewport 斷斷續續真兇
	//（robo 快掃峰值 108°/s 遠低於真人域——五修鐵則二犯）。覆蓋率經濟由墨點
	// 計費承擔，不靠丟排。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Paint", meta = (ClampMin = "5", ClampMax = "4000"))
	float ShaderDotHz = 2000.0f;

	// 霧沉積距離節拍（cm）：沿筆尖路徑每此距離一枚軟 stamp——**小於刷半徑=任何
	// 手速單趟都是連續霧帶**（07-23 五修：時間節拍版被 user 實測抓到「快掃攤到
	// 不相鄰=看起來沒畫」——真人自然掃速 300~1000°/s 遠超 robo 慢掃域；恆定
	// 時間流量在真人操作域把霧餓死。改距離制=可預期性>甩尾漸層花招）
	// 0.2（十版軟霧）：章距 ≤ 刷半徑/10 ⇒ 單章不可見、疊加逼近連續灰場
	//（平滑度的來源之一；另一半=Canvas.ShaderMistAlpha 低濃度）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Paint", meta = (ClampMin = "0.1", ClampMax = "4"))
	float ShaderStampSpacingCm = 0.2f;

	// 移動閘門檻（aim 角速 度/秒）：低於此=停針=不出墨。滑鼠靜止時 delta 精確為零
	//（閘量在 raw aim 上＝解算抖動偽造不了移動）。只管 Shader——稿筆自 08-01 起
	// 豁免（慢工細描=打稿操作域，閘=慢畫無墨；稿筆墨=P、無此閘要防的噪聲源）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Paint", meta = (ClampMin = "0.5", ClampMax = "30"))
	float MistMinAimSpeedDegS = 3.0f;

	// 排半寬 cm（HUD 範圍圈用；與 InkCanvas.ShaderRowHalfWidthUv 同步——帶寬 2cm，
	// 08-02 user 終值）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Paint", meta = (ClampMin = "0.5", ClampMax = "6"))
	float ShaderBrushRadiusCm = 1.0f;

	// （十二版「手速→濃淡」已於十六版填色制退役：Shader=塗色工具（user 定案），
	// 濃度屬於機器不屬於手速——手速調濃淡=塗均勻殺手。FInkStroke.PointFlow
	// 資料鏈保留（恆滿值；存檔/RPC 格式不動、未來漸層工具可回收））

	// 當前工具（07-25 打稿制：預設=麥克筆打稿——先打稿再上墨的正規流程；滾輪三檔循環
	// Stencil→Liner→Shader。本地真相；RepNeedle 複製給他端選 3D 筆模型）
	EInkNeedle SelectedNeedle = EInkNeedle::Stencil;

	// 他端可見的工具狀態（麥克筆 vs 刺青機的 3D 手持模型切換；pattern 同 bPenTriggerHeld）
	UPROPERTY(Replicated)
	EInkNeedle RepNeedle = EInkNeedle::Stencil;

	// 本端視角下的有效工具（本人=本地真相、他端=複製值）
	EInkNeedle ActiveNeedle() const { return IsLocallyControlled() ? SelectedNeedle : RepNeedle; }

	// 工具切換上服（變化時發；server 直寫＋SkipOwner 複製）
	UFUNCTION(Server, Reliable)
	void ServerSetNeedle(EInkNeedle Needle);

	// 巡航速率上限（cm/s，皮膚表面距離）＝液線針守恆式（巡航/導引=液線針專屬；
	// 霧針=自由揮掃不經巡航）
	float TattooMaxSpeedCmPerSec() const { return TattooSpacingK * TattooNibDiameterCm * TattooDotHz; }

	// （07-24 皮繩制：GetTattooStickForHud 拉桿介面退役——導引輸入改為針→游標
	// 追趕向量，直接在 BuildTattooGuidePath 內部計算）

	// --- FP 2D 筆（07-22 user 定案「FP 要 2D 感、像 FPS viewmodel」）：本人入鎖時
	// TP 三件套 OwnerNoSee，取而代之由 NiceInkHUD 畫 2D 筆貼圖＋針線（出針口→墨點
	// 投影）——永遠同大小同位置=viewmodel 體感、零穿插；旁人看 3D 原樣（世界真相層）。
	// 中間版「FP 3D 斜握攻角組」已退役（鎖鏡頭 3D 貼近皮膚必穿插；git 歷史保留）。---
	bool IsPenMachineMode() const { return bPenIsMachineAsset; }
	bool IsPenTriggerHeldLocal() const { return bPenTriggerLocal; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Input", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float LookSensitivity = 1.6f;

	// 設計基準值 × 玩家偏好倍率（設定選單，GameInstance 持久化）——
	// 所有滑鼠輪詢點（視角／臉指向／畫筆游標）一律經過這裡
	float EffectiveLookSensitivity() const;

	// --- 程式化走路（美術語言：硬轉、突兀即目標；消滅 A-pose 滑行）---
	// 站立移動時 Body 在站姿基準上疊「側傾三角波＋步點彈跳」；停步即硬還原。
	// 全端本地模擬（速度已複寫，無需同步）。不喜歡＝bWalkAnimEnabled 一鍵關。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Walk")
	bool bWalkAnimEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Walk", meta = (ClampMin = "0", ClampMax = "15"))
	float WalkWaddleDeg = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Walk", meta = (ClampMin = "0", ClampMax = "10"))
	float WalkBobCm = 2.5f;

	// --- 骨骼站姿＋摺り足步態（2026-08-04 user 委託：站立/走路全換骨骼身體）---
	// 顯示＝BowBody（骨骼、軟肉可彈跳）；靜態 Body 退居真相載體（碰撞/UV 解算照舊、
	// 站立時恆隱形）。摺り足＝力士滑步：雙腳 Z 恆貼地（構造保證不離地）、
	// 撐地腳世界釘住（步幅=速度/步頻守恆式）、滑步腳沿地滑行；屈膝沉腰、骨盆近水平、
	// 重心左右換腳。骨骼資產缺席時退回舊制雕像搖擺（機制照跑）。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Walk")
	bool bSkeletalStandEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Walk", meta = (ClampMin = "0", ClampMax = "25"))
	float GaitStanceDropCm = 10.0f;   // 移動時髖下沉（屈膝深度；速度斜坡帶入）

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Walk", meta = (ClampMin = "0.5", ClampMax = "8"))
	float GaitStepsPerSecBase = 2.8f; // 起步步頻（每秒滑步數；力士碎步）

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Walk", meta = (ClampMin = "1", ClampMax = "12"))
	float GaitStepsPerSecMax = 7.0f;  // 全速步頻（摺り足=小步快滑；碎步化 08-04 二輪）

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Walk", meta = (ClampMin = "10", ClampMax = "70"))
	float GaitMaxStrideCm = 30.0f;    // 步幅上限（碎步；全速頂到上限=輕微滑冰的知情取捨）

	// 雙軌側帶（08-04 二輪 user 打回「腳統一朝左右擺+穿膜」後的構造保證）：
	// 左腳 X 恆鉗 [Min,Max]、右腳鏡像——兩腳在數學上不可能越中線或互相貼近，
	// 與「腳不離地=Z 恆等於地面」同一等級的保證。ref 腳 X=±33.5。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Walk", meta = (ClampMin = "5", ClampMax = "40"))
	float GaitLatBandMinCm = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Walk", meta = (ClampMin = "30", ClampMax = "80"))
	float GaitLatBandMaxCm = 52.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Walk", meta = (ClampMin = "0", ClampMax = "12"))
	float GaitWeightShiftCm = 3.5f;   // 重心橫移振幅（骨盆壓向撐地腳側；也是彈跳的主激勵）

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Walk", meta = (ClampMin = "0", ClampMax = "5"))
	float GaitBobCm = 0.7f;           // 沉浮（摺り足紀律=近水平；微量供彈跳激勵）

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Walk", meta = (ClampMin = "0", ClampMax = "20"))
	float GaitTorsoLeanDeg = 6.0f;    // 上身向行進方向前傾

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Walk", meta = (ClampMin = "90", ClampMax = "3600"))
	float GaitDirSlewDegPerSec = 600.0f;

	// 站立視野俯仰上身（08-15 user 定案：「抬頭低頭要反映在人偶頭部；左右禁止
	//（穿膜）＝全身一起轉、頭身零相對位移」）：本人相機 pitch 上報 → 他端把
	// Neck+Head 繞頸樞軸俯仰（yaw 恆 0）。站立自由視角限定（睡/鎖各自接管）。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Walk", meta = (ClampMin = "0", ClampMax = "1"))
	float LookPitchHeadShare = 0.6f;   // 俯仰分攤：Head 佔比（其餘給 Neck）
	// 08-15 user 定案：他端最多上下 12°、視野 0~89° 以指數曲線分配進這 12°
	// （小角度反應快、大角度慢慢逼近上限）：head = Max × (1−e^(−k·|p|/89)) / (1−e^(−k))
	// ——p=89 恰等於 Max；k 越大越早飽和（k→0 退化為線性）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Walk", meta = (ClampMin = "0", ClampMax = "60"))
	float LookPitchMaxDeg = 12.0f;     // 他端可見俯仰上限（上下對稱）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Walk", meta = (ClampMin = "0.05", ClampMax = "8"))
	float LookPitchCurveK = 2.5f;      // 指數曲線陡度（2.5：視野 30°≈7.7°、60°≈10.9°、89°=12°）
	UPROPERTY(Replicated)
	float LookPitchDeg = 0.0f;         // 上報值（30Hz 節流、變化 >0.5° 才送）
	float RemoteLookPitchDeg = 0.0f;   // 他端平滑追趕值
	float LookPitchLastReport = -1.0f;
	float LookPitchReportedDeg = 0.0f;
	UFUNCTION(Server, Unreliable)
	void ServerReportLookPitch(float PitchDeg);
	float CurrentLookPitchForPose(float DeltaSeconds);
	void ApplyLookPitchToCS(const FReferenceSkeleton& Ref, TArray<FTransform>& CS, float DeltaSeconds);
	void ApplyStandLookPitch(float DeltaSeconds); // 靜止站姿專用（gait 路徑走 ApplyLookPitchToCS）
	bool bStandLookWritten = false;
	bool bWasHiddenForPose = true;   // 隱形→現身邊緣偵測（現身瞬間強制重寫骨姿＝骨矩陣上傳） // 傾斜軸擺轉角速（前搖節奏旋鈕：180° 反轉≈0.3s；大=俐落、小=黏）

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Walk", meta = (ClampMin = "0", ClampMax = "25"))
	float GaitArmSwingDeg = 7.0f;     // 手臂前後小擺（與同側腳反相）

	// --- 軟肉彈跳（jiggle：胸×2/肚/臀×2 五骨阻尼彈簧＝世界空間錨點追趕）---
	// 骨骼身體可見的所有狀態生效（站走/作畫姿/沉睡替身被翻身）；純視覺、各端本地模擬
	//（姿勢輸入本來就同步，彈跳自己長出來——零複製）。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Jiggle")
	bool bJiggleEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Jiggle", meta = (ClampMin = "0", ClampMax = "5"))
	float JiggleGain = 1.25f;         // 浮誇倍率（1=物理位移原樣；穩態擺幅要留在鉗位內
	                                  //  ——衝擊響應碰頂是要的、穩態頂死=「被吹住」病）

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Jiggle", meta = (ClampMin = "1", ClampMax = "20"))
	float JiggleMaxCm = 8.0f;         // 偏移鉗位（防穿模/防瞬移灌爆）

	// 醉倒期的彈簧增益倍率（2026-08-16 user 抓「倒下時肚子浮誇地形變」）：
	// jiggle 是**世界空間**彈簧，翻倒＝整具網格繞 ~90° 高速掃掠 ⇒ 遠離樞軸的肚骨
	// 被甩出遠超步行調校的激勵——實測崩塌全程 jBelly **貼死 8.00 鉗位**（平均 5.03）
	// ＝彈簧飽和＝繞脊椎轉角撞 25° 上限＝大面積蒙皮形變。舊制看不到是因為它**傳送**
	// 落地，而彈簧有「單幀 >100cm 直接貼齊」的傳送保護（連續移動永遠不觸發）。
	// 0.4＝峰值落回線性域（有晃、不釘鉗位）；1.0＝原樣；0＝倒下完全不晃。
	// **晃多少是口味，交給 viewport**。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Body")
	float JiggleCollapseScale = 0.4f;

	// 08-15 user 抓「頭轉到某角度乳頭沉進乳房」：徑向往體內的偏移不對稱鉗（往外照舊 8cm）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Jiggle", meta = (ClampMin = "0", ClampMax = "8"))
	float JiggleInwardMaxCm = 1.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Jiggle", meta = (ClampMin = "0", ClampMax = "30"))
	float JiggleChestMaxRollDeg = 15.0f; // 胸旋轉耦合轉角上限（原 25°；乳頭繞骨原點內掃）

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Jiggle", meta = (ClampMin = "0.02", ClampMax = "0.9"))
	float JiggleDamping = 0.32f;      // 阻尼比 ζ（低=晃更多下；過低=步頻共振 Q 放大頂鉗位）

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Jiggle", meta = (ClampMin = "0.5", ClampMax = "8"))
	float JiggleBellyHz = 2.1f;       // 肚（最大質量=最慢；貼近步頻=晃最兇，層級刻意）

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Jiggle", meta = (ClampMin = "0.5", ClampMax = "8"))
	float JiggleChestHz = 3.4f;       // 胸（原 2.7 正中全速步頻 2.6Hz 共振＝恆頂鉗位實錘）

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Jiggle", meta = (ClampMin = "0.5", ClampMax = "8"))
	float JiggleButtHz = 3.2f;

	// 收斂終止門檻（2026-08-18）：偏移與相對速度都低於此＝**寫入精確 rest、彈簧歸零**。
	// 指數衰減是漸近的（數學上永不到零）——「等它自己衰完」不是保證，明確終止才是。
	// 靜止態＝逐位 rest 是契約，不是巧合。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Jiggle", meta = (ClampMin = "0.001", ClampMax = "0.2"))
	float JiggleSettleEpsCm = 0.02f;

	UPROPERTY(BlueprintReadOnly, Category = "Nice Ink|Paint")
	int32 SelectedColorIndex = 0;

	// 作者槽位（2026-08-31 防作弊 P0-1；帳本=Docs/ANTICHEAT_PLAN.md §3）：每回合洗牌的
	// 不透明分組鍵——落墨多播的線上識別只走 slot，slot→真名對照只活在 server
	//（受害者的改裝客戶端從此讀不到「這筆是誰畫的」）。COND_OwnerOnly＝只發本人：
	// 本地預測的分組鍵必須與回播同源，別人的 slot 沒有任何客戶端需要知道對應誰。
	UPROPERTY(Replicated)
	int32 DrawSlotId = INDEX_NONE;

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
	// 網格層：頭身沿 user 手標 cut_seam_head3 真切開；銜接＝UNeckStretch 每幀生成。
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

	// （趴下姿態已拆除，2026-07-20 user 定案：覆蓋率量測實證趴姿只多 +3.6% 皮膚
	// （站姿 78.3%→81.9%、理論極限 83.4%）——低區的正解是翻身（F），一顆鍵不值。
	// 量測儀 DebugRoboCoverageScan 與真射線入座探針 DebugRoboLeanEnterFromEye 保留。）

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

	// 直接畫制 aim（2026-07-18）：作畫中滑鼠＝臉指向（az=世界 yaw、tilt=俯仰 0=水平
	// 正=向下）。pattern 同 SleepAim：本人本地零延遲、COND_SkipOwner 複製他端擺姿；
	// 相機恆眉心＋臉向＝「你看得到的≡你的臉表達的≡旁人讀到的」。偷瞄 Shift 已退役——
	// 抬頭看他睜眼沒＝把準星從肚皮移到他臉上，窄視野下自然發生。
	UPROPERTY(BlueprintReadWrite, Replicated, Category = "Nice Ink|Lean")
	float DrawAimAzDeg = 0.0f;

	UPROPERTY(BlueprintReadWrite, Replicated, Category = "Nice Ink|Lean")
	float DrawAimTiltDeg = 45.0f;

	// 作畫目標點 P＝擁有端真相（07-26 抖動根治）：他端不再自己 trace P——本地 trace
	// 用「追趕中的 aim＋他端解出的眼位」重推，貼剪影邊緣間歇 miss＝整身甩姿閃爍、
	// 針長鞭打、筆尖與墨兩套真相。改制＝P 隨 aim 上報，他端追趕本複製值＝與墨同源。
	UPROPERTY(Replicated)
	FVector_NetQuantize DrawTargetRepW;

	UPROPERTY(Replicated)
	bool bDrawTargetRepValid = false;

	UFUNCTION(Server, Unreliable)
	void ServerUpdateDrawAim(float AzDeg, float TiltDeg, FVector_NetQuantize TargetW, bool bTargetValid);

	// 無聲甦醒（定案 #8）：走出迷宮出口後睜眼。零系統提示——
	// 其他玩家能觀察到的破綻＝睜眼貼圖＋頭部轉動（睡姿替身驅動，2026-07-15）。
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_EyesOpen, Category = "Nice Ink")
	bool bEyesOpen = false;

	// 裝睡（2026-07-17 使用者定案）：無聲甦醒後按住 Shift＝回到沉睡的姿勢＋閉眼貼圖
	//（對旁人＝還沒醒來的樣子）；放開＝回到按下前的頭部位置與朝向。
	// 實作＝指向凍結而非存/還原：按住期間滑鼠不寫入臉指向，狀態根本沒動過，
	// 放開自然復原（最少狀態＝最少錯）。本人畫面＝全黑（定案 #42 恆等式：
	// 你看得到的≡臉表達的——裝睡不是免費監視器，何時敢重新睜眼本身是賭注）。
	// 切換硬切不補間（美術語言定案 #24）。
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_FeignSleep, Category = "Nice Ink")
	bool bFeignSleep = false;

	UFUNCTION(Server, Reliable)
	void ServerSetFeignSleep(bool bNewFeign);

	// 有效裝睡值（本人＝本地鏡像零延遲；他端＝複製值）；域鉗在 沉睡×睜眼 之內
	UFUNCTION(BlueprintPure, Category = "Nice Ink")
	bool IsFeigningSleep() const;

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

	// server-only 睜眼（P0-3）：兩條甦醒 RPC 驗過時間下限後、以及 robo 的
	// DebugRoboWake（server 決定＝合法繞過下限）都走這一個入口。
	// 睜眼當幀強制全角色 net update——P0-2 的凍結不能等 relevancy 節拍自然回流。
	void ServerOpenEyesNow();

	// P0-1：multicast 落地時的畫布分組鍵——server 端把線上 slot 換回真名
	//（判定/持久化讀 server 畫布），客戶端原樣用 slot。
	int32 ResolveCanvasAuthorKey(int32 WireId) const;

	// --- 伺服器端流程控制（GameMode 呼叫） ---

	// 入睡：鎖移動、閉眼、身體躺到指定位置（仰躺大字，定案 #18）。
	// 甦醒現身：站回座位、睜眼、恢復移動。
	// bAlreadyLying（2026-08-16 儀式版）＝崩塌動畫已經把身體放到躺位 ⇒ **跳過傳送**：
	// 動畫終點與 LieTransform 逐位相同，睡姿接管當幀零跳變（零硬切的最後一哩）。
	void ServerSetAsleep(bool bNewAsleep, const FTransform& LieTransform, bool bAlreadyLying = false);

	// --- 入睡儀式（2026-08-16；所有端都跑，視覺＝GameState 複製參數的純函式）---

	// 儀式期間？（＝輸入全停的單一閘門，不散落在各 Poll）
	UFUNCTION(BlueprintPure, Category = "Nice Ink|Ceremony")
	bool IsCeremonyActive() const;

	// 本人是這段儀式的主角（要走去拿瓶、喝、倒下）？
	bool IsCeremonyVictim() const;

	// 酒瓶握在手上時的世界變換（瓶子每 tick 來問；姿勢層是唯一真相）
	bool GetCeremonyBottleTransform(FTransform& Out) const;

	// 儀式驅動（Tick 呼叫）：走位（合成輸入）＋姿勢＋崩塌
	void UpdateCeremony(float DeltaSeconds);

	// 開場動畫（2026-08-27）：盤腿坐（SitBones＝user 手擺、七月入庫、今日首個消費者）
	// ＋房主舉刺青機（右臂 IK 疊在坐姿 CS 上）。全員同式；坐→站不插值＝剪接跳過。
	void ApplyIntroSitPose(const class ANiceInkGameState* GS, float DeltaSeconds);

	// 儀式姿勢（屈膝/前彎/臂 IK/頭俯仰/翻倒）——BowBody 骨骼載體，與步態互斥
	void ApplyCeremonyPose(float DeltaSeconds);

	// robo：儀式狀態機讀摘要
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	FString DebugRoboCeremonyStats() const;

	// robo：固定世界機位＋注視點（截圖矩陣；ViewFrom 恆盯 actor 自己）
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugRoboViewAt(float CX, float CY, float CZ, float LX, float LY, float LZ);

	// 持物骨（瓶子附著點）——不依賴作畫的一次性校準
	FName CeremonyGripBoneName() const;

	// --- 儀式姿勢旋鈕（viewport 即調）---
	// 拾取值由實測反推：初版 standoff 58／bend 52／crouch 24 ⇒ 肩到瓶頸 96.4cm
	// 而臂長只有 78.0cm（探針數字），差 18cm＝手永遠搆不到。
	// 2026-08-27 蹲踞（そんきょ）改制：彎 64° 是七月已判死刑的幾何（SPEC #44
	// 「sumo 體型站立深彎＝肚腹蒙皮塌陷」）在八月被重新引進——姿勢域掃描定罪：
	// 軀幹前彎預算 10~15°、64° 時 428 條摺＋褌被頂穿 4357 處；而膝是免費的
	// （120° 摺疊≈0）。修法＝**高度從膝出、上身近直立**（力士不彎腰撿東西，
	// 蹲踞本來就是相撲自己的儀式姿勢——題材正解與技術正解重合）。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Ceremony")
	float CeremonyStandoffCm = 28.0f;   // 站到瓶邊的距離（蹲踞手臂行程短、站近一點）

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Ceremony")
	float CeremonyPickBendDeg = 14.0f;  // 上身前彎（掃描安全域 10~15° 內）

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Ceremony")
	float CeremonyPickCrouchCm = 52.0f; // 屈膝下沉（腳釘地＋腿 IK ⇒ 深屈膝＝蹲踞）

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Ceremony")
	float CeremonyDrinkBendDeg = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Ceremony")
	float CeremonyDrinkCrouchCm = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Ceremony")
	float CeremonyLookDownDeg = -14.0f; // 低頭看瓶（蹲踞制：頭留在脖子解算器驗證域內）

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nice Ink|Ceremony")
	float CeremonyHeadBackDeg = 32.0f;  // 仰頭灌酒
	// 08-15 甦醒朝向競態封死（user 抓「醒來全員鏡像到對側」再現、PIE 重現不出）：
	// 睡姿 yaw 改成**複製屬性**（冪等、與封包順序無關），owner client 在 bAsleep 期間
	// 每 tick 斷言 actor yaw／控制器 yaw＝此值——一次性 RPC 寫入不管誰先到、
	// 之後被什麼拉歪，都在下一幀扶正。
	UPROPERTY(Replicated)
	float SleepLieYawDeg = 0.0f;
	UPROPERTY(Replicated)
	bool bSleepLieYawValid = false;

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

	// 伺服器端強制起身（被踹飛、相位切換）
	void ForceExitLean();

	// --- 畫墨 RPC（作畫者 → 伺服器） ---

	// Flow/Flows＝逐針出墨流量 0–255（手速→濃淡；Flows 與 UVs 逐索引對齊，
	// 空陣列=全滿濃度——液線針恆走空陣列省頻寬）
	// StrokeSeq（07-26 本地預測）：作畫者客戶端遞增的筆劃序號——回播對消用；
	// 0＝非玩家路徑（robo/GameMode 直呼），永不對消
	UFUNCTION(Server, Reliable)
	void ServerPaintBegin(ANiceInkCharacter* Target, int32 ColorIndex, FVector2D UV, EInkNeedle Needle, uint8 Flow, int32 StrokeSeq);

	UFUNCTION(Server, Reliable)
	void ServerPaintPoints(const TArray<FVector2D>& UVs, const TArray<uint8>& Flows);

	UFUNCTION(Server, Reliable)
	void ServerPaintEnd();

	// --- 畫墨重播（被畫角色 → 所有端） ---

	// bDotStroke：玩家路徑（ServerPaintBegin）恆 true＝點刺筆劃；
	// robo 線畫（GameMode DebugRoboStroke）傳 false 保留折線語義。Needle=針型（07-23）；
	// Flow/Flows=逐針流量（十二版；空陣列=全滿濃度）
	// StrokeSeq≠0＝作畫者客戶端已本地預測整條筆劃——該端跳過自己的回播（見實作）
	UFUNCTION(NetMulticast, Reliable)
	void MulticastPaintBegin(int32 AuthorId, FLinearColor Color, FVector2D UV, bool bDotStroke,
		EInkNeedle Needle, uint8 Flow, int32 StrokeSeq);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPaintPoints(int32 AuthorId, const TArray<FVector2D>& UVs, const TArray<uint8>& Flows);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPaintEnd(int32 AuthorId);

	// --- 規則操作重播（GameMode 經由受害者角色廣播） ---

	UFUNCTION(NetMulticast, Reliable)
	void MulticastConvertWorkToCarbon(int32 WorkId);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastWashAllMarker();

	// 打稿制（07-25）：甦醒收束＝稿線全洗（GameMode EnterTour 呼叫）
	UFUNCTION(NetMulticast, Reliable)
	void MulticastWashStencil();

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

	// --- 醉夢描圖 RPC（SPEC v4.0 定案 #49/#50）---
	// 信任模型沿用迷宮：client 判定（越線/完成）、server 驗身分/相位（party game 取捨）。

	// 回合開始（EnterSeating）：server 發種子＋難度檔，受害者端決定性重建圖形
	UFUNCTION(Client, Reliable)
	void ClientStartTrace(int32 Seed, const FDreamTraceParams& Params);

	// 描完整條路線＝無聲甦醒（等同舊 ServerMazeExited 語意）
	UFUNCTION(Server, Reliable)
	void ServerTraceComplete();

	// 搖晃攻擊（作畫者 → 伺服器）：花錢搖受害者夢中的圖（server 驗相位/現金/冷卻）
	UFUNCTION(Server, Reliable)
	void ServerAttackShake();

	// 受害者端：套用搖晃（攻擊者顯名＝怒氣要有地址；其餘玩家一無所知）
	UFUNCTION(Client, Reliable)
	void ClientApplyShake(const FString& AttackerName, float Seconds, float AmpCm);

	// 攻擊者端回執（買到/被拒的音效與 HUD 閃示；不透漏受害者夢內結果）
	UFUNCTION(Client, Reliable)
	void ClientShakeAck(bool bBought);

	// HUD 讀取：最近一次搖晃購買回執的閃示時鐘
	float ShakeAckFlashUntil = 0.0f;
	bool bLastShakeAckBought = false;

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

	// --- 雲端隨身資產搬運（B3；EOS PlayerDataStorage）---
	// 位元組列車：UNiceInkSaveGame 序列化 bytes 分塊過網（16KB/塊＋CRC 驗收）。
	// 上行（進房）＝owner client 把自己雲端的資產交給 server 套用；
	// 下行（結算）＝server 把最新資產發回本人、由本人客戶端寫自己的雲端保險箱
	// （PlayerDataStorage 私人不可代寫＝只能經本人）。LAN/PIE 全程不走這裡。

	UFUNCTION(Server, Reliable)
	void ServerPersonaBegin(int32 TotalBytes);
	UFUNCTION(Server, Reliable)
	void ServerPersonaChunk(int32 Offset, const TArray<uint8>& Bytes);
	UFUNCTION(Server, Reliable)
	void ServerPersonaEnd(uint32 Crc);

	UFUNCTION(Client, Reliable)
	void ClientPersonaBegin(int32 TotalBytes);
	UFUNCTION(Client, Reliable)
	void ClientPersonaChunk(int32 Offset, const TArray<uint8>& Bytes);
	UFUNCTION(Client, Reliable)
	void ClientPersonaEnd(uint32 Crc);

	// server 端便利：把 bytes 切塊發下行列車給 owner
	void SendPersonaToOwner(const TArray<uint8>& Bytes);

	// --- 自訂臉房內分發（2026-08-10；登記簿＝UNiceInkFaceShare）---
	// 上行：owner client 把本機臉工件 blob（NIF1 格式）分塊交給 server；
	// 下行：server 把「席位 Seat 的臉」分塊發給本角色的 owner client（viewer）。
	// LAN 與 EOS 同路（臉走房內 P2P 不走雲端）；PIE/robo（WorldType≠Game）不啟動。

	UFUNCTION(Server, Reliable)
	void ServerFaceHello(bool bHasCustomFace); // viewer 報到：server 補發所有已知臉；無臉端（開發沙箱）＝即刻 FaceReady
	UFUNCTION(Server, Reliable)
	void ServerFaceBegin(int32 TotalBytes, FLinearColor Tone); // tone 先行（08-14）：膚色不等列車
	UFUNCTION(Server, Reliable)
	void ServerFaceChunk(int32 Offset, const TArray<uint8>& Bytes);
	UFUNCTION(Server, Reliable)
	void ServerFaceEnd(uint32 Crc);

	UFUNCTION(Client, Reliable)
	void ClientFaceBegin(int32 Seat, int32 TotalBytes, FLinearColor Tone); // tone 先行（08-14）
	UFUNCTION(Client, Reliable)
	void ClientFaceChunk(int32 Seat, int32 Offset, const TArray<uint8>& Bytes);
	UFUNCTION(Client, Reliable)
	void ClientFaceEnd(int32 Seat, uint32 Crc);

	// --- 進房載入閘（08-14；veil＝臉同步齊全前不掀）---
	UFUNCTION(Client, Reliable)
	void ClientFaceManifest(const TArray<int32>& Seats); // 報到當下 server 既有的臉清單＝veil 等待集
	UFUNCTION(Client, Reliable)
	void ClientFaceUpAck(); // 本人臉上行已被 server 收妥

	// HUD 進房載入布判準：本人 client 在 Game 世界、臉同步未齊＝true（8s 保底掀開；
	// host/PIE/robo 恆 false）。一次滿足即閂死＝永不中途復發
	bool IsJoinFaceSyncPending();

	// 臉齊現身閘（08-14 三修＝單一權威制）：bHidden 只有 server 寫——玩家 pawn
	// spawn 即隱形，GameMode 等「當下所有觀看者都 ack 收到他的臉」才 FaceGateShowNow
	// ＝任何端都不可能看到頂著名冊臉的力士（雙寫者競態根絕；晚到者自己的視角由
	// veil 蓋住）。非玩家角色（頭像亭替身/選單舞者＝無 PlayerState）不閘。
	bool bFaceReady = false; // server-only：開局臉齊保險/HUD 主機提示讀
	bool bFaceNone = false;  // server-only：無 blob 可等（無臉端/失敗保底）＝即刻現身
	void ServerSetFaceReady(bool bNoBlobFallback = false);
	void FaceGateShowNow(); // server：全房都拿到他的臉了——現身（冪等）
	UFUNCTION(Server, Reliable)
	void ServerFaceGotSeat(int32 Seat); // 觀看者回報：席位 Seat 的臉已入本端登記簿

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

	// HUD 用：鎖定中的麥克筆游標位置（直接畫制＝恆為螢幕中心）
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

	// 主選單舞台替身（2026-08-06）：無 PlayerState 的展示用角色——直接指定 avatar
	//（正常入局走 EnsureAvatarApplied 讀 PlayerState，這裡繞過）
	void SetupAsMenuDummy(int32 AvatarIdx);

	UFUNCTION(Exec)
	void NiJoin();

	UFUNCTION(Exec)
	void NiEmerge();

	// 指認巡禮清單中第 WorkNumber 幅（1 起算）的作者為第 SeatIndex 席玩家
	UFUNCTION(Exec)
	void NiAccuse(int32 WorkNumber, int32 SeatIndex);

	// 缺陷圈選定位（2026-08-22）：把全場每個身體上的筆劃（UV＋世界座標）傾印到
	// Saved/ni_marks.txt。用法：用紫筆把缺陷圈起來→主控台跑 NiMark→座標進檔
	// ＝量測與修理釘死在圈上（「修的」與「看到的」不再靠截圖猜對齊）。
	UFUNCTION(Exec)
	void NiMark();

	// 迷宮生成器離線統計：NumSeeds 個種子的通關時間/繞路成本分布＋移動 fuzz 自測。
	// 調參前先開表（SPEC 待定 #2 的校準儀器）；結果進 log。
	UFUNCTION(Exec)
	void NiMazeStats(int32 NumSeeds, int32 Cup);

	// robo：模擬沉睡受害者的頭控（本地受害者於下一 tick 消化；滑鼠不可注入）。
	// 睜眼＝(Yaw,Pitch) 設臉指向 (az,tilt)（走真實 ServerUpdateSleepAim 鏈、tilt 過鉗位）；
	// 閉眼＝(Twist,Bend) 直設盲瞄狀態（骨不動，驗噴射 yaw）。
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugRoboSleepLook(float Yaw, float Pitch);

	// robo：模擬裝睡 Shift 按住/放開（本地受害者下一 tick 消化；RPC 逃出 python guard）
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugRoboFeignSleep(bool bFeign);

	// robo：模擬按住移動鍵朝世界方向走 Seconds 秒（本地 PollMove 消化——與真鍵同一入口）
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugRoboWalk(float WorldDirX, float WorldDirY, float Seconds);

	// robo：步態/彈跳機讀摘要（腳貼地/撐地腳釘住/髖沉/五骨偏移——gait 探針斷言用）
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	FString DebugRoboGaitStats() const;

	// robo：第三人稱側視相機開關（步態截圖矩陣用；false=還原本體視角）
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugRoboSideView(bool bEnable);

	// robo：任意方位觀察相機（actor+世界偏移、看向 actor）——正面/側面截圖矩陣用
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugRoboViewFrom(float DX, float DY, float DZ);

	// robo：直設作畫臉指向（本地作畫者下一 tick 消化；滑鼠不可注入）
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugRoboDrawAim(float AzDeg, float TiltDeg);

	// robo：模擬按住左鍵下筆（與真鍵 OR、由 PollLockedDraw 同一條輪詢消化——
	// 直設狀態會被輸入輪詢反殺，裝睡老教訓）
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugRoboPaintHold(bool bHold);

	// robo：合成滑鼠增量（稿筆游標制的真人管線探針——角度命令會 relatch 游標＝
	// 測不到增益/漂移；此鉤子走與真滑鼠同一條 UpdateStencilCursor）
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugRoboMouse(float DX, float DY);

	// HUD：稿筆游標的世界錨（P=墨的落點＝單一真相；P 無效=沿 aim 射線基準距離）。
	// 只在本人稿筆鎖定中回 true——HUD 以 Canvas->Project 投影為 2D 筆/小點/✕ 錨
	bool GetStencilCursorHudWorld(FVector& Out) const;

	// HUD：割線自由游標的皮膚命中點（08-04 二輪修；巡航中才回 true）——
	// 十字記號＝「你指的目標點」，虛線盡頭與它重合
	bool GetTattooCursorHudWorld(FVector& Out) const;

	// HUD：稿筆拉繩墨尖的世界錨（按住 LMB 繪製中才回 true）——2D 筆錨到墨尖
	// =「筆尖在墨出處」的誠實呈現；游標小點/✕ 照舊錨生游標（恆隨手）
	bool GetStencilLazyTipHudWorld(FVector& Out) const;

	// robo：模擬方向拉桿（巡航中每 tick 重申覆寫、免疫滑鼠歸零；(0,0)=解除。
	// 必須在 PaintHold(true) 之後呼叫——非巡航 tick 會把拉桿歸零）
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugRoboPaintStick(float X, float Y);

	// robo：直設針型（0=Liner/1=Shader）——工具狀態非輪詢管、直設安全
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugRoboNeedle(int32 NeedleIndex);

	// robo：直設調色盤選色（robo 無法注入數字鍵；走與 PollPalette 同一條
	// per-stroke 語義=換色即重開筆劃）
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	void DebugRoboColor(int32 ColorIndex);

	// robo：沿法線 trace 皮膚表面點後走真 ServerEnterLean（真流程＝準星 trace；
	// python 硬編體內錨點會把 10cm 眼位埋進肉裡；5.7 python 的 HitResult 反射不可用）
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	bool DebugRoboEnterLean(ANiceInkCharacter* Target, FVector Anchor, FVector Normal);

	// robo：從「當前真實眼位」朝 AimPoint 打入座射線——驗「站在外面點不點得到」
	// 這一段（DebugRoboEnterLean 先傳送到點旁＝跳過此段）。
	// 回傳 "HIT z=<命中高> err=<離目標> eyez=<眼高>"；bEnter=true 時並走真 ServerEnterLean
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	FString DebugRoboLeanEnterFromEye(ANiceInkCharacter* Target, FVector AimPoint, bool bEnter);

	// robo：覆蓋率量測儀——受害者皮膚 UV 網格取樣（UV0=均勻紋素密度⇒樣本數∝表面積），
	// 每點從五種眼高（35/60/98=趴/130/156=站）×方位扇×距離的眼位集合打真入座射線
	//（同 PollLeanEnter 語義：complex trace、命中=受害者皮膚 2.5cm 內、射程≤295）。
	// 回傳 "total= down= low15= any= sp= h35= h60= h98= h130= h156="——
	// 「站/趴到底點得到多少、理論極限在哪」的量測答案
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	FString DebugRoboCoverageScan(ANiceInkCharacter* Target, int32 GridN);

	// 距離圓半徑量測（07-29 圈域制前置調查）：對受害者 UV0 網格逐點問可見性＋
	// 可解性，回報以鎖點為心的半徑統計——R100=最近不可解點距離（嚴格圈上限）、
	// R95=圈內可解率 ≥95% 的最大半徑、最近 5 個不可解點距離（孤立噪聲 vs 真邊界）。
	// 需在 lean-lock 中且眼錨定已成立時呼叫。
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	FString DebugRoboReachStats();

	// robo：直接畫制狀態（機器可讀）——鎖定/aim/相機/筆尖/ghost 數
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	FString DebugLeanSummary() const;

	// ghost 還原用：從自己的真相重建全部網格件材質（Body=MID 重綁、BowBody=皮膚 MID、
	// 其餘=資產預設）。被別人 ghost 過後由對方呼叫；冪等。
	void ReapplyCanonicalMaterials();

	// robo：以當前 aim 走真落墨鏈（中心射線→trace→UV）。回傳 "HIT u v d=<cm>" 或 "MISS <原因>"
	UFUNCTION(BlueprintCallable, Category = "Nice Ink|Debug")
	FString DebugRoboCanvasResolve(float ScreenFracX, float ScreenFracY) const;

	UFUNCTION(Server, Reliable)
	void ServerRequestStartMatch();

	static ANiceInkCharacter* FindByPlayerId(UWorld* World, int32 PlayerId);

	// --- ESC 系統選單（本地、不複寫；開著時吞掉全部遊戲輸入、放出滑鼠游標）---
	bool IsSystemMenuOpen() const { return bSystemMenuOpen; }
	void SetSystemMenuOpen(bool bOpen);

private:
	float CameraPitch = 0.0f;
	int32 AppliedAvatarIndex = INDEX_NONE;

	// --- 雲端隨身資產搬運內部狀態（B3）---
	// server 端上行收件緩衝（每角色一份；PersonaUpExpected<0＝未開始/已拒收）
	TArray<uint8> PersonaUpBuf;
	int32 PersonaUpExpected = -1;
	int32 PersonaUpReceived = 0;
	// client 端下行收件緩衝
	TArray<uint8> PersonaDownBuf;
	int32 PersonaDownExpected = -1;
	int32 PersonaDownReceived = 0;
	// client 端進房上行：等 Persona 雲端拉取完成再發車（0.5s 輪詢、10s 放棄）
	FTimerHandle PersonaUploadTimer;
	int32 PersonaUploadTicksLeft = 0;
	bool bPersonaUploadDone = false;
	void MaybeUploadPersona();

	// --- 自訂臉房內分發內部狀態（2026-08-10）---
	int32 AppliedShareFaceRev = 0;   // 已套用的登記簿臉版本（0=尚未）
	int32 AppliedShareToneRev = 0;   // 已套用的提前 tone 版本（0=尚未；tone 先行制）
	TArray<uint8> FaceUpBuf;         // server 端上行收件緩衝
	int32 FaceUpExpected = -1;
	int32 FaceUpReceived = 0;
	TArray<uint8> FaceDownBuf;       // client 端下行收件（server 逐席序列發送＝單緩衝）
	int32 FaceDownSeat = -1;
	int32 FaceDownExpected = -1;
	int32 FaceDownReceived = 0;
	bool bGaitPhaseSeedPending = false; // 起步時按橫向速度選開步腳（08-15 修「往右先左傾」）

	FTimerHandle FaceShareTimer;     // 開場輪詢：等佔有＋席位就緒
	int32 FaceShareTicksLeft = 0;
	bool bFaceShareStarted = false;
	// 進房載入閘（owner client 本地狀態）
	TArray<int32> JoinFaceWaitSeats; // manifest：要等的席位
	bool bFaceManifestRecv = false;
	bool bFaceUpAcked = false;
	bool bJoinFaceSyncDone = false;  // 一次滿足即閂死
	double JoinFaceSyncStartS = -1.0;
	bool bFaceGateShown = false; // server：現身閘已放行（latch；host 本人/非玩家首 tick 即放）
	TSharedPtr<TArray<uint8>> FaceUpSendBuf; // 上行節奏發送（防 reliable 緩衝溢位）
	int32 FaceUpSendOff = 0;
	FTimerHandle FaceUpSendTimer;
	void MaybeStartFaceShare();
	void TickFaceUpload();

	bool bSystemMenuOpen = false;
	void PollSystemMenu(APlayerController* PC);

	// 程式化走路內部狀態（本地）
	float WalkAnimPhase = 0.0f;
	bool bWalkAnimApplied = false;      // 舊制雕像搖擺（骨骼資產缺席 fallback）用
	bool bStandDoubleActive = false;    // 骨骼站姿顯示中（BowBody=站走顯示載體）
	float GaitStanceAlpha = 0.0f;       // 屈膝深度混成（速度驅動線性斜坡）
	FVector GaitSlideDirCS = FVector::YAxisVector; // 滑步方向（CS；平滑追隨速度向）
	bool bGaitIdleWritten = false;      // 停步歸位姿勢已寫（不重複寫骨）
	FVector GaitPrevFootW[2] = { FVector::ZeroVector, FVector::ZeroVector };
	float GaitFootSpeed[2] = { 0.0f, 0.0f }; // 腳世界速度（探針「撐地腳釘住」斷言）
	bool bGaitPrevFootValid = false;
	void UpdateWalkAnim(float DeltaSeconds);
	void UpdateLegacyStatueWalk(float DeltaSeconds); // 骨骼資產缺席的退路（舊制原樣）
	void ApplyGaitPose(float DeltaSeconds, float Speed2D); // 摺り足擺骨（CS 全身組合＋腿 IK）

	// --- 入睡儀式的連續狀態量（2026-08-16）---
	// **零硬切的實作定義**：這些量全部由 t∈[0,1] 的緩動曲線導出，且崩塌結束時
	// 前五項恆為 0、翻倒項恆為 1 ⇒ 交給睡姿替身的當幀，BowBody 骨姿＝ref pose、
	// 相對變換＝BodyLieRel*，與 UpdateSleepBodyDouble 首幀寫入的值**逐位相同**。
	float CeremBendDeg = 0.0f;        // 上身前彎（Spine/Spine1 分攤——單骨深彎摺爆肚子）
	float CeremCrouchCm = 0.0f;       // 髖下沉（腿二骨 IK；腳目標原地＝貼地構造保證）
	float CeremArmAlpha = 0.0f;       // 右臂 IK 權重（0＝rest 手臂、1＝手在目標點）
	FVector CeremHandTargetW = FVector::ZeroVector; // 右手世界目標（瓶頸／嘴）
	float CeremHeadPitchDeg = 0.0f;   // 頭頸俯仰（Neck/Head 分攤，同 ApplyLookPitchToCS）
	float CeremToppleAlpha = 0.0f;    // 站→躺的剛體插值（0=站、1=躺）
	bool bCeremonyPoseActive = false; // BowBody 已切給儀式（進出時重置殘留）
	// 酒瓶交接＝**捕捉當幀的相對變換**（KeepWorldTransform 的等價物）：
	// 之後 bottleWorld = LocalT * gripWorld ⇒ 交接那一幀世界變換逐位不變＝零跳變。
	// 用「握姿假設」去擺瓶子會在交接幀跳一下（瓶子躺著、手的持物軸朝下）。
	FTransform CeremBottleLocalT = FTransform::Identity;
	bool bCeremBottleLocalValid = false;
	TWeakObjectPtr<class ANiceInkBottle> CeremBottle;
	FTransform CeremLieTransformCache;   // 崩塌終點（＝GetVictimLieTransform，server 取得後各端沿用）
	bool bCeremLieValid = false;
	FVector GetCeremonyMouthWorld(float Alpha) const; // 手抬到嘴前的目標點
	FVector CeremCollapseFromLoc = FVector::ZeroVector; // 崩塌起點（實際位置，不是理論值）
	float CeremCollapseFromYaw = 0.0f;
	bool bCeremCollapseCaptured = false;
	// 走位卡住偵測（0.4s 無進展 → 垂直方向微調 0.3s；探針實測席位 0 的路徑擦過長凳）
	FVector CeremLastWalkPos = FVector::ZeroVector;
	float CeremStuckSeconds = 0.0f;
	float CeremSideStepSeconds = 0.0f;
	float CeremSideStepSign = 1.0f;
	void UpdateCeremonyWalk(float DeltaSeconds, const class ANiceInkGameState* GS);

public:
	// 儀式起手時的席位變換（server 記；現身要站回這裡，不是圈心）
	FTransform CeremonySeatTransform;
	bool bCeremonySeatValid = false;

private:

	// 軟肉彈跳內部狀態（世界空間彈簧；各端本地）
	struct FJiggleBoneState
	{
		FVector PosW = FVector::ZeroVector;          // 質點位置（世界）
		FVector VelW = FVector::ZeroVector;
		FVector LastAnchorW = FVector::ZeroVector;
		FVector LastWrittenCS = FVector::ZeroVector; // 上次寫入骨位（判斷姿勢層是否重寫）
		FVector LastOffsetCS = FVector::ZeroVector;  // 上次疊加平移（還原基準用）
		FQuat LastWrittenRotCS = FQuat::Identity;    // 上次寫入骨旋（同上，旋轉耦合用）
		FQuat LastDeltaRotCS = FQuat::Identity;      // 上次疊加旋轉（還原基準用）
		float LastSpringCm = 0.0f;                   // 彈簧等效位移（探針統計；肚骨改旋轉後
		                                             //  骨位移≈0，量測面要看這個）
		bool bValid = false;
	};
	FJiggleBoneState JiggleStates[5];
	void UpdateJiggleBones(float DeltaSeconds);

public:
	// 立刻回到 rest（2026-08-18）：把本層疊上去的偏移/旋轉從骨頭扣回去、彈簧狀態歸零。
	// 「彈跳結束＝身體各部位在原位」是契約——凡是流程要求身體當場穩定的點
	//（入睡、換網格、隱藏）都經這裡，不再各自 bValid=false（那只在同 tick 剛好有
	// ResetBowBodyBones 時才安全＝隱形的順序依賴）。
	void SettleJiggleNow();
private:

	// robo 走路注入/側視相機
	FVector2D DebugWalkDirWorld = FVector2D::ZeroVector;
	float DebugWalkEndTime = -1.0f;
	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> DebugSideCam;

	// ghost 材質佔用旗標（ApplyGhostView 設、ReapplyCanonicalMaterials 清）——
	// 站姿的皮膚 MID 晚綁防護不得覆蓋 ghost 半透明
	bool bGhostMaterialApplied = false;

	// tick 順序寫死用（08-25）：已掛上 prerequisite 的控制器（換人時重掛）
	TWeakObjectPtr<AController> TickPrereqController;

	// 作畫中（本地端）
	bool bPainting = false;
	TWeakObjectPtr<ANiceInkCharacter> PaintTarget;
	// 本地預測（07-26 網路遲鈍根治）：客戶端落墨當幀直接蓋本地 RT，不等 server
	// 來回；LocalStrokeSeq 隨每次開筆遞增、經 ServerPaintBegin 進 multicast——
	// 作畫者自己的回播整條對消（否則半透明針重複蓋章=變深）。listen 主機不預測
	//（multicast 同幀本地執行、已零延遲）。代價記帳：server 拒收/令牌桶裁針時
	// 本人比他端多幾針（相位邊緣競態/外掛才會發生），回合結算全洗歸零。
	int32 LocalStrokeSeq = 0;
	// 被畫角色端：目前「回播跳過中」的作者集合（Begin 進、End 出；reliable RPC
	// 同 channel 有序＝狀態機安全）
	TSet<int32> ReplaySkipAuthors;
	TArray<FVector2D> PendingPoints;
	TArray<uint8> PendingFlows;    // 與 PendingPoints 逐索引對齊（手速→濃淡）
	uint8 ComputeMistFlowByte() const; // EMA 手速→流量因子（Shader 專用；Liner=255）
	void FlushPendingPoints(); // 分塊 ≤200/RPC（server 單批上限 256）
	float PointFlushTimer = 0.0f;
	bool bEmergeRequested = false;

	// --- 稿筆摩擦聲（08-05；本地端專屬）：marker_loop 速度調變 ---
	// 真麥克筆手感規則：筆沒動就沒聲音（LMB 按住也一樣）、動快變大聲、停頓由
	// EMA 自然滑向靜音。閘＝LMB 按住（非筆劃開著——失去接觸閘的收/開筆閃爍
	// 不得切碎聲音）；稿筆無任何落筆 one-shot（08-05 user 裁決）。loop 只給本人。
	UPROPERTY(Transient)
	TObjectPtr<class UAudioComponent> MarkerLoopComp;
	FVector MarkerSfxLastTip = FVector::ZeroVector;
	bool bMarkerSfxHasLastTip = false;
	float MarkerSfxSpeedEmaCmS = 0.0f;
	// 此筆尖速度（cm/s）＝滿音量；速度→音量走 sqrt（慢工細描仍可聞）
	UPROPERTY(EditAnywhere, Category = "Nice Ink|Audio", meta = (ClampMin = "1", ClampMax = "60"))
	float MarkerSfxRefSpeedCmS = 8.0f;
	UPROPERTY(EditAnywhere, Category = "Nice Ink|Audio", meta = (ClampMin = "0", ClampMax = "1"))
	float MarkerSfxVolume = 0.7f;
	void UpdateMarkerSfx(float DeltaSeconds);

	// 伺服器端：此玩家目前畫在誰身上（Points/End RPC 的路由目標）
	TWeakObjectPtr<ANiceInkCharacter> ServerPaintTarget;

	// 伺服器端出墨驗速（07-22）：點數令牌桶——覆蓋率如今被時間定價（v_max=k·d·f），
	// 改裝客戶端不得偷速。按「針數/秒 ≤ TattooDotHz×1.25」限流（數量=面積的真幣；
	// UV 距離驗速會被跨縫合法大跳誤傷，不用）。超額點裁掉不轉發。
	float ServerPaintDotBudget = 0.0f;
	float ServerPaintLastRefill = 0.0f;
	EInkNeedle ServerPaintNeedle = EInkNeedle::Liner; // 桶補充率分針（shader 4×1.5 vs liner 12×1.5）

	// 站姿席位（入睡時記下，現身時站回來）
	FTransform SeatTransform;

	UFUNCTION()
	void OnRep_Asleep();

	UFUNCTION()
	void OnRep_EyesOpen();

	UFUNCTION()
	void OnRep_FeignSleep();

	UFUNCTION()
	void OnRep_Blinded();

	UFUNCTION()
	void OnRep_FaceDown();

	UFUNCTION()
	void OnRep_Lean();

	// ApplyBowPose 的基準姿 CS 組合與收斂寫入（拆出＝07-20 趴姿戰役遺產，趴姿已移除）。
	// VerifyBones 必須含最深鏈尾（雙手）——收斂逐層傳播、驗淺骨=半收斂手臂
	void ComposeLeanBaseCS(const FReferenceSkeleton& Ref, TArray<FTransform>& OutCS) const;
	void ComposeSitBaseCS(const FReferenceSkeleton& Ref, TArray<FTransform>& OutCS) const;
	bool WriteBowPoseConverged(const FReferenceSkeleton& Ref, const TArray<FTransform>& CS,
		const TArray<FName>& VerifyBones);

	// --- 貼臉鎖定內部 ---

	FVector2D LeanCursorPx = FVector2D::ZeroVector; // 虛擬麥克筆游標（螢幕像素）
	float LeanLockTime = 0.0f;                      // 鎖定起始（鏡頭到位前不落筆）
	bool bLeanCamActive = false;                    // 鎖定中本體相機被世界寫入接管（退鎖要還原掛點）

	// --- 直接畫制內部（2026-07-18）---

	// 本人端臉指向（滑鼠累積；上報節流 pattern 同 SleepAim）
	float DrawAimAzLocal = 0.0f;
	float DrawAimTiltLocal = 45.0f;
	float DrawAimSendAccum = 0.0f;
	float LastSentDrawAz = 0.0f;
	float LastSentDrawTilt = 45.0f;
	// 他端顯示平滑（複製節流跳格 → 指數追趕；進鎖瞬間 snap）
	float RemoteDrawAzDeg = 0.0f;
	float RemoteDrawTiltDeg = 45.0f;
	bool bRemoteDrawSnap = true;
	// 他端 P 追趕值（07-26：DrawTargetRepW 的顯示平滑——與 aim 同節奏）
	FVector RemoteDrawTargetW = FVector::ZeroVector;
	// 上報節流的 P 有效旗標邊緣（aim 靜止但 P 有效性翻轉也要送）
	bool bLastSentTargetValid = false;
	// trace miss 寬限（07-26 抖動根治）：貼剪影邊緣的間歇 miss 曾直接 yaw=Az＝
	// 整身甩 20° 再甩回；寬限內姿勢凍結、持續 miss 才額定速率轉向 aim
	float DrawTargetMissSecs = 0.0f;
	// 應用層姿勢限速的熱身旗標（入鎖首解硬切、之後 120°/s）
	bool bDrawPoseWarm = false;

	// One Euro 濾波（Casiez 2012；筆即游標 07-20 定案）：本人 aim 的速度自適應低通。
	// 姿勢/筆/墨全吃濾波值（DrawAim*Filt）、相機吃生值——濾一次、下游一致。
	struct FAimEuro
	{
		float XPrev = 0.0f;
		float DxPrev = 0.0f;
		bool bInit = false;
		float Step(float X, float Dt, float MinCutoffHz, float Beta);
		void Snap(float X)
		{
			XPrev = X;
			DxPrev = 0.0f;
			bInit = true;
		}
	};
	FAimEuro AimEuroAz;
	FAimEuro AimEuroTilt;
	float DrawAimAzFilt = 0.0f;    // 濾波後 aim（EffectiveDrawAz 的本人來源）
	float DrawAimTiltFilt = 45.0f;

	// 上一 tick 的落墨筆尖位置（墨從筆尖出：幀間沿筆尖軌跡細分＝跨縫縫合）
	FVector LastPaintTipWorld = FVector::ZeroVector;
	bool bHasLastPaintTip = false;
	float DrawUnreachSecs = 0.0f;  // 搆不到持續秒數（HUD 提示閘；本人端）

	bool bHasPendingDebugDrawAim = false;   // DebugRoboDrawAim 待消化（本地 tick）
	FVector2D PendingDebugDrawAim = FVector2D::ZeroVector;
	bool bDebugPaintHeld = false;           // robo「模擬按住左鍵」輸入源
	bool bHasPendingDebugMouse = false;     // DebugRoboMouse 待消化（合成滑鼠增量、
	FVector2D PendingDebugMouse = FVector2D::ZeroVector; // 走真人游標管線；robo 橋接
	                                                     // 鐵則：角度命令歸角度制、
	                                                     // 游標制只屬於增量輸入）

	// --- 稿筆自由滑鼠制內部（07-31 五版；只在 Stencil 工具時活躍）---
	bool bDrawCursorRelatch = false;           // 硬切事件旗標（入鎖/錨點重瞄/robo
	                                           // 角度命令/切工具）＝gaze 與凍結相機
	                                           // 重新對準
	float DrawGazeAz = 0.0f;                   // 注視（臉的來源；τ 指數追游標）
	float DrawGazeTilt = 45.0f;
	bool bDrawGazeInit = false;
	float DrawCamAz = 0.0f;                    // 稿筆凍結相機（六版=邊緣推擠制：
	float DrawCamTilt = 45.0f;                 // 恆靜止、只吃「貼邊後本 tick 的
	bool bDrawCamInit = false;                 // 外推量」；見 StencilCamEdgeFrac）
	FVector StencilLazyTip = FVector::ZeroVector; // 拉繩墨尖 B（稿筆按住 LMB 時
	bool bStencilLazyValid = false;               // 被繩拖著追 P；收筆補完後失效）
	void UpdateStencilCursor(float MouseX, float MouseY, float SensDeg, float DeltaSeconds);

	// --- 刺青巡航內部（本人端；07-22 刺青手感、07-24 皮繩追趕制）---
	float TattooNeedleAz = 0.0f;       // 針 aim（皮繩追趕者；LMB 按住期間＝姿勢/筆/墨
	float TattooNeedleTilt = 45.0f;    // 的驅動源。手 aim=DrawAim*Local=游標與相機）
	bool bTattooChaseActive = false;   // 本次按住的追趕鏈已初始化（起點=按下瞬間的手 aim）
	float TattooChaseErrCm = 0.0f;     // 針落後游標的皮膚距估計（導引/summary）
	bool bTattooCruising = false;      // 本 tick 針由追趕步進推進中（出墨閘）
	// --- 自由游標（08-04 二輪修＝user 擊穿「方向 vs 目標點互斥」誤診）：按住 LMB
	// 中滑鼠純積分推游標（無皮繩、可放很遠）、針以 v_max 恆追——「拉出方向持續走」
	// =把游標甩遠；「細微調整」=游標遠時橫移一點=方向改一點（解析度隨距離放大）；
	// 「慢工精描」=游標貼針=貼手域亦步亦趨。aim 恆=針（畫面歸針、相機/姿勢/複製）---
	float TattooCursorAz = 0.0f;
	float TattooCursorTilt = 45.0f;
	// --- 上墨沿稿（07-25 打稿制）：Liner 落針點 SnapCm 內有稿線＝針吸附沿稿自動走
	//（手勢歸打稿、慢工歸機器——玩家不再操縱方向；動滑鼠即取消回自由巡航）---
	bool bStencilFollowActive = false;
	bool bStencilFollowTried = false;  // 每次壓針只嘗試吸附一次（失敗=整段自由巡航）
	int32 FollowWorkId = INDEX_NONE;   // 追蹤中的稿線（受害者畫布 Works 內定位）
	int32 FollowStrokeIdx = INDEX_NONE;
	int32 FollowPointIdx = INDEX_NONE;
	int32 FollowDir = 1;               // 沿 Points 的行進方向（±1；起步=往點多的一端）
	bool TryAcquireStencilFollow(const FVector& NeedleWorld); // 壓針時找最近稿線
	bool UpdateStencilFollow(float DeltaSeconds, const FVector& PNow); // 沿稿步進（回 false=稿走完/失效）
	// 出墨=距離節拍（首版時間節拍被 robo 抓到：筆尖解算橫向抖動 ±0.2cm 疊在前進
	// 間距上＝針距尾巴 0.31>實線界——距離制針距=構造保證、對抖動免疫；5Hz 在最高速
	// 下自然湧現 v_max/間距=f）；預算=速率天花板 f×1.5（防 robo 傳送/異常快移灌針）
	float TattooDistSinceDot = 0.0f;   // 距上一針的筆尖路徑長（cm）
	float TattooDotBudget = 0.0f;      // 出針預算（+dt×f×1.5、上限 2、每針 -1）
	float TattooAngPerCmEst = 0.7f;    // 角度/皮膚公分 換算估計（trace 回饋更新；
	                                   // 0.7°/cm≈眼距 82cm 的小角近似初值）
	FVector TattooLastDotTipWorld = FVector::ZeroVector; // 上一針筆尖（robo 針距量測）
	bool bHasTattooLastDotTip = false;
	float TattooLastDotGapCm = -1.0f;  // 最近兩針的世界距（robo 實線契約：≤k×筆寬×1.35）
	int32 TattooDotsEmitted = 0;       // 本鎖定累計出針數（robo）
	FVector2D DebugPaintStickPx = FVector2D::ZeroVector; // robo 方向命令（皮繩制語義：
	bool bDebugPaintStickActive = false;                 // 游標恆掛針前皮繩處＝無限走廊
	                                                     // 巡航；(0,0)=游標收回針上=停）
	bool bHasPendingDebugNeedle = false;                 // DebugRoboNeedle 待消化（poll 內切針）
	EInkNeedle PendingDebugNeedle = EInkNeedle::Liner;
	// --- 霧針自由揮掃狀態（07-23 四版；五修改距離節拍）---
	float MistDistAccum = 0.0f;        // 沿筆尖路徑的沉積距離累積（移動閘開才走）
	float MistPrevAimAz = 0.0f;        // 移動閘量測：上 tick 的 raw aim
	float MistPrevAimTilt = 0.0f;
	bool bMistPrevAimValid = false;
	float MistAimSpeedDegS = 0.0f;     // 本 tick aim 角速（診斷/robo）
	void UpdateTattooCruise(float DeltaSeconds); // 皮繩追趕：針 aim 以 v_max 上限沿皮膚
	                                             // 追游標（trace 回饋鉗）＋游標皮繩鉗
	float TattooSpeedDebtCm = 0.0f;    // 速度債（07-22 三修）：上幀短差本幀補——
	                                   // 平均速度恆=v_max、對單幀收斂好壞免疫；
	                                   // 貼邊/死區/邊緣清債（催討過猛=爆衝）
	// 外環速度增益（07-22 三修二段）：債務只能保「aim 跳距總和=命令」，但 raw aim
	// 每跳含面片橫向噪聲分量、針走的是 One Euro 平滑後的較短路徑——跳距帳恆偏高
	//（robo 實錘：債務生效後 aim 足額、針仍只有 80%）。外環直接量「針實走距離」
	// 對「命令距離」的比值做增益補償＝閉環鎖定玩家感受到的速度=v_max。
	float TattooSpeedGain = 1.0f;      // 命令倍率（1cm 視窗自適應、鉗 [0.7,1.7]；種子中性——折損是環境量，閉環自己量）
	float TattooGainCmdAccum = 0.0f;   // 視窗累計：命令距離（BaseStep 總和）
	float TattooGainActAccum = 0.0f;   // 視窗累計：針實走距離（tip 路徑長）
	// 診斷累計（robo 定位鏈：命令→aim 跳距→針實走 哪一段掉速；進鎖歸零、只加不減）
	float TattooDbgCruiseSecs = 0.0f;  // 巡航中累計秒數
	float TattooDbgHopCm = 0.0f;       // 巡航中 aim-P 跳距總和（MovedCm）
	float TattooDbgTipCm = 0.0f;       // 巡航中針（tip）路徑總和
	// 單步巡航步進共用核心（巡航與導引預測共用；const=導引用本地複本模擬）：
	// 從 (Az,Tilt,P) 沿 Dir 前進 StepCm 皮膚距離；OutMovedCm=實走距離（債務記帳）。
	// false=真邊緣（連小步都無效）＝釘住
	bool CruiseStepOnSkin(float& AzDeg, float& TiltDeg, const FVector2D& Dir,
		float StepCm, float& AngPerCmEst, FVector& InOutP, float& OutMovedCm) const;

	void PollDrawAim(APlayerController* PC, float DeltaSeconds, bool bCruise); // 作畫中滑鼠→臉指向（＋皮繩追趕）
	float EffectiveDrawAz() const;   // 本人=local、他端=remote 平滑值
	float EffectiveDrawTilt() const;
	// 中心射線→受害者皮膚（PrevUV=縫區連續性偏好：兩島搶點時選離上一點近的島）
	bool ResolveAimToTargetUV(const FVector& DirWorld, FVector2D& OutUV,
		const FVector2D* PrevUV = nullptr) const;
	bool TraceAimToTarget(const FVector& DirWorld, FVector& OutImpact) const; // 共用射線段
	FVector GetAimRayOrigin() const; // 眉心（骨骼現值；相機/骨缺席有退路）
	FVector2D LastPaintUV = FVector2D::ZeroVector; // 縫區連續性偏好的錨（bHasLastPaintTip 同步）

	// 剛性手臂制（2026-07-20 user 定案「手就一直伸直就好」）：雙臂 IK 全拆、
	// 手臂恆 Backup4 手勢；筆焊死在右手（入鎖恆顯示），對齊靠姿勢解算把筆尖帶到
	// 落墨點——3-DOF（整身 yaw＋Hips 傾＋雙踝搖，後兩者=四關節白名單的側面自由度）
	// 數值解「筆尖=P」；解不到=搆不到=不落墨（走近再畫）。
	bool bPenGripCalibrated = false;   // 一次性靜態校準（neutral 姿勢下 shaft=眉→手延長線）
	FName PenGripBoneName;             // RightHandProp 若在，否則 RightHand
	FVector PenTipLocalCm = FVector::ZeroVector; // 筆尖在握骨旋轉框的公分偏移（無 scale）
	FQuat PenRotInHand = FQuat::Identity;        // 筆身朝向在握骨框的相對旋轉
	float DrawSolveYawDeg = 0.0f;      // 解算暖啟動（上次解）
	float DrawSolveHipDeg = 0.0f;
	float DrawSolveAnkleDeg = 0.0f;
	// （07-20 筆即游標：固定係數顯示平滑 DrawShown* 退役——抖動改由 aim 輸入層的
	// One Euro 濾波處理：靜止強濾抖、快掃近零滯後；姿勢=濾波 aim 的直接解）
	bool bDrawTipReachable = false;    // 本 tick 筆尖可達（落墨閘）
	float DrawTipResidualCm = -1.0f;   // 解算殘差（診斷/robo）
	// 可畫域＝嚴格最大橢圓（07-29 user 逐字定案「量測出來的圈太小就是太小，對方
	// 自己調整，我們就是嚴格給他我們能給的最大『橢圓』」）：入鎖眼錨定後對受害者
	// 皮膚採樣可見性＋可解性（FLeanSolveCtx 同源），在鎖點切面座標 (u,v) 上擬合
	// 「內部無任何不可解樣本」的最大面積橢圓——無下限、無人工放大。域=解析公式
	// (u/A)²+(v/B)²≤1：顯示（veil 殼材質內逐像素算、光滑羽化圓弧、零貼圖=斑在
	// 構造上不存在）與收筆閘/✕/提示全查同一條公式＝單一裁判。殼只存在於作畫者
	// 自己的 client（不複製）；游標本身永不被動。
	static constexpr int32 ReachSampleGrid = 64;  // 採樣網格（~2.6cm 粒度）
	int32 ReachSampleIdx = 0;                     // 漸進採樣游標（時間預算制）
	bool bReachEllipseReady = false;
	TArray<FVector2D> ReachInfeasUV;              // 不可解可見樣本的 (u,v)（cm）
	float ReachMaxExtentCm = 0.0f;                // 可解樣本最遠延伸（無約束軸的上限）
	FVector ReachLockW = FVector::ZeroVector;     // 橢圓心（鎖點世界座標）
	FVector ReachAxisH = FVector::XAxisVector;    // 切面橫軸（垂直於 aim 的水平向）
	FVector ReachAxisV = FVector::YAxisVector;    // 切面縱軸（aim 在切面上的投影＝懸崖向）
	float ReachA = 0.0f;                          // 橫半軸 cm
	float ReachB = 0.0f;                          // 縱半軸 cm
	bool IsInsideReachEllipse(const FVector& P) const;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> ReachVeilShell;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ReachVeilMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> ReachVeilMaterial; // M_ReachVeil lazy load
	void UpdateReachVeilShell();  // 橢圓擬合完成＝設材質參數+掛殼
	void ClearReachVeilShell();   // 出鎖/重鎖＝收殼+重置採樣
	bool bCursorDrawable = true;  // 本 tick 游標點的裁決快取（owner 端；橢圓公式）
	// 落墨點快取：aim 動了才重新 trace。眼睛長在會被解算搬動的頭上——每 tick 重
	// trace＝「眼→P→姿勢→眼」自我參照回饋，aim 靜止時 P 仍會漂移到鉗位角落
	//（07-20 探針實錘：三幀漂 10cm）。P 凍結＝迴圈斷開、姿勢收斂為定點。
	FVector DrawTargetWorld = FVector::ZeroVector;
	bool bDrawTargetValid = false;
	// 眼錨定（07-20 五輪結構解）：入畫收斂後把眉心凍成世界定點——相機與 trace 共用
	// 此定點 ⇒ 準星=P 構造精確、「眼→P→姿勢→眼」回饋環結構性不存在（掃筆滑走
	// robo 實錘）、FP 畫面不隨姿勢收斂晃動。臉仍 look-at P（第三者讀感），
	// 「相機恆眉心」語義收斂為「相機＝入畫定格時的眉心」。
	FVector DrawEyeAnchorWorld = FVector::ZeroVector;
	bool bDrawEyeAnchorValid = false;

	// 姿勢髒檢查（aim 沒動不重寫骨——poseable 全身重寫非免費）
	float LastAppliedDrawAz = 1e9f;
	float LastAppliedDrawTilt = 1e9f;
	bool bLeanPoseDirty = true;

	// ghost 制：入鎖=除自己與 LeanTarget 外全員換半透明材質＋trace/碰撞穿透；
	// 退鎖=還原。冪等、每次進退鎖重申（任何退出路徑都要走到）。
	void ApplyGhostView(bool bEnable);
	TSet<TWeakObjectPtr<ANiceInkCharacter>> GhostedChars; // 目前被本端 ghost 的角色

	// 偷瞄目標追蹤：受害者的真頭會動（甦醒升降/掃視/裝睡收回）——目標移動就重擺姿勢

	// 實體筆本 tick 狀態（UpdatePenVisual 寫；診斷讀）
	FVector PenTipWorld = FVector::ZeroVector;
	FVector PenShaftDirWorld = FVector::UpVector;
	bool bPenStateValid = false;

	// --- 伸縮針狀態（2026-07-21；07-22 分帳制加握管長）---
	float PenNeedleLenCm = 0.0f;        // 本 tick 針顯示長度（樁長=收、>樁=伸出實測）
	float PenGripLenCm = 0.0f;          // 本 tick 握管顯示長度（基準長+伸長量/2）
	float DrawNeedleSolveLenCm = -1.0f; // 伸針解採用的針長（-1=標稱針長內解到、身體全扛）
	bool bPenTriggerLocal = false;      // 本人端 LMB 觸發鏡像（零延遲；pattern 同裝睡）
	UPROPERTY(Replicated)
	bool bPenTriggerHeld = false;       // 他端讀的觸發態（COND_SkipOwner）
	UFUNCTION(Server, Reliable)
	void ServerSetPenTrigger(bool bHeld);

	void ResetBowBodyBones();  // 基底骨集合全重置（防作畫姿/手臂殘留漏進睡姿替身）

	void PollLeanEnter(APlayerController* PC);
	void PollLockedDraw(APlayerController* PC, float DeltaSeconds);

	// --- 睡姿替身（頭部轉動破綻，2026-07-15；臉指向制 2026-07-16 三改版）---
	// 單一頻道：頭部姿勢＝臉指向(az,tilt) 的純函數，相機剛體錨在眉心、朝向＝臉朝向。
	// 你看到哪裡＝你的臉指向哪裡＝旁人讀到的破綻，三者恆等式由構造保證。
	// 一具替身全員可見（含本人）。靜態 Body 只藏不關碰撞——畫墨/噴射 UV 解算照打。
	bool EnsureBowBodyAsset() { return EnsurePoseableAsset(BowBody); }
	bool EnsurePoseableAsset(UPoseableMeshComponent* Poseable); // SK 惰性載入＋皮膚 MID 共享
	// 脖子雙版制（08-15 user 定案：站立/作畫脖子回歸蒙皮網格本體、程序化伸縮脖只留
	// 甦醒者升起）：SK_Sumo_Whole（焊縫＋頸帶權重）＝站立/作畫常駐；SK_Sumo（切開版）
	// ＝睡姿替身（轆轤首）。切換＝SetSkinnedAssetAndUpdate＋皮膚 MID 重指派；伸縮脖
	// 只在切開版啟用（bNeckStretchEnabled）。
	void SetBowBodyVariant(bool bWhole);
	bool bBowBodyIsWhole = false;
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMesh> BowMeshWhole;
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

	// 裝睡本人端狀態（pattern 同 SleepAimAzLocal：本地零延遲、RPC 上服複製給他端）
	bool bFeignSleepLocal = false;
	bool bDebugFeignHeld = false;            // robo「模擬按住 Shift」輸入源（與真鍵 OR，
	                                         // 走同一條 poll edge——直設狀態會被輪詢反殺）
	void SetFeignSleepLocal(bool bNewFeign); // 本人端切換共用點（poll edge 唯一呼叫者）
	void ApplyFeignVisual();                 // 裝睡切換窄路徑：眼皮＋替身 dirty。
	                                         // 不走 ApplySleepVisual——那條路會把本人
	                                         // 臉指向歸零（入睡重置），毀掉凍結契約

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

	// 系統鏡頭（巡禮／指認預覽／結算聚焦）——各端本地生成、依複寫的 WorkId 對位
	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> CinematicCamera;

	int32 LastViewWorkId = INDEX_NONE;
	bool bWideViewActive = false;
	bool bViewOverridden = false;

	void UpdateCinematicCamera(APlayerController* PC);
	void ViewWork(APlayerController* PC, int32 WorkId);
	void ViewWide(APlayerController* PC);

	// 開場動畫導演鏡頭：分拍機位表＋真剪接（blend=0；機位內只做單調緩推——
	// 剪接乾淨、推軌緩慢是兩件事，不要混）。房主近景那一拍房主本人看反拍。
	void ViewIntro(APlayerController* PC, ENiCeremonyStep Step, float T);
	ENiCeremonyStep LastIntroCamStep = ENiCeremonyStep::None;
	bool bIntroSitAudioStarted = false;
	UPROPERTY()
	TObjectPtr<class UAudioComponent> IntroMachineLoop;
	void ViewSelfThirdPerson(APlayerController* PC);
	void RestoreView(APlayerController* PC);
	ACameraActor* GetOrSpawnCinematicCamera();

	bool bThirdPersonActive = false;

	void PollLobby(APlayerController* PC);
	void PollAccusation(APlayerController* PC);
	void PollCounterplay(APlayerController* PC);
	void PollFlip(APlayerController* PC);
	void PollShakeAttack(APlayerController* PC);
	void EnsureAvatarApplied();
	void PollLook(APlayerController* PC, float DeltaSeconds);
	void PollMove(APlayerController* PC);
	void PollTrapDial(APlayerController* PC);
	void PollPalette(APlayerController* PC);
	void StopPaintingLocal();
	void ApplySleepVisual();

	// 惰性墨層生滅（2026-08-25）：身體 MID 由 InkBodyComponent 自己重綁，但伸縮脖
	// 是「抄一份身體 MID」的第二個消費者，抄的門檻是 SkinTone 變動——墨層換貼圖
	// 不動 SkinTone ⇒ 必須顯式踢它重抄。兩個消費者都要跟，漏一個就會脫節。
	UFUNCTION()
	void HandleInkLayersChanged();

	// 迷宮技能授予去重（server；每回合每存檔點只授一次，ServerSetAsleep(true) 重置）
	bool bMazeSprayGranted = false;
	bool bMazeKickGranted = false;
};
