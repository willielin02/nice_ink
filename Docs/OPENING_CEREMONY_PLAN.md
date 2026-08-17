# 開場儀式（轉酒瓶→喝→倒下）實作計畫

> 帳本檔。2026-08-16 建立。範圍＝SPEC「一場遊戲的完整流程」第 0 節「入場與轉酒瓶」
> 與第 1 節「入座昏睡」之間的全部演出。**SPEC 未動**（本計畫不新增任何規則；
> 轉瓶「純儀式、零規則」是既有定案）。

## 0. 使用者定案捕捉（逐字，不改寫）

- 「先隨機選一個人後，讓酒瓶自然地、慢慢地停在對應的人的正前方。也就是實際上酒瓶在
  轉動之前就已經決定好人選了，酒瓶只是一個示意」→ **採用**（理由見 §2）。
- 「大家圍在房間正中央、酒瓶轉動結束後對應的人要揀起酒瓶喝一口，然後倒下，
  接著就開始第一輪遊戲」→ 儀式六拍的規格來源。
- 「我們可以都不要有硬切的地方嗎？程序化動畫、姿勢，是否可行？」→ **零硬切為本任務
  的驗收標準**；逐項稽核在 §7（含兩個誠實的例外）。

## 1. 現況調查（全部實測，附行號）

| 項目 | 現況 | 檔案 |
|---|---|---|
| BottleSpin 相位 | 空相位 2.5s：`SetPhase` → timer → `OnBottleSpinDone` | `NiceInkGameMode.cpp:891-925` |
| 抽人 | `FMath::RandRange` 在**相位結束時**才抽（robo 可用 `DebugForcedVictimSeat` 覆寫） | 同上 `:920-924` |
| 入座 | `EnterSeating` 立即 `ServerSetAsleep(true, GetVictimLieTransform())`＝**傳送**到躺位＋DisableMovement | `NiceInkGameMode.cpp:927-993` |
| 睡姿本體 | sumo 無烘焙睡姿 ⇒ `SleepMesh = StandMesh`，躺＝同一網格整體旋轉：`BodyLieRelLoc(-87,0,-60)` / `BodyLieRelRot(0,90,-90)`（站姿＝`(0,0,-92)`/`(0,-90,0)`） | `NiceInkCharacter.cpp:48-52, 3946-3973` |
| 睡姿替身 | `UpdateSleepBodyDouble` 讓 BowBody 跟隨 `Body` 的**相對**變換 | `:5884-5940` |
| 站姿顯示 | `UpdateWalkAnim`＝BowBody 骨骼；靜止走 `ApplyStandLookPitch`（ref pose＋頭俯仰）、移動走 `ApplyGaitPose`（摺り足＋二骨腿 IK） | `:6565-6944` |
| 擺骨基建 | `RotSubtreeAboutPivotCS`（file-static `:106`）、`ComposeLeanBaseCS`（`:4564`）、`WriteBowPoseConverged`（`:4595`，poseable 快取收斂迴圈）、`ApplyLookPitchToCS`（`:6966`） | 同上 |
| 二骨 IK 範本 | 腿：解析式（餘弦定理＋rest 導出極向） | `:6845-6922` |
| 移動注入 | `DebugRoboWalk(dirX,dirY,sec)` → `PollMove` 每 tick `AddMovementInput`＝與真鍵同一入口 | `:4418-4423, 1225-1240` |
| 相位鏡頭 | BottleSpin → `bWide` → `ViewWide`（`SetViewTargetWithBlend 0.5s Cubic`，無受害者時看 `(0,75,60)`＝躺位） | `:697-767, 868-893` |
| 沉睡黑屏 | HUD 對 `bAsleep && !bEyesOpen` 全屏黑底＋描圖盤 | `NiceInkHUD.cpp:656-663` |
| 相位音效 | 進 BottleSpin→`ENiSound::BottleSpin`、進 Seating→`DrinkGulp`（資產已在 `/Game/Audio`） | `NiceInkHUD.cpp:915-916`、`NiceInkAudio.cpp:14-29` |
| 相位列舉波及面 | 全專案 ~40 處，只在 GameMode／Character 輸入閘／HUD banner＋音效 | `grep ENiceInkPhase::` |
| 場地 | 席位 6 個手擺 2D、`VictimLieSpot(0,75)`、`ProbeFloorZ` 從 +150 打下 | `NiceInkGameMode.h:52-67`、`.cpp:740-773` |
| 酒瓶資產 | **不存在**。`SourceAssets/whiskey.glb`（v3.1 使用者放入、授權未查＝SHIP_PLAN C8）；`Content/Audio/bottle_spin` 音效已有 | — |
| 網路時鐘 | `GameState::GetPhaseTimeRemaining` 已用 `GetServerWorldTimeSeconds()` | `NiceInkGameState.cpp:43-46` |
| 中途加入 | 真開局即 `SetSessionInProgress(true)` ⇒ 儀式期間不會有人進來 | `NiceInkGameMode.cpp:863` |
| 臉閘 | 全員 `bFaceReady` 才准開局 ⇒ 儀式期間不會有隱形人 | `NiceInkGameMode.cpp:838-848` |

**結論**：所有需要的基建都在（程序化擺骨、二骨 IK、收斂寫入、輸入注入、伺服器時鐘、
演出鏡頭、音效層）。本任務**沒有新機理**，是既有零件的重組＋一個新道具 actor。

## 2. 設計決策

### 2.1 先抽後演（採用使用者原案）

伺服器先均勻抽人 → 複製「起始角、圈數、終點角、開轉時間戳」→ 每端各自用**同一組參數**
決定性播減速曲線。理由四條：

1. **網路正確性**：真物理在 4~6 台機器不會逐位一致，最後仍要伺服器宣判 ⇒ 畫面與宣判
   對不上就是穿幫。先抽後演讓每端算出的終角逐位相同。
2. **機率公平**：席位是手擺的、角度不等距（x −300~155），真物理＋扇區判定會讓張角大的
   席位偏高，還要處理「停在兩人中間」。`RandRange` 才是真公平；瓶子只演出結果。
3. **不可分辨**：角速度單調遞減＋隨機圈數，玩家分不出預定與否。SPEC 明文「純儀式、零規則」
   ——儀式本來就是決策的展示。
4. **可測**：`DebugForcedVictimSeat` 得以保留（robo 全套依賴它）。

### 2.2 「零硬切」的定義（本任務的驗收語義）

不是「加平滑補間」，而是：**所有姿勢與位置參數，其對時間的一階導數有界**。實作上＝

- 位置：只由 `AddMovementInput`（CMC 自帶加減速）或有界速度的插值產生，**全程零 teleport**。
- 姿勢：所有角度/位移量由 `t∈[0,1]` 的緩動曲線導出，端點值與前後拍**逐位相接**。
- 交接：每一拍的結束狀態 ≡ 下一拍的起始狀態（不是「接近」，是同一個算式的同一個值）。
- 終點：崩塌結束時的 actor transform 與 `Body` 相對變換 ≡ `ServerSetAsleep` 要寫入的值
  ⇒ 睡姿接管當幀零跳變。

> **與 SPEC #24（美術語言＝程式化硬轉、突兀即目標）的關係**：本儀式是明確的例外
> ——它是演出（cutscene）不是玩家動作。#24 管的是「玩家在做什麼」的即時回饋
> （彎腰／偷瞄／起身）。**SPEC 不動**；若使用者要把連續化推廣到全角色動作，那是另一個
> 決定、另一份工作量，需 SPEC 追記。

## 3. 儀式時間軸（六拍）

圈心＝`VictimLieSpot`（＝酒瓶位置＝最終躺位）。**這是承重選擇**：崩塌終點不需要任何
水平位移修正，`ServerSetAsleep` 寫入的變換與動畫終點同一個值。

| # | 拍 | 預設時長 | 內容 | 主要載體 |
|---|---|---|---|---|
| ① | Gather | 2.5s | 全員從席位走到圈上自己的角位、面向圈心 | 合成輸入＋摺り足步態（既有） |
| ② | Spin | 4.0s | 酒瓶原地轉，ease-out 減速，瓶口停在受害者正前方 | 新 `ANiceInkBottle` |
| ③ | Approach | 1.6s | 受害者走進圈心、停在瓶邊 | 合成輸入＋步態 |
| ④ | PickUp | 1.2s | 屈膝＋上身前彎、右手二骨 IK 伸向瓶頸、瓶附著到手 | 新 `ApplyCeremonyPose` |
| ⑤ | Drink | 1.8s | 起身、手把瓶抬到嘴、頭後仰、瓶身傾倒；`DrinkGulp` 音 | 同上 |
| ⑥ | Collapse | 1.1s | 瓶脫手落地＋膝軟→整體翻倒→沉降到躺姿；本人畫面同步淡入全黑 | 同上＋剛體插值 |

合計 ≈ 12.2s（全部 UPROPERTY 旋鈕；robo 全縮到 0.2s）。短影音「佈局 3 秒」剪 ⑤⑥ 即可。

**回合 2+ 的復用**：③④⑤⑥ 抽成可復用序列 `FNiCeremonySeq`。開場＝①②＋序列；
**每一回合的入座酒／罰酒也走同一序列**（見 §12 未決 D，我的推薦＝一併接上——
否則第一回合連續、第二回合起傳送落地＝自相矛盾的硬切）。

## 4. 架構：資料流與網路模型

### 4.1 複製欄位（`ANiceInkGameState` 新增 5 欄）

```cpp
UPROPERTY(BlueprintReadOnly, Replicated) ENiCeremonyStep CeremonyStep = None;
UPROPERTY(BlueprintReadOnly, Replicated) float CeremonyStepStartTime = 0.0f;   // server world time
UPROPERTY(BlueprintReadOnly, Replicated) float CeremonyStepDuration  = 0.0f;
UPROPERTY(BlueprintReadOnly, Replicated) float BottleStartYaw = 0.0f;          // 本輪轉動起點
UPROPERTY(BlueprintReadOnly, Replicated) float BottleEndYaw   = 0.0f;          // 終角（＝圈心→受害者方位）
```
`CeremonySpinTurns` 併進 `BottleEndYaw`（＝真實總轉角，不取模）＝一個 float 表達全部。

**`VictimPlayerId` 在 Spin 結束才寫**——避免 HUD 在轉瓶期間就掛出受害者名（劇透）。
客戶端從 `BottleEndYaw` 幾何上仍可反推，但零賭注、且動畫必須要它，接受。

### 4.2 伺服器（`ANiceInkGameMode`）

```
EnterBottleSpin()
  ├ 抽人（DebugForcedVictimSeat 優先）→ 存 PendingVictimId（不複製）
  ├ 生成/取得 ANiceInkBottle（圈心、floor probe Z）
  ├ BottleEndYaw = 現角 + (3~5 圈隨機)*360 + Δ(圈心→受害者 yaw)
  └ SetCeremonyStep(Gather) → timer → Spin → timer → …
SetCeremonyStep(Step)
  = 寫 GS 四欄（Step / StartTime=GetServerWorldTimeSeconds() / Duration）＋掛 timer
OnSpinDone()   → GS->VictimPlayerId = PendingVictimId（此刻才揭曉）→ Approach
OnCollapseDone() → EnterSeating(VictimPlayerId, /*bAlreadyLying=*/true)
```
`EnterSeating` 新增參數：`bAlreadyLying` 時**不呼叫**傳送路徑，改呼叫
`ServerSetAsleep(true, LieT, /*bNoTeleport=*/true)`（見 §5.3）。

**斷線防護**：任一步中受害者離線 → 回 `Spin`，`BottleStartYaw = 瓶目前角`（不重置＝
瓶子繼續轉，畫面連續）、重抽終角。人數不足 → 現行 `Lobby` 路徑。
其他玩家離線 → 只影響 ① 的圈位（重算角位，走路目標平滑改變，無跳變）。

### 4.3 客戶端（純函式）

每端每 tick：`t = clamp((GetServerWorldTimeSeconds() - StepStartTime)/StepDuration, 0, 1)`。
所有視覺都是 `(Step, t, 複製參數, 世界幾何)` 的函式 ⇒ **無狀態、無累積、遲到者自動對齊**
（中途加入雖被 session 擋掉，但 PIE/robo 會出現，仍必須成立）。

### 4.4 移動：合成輸入，不是傳送

`Gather`／`Approach` 的走路＝**每個本地控制的 pawn 自己**朝目標點 `AddMovementInput`
（與 `DebugRoboWalk` 同一入口）⇒ CMC 預測正常、他端由既有複製＋步態自然呈現、
零 rubber-band。伺服器**不做任何 snap**：位置誤差由下一拍吸收
（④ 的手 IK 打的是瓶子的**實際**世界位置；⑥ 的插值起點是**實際**位置）。
這就是「零硬切」的構造保證——沒有任何一行 `SetActorTransform`。

圈位分配：按 `SeatIndex` 的角序繞圈（保序 ⇒ 路徑不交叉、不互撞）。到位判定＝距離
< 20cm 或時間到；**永不因未到位而卡住**（步進只看時間）。

## 5. 姿勢層規格

新函式 `ANiceInkCharacter::ApplyCeremonyPose(float DeltaSeconds)`，在 `Tick` 中插在
`UpdateWalkAnim` **之前**並在儀式期間取代它（同一個 BowBody 載體，`bStandDoubleActive`
的活化流程復用；離開儀式時把 BowBody 交還步態＝既有的重置路徑）。

所有拍共用一組**連續狀態量**（每一拍只是給它們不同的目標曲線）：

| 量 | 意義 | 影響骨 | 端點約束 |
|---|---|---|---|
| `CeremBendDeg` | 上身前彎（繞髖樞軸，Spine/Spine1 **分攤**——單骨深彎摺爆肚子蒙皮，`ApplyBowPose` 血價） | Spine, Spine1 | 拍①②③＝0；⑥ 結束＝0 |
| `CeremCrouchCm` | 髖下沉（腿二骨 IK，腳目標＝原地貼地） | 雙腿 | 同上 |
| `CeremArmAlpha` | 右臂 IK 權重（0＝rest 手臂、1＝手在目標點） | RightArm/ForeArm/Hand | ①②③＝0；⑥ 結束＝0 |
| `CeremHandTargetW` | 右手目標世界點 | 同上 | ④＝瓶頸；⑤＝嘴 |
| `CeremHeadPitchDeg` | 頭頸俯仰（沿用 `ApplyLookPitchToCS` 的 Neck/Head 分攤結構） | Neck, Head | ⑥ 結束＝0 |
| `CeremToppleAlpha` | 站→躺的剛體插值參數 | Body/BowBody 相對變換＋actor 位置 | ⑥ 結束＝1 |

**端點約束是硬要求**：⑥ 結束時前五項全＝0、第六項＝1 ⇒ 交給睡姿替身的當幀，
BowBody 的骨姿＝`ResetBowBodyBones()` 後的 ref pose、相對變換＝`BodyLieRel*`
——與 `UpdateSleepBodyDouble` 首幀寫入的值**逐位相同**。

### 5.1 右臂二骨 IK（唯一的新解算）

照抄 `ApplyGaitPose` 腿 IK 的結構（餘弦定理 + rest 幾何導出極向），骨鏈換成
`RightArm → RightForeArm → RightHand`：

```
D = Target - ShoulderCS;  DLen = clamp(|D|, |L1-L2|+0.5, L1+L2-0.5)
PoleRef = rest 肘偏移在垂直 (Hand-Shoulder) 平面上的分量（＝rest 肘朝外/朝下）
A = (L1²-L2²+DLen²)/(2·DLen);  H = √max(L1²-A², 1)
Elbow = Shoulder + D̂·A + Pole·H
```
`CeremArmAlpha` 對「rest 手臂 CS ↔ 解出 CS」做四元數 slerp＋位置 lerp ⇒ 手臂**永不瞬移**。
`RightHandProp` 骨（既有持物骨，筆就掛在上面）＝酒瓶的附著點，附著用
`KeepWorldTransform` ⇒ 附著當幀零位移。

### 5.2 崩塌（⑥）的曲線

```
τ = t/CollapseSeconds
τ∈[0,0.30]  膝軟：CeremCrouchCm ↑（ease-in）、CeremBendDeg 小幅前彎、頭垂；瓶脫手
τ∈[0.30,0.88] 翻倒：ToppleAlpha = smootherstep 加重力偏權（1-cos 型，越倒越快）
              actor loc  = lerp(實際位置, LieLoc, ToppleAlpha)
              Body 相對  = slerp(BodyStandRelRot, BodyLieRelRot, ToppleAlpha)
                          + lerp(BodyStandRelLoc, BodyLieRelLoc, ToppleAlpha)
τ∈[0.88,1.0] 沉降：上述維持在終值，姿勢量以臨界阻尼收到 0（**必須真的到 0**）
```
`BodyStandRelRot(0,-90,0) → BodyLieRelRot(0,90,-90)` 的 slerp 本身就是「向側後方倒下」
的剛體翻轉——躺姿在本專案裡本來就是同一網格的旋轉，所以這個插值不是近似，是**恆等的
中間態**。脫手的瓶子走簡單拋物線落地後靜止（不進物理引擎＝跨端決定性）。

### 5.3 `ServerSetAsleep` 的 `bNoTeleport` 路徑

現行 `bNewAsleep` 分支做四件事：存 `SeatTransform`、`SetActorTransform(LieTransform)`、
`StopMovementImmediately`＋`DisableMovement`、`ClientSyncPoseTransform`。
新增旗標時：**跳過 `SetActorTransform`**（動畫已經把它放在那裡），其餘照舊。
`SeatTransform` 改存「儀式開始前的席位變換」（由 GameMode 在 ① 開始時記下）——
否則現身時會站回圈心而非席位。`ClientSyncPoseTransform` 照送（yaw 斷言鏈不能斷，
08-04/08-15 兩次競態血價）。

## 6. 酒瓶 actor

`ANiceInkBottle`（新檔）：

- `bReplicates = true`，伺服器生成一次（`EnterBottleSpin`；已存在就復用——瓶子從此常駐
  房間中央，回合 2+ 的罰酒直接再撿一次）。
- 視覺純由 GameState 參數導出：`Yaw(t) = lerp(BottleStartYaw, BottleEndYaw, EaseOutQuint(t))`
  ——**位置與角度都不複製**（複製會帶來 33ms 量化階梯；純函式零成本零抖動）。
- 三個狀態：`Idle`（躺在圈心）／`Spinning`／`Held`（附著在 `RightHandProp`）／`Dropped`。
- 碰撞：`NoCollision`（避免撞到力士膠囊、避免影響 lean-lock 的 trace）。

**網格**：`SourceAssets/whiskey.glb` → `Tools/AssetPrep/build_bottle_fbx.py`
（照抄 `build_marker_fbx.py`：gltf 匯入→攤平世界變換→**橫躺擺正、+X＝瓶口方向、
原點＝旋轉樞軸（瓶身重心在地面上）**→FBX 匯出）→ `ue_import_assets.py` 匯入
`/Game/Props/SM_Bottle`。授權未查＝SHIP_PLAN C8 既有缺口，**本任務把它記進 C8 而不
自行決定**；換網格＝改一個資產路徑常數。

## 7. 「零硬切」逐項稽核

| 環節 | 天然狀態 | 處置 |
|---|---|---|
| 相位進場鏡頭 | `SetViewTargetWithBlend 0.5s Cubic`＝已連續 | 保留 |
| 儀式期間鏡頭 | 現行只有一個靜態 wide | 改單一連續運鏡：緩慢環繞→轉瓶時推近瓶子→⑤⑥ 搖上受害者臉。**全程同一個 CameraActor 連續移動、零切鏡** |
| 儀式→Drawing | `RestoreView` 0.35s blend | 保留 |
| 走路 | CMC 自帶加減速 | 合成輸入，零 teleport |
| 轉瓶 | — | ease-out quintic，角速度單調遞減到 0 |
| 瓶子附著到手 | `AttachToComponent` 是父子關係切換 | `KeepWorldTransform`＋在手已到位那一幀執行 ⇒ 世界變換不變＝不可見 |
| 手臂伸出 | rest→IK 會瞬跳 | `CeremArmAlpha` smoothstep 0→1 |
| 頭後仰／前彎／屈膝 | — | 全部緩動曲線 |
| 翻倒 | — | 剛體 slerp＋重力型 ease＋阻尼沉降 |
| 睡姿接管 | `ApplySleepVisual` 直接寫躺姿值 | 動畫終點 ≡ 該值 ⇒ 零跳變（robo c5 斷言） |
| 本人畫面→黑 | HUD 對 `bAsleep` 直接全黑 | 崩塌期間畫 alpha 隨 τ 上升的黑幕 ⇒ 接到全黑時已經是黑的 |
| **眼睛閉合** | **貼圖二值切換（只有睜/閉兩張變體）** | **誠實例外①**：在 ⑤ 頭已後仰、⑥ 起始時切換——鏡頭讀不到；要真正連續需第三張半閉貼圖＝新資產，不在本任務 |
| **入場 `SwapBodyMesh`** | sumo `SleepMesh==StandMesh` ⇒ 實際是 no-op | **誠實例外②**：若未來補上真烘焙睡姿網格，這裡會變成硬切，需改成兩具交叉淡出或放棄該資產 |

## 8. 場地探針（**先於施工**，CLAUDE.md 陷阱年鑑鐵則）

`Tools/RoboTest/robo_ceremony_probe.py`（新）：

1. 對圈心 `VictimLieSpot(0,75)` 周圍 R∈{140,160,180,200}、每 15° 打一根向下射線
   （從 z+150 起，與 `ProbeFloorZ` 同慣例），記錄命中高度與是否無地板。
2. 同時對每個角位打一根**齊胸高的水平射線**（抓低空障礙：柱、矮几）。
3. 席位→角位的直線路徑取樣 20 點，逐點地板檢查（走路會不會掉出世界）。

### 探針結果（2026-08-16 實測，`Saved/robo_ceremony_probe.txt`）

**首輪被污染**：4 個 PIE 力士的 `Body` 擋 `ECC_Visibility` ⇒ 地板射線打到人頭（z≈140）、
膠囊重疊撞到彼此 ⇒ 整條路徑假 blocked。修＝探針忽略所有 `ANiceInkCharacter`
並回報擋路者名字。**教訓：場地探針必須把活體排除，否則量到的是玩家不是場地。**

清潔數據（四個候選圈心 × 六環 × 24 角）：

| 圈心 | r=120 | r=140 | r=160 | r=180 | r=200 |
|---|---|---|---|---|---|
| **lie (0,75)** | 23/24 | 22/24 | **22/24**（壞：15°,345°） | 21/24 | 24/24 |
| mid (0,0) | 23/24 | 22/24 | 22/24 | 20/24 | 24/24 |
| gaitctr (−50,−25) | 24/24 | 24/24 | 24/24 | 21/24 | 22/24 |
| west (−60,25) | 24/24 | 24/24 | 24/24 | 23/24 | 24/24 |

- **地板全平無洞**：每個圈心每一環都 `floor=24/24`，z 恆 3.7（少數 5.4）⇒ 角位高度零變異，
  但仍逐位走 `ProbeFloorZ`（未來換場景自動跟）。
- 障礙＝`StaticMeshActor_4`（東側，躺位 +X 方向 120~180cm 散佈）、`StaticMeshActor_1`
  （西南，擋席位 4 的路徑）、`_6/_7/_12`（西/西南，只在 r≥220）。

**定案（不改既有遊戲）**：
- **圈心＝`VictimLieSpot`(0,75)**——保住「崩塌終點≡躺位、零位移修正」的承重性質。
  （west/gaitctr 雖然更空曠，但要移動躺位＝動到整個遊戲發生的地點，超出本任務範圍。）
- **R＝160**（`CeremonyCircleRadiusCm`）：6 個 60° 間隔角位（0/60/120/180/240/300）
  在實測中**全部淨空**（壞角只有 15°/345°，無一命中），玩家間距 167cm ≫ 膠囊直徑 84。
- **角位旋轉偏移＝伺服器掃描擇優**（24 個 15° 候選，取「全角位淨空且總步行距離最小」者）
  ＋複製一個 float ⇒ 換場景/換人數自動適應，客戶端零重算分歧。
- **席位→角位＝保序指派**（循環序不變的旋轉，最小化總角位移）⇒ 路徑天然不交叉。
- **路徑擦撞**（實測只有席位 0→角位 0 有 6/20 取樣被擋）：CMC 自帶滑行＋卡住偵測
  （0.4s 無進展 → 垂直方向微調 0.3s）＋步進只看時間 ⇒ 永不卡死。

## 9. 檔案清單

**新增**
| 檔案 | 內容 |
|---|---|
| `Source/NiceInk/Public|Private/NiceInkBottle.h|.cpp` | 酒瓶 actor（~180 行） |
| `Tools/AssetPrep/build_bottle_fbx.py` | glb→FBX 擺正 |
| `Tools/RoboTest/robo_ceremony_probe.py` | 場地探針（施工前） |
| `Tools/RoboTest/robo_ceremony_test.py` | 契約測試（~12 檢查） |
| `Tools/RoboTest/robo_ceremony_shots.py` | 六拍截圖矩陣 |
| `Docs/OPENING_CEREMONY_PLAN.md` | 本檔 |

**修改**
| 檔案 | 改動 |
|---|---|
| `NiceInkTypes.h` | `ENiCeremonyStep` 列舉 |
| `NiceInkGameState.h/.cpp` | 5 個複製欄＋`GetLifetimeReplicatedProps`＋`GetCeremonyAlpha()` helper |
| `NiceInkGameMode.h/.cpp` | 儀式狀態機、抽人移到 Spin 開始、瓶子生成、`EnterSeating(bAlreadyLying)`、席位變換保存、斷線分支、6 個時長旋鈕 |
| `NiceInkCharacter.h/.cpp` | `UpdateCeremony()`／`ApplyCeremonyPose()`／右臂二骨 IK／合成移動／儀式輸入閘（**單一 `IsCeremonyActive()` 早退**，不散落）／儀式運鏡／`ServerSetAsleep` 的 `bNoTeleport` |
| `NiceInkHUD.cpp` | 儀式 banner、崩塌黑幕 alpha、逐拍音效（②瓶轉、⑤`DrinkGulp`、⑥`KickThud` 當落地悶響） |
| `NiceInkLocText.cpp` | 儀式提示鍵 ×13 語 |
| `Docs/SHIP_PLAN.md` | 追記＋C8 補酒瓶授權項 |

**不動**：SPEC.md、InkCanvas／InkBody／DreamTrace／NeckStretch／臉系統／選單。

## 10. 旋鈕（全 UPROPERTY，viewport 即調）

`CeremonyGatherSeconds 2.5` / `SpinSeconds 4.0` / `ApproachSeconds 1.6` /
`PickupSeconds 1.2` / `DrinkSeconds 1.8` / `CollapseSeconds 1.1` /
`CeremonyCircleRadiusCm`（探針後定值）/ `SpinTurnsMin 3` / `SpinTurnsMax 5` /
`CeremonyBendDeg` / `CeremonyCrouchCm` / `CeremonyHeadTiltDeg` /
`CollapseKneeFrac 0.30` / `CollapseSettleFrac 0.12` / `bCeremonyEnabled`（一鍵退回舊制）

## 11. 驗收儀器

**`robo_ceremony_test.py` 契約（機讀斷言）**
| # | 契約 |
|---|---|
| c1 | 轉瓶終角指向受害者席位 ±3° |
| c2 | `VictimPlayerId` 在 Spin 結束前恆為 `INDEX_NONE`、其後＝抽中者 |
| c3 | **零 teleport**：儀式全程逐 tick actor 位移 < `MaxWalkSpeed·Δt·1.5`（崩塌拍除外，另有 c4） |
| c4 | **崩塌連續性**：逐 tick `Body` 相對旋轉角步 < 上限、位移步 < 上限（無跳變） |
| c5 | **交接零跳變**：崩塌終幀 actor transform 與 `GetVictimLieTransform()` 差 < 1cm/1°；`bAsleep` 翻轉前後一幀的 `Body` 相對變換差 < 0.1cm/0.1° |
| c6 | 六拍全部走到、相位最終抵達 `Drawing` |
| c7 | 儀式期間輸入無效（注入 lean/paint/emerge → 狀態不變） |
| c8 | 決定性：兩端在同一伺服器時間的瓶 yaw 差 < 0.5° |
| c9 | 手 IK 誤差：④ 結束時右手骨到瓶頸 < 3cm；⑤ 峰值時手到嘴 < 5cm |
| c10 | 腳貼地不破（沿用 gait c 條）＋屈膝期間雙腳 Z 不離地 |
| c11 | 受害者中離 → 回 Spin 重抽、不卡死 |
| c12 | `DebugForcedVictimSeat` 覆寫生效 |

**截圖矩陣** `robo_ceremony_shots.py`：六拍 × 三機位（wide／受害者近景／瓶子俯視）＋
崩塌拍每 0.15s 一張連拍（人眼查翻倒讀感）。

**迴歸組**（改動觸及 Character Tick／相位機／睡姿交接）：
`robo_fullloop_test`、`robo_gait_probe`(10)、`robo_orbit_test`(24)、`robo_trace_test`(22)、
`robo_feign_test`(26)、`robo_lookpitch_probe`(9)、`robo_directdraw_test`(72，既知 flake 一條)。

## 12. 未決（待使用者裁決）

| # | 問題 | 我的推薦 |
|---|---|---|
| A | 圈心＝`VictimLieSpot(0,75)` 還是道場真幾何中心？ | **等探針數字**再定；優先 `VictimLieSpot`（崩塌零位移修正） |
| B | 酒瓶網格＝`whiskey.glb`（威士忌，道場場景違和；授權未查）還是另尋 | 先用它把機制做完（換網格＝一個常數），授權併入 C8 |
| C | 儀式期間玩家能不能自由轉頭？ | **不能**（單一連續運鏡＝短影音佈局鏡）；現行 BottleSpin 本來就是 wide 全景，一致 |
| D | 回合 2+ 的入座酒／罰酒要不要也走喝→倒序列？ | **要**。只做開場＝第二回合起傳送落地，正好違反「都不要有硬切」 |
| E | 醉倒讀感若 viewport 判「像木板」 | 退路＝Blender 手 key 1 秒倒地 clip（全案唯一值得動用動畫資產處）；先不預設 |

## 13. 施工順序（每階段可獨立驗證）

1. **探針**（§8）→ 定圈心/半徑/角位。無編譯、無風險。
2. **資產**：`build_bottle_fbx.py` → 匯入 → 場景裡看得到瓶子。
3. **骨架**：`ENiCeremonyStep` + GameState 欄位 + GameMode 狀態機 + 輸入閘。此時
   儀式＝各就各位＋瓶子轉（無姿勢），`robo_ceremony_test` c1/c2/c6/c7/c8/c12 應綠。
4. **移動**：①③ 合成輸入 → c3/c10 綠。
5. **姿勢**：右臂 IK → ④⑤ → c9 綠。
6. **崩塌**：⑥＋`bNoTeleport` 交接 → c4/c5 綠。
7. **運鏡＋HUD＋音效**：連續運鏡、黑幕、逐拍音。
8. **回合 2+ 復用**（未決 D 為「要」時）。
9. **全迴歸**＋截圖矩陣自查 → 交 viewport。

## 14. 施工記錄（2026-08-16 BUILT-自驗，待 viewport）

**狀態＝機制全通、契約 11/0、迴歸全綠；手感與外觀待使用者 viewport。**

### 實際落地與計畫的差異

| 計畫 | 實際 | 原因 |
|---|---|---|
| BottleSpin 相位涵蓋六拍 | **BottleSpin＝Gather+Spin；Seating＝Approach+PickUp+Drink+Collapse** | 這樣回合 2+ 免費復用後四拍（未決 D＝要），且 Seating 的語義（「喝入座酒、閉眼昏睡」）正好就是它現在演的東西 |
| 瓶子附著用 `AttachToComponent(KeepWorldTransform)` | **捕捉當幀相對變換**（`CeremBottleLocalT = BottleW.GetRelativeTransform(GripW)`） | 數學等價但不動父子關係＝不碰碰撞/複製語義；實測交接位移 0.00cm |
| 圈心/半徑待探針 | 圈心＝`VictimLieSpot`、R=160、角位偏移伺服器掃描 | 見 §8 探針結果 |

### 儀器與實測數字（`Saved/robo_ceremony_result.txt`）

`robo_ceremony_test.py` **11/0**：
- c1 瓶口指向受害者 **err=0.0°**
- c2 揭曉時機：387 個取樣中 0 次提前洩漏
- c3 **零 teleport**（全程 0 次超速位移）
- c4 崩塌連續：逐幀最大 **6.7°／2.36cm**
- c5 **交接零跳變**：崩塌終點距躺位 **0.0cm**、`Body` 相對旋轉 **(0, 90, −90)** 逐位等於 `BodyLieRelRot`、`asleep=True`
- c7 儀式期間輸入無效
- c8 圍圈：最差角位誤差 11.2cm
- c9 手到瓶頸 **0.3cm**（`reachNeed=74.9` vs `armLen=78.0`）
- c10 瓶子跟手：Drink 期間行程 **148.8cm**、逐幀最大 3.99cm
- c11 回合 2+ 走同一序列

迴歸：`robo_fullloop` **16/0**、`robo_orbit` **24/0**、`robo_gait` **10/0**。
截圖矩陣 `robo_ceremony_shots.py` 11 張（`Saved/Screenshots/WindowsEditor/cerem_*.png`）。

### 施工期抓到的四隻（全部是「量測先於機理」的戰果）

1. **場地探針被自己人污染**：力士的 `Body` 擋 `ECC_Visibility` ⇒ 地板射線打到人頭
   （z≈140）、膠囊重疊撞到彼此 ⇒ 整條路徑假 blocked。**場地探針必須排除活體。**
2. **二骨 IK 的子樹疊加雙重計算**：前臂 delta 用 ref 方向算、卻疊在已被上臂帶走的
   前臂上 ⇒ 手幾乎不動（`handErr=71.9cm`）。正解＝前臂 delta 要相對「已被上臂
   帶走之後」的方向算。腿 IK 沒這問題是因為它逐骨直接 Set，不走子樹疊加。
   **同一份公式換一種寫入方式（Set vs 疊加）就會換一種正確性條件。**
3. **臂長不足是獨立的第二隻**：`reachNeed=96.4 > armLen=78.0`。修 IK 只讓手伸到
   最遠，仍差 18cm——站位 58→40cm、前彎 52→64°、屈膝 24→32cm 才夠到。
   **「手沒到位」可以同時有解算 bug 和幾何不可達兩個病因，數字能分開它們。**
4. **空洞契約**：c10 初版只驗上限（逐幀位移 < 15cm），結果瓶子**從頭到尾沒動**
   也「通過」。真因＝`GetCeremonyBottleTransform` 依賴 `PenGripBoneName`，而那是
   `ApplyBowPose` 首次入鎖才做的一次性校準——開場儀式時沒人畫過畫，恆為
   `NAME_None`。**凡「不該跳」的契約必須同時有下限（該動的必須真的動），
   否則「什麼都沒發生」永遠通過。** 這隻是截圖自查抓到的（數字全綠）。

### 已知殘項

- **酒瓶外觀**：`whiskey.glb` 的 PBR 材質帶不進 FBX（只有槽名）⇒ 現用佔位配色
  MID（深綠瓶身／琥珀酒／和紙標籤／近黑瓶蓋，照抄實體筆的 BasicShapeMaterial 做法）。
  威士忌瓶在道場仍違和；換網格＝改 `NiceInkBottle.cpp` 一個資產路徑常數。授權＝C8。
- **連續運鏡未做**：儀式期間是既有的靜態 wide 機位（進出有 0.5s/0.35s blend＝
  不是硬切），計畫 §7 的「單一連續運鏡」屬外觀打磨，待 viewport 後再決定是否要。
- **崩塌期間本人畫面的黑幕淡入未做**：目前是沉睡瞬間直接全黑（HUD 既有行為）。
- **逐拍音效未接**：目前沿用既有的相位音（進 BottleSpin＝轉瓶聲、進 Seating＝喝酒聲），
  時機已不完全對應（喝酒聲在 Approach 開始時就響）。
- 眼睛閉合仍是二值貼圖切換（§7 誠實例外①）。

## 15. 風險登記

| 風險 | 徵候 | 對策 |
|---|---|---|
| 醉倒像木板 | viewport 主觀 | 曲線旋鈕先調；退路＝手 key clip（未決 E） |
| 六人圍圈互相卡住 | 有人到不了角位 | 角序保序＝路徑不交叉；步進只看時間，永不卡死 |
| 手搆不到瓶（sumo 體型＋肚子） | c9 超標 | 屈膝深度／前彎角是旋鈕；真搆不到＝改成半跪拾取（同一套 IK） |
| `WriteBowPoseConverged` 每 tick 10 pass 成本 | 幀率 | gait 已經每 tick 這樣做＝成本已知可負擔 |
| PoseableMesh 快取（寫完立刻讀＝上一幀） | 筆/脖子/相機讀到舊姿 | 一律走既有 `WriteBowPoseConverged`（內含 `RefreshBoneTransforms`） |
| 儀式期間伸縮脖跳過快取殘留 | 脖子破洞 | 姿勢有變即 `NeckStretch->ForceRebuild()`（既有 API） |
| 客戶端時鐘偏移 | 兩端瓶角不同 | c8 斷言；`GetServerWorldTimeSeconds` 是引擎既有校時 |


## 16. 舞台搬到房間正中央（2026-08-16 續，user 定案）

> 「我希望整個遊戲都在房間的正中央進行」

### 量測（三次才拿到可信數字——方法本身就是教訓）
1. **膠囊 overlap 不可用**：道場部件全是 complex-as-simple 碰撞，`OverlapBlockingTestByChannel`
   對三角網格查不到 ⇒ 首版把整片牆判成淨空、可走域一路延伸到掃描邊界。
2. **純射線也會被結構誤導**：改成量牆距後，+X 方向 12m 內量不到邊界（長廳＋兩端門洞）。
3. **直接問關卡＝唯一可信**：列出所有部件的名字與包圍盒，一次定案。
   **「射線/overlap 都會被場景結構騙，部件包圍盒不會。」**

### 關卡真相
| 部件 | 中心 | 範圍 |
|---|---|---|
| `floor_Shape` | **(403, 27)** | x −309…1115、y −352…406（**14.2 × 7.6 m 長廳**）|
| 榻榻米 ×3 | **(431, 41)** | x 130…732、y −157…240（＝天然舞台，也是 CLAUDE.md 記的基準點 430.7,40.9）|
| 拉門 ×8 | x≈−306 / x≈1110 | **東西兩端**各四片 |
| 天花板 | z≈363 | — |

**舊配置＝整場遊戲擠在 (0,75)＝長廳最西端、緊貼西側門口**；席位 3/4 射線淨空只有 72cm / 6cm。

### 改動
- `VictimLieSpot` (0,75) → **(430, 40)**（榻榻米中心）
- `SeatSpots` → 繞 (430,40) **半徑 230 的六等分環**（儀式圍圈 R=160 在其內側，Gather 保有靠攏動作）
- **`ClampToRoom`（演出鏡頭夾限）從桑拿房舊值改成道場實測值**：x −285…1090 / y −330…385 / z 40…340
  ——**不改就會把每一顆演出鏡頭拉回舊區、全部失準**（舊 x 上限 170 比新舞台西邊 260cm）
- `ViewWide` 寫死的 fallback (0,75,60) → 讀 GameState 的舞台中心
- 新增 `EnsureStageGeometry()`＝舞台幾何寫進 GameState 的**單一來源**（PostLogin 即生效）；
  改 `VictimLieSpot` 一處，鏡頭/走位/儀式全鏈自動跟

### 驗證
儀式 **11/0**（新位置重跑，瓶口指向誤差 0.0°、崩塌終點 0.0cm）＋fullloop **16/0**＋
截圖 11 張（力士圍在榻榻米上、酒瓶可辨識）。**手感與外觀待 user viewport。**


## 17. 醉倒期彈簧與沉睡外觀（2026-08-16～18，user viewport 追查）

### 倒下時肚子浮誇形變＝jiggle 彈簧飽和（我造成的，已修）
- 量測：崩塌全程 `jBelly` **max 8.00＝恰好 `JiggleMaxCm` 鉗位、mean 5.03**＝整段飽和；
  飽和時切向分量換成繞脊椎的旋轉並撞 25° 上限，而肚骨蒙皮權重域極大 ⇒ 大面積形變。
- 真因：jiggle 是**世界空間**彈簧，且有「單幀錨點移動 >100cm 直接貼齊」的傳送保護。
  舊制受害者**傳送**落地＝保護每次觸發、彈簧從不被激勵；儀式版是**連續倒下**＝每幀都在
  門檻內，「整具網格繞 90° 高速掃掠」變成真實激勵，量級遠超步行調校值。
  **鐵則：凡舊制靠瞬移繞過的保護，改成連續就會全部醒過來。**
- 修：`JiggleCollapseScale`（預設 0.4，只在 Collapse 拍、只對受害者）把響應壓回線性域
  ——修後 **max 4.40 / mean 0.86**，不再釘鉗位。**晃多少是口味＝viewport 旋鈕。**

### 躺著時的形狀＝既有設計，不是本批造成（A/B 實證）
- `robo_sleeppose_ab.py` 同機位傾印對賬：修掉落地殘餘餘振（入睡瞬間強制貼齊彈簧）後，
  **儀式路徑與舊傳送路徑逐項完全相同**（actor 變換＋Body/BowBody 相對變換＋25 根骨）。
- 沉睡姿勢的來源＝`SK_Sumo` 的 ref pose（bind pose）＋整具剛體旋轉 `BodyLieRelRot(0,90,-90)`；
  `SleepMesh` 從未被指派（只有 fallback＝StandMesh），`DrawPoseData.h` 只有
  `SitBones`/`LeanBones`、**沒有睡姿骨表**，`UpdateSleepBodyDouble` **脖子以下不擺骨**。
  看起來像「大字」是因為相撲 bind pose 本來就張開（rest 膝 X ±58.7cm、大腿間距 67cm）。
- **這是既有設計，user 明確表示照舊。未經指示不得更動。**
