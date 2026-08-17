# Robo-test harness（自駕 PIE 測試）

## Play 模式切換（2026-07-17 起有兩種工作流）

- **party 模式**（開發試玩預設）：`play_mode_party.ps1`——編輯器開機＝主選單圖，
  Play＝**4 個獨立行程視窗**從主選單起跑（真 ServerTravel＋真 LAN 搜房＝完整
  happy path：選單→建房/加入→大廳→ENTER 開局）。獨立行程較重（每窗一次引擎啟動）。
- **robo 模式**（跑本目錄任何測試前必切）：`play_mode_robo.ps1`——道場圖＋
  3 客戶端 listen 單行程 PIE（tick harness 的 python 只搆得到同行程的 PIE 世界）。
- 兩支腳本都要**編輯器關閉時**跑（編輯器退出會回寫 play 設定）。


編輯器啟動時自動：開 PIE（3 客戶端 listen）→ 等 Drawing → 驅動 lean-lock
全鏈驗證（湊近複寫/細筆筆劃/踹飛中斷/彎腰擺拍連拍）→ 結果寫到 stdout log。

啟用方式（測完務必移除，否則每次開編輯器都會自動跑）：
在 `Config/DefaultEngine.ini` 加入

    [/Script/PythonScriptPlugin.PythonScriptPluginSettings]
    +StartupScripts=<絕對路徑>/Tools/RoboTest/robo_leanlock_test.py

要點（血淚教訓）：
- MCP/tick 回呼裡的 python 受 FEditorScriptExecutionGuard 影響，RPC 全部被壓成本地——
  凡是要走網路的動作一律經 GameMode 的 Debug*（timer-deferred）鉤子。
- 屬性複寫不受 guard 影響，直接讀即可。
- 受害者/擁有者的輸入輪詢會「撤銷」機器人塞的輸入態（如 peek）——這是產品正確行為，
  不是 bug；輸入手感只能真人驗。

## 醉夢描圖套件（2026-08-02 SPEC v4.0——迷宮退役、描圖取代）

- `robo_trace_test.py`＝甦醒小遊戲 v4.0 常駐套件（**22 檢查**）：t1 開局發夢
  （統一 60s×v_max=線長 108；帶半寬=筆寬 0.30＝發夢當下從受害者
  TattooNibDiameterCm 導出；**DebugForcedTraceSeed=42＝cup0 池[1]=Fan＝烘焙表
  索引 2** 的選圖決定性——烘焙表序/池組成改動要重對）、t2 生成統計三檔×200 種子（自距 ≥2.6×帶半寬＋重試率＋
  **blobFallback=0**＝模板自己站得住＋**autoDevViol=0**＝pursuit 可描性模擬
  全樣本帶內＋圖案池覆蓋）、t3 夢不複製（server/observer 端元件 inactive）、
  t4 自動沿線描（真實追趕/判定路徑、速度=v_max、零失敗）、t5 搖晃攻擊
  （扣款 500/冷卻拒收/抬針安全＝fails **delta** 斷言）、t6 越線=重來（fails+1、
  進度歸零、針回起點；**curS 是環上位置**——起點附近可讀 total-ε，斷言用
  環域距離）、t7 描完→bEyesOpen→現身進巡禮（108cm≈60 遊戲秒、牆鐘上限 150s）、
  t8 噴射封存（零投射物、charges 恆 0）。結果檔 Saved/robo_trace_result.txt。
- `robo_trace_shots.py`＝描圖三態截圖自查（seat0 停靠視口；**DebugForcedTrace-
  Seed=98＝cup0 池[4]=Wave（北齋浪）**）：描圖中/搖晃中/失敗紅框。血價：失敗閃
  首版用頂點 alpha 蓋整盤＝不透明大紅餅（canvas 三角形 alpha 混合不可信）；
  autopilot 前瞻是曲率的函數（2cm 前瞻在急彎切內側出帶；現值 0.7＝與離線
  pursuit 閘同值）。
- **迷宮套件退役**（v4.0）：`robo_maze_test.py`／`robo_maze_shots.py`／
  `robo_lightring_shots.py`／`robo_exit_seating_repro.py` 隨迷宮封存——照跑會卡
  wait（迷宮永不啟動）。**活套件的喚醒鉤子已全數換血**：
  `DreamMaze.debug_trigger_exit()` → `DreamTrace.debug_force_complete()`
  （feign/orbit/neck_observer/neck_probe/seat1_sync/drawpose_shots 已改）。
- 沉睡者夢內回饋音＝UiClick（音效層沉睡靜音唯一豁免）；robo 攻擊者＝
  `GameMode.DebugRoboShake()`（timer-deferred、第一位非受害者、走真 Handle 路徑）。

## 截圖自查範本（2026-07-15）

- `robo_lightring_shots.py`＝迷宮呈現截圖自查的現行範本：DebugForcedVictimSeat=0
  （受害者＝主機停靠視口）＋ctypes SetForegroundWindow 聚焦＋HighResShot 四態連拍
  （原點/走廊/旋轉中/旋轉後），結果檔寫 Saved/robo_lightring_result.txt。
  舊 `robo_maze_shots.py`（seat1 浮動視窗版）不可靠——HighResShot 只有聚焦視窗會處理。

## 作畫姿勢/穿膜/偷瞄截圖矩陣（2026-07-17）——**過期儀器（07-27 實錘勿再跑）**

- **本節套件屬長跪制（v3.8）時代**：07-18 直接畫制（剛臂＋伸縮針）後「頭距鎖點
  22cm」「眼位 10cm」契約不再成立（伸縮針的存在就是為了讓身體不必把臉湊到 22cm），
  `DebugRoboPeekHold` 鉤子也已隨偷瞄退役刪除——照跑＝8 個假 FAIL＋一個 EXC
  （07-27 誤跑實錘）。作畫制現行常駐套件＝`robo_directdraw_test.py`（72 檢查）；
  首鎖視野/握筆/旁觀抖動＝`robo_remotejitter_probe.py`。

- `robo_drawpose_shots.py`＝作畫姿改制（長跪，2026-07-17 二改）的驗收儀器：第一人稱（host=作畫者）
  ×四鎖定點（肚頂/臉/側腹/大腿）＋偷瞄旗艦鏡頭；第三人稱（client2=模特、host=攝影機）
  ×作畫/偷瞄。數值斷言：頭到落筆點 22cm、腳貼地、膝朝前（位置重定向的鏡射驗證）、
  view 走本體相機（OwnerNoSee 生效前提）、眼位 10cm、偷瞄鏡頭對準替身真頭、偷瞄升頭。
  產出 drawpose_*.png＋Saved/robo_drawpose_result.txt。
- 新 debug 鉤子：`DebugRoboPeekHold(bool)`＝模擬按住 Shift 偷瞄（直設 bPeeking 會被
  owning 端輪詢反殺——同裝睡教訓）；`DebugRoboEnterLean(Target, Anchor, Normal)`＝
  C++ 內 trace 皮膚表面點再走真 ServerEnterLean（python 硬編體內錨點＝眼位埋進肉裡
  的假警報；5.7 python 的 HitResult 反射不可用）。
- 既有 `robo_leanlock_test.py` 兩條「FAIL」＝預期：peek replicated FAIL（owner 輪詢
  反殺 robo 態——改用 DebugRoboPeekHold 才測得到）；kick PARTIAL（GNiceInkKickEnabled
  =false 拳腳封存中，該段測試過時）。

## 出口死鎖迴歸（2026-07-15）

- `robo_exit_seating_repro.py`＝「入座階段衝到出口→Drawing 推門」迴歸：
  重現 2026-07-15 user 抓到的死鎖（Seating 停門檻＋RailNavigate 端點無 Exit 分支
  ＝永遠卡死）；期望 VERDICT: EXITED。跑前把腳本裡 OUT 路徑改到可寫位置。
  儀器：DebugPlaceAtExitCell（傳送不觸發）＋DebugRoboNavTo（強制導航走真實
  RailNavigate 路徑——robo 無法注入滑鼠/左鍵，自然出場只有這條驗法）。

## 甦醒臉指向制＋轆轤首伸縮脖（2026-07-16 三改版，取代 1-DOF 環繞軌道）

- `robo_orbit_test.py` v4＝臉指向制端到端驗證（24 項）：閉眼盲瞄不動骨/不漏指向→
  睜眼零代打＋FOV 72→DebugRoboSleepLook 設臉指向（睜眼語義＝(Yaw,Pitch)=(az,tilt)，
  az 180=腳側/0=頭頂側、tilt 0=朝天；閉眼語義＝盲瞄 twist/bend 不變）→
  (az,tilt) 經真實 ServerUpdateSleepAim 鏈上 server（tilt 過量測鉗位表，170 只給 100）→
  相機嚴格錨眉心（15.26cm）＋臉朝向、roll≈0→他端同純函數求值零誤差→
  抬升平台制（深壓/仰看皆 46cm 恆高）→脖子（UNeckStretch）rest 收合隱藏/伸長可見→
  現身 FOV 還原 90；尾聲八方位截圖 aim_azXXX（tilt 85）。結果檔 Saved/robo_orbit_result.txt。
- `robo_seat1_sync_test.py`＝受害者=client 的跨端一致性＋對視鏈迴歸（11 項）：
  入睡/回座 actor yaw 兩端一致（server 對 autonomous proxy 的傳送 yaw 永不推回
  owning client——ClientSyncPoseTransform 修的那隻 bug 的迴歸籠）＋頭骨「旋轉」三方
  一致（頭骨原點=旋轉樞軸、位置檢查對方位不敏感＝舊測試盲區）＋複製值逐位一致＋
  幾何反解 (az,tilt) 對準真實玩家（相機前向誤差 <6°＝對視鏈終極驗證）。
- `robo_neck_observer.py`＝伸縮脖外觀截圖（seat1＝主機視窗當旁觀機位、拍真複製鏈
  外觀）：(az,tilt) 陣列 × side/close 兩機位，neckobs_azXXX_tXXX_*.png。
- `robo_neck_probe.py`＝脖管幾何診斷（GetDebugSummary 現場數字＋DumpNeckMesh CSV
  傾印＋A/B 隱藏對照截圖）。
- `robo_neckdraw_probe.py`（2026-07-26）＝**作畫姿勢（lean-lock）脖子探針**：模特兒
  DebugRoboEnterLean 鎖三點（belly/face/flank）→ 旁觀機位拍後頸/喉側特寫＋
  DumpNeckMesh CSV＋弦長/骨骼數字；配套離線分析（逐列折角/軸向跨距/環半徑）見
  memory `project_neck_drawpose_fix`。抓到「壓縮域設計錯配」（喉摺 111°）與
  「隱藏判定用環心距誤殺」（後頸破洞）兩隻。**讀數注意：DumpNeckMesh 在 vis=0 時
  回傳的是上一次可見的舊 section——先對 vis 旗標再信 CSV。**
- 陷阱補充：CameraComponent 世界旋轉 python 沒有 get_component_rotation——
  用 get_socket_rotation("None")；PC 取 pawn＝get_controlled_pawn()；
  debug_robo_emerge() 無參數；set_actor_location 只吃 3 參數。
- **GUI 編輯器崩潰後殘留 Saved/Autosaves/PackageRestoreData.json＝下次啟動被
  Restore 對話框擋死（StartupScripts 永不執行、log 死寂）**——robo 啟動前清 Autosaves。
- ini 的 StartupScripts 行用「git checkout 還原再 Add-Content」換腳本——直接 replace
  容易疊行（腳本會跑兩份互咬）。**換完必 `grep -c StartupScripts` 確認只有一行**
  （07-29 實錘：radius 探針沒拆就疊 veilshot＝兩 harness 互咬，radius 的
  ServerExitLean 把 veilshot 的鎖踢掉）。

## 可畫域橢圓制儀器（2026-07-29）

- `robo_veilshot.py`＝veil 殼視覺自查：host 鎖肚頂→等橢圓擬合完（maskOn=1）→
  三角度截圖（正對/深俯 75°/邊界 58°）→**重鎖段**（exit→re-enter→RAW→截圖）＝
  「第二鎖遮罩消失」類 bug 的迴歸籠。**HighResShot 1＝視窗原生尺寸**——固定
  1280x720 的縱橫比≠視窗會把 HUD/投影驗證做成假象。
- `robo_reachradius.py`＋`DebugRoboReachStats`＝可達域半徑統計（R100 嚴格圈/
  R95 穩健圈/最近不可解點分布）：橢圓域大小的量測儀（設計裁決前先量）。
- `robo_reachmap_probe.py` 現行語義（顯示制後）：游標全自由（eff=命令值）、
  死區誠實 reach=0、回程無黏死；RAW 行印原始 summary（tblCols 時代欄位已改
  maskRow=採樣游標/maskOn=橢圓就緒）。

## veil 覆膜制儀器（2026-07-31）

- `robo_veilflicker.py`＝veil 顯示雙用探針：①**時域閃爍差分**——鎖肚頂→深俯角
  （畫面大半=veil）→同機位隔 1s 連拍 veilflk_a/b，配 `veil_flicker_diff.py`
  （face pipeline venv 的 python 跑）逐像素差分：p99≤2/255＝TAA 噪底、更高＝
  時域閃爍實錘（translucent 螢幕空間偏移取樣在 TAA/TSR 下天然閃——07-31 焦外版
  模糊 4-tap 就是這樣死的）；②**跨鎖點一致性**——第二段換鎖下腹（較小橢圓）拍
  veilflk_c，驗條紋實體寬度不隨橢圓縮放（固定公分制的迴歸籠）。
- 鐵則：閃爍/呼吸類 bug 單張截圖是瞎的，差分才是儀器；口味迭代（條紋深淺/帶距）
  每輪跑一次此探針＝順手拿到外觀截圖＋時域數字雙證據。

## 稿筆游標制儀器（2026-07-31～08-01）

- `robo_stencilcursor_probe.py`＝稿筆（Stencil）操作契約常駐探針（20 檢查）：
  c1 入鎖掛游標、c2 兩鎖點同滑鼠步數位移比（增益恆定）、c3 停手零漂移、
  c4 gaze 收斂、c5 快掃落墨、c6 relatch 角度命令逐字、**c7 單擊守恆**
  （按住 1.5s 零滑鼠→恰落 1~4 針：下限=點得出點、上限=靜止不灌墨——08-01
  拆移動閘後的守恆鎖）、**c8 慢速域雙段**（0.05 單位/tick＝舊閘 3°/s 門檻下→
  按住期間墨在流 inflight gain≥1〔拉繩穩定器 08-02：墨尖落後游標 ≤L；
  預設 0=關〕＋收筆補完後全量守恆 gain≥5；舊閘下=0＝回歸鎖）。
  （08-02 曲線制 c9/c10 已隨功能整組刪除——user 定案；決策史見帳本。）
  **c9 切工具視野不跳**（08-02 二段，user 定案「以切換時視野在哪為準」）：游標
  拉離中心 ~9° 後切 Liner→相機朝向凍在原地（summary camAz/camTilt 真值差 ≤0.15°）
  ＋aim 歸位到相機＝游標歸中；切回稿筆→相機仍不動、游標從中心命中點再生（5 檢查）。
  結果檔 Saved/robo_stencilcursor_result.txt。
  **量級鐵則**：游標增益實測 ≈0.52°/單位＝0.37cm/單位（c2=24 單位走 9cm）——
  新增滑鼠舞步先用這個數反算尺寸，別用感覺估（08-02 血價：72 單位腿=27cm
  飛出身體=三假 FAIL）。
- `robo_fovaxis_probe.py`＝視野半角量測儀（08-01 邊緣推擠戰役）：deproject
  畫面中心/四緣實測可視半角 vs 程式假設——實錘引擎維持**垂直 FOV**
  （halfV 恆定、halfH 隨視窗長寬比走；「FOV36=水平半角 18°」只在 16:9 成立）。
  任何「邊界/邊緣帶/螢幕比例」類手感 bug 先跑這支拿真實半角，不要用 FOV 假設心算。
  結果檔 Saved/robo_fovaxis_result.txt。
- 邊緣推擠終案（08-01 六修）：讓位錨=**游標標記**（GetStencilCursorHudWorld
  投影）＋四邊統一像素帶——貼邊主詞必須同時「可見」且「恆隨手」（生 aim/
  濾波 aim/筆網格尖三代錨的死因各異，全史=DIRECT_DRAW_PLAN.md 08-01 各節）。

## 摺り足步態＋軟肉彈跳儀器（2026-08-04）

- `robo_neckwhole_shots.py`（2026-08-15）＝脖子縫合版（SK_Sumo_Whole）截圖套件：host
  當機位、client 模型當被拍者，站立平視/抬頭 60°/低頭 60°/lean-lock 埋頭 各正面
  （-forward 側 130cm、頭高）＋側面；產出 neckwhole_*.png 自查「單一連續皮膚、無
  切線/材質差/楔縫」。機制契約由 orbit/feign/gait/lookpitch 守；質感=user viewport。
- `robo_neckgap_shots.py`（2026-08-16）＝站立/作畫脖子縫隙 A/B 截圖＋量測：host 眼睛當機位
  （離 Neck 骨 75cm、地板高）、client 模型當被拍者，站立/俯仰 ±8°/±60°/lean 各正側背，
  每張記 GetDebugSummary（meshw=身側環真權重配對數、tableErr=烘焙表 vs 真權重環點差、
  resampSkew=角度重取樣端點 vs 孿生頂點距離）；cvar `ni.NeckRingFromMesh 0/1`（環權重源）、
  `ni.NeckStretchOff 1`（整條脖子關掉＝分辨破圖是補丁還是殼）、bSkeletalStandEnabled 關
  ＝靜態未切 SM 對照；lean 要在 **server 世界的 model actor** 上呼 DebugRoboEnterLean（guard
  把 RPC 壓本地）。啟動 log `NeckStretch: ... seamNormalGap` >2° ＝縫會現形（資產管線出事）。
- `robo_lookpitch_probe.py`（2026-08-15）＝站立視野俯仰上身探針（**9 檢查**）：本人
  相機 pitch ±40° → 本人端與旁觀端 Neck→Head 骨向量 Z 分量同號變化（c1 抬頭
  dz>+0.02、c2 低頭 dz<-0.02、c3 兩端同量 |Δ|<0.02＝複製追趕收斂；閾值對應
  12° 上限×指數曲線）＋c5 視野 89° 頭停在 12° 上限（dz 帶 0.05~0.25）＋**c6 俯仰
  掃描 −89~89 每 7° 旁觀端伸縮脖 |pitch|≥5 必可見**（NeckHideChordCm 門檻迴歸籠）＋c4 抬頭中
  轉身 90° 頭骨相對身體水平偏航 <25°（user 定案：左右不上身、頭身零相對位移）。
  量骨向量不量骨旋轉＝不賭 FBX 骨軸向（陷阱年鑑）。
- `robo_gait_probe.py`＝骨骼站姿/摺り足/彈跳常駐探針（**10 檢查**；走法＝四段
  身體相對方向：前走/右橫移/斜走/邊走邊轉 120°/s，每段先傳送回房中心）：
  c1 顯示換軌（站立 Body 隱形/BowBody 可見）、c2 雙腳恆貼地（踝骨 Z ≤ rest+3cm
  ——構造保證實測面）、c3 撐地腳世界釘住（慢腳速<體速一半、佔比 ≥70%；只評
  前走段——雙軌守恆式在主軸成立）、c4 沉腰屈膝（髖低 ≥5cm）、c5 彈跳激勵
  （肚彈簧等效位移峰 ≥0.4cm——旋轉耦合後骨位移歸零、量測面=LastSpringCm）、
  c6 停步收斂（3s 肚<0.3＋髖回位）——**注意 c6 量的是彈簧內部狀態，不是「肉回到
  原位」**（2026-08-18 定罪：錨點被污染後彈簧讀 0、骨頭卻歪 3.6cm，c6 照樣 PASS
  ＝空洞契約）；回原位的契約住在 `robo_jiggle_rest.py`、**c7 防飽和**（肚釘鉗位樣本佔比<50%——
  「絕對速度阻尼等速拖尾 2ζv/ω 恆撞鉗位」與「驅動頻率撞彈簧共振」兩病的迴歸籠）、
  **c8 雙腳不越側帶**（全走法全樣本 |footX| ≥ 帶下限-2 容差＝穿膜構造保證面）、
  **c9 膝恆外開**（|kneeX| ≥ 40 vs rest 58.7＝馬步外弓保留）、**c10 左右大腿
  骨段間距**（≥30、rest≈67＝骨級穿膜代理）＋四向截圖矩陣（side_fwd/front_lat/
  front_turn/back_settle——正面＝設 control yaw 面向南牆機位再橫移）。
  鉤子＝`DebugRoboWalk(dirX,dirY,secs)`（PollMove 消化＝與真鍵同入口）＋
  `DebugRoboGaitStats()` 機讀摘要＋`DebugRoboViewFrom(dx,dy,dz)` 任意方位觀察
  相機。結果檔 Saved/robo_gait_result.txt。
  **鏡位血價**：房中心+角色前方 230cm＝相機正好塞進座位角色體內（front 系全滅）
  ——截圖一律南牆機位（40,-240,25）＋「讓角色面向鏡頭」拍正面。
- `robo_jiggle_rest.py`＝**「停下＝回到原位」契約**（2026-08-18；user 抓「暫停一切
  動作時肥肉定格在非原位」）：走 1.1s→停 5s ×3 循環，量**骨頭 CS vs pose 層真 rest**
  （REF＝當場 `jiggle_enabled=False` 讓 pose 層 rest 露出來）。
  c0 無動作零噪音、cN_returns_to_rest（彈跳層殘留 <0.02cm/0.02°）、
  cN_was_excited（峰值 >1cm＝**下限也驗**，防「什麼都沒發生也 PASS」）。
  量測面兩層要分清：`own_dev`＝扣掉 Hips 共模後**彈跳層自己**的殘留（契約面）、
  `dev`＝絕對位移（含 pose 層自己的殘留，實測會間歇留 0~0.14cm＝步態層另一筆帳）。
  修前 2.18~3.58cm 永久凍結、修後 0.000cm；回原位耗時實測 **1.20s**（ζ=0.32、
  肚 2.1Hz ⇒ τ=0.237s）。結果檔 Saved/robo_jiggle_rest.txt。
- `robo_headlight_probe.py`＝頭燈假光斷層診斷 A/B（2026-08-05）：同機位三張
  ——skel（骨骼現行）/flat（HeadlightFloor=1 關頭燈）/statue（bSkeletalStandEnabled
  =False 舊雕像）。判讀：斷層 flat 消失=著色（法線）病、flat 仍在=貼圖病；
  statue 無=兩資產分歧。胸斷層戰役定罪鏈（真幾何+SM 管線抹軟法線+**5.7
  Interchange 無視 FbxImportUI 選項**鐵坑）全文=Docs/BODY_MOTION_PLAN.md 四輪節。
- 探針教訓三條：①新探針 boot 段必關 bThrottleCPUWhenNotForeground（背景節流
  假數據簽名＝多個 moving 樣本 phase/腳速**完全相同**＝巨量 dt 幀混疊）；
  ②道場 X 域 ~440cm——走路探針不准直走 >1.5s，一律「朝/背房間中心」由當下
  位置實算方向＋折返；③彈簧類數值探針要有防飽和契約——「有晃」(c5) 和
  「晃得像話」(c7) 是兩個獨立可 FAIL 的維度。

## 入睡儀式套件（2026-08-16～18）

- `robo_ceremony_test.py`＝**11 契約**常駐套件：c1 轉瓶終角指向受害者（±6°）、c2 揭曉時機
  （Spin 結束前 VictimPlayerId 恆 INDEX_NONE）、c3 **零 teleport**（逐取樣位移上限）、
  c4 崩塌連續性（Body 相對旋轉/位移逐幀角步有界）、c5 **交接零跳變**（崩塌終點 ≈ 躺位、
  bodyRel 逐位＝BodyLieRelRot）、c6 六拍全走到、c7 儀式期間輸入無效、c8 圍圈到位、
  c9 手到瓶頸、**c10 瓶子跟手（上限＋下限雙向）**、c11 回合 2+ 也走儀式。
  結果檔 Saved/robo_ceremony_result.txt。
  **c10 的下限是血價**：初版只驗上限，結果瓶子從頭到尾沒動也 PASS（真因＝握骨名依賴
  作畫的一次性校準、儀式期恆 NAME_None）——**「不該跳」的契約必須配「該動」的下限**。
- `robo_ceremony_probe.py`＝場地探針（施工前）：`DebugRoomCenterProbe`（房間中心／最大內接
  淨空圓／**部件包圍盒傾印**）＋`DebugCeremonyProbe`（指定圈心的環形淨空與席位路徑）。
  **量測順位鐵則**：膠囊 overlap 對 complex-as-simple 查不到（會把牆判成淨空）→ 純射線
  會被結構誤導（長廳兩端門洞）→ **部件包圍盒才是不會騙人的來源**。探針一律排除活體
  （力士 Body 擋 Visibility＝射線打到人頭）。
- `robo_ceremony_shots.py`＝六拍 × 固定機位截圖矩陣（11 張，cerem_*.png）。機位走
  **新鉤子 `DebugRoboViewAt(camX,camY,camZ, lookX,lookY,lookZ)`**——`DebugRoboViewFrom`
  恆盯 actor 自己，拍不到「場中央的酒瓶」這類主體；python 在 PIE 世界沒有 spawn actor API。
- `robo_collapse_jiggle.py`＝醉倒期軟肉彈簧診斷：逐 tick 取 jBelly＋jiggle 開/關 A/B 截圖。
  定罪過「崩塌把世界空間彈簧激到撞死 JiggleMaxCm 鉗位」（max 8.00＝鉗位、mean 5.03）。
- `robo_sleeppose_ab.py`＝**沉睡外觀 A/B 傾印對賬**（`CEREMONY = True/False` 改一行跑兩次
  再 diff）：actor 變換＋Body/BowBody 相對變換＋25 根骨的 CS 位置與旋轉＋兩張截圖。
  **「我沒改到那個表現」要用這支證明，不是靠讀 diff 宣稱**（08-18 血價：讀 diff 說沒改，
  實測才發現落地殘留 0.9cm 彈簧餘振）。
- python 坑（本批新增）：列舉屬性回的是物件不是 int（用 `.value`）；bool 屬性去掉開頭的 b
  （`bLeanLocked`→`lean_locked`、`bAsleep`→`asleep`、`bCeremonyEnabled`→`ceremony_enabled`）；
  元件沒有 `get_relative_rotation()`（走 `get_editor_property("relative_rotation")`）；
  逐 tick 輪流取樣多角色時**相鄰樣本永遠不同人**，差分類契約要先依 pid 分組。
