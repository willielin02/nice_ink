# 醉夢圓形迷宮——實作規畫（SPEC v3.3 定案 #30/#31）

> 2026-07-11。本文件是甦醒小遊戲 v3.3 的完整實作規畫：架構、資料模型、生成管線、
> 網路事件流、難度參數表、測試計畫。設計權威是 `SPEC.md`；本文件只講「怎麼做」。
> 待定 #2（數值）與待定 #13（陷阱呈現）在此給出首版實作值，全部可調、等 playtest 校準。

## 0. 需求對照（SPEC 條文 → 實作承諾）

| SPEC 條文 | 實作 |
|---|---|
| 2D 圓形迷宮，中心→外緣出口＝甦醒 | 極座標環形網格（theta maze），出口＝外環缺口，跨出即 `ServerMazeExited` → `bEyesOpen=true` |
| 參數化生成、每回合不同 | server 每回合發種子，受害者 client 決定性重建（`FRandomStream`） |
| 陷阱＝其他玩家，恰一位置、整場固定、可重複觸發 | 陷阱格＝生成時定死；踩中偵測在受害者 client，永不移除 |
| 踩中＝退回上一存檔點／原點 | client 死亡序列狀態機；RespawnPos＝最近經過的存檔點，否則中心 |
| 兩存檔點＝噴射/拳腳，全自選 | 經過→`ServerMazeCheckpointReached`→沿用既有 SprayCharges/KickCharges；每回合每點只授一次 |
| 旋轉懲罰：只通知兇手、5 秒轉盤 −360~360、逾時 0 | `ClientOpenTrapDial` 只發兇手；server 5.6s 失效保險→0 度；轉盤＝滾輪（不徵用 lean-lock 游標） |
| 兇手公開、度數隱藏、動畫不可估計 | 死亡畫面亮兇手名；度數永不顯示；動畫固定時長＋晃動曲線（0 度也照播、不可分辨） |
| 其餘玩家零通知 | 事件只走 victim↔server↔killer 三點 Client/Server RPC；無 multicast、無 GameState 痕跡 |
| 延遲甦醒合法 | 出口不吸入——跨過缺口才算，站在缺口前無事發生 |
| 作畫者看不到夢進度 | 迷宮狀態全在受害者 client 本地＋server 驗證；不複製給他端 |
| 死亡可歸咎（rage 鐵律，待定 #13） | 陷阱破綻＝`TellRange` 內顯形的「躲很爛的力士」記號；生成保證繞路存在（陷阱格封死後四要點仍連通） |
| 酒越深夢越深（待定 #2 傾向） | `MazeParamsPerCup[3]`：整組參數按罰酒杯數換檔 |
| 退化態防護（待定 #2） | 生成接受迴圈：理想通關時間落 `[Min,Max]` 帶外就換種子重生成；永不通關靠規模上限＋braid 下限（**不加計時器**——保底計時＝保護受害者，違反 rage 純度） |

## 1. 核心架構

```
FDreamMazeParams   (USTRUCT)         難度參數，GameMode Config UPROPERTY，每杯一組
FDreamMazeLayout   (純 C++ struct)   一座迷宮：環/扇格、邊、陷阱格、存檔點、出口
FDreamMazeGen      (純函式)          Generate(Params, Seed, TrapCount) → Layout＋統計
                                     零 UObject、零時間源 → 可離線萬種子跑分布
UDreamMazeComponent (ActorComponent) 受害者 client：模擬（移動/碰撞/踩陷阱/存檔/出口）
                                     ＋死亡序列狀態機＋旋轉顯示變換＋canvas 繪製
ANiceInkCharacter                    RPC 端點（下述事件流）＋兇手轉盤本地狀態
ANiceInkGameMode                     發種子、驗證、轉盤路由、失效保險、robo hooks
ANiceInkHUD                          沉睡 UI 佈局（迷宮盤＋姿勢面板＋工具）＋兇手轉盤
```

**心智地圖活在螢幕座標系**——這是整個旋轉懲罰的實作本質：

- avatar 位置永遠存迷宮座標；顯示層一個 `DisplayAngle`，旋轉懲罰只動它。
- **輸入反向旋轉**：WASD 的螢幕向量乘 `DisplayAngle` 的逆變換才進迷宮座標——
  轉完之後玩家操縱的是「他看到的」，不是「迷宮記得的」。
- **0 度不可分辨**：動畫固定時長、無論淨值多少都照播（含 0）；晃動曲線讓度數
  數不出來，角速度上限讓定位點盯得住。
- **死亡序列堵圓點洩漏**：旋轉時 avatar 圓點不在場（死亡畫面→視野跳到重生點→
  牆型旋轉→圓點才浮現）——否則記住轉前圓點螢幕位置就能廉價反推度數。

## 2. 資料模型：極座標環形網格

- 環 0 ＝中心圓室（單格，半徑 1.5 cell）；環 1..N 每環若干扇形格，
  弧寬超標時扇形數倍增（格子近似正方）。座標單位＝cell（1 環厚＝1）。
- 邊只有兩種：**環邊**（r ↔ r+1，弧牆）與**徑邊**（同環相鄰扇形，徑向牆）。
  倍增環的環邊按外側子格的角域切分。
- 出口＝最外環隨機一格的外緣開口（缺口＝該格弧域中段）。
- 幾何全解析：碰撞＝子步進＋「新舊格相同或經開邊相鄰才放行」；牆＝閉邊的弧/線段，
  細分成短段畫進 canvas。旋轉自相似由資料結構保證（全圖只有弧與徑向線）。

## 3. 生成管線（`FDreamMazeGen::Generate`，接受迴圈）

1. **建網格**：`RingCount`／`BaseSectorCount`／`TargetCellArcWidth`。
2. **長迷宮**：growing-tree；`Branchiness` 在 backtracker（長廊）↔ random（多岔）
   之間內插；`RadialBias` 控制挖門偏好徑向或環向。
3. **編辮**：`BraidFactor` 比例的死路打通成環路（繞開陷阱與迷路自救的餘地）。
4. **放出口**：外環隨機格。
5. **放存檔點**（先於陷阱）：繞路成本帶驗證——
   `d(中心→點)+d(點→出口)−d(中心→出口) ∈ DetourRatio 帶 × d(中心→出口)`，
   噴射/拳腳各一格、彼此隔開。繞路成本＝「拿技能要多睡多久」＝三杯制耦合的實體。
6. **放陷阱**（數量＝其他玩家數 3–5）：不進前 `TrapMinDepthRing` 環、不佔
   中心/存檔點/出口、不貼存檔點、彼此隔 `TrapMinSeparation`；`TrapPathBias`
   加權貼主路徑。**硬約束：陷阱格視為封死後，{中心, 兩存檔點, 出口} 仍連通**
   ——「識破後繞路」必須有路可繞（可歸咎鐵律的圖論形式）。
7. **接受檢查**：BFS 最短路 ÷ `AvatarSpeed` ∈ `[MinIdealSolveSec, MaxIdealSolveSec]`；
   失敗換衍生種子重來（上限 8 輪，之後放寬並 log）。

陷阱→玩家對應：server 把作畫者 PlayerId 洗牌後隨迷宮種子一起發給受害者，
`TrapCells[i] ↔ ArtistIds[i]`；server 本身不需要 layout（信任模型見 §5）。

## 4. 受害者端模擬（UDreamMazeComponent）

- 只在「本地控制＋bAsleep＋!bEyesOpen＋已收到 ClientStartMaze」時運轉。
- **移動**：WASD（螢幕系→逆旋轉→迷宮系），子步進碰撞。滑鼠仍歸姿勢面板／轉頭，
  Q/E 噴射拳腳照舊（PollCounterplay 不動）。
- **戰爭迷霧**：只畫 `VisionRadius` 內的牆（`FogEdgeSoftness` 淡出）；
  **看過的牆不留殘影**（`WallAfterglowSec` 預設 0——自動地圖＝替受害者記憶＝保險）。
  外緣大圓輪廓常駐（旋轉對稱＝零資訊）；出口缺口進視野才顯形。
- **陷阱破綻（待定 #13 首版）**：`TellRange` 內畫「躲很爛的力士」記號
  （膚色圓點＋髮髻＋微晃）；`TellRange > TrapTriggerRadius`＝先看見再踩到，
  貼近偵察是技術。`TrapTriggerRadius` 預設封滿走廊（繞路是唯一解）；
  調小即開放「擠身而過」高風險動作（playtest 開關）。
- **存檔點**：常駐圖示穿霧顯示（位置可見、路徑要自己找——「賭裸奔」要成為
  真選擇得先看得見在賭什麼；我方補位設計，user 可否決）。經過＝存檔＋發技能。
- **死亡序列狀態機**：Walking → 踩中（送 `ServerMazeTrapHit`）→ DeathScreen
  （亮兇手名，`DeathScreenSec`）→ AwaitRotation（視野移到重生點、圓點隱藏、
  牆型可見＝給「盯緊旋轉」的參考畫面；兇手轉盤等待藏在這段）→ 收到
  `ClientApplyMazeRotation` → Rotating（固定 `RotAnimDuration` 晃動動畫）→
  圓點在重生點浮現 → Walking。client 10s 失聯保險（僅魯棒性，非設計保護）。
- **出口**：跨過外緣缺口→`ServerMazeExited`（僅 Drawing 相位）→ server 睜眼。
  之後照舊：WASD＝現身（既有 `HandleEmergeRequest`，閘門從 `MinigameHits>=3`
  改為 `bEyesOpen`）。

## 5. 網路事件流（信任模型沿用既有：client 判定、server 驗身分/相位/上限）

```
EnterSeating(server)：
  蒐集作畫者 id、洗牌 → 選種子 → Victim->ClientStartMaze(Seed, Params, ArtistIds)
踩陷阱：victim client 偵測 → ServerMazeTrapHit(KillerId)
  → server 驗證（Drawing/Seating、victim 本人、沉睡未睜眼、KillerId 合法）
  → Killer->ClientOpenTrapDial(5s)（只發兇手）＋ server 5.6s 失效保險
兇手轉盤：滾輪選 −360..360，5s 到自動 ServerSubmitTrapDial(angle)
  → server 驗證（是 pending 兇手）→ Victim->ClientApplyMazeRotation(angle)
存檔點：victim client → ServerMazeCheckpointReached(type)
  → server 每回合每點一次 → ++SprayCharges / ++KickCharges（既有欄位；改 OwnerOnly 複製）
出口：victim client → ServerMazeExited → bEyesOpen=true（睜眼貼圖＝唯一破綻，照舊）
```

- **零第三方資訊**：無 multicast、無 GameState 欄位；「受害者退回原點」只能靠
  當事人語音流出（SPEC 規則本體，不是最佳化）。
- server 不重建 layout、不驗證位置宣稱——與 `ServerMinigameHit` 信任 client
  timing 同一個 party-game 取捨，記錄在案。

## 6. 難度參數表（`FDreamMazeParams`，GameMode `UPROPERTY(Config, EditAnywhere)`）

外層 `TArray<FDreamMazeParams> MazeParamsPerCup`（0/1/2 杯各一組）＝
「酒越深夢越深」直接烘進架構。DefaultGame.ini 可覆寫、免重編譯。

| 軸 | 參數 | 預設 (杯0 / 杯1 / 杯2) | 控制什麼 |
|---|---|---|---|
| 規模 | `RingCount` | 5 / 6 / 7 | 迷宮深度＝作畫時間主旋鈕（HUD 可讀性上限 ~9） |
| | `BaseSectorCount` | 8 | 第 1 環扇形數 |
| | `TargetCellArcWidth` | 1.25 | 弧寬超標即倍增扇形（走廊密度） |
| 質地 | `Branchiness` | 0.35 / 0.4 / 0.45 | 0=長廊少岔 ↔ 1=短枝多岔（記憶難度） |
| | `BraidFactor` | 0.30 / 0.22 / 0.15 | 環路密度＝繞開陷阱/迷路自救餘地 |
| | `RadialBias` | 0.55 / 0.5 / 0.45 | 徑向廊多（好定向）↔ 環向廊多（易暈） |
| 視野 | `VisionRadius` | 2.8 / 2.3 / 1.9 | 霧半徑＝讀圖難度主旋鈕（醉夢昏暗） |
| | `FogEdgeSoftness` | 0.8 | 霧緣淡出帶寬 |
| | `WallAfterglowSec` | 0 | 殘影（動它前先問「在保護誰」） |
| 移動 | `AvatarSpeed` | 2.4 cells/s | 通關時間＋閃避反應餘裕 |
| 陷阱 | `TrapMinDepthRing` | 2 | 開局安全區 |
| | `TrapMinSeparation` | 3 | 陷阱間隔（圖距） |
| | `TrapPathBias` | 0.5 / 0.65 / 0.8 | 貼主路（擋路度） |
| | `TellRange` | 1.7 / 1.5 / 1.3 | 破綻顯形距離＝可歸咎性的量化本體 |
| | `TrapTriggerRadius` | 0.42 | 預設封廊；調小＝可擠身而過 |
| 存檔點 | `CheckpointDetourMin/Max` | 0.15–0.55 | 拿技能的繞路成本帶（三杯制耦合） |
| 驗證 | `MinIdealSolveSec` | 3 / 4 / 5 | 秒通關防護（接受迴圈下界＝各檔原生 p10，只砍退化短尾） |
| | `MaxIdealSolveSec` | 20 / 25 / 30 | 規模上界 |
| 旋轉 | `RotAnimDuration` | 3.0 / 3.4 / 3.8 | 追蹤窗口長度（0 度也播滿） |
| | `MaxAngularSpeedDeg` | 240 | 盯得住的上限（追視極限） |
| | `WobbleAmplitudeDeg` | 60 / 75 / 90 | 數不出度數的保證 |
| | `WobbleOctaves` | 2 | 晃動頻率層數 |
| | `DeathScreenSec` | 1.2 | 兇手亮相時長 |

**校準紀錄（2026-07-11 首輪開表，400 種子/組）**：理想通關時間幾乎只由 `RingCount`
決定（ring5/6/7 原生 p10≈3/4/5s、p50≈5/6/7.3s；braid/radial 對最短路影響 <1s——
從中心長出的 growing-tree 天生徑向距離近最小）。真實遊玩（迷霧＋走錯＋每次死亡
約 10s 懲罰鏈）估為理想值 2–4 倍 → 回合約 30–90s。繞路成本原生 p50≈0.33，落帶內。
**要拉長回合就加環數（並重開表定帶），不要指望 braid/radial。**

**不進表的 SPEC 定案常數**：轉盤 5 秒、±360°、逾時 0 度（程式碼常數）。
**記帳項**：陷阱數隨人數 3–5 浮動，4 人房 vs 6 人房實質難度不同——要不要用
`RingCount` 隨人數補償，開統計表＋playtest 後再決定。

## 7. 負面守則（刻意不存在的東西——動之前先問「這條規則在保護誰」）

- **不做獨特地標**（燈籠、裝飾）——旋轉後廉價重定位＝保方向感的保險。牆面特徵均勻（美術禁令）。
- **不做已探索殘留地圖**、**不做通關保底計時器**、**不做度數任何形式的顯示**。
- **姿勢面板永不顯示墨跡**（既有護欄，不因迷宮 UI 改版而鬆動）。

## 8. HUD 佈局（canvas 直畫，無 UMG）

- 沉睡畫面：全黑底；迷宮圓盤置中偏上（半徑 ~min(W,H)×0.30）；姿勢面板左下（沿用）；
  工具庫存右下（沿用）；語音提示底部（沿用）。
- 迷宮視覺：深靛底、淡色牆、白點 avatar（帶朝向短刺）、存檔點 S/K 菱形圖示、
  陷阱＝膚色點＋髮髻記號（TellRange 內）、外緣圓常駐、出口缺口進視野才亮。
- 兇手轉盤（覆蓋在作畫視野上，不打斷 lean-lock）：圓盤＋指針＋倒數條；
  **滾輪**每格 ±15°；5 秒到自動送出當前值（預設 0）；不顯示給任何第三人。
- 終局昏死（Finale 的輸家）：無迷宮（server 不發）——黑屏＋「昏死不醒」。

## 9. 退役清單（舊往復指標小遊戲，SPEC 定案 #5 → #30 取代）

- `ANiceInkCharacter`: `MinigameHits`、`MinigameIndicatorPos`、`PollMinigame`、
  `MinigameCooldownUntil`、`ServerMinigameHit`。
- `ANiceInkGameState`: `MinigamePeriod/ZoneWidth/MissCooldown`。
- `ANiceInkGameMode`: `MinigamePeriodSeconds/ZoneWidthByCup/MissCooldownSeconds`。
- `ANiceInkHUD::DrawVictimSleepUI` 的指標軸／pips／冷卻段。
- 現身閘門 `MinigameHits >= 3` → `bEyesOpen`（robo 的 bForce 路徑不變）。

## 10. 測試計畫

- **離線統計**：`NiMazeStats <種子數> <杯>`（Character Exec）——跑 N 種子輸出
  理想通關秒、繞路成本、陷阱貼路距離、重生成率的分布——待定 #2 的數值不用猜，開表看。
- **robo 全事件流**（`Tools/RoboTest/robo_maze_test.py`，3 客戶端 listen server）：
  1. 等 Drawing → 讀受害者 client 的迷宮摘要（格數/陷阱/存檔點/出口）；
  2. Debug 傳送 avatar 到陷阱格 → 真事件鏈（client 偵測→Server RPC→兇手
     ClientOpenTrapDial）→ `DebugRoboMazeDial(170)` 代兇手送度數 → 驗證受害者
     `DisplayAngle` 旋轉、重生點正確；
  3. 傳送到存檔點 → 驗證 SprayCharges=1；再傳送踩陷阱 → 驗證重生在存檔點；
  4. 傳送到出口 → 驗證 `bEyesOpen`；`DebugRoboEmerge` → 巡禮照常。
  - robo 原則：debug 函式只改狀態（傳送），RPC 發送一律發生在元件 TickComponent
    （遊戲 tick、guard 外）；ini 的 StartupScripts 行測完必拔。
- **驗收閘門**：robo 只證明幾何與事件流成立；旋轉動畫「盯得住、數不出」的手感、
  迷霧壓迫感、陷阱破綻讀感，**以 user viewport 為準**。

## 11. 檔案清單

新增：`Source/NiceInk/Public/DreamMaze.h`、`Private/DreamMaze.cpp`（參數/佈局/生成/統計）、
`Public/DreamMazeComponent.h`、`Private/DreamMazeComponent.cpp`（模擬＋繪製）。
修改：`NiceInkCharacter.h/.cpp`（RPC 端點、轉盤本地態、退役舊小遊戲）、
`NiceInkGameMode.h/.cpp`（參數、發種子、轉盤路由、robo hooks）、
`NiceInkGameState.h/.cpp`（拔舊欄位）、`NiceInkHUD.h/.cpp`（沉睡 UI 改版＋轉盤）。
新增：`Tools/RoboTest/robo_maze_test.py`。

## 12. 我的補位設計（user 可否決的三項，均已給推薦）

1. **存檔點圖示穿霧常駐**（位置可見、路徑自己找）——「全自選」要是知情的賭。
2. **技能每存檔點每回合一次**（重複經過只更新重生點、不補 charge）——沿用既有 charge 語義。
3. **兇手轉盤＝滾輪**——兇手可能正 lean-lock 作畫，游標是他的筆，不徵用。
