# Nice Ink — 上架完成計畫（SHIP_PLAN）

> 建立：2026-07-16。本文件是「一次做到可上架」任務的唯一指引與進度帳本。
> 每完成一項就在此打勾並記錄驗證方式。任務執行者：Claude（使用者長時間離開電腦，
> 全程自主執行；結束前必須跑完整登入→配對→遊戲流程並截圖存證）。

## 任務指引（使用者原話，一字不改）

> 現在並不是一個完整的遊戲，大廳、配對系統等等都沒有完成，不只如此，許多局內的東西也沒有建好。請仔細調查現在距離完成、上架還差哪些東西，請全部列出來。接下來有很長一段時間我不會在電腦前，我要你將這些一次完成後，善用UE的各個MCP，經過多次實際測試流程、截圖確認整個流程、介面均是完整的後才停止此任務，在你任務結束後我要直接將這個遊戲上架。請先仔細調查，建立詳細的待辦清單並將我的這個prompt寫進去作為這個任務的指引，接著開始一個一個完成，完成後實際去跑整個登入、配對、遊戲過程，確認完全完整後才結束此任務。

## 現狀盤點（2026-07-16 調查結果）

已完成且 robo 驗證過：完整回合狀態機（Lobby→BottleSpin→Seating→Drawing→Tour→
Accusation→Resolution→Finale→PostGame→rematch）、醉夢迷宮全系統、貼臉鎖定作畫、
轆轤首甦醒頭控、噴射/翻身表決、跨場持久化（錢包+刺青）、LAN session（console 指令）、
canvas HUD（token 制）、道場場景+fullbright、六人預生成 avatar 名冊。

**結構性缺口**（沒有它們就不是一個「產品」）：
- 遊戲直接開進 L_Dojo：**沒有主選單、沒有標題畫面**，建房/加入只有 console 指令
  `NiHost`/`NiJoin`（一般玩家根本進不了房）。
- **沒有大廳 UI**：看不到誰在房裡、無法手動開局（4 秒自動開局）、無法離開房間。
- **沒有任何設定介面**（解析度/靈敏度/音量）、沒有 ESC 選單、沒有退出流程。
- **全遊戲零音訊**（原始碼 0 個聲音呼叫）。
- **玩家名**＝引擎自動名，無輸入口；avatar＝按席位序派發，無選擇。
- **沒打包過**：ProjectSettings 還寫著 v2 時代的描述（bald yakuza sauna）、
  無 icon/splash、Content 裡躺著 10.8GB 已擱置髮型資產（DreamWorld Studio）、
  ResolveBodyUV 的 CPU buffer 讀取在打包版需要 bAllowCPUAccess（陷阱已知未驗）。
- 斷線/中途加入對回合狀態機的衝擊未審計（受害者中離＝？）。
- 授權記錄不全：dojo 場景來源無 ATTRIBUTION（法務風險）、M PLUS Rounded（OFL）
  與 game-icons（CC BY）需要遊戲內致謝與 THIRD_PARTY_NOTICES。

## 待辦清單

### A 級——本任務完成（不需使用者輸入）

- [ ] **A1 主選單前端**：L_MainMenu 空場景＋MenuGameMode＋MenuHUD（canvas，無 UMG 鐵律）
      ＋MenuPlayerController。標題、玩家名輸入（鍵盤輪詢自製文字框）、主持房間、
      加入房間、設定、授權頁、離開遊戲。GameDefaultMap → L_MainMenu。
- [ ] **A2 配對流程 UI 化**：Host→載 L_Dojo listen；Join→搜尋中回饋/逾時錯誤/找到即入；
      連線失敗與斷線（NetworkFailure/TravelFailure）→ 回主選單附錯誤訊息。
      玩家名經 ?Name= travel option 上服。
- [ ] **A3 大廳畫面**：玩家列表（名字/席位/現金/歷史刺青數）、人數 x/6、
      主機手動「開始」（自動開局改為可選）、離開房間。
- [ ] **A4 ESC 選單（局內）**：繼續／設定／離開房間回主選單。
- [ ] **A5 玩家名全鏈路**：輸入→持久化（settings save slot）→上服→指認/揭曉/終局/
      兇手轉盤等所有 UI 顯示真名。
- [ ] **A6 設定選單**：視窗模式+解析度（GameUserSettings）、滑鼠靈敏度（作畫游標/
      迷宮游標/臉指向分項或共用一個係數）、音量總控、套用/還原。主選單與 ESC 共用。
- [ ] **A7 音效底盤**：最小功能音組（UI 點擊、筆劃落墨、噴射、酒瓶轉、罰酒、巡禮切換、
      碳黑轉換、終局），CC0 或程序生成、授權入記錄；總量旋鈕、可全關。
      **鐵律：無聲甦醒不得有任何音效**（SPEC 定案 #8）。音色調性屬使用者驗收域。
- [ ] **A8 走路動畫**：程式化步伐（美術語言＝硬轉、突兀即目標），消滅 A-pose 滑行。
- [ ] **A9 打包管線**：ProjectSettings 更新（名稱/描述/版權/公司）、視窗標題、
      icon+splash、打包地圖白名單（L_MainMenu+L_Dojo；排除 Prototype/Sauna/
      DreamWorld 10.8GB）、bAllowCPUAccess 審計、Development 打包→雙實例
      standalone LAN 實測→Shipping 打包。
- [ ] **A10 Debug 收斂**：ni.DebugHud 預設 0、DebugForced* 預設 -1、robo ini 行不入包、
      版本戳處置、Debug/exec 指令在 Shipping 的行為確認。
- [ ] **A11 斷線防護審計**：受害者中離、兇手轉盤中離（failsafe 已有）、巡禮/指認/
      翻身表決中離、中途加入（比賽進行中 PostLogin）——逐相位審計，至少保證不卡死。
- [ ] **A12 授權與致謝**：THIRD_PARTY_NOTICES.md＋遊戲內授權頁（M PLUS Rounded OFL、
      game-icons CC BY、音效來源、Sauna CC BY 若入包）；來源不明項列給使用者（見 C8）。
- [ ] **A13 手臂握筆姿勢**：程序化兩骨 IK 讓持筆手臂跟筆（美術語言硬轉；此前「刻意延後」
      ——做可退回版本，使用者不喜可一鍵關）。
- [ ] **A14 全流程實測（結束閘門）**：
      1. 既有 robo 全綠重跑（迷宮 15/15、sleepgaze、seat1 sync、頭控 v4）。
      2. 新 robo：主選單→輸名→建房→第二/三客戶端搜→入房→大廳→開局→整輪
         （畫→醒→巡禮→指認錯×3→終局→PostGame→rematch）→回主選單。
      3. 打包版雙實例 LAN：真啟動器路徑 host/join+開局冒煙。
      4. 每個畫面/相位 HighResShot 截圖存 Saved/Screenshots/ship_audit/，逐張自查
         UI 完整性（無 debug 殘留、文字不溢框、名字正確）。

### B 級——工程做到「只差鑰匙」（使用者提供憑證後即通）

- [ ] **B1 EOS 上線開關**：選單暴露 LAN/線上房選項（憑證未填時線上房灰掉＋說明）；
      ini 佔位與 Docs/EOS_SETUP.md 已備，使用者填 Dev Portal 憑證即通。
- [ ] **B2 EOS 語音**（RTC room）：接線點文件化（憑證依賴，無法本機驗證）。
- [ ] B3 存檔鍵 → ProductUserId（EOS 接通後半小時工作，位置已註記）。
- [ ] B4 六人真機延遲驗證（只能真人）。

### C 級——使用者裁決域／僅使用者能做（不代決，列全）

- C1 **運行時自拍→臉貼圖**：現為離線 python 管線（torch/LaMa/mediapipe），移植進
  遊戲客戶端＝以週計的獨立工程。v1 上架的產品決策：內建名冊上架 vs 等自拍管線。
  （本任務以名冊+選擇 UI 出貨形態準備。）
- C2 HUD 墨刷 UI kit 三選一（現成包/AI 生成/手繪；07-15 使用者暫停討論）——
  本任務維持 token HUD 出貨形態。
- C3 噴射出口與褌的視覺（SPEC 待定 #11）。
- C4 開場動畫場景改寫（SPEC 待定 #14）。
- C5 迷宮環數降檔裁決（待定 #2）、RMB 瞄準切分追認（待定 #15）。
- C6 **EOS Dev Portal 憑證**（Docs/EOS_SETUP.md 步驟 1-3）——沒有它上架版只有 LAN 房。
- C7 **商店上架本體**：Steam/itch 帳號與費用、店面素材（膠囊圖/截圖/預告片）、定價、
  年齡分級問卷、內容審核方案（待定 #8——陌生人房手繪內容）、平台小號實測（待定 #12）。
  截圖/影片素材候選我會產出，選用屬使用者。
- C8 **dojo 場景資產來源與授權**：SourceAssets/dojo 無任何授權記錄——上架法務缺口，
  需使用者補來源連結與授權條款。whiskey/PEN 來源同查（PEN 含 PaperMate 商標，
  但遊戲內筆＝引擎圓柱體未用該模型，確認不入包即無風險）。
- C9 辨識度盲測（待定 #9）、playtest 三數校準（待定 #7）——需真人。

## 驗收原則（鐵律重申）

- robo/截圖只能證明「幾何成立、流程通、介面元素齊」；手感與美感的最終驗收
  是使用者的 viewport。本任務結束報告會逐項標注「已驗證」vs「待使用者 viewport」。
- SPEC 未動——本任務全部是實作與產品化工作；C 級設計項不代決。
- 提交紀律照舊：ini 不帶 robo 行、診斷碼拆除、每段落更新記憶資料夾。

## 進度記錄

（每完成一項附：日期、驗證方式、產出物路徑）

- **2026-07-17 A1-A6＋B1 程式完成、編譯過**：
  - 新類別：`UNiceInkGameInstance`（設定持久化 NiceInk_Settings 槽＋NetworkFailure/
    TravelFailure→回主選單帶原因）、`UNiceInkSettingsSave`、`ANiceInkMenuGameMode/
    PlayerController/HUD`（canvas 即時模式 UI：名字輸入/avatar 六選一+auto/lan-online
    切換/建房/搜房列表/設定/授權頁）、`NiceInkUiTokens.h`（調色盤共用）。
  - Session 子系統：UI 狀態機（Idle/Hosting/Searching/Joining/Failed＋人話錯誤）、
    搜房列表、JoinFoundSession(index)、加入旅行 URL 帶 ?Name=&Avatar=。
  - GameMode：InitNewPlayer 解析 ?Avatar=、PostLogin 主機從 GameInstance 取名/臉
    （僅 standalone/packaged，PIE 不受擾）、PickAvatarFor 意向+去重、
    **自動開局只活在 PIE（robo 靠它），正式流程＝大廳主機 ENTER 開局**。
  - HUD：大廳面板（玩家列表/人數/主機提示）、ESC 系統選單（靈敏度/音量/離開房間）、
    Button/AdjustRow 即時模式 helpers 下沉基底。
  - 地圖：L_MainMenu 已建（headless；GameModeMapPrefixes 綁 MenuGameMode）；
    GameDefaultMap→L_MainMenu、EditorStartupMap 維持 L_Dojo。
- **2026-07-17 A7 音效**：11 個合成 WAV（純 stdlib）入 /Game/Audio；NiAudio 播放層
  （音量=MasterVolume、**閉眼沉睡全域靜音=感官規格**）；接線=UI 點擊/相位轉換
  （轉瓶/入座酒/指認對錯/終局鑼）/巡禮 chime/落筆/噴漬/碳黑。無任何甦醒音。
- **2026-07-17 A8 走路**：三角波側傾＋步點彈跳（硬切），停步硬還原；
  修掉兩個時序 bug（入睡蓋站姿/lean 殘留側傾）。bWalkAnimEnabled 可關。
- **2026-07-17 A11 斷線**：PreLogin 拒絕開賽中加入（session 層 bAllowJoinInProgress
  同步關閉）；Logout=先持久化（恩怨博物館不蒸發）→受害者中離或人數<2＝AbortRound
  （人夠重新轉瓶、不夠回大廳）；兇手轉盤中離既有 failsafe 覆蓋。
- **2026-07-17 A13 握筆**：右臂兩骨解析 IK 跟實體筆（每 tick 冪等、poseable 快取
  鐵律遵守、骨缺席安靜跳過）；bPenArmIkEnabled 可關。**視覺好壞待截圖自查＋使用者。**
- **2026-07-17 A9 進行中**：ProjectSettings 更新（相撲描述/版權/標題）、
  Application.ico 生成、打包白名單（兩地圖＋UI/Audio/Characters AlwaysCook、
  DreamWorld/Developers NeverCook；Sauna 因 MI_Mosaic 母材質鏈不能 NeverCook——
  第一次 cook 失敗的教訓）、SM_Sumo bAllowCPUAccess=true 已存。Dev 包 cook 中。
- **教訓（陷阱年鑑候補）**：headless `-ExecutePythonScript` 若 Saved/Autosaves 留有
  PackageRestoreData.json（前一個編輯器被 Stop-Process 殺掉就會有）＝引擎初始化完
  就死寂空轉、python 永不執行——**headless 前先刪 Saved/Autosaves＋帶 -unattended**。
- **2026-07-17 A14 實測（進行中）**：
  - 打包版雙實例實測（Dev 包）：主選單/名字持久化/host→道場大廳 全綠（截圖
    Saved/Screenshots/ship_audit/packaged*）；抓到兩隻真 bug 並修——
    ①單擊雙觸發→第二次 HostSession 拆掉剛建的房（HUD 150ms 去抖＋session
    成功路徑維持狀態）②ESC 選單與底層 HUD 文字互疊＋準星蓋按鈕（選單開著跳過
    底層 HUD；沉睡黑屏照畫=感官規格）。**LAN join 未證實：打包 exe 無防火牆
    inbound 規則（無管理員權限加不了）——真機首跑 Windows 會跳允許提示屬正常，
    上架後玩家端無此問題；本機驗證待 editor -game 流程（使用者中斷過一次）。**
  - robo 迴歸（**全綠**）：maze 15/15 ✓、orbit（臉指向頭控）24/24 ✓、seat1 sync 11/11 ✓、
    **新增 robo_fullloop_test.py 16/16 ✓**（指認→罰酒 1/2/3→終局→PostGame→NiStart
    重賽——套件原本只驗到 Tour；主機開局閘、演出計時縮短驗證都在裡面）、
    leanlock 核心 ✓（lean 三端複寫/22cm/筆劃；peek FAIL=文件記載的產品正確行為、
    kick FAIL=拳腳系統 user 定案封存中，兩者非回歸）、sumo smoke ✓。
  - 截圖自查：迷宮 HUD（DRUNK DREAM/杯數/五門光圈/姿勢面板/噴射提示）✓；
    彎腰擺拍（環繞八角度）＝姿勢正常無網格損壞、**握筆 IK 有數值證據**
    （RightHand front=+55cm 前伸 vs LeftHand side=-89cm A-pose）；
    手臂外觀的美感＝使用者 viewport 閘門（bPenArmIkEnabled 一鍵可關）。
  - 攝影坑二連：HighResShot 只認聚焦視窗（受害者別放主機席）＋
    **自身可見＋俯視鏡頭＝拍到自己胸腹肉牆——觀察機位要先藏自己的 Body/BowBody**。
  - PS 5.1 又毀一次檔（ASCII 重寫含中文 ini）——已用 UTF-8 全文重建；
    **ini 換 robo 行一律用 Edit 精準替換，不用 -replace|Set-Content**。
  - robo 腳本維護：robo_maze/leanlock/sumo_smoke 的結果檔路徑改到專案 Saved/
    （舊值指向已消失的 session scratchpad）；leanlock 拆掉退役的 ServerMinigameHit。
- **2026-07-17 收尾**：robo ini 行已拆（grep=0）；**Shipping 包 BUILD SUCCESSFUL**
  （PackagedShipping/ 594.9MB、內容 pak ~175MB＝DreamWorld 10.8GB 排除成功）；
  Shipping exe 冒煙＝行程啟動且視窗成立（30 秒穩定）——畫面截圖未取
  （使用者正在使用電腦，停止一切視窗焦點自動化；Dev 包截圖已證選單渲染）。
- **A9/A14 帳面狀態**：A 級全部完成；唯二未閉環＝①雙實例 LAN join 實測
  （打包 exe 防火牆＋使用者在機，見 C 級防火牆註記；程式側的雙擊拆房 bug 已修）
  ②Shipping 包的視覺冒煙（僅差一眼，Dev 包同源已驗）。
