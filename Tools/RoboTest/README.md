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

- `robo_stencilcursor_probe.py`＝稿筆（Stencil）操作契約常駐探針（14 檢查）：
  c1 入鎖掛游標、c2 兩鎖點同滑鼠步數位移比（增益恆定）、c3 停手零漂移、
  c4 gaze 收斂、c5 快掃落墨、c6 relatch 角度命令逐字、**c7 單擊守恆**
  （按住 1.5s 零滑鼠→恰落 1~4 針：下限=點得出點、上限=靜止不灌墨——08-01
  拆移動閘後的守恆鎖）、**c8 慢速域**（0.05 單位/tick＝舊閘 3°/s 門檻下→
  全程有墨 gain≥5；舊碼此條必 0=回歸鎖）。結果檔 Saved/robo_stencilcursor_result.txt。
- `robo_fovaxis_probe.py`＝視野半角量測儀（08-01 邊緣推擠戰役）：deproject
  畫面中心/四緣實測可視半角 vs 程式假設——實錘引擎維持**垂直 FOV**
  （halfV 恆定、halfH 隨視窗長寬比走；「FOV36=水平半角 18°」只在 16:9 成立）。
  任何「邊界/邊緣帶/螢幕比例」類手感 bug 先跑這支拿真實半角，不要用 FOV 假設心算。
  結果檔 Saved/robo_fovaxis_result.txt。
- 邊緣推擠終案（08-01 六修）：讓位錨=**游標標記**（GetStencilCursorHudWorld
  投影）＋四邊統一像素帶——貼邊主詞必須同時「可見」且「恆隨手」（生 aim/
  濾波 aim/筆網格尖三代錨的死因各異，全史=DIRECT_DRAW_PLAN.md 08-01 各節）。
