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

- [x] **B1 EOS 上線開關** ✅ 2026-08-05：憑證已填、DefaultPlatformService=EOS 啟用、
      登入閂（persistentauth→portal fallback）、單機實測建房成功（見進度記錄）。
- [x] **B2 EOS 語音**（RTC room）✅ 2026-08-05：lobby 建立即開語音房、自動入房實測
      通過（NiVoice 探針 `loggedIn=1 channels=1` log 為證）。
- [x] **B3 存檔鍵 → ProductUserId＋PlayerDataStorage 雲端隨身** ✅ 2026-08-06
      BUILT-自驗（user 定案「依討論結論對齊實作、終態=Steam」）：
      ①存檔鍵＝PUID（`NiceInk_P_<puid>`；取 NetId「EAS|PUID」後半＝**Connect 層、
      不碰 EpicAccountId**——B5 切 Steam 票證登入時同一條鍵路徑零改動；無 PUID
      （LAN/PIE/robo）fallback 舊名字+席位鍵＝既有測試零擾）。
      ②`UNiceInkPersonaSubsystem`（GameInstance 子系統）＝雲端隨身層：登入成功
      （SessionSubsystem 登入閂兩路都掛鉤）拉 `persona_assets_v1.sav`＋
      `persona_settings_v1.sav`（引擎 OSS EOS 的 IOnlineUserCloud＝PlayerDataStorage）。
      ③資產流：進房上行＝owner client 分塊 RPC（16KB＋CRC）交 host 驗證套用
      （錢包鉗位/Marker 拒收/1024 幅瘋值上限；PS.bAssetsRestored 防雙重還原、
      12s 逾時 fallback 主機本機槽）；結算下行＝每個 Persist 點（雷射/終局/搖晃
      扣款＋**新增碳黑誕生點即刻落盤**）host 回傳本人→本人寫自己雲端保險箱
      （PlayerDataStorage 私人不可代寫）；主機本機槽降級為熱備。
      ④偏好雲端同步＝Revision 比帳（高者贏、平手本機贏；-culture= robo 覆寫恆優先）。
      記帳：listen server 無絕對防竄改（門檻＝EOS API 級，派對遊戲接受）；
      **Epic 帳號 PUID ≠ Steam 票證 PUID＝切 B5 時開發期資產不搬家（預期非 bug）**；
      房內他人臉/資產分發不歸此層（自拍上傳=SPEC #52 另案）。
      驗證：編譯過＋robo fullloop/trace 迴歸（LAN/PIE 原路）；**雲端 E2E 併 B4
      雙機驗收**（PIE 不支援 EOS，單機 -game 只能驗到寫入 log）。
- [x] **B7 個人檔案頁＋自拍臉 runtime 接入（開發機版）** ✅ 2026-08-06 BUILT-自驗
      （user 指令「名字/照片要有地方設定、現金/紋身要有地方看」＋SPEC v4.0e
      「辨識＝名字＋臉icon」）：①主選單第六頁 Profile＝名字欄回歸（CommitName
      沿用）＋現金（Persona 雲端資產視圖；未登入顯「尚未登入」）＋上傳自拍鈕
      ＋眉毛鐵律提示（BrowHint 8 鍵×13 語入 NiLoc）；②自拍→臉＝
      Tools/FacePipeline/intake_selfie.py（v7 管線＋sumo 眼罩單人烘焙）→
      PersonaSubsystem 背景行程（venv python；FTSTicker 輪詢）→runtime 匯入
      三貼圖＋膚色→Saved/PlayerFace/ 正本＋開機自載→舞台力士當場換臉
      （DressDancerFromPersona＋ApplyCustomAvatar）；③選單靜默登入
      TrySilentLogin（persistentauth-only 零彈窗）＝進房前雲端 persona 到位、
      **實測 EOS 雲端寫入 OK（cloud write OK log 為證＝B3 真線首驗）**；
      ④局內名字回歸＝大廳/揭曉/指認/頂欄臉像+名字並列；⑤robo 鉤子
      NiMenuShowProfile/NiMenuSelfie/NiMenuShot（Shot showui）。
      **08-07 續批（user 五連裁決）**：①名字開放 Unicode（Sanitize 改黑名單制
      ：擋控制/空白/URL 語法字/檔名保留字；中日韓可入名）；②檔案對話框換
      IFileOpenDialog COM（引擎 DesktopPlatform=GetOpenFileNameW 古典模板＝
      高 DPI 點陣糊——自接現代對話框、Shipping 可用、DesktopPlatform 依賴移除）；
      ③處理中狀態列附已耗秒數（實測 61~107s/張）；④**臉庫**＝上傳全保存
      Saved/PlayerFace/library/<時間戳>/（含 256² 縮圖=HUD 裁切框膚色打底）、
      active.txt 指針、個人檔案頁縮圖列點選即換（酒金底線=穿著中）、舊平鋪檔
      自動遷移 library/legacy；⑤已有臉→按鈕變「重新上傳自拍」（ReuploadSelfie/
      SavedFaces ×13 語）。E2E 綠（遷移+新臉入庫+切換+文案截圖自查）。
      **已知缺口（誠實記帳）**：(a) ~~自訂臉只有本人看得到——雲端儲存+房內分發
      （MB 級 bytes 列車）未做＝房內他人仍見名冊臉~~（**08-10 房內分發 SHIPPED
      ＝FaceShare 登記簿+臉列車，見 2026-08-10 進度記錄；殘=雲端儲存**）；(b) ~~管線=
      本機 venv 依賴＝正式出貨前必須裁決 SPEC #52 三選一~~（**08-07 已裁①遊戲
      內建且 C1 M1~M5 全站完成**——管線=C++/ONNX 內建、venv 退役為 -facevenv
      對照組，見 C1）；(c) legacy 遷移臉無縮圖=素塊（重上傳即有）；(d) 手感/
      版面以 user viewport 為準。
- [ ] B4 六人真機延遲驗證（只能真人）；**雙機 EOS 驗收**（join 流程＋雙向語音，
      需兩台機兩個 Epic 帳號——同帳號雙開撞 PUID、PIE 不支援 EOS P2P）。
- [ ] B5 **Steam 票證登入切換**（正式終態＝玩家零帳號零彈窗，Meccha 同款）：
      待使用者辦 Steamworks（$100＋文件審核）拿 App ID→EOS Portal 身份提供程序
      設 Steam→接線（引擎原生 ConnectLoginNoEAS 路徑）。現行 Epic 帳號登入
      保留＝開發環境＋非 Steam 版備援。
- [ ] B6 EAS 品牌驗證（去掉登入頁「未經驗證」警示）：需自有網域 DNS 驗證＋
      隱私政策頁＋128px logo→提交審核。上架前做。

### C 級——使用者裁決域／僅使用者能做（不代決，列全）

- C1 **運行時自拍→臉貼圖＝遊戲內建 C++/ONNX（✅ 2026-08-07 使用者裁決
  「當然是1遊戲內建」——SPEC #52 三選一收束）**。移植路線圖（以週計；每站
  以「與 python 管線輸出對賬」為驗收閘）：
  - [x] **M0 地基勘察** ✅ 08-07：UE 5.7 NNE 推理插件確認（**NNERuntimeORT**
        ＝正式 ONNX Runtime，CPU+DirectML）；模型盤點＝LaMa **已是 ONNX**
        （208MB fp32，免移植；可 fp16 量化砍半）、BiSeNet pth→**已轉
        bisenet_512.onnx（53MB 單檔自包含，pth vs onnx label 對賬 100.000%**，
        儀器=Tools/FacePipeline/export_bisenet_onnx.py）、MediaPipe
        face_landmarker.task=風險點（見 M2）。
  - [x] **M1 NNE 接線** ✅ 08-07：NNERuntimeORT＋OpenCV 插件啟用；模型改
        **raw .onnx 入 Content/FaceBakery/models（NonUFS staging，非 uasset）**
        ——UNNEModelData::Init 在 runtime 從 bytes 直建、GetModelData 現場叫
        ORT（不用編輯器匯入/AlwaysCook）；推理殼=Face/NiceInkFaceOnnx
        （坑：SetInputTensorShapes 後輸出形狀可能不解析→符號形狀 fallback）。
        **NNE ORT 非編輯器目標預設 IntraOp=1 單線程＝打包版陷阱**——
        DefaultEngine.ini 已設 GameThreadingOptions IntraOp=0。
  - [x] **M2 臉部特徵點** ✅ 08-07（比路線甲更優的形態）：**task 解包後兩顆
        tflite 都是純標準 op**（新版 face_landmarks_detector=256²/478 點無
        attention 自訂 op）→ tf2onnx 直轉＝**同權重同索引零重標**
        （models/face_detector.onnx 128²/896 anchor＋face_landmarks.onnx）。
        C++ 全鏈=Face/NiceInkFaceLandmarks（BlazeFace 解碼/加權 NMS/眼點旋轉
        ROI/裁切/映回）；對賬儀器=Tools/FacePipeline/mp_onnx_landmarks.py：
        vs mediapipe 官方 9/10 張 mean<2px、唯一離群=眼部全遮臉（雙方同等
        合法猜測、overlay 目檢確認）。
  - [x] **M3 古典影像處理 C++ 化** ✅ 08-07：引擎自帶 OpenCV 插件
        opencv_world455＝**完整 contrib 版（shape/photo/calib3d 全有）**，
        TPS/TELEA/fitEllipse/estimateAffine2D 全部原生呼叫；Poisson 膜/羽化/
        自然填充=numpy 直譯。坑：**UE 的 imgcodecs 沒帶 jpeg**→影像 IO 全走
        UE ImageWrapper（順帶 unicode 路徑安全）；**UE 的 OpenCV 無平行化**
        →BlurF 自寫 ParallelFor 可分離高斯（同 kernel 同邊界）＋TPS map
        建一次 remap ×5（輸出 drift=0.1/255=浮點噪聲）。
  - [x] **M4 管線編排 C++ 化** ✅ 08-07：Face/NiceInkFaceBakery{Core,Warp}
        =selfie_to_face_texture v7 十二步全移植（現行組態：鬍留貼圖/擴張島/
        眼閉變體；死路不移植=鬍區 LaMa 重生、DIRECT_MODE、legacy 單 mask、
        v5 flatten、seam QA 儀器）。**金樣本對賬（8 張）**：face_open/closed
        rgb mean 2.0~2.9（p95≤9、多人照 5.4）、alpha 全等、eye_mask_ink
        ≥99.997%、skin_color Δ≤3、emma **同文案拒收**（Head mask too small）；
        儀器=gen_gold_refs.py＋compare_parity.py＋NiFaceBake console 指令
        （dump 模式含中間產物）。已知等價差異（記帳）：顆粒 RNG 不同源
        （統計等價）、BiSeNet 前處理 PIL vs INTER_AREA（label 一致 99.7~99.9%）、
        JPEG 解碼器、**EXIF 方向不套用**（cv2.imread 會套——手機直幅照差異點，
        待補 EXIF 旋轉）。
  - [x] **M5 併入 Persona** ✅ 08-07：BeginSelfieIntake 內建管線優先
        （`-facevenv` 強制舊 venv 路=對照組）；眼罩 sumo 烘焙＋縮圖＋
        skin_color.json 同批 C++ 化（工件合約與 intake_selfie.py 一字不差、
        intake_log.txt 照寫）；模型常駐快取（首次 ~65s 載入、之後零冷啟）。
        E2E 實測（NiMenuSelfie 真 Persona 路）：DONE native＋ActivateFace＋
        FaceRevision 遞增。**耗時：暖 88s/冷 156s**（python venv 61~107s——
        大魚已收割：TPS 107→22s、flat-field 56→26s；剩餘大頭=LaMa 推理
        ~15s/趟×2＋冷啟 session 65s）。
  - [x] **打包驗證** ✅ 08-07：Development BuildCookRun BUILD SUCCESSFUL；
        FaceBakery models/data 全數 NonUFS 進包＋opencv_world455.dll＋
        NNERuntimeORT Onnxruntime binaries 就位；**包內 exe 跑 NiFaceBake
        =DONE 34 秒**（Development 最佳化＋IntraOp=0 ini 生效——比編輯器
        二進位快 4 倍、比 venv python 快 2~3 倍），輸出 vs python ref
        =rgb mean 2.04（與編輯器版一字不差）。
  - 遺留（後續）：(a) fp16 量化（260MB→~130MB 入包＋冷啟砍半）；
    (b) EXIF 方向（cv2.imread 會套、ImageWrapper 不套——手機直幅照）；
    (c) **模型不在 git**（同 venv 時代）——新機器要從
    Tools/FacePipeline/models 複製到 Content/FaceBakery/models；
    (d) 個人檔案頁實拍手感/畫質＝以使用者 viewport 為準。
- C2 HUD 墨刷 UI kit 三選一（現成包/AI 生成/手繪；07-15 使用者暫停討論）——
  本任務維持 token HUD 出貨形態。
- C3 噴射出口與褌的視覺（SPEC 待定 #11）。
- C4 開場動畫場景改寫（SPEC 待定 #14）。
- C5 迷宮環數降檔裁決（待定 #2）、RMB 瞄準切分追認（待定 #15）。
- ~~C6 EOS Dev Portal 憑證~~ ✅ 2026-08-05 使用者完成（org NerdSoftStudio／product
  NiceInk／client＋policy＋EAS 應用程式三許可；配方全文=Docs/EOS_SETUP.md）。
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
- **2026-08-05 BGM（單一恆定循環，使用者定向「一首走全場」）**：Suno 生成
  「The Sneaky Koto」→rubberband 0.75× 不變音高→-16 LUFS→尾頭 2s 等功率交叉
  淡接＝無縫 loop 100.6s（源檔+再生指令=SourceAssets/Music/SunoBgm/README）；
  資產=/Game/Audio/bgm_sneaky_koto（looping=True）；播放層=GameInstance
  EnsureBgmPlaying（ANiceInkHUD::BeginPlay 喚起=主選單/道場共用入口、
  bPersistAcrossLevelTransition 跨關卡不斷、音量=MasterVolume×BgmScale 0.30
  底噪級即時生效）；**恆定不掛任何遊戲狀態＝零洩漏構造保證**；沉睡者照播
  （BGM 無情報身分，不走 NiAudio 靜音——補位設計待使用者追認）；-game 實跑
  驗證 NiBgm log 綠。**授權待辦：Suno 商用權=付費方案綁定，出貨前確認（C8）。**
- **2026-08-05 稿筆真麥克筆聲（user 耳測選定 freesound CC0 351145）**：切段診斷
  →marker_loop（D 段穩定區+0.25s 交叉淡接=無縫摩擦床 1.35s）+marker_dab（B 段
  =落筆觸感音 0.353s）入 /Game/Audio；落筆聲按針型分流（稿筆=MarkerDab、機器針
  =StrokeStart 照舊、全端重放）；摩擦 loop=本人專屬 UpdateMarkerSfx（音量=
  √(筆尖速度/8cm/s)×0.7×MasterVolume、EMA τ0.06s——**筆沒動就沒聲（LMB 按住
  也一樣）、停頓歸零、收筆即停**；音高 0.94~1.06 隨速度；旋鈕 MarkerSfxRefSpeedCmS/
  MarkerSfxVolume）；候選庫+淘選記錄=SourceAssets/Sfx/MarkerPen_Audition/README。
  驗證=directdraw 迴歸 71/72（唯一 FAIL=cruise tipSpd 既知 flake 同簽名）；
  設計捕捉：沉睡者夢中描圖=有自己的刺青機聲（滅別人的情報音、自己的操作聲
  照有）——刺青機 buzz 程序合成=下一批。
  **二輪（user 打回「勻速畫卻一段段急促」+dab 不要）**：真兇=失去接觸閘讓一次
  拖曳反覆收/開筆——per-BeginStroke 的 dab 每次重開連響（包絡量測先洗清素材
  =A/D 段 span≤2.3dB 平坦）；三刀修=dab 全拆（enum/分流/資產退役，機器針
  StrokeStart 照舊）＋loop 閘改 LMB 按住 bPenTriggerLocal（收/開筆閃爍不再切
  聲音）＋marker_loop 設 PlayWhenSilent（靜音期引擎不偷停=無重啟爆音）；
  編譯綠；**音色/手感待使用者 viewport**。
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
- **2026-08-05 B1+B2+C6 EOS 上線全戰役（架構考證→改道→接線→單機實測全通）**：
  - **架構考證（user 指定照抄 Meccha Chameleon）**：安裝目錄+二進位字串掃描實錘
    其全棧＝UE5 listen server over EOS P2P（NAT 失敗走 Epic 免費中繼）＋EOS 大廳
    ＋EOS Voice RTC（Vivox 零引用）＋Redpoint 付費外掛＋Steam 認證。結論＝與本
    專案既有鷹架同形狀。
  - **Redpoint 免費版死路（血價教訓）**：免費版＝純預編譯＋只發最新引擎（5.8）
    ＋強制跟版＝引擎人質條款；本專案 5.7 物理不可用→全棄，留 5.7 走引擎原生
    OnlineSubsystemEOS＋EOSVoiceChat（架構等價＝同一批 Epic 服務，外掛只是接頭；
    Meccha 用的是付費版無此條款）。
  - **接線（編譯綠＋robo_feign 26/0 EOS 啟用下迴歸全綠）**：憑證五值入 ini＋
    `bUseLobbiesVoiceChatIfAvailable=!bLan`（lobby 即語音房）＋四個建房/搜房入口
    改跟隨 `IsOnlineServiceConfigured()`（NULL=LAN、EOS=網路，零手動切換）＋
    **登入閂**（persistentauth 靜默→失敗自接 accountportal 開瀏覽器——引擎
    fallback 只掛 AutoLogin 路徑的坑）＋**NiVoice 探針**（進網路圖每 3s log 語音
    狀態、入頻道自停、30s 未入大聲警告）。
  - **登入制鐵事實**：桌面原生 OSS 無 device-id 顯名支援（ADD_USER_LOGIN_INFO=0
    編譯期關閉）→Epic 帳號制是桌面唯一穩路；請求 scope 恆為 basic_profile+
    friends_list+presence+offline_access（AuthScopeFlags 縮不掉、實測無效）→
    **Portal 端 EAS 三許可全開對齊是唯一解**（三連錯全史=EOS_SETUP.md 配方）。
  - **單機實測全通（log 為證）**：Epic 登入→EOS lobby 建房 OK→RTC 語音房自動
    入房 OK（`NiVoice: loggedIn=1 channels=1`）；小記帳＝RTCAudio input device
    枚舉警告（ESC 語音設定時處理）。語音出聲走 EOS SDK 音訊裝置不經 UE 音訊
    ＝沉睡全域靜音天然不殺語音（SPEC 聽覺開放=構造保證）。
  - **安全**：ClientSecret 在 DefaultEngine.ini——**repo 必須維持 private**（已驗
    PRIVATE）。
- **2026-08-05 選單 v2 全改版＝房間碼前門＋素色簡約風（結構刀＋視覺刀，user 授權
  「兩刀一起做完再驗收」；BUILT-自驗、未提交待 viewport）**：
  - **起因（user 三連問擊穿）**：「朋友怎麼搜房？」＝配對前門從未被設計——舊選單
    是開發者控制台（network lan/online 切換、EOS 開發提示字、測試 avatar 選擇欄
    全部露在玩家臉上）＋公開房列表是唯一入口（主客顛倒）。
  - **結構刀＝房間碼制**：建房生成 4 字母房碼（字元集剔 I/L/O 混形；session 廣告
    屬性 NICODE/NIPUB）→大廳大字顯示（GameInstance→GameMode PostLogin→GameState
    複製全員）；加入頁第一格＝輸碼框（輸滿 4 字 ENTER 或按鈕直達）；建房可選
    invite only（預設）/public——私房不進列表只有碼能進；公開房列表退居陌生人房
    瀏覽器（no ping、host+人數）。LAN/EOS 同一條碼路（搜尋結果按 NICODE 比對）。
  - **拔開發內臟**：network 切換、EOS 提示行、face 佔位欄全拆（連線模式恆自動跟
    IsOnlineServiceConfigured、avatar 恆席位輪派）；licenses 降級小幽靈鈕。
  - **視覺刀＝素色半透明簡約風（user 定案：Meccha/Schedule I 式、不買素材包）**：
    runtime SDF 圓角紋理＋9-slice `DrawRoundedBox`（canvas 三角形零 AA 的繞道）；
    Button/PanelBox 全域重製（無邊框、圓角、紙色薄膜、accent＝實心酒金）；主選單
    卡片式重排＋房碼格＋分隔線。墨底疊墨背景＝隱形＝踩過的坑（格底改紙色薄膜）。
  - **robo 鉤子（常駐測試 API）**：`NiMenuAutoHost/NiMenuShowJoin/NiMenuJoinCode`
    （timer-deferred、-ExecCmds 開機直呼）＋房碼入 log（`NiSession: room code`）。
  - **自驗（截圖矩陣 Saved/Screenshots/menu_v2/）**：主選單/加入頁（空狀態＋填碼
    ＋錯碼紅字失敗訊息）/大廳房碼大字＝全綠；host 端全鏈（碼生成→session→
    GameState→大廳 HUD）跑真流程證實。**血價：截圖工具要 SetProcessDPIAware**
    （125% 縮放下 GetWindowRect 回邏輯座標＝底部被裁＋版位全偏——先誤判成排版
    bug）；**user 在機時禁一切焦點/輸入注入自動化**（topmost+NOACTIVATE 截圖法）。
  - **未閉環（記帳）**：①join-by-code 的「搜到→比對→加入」E2E——本機防火牆無
    UnrealEditor inbound 規則（非管理員 session 加不了）＝LAN beacon 廣播被擋、
    同機 EOS 雙實例＝同帳號進不了同 lobby → 併入 B4 雙機驗收一起測；②正式版
    LAN 玩家首次 host 會跳 Windows 防火牆詢問＝標準行為，寫進之後的玩家支援文件。
- **2026-08-06 選單 v3＝Slate 白卡制＋跳舞舞台（user 兩連打回 canvas 版後裁決
  換 Slate；BUILT-自驗、未提交待 viewport）**：
  - **裁決鏈**：canvas 深棕版被打回「字雜亂/畫質差/說好的素色半透明呢」→診斷=
    投影糊字+深棕疊深棕+「素色半透明」被我錯譯成既有墨色盤；改白卡後 user 問
    「canvas 是你能做到最好的方法嗎」→攤牌 canvas 無 UMG 是初始 commit 我自選、
    user 從未裁決→user 定案 **Slate C++ 直寫**（零資產原則不變、買到 SBackgroundBlur
    真毛玻璃）＋新需求「按鈕後面=自己的力士跟 BGM 跳舞（用現有移動彈跳系統）」。
  - **落地**：SNiMenu（NiceInkMenuWidget）四頁全 Slate：毛玻璃白卡（Paper 0.78+
    blur 14）+墨字+酒金主鈕+房碼四格+公開房卡；MenuHUD=widget 宿主+robo 轉接；
    NiceInkMenuStage=程式生成舞台（地板/相機/無影光×2/完整角色替身 RT 壓 512/
    AIController 直驅）；編舞=BGM 拍相位（GameInstance 起播時刻+BeatSec 0.62 旋鈕）
    每 2 拍換邊橫移±55cm、每 16 拍轉圈——摺り足+Jiggle 全現成零新動畫。
  - **鐵坑六連**：裸 FontFace 餵 FSlateFontInfo=豆腐字（要複合 UFont+GC 持有）；
    mesh 前向=actor -X；UE FOV=水平角；橫移角色必關 MotionBlur；雙平行光要
    ForwardShadingPriority；BGM 喚起在 ANiceInkHUD::BeginPlay（改繼承鏈要自己叫）。
    另：-ExecCmds 多命令=逗號分隔（`|` 整串作廢）。
  - **自驗**：截圖矩陣 s4/s5（root 毛玻璃透舞者/join 房碼格+公開房卡/隔秒兩幀
    舞姿不同=步態+彈跳活）＋NiBgm log；舞步對拍手感/BeatSec 校準=user viewport。
- **2026-08-06 本地化帳（調查完成、scope 待 user 裁決）**：Meccha Chameleon
  Steam 實查=13 語（英/日/西[西班牙]/簡中/韓/法/義/德/阿拉伯/葡[巴西]/俄/繁中/土）
  介面+音訊+字幕全標支援。對齊我們=六文字系統字體矩陣（Zen Old Mincho 拉丁+日文
  已上線；Noto Serif TC/SC/KR/Cyrillic+Noto Naskh Arabic 待掛——複合字體按字符
  範圍掛面=程式零改動、全 OFL 免費）＋**真正的大項=本地化系統**：選單字串現為
  硬編英文→字串表制（FText/StringTable）+13 份翻譯+語言設定項+阿拉伯 RTL 驗證
  （Slate ICU 整形原生支援、未實測）。**待 user：目標語言清單定案**（是否照抄
  Meccha 13 語）→定案後開工字體矩陣（半天）+字串表改制（工作項另計）。
- **2026-08-06 本地化第一階段 SHIPPED-自驗（user 定案照抄 Meccha 13 語）**：
  自管字串表 NiceInkLocText（43 鍵×13 語、零資產、譯文=Claude 初稿**上架前建議
  母語者過目**）＋GameInstance 語言設定（存檔持久化/OS 自動偵測/-culture= 覆寫
  跟隨）＋ApplyLanguage 同步 SetCurrentCulture（字體矩陣繁簡分流開關）＋
  SessionSubsystem 錯誤鍵化（英文留 log、鍵給選單翻譯）＋Settings 語言列
  （原生名顯示、切換即重建選單）＋大廳 HUD 四句同步＋robo 鉤子 NiMenuLang。
  自驗=繁中（源流明體）/日（Zen）/阿拉伯（Naskh+RTL 整形+標籤自動靠右）三語
  截圖全過。**剩（下一工作項）**：局內 HUD 全字串本地化（相位橫幅/巡禮/指認/
  終局等，字量大於選單一個量級）；阿拉伯全鏡像版面（現=文字 RTL、版面 LTR
  =業界最小可行）；13 語翻譯母語校對。
- **2026-08-06 語言入口迷路窘境修（user 點題「看不懂語言連設定都到不了」）**：
  業界調查（native names/不用旗幟/globe 或「文A」符號/一頁全列）→實裝：主選單
  底列「文A」鈕（Google 式語言符號、零文字依賴、零新資產）→語言頁=13 母語名
  兩欄網格一頁全列（當前語言金色高亮、點選即套用+選單重建）；Settings 舊箭頭
  循環列退役改開同頁；robo=NiMenuShowLang。語言持久化端到端驗證
  （NiLang 探針 log：saved 優先→OS 偵測 zh-TW→繁中✓、-culture= 跟隨）。
- **2026-08-06 身分系統臉制 SHIPPED-自驗（SPEC v4.0d 定案 #52；user 定案「一切
  用自拍臉像」）**：名字自玩家畫面全面退場——主選單名字欄刪除（舞台上跳舞的
  =身分顯示）、大廳名列=席位+臉像+現金、巡禮揭曉=真作者臉像 96px 放大登場、
  指認嫌疑人=大臉像、頂欄受害者=臉+狀態、翻身提案人名拔除（合作提案不承重）、
  公開房列表房主名→純人數；撞名問題整組蒸發。實作=DrawFaceTok（FaceIconCache
  +紙框圓角+**FaceUV 版面 UV 裁切 (0.30,0.22)+(0.40,0.40)**——整張畫=膚色方塊
  鐵坑、臉貼圖外圈透明要疊膚色底+Translucent）。底層隱形 ID 保留（log/引擎；
  Steam persona 承載封鎖檢舉=B5 隨附）。記帳：①runtime 自拍上傳升格身分系統
  本體=上架前必做（架構三選一分析待出：C++/ONNX / 雲端 / 外掛工具
  →**08-07 已裁①遊戲內建、C1 全站完成**）；②房主
  臉像上房間列表=待自拍上雲；③殘留名字點=迷宮亡靈 banner（封存系統）+debug HUD（開發用）。
- **2026-08-10 首啟身分四連 BUILT-自驗（user 五條定案逐字：①選單背景預設力士
  ＝作者臉②上傳後只能用自己的照片、作者臉永不成為可選項③一定要上傳照片才能
  開始遊戲④隱私如實以告⑤預設名/語言從平台拿減摩擦）**：
  - 作者臉=Content/AuthorFace/（08-10 最新管線烘的四工件、runtime 匯入、
    DefaultGame.ini NonUFS staging 已加）；PersonaSubsystem::GetAuthorFace 懶載
    （ImportFaceDirInto 共用核心抽出）；MenuStage 無自訂臉時穿作者臉、上傳完成
    即被自訂臉分支蓋掉——作者臉只住選單舞台、永不進臉庫=「非選項」構造保證。
  - 臉制閘門：SNiMenu Host/Join 按鈕無臉=導個人檔案頁+紅字 FaceGateHint
    （13 語）；Join 頁入口被鎖=頁內鍵盤入房路徑天然在閘後；robo 鉤子
    （NiMenuAutoHost/JoinCode 走 PC/subsystem 層）不受閘=測試 API 保留。
  - 隱私聲明 PrivacyHint（13 語、個人檔案頁上傳鈕下）：「照片只在你的電腦上
    處理——我們沒有伺服器，開發者永遠不會收到你的照片。只有同房間的玩家看得到
    你的臉。」——每句在架構上恆真（本機 C++/ONNX；房內分發=同房可見本來就要）。
  - 平台名優先：GetEffectiveDisplayName()=自訂名>平台暱稱(Epic 現行、B5 Steam
    同 OSS Identity 介面零改動)>session 保底 rikishiNN（**不再落檔**——鷹架名
    不進玩家存檔）；CommitName 只在「輸入≠有效名」才存自訂；?Name=/GameMode
    主機路/名字欄播種全改走有效名。**鐵坑：NULL/LAN 假登入的暱稱=電腦名-編號
    （實測 Willie_desktop-5）=洩漏主機名——IsOnlineServiceConfigured 閘住只認真
    平台**。語言=OS 偵測現狀即平台行為；Steam GetCurrentGameLanguage 拉取=B5。
  - 驗證：編譯零錯誤；-saveddirsuffix=ROBO 乾淨身分兩實例 NiMenuShot 截圖
    （主選單=作者臉舞者✓、個人檔案頁=rikishi10 保底名+隱私聲明✓）；閘門點擊
    路徑未上儀器（無輸入注入）=user viewport 一鍵可驗。
  - 記帳：①強制上傳的完整價值要等「自訂臉房內分發」（C1 待做——沒有它房內
    他人仍看名冊臉）；②烘焙 88s 在「等房期間背景跑」的 UX 前置未做；③名冊六
    測試臉（含 caseoh/ibai 真人）出貨前要處理=閘後只剩 dev 路但資產仍在包裡。
- **2026-08-10 自訂臉房內分發 SHIPPED-自驗（C1 待做項清掉；user 質問「為什麼
  不修」後當場補完——強制上傳制的最後一哩）**：
  - 架構=`UNiceInkFaceShare`（WorldSubsystem 登記簿：席位→臉貼圖三件組+膚色
    +版本；NIF1 blob 格式=magic+tone+三段 png 原始位元組）＋角色臉列車
    （ServerFaceHello/Begin/Chunk/End 上行、ClientFaceBegin/Chunk/End 下行；
    16KB 塊+CRC=沿用 B3 位元組列車）＋GameMode 集散地（seat→blob 庫、viewer
    報到名冊、節奏發送佇列 8×16KB/0.1s≈1.3MB/s=防 reliable 緩衝溢位；晚到者
    ServerFaceHello 報到即補發全房已知臉、跳過本人席位）。
  - 套用端：EnsureAvatarApplied 每 tick 輪詢登記簿（map find+int 比對）、
    版本變了就 ApplyCustomAvatar 蓋名冊臉（名冊重套=版本歸零強制重疊；
    SwapBodyMesh 換睡姿網格臉貼圖住元件成員=天然存活）；DrawFaceTok（大廳/
    巡禮/指認全部臉像 icon）改自訂臉優先、沒到貨前名冊臉墊著；本人臉=
    MaybeStartFaceShare 直讀 Persona 記憶體貼圖零延遲入簿（不等網路回聲）。
  - 局內本人臉同時修好：此前 ApplyCustomAvatar 只有選單舞台在用＝**局內連
    自己都是名冊臉**——現在四路全通。
  - 範圍閘：WorldType==Game 才啟動（PIE/robo 全部既有測試零干擾）；LAN 與
    EOS 同路（臉走房內列車不走雲端）。
  - 驗證（listen+direct-connect 兩實例、兩個不同臉身分 -saveddirsuffix 沙箱、
    NiShot 截圖+log 四向對賬）：host 端「seat 0 applied (server view)+seat 1
    applied (server view)」／client 端「seat 1 applied (client view)+seat 0
    applied (client view)」＝本人臉×兩端+對方臉×兩端全過；blob 1.27MB/1.60MB
    CRC 全過；大廳席位列臉像 icon 上屏。
  - 新工具：`NiShot`（GameInstance Exec 延遲截圖=任何世界/任何 PC 類可用、
    core ticker 跨 travel——NiMenuShot 只活在選單 PC 的補位）。
  - 過程鐵坑：①EOS ini 下裸 `/Game/Maps/L_Dojo?listen`+`127.0.0.1` 直連=
    network error 回選單（EOS net driver 要 session 層；直連測試必帶
    `-ini:Engine:[OnlineSubsystem]:DefaultPlatformService=NULL`——play_ingame.bat
    未帶=EOS ini 下已壞，待修）；②同機 LAN 搜房 0 命中（防火牆擋 beacon）
    =join-by-code 同機 E2E 不可用、驗證改走直連（B4 記帳原樣）。
  - 記帳殘項：臉 blob 未壓縮（png 原樣 ~1.3MB/人；6 人房 host 上行尖峰
    ~40MB=EOS 中繼下待實測）；換臉只在進房時上傳一次（房內不重發＝夠用，
    選單才能換臉）；play_ingame.bat 的 EOS ini 直連壞需補 NETARG。
- **2026-08-10 作者臉誤植事故＋修正（血價）**：我把 Saved/PlayerFace 臉庫最新
  上傳（08-07/08-10）當成「user 本人的臉」烘進 Content/AuthorFace——實為 user
  測試管線時餵的 **CaseOh 測試照**（對照表 Saved/face_identity_sheet.png 定罪；
  差點把真人實況主肖像當作者臉出貨）。**user 親自指認：本人臉=7AF4**
  （test_selfies/7AF4F8FF-*.jpg）。修正=venv intake_selfie 對該照重烘四工件
  →替換 Content/AuthorFace→乾淨身分截圖驗證舞台=本人✓。
  **鐵則：臉=身分資產，「誰的臉」永不假設、必經 user 指認**；臉庫≠本人
  （開發機臉庫全是測試照）。名冊六臉來源：0=7AF4（user 本人）/1=cvd/
  2=caseoh（實況主）/3=ibai（實況主）/4=img1/5=img0——出貨前名冊處理時
  2/3 為肖像權硬阻斷、0 為 user 自己授權自己。

## 2026-08-10 選單 UI 邏輯修（user 20 條驗收單→BUILT-自驗待 viewport）

起點＝user 對選單 UI 的邏輯層總批判（「UI 不是視覺的東西」）：控制項歸屬、
因果可預期、狀態有出口、記號一義。二十條問題單 user 裁決成立後全權委託修復。
本批只動邏輯與版面結構，白卡墨字視覺語言不動；力士舞台/卡片互斥＝user 明示
先不管，未動。

- **主選單**：開房間＋邀請制/公開切換裝進同一個 inset 盒（歸屬用容器表達；
  「房間可見性」標籤退役＝盒子即歸屬）；切換下加當前選項後果說明（邀請制=
  只有拿到碼能進／公開=出現在公開列表）；**加入房間升同級**（與開房間同款
  PrimaryStyle＝兩個並列前門）；卡底新增門口預告三行（無臉=先上傳自拍／
  自拍處理中／未登入=開房會開瀏覽器登入 Epic）＝臉閘門與登入 portal 不再是
  按了才知道的驚訝；**離開二段確認**（3s 武裝「再按一次離開」）＋與日常鈕
  拉開 40px；狀態行旁掛**取消鈕**（進行中可按）。
- **連線層**：新增 `CancelMenuAction()`（hosting/searching/joining 全可取消；
  bCancelRequested 旗標由完成回呼消費——建成/加成則 DestroySession 不旅行、
  搜尋結果丟棄、登入等待中清 PendingAfterLogin＋失敗不再開 portal 不報錯）。
- **加入頁**：房號格下加「直接用鍵盤輸入」；狀態/錯誤行從螢幕底移到加入鈕
  正下方（報錯貼著出事的地方）；重新整理鈕移進公開房卡片標籤列（歸它管的
  列表旁）；底帶只剩返回；房列表行補 ping（臉像待雲端上行=既有記帳）；
  **OnMouseButtonDown 焦點回收**（點頁面空白處把鍵盤焦點抓回=打字不再無聲
  失效）。
- **個人檔案頁**：眉毛鐵律＋隱私聲明移到上傳鈕**之前**（必讀先於動作；眉毛
  警告升 Ink 色階）；現金值只放值（未登入=$ —、已登入雲端未到=$ …——
  硬編碼 $10000 猜測值退役）＋未登入時下行說明「開房/加入會自動登入」（死路
  開出口；LAN=僅標未登入）；名字欄改**hint 制**（自訂名=實值；保底/平台名=
  淡字 hint＋「這是隨機名——點上面改成你的名字」說明＝隨機名不再冒充已取名）
  ＋儲存後「已儲存」2s 回饋；臉閘門紅字與「處理中」狀態互斥（不再同屏互咬）。
- **設定頁**：顯示組（視窗模式+解析度+套用鈕）裝 inset 盒＝套用鈕管轄範圍
  一眼可見；靈敏度/音量在盒外＋「調整立即生效」註記；**無邊框下解析度列
  停用**＋「無邊框固定使用桌面解析度」說明（死旋鈕不再假裝有效）；套用鈕
  只在 pending≠引擎現值時可按；**未套用按返回=二段確認**（amber 警告 4s、
  再按才丟棄）；語言列加「›」＝導航列與調值列分家。
- **導航對稱**：語言頁記住來源（LangOrigin）——從設定進、選完語言/返回都回
  設定（RecreateMenu(bOpenSettings) 跟著來源走）；**ESC=與返回同目的地**
  （授權→設定、語言→來源頁、設定→二段確認、個人檔案→存名字回主選單）。
- 新 robo 鉤子：`NiMenuShowSettings`；新 loc 鍵 13 個×13 語（Claude 初稿）。
- 驗證：編譯過；四頁 NiMenuShot 幾何自查（root=盒+同級鈕+門口預告、join=
  鍵盤提示+刷新歸位+空列表文、profile=$ …+警告序+自訂名實值、settings=
  分組盒+解析度停用+套用鈕停用態）全過。**未機測**（邏輯已入、狀態難以無損
  staging）：取消鈕實戰、二段離開/返回、已儲存回饋、ESC 各路由、焦點回收、
  處理中門口預告；**總驗收=user viewport**。
- **2026-08-11 追記＝主選單兩步流（user 裁決「頂層只放動詞」）**：08-10 的
  「開房間＋可見性同盒」再改——主卡收斂成**開房間/加入房間兩顆同級鈕緊貼
  相鄰、中間零內容**；可見性二選＋後果說明＋確認鈕移進新的**開房設定頁**
  （EPage::Host，與加入頁同構：標題/白卡選項/Primary 確認/狀態行+取消/返回；
  臉閘門照舊擋在主卡按鈕）；新 robo 鉤子 NiMenuShowHost。自查截圖=
  uifix2_root/uifix2_host（兩鈕相鄰同級✓、開房頁選項+說明+確認✓）。
- **2026-08-11 追記②＝user 三連抓（腳沉黑/語言雙入口/頁高不齊）**：
  ①**舞台腳沉黑定罪＋治本**——選單舞台只有兩盞俯角平行光、深蹲小腿/腳面
  朝下＝全照不到→沉進黑背景「像被遮住」（07 版 FillLux 1.6→2.4 是同病治標）；
  修=第三盞仰角補光（UpLux 1.6 旋鈕、無影、相機側 +32°），三相位截圖驗證
  後腳全亮。②**語言入口唯一化**——設定頁語言列砍除，「文A」主選單鈕=唯一
  入口（救援語義：任何語言狀態都找得到）；LangOrigin 機制保留。③**全站骨架
  統一**（上批問題單第 4 條被我漏修且未申報——記過）：七頁全部=標題帶 top 56
  ／內容 FillHeight 置中／底帶（返回/導航列）錨 bottom 56。
  尚欠：整體構圖美術層（三坨漂浮物/毛玻璃卡在黑底=灰板）待 user 給
  ground truth 參考後重排——user 已拒絕實作者自行設計視覺。
- **2026-08-11 追記③＝設定頁立即套用制（user 定案「改什麼都立即套用」；
  調查→配方→實裝）**：套用鈕/dirty 檢查/未套用二段返回警告/「立即生效」
  註記/顯示分組盒全退役——整頁單一模型（改了就生效、就存檔）。配套三件
  （裸立即套用的三個真問題各對一解）：①**去彈跳 0.8s**＝步進器連按只切最後
  一檔（防每格中繼值各黑閃一次）；②**解析度清單改執行期查詢**
  （GetSupportedFullscreenResolutions／windowed 用 GetConvenientWindowedResolutions、
  查詢失敗退回靜態表）＝顯示器做不到的檔位不出現（拆套用鈕=拆還原保險，
  必須從源頭消滅壞組合；順帶修好 16:10/超寬選不到原生解析度）；③**套用後
  1.2s 對賬 GSystemResolution**＝要求沒被驅動接受→UI 播回真值+存檔跟真值走
  （無邊框恆桌面解析度=不比解析度）。離開設定頁去彈跳照常落地。ESC/返回
  簡化回直通。自查=uifix3_settings（四列扁平/無套用鈕/無邊框停用列+說明）；
  實際切換行為未機測、待 viewport。備註：解析度列在無邊框下顯示的存檔殘值
  （引擎預設 1280x1024）已停用不作數。
- **2026-08-11 追記④＝視窗模式簡化制（user 裁決「視窗就該像瀏覽器」）**：
  獨占全螢幕與解析度選單整組退役——黑閃/去彈跳/對賬回滾/確認倒數缺口的
  存在理由整類消滅（追記③的立即套用機具同日拆除）。新制：①視窗模式=
  **無邊框/視窗二態**即點即切（同為合成器視窗=不經顯示器重同步=不黑閃）；
  ②**視窗=瀏覽器式**：標題列+最小化/最大化/關閉+邊角拖拉改大小（引擎原生
  bAllowWindowResize 預設開、GameEngine.cpp:549 查證）；進視窗模式給桌面 70%
  初始大小（FSlateApplication::GetCachedDisplayMetrics；FDisplayMetrics::
  RebuildDisplayMetrics=ApplicationCore 模組 LNK2019 鐵坑）；③效能旋鈕改
  **渲染比例**（r.ScreenPercentage 50~100%、ECVF_SetByGameSetting、±5 步進、
  立即生效零切換；RenderScalePct 入 SettingsSave=本機+雲端同步、LoadSettings/
  雲端套用後 ApplyRenderScale）；④legacy ini 殘留 Fullscreen=選單建構時自動
  遷移無邊框。設定頁終態四列：視窗模式/渲染比例/滑鼠靈敏度/主音量——
  全部即點即生效即存檔。新 loc 鍵 RenderScale×13。自查=uifix4_settings；
  未機測：二態實切/拖拉改大小/渲染比例視覺效果，待 viewport。
- **2026-08-11 追記⑤＝效能量測（user 問「確切誰需要調畫質」）→撞出 TSR 災情
  →定罪→ini 修正**：
  - 量測矩陣（stat unit 截圖法、r.VSync 0/t.MaxFPS 0、editor 二進位 -game）：
    **修正前（=出貨現狀）RTX 3060（Steam 最大宗主流卡）空道場**：
    ≈1080p GPU 35.8ms（26fps）／2560x1380 49.8ms（20fps）／≈4K 87.9ms（11fps）；
    選單同病 51.9ms——**中位數顯卡連 1080p 60fps 都不到＝全員災情**。
  - **分解定罪：唯一病根=UE5.7 預設 TSR 抗鋸齒**（本機病態：~37ms@1440p，
    遠超正常 1-3ms 量級）——只換 TAA=12.7ms（4×）；只關 Lumen/VSM 保留 TSR
    =49ms（無效）。Lumen GI/反射/VSM 在 fullbright 場景近乎免費（無光源無
    陰影投射者=系統閒置）。
  - **修正入 ini**（DefaultEngine.ini RendererSettings）：AA=TAA＋Lumen GI/
    反射/VSM 關閉（fullbright 無消費者、他牌硬體不賭）。像素差分驗證畫面
    幾何無變（mean 1.56/255、殘差=AA 邊緣風格）。
  - **修正後實測**：3060 @2560x1380=12.4ms（78fps）；**Intel UHD 770 內顯
    @1080p=19.1ms（≈50fps）**（-graphicsadapter=1 直測、log 驗證 adapter 1；
    含跨卡 present 開銷）。
  - **「誰需要調畫質」的量測答案（修正後）**：①內顯玩家（無獨顯筆電）＝
    1080p ≈50fps 可玩、要 60 才需要降渲染比例（更老的 UHD 630 級估 ~25fps
    =必須降）；②4K 螢幕＋中階以下卡＝估 ~36fps（Steam 4K 佔比 ~4% 且通常
    配好卡=極小群）；③其餘全部（獨顯+1080/1440p）=60fps+ 無需求。
    CPU 恆 ~2ms=純 GPU fill bound=渲染比例旋鈕對症。
  - 遺留驗證：TAA 視覺（veil 需重跑 robo_veilflicker——當年在 TSR 下校準）；
    打包版重測 TSR 異常是否 editor 二進位限定（好奇項非阻斷）；正式 6 人房
    多角色成本未量（skinned+Jiggle 增量、fill 主導不變）。
- **2026-08-11 追記⑥＝樣式源統一（user 定案「用專業方式做字級間距」）**：
  NiceInkUiTokens.h 從調色盤升級為完整樣式源——①**字體角色表 NiType**
  （9 角色：Display 88/Title 42/Value 26/Action 20/ActionSmall 16/Body 15/
  Note 12/Label 10+280 字距/Micro 10；每行字必屬一角色、程式碼禁填裸字級——
  SNiMenu 的 Font/Serif/Label 三 helper 退役、換單一 Ty(Role)）；②**間距網格
  NiSpace**（XS4/S8/M16/L24＋BandTop/Bottom 56＋CardW 460/Wide 560＋
  CardPad 32×28＋BtnPadV 14——版面槽位間距全 token 化、4 倍數網格）；
  ③canvas HUD 四級字階（34/21/15/12）併同表（TierSize 引 NiType::Hud*）。
  掃蕩結果=約 45 個字體裸數字+30 個間距裸數字歸零；歸一化的字級變動（18→20、
  17→16、16→15、14→15、13/11→12）＋卡寬 430/600→460/560＋卡內距統一。
  規則：**改字級/間距只准改表**。自查=style_root/host/profile/settings 四圖；
  視覺終審=user viewport。
- **2026-08-12 追記⑦＝內容審視落地（user 定案六項）**：①**首啟創角流程**——
  開機無臉＝直進「創建你的力士」頁（個人檔案頁換帽：標題切換、無返回無 ESC、
  底帶=文A+離開二段確認；臉在 Persona 開機同步載入=Construct 可靠判定；走
  OpenProfilePage=名字欄照常播種）；臉到手=Tick 自動進主選單（力士戴自己的臉
  跳舞=第一印象）；連帶退役：主選單門檻預告兩行、臉閘門紅字、Host/Join 的
  HasFace 檢查、bFaceGateNudge（「需要用字解釋的流程=錯的流程」）；robo 不受
  影響（AutoHost 直呼連線層）。②**詞改玩家語言**：渲染比例→畫質、無邊框→
  全螢幕（借 Fullscreen 鍵、Borderless 鍵退役）、房間可見性→誰能進房、中文
  房間碼/輸碼→房號統一（AskHostCode/JoinWithCode/ErrEnterCode/
  ErrNoRoomWithCode/LobbyCodeHint/InviteOnlyDesc 六處）。③尚未登入→行動句
  「開房或加入時會自動登入 Epic（會開啟瀏覽器）」（SignInBrowserNote 重寫；
  LAN=整行藏；NotSignedIn 鍵退役）。④公開房卡=沒房收成一行字+重新整理、
  有房才展開（卡內空狀態退役）。⑤Label 10→12/字距 280→160（中文無大寫=
  英文小標籤慣例的殼不能硬搬）＋InkDim 加深 3.4:1→5.0:1（過 WCAG 4.5）。
  ⑥語言頁標題文A→語言（Language 鍵升標題、EN 改 Language；入口鈕不動）。
  新 loc 鍵 CreateYourRikishi×13。自查=copy_onboard/join/settings/lang 四圖
  全過；FaceGateHint/FaceGateDoor/FaceProcessingDoor/Borderless/NotSignedIn
  鍵留表未用（決策史）。**未修已知項：語言頁選中仍=酒金（選取/動作同色=
  首輪審計第 1 條），待 user 裁決改墨底。**
- **2026-08-12 追記⑧＝階層手術（user 三連定罪：強調押錯/四層擠一號/系統自保）**：
  ①**字階拉開**——Note 12→13、Label 12→11（粗體+字距 200、對比已 5:1）、
  新 Warning 角色（15 圓體粗+墨=比說明大兩階）、FRole 加 bBold 軸；階梯鐵則
  入表註解：相鄰身分至少差兩樣、同尺寸必粗細+顏色雙軸、**空值不得領大字**
  （現金 $ —/$ … 降 Body 級、有真數字才 Value 26——Font_Lambda 動態切）。
  ②**個人檔案/創角卡重排**（強調跟任務走）：臉庫提頂（這頁的身分本體）→
  眉毛警告（Warning 級）→隱私→上傳鈕→狀態→名字（可選退後）→現金
  （**創角模式整組隱藏**=新玩家的空錢包是純噪音）。自查=hier_onboard
  （乾淨沙箱首啟：警告→鈕的單任務讀感✓）+hier_profile（臉庫置頂✓空值小字✓）。
  ③工作坊事實修正：**EOS persistentauth 快取在系統層不在 Saved**——
  -saveddirsuffix 沙箱照樣自動登入、雲端名字/偏好跟帳號回來（臉不在雲端=
  創角照觸發）；play_fresh.bat 註解已更正、真全新帳號=登出 Epic 或換帳號。
  病歷記帳：「四層擠一號」是 Label 10→12 修可讀性時未查同級撞名造成——
  改一值必查相鄰層（已入階梯鐵則）。
- **2026-08-12 追記⑨＝個人檔案頁雙模式排序（user 抓「順序為創角優化、日常
  繼承=錯」）**：卡內容拆四塊積木（臉庫/名字/現金/上傳區塊）＋建構時按模式
  組裝——**創角**＝任務優先（眉毛警告→隱私→上傳→名字）；**日常**＝你的
  東西在上（臉庫→名字→現金→細分隔線→上傳區塊沉底=「要改臉才需要的」
  整組分家）。bOnboarding 改在建 UI 前計算；創角完成點 Tick 觸發
  RecreateMenu（內部延遲一 tick=安全）重生日常版面。自查=order_onboard/
  order_profile 兩圖（創角=上傳優先✓、日常=資產在上+分隔線✓）。
- **2026-08-12 追記⑩＝日常個人檔案排序終案（user 二選一裁決）**：名字→現金
  →分隔線→臉庫→眉毛警告→隱私→重新上傳——分隔線語義=「你是誰/你有什麼」
  vs「臉的管理區」；臉庫與重新上傳是同一件工具的兩半（縮圖點選=換臉=動作）
  不拆家。自查=order2_profile。
- **2026-08-12 追記⑪＝頭像亭（user 委託「每角色一個臉 icon、局內辨識+臉庫
  選擇、要好看」）**：icon 改制＝**遊戲內那顆頭的 3D 正面肖像**（丁髷+膚色+
  臉貼圖、頭滿框、黑底）——取代臉貼圖 UV 裁切（膚色豆腐）。理由：辨識對象
  =場上那顆頭（零翻譯）、選臉選的是上身效果、丁髷輪廓=本作圖騰。
  實作=`ANiceInkPortraitBooth`（全程式隱形攝影棚：世界下方 -8000、替身
  ANiceInkCharacter（墨 RT 壓 256）、SceneCapture FinalColorLDR 256RT/張、
  **光照通道 2 全隔離**（世界光不進亭、亭燈不漏世界=跨關卡亮度恆定）、
  key+rim 雙點光（平坦無衰減=fullbright 局部版）、快取鍵=字串
  （lib_<id>/seat<N>_v<rev>/roster<idx>）、**WorldType==Game 範圍閘**
  （FaceShare 同款）=PIE robo 全套零干擾、亭缺席時 HUD/選單自動走舊裁切路
  墊檔）。消費端：Persona GetFaceThumb（thumb.png 退役、改亭肖像+菜單 0.5s
  暖機重試）＋HUD DrawFaceTok（FaceShare 席臉肖像優先、roster 後備、舊路墊檔）。
  取景五輪（診斷輪抓到 spawn yaw 0=拍到背、骨骼查詢不穩→明確旋鈕瞄準）：
  終值 dist150/aimZ61/FOV22/曝光8.0/rim20。驗證=booth_v4/v5 縮圖（正臉滿框
  髷尖入框）＋booth_dojo（局內席位列肖像上屏）。已知記帳：黑髮對黑底輪廓
  仍偏溶（rim 20 偏弱、旋鈕在）；listen host 的亭替身活在伺服器世界（GameMode
  全角色迭代器理論可觸=待 4p 實測）；roster 肖像路未實測；intake 的 thumb.png
  生成成孤兒（無消費者、待拆）。
- **2026-08-12 追記⑫＝頭像亭 v2（user 兩刀：過曝＋「不是方形、要正交精準裁
  頭形」）**：捕捉改**正交投影**（OrthoWidth≈頭寬 46cm、距離無關）＋
  **SceneColorHDR**（文件保證 A=inverse opacity＝透明背景唯一可靠來源；
  FinalColorLDR 的 alpha 是垃圾＝方形黑底版病根）＋**CPU 端後製**：讀回→
  alpha 反轉→頭界 bbox→方形精準裁切（+8% 邊距）→ColorGain 手動曝光→
  sRGB 轉換→透明背景 UTexture2D（成品非 RT）。過曝根修=拋棄 tonemap 鏈、
  HDR 線性直轉（gain 1.0 即自然膚色）。HUD 畫法＝無框無底直畫頭形
  （BLEND_Translucent）；選單縮圖天然浮在卡上。終值 AimZ64/Ortho46/gain1.0。
  驗證=booth_v6（透明+膚色自然）/booth_v7（丁髷全入框、肩出局＝恰好頭部）。
- **2026-08-12 追記⑬＝臉庫縮圖去底板（user 抓「改了貼圖沒拆按鈕方形底＝
  還是方形」——驗收只放大貼圖沒看在頁上的完整樣子=記過）**：縮圖鈕改
  FaceTileStyle（平常全透明、hover 0.08/按下 0.14 微亮）＋穿著中金線從
  「壓下巴」移到頭像正下方（SOverlay→SVerticalBox）。驗證=booth_v9 原位
  縮放圖（頭形裸浮於卡、金線在下）。教訓：icon 驗收必看「在頁面上的完整
  呈現」不是貼圖本體。
- **2026-08-12 追記⑭＝頭像亭 v3 曝光量測＋肩楔物理解（user 三刀：「看不到
  過曝嗎？臉下面黑黑的？為什麼有肩膀？」）**：①過曝改**量測制**（目測
  「自然」=29.7% 撞頂 255 的實錘——膚色像素直方圖 p50/p90/clipped% 進迭代
  迴圈）：Key 12→7＋gain→0.62 收到 6.3%，殘餘定位=鼻樑熱斑純 R 頻道
  （p50=189 全臉正常、再壓 gain=整臉拖暗）→**高光軟膝蓋**（線性域 0.8 以上
  Reinhard 壓進 [0.8,1)、中間調零觸碰）＝終值 0.8% 撞頂。②臉下黑帶=下巴底
  無光→FillLight（下前方 5.0）。③肩楔＝輪廓啟發式三連陣亡（寬比切=切在
  臉頰（力士臉最寬處）、貼邊切=切在眼睛（肩丘與臉同列並肩）、中央連通段=
  肩與下顎輪廓相連）→**物理解=SCS_SceneDepth 第二趟深度遮罩**（肩在鼻尖
  後 15~25cm）；單一閾值血價（16cm=丁髷被砍、26cm=肩楔活著——髷與肩深度
  重疊）→**上下分域**（肩不可能在頭頂：上域 34cm 保髷、下域 14cm 殺肩、
  分域線=頭直跨 58%）。終值 Key7/Fill5/gain0.62/膝蓋0.8/深度34↕14@58%。
  驗證=booth_v17 量測（clipped 0.8%、p50 189）＋原位整頁圖（頭形乾淨、
  髷完整、無肩無黑帶）。教訓：**視覺缺陷用數字定罪再修**（直方圖>目測）；
  **輪廓啟發式對「臉頰比肩寬」的體型必敗＝幾何問題用幾何資料（深度）解**。
- **2026-08-12~13 追記⑮＝頭像亭 v4 均勻光制＋模型預熱＋縮圖放大**：①亭燈
  三點光（Key/Fill/Rim）退役→**±XYZ 六面同強度點光＝全方向均勻**（user
  「不產生陰影的打光」；SkyLight 不可用=USkyLightComponent 無 LightingChannels
  （ULightComponentBase 旁系）＋桌面 deferred 天光 shader 不理通道＋地下
  SLS_CapturedScene 捕到虛空——三重定罪後六面點光=隔離架構下唯一解）；
  ②過曝根治=**膚色錨定自動曝光**（SceneColorHDR=無 tonemapper 的裸輻射、
  手動 gain 每換光重校一次=錯誤架構→量頭部像素中位亮度÷已知膚色亮度=光場
  常數 K 除回＝還原 albedo；亮度與光強解耦、膚色深淺身分特徵保留）；
  ③偏灰根治=**ACES 擬合曲線（Narkowicz）＋Saturation 1.15**（主畫面的不灰
  來自引擎 tonemapper、capture 繞過整條後處理鏈）；④五官增顯=**unsharp**
  （均勻光把形狀陰影歸零、五官只剩貼圖色差→亮度 6px 模糊差放大 0.8、alpha
  加權防輪廓黑暈；DetailAmount/DetailRadiusPx 旋鈕）；⑤ONNX 模型預熱=開機
  背景建四模型（LaMa session ~60s 與創角頁待機重疊；無臉玩家才預熱、GIsEditor
  閘 robo 零干擾、與 RunIntake 同鎖永不重載；實測 14.4s 完成）；⑥臉庫縮圖
  56→84+SWrapBox 換行（4+2）。
- **2026-08-13~14 追記⑯＝大廳配對補完＋LAN session 真兇翻案**：Meccha UX
  web 考證（房名+密碼+地區+tags 瀏覽器制；「我們的 4 字母房碼=更新一代」）
  →user 點名六缺口：①名字寬度截斷 FitTok（16 碼元≠顯示寬、CJK 爆版面；
  七繪製點全套用、.Left(14) 退役）＋輸入框打字即截；②房主標示
  （PlayerState.bIsRoomHost＋大廳「host」金綴）；③房間人數 4~6（「上限/下限」
  官僚概念退役=房主直接決定這房幾個人、坐滿關門；開局門檻 4=規則藏在開始鈕、
  PIE 維持 2 服務 robo；lobby 三字串寫死「/ 6」退役）；④ESC 踢人（listen
  server 上 HUD 免 RPC 直呼 GameMode→GameSession::KickPlayer＋KickedNetIds
  本場拒再入）；⑤地區/tags 不採用→**語言=配對邊界**（user 定案：哏要看得懂
  語音要能聊才有指認情報流）。**LAN 搜房真兇**（user 拒收「結構性解不了」）：
  防火牆與共用 port 單播（量測皆真）都是紅鯡魚——verbose log 活體定罪=查詢
  抵達主機、主機零回應＝`AGameMode` 開場自動 StartSession→session InProgress
  ＋bAllowJoinInProgress=false→IsSessionJoinable 靜默假。修=NiceInkGameSession
  no-op＋SetSessionInProgress 在真開局/回大廳鏡射 session 狀態（場間大廳
  重新可搜=順帶更正確）；驗證=同機自駕 E2E FindSessions 1 found→travel 進
  道場。-nilanloopback 降級保底留置（play_full_flow*.bat 帶旗標）。
- **2026-08-14 追記⑰＝公開房規模版（user 爆粗定案「一切按規模設計」）**：
  ①房名=徵人啟事（NINAME 屬性、24 碼元、控制字元消毒保空白、公開限定；
  hint「讓別人知道你在找怎樣的玩家」）；②語言全鏈=NILANG 屬性（建房頁公開
  限定 13 語選擇器、預設跟介面語言）＋列表過濾（EOS=查詢端屬性過濾=規模正解、
  LAN=顯示層）＋過濾 chip UI（預設我的語言/全部/指定）；③列表 UX=左房名
  （無名公開房顯房號=可唸身分錨；「不顯他房碼」限縮至私房）右人數點點●○
  ＋ping、排序三鍵（同語言→人多=快成局→ping）、上限拆除+捲動、空狀態
  「自己開一間」捷徑、12s 自動重搜；④MaxSearchResults 32→100（隨機子集病）；
  ⑤搜尋體感三連修=雙緩衝（開搜不清列表=閃爍根治）＋背景搜尋靜音（狀態列/
  按鈕/取消全不動；碼路搭上轉前景）＋**即時串流+碼路早退**（LAN_QUERY_TIMEOUT
  =5 引擎 #define 不可配置、但回應毫秒級就在 SearchResults——0.2s 輪詢即到
  即上桌、碼命中退訂回呼直接加入=5s→<1s，量測 early join→1.3s Welcomed）
  ＋可點判準改「加入流程能否受理」（只擋 Joining/Hosting；完成回呼見 Joining
  不踩狀態機）。碼路護欄=永不過濾＋搭便車搭上過濾搜尋漏接=重搜一次不過濾。
  待實機=EOS 查詢端過濾（併 B4）、選擇器點選手感。
- **2026-08-14 追記⑱＝場內移動/動作同步卡頓三兇根治（user「請仔細調查」→
  「請全部修好」）**：①**臉分發列車 vs 引擎頻寬帽**（主兇）：blob 實測 1.2~1.6MB/人
  （log 定罪）、列車節奏 8×16KB/0.1s≈1.3MB/s，撞引擎預設每連線帽 **100KB/s**
  （MaxClientRate/ConfiguredInternetSpeed 皆 100000、LAN 同帽；07-26 只調了 tick
  頻率沒動帽）→每張臉 12~15s 連線滿載、飽和期間移動屬性複製整段被跳過＝
  進道場/有人進房後長段卡頓；上行同帽＝上傳者自己的 ServerMove 排隊＝他的
  角色全房卡。修=ini 帽提 1MB/s（四值：MaxClientRate/MaxInternetClientRate/
  ConfiguredInternetSpeed/ConfiguredLanSpeed）＋列車降速 2×16KB/0.1s≈320KB/s
  ＋下行改**每收件者各自額度**（舊全域 budget=多收件者串行灌單線；現並行、
  每連線恆 320KB/s；B3 persona 資產列車=KB 級不需動）。B7 blob 壓縮仍是根治面
  （PNG 降階可 1.5MB→200KB 級）＝原帳保留。②**可見身體繞過引擎網路平滑**
  （結構性）：CMC 的 simulated-proxy＋listen-server 平滑只寫 ACharacter::Mesh
  相對變換——本作 Body/BowBody 掛 capsule 下＝每個網路修正原封硬跳在可見網格
  （EOS P2P 抖動/飽和期間=可見瞬移；jiggle 把 <100cm 的 snap 當激勵放大體感）。
  修=無資產 Mesh 釘 capsule 原點當**平滑載體**、Body/BowBody 改掛其下＝免費
  繼承引擎 Exponential 平滑；**本人端 Tick 強制 NetworkSmoothingMode=Disabled**
  （平滑只服務「看別人」——本人若吃平滑，client 修正時凍結相機錨/可畫域採樣
  會讀到半路骨位；防禦性補丁、本人視覺=capsule 硬跳＝改制前行為原樣）。
  server 判定/畫墨 UV 解算讀的位置零變動（authority 端偏移恆零）。③**遠端
  轉身 1.4° 階梯**：FRepMovement 旋轉量化預設 8-bit/軸→ShortComponents 16-bit。
  驗證=robo_gait_probe **10/0**＋robo_remotejitter 時序全綠（remote hips p50
  0.14/p95 2.05cm 與 owner 同量級、marker 兩契約 PASS）；probe 曾報 first-lock
  dCamToPoint 102.5cm FAIL→**stash 對照實驗定罪=既有過時檢查非迴歸**（07-26
  寫的 90cm 上界對應機器工具眼錨；07-25 起入鎖預設=Stencil＋07-31 凍結相機
  =102.5 為現行合法幾何；儀器上界修 120 已記註）。**注意：in-process PIE
  迴路零網路修正＝平滑改動在 robo 環境是嚴格 no-op（前後統計逐位相同實證）
  ——②的效果只在真網路（EOS P2P/雙機）顯形，手感閘=user viewport 雙機實測**。
  鐵坑沉澱：引擎頻寬帽 100KB/s 是隱形地板——任何 MB 級 reliable
  列車先算帽；「listen server 也平滑 client 的 pawn」與「平滑只寫 GetMesh」是
  同一條引擎事實的兩面，自訂顯示元件要嘛掛載體吃平滑、要嘛本人 Disabled。
- **2026-08-14 追記⑲＝臉 blob 壓縮 NIF2＋tone 先行（user「進房臉/皮膚載入
  怎麼這麼慢」→「動手」；追記⑱ 列車降速的代價面根治＝blob 壓縮舊帳結清）**：
  ①**NIF2 格式**：face_open/closed 的 RGB 走 JPEG q90、alpha 走無損灰階 PNG
  分載（島罩/頸淡出=承重合成通道、JPEG 不帶 alpha——合成端 JPEG 解回 BGRA
  ＋灰 PNG 塞回 A、CreateTransient 上 GPU）；眼罩照舊 PNG；解碼端相容 NIF1；
  打包結果快取 Dir/blob_nif2.bin（library 時間戳資料夾不可變=快取恆有效、
  省每次進房轉碼）。②**tone 先行**：膚色 16 bytes 不搭列車尾班車——FaceBegin
  RPC（Server/Client 兩向）直接帶 FLinearColor，登記簿 StoreToneEarly 獨立
  ToneRevision（不觸發貼圖套用路）→EnsureAvatarApplied 先套 ApplySkinToneOnly。
  ③實測（同機 LAN E2E、host 真臉+P2 沙箱臉）：blob 1,219,835→**364,683**／
  1,623,678→**418,239**（3.3~3.9×）；client Welcomed→雙向臉全套用 **2.2 秒**
  （throttle 前全速灌=1.9s 但餓死移動；throttle 後未壓縮=5.5s 上行+5s 下行；
  現在=流量控制下拿回全速體感）；tone 先行實測比臉貼圖早 ~1.3s 上身。
  四向對賬全綠（host/client view×兩席）。**視覺質感（JPEG q90 臉）待 user
  viewport**；殘帳=自訂臉雲端儲存＋88s 背景烘焙 UX（原帳照舊）。
- **2026-08-14 追記⑳＝進房臉同步收官：提速並行＋進房載入閘（user「先做成並行，
  再收進進房載入」）**：①**提速**=臉列車 2→4 chunks/0.1s≈640KB/s（NIF2 後一張
  ~0.7s；帽 1MB/s 恆有餘裕）＋FaceShare 就緒輪詢 0.5→0.1s（握手省 ~0.4s）；
  上行/下行本就並行、多收件者本就並行。②**進房載入布（veil）**=臉同步齊全前
  HUD 蓋整屏（判準=本人臉上行 ack＋ClientFaceManifest 席位集全入簿；8s 保底
  掀開=傳輸失敗不卡死）；文案借現成 StatusJoining 13 語鍵。③**bFaceReady
  現身閘**=server 收到該員 blob 才設 ready（複製旗標）、未 ready 的力士對旁人
  SetActorHiddenInGame 整體隱形——**房內從頭到尾不存在頂著名冊臉的力士**
  （#52 身分載體閉環）；保底三重=host 恆即刻 ready／無臉端 hello 帶旗標即刻
  ready／server 報到後 10s 強制 ready（>veil 8s）；閘只在自己閘態變化時動手
  =不與 ghost 全隱退化路互咬。④**開局臉齊保險**=RequestStartMatch＋HUD 開始
  提示同判準（Game 世界限定、PIE/robo 不受擾）。實測（同機 LAN E2E）：
  **Welcomed→veil 掀開 0.89s（ready 非 timeout）**、→雙向臉全套用 ~1.1s
  （前一輪 2.2s；快取命中省掉 0.43s 現場轉碼實證）。鐵坑：**閂死旗標只准掛
  恆真條件**——IsLocallyControlled 在剛進世界時 Controller 複製晚一兩幀=瞬態
  假、上閂=veil 永不出現（首輪 E2E 實錘）；同機重啟撈房碼要防 log 輪替時序
  （撈到舊場 HVZD 實錘、先驗 Log file open 時間戳）。現身閘視覺與 veil 體感
  =user viewport（雙機 EOS 併 B4）。
- **2026-08-14 追記㉑＝現身閘三修：單一權威制（user 回報「一邊看得到、另一邊
  幾秒後才看到」→定罪→重構）**：①**病根 A（多秒空窗）**=臉列車單 tick 爆發
  4×16KB——頻寬帳逐幀記（每幀預算=Rate/60≈17KB@1MB/s），64KB 爆發把該連線打進
  飽和數幀、bFaceReady/bHidden 小屬性被餓（log 定罪：P4 現身旗標對 P3 晚 2.3s
  =「一邊看不到人」本尊）。修=列車抹平 1×16KB/0.025s（同 640KB/s）＋帽 2MB/s
  （單塊恆低於每幀預算）。**鐵則：頻寬帳逐幀記——平均速率合帽≠逐幀不超帽，
  爆發節奏照樣餓死屬性複製。**②**病根 B（雙寫者競態）**=client 本地閘與 server
  複製的 bHidden 互相蓋寫（變化偵測/逐 tick 斷言兩版都有洞：P2 端時序恰好可用、
  P3 端 gate 全程沒接手=名冊臉閃現 0.7s）→**重構=單一權威**：bHidden 只有
  server 寫——玩家 pawn spawn 即隱形，觀看者收妥 blob 回 ServerFaceGotSeat ack、
  GameMode 等「發放當下全部在册觀看者 ack 齊」才 FaceGateShowNow（5s 保底）；
  client 端閘整組拆除；晚一步報到的觀看者不進 ack 名單=他自己被 veil 蓋著。
  bFaceReady/bFaceNone 降級 server-only（複製拆除）。③**pid=-1 迴歸修**=閘只認
  有 PlayerState 的真玩家——頭像亭替身/選單舞者曾被閘隱形（23:28~00:00 的
  build 選單舞者會消失，已修）。實測（host+P3+P2 三實例）：新人 join→隱形→
  最後一個觀看者 ack 後 **2ms** 現身（全程 1.65s、與本人 veil 同步在 134ms 內）；
  veil 0.83/1.34s 全 ready；無 pid=-1 閘 log。**教訓：可見性這類「一份狀態、
  多端消費」的東西，寫者必須唯一——client 補強看似保險，實為競態來源。**
- **2026-08-15 追記㉒＝開步腳播種（user 抓「第三人稱往右移會先左傾再右傾」）**：
  步態相位起步恆歸零＝左腳撐地先行（重心第一拍壓左）——往左/前後讀感自然、
  往右起步第一拍反向壓左＝「動作遲疑」讀感。修=起步時按初始橫向速度播種
  WalkAnimPhase（明確往右（VelCS.X<-20；CS +X=角色左側）從 0.5=右腳撐地起、
  其餘照舊 0）；bGaitPhaseSeedPending 掛在兩處歸零點（活化/停步）。守恆式/
  雙軌制/貼地構造零變動；robo_gait_probe **10/0**（含右橫移段）；手感待 viewport。
  **二刀（user「幾乎沒有改善」後重診）**：一刀修錯靶——重心橫移在起步瞬間被
  0.17s 蹲斜坡壓近零＝根本不是可見的第一拍。真兇=**GaitSlideDirCS（上身前傾
  的傾斜軸）從不重設**：殘留上一段行進方向，起步時前傾角隨蹲斜坡長出、軸卻
  指舊向；且 vector-lerp 對反向目標先縮後過零硬跳＝朝錯邊傾 ~80ms 才甩正。
  修=起步播種傾斜軸（當下速度向直設）＋行進中 >90° 轉向硬切（dot<0→直接換向；
  美術語言=明確硬轉；≤90° 順向微調照舊 τ100ms）。gait probe 重跑 **10/0**；
  手感待 viewport。**教訓：手感 bug 先找「可見的第一拍是哪個量」——被斜坡
  壓制的量修了也無感；平滑器的殘留狀態（不重設的方向濾波）=起步方向 bug 常客。**
  **三刀終案（user「即時了但少了平滑與前搖、質感沒了」）**：二刀把病和優點
  一起砍了——殘留舊向其實是前搖（anticipation）的天然起點，原版的病只在
  「擺過去的方式」（向量 lerp 對反向先縮後過零硬跳=卡 80ms 再啪）。終案=
  **等角速擺轉**：傾斜軸以 GaitDirSlewDegPerSec（新旋鈕、預設 600°/s）連續掃向
  行進向——起步先朝殘留舊向微傾、~0.3s 弧線掃到行進側=前搖回歸且全程無卡
  無跳；小角度微調近瞬時；正對 180° 取道身前（重心經前方轉移讀感）；二刀的
  播種/硬切全拆。gait probe **10/0**×3；**前搖節奏=viewport 旋鈕題**（大=俐落
  小=黏）。**教訓：user 連打兩輪相反方向（「反向傾是 bug」→「前搖沒了」）＝
  病根不在方向而在「運動的連續性」——修 bug 前先分離「病」（卡+跳）與「質感
  載體」（殘留方向=前搖），別一刀全砍。**
- **2026-08-15 追記㉓＝站立視野俯仰上身（user 定案逐字：「抬頭低頭要反映在旁人
  看到的人偶頭部；左右禁止（穿膜）＝全身一起轉、頭身零相對位移，維持現狀」）**：
  此前站立自由視角的相機 pitch 只活在本地相機、他端人偶頭恆平視。實裝=本人
  PollLook 上報 LookPitchDeg（30Hz 節流、變化 >0.5° 才送、COND_SkipOwner；listen
  主機直寫）→ 他端 K20 追趕（同 aim 慣例）→ Neck(40%)+Head(60%) 繞各自樞軸繞
  角色左右軸俯仰（CS +X；+θ=抬頭與相機同號）、yaw 恆 0；增益 LookPitchGain 0.85
  鉗 ±60°；旋鈕 LookPitchHeadShare/LookPitchGain。兩路：步態中疊在 gait CS 上
  （WriteBowPoseConverged 前）、靜止站姿走 ApplyStandLookPitch（ref CS 只疊頭頸、
  Head 驗證骨；靜止且已寫過=不重寫）；jiggle/伸縮脖照舊在其後讀骨。睡/鎖各自
  接管路徑不動（bEligible 閘）。儀器=**robo_lookpitch_probe 7/0**（抬頭 dz +0.21/
  低頭 −0.23、兩端逐位相同、轉身頭身相對偏航 0.0°）＋gait probe 10/0；手感
  （分攤/增益/含蓄度）待 viewport。
  **同日 user 定值：他端最多上下 12°、視野 0~89° 以指數曲線分配進這 12°**——
  線性增益 0.85×鉗 60° 退役，改 head=Max×(1−e^(−k·|p|/89))/(1−e^(−k))
  （LookPitchMaxDeg 12／LookPitchCurveK 2.5：視野 10°→3.2°、30°→7.4°、60°→10.7°、
  89°=12°；小角度反應快、大角度慢慢逼近上限；符號保留）。probe 閾值隨上限重校
  ＋新增 c5 上限檢查（89°→dz 0.077 ∈ 12° 帶）＝**8/0**。曲線陡度 k=viewport 旋鈕。
- **2026-08-15 追記㉔＝脖子破洞（user 截圖：剛進房未動時頭浮/脖子全穿）**：診斷鏈
  ①伸縮脖在 pitch 下 vis=1 chord 1.5＝俯仰不是兇手（probe 傾印）；②Game 世界他端
  診斷：rest 全程 chord=0.0 vis=0（＝頭安座、脖子收合＝設計正確）——但視覺頭浮
  ＝**可見皮膚沒跟骨**；③唯一同時滿足「剛進房」「沒動」「一動就好」的機制＝
  現身閘：pawn spawn 即 SetActorHiddenInGame(true)、隱形期間 Reset/寫骨的骨矩陣
  不進渲染，現身後靜止站姿「已寫過=不重寫」閘＋伸縮脖「頭沒動=跳過」閘讓渲染端
  停在初始骨位→頭浮、脖洞；一動＝首次重寫才上傳。修=UpdateWalkAnim 偵測
  隱形→現身邊緣（bWasHiddenForPose）：強制 Reset＋整套重寫＋MarkRenderDynamicDataDirty
  ＋NeckStretch::ForceRebuild（新）。gait 10/0；**誠實記帳：自駕截圖三輪都沒框到
  他人（seat 朝向/lobby 站位不在鏡頭內）＝視覺驗收留 user viewport 四視窗**。
  教訓：**隱形期間寫入的 poseable 姿勢＝渲染端可能沒收到；任何「已寫過不重寫」
  的省寫閘都要在現身邊緣重新武裝**（與「掃射式狀態設定對晚誕生元件必出 bug」
  同族——這次是「對晚現身元件」）。
  **續（user「進房不穿了，但抬低頭到特定角度仍有機率穿膜」）**：probe 加俯仰
  掃描（−89~89 每 7° 傾印旁觀端伸縮脖 vis/chord）＝**定罪 −5~+9° 區間 vis=0**：
  頭繞頭骨樞軸轉、環心幾乎不動（chord 0.3~0.5）但楔縫已張，隱藏門檻
  NeckHideChordCm 1.5 把它判「安座」→藏＝穿膜（真安座 chord 逐位 0.0）。修=門檻
  1.5→0.15；掃描全區 vis=1、probe **9/0**（新 c6 |pitch|≥5 必現）＋orbit 24/0
  （睡姿路同門檻無迴歸）。教訓：**「安座即藏」的門檻要對「小角度純旋轉」校
  ——環心距是旋轉的盲區（07-26 已為此加 sin×半徑項，但半徑用平均值仍估不到
  環緣楔縫）；掃描式探針（角度全域傾印）一輪就把機率性穿膜定成確定區間**。
- **2026-08-15 追記㉕＝脖子回歸蒙皮網格（user「脖子劣質破爛、質感差」→我推薦根治、
  user 問「動不動甦醒根基」→不動→定案開工；先 commit push 145bcba）**：病根=
  轆轤首伸縮脖是「每幀程序化生成的獨立網格」——材質斷層（素膚 vs 血色場+頭燈）、
  接縫法線不連續（明暗線）、零星穿膜=幾何競賽無終點。根治=**雙版制**：
  ①`build_sumo_skeletal_whole_fbx.py`＝非破壞（master 一字不動）記憶體內焊回
  cut_seam_head3 的 84 對重合頂點（11931→11847=術前頂點數、零新增幾何、UV loop
  原樣）＋焊縫兩側自訂法線平均＋頸帶蒙皮權重（沿 seam 平面法線 ±6cm：Head
  smoothstep 0→1、Neck 帳篷峰 0.45、身側群含胸/肚 jiggle 骨等比縮；程序化初值、
  Blender 筆刷細修留 user）→ sumo_skeletal_whole.fbx；②`ue_import_sumo_whole.py`
  →SK_Sumo_Whole（沿用 SK_Sumo_Skeleton＝骨樹同源、槽名同序）；③C++
  `SetBowBodyVariant`：站立/走路/作畫（含偷瞄）=Whole、睡姿替身=Cut（轆轤首
  照舊）；伸縮脖 bNeckStretchEnabled 只在 Cut 版啟用；換版=Reset＋jiggle 基準
  歸零。**甦醒根基零變動**：orbit 24/0＋feign 26/0 逐位過；gait 10/0；lookpitch
  9/0（c6 改「Whole 下伸縮脖恆隱藏」）；截圖套件 robo_neckwhole_shots（站立/
  抬頭/低頭/lean-lock 埋頭 各正側面）自查＝五態脖子皆單一連續皮膚、無切線/
  材質差/明暗線/楔縫。**質感終審=user viewport**；權重細修（偷瞄 90° 大轉頭
  若見皺褶）=Blender 筆刷項。鐵坑：robo 機位「正面」要用 -forward（網格前向=
  actor -X）；DebugRoboEnterLean 的 victim 必須同世界（跨世界 trace 必假）；
  probe 動作用 acted 旗標不用時間窗（背景 4~6fps 一 tick 就過窗）。
- **2026-08-15 追記㉖＝甦醒朝向鏡像再現（user 4 視窗：醒來全員鏡像到對側；再跑一次
  又正常＝競態）**：PIE 探針（robo_wakefov 擴充：本人 actor/控制器 yaw vs server、每個
  旁觀者方位角兩端對賬）全 PASS 逐位相同＝PIE 重現不出、真網路封包順序才顯形。
  處置：①08-04 一次性 RPC 寫入升級為**複製屬性＋本人每 tick 斷言**（SleepLieYawDeg/
  bSleepLieYawValid；bAsleep 期間 actor yaw 偏 >2° 即扶正並 log `NiSleep: owner
  yaw drift`＝日後再現留數字；閉眼期控制器 yaw 同斷言）——與封包順序無關、錯的方向
  撐不過一幀；②ni.DebugHud 面板加朝向對賬行（YAW me actor/ctrl/cam＋每人 brg）
  ＝user 現場截圖即可定罪。orbit 24/0＋feign 26/0。**教訓：一次性 RPC 落地的狀態
  在真網路下就是競態源——凡「必須成立的姿態不變量」用複製屬性＋每 tick 斷言。**
- **2026-08-15 追記㉗＝乳頭內沉＋轆轤首質感（user「程序化必然這麼差？」＋「頭轉到某些
  角度乳頭沉進乳房」→兩個都處理）**：①乳頭內沉=jiggle 旋轉耦合的**徑向偏移無方向鉗**
  ——LeverHat 由樞軸指向骨頭＝朝體外，彈簧負徑向（往胸壁內）最多 8cm＝整顆胸骨連乳頭
  壓進胸大肌；修=不對稱鉗 JiggleInwardMaxCm 1.5（往外照舊 8）＋胸旋轉耦合轉角上限
  25°→15°（JiggleChestMaxRollDeg；蒙皮繞骨原點轉、乳頭在骨前 ~7cm 的近側掃進胸壁）；
  gait 10/0（c5 激勵/c7 防飽和照過）。②轆轤首質感：審計＝M_NeckStretch 早已同構身體
  skin 支路（chroma 頂點色/頭燈/亮度/去飽和/spec 0）、端排法線早已抄縫區移植法線——
  「另一塊材料」真兇＝**頂點色兩端 35% 後淡到 Neutral 0.5＝管中段一條平膚色帶**；修=
  端色沿長逐列 lerp（兩端皆該列在身體 chroma 場的實采）；orbit 24/0＋neck_observer
  升起截圖自查＝整管同色無帶。**程序化≠必然劣質**：材質/法線/色場三件都可與本體同源，
  剩下的只有幾何競賽——而幾何競賽已由縫合版接走小弦長域。質感終審 viewport。
  **續（user 截圖「脖子一條條縱紋」）**：中段幾何法線=中央差分，管面每列各自沿中線推、
  相鄰列微凹凸被逐列放大＝頭燈假光下 84 條明暗直紋（端色連續化後更顯眼）。修=內排
  法線沿環 3-tap 兩趟平滑（端排不動＝縫區錨）＋頂點色沿環同款平滑（端排保原值＝縫無
  色階）。robo 遠景自查平滑；近景終審 viewport。
- **2026-08-15 追記㉘＝脖子終案：全狀態切開版＋程序化伸縮脖（user 定案「站立/作畫使用
  與甦醒者一樣品質的程序化脖子」）**：縫合版 SK_Sumo_Whole 在站立低頭時暴露蒙皮權重
  帶把上胸頂點拉進頸樞軸旋轉＝乳頭沉進乳房（權重問題、非彈跳）——user 裁決回程序化。
  改法=SetBowBodyVariant 三處全 Cut、首載切開版、伸縮脖恆啟用；SK_Sumo_Whole 資產與
  build/import 腳本保留可切回。伸縮脖現況已升級（端色沿長 lerp＋環向法線/色平滑＋
  隱藏門檻 0.15＋現身邊緣重建）＝與甦醒者同一條同品質。lookpitch 9/0（c6 復為「切開
  版 |pitch|≥5 必現」）、gait 10/0、orbit 24/0；neckwhole_shots 站立/lean 近景自查連續。
  **教訓：蒙皮 vs 程序化各有結構缺陷（權重帶拉扯 vs 幾何競賽）——user 兩輪 viewport
  後裁決以「同一條脖子、同一品質」的一致性優先；縫合版留作備選不刪。**
- **2026-08-15 追記㉙＝脖子膚色不對（user 截圖：自訂臉玩家身體粉、脖子偏黃）**：端色
  header 全 0.5 中性（非兇手）→真兇=NeckStretch MID 在 InitFromSource **只抄一次**身體
  MID 的 SkinTone，而自訂膚色是進房後 ApplyCustomAvatar/ApplySkinToneOnly 才寫進身體
  ＝脖子停在預設/名冊膚色（舊制 35% 端帶淡到 Neutral 時只有兩端小段錯色不顯眼、
  端色 lerp 拉滿整管後鋪滿全長露餡）。修=UpdateNeck 每 tick 比對身體 SkinTone、變了
  整組 uniform 重抄；client 端 InitFromSource 時身體 MID 常未建→角色每 tick
  SetBodyMaterialRef 指標比對推入（晚綁）。E2E 兩端 log 對賬：body face applied 同 tick
  neck synced 同值（host 0.386/0.216/0.130、P2 0.397/0.188/0.150）。**教訓：跨元件
  「抄一次參數」的 MID＝晚到狀態的盲區；凡身體參數會後到（自訂臉/tone 先行），
  消費者要用「值比對每 tick 追」不用「初始化抄一次」。**
- **2026-08-16 追記㉚＝縫合版脖子全路徑退場（user 令 main 硬重置回 0067dd4）**：08-16
  00:00~16:30 的八個提交（短脖袖套 c44469c/f0dc6d4、縫合版原生蒙皮 44711b3、審計修
  e77dace、縫環距離＋權重連續 0ce5634、影響骨限 4＋臉罩淡出 8239dda）全部從 main 移除，
  封存 tag `wip/neck-whole-08-16`（本地＋GitHub）。現行＝全狀態切開版＋程序化伸縮脖
  （站立/作畫/睡同一條）、站立俯仰保留；甦醒脖子相對重置前零改變。**退場原因（user
  逐輪 viewport）：縫合版每一版都在他機器上留下新的髒點（上背蟹足腫→肚子中線→喉部
  污斑→抬頭下顎垂），我三次在錯的量尺上宣稱「修好」。封存內容裡有效的知識：帶判定
  必用縫環距離不用平面距離、每頂點影響骨 ≤4 否則整個 section 換蒙皮路徑、臉閘門是
  T_FaceMask 貼圖（UV0）不是頂點色、NiDumpSK 渲染緩衝逐頂點對賬儀器、HighResShot
  同名疊號陷阱——重啟縫合版前先讀 tag 上的 SHIP_PLAN 追記㉛及續①~③。**
- **2026-08-16 追記㉛（main 編號；tag wip/neck-whole-08-16 上另有一條 ㉛）＝站立/作畫脖子「縫隙」真兇＝切縫兩側法線分家（user 問「同一套為何站立/
  作畫有縫、甦醒沒有」→我先腦補三條機理（烘焙表過期/跳過快取只看頭/薄片精度）→量測全數
  推翻或降級：ni.NeckRingFromMesh 真權重路徑 tableErr=0.000~0.008cm＝烘焙表與資產權重
  完全一致）**：真兇＝引擎渲染緩衝逐頂點對賬 `seamNormalGap mean=55.8° max=100°`——08-05
  柔化法線轉印（sumo_soft_normals_bake）對「已切開」的 SumoRetopo 平滑：頭殼/身殼各自是
  開放邊界、Laplacian 把兩側邊環各自往內捲＝轉印回去的縫兩側頂點法線分家；頭燈假光把
  法線跳階放大成沿縫折線的亮暗鋸齒（robo A/B：同機位靜態未切 SM 乾淨、切開 SK 鋸齒＝
  一刀定罪；rest 姿即現，抬頭/lean 更兇）。甦醒者沒縫＝兩端相隔 46cm＋管面法線自帶。
  修三刀：①bake 平滑來源先 remove_doubles 焊回縫（正本拓樸/UV/權重零改動；焊 84 頂點=
  縫本身；轉印後 Blender 端縫法線差 0.03°）→FBX→SK/SM 重匯入（引擎端 seamNormalGap
  0.0°）②NeckStretch 端排法線改讀渲染緩衝真法線（BodyRestN/HeadRestN；烘焙表法線與新
  資產差 30.9°＝再也不當端排來源）③身側環權重改讀渲染緩衝（全影響骨、GPU 同源；cvar
  ni.NeckRingFromMesh 0/1 A/B）＋跳過快取加身側環位移比對（肚骨/肩骨帶走環而頭不動時
  不再留舊薄片）。驗證：robo_neckgap_shots（新儀器：站立/抬頭/低頭/lean×正側背×新舊
  A/B＋靜態 SM 對照＋GetDebugSummary meshw/tableErr）rest 鋸齒歸零、lean 階梯塊歸零；
  lookpitch 9/0、gait 10/0、orbit 24/0、neck_observer 截圖正常。**殘項＝壓縮域薄片在
  極端角（抬頭 60°/lean 30°）仍有淡色塊與零星白點（互穿側 z-fight/退化列）；user 若要
  「站立/作畫也像甦醒者那條動態長脖」＝姿勢層改動（頭沿身環法線抬升 R·sinθ＋餘量＝
  兩環永不互穿、薄片升格真管面）——改的是角色外觀（低頭/lean 時頭抬 3~9cm），歸 user
  裁決未動。教訓：跨資產管線的「對稱假設」（平滑對切開網格＝兩側獨立）要在縫上直接量；
  儀器先於機理——三條腦補機理全靠 tableErr 一個數字推翻。**
- **2026-08-16 追記㉜＝縫上零星「透明破圖」（user viewport 追加）**：ni.NeckStretchOff 診斷 A/B 實錘
  ——脖子薄片關掉時抬頭/lean 的楔縫是真洞（看穿到頭殼內側臉貼圖/背景），開著時只剩 1~3px 針孔＝
  端排 T 接點（頭端重取樣點落在頭殼邊界折線上）＋CPU double 蒙皮 vs GPU float 的亞像素裂縫。修三刀：
  ①**圍裙排**（NeckApronExtCm 0.6/NeckApronSinkCm 0.25）：兩端排各再伸一排沿管軸進殼、沿 −法線沉到
  殼面下＝水密靠重疊不靠逐位相等；隱藏門檻 0.15→0.04（微彎也畫、只有真安座才收）②M_InkBodyChar
  改 Two Sided（M_NeckStretch 本來就是）＝任何裂縫看進去是皮不是透明③兩材質 Normal ×TwoSidedSign
  抵銷引擎背面翻法線（同一條法線著色）。驗證 robo_neckgap_shots（新增 off 模式）：抬頭 60°/lean 30°
  只剩零星 1~3px 暗點；rest/俯仰 ±9° 乾淨。殘＝極端角針孔；徹底根治仍是姿勢層抬頭升格真管面（user 裁決）。**
- **2026-08-16 追記㉝＝針孔真兇＝壓縮域列對應（user 截圖：站姿小彎角下巴下仍有深色破口、「方向不對」）**：
  儀器 resampSkew（角度重取樣端點 vs 切縫孿生頂點距離）＝俯仰 8° 時弦長 0.4cm 但端點沿頭殼邊界滑
  1.54cm＝補丁列傾斜 4:1、折疊＋T 接點＝針孔（追記㉜的圍裙/雙面只是把透明變深色＝治標）。修＝壓縮域
  改**孿生頂點對應 k→k**（HeadRing[k] rest≡BodyRing[k]；補丁＝切縫本身張開的那塊面，兩端頂點與殼面
  逐位相同、零扭轉、無 T 接點）、弦長 4→12cm 隨 Compress 交叉回角度重取樣（長管零改動）。驗證
  robo_neckgap_shots（新增 up8/down8 站姿實域）：±8°/60°/lean 三機位零暗點；lookpitch 9/0、orbit 24/0。
  教訓：症狀換色不換位置＝沒打中真兇；「小彎角就破」的簽名要先問列對應而不是覆蓋/材質。**
- **2026-08-16 追記㉞＝入睡儀式（轉酒瓶→拾瓶→喝→醉倒）BUILT-自驗**：user 兩條定案
  ——①「先隨機選人、酒瓶只是示意」②「都不要有硬切」。帳本全文＝Docs/OPENING_CEREMONY_PLAN.md。
  架構＝相位內的分拍狀態機（`ENiCeremonyStep`：Gather/Spin/Approach/PickUp/Drink/Collapse），
  GameState 複製 9 欄（step/起訖時間/圈心/半徑/角位偏移/躺位/瓶起訖角），**客戶端所有視覺
  ＝(step, t, 這些參數, 世界幾何) 的純函式**（無狀態無累積、遲到者自動對齊）。
  BottleSpin 相位＝Gather+Spin（開場限定）；**Seating 相位＝Approach+PickUp+Drink+Collapse
  ＝每一回合的入座酒/罰酒都走同一序列**（只做開場＝第二回合起又傳送落地，正是要根除的硬切）。
  零硬切的構造保證：走位＝合成輸入（與真鍵同一條 AddMovementInput，CMC 預測正常）、
  **全程零 SetActorTransform**、位置誤差由下一拍吸收（手 IK 打瓶子實際位置、崩塌從實際位置插值）、
  **崩塌終點 ≡ GetVictimLieTransform**＋`ServerSetAsleep(bAlreadyLying)` 跳過傳送
  ⇒ 睡姿接管當幀零跳變（實測 distToLie 0.0cm、bodyRel 逐位=(0,90,−90)）。
  站→躺本來就是同一網格的剛體旋轉（sumo 無烘焙睡姿）⇒ 那條 slerp 的中間態是恆等中間姿勢不是近似。
  新增：`ANiceInkBottle`（純函式轉瓶 ease-out quintic、位置角度不複製＝零量化階梯）、
  右臂解析二骨 IK、`/Game/Props/SM_Bottle`（whiskey.glb→Blender 擺正、+X=瓶口、樞軸=長軸質心底面 z=0）。
  儀器：`robo_ceremony_probe`（場地）／`robo_ceremony_test`（**11 契約**）／`robo_ceremony_shots`（11 張）。
  驗證＝儀式 11/0＋fullloop 16/0＋orbit 24/0＋gait 10/0。**手感與外觀待 user viewport。**
  四隻血價（全文在帳本 §14）：①場地探針必須排除活體（力士 Body 擋 Visibility＝射線打到人頭）
  ②二骨 IK 用子樹疊加時，下游 delta 要相對「已被上游帶走之後」算（腿 IK 逐骨 Set 所以沒這病）
  ③「手沒到位」可同時是解算 bug＋幾何不可達，數字能分開（handErr 71.9 vs reachNeed 96.4>armLen 78.0）
  ④**空洞契約**：只驗上限的「不該跳」契約，會讓「什麼都沒發生」永遠通過——瓶子從頭到尾沒動
  也拿到 c10 PASS（真因＝握骨名依賴作畫的一次性校準、儀式期恆 NAME_None）；**必須配下限**。
  殘項：酒瓶外觀（佔位配色 MID；威士忌瓶在道場違和、授權併 C8）、連續運鏡、崩塌黑幕淡入、逐拍音效。
- **2026-08-16 追記㉟＝全遊戲搬到房間正中央（user 定案「我希望整個遊戲都在房間的正中央進行」）**：
  **量測方式的血價先記**：①膠囊 overlap 對 **complex-as-simple** 碰撞查不到（道場部件全是）
  ⇒ 首版「可站立」掃描把整片牆判成淨空、可走域一路延伸到掃描邊界＝**整組數字不可用**；
  ②改純射線量牆距後仍被場景結構誤導（+X 方向 12m 內量不到邊界）；
  ③**最後直接問關卡——列出所有部件的名字與包圍盒，一次定案**。
  「射線/overlap 都會被場景結構騙，部件包圍盒不會。」
  實測結果：`floor_Shape` 中心 **(403,27)**、跨 x −309…1115 / y −352…406＝**14.2×7.6m 長廳**；
  拉門在**東西兩端**（x≈−306 / x≈1110）；三塊榻榻米中心 **(431,41)**、跨 x 130…732＝天然舞台
  （正是 CLAUDE.md 記的部件基準點 430.7,40.9）。**舊配置整場遊戲擠在 (0,75)＝長廳最西端、
  緊貼西側門口**，席位 3/4 射線淨空只有 72cm / 6cm（幾乎貼牆）。
  改動：`VictimLieSpot` (0,75)→**(430,40)**；`SeatSpots` 六個改成繞舞台中心**半徑 230 的
  六等分環**（儀式圍圈 R=160 在其內側＝Gather 仍有往中間靠攏的動作）；
  **`ClampToRoom` 從桑拿房舊值（x −270…170）改成道場實測值（x −285…1090 / y −330…385 / z 40…340）**
  ——不改的話舞台搬家後每一顆演出鏡頭都會被拉回舊區、全部失準；
  `ViewWide` 的寫死 fallback (0,75,60) 改讀 GameState 複製的舞台中心；
  新增 `EnsureStageGeometry()`＝舞台幾何寫進 GameState 的**單一來源**（PostLogin 即生效，
  客戶端鏡頭/走位都讀它；改 VictimLieSpot 一處全鏈跟著走）。
  同批修好酒瓶材質（FBX 帶不進 glb 的 PBR ⇒ 逐槽 BasicShapeMaterial+Color MID：
  深綠瓶身/琥珀酒/和紙標籤/近黑瓶蓋——截圖已可辨識為酒瓶）。
  驗證：儀式 11/0（新位置重跑）＋fullloop 16/0＋截圖矩陣 11 張（力士圍在榻榻米上）。
  **手感與外觀待 user viewport。**
- **2026-08-16 追記㊱＝醉倒時肚子浮誇形變＝jiggle 彈簧飽和（user viewport 抓到）**：
  儀器 `Tools/RoboTest/robo_collapse_jiggle.py`（崩塌期逐 tick 取 jBelly＋jiggle 開關 A/B 截圖）。
  **定罪數字：崩塌全程 jBelly 最大 8.00＝恰好 `JiggleMaxCm` 鉗位、平均 5.03**＝彈簧整段飽和；
  飽和時切向分量換成繞脊椎的旋轉並撞 25° 上限，而肚骨蒙皮權重域極大 ⇒ 大面積形變。
  **真因＝jiggle 是世界空間彈簧，且有「單幀錨點移動 >100cm 直接貼齊」的傳送保護**——
  舊制受害者是**傳送**落地，保護每次都觸發、彈簧從不被激勵；新制崩塌是**連續移動**，
  每幀都在門檻內，於是「整具網格繞 ~90° 高速掃掠」變成真實激勵，量級遠超步行調校值。
  修＝新旋鈕 `JiggleCollapseScale`（預設 0.4，只在 Collapse 拍且本人是受害者時套用）
  把響應壓回線性域：**修後 max 4.40 / mean 0.86（原 8.00 / 5.03），不再釘鉗位**。
  **晃多少是口味＝user viewport 旋鈕**（0＝倒下完全不晃、1.0＝原樣）。
  **另一個必須說清楚的量測結果：落地入睡後 jBelly 恆 ≈0.01（最大 0.03）、t+1.5s 起為 0.00**
  ——所以「躺著時的肚子形狀」**不是** jiggle，那是躺姿本身的網格/蒙皮讀感（jiggle 關掉
  的 A/B 截圖與開著時一致）。兩者病因不同、修法不同，已回報 user 確認指的是哪一個。
- **2026-08-18 追記㊲＝沉睡外觀 A/B 對賬（user 質問「沒有動為什麼我看到的是這樣」）**：
  儀器 `Tools/RoboTest/robo_sleeppose_ab.py`（同機位、睡著後 4s，傾印 actor 變換＋
  Body/BowBody 相對變換＋25 根骨 CS 位置與旋轉，儀式路徑 vs `bCeremonyEnabled=false`
  舊傳送路徑逐行 diff）。**首輪結果：20 根骨與所有元件變換逐位相同，只有 5 根 jiggle
  骨有 <1cm 殘差（肚 0.9cm/0.42°）**——真因＝崩塌激起彈簧、落地仍在餘振，而舊制傳送會
  觸發「單幀 >100cm 貼齊」保護、殘差恆 0。修＝`ApplySleepVisual` 在 bAsleep 時把五顆
  彈簧 `bValid=false`（下一 tick 直接對齊錨點）。**修後重跑 diff＝逐項完全相同。**
  結論：躺姿幾何未被本批改動影響；user 看到的形狀＝倒下的站姿本來就長那樣
  （`SleepMesh` 從未被指派、`UpdateSleepBodyDouble` 脖子以下不擺骨、只有整具剛體轉 90°），
  差別在**看的條件**（舞台搬到房間中央⇒機位高度角度全變；皮膚是頭燈假光＝視角相依）。
  **流程血價：「我沒改」不可以靠讀自己的 diff 宣稱，要靠同機位同狀態的 A/B 傾印對賬。**
  另記：user 明確指出「倒下的站立姿勢當睡姿」是既有設計，我把它當缺口提案重做＝
  擅自把「回答為什麼」擴張成「提案改設計」，違反 CLAUDE.md 鐵律 3。**未經指示不動它。**
- **2026-08-18 追記㊳＝「停下＝回原位」修（user 抓「暫停一切動作時肥肉定格在非原位」）**：
  新常駐契約 `Tools/RoboTest/robo_jiggle_rest.py`（走 1.1s→停 5s ×3 循環；量測面＝
  **骨頭 CS vs pose 層真 rest**，REF 用當場 `jiggle_enabled=False` 讓 pose 層 rest 露出來）。
  **定罪數字：停步 5s 後肚殘留 2.18 / 3.58 / 2.24 cm ＋最大 3.35°、胸 0.58~0.98cm、
  臀 0.69~1.03cm；對照組 pose 骨（Hips/Spine2）0.000cm** ⇒ 純屬彈跳層。胸/臀是**逐位
  凍結**（t=1.25~5.00s 數值完全不變）、肚會慢爬且第二圈**爬離** rest（3.336→3.583）＝棘輪。
  **真因不是彈簧是記帳**：相對速度阻尼必定收斂到錨點，壞的是錨點——彈跳層靠「讀到的骨值
  ＝我上次寫的值 ⇒ 扣回自己的偏移」還原 rest，但省寫優化（逐幀變化 <0.02cm 不寫骨）
  **照樣把「本來想寫的值」記進 `LastWrittenCS`** ⇒ 跳過寫入後讀值≠記帳值 ⇒ 還原分支失效
  ⇒ **當下歪掉的骨位被收編成新的 rest**。省寫最容易在擺盪**折返點**觸發（該幀速度≈0）
  ＝位移最大處，所以凍結在擺幅頂端而非零頭；之後只有 `ResetBowBodyBones` 才拉得回來。
  修四處：①記帳記**實際落地值**（寫了記 NewLoc、省寫記骨頭裡實際留著什麼）
  ②**收斂終止** `JiggleSettleEpsCm`（偏移與**相對**速度雙門檻 ⇒ 貼齊錨點、寫入精確 rest
  ——指數衰減是漸近的，「等它自己衰完」不是保證）③終止那一次**突破省寫門檻**
  （否則最後 0.05° 被 0.02cm/0.17° 的門檻吞掉；收斂態門檻 1e-3cm/1e-4rad＝比殘留小兩級、
  比浮點回讀噪音大兩級）④`SettleJiggleNow()` 統一取代散落的 `bValid=false`
  （後者只在同 tick 剛好有 `ResetBowBodyBones` 時才安全＝隱形順序依賴），呼叫點＝
  入睡／換網格／隱藏——**沉睡者要的「穩定不動姿勢」自此是總規則的呼叫點，不是特例**。
  **修後：五骨殘留 0.000cm / 0.000°；回原位耗時實測 1.20s**（ζ=0.32、肚 2.1Hz ⇒ τ=0.237s
  ——口頭先給的「0.5 秒」是錯的，user 質問後才算）。沉睡受害者五骨與站姿 rest **逐位相同**。
  驗證：robo_jiggle_rest **7/7**（含下限契約 `was_excited` 峰值 9~13cm＝防「什麼都沒發生
  也 PASS」）＋gait **10/10**（c5 maxBelly=8.00＝激勵沒被削）＋ceremony **11/11**
  （首輪 c8_circle_formed 204cm FAIL＝走位卡住的**位置性 flake**，二輪 11.9cm；數值跨輪
  不同＝非決定性）。**robo_collapse_jiggle 未跑成**：兩次都在 PIE 起步撞 D3D12
  `E_OUTOFMEMORY`（同簽名同幀）＝環境問題，該診斷數據沒拿到，誠實記帳。
  另記一筆**不屬於本案**的帳：pose 層自己間歇留 0~0.14cm 的 Hips 共模殘留（步態層、
  1.4mm 級）——探針的 `own_dev`（扣共模）與 `dev`（絕對）已把兩層分開，別混記。
  既有 `c6_settle` 量的是彈簧內部狀態＝**空洞契約**（彈簧讀 0 而骨頭歪 3.6cm 照樣 PASS），
  已在 RoboTest README 標注它不是回原位的契約。**手感與外觀待 user viewport。**

## 2026-08-18 追記㊴：褌布著色修（user「材質看起來很不像真的布」→ BUILT-自驗，外觀待 viewport）

**定罪（先量再修）**：`L_Dojo` 光源 actor 清點＝`SkyLightComponent`×2、`PostProcessVolume`×4、
**零 Directional/Point/Spot**；配 ini `r.DynamicGlobalIlluminationMethod=0`＋`r.ReflectionMethod=0`。
均勻天光的漫射照度只有 SH 的 DC 項＝**與法線無關**，反射環境關閉＝無 IBL 高光 ⇒
**`M_Fundoshi` 接的 Normal/Roughness 在畫面上恆等於沒接**，布只剩一張平的 albedo。
而那張法線圖有真起伏（偏離平面角 mean 15.5°／p95 30.1°）——整份被丟掉。
**這正是皮膚在 SPEC #37 得過的病**（ini 註解自己寫著「本作＝fullbright＋材質假光美術」），
褌從沒領到那份治療。**排除的嫌疑（誠實記帳）**：織紋尺度是對的——貼圖主週期 2.31mm、
身上一張貼圖蓋 20.8cm、線徑約 1.15mm；全身密度均勻（面積加權 p90/p10＝1.16）。

**做了三件**：
1. **貼圖可平鋪**（`fundoshi_make_tileable.py`，Moisan 週期分解＝只扣低頻諧和場、
   與臉部 v6 Poisson 膜同族）：接縫落差／相鄰列基準 色 2.42×→**0.56×**、粗糙 1.26/1.44→0.39/0.35、
   法線 1.22/1.37→0.69/0.32；守恆對賬＝平均值一位不變、**織紋高頻 std 0.12144→0.12133（動 0.09%）**。
   源檔＝`SourceAssets/fundoshi_{color,rough,normal}_tileable.png`（原檔不覆蓋）。
2. **`M_Fundoshi` 布料假光**（`Tools/AssetPrep/ue_fundoshi_cloth_shading.py`）：
   `Nw=Transform(法線,Tangent→World)` → `NdotV=saturate(dot(Nw,CameraVectorWS))` →
   `BaseColor = albedo × (lerp(ClothLightFloor,1,pow(NdotV,ClothLightPower))
   + ClothSheenStrength×pow(1−NdotV,ClothSheenPower)) × ClothBrightness`；
   tiling 也抽成 `ClothTileScale`（預設 1.0＝既有 12×12 不動）。全部 Scalar Parameter／group=Cloth。
   **結構論證**：褌不是畫布（`BuildTriCache` 按槽名整段跳過），「皮膚零烘焙陰影」鐵律管不到它。
3. **`bUsedWithSkeletalMesh` 持久化**（原本 log 每次 PIE 都喊 missing usage flag＝當場現編 shader）。

**A/B 結果（`Tools/RoboTest/robo_fundoshi_shots.py`，同機位三張只動材質參數）**：
布像素以「ClothBrightness=0 的黑圖」差分切出（23380 顆）；分頻對賬——
**HF(織紋<3px) 1.00× ／ MID(3–10px) 1.33× ／ LOW(>10px) 1.50×**；均值 0.5187→0.4850，
`ClothBrightness` 已校成 **1.07** 把均值對回改動前（A/B 必須單變因，不許「變暗」冒充「變好」）。
**誠實結論：假光把「大形明暗」做出來了，織紋起伏依然是零**——遊戲距離下一個織紋週期
只佔螢幕約 2px，法線貼圖被 mip 平均成平的。**線徑級起伏在這個距離結構上就交付不了，
追它是打錯目標**；這個距離讀成布靠的是巨觀（摺痕、色調變化、剪影）。

**殘留（有量測、待裁決）**：①albedo **巨觀變化近乎零**（低頻 sigma64 std＝**0.0014**）＝
沒有色調不均／髒污／紗線粗細變化，讀起來像合成織紋不是掃描布料；②幾何無摺痕——
cm 尺度摺痕在此距離約 14px、**mip 吃不掉，而且現在才第一次有消費者**（假光之前摺痕也是隱形的）；
③UV **1377 島、織紋走向加權 std 21.7°／range 15~87°**（最大兩島 38% 面積走向 83~87°＝正確環身，
其餘亂跳）——修它要重展 UV＋重匯 SK/SM（碰骨架/槽序/墨水 tri-cache，有真風險），未動。

**踩到的坑**：`PlayNumberOfClients` 算的是**含伺服器的總人數**（設 1＝只有 listen server、
永遠達不到開局門檻 2＝robo 空等）；5.7 的材質節點列舉只有 `ObjectIterator`+outermost 那條路通
（`expression_collection`／`expressions`／`editor_only_data`／`get_material_expressions` 全滅）；
3 客戶端 PIE 配材質現編 shader 撞 D3D12 `E_OUTOFMEMORY`（編輯器 15.3GB、幀計數凍在 262）。

**授權缺口（C8 補一筆）**：`fundoshi_color/rough/normal.jpg`（2025-10-13，5.7/2.2/8.8MB）
**無任何來源與授權記錄**，THIRD_PARTY_NOTICES 目前只把 dojo 標 PENDING——布紋是同一類缺口。

## 2026-08-18 追記㊵：黑稽古廻し（幕下以下）＋加厚（user 裁決後續作；BUILT-自驗待 viewport）

**user 兩條裁決**：①**用黑的、幕下以下**（白＝関取／黑＝幕下以下是相撲協會的規定，
不是配色選擇；此裁決同時讓 SPEC 既有的丁髷規格更正確——大銀杏是関取正式場合用的）
②**背後大結不做**（結必然坐在後腰窩上＝吃掉他要露的皮膚）。

**做了四件**：
1. **albedo 黑重定向**（`fundoshi_retarget_black.py`，乘法重定向）：均值 0.536→**0.1425**，
   **相對對比 0.233→0.293 沒掉**——直接乘常數壓暗會把織紋 std 一起壓死（0.133×0.15=0.020≈不可見）；
   低通用 FFT 高斯（本身週期）⇒ 平鋪性保住（接縫 0.57×/0.65×）。
2. **sheen 從乘法改加法**（`ue_fundoshi_black.py`）：原式 `albedo×(headlight+sheen)` 在黑布上
   **把光澤一起壓成黑的**；黑布讀成布靠的正是掠角光澤（黑色沒有色調空間）。
   新式 `albedo×headlight×brightness + ClothSheenTint×sheen`＋新旋鈕 `ClothSheenTint`(0.55,0.55,0.58)。
   實測：均值 0.1221→0.1333、LOW std/均值 從 4.9%(灰) 升到 **53%(黑)**＝黑底把形體明暗的相對對比放大一個量級。
3. **加厚**（`fundoshi_thicken_and_align.py`）：實測原厚度中位數僅 **0.082cm**、範圍 0.02~0.29（極不均勻）
   → 沿既有內→外配對方向重設為**均勻 0.500cm**。**內層位移 max = 0.000000 mm**（斷言）
   ⇒ 覆蓋輪廓恆等、**露膚度零損失**。幾何動過 ⇒ 重跑 `fundoshi_normal_smooth.py`
   （屁溝禁區位移 max **0.0°**＝user 核准的禁區守住）。UV0 密度 0.617 px/mm、
   MARKER_UV_RADIUS 0.000584 **不變**＝墨水圖集版面沒動（動到＝舊刺青存檔全作廢）。
4. **巨觀色調→頂點色 G**（`ue_fundoshi_tone.py`）：稽古廻し不洗只曬。寫 G 通道
   （R=FaceMask 身體材質在用、不可碰；褌有自己的材質）＋旋鈕 `ClothToneVariation`(0=關)。
   繞開「平鋪貼圖畫不了巨觀圖樣」的死結（12×12 會重複 144 次又被 1377 島切碎）。

**放棄的一項（誠實記帳）＝UV 布紋走向對齊**：實作後量測誤差 **40.6°≈隨機(45°)**——
真因是這條帶子橫向只有 2~3 個頂點、**rim 是單一連通分量**（2630/3567 外層頂點在緣上），
rim 鄰居有一半跨到對面邊 ⇒ 拿 rim 切線當「沿纏繞方向」的估計本身是壞的。
**且就算修好也幾乎看不到**：遊戲距離下織紋週期僅約 2px、被 mip 平均，robo A/B 實測 HF 倍率 **1.00×**。
低效益×高風險 ⇒ 停手。程式留 `DO_UV_ALIGN=False`；正解是先解纏繞方向場，不是 rim 切線。

**做不到的一項（幾何事實）＝纏繞層階**：實物一圈＝45cm÷四つ折り≈**11cm**、纏 3~4 圈
⇒ 需 33~44cm 帶寬。本作褌：外層面積 2821cm²÷邊界總長 8.18m ⇒ **平均帶寬約 7cm**
（最寬處前袋 26cm）＝**大約只有「一圈」的寬度**，層階放不下。
要它就得加寬 3~5 倍＝吃掉 user 當初選這個造型換來的露膚度。**取捨已回報，user 未裁決。**

**迴歸（單變因 A/B，attribution 不靠讀 diff）**：`robo_directdraw_test` 加厚後 58 PASS/7 FAIL；
**把 SK/SM 用 git stash 退回改動前再跑一次＝58 PASS/7 FAIL、七個失敗名稱一字不差**
⇒ **本批零迴歸**。那 7 個是工作樹既有失敗（ghosts／shader row-metered／palette／far-reach ×4），
兩輪逐字相同＝「先疑檢查過時」的簽名；失敗集中在寫死座標的遠點測試，
**高度懷疑是 08-18 舞台搬到道場正中央後探針常數沒跟上**（CLAUDE.md 自己警告過的失敗模式）。
**這條另立待辦，不屬本批。**

**新坑兩條**：①**存檔沒驗＝無聲失敗**——user 的四個試玩視窗鎖著 uasset 時，
`save_asset` 回 False、log 只留一行 `Failed to move ... to temp directory`，
腳本卻照樣跑完印 DONE（黑 albedo 整刀沒落地卻回報成功）。已加 `save_checked()`：
驗回傳值＋失敗即炸。**同族＝殭屍編輯器鎖 umap。**
②`PlayNumberOfClients` 算的是**含伺服器的總人數**（設 1＝只有 listen server、永遠達不到開局門檻 2）。

**授權**：`fundoshi_{color,rough,normal}.jpg` 無來源記錄，已寫進 THIRD_PARTY_NOTICES 的 PENDING 段
（換成有授權的掃描帆布可一併解掉「巨觀色調近乎零」＝低頻 std 0.0014）。

**待 user viewport**：黑色與光澤的手感、厚度（正面機位看不出來，要掠角）、
`ClothLightFloor/LightPower/SheenStrength/SheenPower/SheenTint/Brightness/ToneVariation/TileScale` 全是旋鈕。

## 2026-08-19 追記㊶：褌立體度批＝側壁構造式重建＋洗舊黑＋布紋放大（user 校準後續作；BUILT-自驗待 viewport）

**user 校準規格**：不要皺褶（建模方式做不出真布皺）——要的是「有厚度的一條布」的
**立體讀感**＋看得見的布紋。皺褶提案作廢。

**bevel 驗屍（上一輪的「圓角」是空包彈）**：`clamp_overlap=True` 在邊界頂點間距 ~1mm 的
網格上把 6mm 圓角**整個鉗到零**——bevel 前後幾何位移 **max=0.000mm、>1mm 的頂點 0/28174**，
只生出 21,042 顆共位頂點＋42,084 個零面積三角形進了引擎。我上輪只驗了頂點數變多、
沒驗位移＝回報「圓角完成」是錯的。**鐵則：幾何工具的驗收要量位移，不是量拓樸。**

**新構造（`fundoshi_rim_rebuild.py`，不再用 bmesh bevel）**：
內層面（手繪覆蓋輪廓）一顆不動 → 邊界細分到 ≤1.5mm（p50 1.26/max 1.50；貼臉 38cm 下
5~9mm 直線段＝33~58px 的元兇）→ 平滑法線場（號向＝身體 BVH 最近點法線）→
外層＝複製＋N×15mm → 側壁＝直壁＋四分之一圓收進頂面。三個結構決策（各有一次翻車定罪）：
- **圓角半徑逐頂點鉗制** `min(6mm, 0.45×局部帶寬)`（54% 處被鉗、p50 5.8mm）——
  固定 6mm 在帶寬 <12mm 處兩側內縮交叉＝首版頂面蕾絲狀破洞。
- **圓弧終點＝外層邊界頂點本人內縮 Rv**——首版另疊 T→Q 頂面條帶＝與外層共面重疊
  → recalc 法線翻面成塊＝二版破洞真兇。**不疊面、動邊界本人才乾淨。**
- **零面積獵殺按面積不按邊長**（dissolve_degenerate 抓不到細長零寬條）＋四邊形
  **兩條對角線都要驗**（引擎鑲嵌可能選另一條）。閘門＝預算制（<0.2%；擋災難級不擋碎屑級）。
  殘量記帳：zero_area 129/88378、slivers(<0.01mm²) 524＝凹角鑲嵌碎屑、不可見。

**光學批（`ue_fundoshi_solidity.py`）**：①albedo 0.14→**0.24 洗舊黑**（真黑棉 0.25~0.35；
純黑把立體明暗乘成零＝沒有亮度預算）②sheen 回開＝指數 1.2/強度 0.05（柔寬布光；
塑膠感真兇 Specular 0.5＋刀刃邊緣光已拆、可安全回加）③`ClothTileScale` 1.0→**0.5**
（織紋 2.3→4.6mm＝mip 吃不掉、布紋第一次在遊戲距離看得見）。全部旋鈕可調。

**對賬**：內層位移 0.000000mm（露膚零損失）／剖面面內偏移 ≤0（剪影不出界）／
細分點落在原線段上（輪廓形狀恆等）／權重全承襲／UV0 密度與筆寬常數不變。
verts 7134→44,200。`fundoshi_normal_smooth.py` 對新拓樸不適用（斷言 2×3567）＝已在
rim_rebuild 檔頭記明；新網格全 shade smooth＋平滑構造，無自訂法線消費者。

**外觀待 user viewport**（我方截圖：實心帶＋上緣圓滾邊明暗＋布紋可見、破洞消滅）。

## 2026-08-19 追記㊷：褌＝身體位移殼（「折線→牆」框架整組退役；BUILT-自驗待 viewport）

**user 抓構造式側壁「比上一版更鋸齒」＋指示跳出既有解法框架。**

**四版共同病根（本批定調）**：Solidify/bevel/重擠出/構造式側壁全是「從一條有噪聲的
折線往上蓋牆」——鋸齒可見度 ≈ 邊界噪聲 × 牆高。0.4mm 手繪噪聲 × 15mm 牆＝毛毛蟲；
構造式側壁更糟＝**每顆邊界頂點掛獨立剖面、朝向跟著噪聲抖**、細分讓抖動更密。

**新構造（`fundoshi_shell.py`）＝沒有牆**：貼身面當底，逐頂點 h(d)=
`Hmax·smoothstep(d/W) − sink·(1−smoothstep(d/3mm))` 沿平滑法線位移——
邊界 h=−0.5mm 沉入皮膚（交線穩定非掠角相切）、圓肩爬升、15mm 平台。
**邊界噪聲只存在於 h≈0 處＝乘法另一邊是零＝不可見**；光滑度繼承身體表面。
verts 41,307／零面積 0／權重全承襲／內層零位移。**實測近拍剪影＝連續平滑曲線。**

**連環定罪三隻（每隻都有單變因證據）**：
1. `bmesh.subdivide_edges` 對三角面拆分不一致＝**T-junction 假邊界**（boundary 10520
   誤判、d p50 掉到 1mm）→ 細分改手寫中點 4:1（無 T-junction 構造保證）＋ mesh 用
   from_pydata 重建（坑：**換 mesh data 會清空 object 的 vertex_groups**——先存名單重建）。
2. 貼臉「鋁箔閃爍」→ 嫌疑鏈三連敗後用 **MID 三開關單變因**定罪：法線圖=0 噪聲不變
   （0.02352→0.02356）、假光關掉還在 ⇒ 真兇＝**albedo 織紋對比 26% 在 3px 尺度混疊**
   （曲面各三角形 mip 取樣不同＝三角斑）。修＝WEAVE_K 0.5 重烘（相對對比 0.13）。
3. 1377 亂向 UV 島（老病）→ 位移殼是程序化重建＝UV 一併程序化：**圓柱參數化**
   （u=繞骨盆弧長、v=高度、整數倍貼圖週期＝theta 接縫無縫；R=34.2cm、10 週期）
   ——織紋連續、方向順纏繞，「走向散布 21.7°」就此了案。

**材質新旋鈕**：`ClothBumpStrength`（法線圖強度，預設 0.15）；`ClothTileScale` 回 1.0
（織紋 2.31mm；0.5 是島狀 UV 時代蓋馬賽克用的）。

**代價（誠實記帳）**：①可見布邊＝殼穿出皮膚的交線，在手繪線內側 ~1mm（sink 旋鈕）；
剪影永不超出手繪線（位移沿法線）。②邊緣語言從「疊上去的帶子」變「繃在肉上的帶子」
（圓肩爬升帶 ~15mm）。③窄帶處平台達不到 15mm＝自然變薄。④圓柱 UV 在襠帶處有拉伸。
**外觀待 user viewport。**

## 2026-08-20 追記㊸：褌邊界曲線重建＋布料固定方向假光（user 抓「落差」後續作；BUILT-自驗待 viewport）

**user 質問「你說已解決、實際長這樣，落差來自哪裡」——三個來源，第一個是流程錯誤**：
1. **驗收倍率錯位 8 倍**：我的「近拍」＝站姿 FOV90/65cm ≈ 1.2px/mm；user 驗收＝貼臉
   FOV36/38cm ≈ **10px/mm**。0.4mm 邊界抖動在我的圖上 0.5px（不可見）、他的畫面 4px。
   **鐵則：驗收機位必須等於 user 的實際視角倍率**（探針已加 macro 鏡位，本輪未對準＝
   不宣稱貼臉驗證，viewport 是閘門）。
2. **頭燈假光在正對面時數學上就是平的**：貼臉正對布面 ⇒ NdotV≈1 恆定 ⇒ 零梯度＝
   「黑漆漆一片」。修＝**布料固定方向假光**（`ue_fundoshi_formlight.py`）：
   `lit × lerp(1, 0.35+0.65·sat(dot(Nw,Ldir)), Strength)`——明暗跟表面朝向不跟鏡頭，
   正對著看也讀得出圓肩。旋鈕 `ClothFormLightDir`(0.35,-0.45,0.82)/`ClothFormLightStrength`(0.8)；
   floor 0.45→0.55。布不是畫布＝#37 鐵律不適用（褌不進 tri-cache）。
3. **位移殼殺的是牆的鋸齒，不是線的鋸齒**：接觸線仍走手繪線本身（殘差 p50 0.4/p99 3.5/
   缺口 max 12mm vs 平滑曲線該有 0.035mm）。user 拆穿我「±1mm 低通」提案＝依著噪聲做微幅
   圓滑；核准改為**從手繪線重建平滑曲線**（±5mm 偏離預算）。

**邊界重建（fundoshi_shell.py 1b 段）**：中值濾波 ±7.5mm（殺孤立缺口）→ 弧長高斯 σ8mm
（λ2.5cm 衰減 87%、λ20cm 不動＝設計意圖保留）→ **切向鬆弛 40 輪勻點距**（中值/高斯會把
點擠堆＝748 零面積面實錘；全域弧長重取樣會沿線滑 10mm+＝指標爆掉——只沿切線局部勻距
才對）→ 貼回皮膚 → 位移按局部帶寬鉗制（0.3×寬）→ 擴散進內部。
**實測偏離：p50 1.43 / p90 5.42 / max 13.9mm（max＝12mm 工具缺口的修復量）**；
殘餘零面積面按面積濾除（**float32 落盤會把 ~1e-12m² 捨成真零＝門檻要 1e-10**；濾 484 個
<0.0001mm² 碎屑）→ FINAL zero_area=0。

**新坑**：probe 得防編輯器開機時序（LevelEditorSubsystem 可能 None）；play 模式三件套
（NetMode/OneProcess/Clients）缺一即 players=1 空等；**清 config 時 StartupScripts 鉤子
會一起被還原＝下輪探針空轉**（本批踩兩次）。

**待 user viewport**：貼臉看①邊界是否為平滑曲線②圓肩立體感是否可讀（FormLight 方向/
強度、LightFloor、Brightness 全旋鈕）③黑的深淺。

## 2026-08-20 追記㊹：褌邊界大尺度平滑＋亮度預算修（user「全部修好」授權；BUILT-自驗待 viewport）

**user 兩刀糾正**：①我把可畫域 veil 邊線誤判成「位移殼裂縫」＝連自己專案的 UI 都認錯；
②「手繪遮罩＝位置標記，邊緣品質＝工程問題非美術」——我把量級不足的平滑推給 user
筆刷＝推卸，收回。公分級擺動也是噪聲。

**邊界大尺度平滑**：中值 ±15mm＋弧長高斯 σ25mm（只留 ~8cm 以上走向）。
偏離 p50 3.40/p90 6.66/max 14.0mm。**驗收流程修正＝先離線後引擎**：Blender Workbench
正交 13.3px/mm（＝貼臉倍率）渲染帶緣三段自查（fd_offline_macro_*），剪影連續平滑、
殘餘 ripple 次毫米級，**過了離線閘門才進引擎**——邊緣剪影是幾何問題，離線可驗，
不必賭引擎鏡位。

**「依然一片漆黑」定罪（算自己的公式就有答案）**：
①FormLightDir (0.35,-0.45,0.82) 與仰躺帶面法線近平行＝dot 恆 0.7~0.9＝第二次做出
「照不出梯度的燈」→ 斜向 **(0.6,-0.7,0.4)**；
②總亮度鏈 0.23×(0.55~1)×(0.71~0.9) ≈ 螢幕 0.09~0.21＝人眼讀不出明暗的區間
→ **Brightness 1.8**（最亮處 ~0.35 深灰）＋ **Floor 0.4**（對比 >2:1）。
引擎截圖：織紋可讀、邊緣平滑、帶面有梯度。**待 user viewport。**

## 2026-08-20 追記㊺：褌＝平滑線實體（合體終案；BUILT-自驗待 viewport）

**user 兩問擊穿薄膜版**：「貼在皮膚上嗎？有厚度嗎？」——誠實答案：只有邊圈貼皮、
中間浮 15mm、零厚度單面薄膜＝沒有布的斷面。緩坡邊讀成「皮膚鼓起被塗黑」非「布壓在肉上」。
user 拍板「結合成有厚度的」＝**牆版實體構造 × 薄膜版平滑線**（`fundoshi_solid.py`）：
- 內層＝貼身面（σ25mm 重建平滑輪廓）**整體沉入皮膚 1.5mm**＝牆腳埋住、可見接觸線
  ＝牆身陡角穿出皮膚（對 ±1mm 變形穩定）；底面背面剔除＝無 z-fight
- 外層＝原位＋15mm；側面＝垂直牆＋上緣圓角 r=5mm（逐頂點帶寬鉗制、
  圓弧終點＝外層邊界本人內縮＝不疊共面條帶）
- 牆版鋸齒病根「噪聲線×牆高」已無噪聲線；**不細分**（實體牆不吃坡道解析度、
  平滑曲線上 5mm 弦差 0.06mm）＝15,024 verts / 30,050 tris（薄膜版 41k→輕 63%）
- 繞法交給 normals_make_consistent（封閉實體＝外向有唯一解）

**驗證＝全線掃描不抽樣**：離線 13.3px/mm × 12 機位（腰 6＋腿 6）——剪影全段平滑曲線、
布斷面亮邊成立（「一條布」讀感回來）；引擎截圖＝邊平滑＋立體梯度＋織紋可讀。
**記帳殘項**：①頂面靠邊的三角形著色雜訊（外層邊界內縮帶的低頻著色，13px/mm 可見）
②織紋在中距離讀成大棋盤格（碳纖感）——TileScale/織紋對比屬 user 口味域。
**待 user viewport。**

## 2026-08-20 追記㊻：褌終案＝陡板剖面位移殼＋σ40＋宏觀紋理層（BUILT-自驗待 viewport）

**user 兩項指示**：低通再往上（σ25→40mm，偏離 p50 4.6/p90 8.7/max 15.8mm）；
遠觀紋理不可見（織紋 2.31mm 遠距=次像素被 mip 抹平＝單一尺度物理限制）。

**構造終案＝陡板剖面位移殼**：實體版（逐頂點蓋牆）在轉角二連翻車——「鄰居符號測試」
與「內部場方向」兩種 B 定義都在腿口/腰 V 轉角產生剖面鏈交叉＝頂緣碎裂片（12 機位掃描
實錘）⇒ **逐頂點蓋剖面路線廢棄**。回到位移殼（轉角自動平滑＝高度場是平滑距離場的
函數）、剖面改陡板：`h = -0.5mm沉 + 15mm·sin(π/2·d/9mm)`——9mm 內直升（59°）＋sin 圓肩
＝「有斷面的實體板」讀感、零牆體拓樸。E=7mm 首版坡面細皺（2.2mm 網格跨 7mm 只有
3 排頂點）→ 9mm＝4 排收斂。

**宏觀紋理層**（`ue_fundoshi_macro.py`）：同一張布紋 ×0.12 尺度疊乘
`albedo × lerp(1, macro/mean, 0.4)`——週期 ~18mm、2m 距離 ~11px 可見；旋鈕
`ClothMacroScale`/`ClothMacroStrength`。

**12 機位離線掃描（13.3px/mm）**：主帶全段＝乾淨板狀斷面＋平滑剪影；
**殘留記帳**＝襠帶區（離皮懸空＋高曲率）坡面仍有皺紋與一處階狀缺陷（1/9 號機位）
——非高分畫布區、待 viewport 裁決要不要追。**外觀待 user viewport。**

## 2026-08-20 追記㊼：褌邊緣鋸齒真兇＝網格解析度（低通調不掉）＋自適應細分修（BUILT-自驗待 viewport）

**user 問「低通要調到多少才能消除」——答案：調不掉。** 定罪鏈：
①snap 理論被數字否決（貼皮吸附只加 HF 0.03mm、法線深度法無差）；
②屁股專拍離線乾淨 ⇒ 缺陷是局部段；③對上細分報表：**全域 2 輪後邊長 p90 仍 5.54mm**
——坡道起皺線（牆腳折痕）被 5mm 粗網格多邊形化＝user 貼臉看到的階梯。
**噪聲在網格不在曲線＝任何 σ 都修不到。**

**修**：①全域細分 1 輪＋**邊緣帶自適應紅綠細分**（保共形、無 T-junction；帶寬 8mm、
目標邊長 3mm、3 輪預算制——首版帶寬 14mm/目標 1.6mm 連鎖爆到 69 萬 tris）
②坡腳 C1 軟起（3.5mm 漸入＝把硬折痕攤開，殘餘多邊形化變 1px 級）。
成品 80,737 verts / 151,565 tris。
**效能債記帳**：帶子 151k tris ≈ 身體 6.5 倍、六人房 ~900k——旋鈕＝RED_D/RED_EDGE/
輪數；嫌重可退到帶寬 6mm/目標 4mm（~70k）。
離線驗證：12 機位全線＋屁股專拍 6 機位——邊緣收斂、殘餘小皺集中襠帶 V 區（記帳）。
**待 user viewport。**

## 2026-08-20 追記㊽：褌邊緣殘餘不平整＝三層拆解（user「這真的無解嗎」；BUILT-自驗待 viewport）

**答案＝不是無解，但有效能地板**。殘餘拆三層：
1. **著色扇貝紋**（亮邊沿邊方向的抖動）→ **沿邊各向異性法線平滑**修掉（iso-d 高斯權重
   ＝只沿等距線抹平、不跨剖面＝斷面立體感保留；零多邊形成本；band 52,378 顆 12 輪）。
2. **剪影弦差**：牆腳帶再加密一級（1.5mm）實測連鎖爆到 **413k tris**＝六人房不可受
   → 回退單級 8mm/3mm（151k）。**殘餘＝剪影 ±1mm 緩波**（低頻、非鋸齒）＝
   幾何解的效能地板，記帳為已知極限。
3. **TAA 1px 呼吸**＝顯示端物理地板。
**待 user viewport**：階梯應已消失、殘餘為 ±1mm 緩波；不滿意的話唯一路＝吃 413k。

## 2026-08-20 追記㊾：褌雙密度＋法線朝向 bug 修（user「先做做完我看」；BUILT-自驗待 viewport）

**烏漆媽黑真兇＝自訂法線朝內**：aniso 平滑的繞向判定拿「翻面後的面」對「翻面前算的
VN」＝整條帶子法線反向＝形體光恆拿底值（全黑）＋邊緣著色亂（user 最後那張圖的鋸齒
有多少是幾何、多少是壞著色，在該狀態下不可分）。修＝直接對位移法線場取號。

**「為什麼圓滑要高面數」＝幾何表示法固有成本**：多邊形以平面片逼近曲面、誤差≈邊長²/8R
——褌的圓滑邊＝曲率半徑 5mm × 長 8.2m 的摺線＝最貴組合。**洞見＝成本只需付在一具身上**：
貼臉只看得到睡姿受害者＝SM 靜態單一實例；站立骨骼身體沒人湊近。
**雙密度**（`FD_TIER` 環境變數）：SM=hi（兩級細分 215k verts/413k tris、
sumo_skeletal_hi.fbx）、SK=lo（79k tris、_lo.fbx）；ue_import 分流。
效能帳：hi 只在受害者沉睡時渲染一具靜態網格；六個站立骨骼身體共 ~475k＝可受。
**待 user viewport**：著色恢復後殘餘鋸齒的真面目才第一次可判。

## 2026-08-20 追記㊿：褌＝光滑板構造（「布貼著皮膚做」前提退役；BUILT-自驗待 viewport）

**user 問「有沒有辦法在我畫的範圍上建立一個圓滑的褲子」——答案＝有，病根不在邊在底。**
量測定罪：身體在褌區是 **3cm 邊長的多面體**（edge p50 30.8mm、dihedral p90 17.7°、
面片矢高 1~2.3mm 最糟 7mm）；舊位移殼「貼身面細分 3mm→沿法線抬 15mm」把每條稜線
忠實採樣再放大＝**殼比皮膚更粗**（環向 σ15mm 高通 std：殼 1.4~1.9mm vs 皮膚 0.6~1.0mm）。
㊶~㊾ 十四輪全在修邊，沒有一輪動過「底面＝皮膚」這個前提。

**洞見＝布不是皮膚的函數**：布下皮膚無消費者（不可見、不可畫），布與皮膚之間只剩
「布 ≥ 皮膚＋ε」一條不等式——形狀可以自己光滑。**新構造三件拆開**（`fundoshi_plate.py`，
舊 `fundoshi_shell.py` 退役）：
1. **形狀 U**＝身體 cage（11,931 頂點原始 quad 網）在褌區逐頂點沿法線外推 o_v，取
   **Catmull-Clark 極限面**（level 3）＝自己 C2 光滑；o_v 由「U ≥ 皮膚多面體+0.5mm」
   迭代解出（8 輪收斂、violators=0、o p50 2mm max 8.6mm）。光滑度來自細分曲面本身，
   不再繼承粗網格。**線性孿生**（SIMPLE subdiv 同拓樸）＝遮罩等值線的載體
   （CC 極限面參數化有切向漂移、等值線必須在雙線性域上切）。
2. **裁切 C**＝手繪遮罩 UV 空間 σ15mm 高斯模糊場的 0.5 等值線（marching triangles＋
   snap 防細長三角）——**線只裁切、不決定幾何**，「噪聲×牆高」的乘法從構造上消失。
3. **接觸**＝頂板 U+5mm 布厚、牆＝頂緣沿法線直落牆腳、**牆腳＝等值線本人沉 3mm**
   ＝可見範圍恆等於手繪線；頂緣/牆腳各沿迴圈 σ6mm 平滑＝頂緣零鋸齒。
   底板封閉（54,762 verts / 104,604 tris 單一密度；**FD_TIER 雙密度退役**、_hi/_lo 同檔）。

**三者對齊閉環（user 直問「畫的範圍/禁畫區/布蓋住的範圍能否完全對齊」）**：
`fundoshi_plate.py` 末段從布網格**射線反推衍生遮罩** `fundoshi_mask_final.png`
（骨盆區＝布實際覆蓋、元結帶＝手繪原樣；與手繪 xor=2.5%、p50 差 1px）；禁畫層三個
消費者（bake_sumo_ink_masks.py／intake_selfie.py／**NiceInkFaceBakery.cpp**）全改讀
它＝布、禁畫、手繪單一來源；六張 T_EyeMaskInk 已重烘重匯。手繪 sharp 遮罩仍是
authoring 正源（永不覆蓋），final 是衍生物。

**驗證**：離線 13 機位 13.3px/mm A/B（`Saved/FundoshiPlate/AB_contact_sheet.png`）＝
頂緣/剪影乾淨、後 V 區舊皺全消；頂板 dihedral p50 0.92°（舊 5.07°）；穿刺 facing=0
（lateral 348 顆全在股溝夾縫＝兩臀之間不可見，記帳）；robo_sumo_smoke 全 PASS
（tri-cache 23674 不變＝墨水數學零波及、UV 雙向、跨端複寫、SK 38 骨）。
**新螢幕截圖工具**＝robo_fundoshi_plate_shots.py（睡姿受害者表面射線機位）。

**血價/鐵坑**：①「布下有皮膚頂點」的約束不能用「最近皮膚點」判（倒懸股溝側壁下
沿法線抬升＝越抬越深、o 發散到 61mm）——沿自身法線射線判才收斂；②牆腳貼回三角化
皮膚會在非平面 quad 處側跳 5.7mm＝頂緣 zigzag（雙線性孿生≠三角化多面體），正解＝
不貼回、用 SINK 3mm 吃掉歧義；③抬升量要 1-ring 中值濾波＋GAP_MAX 上限（射線打到
對面 U 的離群值）；④**3-client PIE 在 user 桌面重載下穩定 D3D12 OOM**（殭屍編輯器
佔 VRAM 也參一腳）——robo_sumo_smoke 改 2 人 PIE、跨端驗證走 UEDPIE_1。
**殘留記帳**：股溝夾縫內布面互插（不可見）；牆面 ~2cm 間隔淡直紋（來自牆腳跨稜線，
σ6mm 平滑後已弱）；效能 104k tris（舊 hi 413k 的 1/4、六人 ~630k）。
**外觀待 user viewport。**

## 2026-08-20 追記51：褌破洞雙修＝底板焊牆腳（水密）＋股溝擠壓推出（user 抓「破洞看到褲子裡面」；BUILT-自驗待 viewport）

**破洞一＝底板開縫（主兇）**：底板整圈停在布面高度、牆腳沉在皮膚下＝繞全帶一圈的
2~9mm 開縫（離線圖的黑豎縫＝同源）。修＝**底板輪廓頂點直接共用牆腳頂點**＋
`open edges == 0` 水密斷言（構造性排除，進常駐驗收）。
**破洞二＝股溝布側陷皮**：布側邊 rest pose 就陷在大腿皮下（348 顆、最深 7.6mm），
站立時夾在腿間看不見、睡姿張腿＝黑帶中間露膚。修＝**擠壓推出**（真布行為：被大腿
夾住貼著推）——陷入頂點沿最近皮膚法線推到皮外 0.8mm、頂/底同 delta、場過 1-ring
平滑、**平滑後再壓到收斂**（平滑會把矯正拉回皮下——終態必須重新滿足約束）；
facing/lateral 穿刺雙歸零。**M_Fundoshi 改 two-sided**（薄實體布；姿勢拉開的縫看到
布內側而非穿透）。

**驗證**：離線 13 機位重掃（豎縫消失）＋引擎扇形掃描 33 機位（沿身體軸每 10cm×三側線
＝機位不可知時的地毯拍法；robo_fundoshi_plate_shots.py）。
**誠實記帳**：睡姿股溝帶側邊仍有波浪皺（wall foot 在大腿內側、姿勢蒙皮差殘留）；
布邊附近皮膚上的「矩形暗塊」＝螢幕空間 AO/接觸陰影方塊（均勻壓暗 28%、軸對齊＝
貼圖/幾何皆排除；舊殼時代已存在、two-sided 不影響）——另案待查（SSAO 設定）。
**外觀待 user viewport。**

## 2026-08-20 追記52：褌邊緣公分級去噪＋後腰窩 V 保護（user「公分級的走向也能去噪嗎」；BUILT-自驗待 viewport）

**user 授權公分級低通、後腰窩 V 凹（縦褌 T 交接、刻意設計）除外**。線只裁切不決定
幾何＝σ 可以放心加大：等值線（σ15 場）抽出後再**沿弧長 σ40mm 高斯平滑**（偏離
p50 4.3/p90 7.5/max 12.8mm＝舊 σ40 核准量級）；**保護區**＝後腰窩（|x|<0.11、
y<-0.10、4cm smoothstep 軟過渡）權重歸零＝V 凹一分不動（腰圈 6.9% 受保護；量測
定位 V 谷底=(0,-0.17,0.49)、背側=-y 以 butt 地標定證）。

**兩條新血價**：①**平滑曲線不可再投影回多面體皮膚**——nearest/射線投影都會把平滑
線重新量化成 3cm 面片段（頂緣階梯平台實錘）；正解＝頂緣活在光滑 U 上（平滑線沿
法線抬上 U）、牆腳才射線落皮（藏皮下、量化不可見）。②**封閉實體上「刪退化面」＝
開洞**（水密斷言當場抓到 3 開放邊）——退化面用 0.03mm 頂點微推救活，不刪。

衍生遮罩/眼罩隨新輪廓重烘重匯（xor vs 手繪 5.5%＝σ40 預算內；三者對齊機制不變）。
**殘留記帳**：貼臉掠角下邊緣帶仍有 ~1-2mm 級細平台（邊緣帶高度場殘餘、非線噪聲）。
**外觀待 user viewport。**

## 2026-08-20 追記53：褌邊緣高度剖面去噪＋邊界位移形變場（user「只針對單一方向嗎」；BUILT-自驗待 viewport）

**user 問「去噪只有單一方向嗎」——定罪＝兩個殘漏**：①**離皮高度剖面**只吃過 σ6
（σ40 平的是面內走向）＝邊緣沿線上下起伏殘留 → 高度也上 σ25（V 保護區照豁免）
＋皮膚淨空回夾（只准抬不准沉、凸點處局部讓步 1.5mm）。
②**只搬邊界頂點＝邊緣帶手風琴摺片**（12.8mm 位移壓過 2~4mm 的第一圈三角形＝
渲染上的頂緣平台/階梯；頂緣環本人量測是平滑的＝階梯是摺疊的邊緣帶剪影）→
邊界位移改成**形變場**傳入內部（σ12mm 高斯內插、離邊 25mm smoothstep 衰減、
13,367 顆內部頂點跟隨、重新抬回 U）。

離線 13 機位 A/B＝頂緣平台消滅、主線公分級平滑；殘留＝掠角下細鋸齒微屑（1mm 級，
牆頂角可見性閃爍）。衍生遮罩/眼罩重烘重匯（xor 6.1%）。**外觀待 user viewport。**

## 2026-08-20 追記54：褌邊「皺」真兇＝分區法線污染（user「感覺依然沒有做」；BUILT-自驗待 viewport）

**user 質疑正確：幾何平滑一直在做，畫面卻不動——因為主兇是著色不是幾何。**
定罪鏈：①資產/開局時間對賬＝遊戲確實在跑新網格；②牆面法線轉角量測 p50 0.4°/max 8°
＝牆幾何其實平滑；③熱點定位（牆法線轉角聚類）＝(±0.30,-0.18,0.57) 臀後緣＝user
截圖位置；④定點渲染＝「皺」＝牆上 ~5mm 間距豎肋＋頂緣翻捲讀感——**頂點法線把
頂板/牆/底板三區混平均**（牆與頂板共用頂點、切割邊界第一圈碎三角形大小不一
＝法線沿環抖動＝著色肋；平滑幾何當然無感）。

**修**：①**分區自訂法線**（top/wall/under 各自面積加權平滑、區界=銳邊＝真布摺邊）
＝肋紋/翻捲瘤一刀消失（A/B 定點圖實錘）；②擠壓推出改壓光滑 U（壓多面體=面片瘤）；
③保護區改 σ12 輕平滑（V 形狀半徑>>12mm＝形狀保留噪聲照除）；④頂緣高度貼死 U＋σ8
（σ25 獨立平滑=與內部高度分家=縫邊捲）；⑤牆腳深度沿環 σ8＋至少埋 0.5mm 回夾。

**鐵則**：「怎麼平滑都無感」＝先驗著色再驗幾何（壞著色下幾何缺陷不可歸因的姊妹條）；
多區共享頂點的網格、區界要 split normals。**外觀待 user viewport。**

## 2026-08-21 追記55：褌邊凹凸終兇＝牆×多面體皮膚交線＋裾邊唇（user「這些凹凸到底是什麼」；BUILT-自驗待 viewport）

**答案：凹凸＝布牆與皮膚多面體的交線。** 20px/mm 現場圖攤牌：皮膚本身是 3cm 面片
多面體（掃描重拓樸 ≤20k 預算的固有解析度），布再平滑、「布被皮膚遮住的那條線」
也逐面片跳（±1~2mm/3cm 週期＝user 貼臉看到的角狀缺口）。布側平滑已全部打完
（凹凸不在布上）；交線是皮膚解析度的鏡子，遮住它＝**裾邊唇**：牆在皮上 1mm 處
接一圈 5mm 寬布唇壓在皮膚上、外緣埋皮下 1.5mm ⇒ **可見邊界改由布唇的光滑邊定義**、
鋸齒交線藏唇下（順帶＝真實廻し布邊壓肉讀感）。

**施工血價三條**：①唇上下兩層共點共面＝z-fighting 斑駁流蘇——薄片要真厚度
（底層 -0.5mm 自己的頂點＋內封條帶＝水密）；②唇外推方向用逐點內鄰平均＝切割
頂點處抖動＝流蘇——方向與外緣都沿環 σ6 平滑＋「至少埋 0.8mm」回夾；③本輪
中途誤判三次（回夾尖峰/高度剖面/形變場殘餘）——kink 指標混入真曲率＝
**幾何指標要先扣除設計曲率才能當噪聲儀**。

成品 131,800 tris（唇 +7k）；衍生遮罩含唇覆蓋（xor vs 手繪 9.9%＝唇寬 5mm 如實
入帳、布=禁畫同步不變）。**外觀待 user viewport**：貼臉的角狀缺口應消失、
邊帶一圈唇線＝新的設計元素（不喜歡可退：HEM_W=0）。

## 2026-08-21 追記56：平唇退役＝捲邊圓筒＋全邊行軍驗證（user「為什麼來回這麼多次白忙活」；BUILT-自驗待 viewport）

**user 的質問成立，白忙的兩個流程根因如實記帳**：
①每輪只在「我定罪的那個點」驗證——違反自己 08-20 記下的「全線掃描不抽樣」鐵則
＝每次我這裡綠、user 那裡爛。②**病根一直只有一個**：任何「逐點跟皮膚表面走」的
布特徵都繼承皮膚 3cm 面片噪聲——線平滑/高度/法線/擠壓/平唇五刀都真實、都只治
同一根病的一張臉；平唇又是逐點貼皮＝當場復發（被面片吞吐＝user 截圖的破布條）。

**終構造＝捲邊圓筒（平唇退役）**：截面圓弧（R=4mm、25°~165° 五環）掃過光滑頂緣
框架、**完全不投影皮膚**＝可見輪廓構造性光滑；起捲線用光滑深度（皮上名目 3mm）、
筒底埋皮下 ~5mm（面片 ±2mm 也埋住）；面片交線發生在筒腹下側＝被筒身遮住。
真實廻し布邊本來就是捲的＝設計語言自洽。牆+捲邊同一平滑組（布連續捲過）、
頂板交界照舊銳邊。154,072 tris。

**驗證改制＝全邊行軍**：三條頂緣環每 6cm 一機位、20px/mm 掠角、130 張全部人工
過目（contact sheet）——整條邊連續光滑捲邊；**殘留記帳**＝股溝隧道最深處捲邊
自交小皺（m1_032~035/m2_009~011；曲率半徑<捲徑的掃掠自交、物理上布本來就被夾、
多數角度被臀縫遮擋）。**外觀待 user viewport；行軍圖存 Saved/FundoshiMarch/。**

## 2026-08-21 追記57：基底修＝皮膚邊帶細分（user 點破「凹凸是皮膚本身、布不可能不貼皮」；BUILT-自驗待 viewport）

**user 的歸因正確＝終於修在病根本體**：布邊可見線（牆交線/肉縫皺摺線）畫在皮膚上，
皮膚是 3cm 面片多面體 ⇒ 線天生鋸齒＝布側五刀全治不到。修＝`sumo_edge_band_refine.py`：
**只在布邊界 ±20mm 的皮膚窄帶**（554 面/全身 4.7%）細分 4:1（3cm→7.5mm、矢高 1/16）
＋投影到原網格 CC 極限面（12mm 內全額、22mm 歸零＝帶外一頂點不動）；
body tris 23,674→**41,236**；最大位移 7.1mm＝股溝皺摺撫平（大半蓋布下）；
UV0/FaceUV/HairUV/FaceMask/權重全由 bmesh 內插保留；備份=masters/v23_prebandrefine。

**施工血價**：細分後的皮膚不可當布的 cage——褌密度跟著爆 16×（1.29M tris 實錘）；
修＝**布 cage＝細分前身體（從備份 append）、細分後身體只當 skin_bvh 目標**
（布不需要繼承皮膚解析度，只需要踩在光滑皮膚上）。布回 154k tris。

**驗證**：離線行軍 130 機位（邊全線光滑；Workbench 過渡圈斑駁＝引擎實拍不存在）
＋引擎 33 機位扇形掃描（腹邊/股溝帶/臀縫帶全乾淨——臀縫皺摺線第一次平滑）
＋robo_sumo_smoke 全 PASS（tri-cache 41,236、UV 雙向、跨端複寫）。
**記帳**：墨水 tri-cache 掃描 1.74×（20k 預算→41k；directdraw 效能迴歸未跑=待辦）；
butt 探針地標 UV 換島（皺摺撫平後最近面翻到對側=探針現象非 bug）；
股溝隧道深處捲邊自交小皺照舊（被遮擋）。**外觀待 user viewport。**

## 2026-08-21 追記60：法線 v3＝帶外逐位還原 v23＋單一場（user「為什麼動到肚臍/褲外」；BUILT-自驗待 viewport）

**user 抓到的是真犯規**：法線 v2 整顆身體清掉、從「平滑代理」按位置重取樣——肚臍等
小而深特徵在代理上位置偏移＝取樣錯位＝**褲外著色被改**（幾何未動、著色動了，錯就是錯）。

**v3 定案（兩道閘門逼出來的）**：①帶外＝v23 烘焙法線 POLYINTERP_NEAREST 原樣抄回
（GATE1 斷言逐位一致：max 0.034°＝肚臍還原保證）；②帶內原計畫用「同配方光滑場」
——GATE2 擋下：**v23 的烘焙含歷史特調（頸縫端排等）≠ 純配方場**、帶界差 9~14°＝
必有線 ⇒ 改**單一來源**：整顆身體只取樣 v23 實際法線場（帶內幾何僅移 ~2mm、同場
取樣成立）＝構造上無分界線。③GATE2 增量尺自帶 ~10° 階梯誤差（最近頂點近似）＝
尺比病粗 ⇒ 降級報告值；判準=掠射光渲染三聯圖（肚臍/帶界/大腿：cur≈v23 基準、
v25 壞版可辨、無帶界線）＋user viewport。

**鐵則**：①法線修復的半徑不得大於幾何改動的半徑（帶外一個 loop 都不許動）；
②歷史烘焙=實際資產不可用「同配方重煮」替代（含特調）——要原樣抄；
③量尺解析度必須高於病灶量級，否則降級 report-only、判準交給渲染。

## 2026-08-21 追記61：捲邊毛刺雙修＝對邊近接錐縮＋擠壓交替鬆弛（user「毛刺垂直於褲面」；BUILT-自驗待 viewport）

**定罪鏈**：①毛刺不在頂緣線（σ40 線曲率半徑恆 >9mm、曲率錐縮零觸發）；
②真兇＝**臀縫隧道裡兩條布邊相距數 mm、塞不下兩個 4mm 捲邊**＝截面互越摺片。
**修**：①捲徑按「與對面布邊的距離」錐縮（R=0.35×對邊距、下限 0.8mm、σ10 平滑）
＋**方向判準**（同帶兩緣背對背外捲不互撞——首版沒判方向把腰帶側髖整段誤錐到 1mm）；
②擠壓推出改 6 輪「平滑↔補壓」交替鬆弛（單輪平滑=大推量區殘摺）。

**失敗記錄（誠實）**：U 外推場的雙向 Laplacian 鬆弛不收斂——平滑砍掉的峰是約束
需要的（振盪殘留 2mm 穿刺）⇒ 恢復只增不減版；**臀縫深處 U 皺＝籠解析度下的真實
約束需求＝實體極限**（記帳），布面穿刺恆 0。**外觀待 user viewport**：
開放邊上的毛刺應消失；臀縫最深處（多角度被遮擋）殘摺為已知極限。

## 2026-08-21 追記62：布皮同源緩波修＝粗籠帶內強 Taubin＋布籠同源（user 定罪「布邊波浪=貼邊皮膚波浪」；BUILT-自驗待 viewport）

**user 的推理鏈全對**：布邊側視的不規則波浪＝皮膚在 2~8cm 波長帶的起伏（振幅 ±4mm 級）
穿透而來——①布必須墊在皮膚凸點上（防穿刺）＝凸點頂進布面；②我先前的窄條 Taubin
只殺 <2cm 面片噪、這一帶整段穿過；③褲帶正下方的皮膚根本沒在平滑範圍內。

**修**：布蓋住＋邊緣帶的**粗籠**頂點做強 Taubin（60 對、同面判準防臀縫互抹、
位移上限 5mm、窄條鄰居<4 不動）＝2~8cm 帶在 3cm 籠上近 Nyquist 天生強衰減、
>12cm 設計形保留；同一顆平滑籠另存 `cage_smooth_v3.blend` ＝**布的 U 從它長**
（布皮同源同平）。邊視角渲染 A/B（Saved/FundoshiPlate/we_edgeon*.png）＝側帶拉直。

**量尺第五/六次翻車記錄**：①σ10−σ40 帶通在曲面上帶 2~8mm 曲率偏置（凸面被高斯
往內拉、兩 σ 拉量不同）＝量測與修復算子雙雙污染——帶通量測不可在曲面折線上直接用；
②字串手術誤刪自己的「寫回網格」迴圈＝平滑算了沒生效（數值逐位相同＝先疑沒生效）。
**鐵則：曲面上的頻帶操作一律用 Taubin（零收縮=無偏），不用高斯差。**
**外觀待 user viewport**；殘留＝臀縫窄條區（韁繩排除）與 5mm 上限外的大振幅段。

## 2026-08-21 追記63：地基終解＝全身高解析度化（user 定案「細分＋弄平＋重做褲子」；BUILT-自驗待 viewport）

**補丁循環的了結**：褌邊 20 餘輪的根源＝「3cm 粗籠地基上做毫米級的布」。user 拍板
全身解法：`sumo_body_hires.py`＝v23 乾淨籠 → 布區中頻 Taubin（60 對、同面判準、
上限 5mm＝user 定罪的 2~8cm 皮膚波浪）→ **全身 Catmull-Clark 細分一級**
（同一個雕塑的光滑版；uv_smooth=NONE＝UV0 手繪版面逐島線性凍結；權重/FaceMask 內插；
頸縫兩殼細分後 156 對仍逐位配對）。23,674→**94,556 tris**。
窄帶細分路線（sumo_edge_band_refine 系列）全退役。

**布重建於光滑地基**（plate SUBD 3→2 配 1.5cm 籠＝布密度不變 154k）：
**墊高場 o max 9.8→2.4mm、股溝 lateral 穿刺首次自然歸零**＝光滑地基幾乎不需要墊
＝之前所有「凸點頂布」病一次消失。法線 v1 配方重烘（uniform 密度＝無帶界摺、
seam 156 對 gap 0.02°）。行軍 130 機位＝歷來最乾淨（殘=臀縫入口 2~3 格小皺）。

**directdraw 效能閘門**：2 客戶端版 57 PASS / 8 FAIL——FAIL 歸屬：4×far 遠點＋cruise
tipSpd＝08-18 既有（舞台搬家寫死座標待辦）；flow d_dotN=418（有出墨、低門檻 16%）
＋palette shader 掃空＋ghosts＝固定掃描參數對新網格失準同族（**探針常數重校＝另立
待辦**，非出墨回歸——smoke 於 hires 版全 PASS：tri-cache 94,556、UV 雙向、跨端複寫、
出墨、RT 匯出）。**3 客戶端完整版在 user 桌面現況 VRAM 不足＝D3D12 fatal**（crash
reporter 實錘）——本機 3-client 迴歸從此停用、跨端驗證恆走 2 人 UEDPIE_1。
**外觀與畫畫手感待 user viewport**（效能主觀卡頓請回報：預算 4.7×、冒煙數字未見異常）。

## 2026-08-21 追記64：毛邊＋側面鼓包雙修（user「一起修好後我再看」；BUILT-自驗待 viewport）

**毛邊**＝捲徑錐縮誤傷（判準太鬆把半條腰帶逐點誤錐 0.8~4mm 抖動）：收緊＝只有
「真隧道」（對邊 <12mm 且落在外捲方向正前方 dot>0.5d）才錐、半徑場 σ15 重平滑
——誤錐 818→220 顆、三環捲徑 p50 全回滿徑 4mm。

**側面鼓包**＝防穿刺墊高場的孤立峰（bump max 2.67mm ≈ o max 2.39mm 定罪）：
修＝**統一基礎淨空 2.5mm**（ring 羽化）⇒ 逐點修正歸零＝痘構造性消失；
複測＝側髖簇（±0.44,-0.17,0.72）從榜上消失 ✓。剩前中央簇＝襠帶×前腰帶交接的
設計摺（2-ring 量尺把設計皺讀成凸起＝已知量尺極限、不追）。
布浮高 +2.5mm＝視覺無感；U0 首輪違規歸零（順帶抓掉空陣列 percentile 崩潰）。
**外觀待 user viewport。**

## 2026-08-21 追記65：褲子周圍皮膚「普查＋整平」（user 定案流程；BUILT-自驗待 viewport）

user 抓到帶側凸起仍在＋大腿前側凹陷，並下令**停止逐點打地鼠、要一個把所有
未發現凹陷一次解決的方法**。定案流程（user 逐輪確認後「做」）＝
`Tools/AssetPrep/sumo_skin_flatten.py`：

1. **局部膜**＝布 8cm 帶（羽化至 10cm）零收縮 Taubin ×200 對＝理想光滑版（只當尺）；
   同面判準（法線 dot>0.3 才相鄰）＝臀縫/腿縫構造性不互抹。
2. **普查**＝逐頂點量「皮膚−膜」深度＋連通聚類造冊：29 簇。深 ≥6mm 自動保護
   3 簇＝兩瓣臀＋前肚垂懸（全是解剖設計）；肚臍在區域外連算都沒算。
3. **修**＝往膜靠攏：峰深 ≤3mm 全拉平、3~6mm 羽化、≥6mm 整簇不動＋因子場
   空間平滑防硬階；帶外逐位不動（斷言）。套用 2954 顆、最大 3.2mm，
   修後對膜殘差 p99 0.89mm＝噪聲清空。
4. 布在新皮膚重建（cage_smooth_v3 重存；水密、零穿刺、捲徑 p50 滿徑 4mm）
   ＋全身法線重烘（縫 156 對 gap 0.02°）＋掠射光環拍 11 機位自查
   （flat_ring*/flat_spot_*：大腿前側、髖側、帶側全平順）。

備份=masters/sumo_character_master_v27_preflatten.blend。**外觀待 user viewport。**

## 2026-08-22 追記66：貼臉缺陷三層定罪（引擎內開關手術）＋帶緣錐縮 v3＋血色場重烘

user「兩個問題都還在」後停止 Blender 域自證，全部改在**引擎內、user 倍率（FOV36 貼臉）**量：
新儀器 `robo_skin_ab.py`＝睡姿受害者射線網格（頂面/側面）＋同機位開關手術三連拍
（預設/血色場0/假光拉平）＋AO 開關 A/B。定罪結果：

1. **幾何在引擎內乾淨**（頂面二次擬合殘差 p50 0.8mm；SM_Sumo 248,548 tris＝資產新鮮）；
   SSAO A/B 逐像素相同＝非 AO；SM 法線匯入補設＝逐像素相同＝本來就有帶（假說死）。
2. **帶頂邊波浪（user 的凸起）＝真幾何**＝貼臉倍率下才可見的錐縮殘餘（髖側被斜上方
   襠帶邊誤觸發）。修＝**錐縮判準 v3 只認正面對撞**（<9mm＋夾角≤45°＋法向差<8mm）；
   誤錐 227/306/195→112/187/165、p5 1.90→2.54mm；重建後同機位邊緣明顯變順。
3. **皮膚多邊形淡斑＝血色場 T_BodyChroma 是 07-14 老烘焙**（三代網格前的 512 貼圖；
   覆蓋失敗填充的直邊楔形錯位到可見處；開關手術實錘=斑住在 chroma 層）。
   修＝現網格重烘（ICP 殘差 1.77mm、157k 紋素；sumo_body_chroma_geo/post 改吃
   CHROMA_S 環境變數——舊 scratchpad 蒸發的血價）＋T_BodyChroma 原設定重匯。
4. 橙色矩形（大腿旁）＝布與腿剪影間看到的背景木地板＝非缺陷。

鐵坑：robo 模式 play_mode_robo.ps1 還在開 3-client＝已定罪的 D3D12 OOM 又炸兩次
（**ps1 已改 2**）；MID python 讀參數=get_scalar_parameter_value（無 k2_ 前綴）；
探針開場的 ab_* 清掃把 _OLD 對照檔一起刪＝對照檔命名要避開清掃 glob。

**睡姿顯示鏈確認**：沉睡身體＝SM_Sumo 靜態網格剛體躺放（SleepMesh 恆=StandMesh、
無蒙皮變形）＝出貨鏈的 SM 匯入就是 user 貼臉看的東西。**待 user viewport**：
帶緣波浪與大腿凹陷是否消失；若仍在，指位（現在儀器能在同倍率下復現追擊）。

## 2026-08-22 追記67：布面「完全平滑」戰役（user 裁決「先處理這個」；BUILT-自驗待 viewport）

user 質問「褲帶的面現在是完全平滑的嗎」→ 答案=不是，且我前兩把尺都有偏置：
極座標法在髖角/摺返跳面（127mm 假殘差）、quadric 法的 45° 鄰域門檻擋不住 25°
捲邊環（全正號假凸）。**正尺＝quadric＋銳邊距離排除**（分區裂法線邊 8mm 內整圈
不判）：面內部其實 p99 0.48mm，唯一真波紋=後髖兩側 0.6~1.5mm 正負混合。

修兩刀（fundoshi_plate.py）：
1. **頂緣貼回 U**（snap-to-U 最近點投影、>8mm 不貼保險）——量出僅 ~0.1mm＝
   原嫌疑（邊緣機械塗抹差值）不成立，但構造保證留下（淨空回夾觸 0 顆=成功斷言）。
2. **7c 頂板終拋光**＝真解：構造無關零收縮 Taubin ×40 對（輪廓+2 圈不碰、
   同面判準 dot>0.9＝V 谷/襠帶交接設計皺不跨、cap 1.2mm）＋補壓回 U 淨空。
   **戰果：面殘差 p99 0.48→0.241mm、max 1.47→0.66mm**；水密/零穿刺/捲徑不變。
   出貨全綠＋引擎貼臉複驗（robo_skin_ab）＝帶頂邊順、面乾淨。

鐵坑：**驗證探針啟動前必查 play 模式**——收尾切回 party 後兩次直接啟動編輯器
＝PIE 開不了局（FAIL: no drawing phase 而 log 死寂的簽名=NetMode 不對，不是慢）。
量尺教訓（第 7、8 隻）：極座標假設星形、鄰域法線門檻要按「最淺的設計摺角」定，
不是按直覺 45°。**外觀待 user viewport；殘餘=後髖 0.66mm 單點＋襠帶交接設計摺區。**

## 2026-08-22 追記68：帶緣「一排凸起」根治＝頂緣線 σ12 終平滑（BUILT-自驗待 viewport）

user 修正描述：不是兩顆齒、是**整條邊一排凸起**（正前方到處都是）、側看掠射才成齒
＝系統性、週期性＝離散化簽名。定位（自建掠射掃描儀 render_graze_front 重現成功）：
7c 拋光**故意跳過邊緣 2 圈**＝全布唯獨邊緣帶沒被拋光；snap-to-U 又把頂緣線釘回
這條未拋光的波紋 U ⇒ 邊緣線殘留 1.5~4cm 週期波 ⇒ 掠射遮擋把 0.3~0.7mm 放大成齒排。

修＝**頂緣線最後一道 σ12 沿線平滑**（殺 1.5~4cm 頻段；後腰 V protect 豁免走 σ6；
與 U 的分歧由 6b 調和帶吸收；皮膚淨空錐夾照跑）＋捲向場 o_sm σ8→12 同頻段。
實測移動 p50 ~1mm p90 2.4~2.8mm＝該頻段真有料；水密/零穿刺/捲徑不變；
掠射 A/B：舊銳利缺口消失、殘餘=軟緩大弧（設計形狀）。出貨全綠。

儀器沉澱：剪影獵刺器（silhouette_spike_hunt=32 機位開運算差分+反投影 3D 點名）
＋正前帶邊掠射掃描（render_graze_front=A/B 驗收機位、與 user 視角同構）。
量尺教訓（第 9 隻）：81px 高通窗量不到 2~4cm 齒（被當趨勢扣掉）——高通窗必須大於
目標波長；RMS 含設計大弧=假回歸（-26% 那格目視反而更好=先看圖再信數字）。
**待 user viewport：正前方帶邊的一排凸起是否消失。**

## 2026-08-22 追記69：掠射鋸齒＝遮擋放大戰役（檢討＋帶鄰皮膚兩級細分；BUILT-自驗待 viewport）

user 令「先仔細檢討」。**檢討全文**：七輪撲空的根因＝①修了七個真實但非 user 所指的
缺陷、從未驗證對應；②驗收機位漏了「與皮面的夾角」維度（user 貼臉=1~3° 掠射、
我全部驗收在 10~25°＝放大係數差 10 倍）；③把光學槓桿問題當表面加工問題磨公差。

**真掠射儀器（render_true_graze，2° 貼皮）首次完整重現 user 的鋸齒**；
光滑代理皮膚 A/B 齒消失→定罪皮膚微起伏——但代理同時縮了 1mm（幾何也變）＝
歸因被污染（量測為真≠歸因為真再犯）。實施：布鄰帶皮膚細分兩級（1.5→0.75→
0.375cm、離布 10cm/5cm）＋緊膜整平（sumo_skin_hires_band.py、BAND_* 環境變數；
帶內對膜殘餘 p50/p90=0.000、max 0.62mm=cap 保護的設計形狀）；body 204k tris。
**第 3 級對 2° 酷刑機位零改善**＝殘餘齒的調變不在皮膚微起伏（嫌疑轉向布冠高變化
/極限掠射的多邊形本質）。2° 機位比遊戲實際視角（眼距 15~25cm≈5~10°）嚴苛數倍
——實際視角下是否已修好＝**只能 user viewport 裁決**。若仍在：下一階=勒肉溝設計
（布壓進肉、圓摺藏交線＝構造解，需 user 點頭）或布冠淨空拉高。

布未動（皮膚位移 ≤1.5mm << 布淨空 3mm）；法線重烘 x2、出貨全綠。
備份鏈=masters/v28_prebandhires（細分前）、v29_pregraze2（第 3 級前）。

## 2026-08-22 追記70：鋸齒排真兇＝底板裙擺（反投影鐵證；五刀戰報＋殘案記帳）

user 三張截圖同構圖（黑條收尖+齒排）＋「不是兩顆是一排」→ 逐層獵殺：
1. **反投影定罪（probe_teeth_id＝像素→射線→三角頂點索引分類）**：齒＝FOOT×UNDER
   三角＝**底板→牆腳的裙擺帶**（底板在皮上 ~2.5mm、牆腳皮下 3.3mm、6mm 落差在單排
   面內垂直墜落）；張腿睡姿從縫隙內側看＝一輪廓頂點一齒。齒距 0.13mm/px 實測=輪廓
   取樣距（2mm）＝取樣尺度構造物。
2. 五刀戰報：錐縮 v4 構造性防撞（r_cap=間距/2）→零像素變化；腳線 cone-max 平滑
   →零變化；半徑場 cone-min（平滑後逐點重夾=鋸齒機這條鐵則的第三犯修正）→零變化；
   隧道退化制（R<2mm 捲邊退化平牆插值＝扭曲四邊形移除）→零變化；底板 3 圈緩坡
   →**更糟**（鄰點各抓不同側的腳=方向打架）＝已回退。前四刀雖沒打中齒、但都是真修
   （防撞構造保證/腳線光滑/半徑場光滑）＝保留入庫。
3. 壓溝制（sumo_skin_press_groove=沿布埋入線壓 2.2mm 溝）＝入庫（開放邊藏捲下）；
   對縫隙齒無效（溝越深露越多=方向搞反的教訓）。
**殘案**＝裙擺帶需要「設計過的」構造改案（候選：隧道區腳深改淺=裙變矮、或底板邊
沿「同段自己的腳」漸變（不可 KD 亂抓對側）、或隧道封橋）。**下一輪先做隧道淺腳版。**
鐵則沉澱：①「平滑後逐點重夾」=鋸齒機（半徑/腳深/高度場三案同病）——約束一律先
cone-min/max 攤成坡度受限包絡再夾；②反投影頂點索引分類=構造件定罪的終極儀器
（四刀猜錯、一刀定案）；③零像素變化=改動沒碰到兇手的構造件（別再調參）。

## 2026-08-22 追記71：裙擺齒高手術＝隧道淺腳＋全域淺沉（BUILT-自驗待 viewport）

追記70 殘案當回合續戰：裙高（底板 2.5 + 腳深 + 沉降）＝齒的振幅 ⇒ 動高度不動三角化。
①隧道淺腳（對邊 <10mm、tun 場 σ10 平滑）：腳深 ≤1.5mm、沉降 0.5mm ⇒ 下帶鋸齒
**厚帶→細淨線實測**；②全域淺沉 SINK 3→0.8mm＋腳深帽 1.8mm（深埋=3cm 粗皮時代
設計、皮膚現 0.1mm 級光滑+壓溝=0.8mm 即密封）；凹穴 ray+0.3 保底=不浮空。
vt 同機位：下帶完全乾淨、殘=上帶皺褶穴段（ray 保底維持深腳=裙仍高）＋數顆小碎片。
出貨全綠（水密/零穿刺）。**殘案下一刀（若 user 仍見）**：皺褶穴段＝腳深與可見性
的取捨要挑「浮空但藏在皺褶陰影」或「貼皮但裙高」——建議實測後再裁。

## 2026-08-22 追記74：兩大真兇終定罪＝血色場塊斑＋BowBody 顯示鏈（儀器史詩級修正）

**破案雙鑰**：①睡者顯示元件＝**BowBody（SK 骨骼）非 SM_Sumo**（全元件黑化診斷：Body
vis=False、BowBody vis=True；「SM=睡姿顯示」是錯誤前提、五天所有 SM 側自證白做一半）；
②**開關儀器從未生效**（多客戶端 PIE 截圖=聚焦視窗世界、robo 只改 UEDPIE_0+又打錯元件
＝之前所有 A/B「零變化排除」全是假的；猛藥對照組=亮度歸零應全黑一驗即知）。
儀器修好後資產級 A/B 一發定罪：**「凹陷」＝T_BodyChroma 血色場塊狀內容**（512 低解析
+取樣楔形；重烘版照樣有）——中和成均勻灰 ⇒ 同機位斑全滅、皮膚乾淨。
**現出貨=血色場中和版**（#37 的血色斑駁設計暫時下線＝乾淨>塊斑；重烘一張無硬邊的
血色場=待辦）。同批入庫：蒙皮權重場平滑（sumo_weight_smooth.py＝Hips/Spine*/UpLeg/
Leg 30 對 Taubin+重歸一；權重=3cm 粗網格時代畫的、細分只線性內插=權重稜×躺姿旋轉
=變形雜訊——此修獨立成立）；FBX use_triangles 三角化釘死；T_FundoshiMask 換 final、
edge shadow 中和（兩個 7 月殭屍消費者）。
鐵則（最痛一課）：**開關實驗先跑猛藥對照組驗儀器**；**顯示鏈的「哪個元件在畫」用
黑化診斷實測、不用讀碼推理**（讀碼推理錯了兩次：SM 說、SleepMesh 說）。

## 2026-08-22 追記75：戰役中止收帳（user 終止）

**最終定論（證據鏈完整、修未完成）**：兩問題（帶邊齒排＋大腿波浪）的病根＝**躺姿蒙皮
變形**——睡者顯示=BowBody(SK)、躺姿=站姿被骨架折過去、髖/大腿權重過渡帶正落在
「褲帶壓大腿」段＝折痕即缺陷；靜止≠無變形（變形完凍結）。休息姿勢的幾何已磨到玻璃級
＝全部打不到這層。**正式修法（已設計未實施）**：Blender 擺出遊戲同款躺姿→在折完的
形狀上清折痕→烘專用睡姿網格→接進現成的 SleepMesh 插槽（程式本來就留了、一直 fallback
站姿）；頭部轉動照舊走替身。剛體/變形同機位對照實驗腳本縫壞未完成（該重寫乾淨小腳本）。

**現出貨狀態**＝v31 幾何（布全修+褲周皮膚細分整平；全身整平與權重平滑已退回）＋
血色場中和（斑源已滅）＋布遮罩/邊影換新＋use_triangles。備份鏈 v27~v32 俱在。


## 2026-08-23 追記76：褌＝弓形實體＋皮膚勒痕整形（user 逐輪定案；BUILT-自驗待 viewport）

**起點＝追記75 的誤診**（「躺姿蒙皮變形」）**被讀碼推翻**：入睡路徑 `ResetBowBodyBones()` 把 37 骨
全回 SK rest、只擺 Neck/Head，躺下＝整個元件剛體旋轉 ⇒ **髖/大腿零蒙皮變形**，追記75 的修法
（烘 SleepMesh）會是第 41 次原地踏步。真正的病灶用「離輪廓幾圈」分層 dihedral 一次定位：
布面內部玻璃級（p90 4°），**唯獨貼邊兩圈 p90 40°/63°、1107 個翻摺面、邊線頂點相對板面 ±2mm
逐點亂跳**——40 個 commit 的驗收指標（全板中位數/水密/零穿刺/線本身 kink）構造上量不到它。

**三刀（user 逐句定案）**：
1. **皮膚「不是推平，是大腿該有的圓滑飽滿」**（`sumo_skin_band_fair.py`）：lump 圖（R=3cm 帶通）
   定罪＝沿褌路徑整圈的「藍溝＋紅脊」＝掃描本人的內褲勒痕＋髖摺（3~8cm 尺度＝所有 σ6/σ15
   整平的盲區）。修＝cotan 雙調和補洞（洞外兩圈固定＝位置與斜率接上、完整 3D 位移）。
   **兩次 user 打回換來兩條**：①硬圓周自由區＝肚下可見壓痕與淺凹 ⇒ 改**軟衰減**
   （能量 = 彎曲能 + Σ area·μ(d)·|x−x₀|²，μ 在腳印外 3cm 內為 0、3→10cm smoothstep 升到釘死）
   ②雙調和在腳印邊界會生多點震盪（大腿上段 5.1mm 新凹）⇒ **單點去尖刺＋位移場空間平滑 σ12**
   （降到 1.2mm，填充完好）。死路三條已記帳：uniform Laplacian 被密度差帶偏、σ40 MLS 在肚摺旁
   震盪、只沿法線動填不起 V 底。
2. **褌＝弓形實體**（`fundoshi_arc.py`，`fundoshi_plate.py` 退役）：user「不需要布上緣，以兩邊的
   接觸線為邊界」。v1 沿用 plate 的粗籠裁切域＋最近點投影＝**線位置偏**（籠↔皮膚差 2cm 時切向漂）
   ⇒ v2 **裁切域＝整形後的皮膚本身**（同一套 UV0，等值線直接切在皮膚三角形上、零投影）。
   實測帶寬 p50 45mm；水密、內部零穿刺、邊緣圈 dihedral p90 7°（舊 40/63°）。
3. **凹槽量測與位移**（`fundoshi_mask_warp.py`；user「以我手繪的樣子為基準，位移與稍微形變至
   凹槽處，保留後腰走勢」）：平均曲率圖抽出四條谷線（肚/腿摺、屁溝、腰窩、臀下摺）＋手繪線相對
   偏差（正面高 3~5cm、背面低 8~10cm）；位移＝前/背各一條隨 x 的剖面、髖側 smoothstep 混合、
   搬運用「分步沿皮膚走＋正向噴灑＋測地侵蝕還原寬度」（拉回取樣在發散處會把帶子拉成零寬＝撕裂）。
   **user 回溯**：加寬 1.25×、鏡射對稱、凹槽位移三批全部回溯，出貨＝v38（原始手繪遮罩、未加寬）；
   工具與備份鏈全留（v40 加寬／v42 加寬+同源整形／v44 對稱+前移、`fundoshi_mask_sharp_v2.png`）。

**血價**：①**改帶寬必須同步重跑整形**——腳印與布不同源＝接觸線落在只填一半的衰減帶＝
「凹凸又回來了」（user 抓到，量測證實皮膚逐位相同、動的是線的位置）②2D 投影看不出 3D 線的
折角，量「沿法線高度殘差」會漏掉側向跳步（user「我叫你看 3D 的線，你轉成 2D 是能看出什麼」）
③`sp` 當區域變數會蓋掉 `scipy.sparse` 別名（AttributeError 在 200 行外炸）。

**出貨狀態**＝v38（皮膚 v37 軟衰減＋弓形布＋原始手繪遮罩）；SK_Sumo/SM_Sumo 同源同時重匯，
**三條顯示路徑（站立/走路、作畫貼臉、沉睡）與選單跳舞力士全部走 BowBody=SK_Sumo**（讀碼查證）；
`SK_Sumo_Whole`（縫合版脖子、08-16 封存）未跟隨更新，切回前必須重匯。
腳本自 v38 後又加了五條改良（接觸線搬動上限/內圈鬆弛 20 輪/抬升法線平滑/頂面拋光/MIN_ISLAND 800）
＝**重跑會得到略有差異（更乾淨）的布，逐位重現出貨版用 v38 備份**（已寫進腳本 docstring）。


## 2026-08-24 追記77：皮膚形狀整形 SHIPPED-自驗 ＋ 褌邊六連敗（尺度與目標函數的血價）

### 一、皮膚形狀整形（成功，待 user viewport）

**起點**＝user 用 `NiMark` 紫筆圈出右前大腿一點「附近有點凹凸不平」。定位鏈：
UV(0.121,0.486) → 剛體擬合判 V 軸分支 → 靜止座標 → 全套量測。**該點是凹**，深 0.16~0.26mm、
寬 1~2cm，位於 `sumo_skin_hires_band.py` 治療區（`REGION_FULL=0.10`）的**外緣**——
08-22 的緊膜整平只作用到離布 10cm，這點在 10.22cm。

**新工具 `sumo_form_repair.py`**：沿法線推，凹推出、凸壓入，推量＝偏離「走勢面」多少。
走勢面＝**環狀擬合**（R_HOLE 1.8cm ~ R_TREND 8cm 的環，中央挖空 ⇒ 缺陷不參與走勢；三次多項式）。

**每次跑先過合成測試**：注入已知 0.3mm 凹/凸，同點 A/B 量回應——回收率 100%、方向正確、
對照組 0.000mm。三個都過才准動位移。

**三條契約全綠**（各對應 user 退回「全身整平」時的一個症狀）：
| 契約 | 抓什麼 | 結果 |
|---|---|---|
| 1a 總偏差能量 | 有沒有真的修掉 | 2772→2668（−3.7%）|
| 1b 乾淨面積 | 是不是只是攤開 | 7710→8027（+4.1%）|
| 1c ≥0.40mm 面積 | 有沒有造新缺陷 | 864→821 |
| 2 大尺度形狀 | 有沒有吃掉走勢 | 15cm 低通位移 max 0.023mm（門檻 0.05）|
| 3 法線角分布 | 不規則陰影 | p50 0.886→0.902 / p90 3.000→**2.945** / p99 7.596→**7.453** |
禁區位移 max **0.000000mm**（臉/髮髻/頸縫環/會陰股間/臀縫/乳頭/肚臍/手腳）；UV0・拓樸・權重零改動。

**過程中被契約擋下四次**，每次都是真錯：①測試點選在空中（不在表面，量到 nan）②8cm 二次面
在大腿上包不住（±40° 弧）＝0.3mm 凹被讀成 −1.63mm 模型誤差 ③走勢面被缺陷自己汙染
（只回收 19% 且方向錯）→ 改環狀擬合後 100% ④**擬合噪聲被刻進幾何**（逐點獨立解 ±0.05~0.1mm
散布，直接套用讓 0.05~0.1mm 級面積變多、法線角變寬＝**與 user 描述的症狀同簽名**）→ 改
**面積加權高斯平滑**（不是球內均值——均值在密度不均的網格上被密的一側帶偏，正是 uniform
Laplacian 的同一個病根）。另補一條契約：原本只驗法線 p90/p99，漏 p50；有一版 p50 +5% 照樣過關。

**法線重烘 `sumo_form_repair_normals.py`**：只換動過的區域，其餘逐位保留（遠端誤差 0.0489°
≤ 儲存量化地板 0.0493°＝**對照實驗**證明是 16-bit 壓縮而非混合誤差）。不能直接跑
`sumo_soft_normals_bake.py`（會重算全身）也不能跑 `xfer_v2`（它的前提「帶外幾何與 v23 逐位相同」
被本次整形破壞）。

**布不用重做**（實測）：修復區從離接觸線 3cm 才淡入，而布腳印最寬 2.7cm ⇒ 接觸線 ±3cm 內
30,295 個頂點位移 **0.00000mm**、布網格淨空無退化、零新穿刺。所以保留 v38 那顆布，
也避開「重跑會得到另一顆」的風險。

### 二、褌邊「一抖一抖」六連敗

user 指出褌帶橫過髖部那條邊在抖。六次嘗試**全部失敗或中性**，資產已全部退回。

| # | 方法 | 結果 |
|---|---|---|
| 1 | 拿掉 3mm 搬動上限＋鄰域跟隨 | 3D p99 2.53→2.29（只降 10%）⇒ 上限不是成因 |
| 2 | 單點窮舉（能量只看 ±6mm） | p99→57°、max→127°。在一個尺度優化、另一尺度造 1mm 尖刺 |
| 3 | 全域表面順化（每步重投影） | p50 0.195→0.770＝被壓到**面片二面角下限 0.68°** |
| 4 | 整段 Laplacian 重排 | 3D 尾巴砍半（p99.9 16.3→8.2），但**投影下典型變糙**（p99 中位 2.20→2.50）＝交換 |
| 5 | 單點窮舉＋帶寬項 | 帶寬項單位用 m² 再乘 1e6 ⇒ 壓死角度項；問題點 409→885 |
| 6 | 區段聯合窮舉＋帶寬項（修單位） | 收斂但 3D p99 2.5→**15.0**、投影 p99 中位 2.20→**13.8** |

**最大教訓（六次裡四次栽在這）：優化的目標函數必須就是驗收的量測函數，不能是它的近似。**
每次都能報「全域能量下降 6159」而驗收指標爆炸——因為目標是局部窗的原始角平方和＋手設
權重的帶寬項，量測是弧長重取樣後 band-pass 的轉折角，兩個是不同的函數。

**第二大教訓：量測的尺度要先用 user 看到的圖校準。** 最後在 0.1mm/px 的渲染上一看就發現，
可見的是**邊緣在 2~5cm 尺度的起伏＋帶寬忽寬忽細**，而我的 band-pass 特地把 σ20mm 以上
當「合法彎曲」濾掉 ⇒ **六次都在修一個不在那裡的問題**。鐵則：**尺要先能解釋那張圖，才算對**。

### 三、量測本身的坑（本輪新增，每條都吃過）

- **索引平滑 vs 弧長重取樣**：點距不均時用索引做高斯平滑，平滑點會沿線滑走，製造出
  「98% 的能量在切向」的假訊號。實測弧長等距重取樣後該分量掉到 0.4%。
- **從渲染圖抽邊界有像素量化**：逐列取整數像素＝±0.125mm，在 3mm 弦上就是 **±4.8°** 的假
  轉折角，會把真訊號淹掉。要嘛量 3D，要嘛做次像素邊緣。
- **投影量轉折在切向≈視線時病態**：角度會爆成 100~146°。必須剔除切向與視線夾角 <25° 的段。
- **UV 的 V 軸分支判定用剛體擬合（Kabsch）**：單點距離判過一次，錯了（殘差 0.27mm vs 1.56mm
  才是決定性判準）。
- **`edge_lines.npz` 是舊檔**，沒有任何腳本會寫它——拿它當「現行接觸線」會量到過時的東西。

### 四、流程坑

- **遊戲視窗鎖住 uasset ⇒ 匯入無聲失敗**：log 照印 `Saving Package` 與 `DONE_IMPORT`，
  只有一行 `LogSavePackage: Warning: Failed to move ... to temp directory`，而 .uasset mtime
  不動。這是「殭屍編輯器鎖 umap」的同族。**匯入後必須驗 mtime**。
- `-ExecutePythonScript` 少帶 `-FullStdOutLogOutput` 時整個 python 沒被執行（editor 正常退出、
  exit 0、log 無任何 python 行）。

### 五、現狀

出貨＝**皮膚整形版 ＋ v38 布**；master／FBX／SK_Sumo／SM_Sumo 四者一致。
備份：`v45_preformrepair`（皮膚整形前）、`v46_prefdrerun`（＝現行出貨狀態）。
褌演進史與現行腳本適用性判斷另見 `Docs/FUNDOSHI_HISTORY.md`。
**皮膚整形待 user viewport 驗收；褌邊未解，下一步是先把大尺度（2~5cm）的尺做對。**


## 2026-08-24 追記78：褌邊終解＝我們一直在修一條看不見的線（BUILT-自驗，viewport 待驗）

**定罪**：`SINK_EDGE=0.8mm` 把布緣（腳本裡叫「接觸線」的那圈）壓在皮膚**底下**——
實測 3706/3706 個布緣頂點帶號距離全等於 **−0.800mm**、露出皮膚的 **0 個**，整片牆
（−0.8→−1.5mm）一起埋著。**玩家看到的黑／膚交界是另一條曲線**：布頂面穿出皮膚的
`h=0` 等值線，位在布緣內側 d\*=1.18~2.44mm（p50 1.39），且 **d\* 由局部帶寬決定
（corr 0.916**；35mm→1.26mm、>90mm→2.41mm。解析式 `d*=SINK/剖面在緣的斜率`，
斜率隨帶寬從 0.95 掉到 0.37）。管線對它零處理：沒切出來、沒平滑、沒閘門——
而**要量的資料早就在出貨閘的 `top vs skin` 裡，只是算完就丟**。

**為什麼六天沒人發現**：它叫「接觸線」。**鐵則：對 user 用眼睛驗收的東西，動手前先證明
「我量的那條線＝他看的那條線」；命名相符不是證明，帶號距離或渲染才是。**

**修法**＝把專案對布緣的配方原封不動搬到可見線，**布緣降級為致動器**（看不見，唯一職責是
保持被埋住）：抽 `h=0` 等值線（plate 上 marching triangles；h=0 時頂點恰在皮膚上⇒直接用
skin_co 內插）→ 弧長等距 1mm → σ15mm FFT 循環卷積 → 位移上限 → 經 KD 最近點反送到布緣＋
鄰圈 smoothstep 衰減 8mm → 重投影皮膚 → A3 同款鬆弛 → **場全重算、等值線重抽**，迭代 3 輪。
掛在 `ARC_XFIT`（預設 0＝行為與舊版逐位相同）；B/C/D 抽成 `compute_fields()` 才能迭代。
儀器＝新增 `Tools/AssetPrep/fundoshi_visible_edge.py`（唯讀）＋腳本內建 `GATE visible`，
兩者吻合（p50 0.208 vs 0.209）。

**成績（獨立儀器；出貨 v38 → XFIT r3）**
| 量（可見線） | v38 | r3 | |
|---|---|---|---|
| 1.5~5cm 側向 p50 / p90 / p99 | 0.208 / 0.684 / 1.151 | **0.164 / 0.511 / 0.986** | −21% / −25% / −14% |
| <1.5cm 側向 p90 | 0.094 | 0.054 | −43% |
| 轉折角 弦6mm p90 / p99 | 3.623° / 6.835° | **2.343° / 4.011°** | −35% / −41% |
| 可見帶寬 p50 / 1.5~5cm 起伏 std | 43.4mm / 0.55·0.50·0.54 | 43.5mm / 0.52·0.43·0.52 | 持平 / −5~13% |
| 法向 1.5~5cm p90 | 0.520 | 0.519 | 持平 |

水密 ✓、翻面 757→747（未增）✓、布緣仍全埋（max −0.799）✓、禁畫衍生遮罩同源重生 ✓。
**代價**：`GATE ring0` 二面角 p50 1.12→1.69、>15° 面 129→300＝埋在皮下 1.4mm 窄帶的
三角形品質（正是 3mm 上限原本在保護的），看不見但會參與可見線頂點的法線平均——待 viewport。
收斂未到底（p90 0.674→0.574→0.537→0.512），停在 3 輪。

**順帶結案兩件**：①**那五條「v38 後未生效的改良」對可見線毫無作用**（純重跑 p90 0.684→0.674）
②log 的 `contour σ6 smooth move p90 3.00` ＝整條頂在上限 ⇒ **布緣連自己那道 σ15mm 平滑都沒
真的吃到**，3mm 上限正是 1.5~5cm 帶的 binding constraint（追記77 第 1 次拆上限只降 10%，
是因為當時量的是 mm 級尾巴＝**同一刀在對的尺子下是有效的**）。

**副產物**：可見線串環比布緣乾淨——它是真等值線，交點分支度恆 2、5604/5604 串成 3 條閉環；
布緣 rim 圖有分支度 3/4 節點，串出 22 段碎片。

**流程坑（新）**：PowerShell `Start-Process -ArgumentList` 傳 `-ExecutePythonScript=<含空白路徑>`
⇒ 參數被空白切斷、`LogPython: Error: Could not load Python file`，但 **exit code 仍 0、
uasset mtime 不動＝無聲失敗**。解法＝腳本先複製到無空白路徑再跑；**匯入後一律驗 mtime**。

**出貨狀態**：master／FBX／SK_Sumo／SM_Sumo 同批更新（03:45~03:47，骨架維持 SK_Sumo_Skeleton）。
退路＝`masters/sumo_character_master_v47_prexfit.blend`（與 v46／出貨逐位相同）。

### 追記78 補（同日稍晚，user 授權「依你的直覺去完善」）

驗收通過後我把自己列的七條殘留逐條處理，四條有結果、三條判定不動：

**① 可見線的著色（此前只量形狀，沒量明暗）＝我的擔心成立，已修**。新增量測：沿可見線內插
自訂 split normal，量相鄰 1mm 的法線夾角。第一版 XFIT **著色尾巴變糟**（p99 2.203→3.108°、
max 7.59→28.90°）——ring0 二面角劣化確實傳到了看得見的地方。
真因＝**位移的指派抖動**：每個布緣頂點各自 KD 找最近的可見線取樣點，對應在帶子變窄處會跳，
相鄰頂點拿到不連續位移 ⇒ 細長三角形。修法＝**位移場先沿布緣環 σ4mm 平滑再套用，且改用
Dijkstra 已算好的測地對應（`nearest_c`）而非 KD 最近點**。
結果：ring0 >15° 面 300→**157**（基準 129）、翻面 707（比基準 757 還少）、
著色 p99 3.108→**2.485**、max 28.90→**12.11**，而形狀成績零損失（p90 0.512→0.514）。
**殘留＝仍比基準差 13%（p99 2.485 vs 2.203）**；加大鬆弛輪數 6→14 **完全沒差**
（p99 2.533→2.485）⇒ 照既有鐵則「調參無邊際效果＝參數不在病因鏈」停手。
判定為**結構天花板**：可見線活在 1.4mm 窄帶裡而網格邊長 3.8mm，那條帶在網格上沒有解析度；
要真解得細分該帶（另一個構造改動，另案）。

**② 姿勢穩健性（最大的未測表面）＝契約 PASS，且是構造保證**。新工具
`fundoshi_pose_check.py`：直接擺姿勢、用 evaluated mesh 量布緣相對變形後皮膚的帶號距離。
rest／走路 大腿±25°／大步±45°／蹲踞 60°+外開20°／軀幹前彎35°+扭25°／**Jiggle 五骨全開 15°**／
複合 大腿40°+軀幹30°——**七個姿勢的埋深 max 全部＝−0.798mm，刺出皮膚 0 個**。
埋深對姿勢不變到小數點後三位 ⇒ 布與皮膚權重一致到 LBS 原樣搬移偏移量。

**③ `GATE visible` 升級成真契約**：`assert p90 <= ARC_VIS_P90_MAX`（預設 0.60mm＝達成值
0.514 加餘裕；出貨前基準 0.674）。同時 **`ARC_XFIT` 預設 0→3**＝腳本預設就重現出貨組態
（設 0 復原 08-24 前行為）。此前它只印一行就放行＝空洞契約，正是本專案吃過最多虧的形態。

**④ 儀器對布緣的輸出是壞的（會害下一個人）＝已修**。`fundoshi_visible_edge.py` 原本用頂面邊
猜布緣連通性，撿到跨窄帶的邊與 snapped 切點，串出 22 段碎片、轉折角印到 179° 還照印。
改用**牆面推導**（牆的第一個三角形恰為 `(a,b,底b)`，兩個頂面頂點就是輪廓邊）＝精確解：
現在串成 `[1571,1072,1063]`＝與腳本自印的輪廓完全一致、3706/3706、分支度異常 0；
並加了不完整時的明文警告。

**判定不動的三條（附理由）**
- **禁畫遮罩比可見布寬 1.2~2.4mm**（它從布緣衍生，不是從可見線）。換算＝**0.73~1.51 個遮罩
  像素**（4096 圖 0.617px/mm），已在量化地板上；而改它會動到 `fundoshi_mask_final.png`，
  連帶 C1 臉管線的金樣本對賬。**成本大於收益，記帳不改。**
- **帶寬配對仍是「最近非鄰近點」**（迴轉處會抓到自己、搜不到 fallback 90mm）。今天無害
  （它只經 d\* 影響可見線 0.055mm），但**它也決定布的鼓度**——動 `ARC_WIDEN_MM` 時是地雷。
- **可見帶寬比手繪遮罩窄 2.4~4.9mm 且隨帶寬變**（d\* 兩側各吃一份）。改遮罩不會 1:1 反映到
  畫面，這是已知偏差，記帳供未來調形狀時扣除。

**已排除的疑慮**：這道平滑不是棘輪——腳本每次都從手繪遮罩重切布緣，不是從上一顆布長出來；
純重跑與出貨 v38 幾乎逐位吻合即為證明。周長 −0.39% 是一次性固定代價。

出貨：master／FBX／SK_Sumo／SM_Sumo 同批更新（04:11~04:12）。

## 2026-08-24 追記79：現況體檢「斑點／凹洞」——靜止乾淨，動起來才出事（唯讀調查）

user 問「現在人物身上會不會有奇怪的斑點或凹洞」。**只做量測，未動任何資產。**

### 一、斑點（貼圖側）＝目前是關掉的
- `T_BodyChroma` 出貨內容＝`body_chroma_neutral.png`（**32×32 全等 32767**，uasset 內
  import 紀錄實查）⇒ 追記74 定罪的「血色場塊斑」現在完全平坦、畫不出斑。
- `T_FundoshiEdgeShadow`＝`fundoshi_edge_shadow_neutral.png`（中和）；`T_FundoshiMask`＝
  `fundoshi_mask_final.png`（正確版）。皮膚材質無任何烘焙 AO／陰影貼圖（#37 鐵律仍成立）。
- **殘留地雷**：`Tools/AssetPrep/ue_import_sumo.py:96-97` 仍匯入 `fundoshi_mask_sharp.png`
  與**未中和的** `fundoshi_edge_shadow.png` ⇒ 誰重跑一次匯入，追記74 的兩個修就無聲回退。
- 代價記帳：血色斑駁（SPEC #37 的設計）目前等於下線，「無硬邊血色場重烘」仍是待辦。

### 二、凹洞（幾何側）＝靜止 sub-mm，**姿勢下翻倍**
`sumo_bump_scan.py` 對現行 master 重跑（自檢兩條 PASS）：外露皮膚、大尺度平坦區
|amp| p50 0.027 / p90 0.126 / p99 0.388mm、≥0.3mm 922 個頂點。與 08-23 21:46 那次相比
（追記77 的整形動了 22,576 頂點、最大 0.58mm）：p90 0.220→0.212、≥0.3mm 5724→5596＝
**真的有改善但只有 3~4%**，量級與追記77 自己報的 −3.7% 一致。

**新量的（此前全專案只在 rest pose 上量過）**：把同一把尺搬到 evaluated（擺過姿勢的）網格上——
| 姿勢 | 平坦區 p90 | ≥0.3mm | 相對 rest 最壞點 |
|---|---|---|---|
| rest | 0.126 | 922 | — |
| 走路 大腿±20° | 0.181 | 1673 | +1.44mm |
| Jiggle 五骨 15° | 0.206 | 2190 | +2.85mm |
| 大步 ±45° | 0.219 | 2427 | +1.69mm |
| 軀幹前彎 35°＋扭 25° | 0.228 | 2873 | +2.90mm |
⇒ 站著磨到玻璃級的那批數字，**一動起來就被蒙皮吃掉一半以上**。追記74 入庫的
`sumo_weight_smooth.py`（權重稜平滑）在追記75 被連同全身整平一起退回，之後沒有回來過。

### 三、真正的新發現：**布緣在動的時候會刺出皮膚**（黑色布牆露在大腿前側）
`SINK_EDGE=0.8mm` 只有 0.8mm 餘裕，而布緣的權重是沿皮膚三角形內插、皮膚表面在 LBS 下是
「先變換再內插」，兩者在姿勢下發散。實測（兩種獨立量法互證：BVH 最近點 vs **rest 重心座標
對應**，走路 ±20° 得 48 vs 47、Jiggle 8° 得 38 vs 34）：
| 姿勢 | 刺出皮膚的布緣頂點 | 最大 | 位置 |
|---|---|---|---|
| rest | **0 / 3706** | −0.798 | — |
| 走路 大腿±15° | 24 | +0.33mm | 股間背側（多半被腿擋住）|
| 走路 大腿±20° | 47 | +0.87mm | 同上 |
| **Jiggle_Belly 繞X 10°** | **26** | **+0.87mm** | **右大腿前側（正對鏡頭）** |
| **Jiggle_Belly 繞X 25°（引擎鉗位）** | **236** | **+2.48mm** | **兩側大腿前側** |
| 軀幹前彎 35°＋扭 25° | 337 | +3.00mm | 大腿前側為主 |
**單骨拆解定罪＝`Jiggle_Belly`**（Chest_L/R 各 0 個、Butt_L/R 各 18/3 個且都在背面）。
25° 就是引擎鉗位本人（`NiceInkCharacter.cpp` `MaxRollRad = 0.44f`），而肚骨頻率 2.1Hz
貼著步頻 ⇒ 走路時它一直在被激勵。**症狀預測＝走路／晃動時大腿前側沿布邊冒出黑色碎點或
鋸齒邊，站著不動就沒有。**（此為 Blender LBS 量測，引擎端影響骨上限可能略有差異；
**是否看得見仍以 user viewport 為準**。）

### 四、為什麼追記78 的「姿勢穩健性 PASS」沒抓到＝**又一個空洞契約**
`fundoshi_pose_check.py` 只寫 `rotation_euler`，**沒切 `arm.data.pose_position`**——
master 出廠是 `REST`，而且 pose 裡還躺著一組 DrawPose（9 根骨非單位）⇒ **七個姿勢量到的
全是 rest 網格**，這就是「埋深全等 −0.798mm」的真正原因；當時把它解讀成「LBS 構造保證」。
**已修**（本次唯一的寫入）：①切 POSE ②`matrix_basis.identity()` 清殘留 DrawPose（清完與
REST 逐位相同＝基準乾淨）③每個姿勢都驗「網格真的動了」的下限 `MIN_MOVE_MM=5.0`
④「軀幹前彎」原本轉 `Hips`＝整具剛體旋轉、相對形變恆為零＝第二個空洞測試，改轉 `Spine`
⑤加入 Jiggle_Belly 10°/25° 兩列。修好後**契約 FAIL：最壞 +2.994mm**。
鐵則再犯記帳：**「不該動」的契約必須同時驗「該動的真的動了」**——這條在 08-16（酒瓶）
已經用血買過一次，這次換成姿勢沒擺、數字漂亮。

### 五、建議的下一刀（未做，等 user 裁決）
推薦**改權重、不要改布**：`Jiggle_Belly` 的蒙皮權重域一路吃到大腿前側（單骨測試證明），
肚子的彈簧本來就不該讓大腿變形——沿鼠蹊摺以下把它淡出（08-05 已對背側做過同型手術
＝繞背 13%），可以同時修掉「布緣刺出」與「大腿前側動起來變粗糙」兩個症狀。
反對加深 `SINK_EDGE`：可見線位置 `d* = SINK / 剖面斜率`，0.8→3mm 會讓可見布邊各邊內縮
4~9mm＝直接改掉 user 剛驗收過的布寬。
儀器：`fundoshi_pose_check.py`（已修，現在會 FAIL）、`sumo_bump_scan.py`（RENDER=1 出四視角熱點圖）。

### 追記79 補（同日稍晚，user 授權「完整編輯器控制權」）：斑／凹洞的引擎內定罪

user 用 NiMark 圈了兩點（`Saved/ni_marks.txt`）＋「背腰部有很多斑，沒法標註」。

**一、圈選點的定位與量測**（圈2 可靠、圈1 存疑）
UV 分支用**成對距離指紋**選（UV0 有重疊分支＝追記77 血價）：圈2 → 右臀/髖背
(−0.403,+0.403,+0.862) 內部殘差 2.2mm（次佳 8.9mm）；圈1 的筆劃跨 UV 接縫，最佳解
右大腿上前 (−0.488,−0.164,+0.699) 殘差 2.3mm，但**兩圈合起來對不上同一個剛體
（殘差 22mm，含尺度也一樣）⇒ 圈1 的定位不敢說死**。
兩圈量下來都是**圈內比周圍還乾淨**：圈2 幾何 |r| p90 0.087mm vs 環外 0.270mm、
著色 0.14° vs 0.21°；圈1 0.236 vs 0.405mm。**他圈的地方在網格上沒有東西。**

**二、材質側全部排除（離線讀出貨參數）**
`SkinDetailAmount=0`／`SkinNormalStrength=0`／`SkinRoughVariation=0`（斑駁/細節/
平鋪法線三層全關）、`T_BodyChroma`＝32×32 純色、`SkinTone` 是單一顏色
⇒ **皮膚材質對空間沒有任何變化，斑只能來自 N·V**。

**三、引擎內 A/B 定罪（新儀器 `Tools/RoboTest/robo_skin_spots.py`，11 機位×多變體）**
猛藥對照組＝`HeadlightFloor=1 / HeadlightPower=0`（假光全關）；空對照組＝`ChromaStrength=0`。
皮膚區域局部亮度起伏（σ25px 高通／局部均值）：
| 機位 | 出貨 | 假光關 | 降幅 |
|---|---|---|---|
| 躺姿俯視 | 2.916% | **0.708%** | −76% |
| 躺姿髖掠射 | 2.795% | **0.677%** | −76% |
| 站立胸前 | 1.490% | **0.321%** | −78% |
| 站立胸前掠射 | 1.343% | **0.268%** | −80% |
| 站立上背掠射 | 1.703% | **0.449%** | −74% |
| 站立腰背 | 0.719% | **0.270%** | −62% |
空對照組 `ChromaStrength=0` 與出貨差 **0.4/255＝TAA 噪聲**（2.878% vs 2.875%）。
假光關掉後整具身體＝**一片純色剪影**（`Saved/Headlight/flat_highpass.png` 全灰）。
⇒ **「斑」與「凹洞」是同一件事：幾何法線的抖動，被頭燈假光放大成明暗。**
**不是貼圖、不是血色場、不是斑駁層（本來就關著）、不是墨、不是後製。**

**四、唯一的例外：躺者胸口一條細黑線，假光關掉仍在** ⇒ 再拆一層：把
MarkerRT/TattooRT/MistRT 換成白圖後**線消失**（線框內高通 p1 −14.8 → −0.9），
再關 FaceTex 零變化 ⇒ **那條線是墨（該玩家身上的既有刺青），不是缺陷**。

**五、兩把尺對得上（這才算定罪）**
離線量的自訂法線粗糙度：胸前 60.4% / 上背 38.5% 的頂點 >1.5°，腰背 8.3%、肚 4.1%、
大腿前 0.3%、臀髖 0.1%。換算到畫面：2.9% 亮度起伏 ⇒ ~4° 法線偏差 ⇒ 1cm 尺度上
~0.3mm 的高度漣漪——與 `sumo_bump_scan` 量到的 |amp| p90 0.13~0.21mm 同一個量級。
**離線幾何與引擎像素兩把獨立的尺，指到同一個數字。**

**六、為什麼是胸和上背**
08-22~24 的三批整平（`sumo_skin_hires_band` REGION_FULL=0.10／`sumo_skin_band_fair`／
`sumo_form_repair`）**作用域全部是「離褌接觸線 10cm 內」＝髖與大腿**。所以現在最乾淨的
就是那兩區（0.1~0.3%），而**胸與上背從掃描重拓樸上線以來沒有被治療過**，殘留的掃描
噪聲原封不動。user 圈的兩點落在最乾淨的區域，看到的是同一個效應被**掠射角**放大
（貼皮視角下 0.3mm 起伏＝可見的明暗）。

**下一刀（未做）**＝把 `sumo_form_repair.py` 的治療域從「離褌 10cm」擴到胸／上背／
軀幹全域，沿用它現成的三條契約（總偏差能量↓／乾淨面積↑／大尺度形狀不動 <0.05mm）
＋禁區清單，再用本檔的 A/B 量「出貨 vs 修後」的亮度起伏當驗收。
鐵坑（本輪新踩）：**3 客戶端 PIE ＝ VRAM OOM 崩潰**（2 客戶端才穩，站立者拍主機自己＋
關 BowBody 的 OwnerNoSee）；**HighResShot 是下一幀才抓，同拍改材質會讓檔名與內容錯開一格**
（第一輪 `_v0def` 檔裡其實是 flat 畫面，靠「nochroma 應該等於 def」這條自檢抓到）。

### 追記79 補二：全身「斑」普查儀 `sumo_spot_census.py`（唯讀，自檢兩條全綠）

user：「先用一套儀器量出所有斑」。**只量不修。**

**量的量**＝`rough(v)=angle(自訂 split normal, 拓樸 2 環平均)`——因為引擎 A/B 已證斑＝法線
抖動；鄰域用拓樸環不用半徑球（半徑球在肚下緣/股間/乳下會把對面那片皮膚平均進來＝假訊號 p50 32°）。
**換算畫面**：材質實值下最敏感視角 θ=54.7°，`|ΔB/B| = 0.98% / 度`，所以報表同時給「度」與「亮度差 %」。
**門檻**＝治療過的髖/大腿（離布 2~8cm、user 從沒抱怨過的區）p99 ＝ **0.854 度（0.84% 亮度差）**；
左半校準、右半驗證（同一塊校又驗＝套套邏輯）。

**自檢兩條（BOTH PASS）**
- 下限：乾淨底注入 1/2/4/8 度 → 量回 0.80/1.54/3.04/6.04，**回收率 0.757、線性 R²=0.99980**
  （報表的度 ÷0.757 ≈ 真實傾角）；真實場注入 1 度 → 峰值 0.92 度 > 門檻 0.85 ⇒ 抓得到。
- 上限：held-out 對照組（右半治療區）命中率 **1.02%**。
兩個第一版都是錯的、被自檢擋下＝**①線性度必須在乾淨底上量**（角度是向量疊加，在有噪聲的真
實場上「峰值減基準」會被壓成非線性 R²=0.93，我原本會誤判儀器壞掉）**②分群必須用最陡上升
分水嶺、不能用連通分量**（門檻掃出 7.5% 皮膚時整片胸腹連成「一塊 129cm 的斑」＝把地圖當清單）。

**普查結果**：可見皮膚 38,627 cm²，**斑 426 塊、總面積 8,647 cm² ＝ 22.4% 的可見皮膚**。
| 部位 | 面積佔比 | p90(度) | 部位 | 面積佔比 |
|---|---|---|---|---|
| 胸背中 | **88.5%** | 2.66 | 髖前 | 9.1% |
| 腰前 | **53.2%** | 3.85 | 大腿下背 | 7.0% |
| **小腿前** | **48.4%** | 2.04 | 大腿下前 | 3.8% |
| 胸背 | **47.4%** | 2.15 | 大腿上前 | 2.1% |
| 小腿背 | 37.0% | 3.59 | 大腿下側 | 1.7% |
| 胸前 | 36.4% | **4.09** | **髖背** | **0.8%** |
| 腰側 | 30.0% | 2.97 | | |
| 腰背 | 27.4% | 1.74 | | |
最大一塊＝胸前中 150 cm²／直徑 17cm／峰值 8.4 度＝**8.2% 亮度差**；第二 胸背中 77 cm² 峰值 10.4 度。
**新情報：小腿也很髒（前 48%／背 37%）**——user 只提過背腰，小腿是量出來才知道的。
乾淨的正是三批整平做過的髖/大腿（0.8~2%）＝**治療有效、只是作用域太小**，這也是門檻的可信來源。

產出：`Saved/SpotCensus/spots.txt`（426 塊全表，含 UV＋3D 座標可回查）／`spots.npz`／
`census_{front,back,left,right}.png` 熱點圖。解剖摺痕另列不計入總帳。
**未涵蓋**：頭臉（z>1.28，另有臉管線）、手腳掌、布底下的皮膚。「高度 mm」欄只是指示值。

**追記79 補二的自我更正（同回合，出圖時抓到）**：那張分區排名有**密度混淆**，不可當嚴重度倍數。
`rough` 的鄰域是「拓樸 2 環」＝細網格上 10mm、粗網格上 26mm，而門檻是拿**細網格區**校的
⇒ 對粗區系統性高估。加上邊長欄後關係一目了然（單調）：
| 部位 | 斑佔面積 | 邊長 |
|---|---|---|
| 髖/臀背（治療過） | 0.9% | **4.1mm** |
| 大腿上前（治療過） | 2.0% | 7.4mm |
| 胸背中 | 88.5% | 11.5mm |
| 胸前 | 36.4% | 13.7mm |
| 小腿前 | 48.4% | **16.9mm** |
⇒ **「哪裡髒」與「網格多粗」是同一條曲線**。合理的重新表述：13~17mm 的三角形在有弧度的地方
根本沒有解析度可以平順地表達 1~5cm 的形狀，**粗三角形＋弧度＝刻面＝斑**；治療過的帶子乾淨，
是因為它同時被**細分**（`sumo_skin_hires_band`）**又**被整平。所以下一刀不只是「把整平擴大」，
很可能要先補密度——**這是假說，未驗**；決定性實驗＝只對胸/上背做平滑細分（形狀不變）再重量，
若 rough 大幅下降＝刻面定罪。
試過的死路：改用固定 15mm 物理半徑鄰域——粗區球內只有 4~6 個鄰居，量出「肚子 p50 10.4°」
這種明顯錯的數（該區目視乾淨）。像素側量法也失敗：σ22px 高通在 2.6cm 尺度上被真解剖形狀
主導（治療區反而比上背高）＝**尺沒有把「真形狀」與「缺陷」分開**。
出圖：`Saved/SpotCensus/SPOT_SHEET.png`（四視角＋前 24 名編號＋色階圖例）。

### 追記79 補三：user 判定「儀器不對」——改成讓他自己驗（`NiSpotMap` 遊戲內疊圖）

user：「我在圖中看到的也是灰色，所以代表你的儀器並不對」。**接受。**
儀器會瞎的結構性理由：它量的是 **Blender 主檔**，而畫面是 GPU 著色的結果，中間隔著
匯入量化／切線基底重算／三角形內插至少三層，任何一層都能在「主檔上乾淨」的地方生出斑。
（順帶排除：`bUseHighPrecisionTangentBasis` 在 SK_Sumo 的建置設定裡有序列化，量化不是首嫌，
但也沒有被證明無罪——因為我根本沒量過引擎那一側的法線。）

**做法＝把尺搬進他的視野**（褌邊六天的鐵則：先證明「我量的線＝他看的線」）：
- `Tools/AssetPrep/sumo_spot_bake_uv.py`：把普查結果烘成 UV0 貼圖 `SourceAssets/spot_map.png`
  （紅＝超標、透明＝乾淨；2048²，覆蓋 17.6% 紋素）→ `/Game/Characters/Debug/T_SpotMap`。
- `UNiceInkGameInstance::NiSpotMap(int32)`（Exec）：把該圖塞進每個角色的 **MarkerRT 參數**
  ＝當成墨層貼上身，**材質零改動**；`NiSpotMap 0` 還原真墨層。
- 自驗 `Tools/RoboTest/robo_spotmap_check.py`：ON 與 OFF 前後三連拍——
  **ON vs OFF 差 mean 2.762 / p99.9 145（疊圖確實生效）；OFF前 vs OFF後 mean 0.398 / p99.9 3.0
  （還原乾淨）**。不驗過不交給 user，因為他要拿它去核對自己的眼睛。

**接下來的判讀規則（三種結果各代表什麼）**
① 他看到的斑**被塗紅** ⇒ 尺是對的、位置也對，直接進修復。
② **沒被塗紅** ⇒ 尺瞎在引擎側，下一步＝改用「出貨 ÷ 假光關」的**比值影像**當尺
（已在既有 11 組截圖上驗過可行：比值把貼圖/墨/膚色/環境全約掉，只剩頭燈項＝法線的函數，
量到的起伏 p90 0.5~1.1%、p99 0.8~2.0%），在他的機位上做偵測。
③ **紅得到處都是、他只看到幾塊** ⇒ 門檻太鬆，用他指的那幾塊重新校門檻。
出貨狀態：ini 已清、play 模式已切回 party、editor target 編譯通過。

### 追記79 補四：**儀器量錯了法線——引擎匯入時就把主檔法線丟掉了**（根因，user 打回三次後找到）

user：「你有自己量過我標記的地方在你的量測下是呈現什麼顏色嗎？」→ 逐點量：**灰**（圈1 0/74、
圈2 0/58 個筆劃點超標）。他看得見而尺是灰的 ⇒ 尺錯。往下追到底：

**根因**＝`Tools/AssetPrep/ue_import_sumo.py:37`
```
skd.set_editor_property("normal_import_method", FBXNIM_COMPUTE_NORMALS)   # 法線=重算
```
（08-05 為了「SK/SM 法線同源」而定的）⇒ **引擎把主檔的自訂 split normal（柔化法線轉印）
整份丟棄、自己從幾何重算**。而我的普查從頭到尾讀的就是那份被丟棄的柔化法線——
**一份沒上畫面、而且本來的職責就是「把幾何缺陷抹掉」的資料**。等於拿修圖後的照片去找瑕疵。

**換成引擎真正在用的法線後，同一個座標的答案就變了**（3D 距離守衛，UV 圓盤會跨島）：
| | 柔化法線（舊尺／已被引擎丟棄） | 引擎重算法線（真正上畫面） |
|---|---|---|
| 圈1 圈內 30mm | p90 0.395 / max 0.461 / **超標 0**/142 | p90 0.594 / **max 2.212 度＝2.17% 亮度差** / **超標 5**/142 |
| 圈2 圈內 25mm | p90 0.121 / max 0.163 / 超標 0/165 | p90 0.277 / max 0.511 / 超標 0/165 |
⇒ **圈1 從「什麼都沒有」變成「峰值 4.8 倍、5 個頂點超標」**；圈2 仍乾淨（對照環反而更粗），
它還沒有解釋。

**儀器已修**（`sumo_spot_census.py` 改讀重算法線，舊值保留對賬）。全身重量：
門檻 1.314 度（對照組 p99；舊尺 0.854）、斑 **706 塊 / 7,311 cm² ＝ 18.9% 可見皮膚**。
自檢 BOTH PASS，並新增一條必印的公開數字：**偵測底線 1.69 度（1.65% 亮度差）以下抓不到**
——儀器的盲區要是公開數字，不能預設它什麼都抓得到。

**那個座標上還查了什麼（全部排除，附證據）**：邊長/valence/三角形細長比/二面角/split normal
份數/UV 縫份數/UV 紋素密度與其鄰域變異/蒙皮影響骨數與最大權重/離布距離——**沒有一項是極端值**
（全在全身中位數上下）；`T_FundoshiMask` 做了資產級 A/B（換全黑→還原三連拍）：
**A vs B mean 0.137 ＝ 與「A vs 還原」的 0.138 同級＝TAA 雜訊 ⇒ 那張遮罩在皮膚上什麼都沒畫**；
`body_cloth_normal` 在兩點都是平的 (127,127,255)；edge shadow 中和；chroma 純色；
face/hair mask 為 0；墨層在皮膚上是**均勻**一層（扣掉整體位移後 std 1.79/255）。
另外查掉一條假線索：`sumo_skeletal_lo/hi/標準` 三個 FBX **md5 完全相同**＝不是「渲染低模」。

**鐵則（本專案再犯一次的形態）**：**尺要量 GPU 拿到的那份資料，不是作者檔裡的那份。**
同族血價＝褌邊六天量在看不見的接觸線上（追記78）。差別是這次連「資料有沒有進引擎」都沒查。

### 追記79 補五：兩個圈都不是凹陷——是「斜面」；而且它比我們的整平還老

user：「是暗塊還是凹陷你量不出來嗎？」——量得出來，判準是：
**凹陷**＝高度相對走勢面下沉＋亮度呈偶極；**暗塊**＝高度不變、整片法線一致偏開鏡頭、亮度單調。
（法線一律用引擎重算那份。）

| | 高度殘差（噪聲） | 正對 | 斜視 55° | 判讀 |
|---|---|---|---|---|
| 圈1 | +0.054mm（0.510mm）| **+5.5%** | **−6.29%，94% 頂點偏暗** | **不是凹陷＝斜面** |
| 圈2 | +0.116mm（0.310mm）| +1.9% | +0.92%（±混雜，43% 偏暗）| 不是凹陷；無一致訊號 |

⇒ **圈1 是一片被翻了角度的皮膚**（約 6.4° 的傾斜）：正對看比周圍亮、斜看比周圍暗 6%——
「換個角度就不見了」正是斜面的簽名。這也解釋了為什麼我先前所有「相對鄰域」的尺都抓不到它：
斜面在局部是**平的**，它只是整片偏了個角度。**下一版儀器必須加一條「相對走勢面的傾角」指標，
不能只有局部粗糙度。**

**逐版對賬（同一塊皮膚，六個備份）**：現行 −6.29% ／ v47 −6.29 ／ v45 −6.32 ／ v38 −6.32 ／
v37 −6.32 ／ **v36（所有整平之前）−7.44%、97% 偏暗**。
⇒ **這片斜面不是我們做出來的，它從掃描重拓樸就在；我們的整平還讓它輕微變好（−7.44→−6.29）。**
圈2 至今無法重現 user 看到的東西——三種角度、兩種法線、所有貼圖層都查過。

### 追記79 補六：圈2 終於量到了——它不是缺陷，是假光的「地板值」

在跑起來的遊戲裡用 `ResolveUVToWorld` 把 user 的兩個 UV 轉成世界座標（新儀器
`Tools/RoboTest/robo_marklook.py`，六個機位×出貨/假光關）：
- m1 uv(0.11341,0.50206) → world (412.6,−10.3,55.5)、表面法線 (0.49,−0.83,0.27)＝**朝上朝外**
- m2 uv(0.18297,0.19234) → world (394.3,50.7,62.2)、**比受害者身體中心低 37cm、法線朝下 z=−0.71**
  ⇒ **m2 在躺姿身體的「轉過去那一面」**。

而頭燈假光有地板：`B = 2.0*(0.55 + 0.45*(N·V)^1.5)`
| 入射角 | 0° | 45° | 60° | 75° | 90°+ |
|---|---|---|---|---|---|
| 亮度（相對正對） | 100% | 82% | 71% | 61% | **55%（地板，之後不再變）** |
⇒ **凡是轉離鏡頭的皮膚，本來就比朝上的肚子暗到 45%**。這個量級是我一路在追的缺陷
（2~6%）的**七到二十倍**。而我的尺（不論哪一版）都是「**扣掉大尺度走勢之後**的局部偏差」——
把大尺度形狀當成合法背景減掉，正是它的設計。**所以 user 圈的那塊，剛好就是我的尺
按照設計會丟掉的東西。**

**結論**：圈2 不是斑點也不是凹洞，是身體轉過去的那一面在假光地板值下的正常亮度。
要改的話旋鈕是 `HeadlightFloor`（現 0.55）：調高＝轉過去的面不會那麼暗（身體變扁），
調低＝對比更強。**這是口味，屬 user viewport 裁決，不是缺陷修復。**
圈1 則是真的有東西：約 6° 的斜面、掃描時代就在（v36 −7.44% → 現行 −6.29%）。

**鐵則沉澱**：**「使用者說的斑」與「儀器定義的斑」可以是兩個不同的東西。**
我花了整整一輪把每一層資產都排除，卻沒有先問「他看到的暗，會不會根本是形狀本身」——
因為我的尺從第一行就把大尺度形狀減掉了，那個假設從來沒有被拿出來檢查過。
下次遇到「量不到」時，**第一件事是把尺刻意丟掉的那些量拿回來看一遍**。

### 追記79 補七：「筆跡卡住」——同一個根因，而且是無聲失敗

user 補了關鍵症狀：「為什麼我的筆跡在那裡會感覺卡住？」——這把整件事翻過來：他圈的不是
「看起來怪」，是**畫不過去**。

**機構（程式裡明寫）**：`bCursorDrawable`＝游標落點出了可畫域就**抬筆**
（`NiceInkCharacter.cpp:2676`；鎖定後用橢圓、未鎖用活解算 `bDrawTipReachable`）。
落墨另有一條：射線命中點離皮膚 >1.5mm 就整筆不落（`ResolveAimToTargetUV`，
「打在褌上不落墨」的物理遮擋規則）。游標在邊界上來回 ⇒ 墨斷斷續續 ⇒ **手感就是「卡住」**。

**為什麼偏偏是圈2**：實測它的世界座標比受害者身體中心低 37cm、表面法線朝下
⇒ 它在躺姿身體**轉過去的那一面**。那正好同時是：
① 假光地板值區（比朝上的肚子暗 45%）＝他看到的「暗」
② 手臂搆得到的極限＝墨閘會關掉的地方＝他感覺到的「卡」
**兩個症狀是同一件事：那塊皮膚在你看得到／搆得到的邊界上。**

**而回饋在那裡剛好是瞎的**（無聲失敗，本專案鐵律第 2 條）：
- 灰紗（可畫域）**在稜線掠射角會被透視壓成看不見的細縫**——這條程式註解自己寫著
  （`NiceInkHUD.cpp` 附近，「腳掌實錘」）。
- 補位的筆尖紅 ✕ 與「out of reach」提示行**都只在 `bLeanLocked` 分支裡畫**
  （`NiceInkHUD.cpp:724` / `:1505`）。**沒鎖定時，搆不到＝墨無聲停止，畫面上零解釋。**

⇒ **結論：圈2 不是缺陷，是可畫域邊界；真正的缺陷是「邊界在那個角度看不見、而且沒鎖定時
連補位提示都沒有」。** 這是 UX bug，不是幾何 bug。
**下一刀（待 user 裁決）**：①未鎖定時也畫筆尖 ✕／提示行 ②稜線處的可畫域改用不依賴
透視的記號（筆尖級或螢幕級），不要只靠皮膚上的紗。

## 2026-08-25 追記80：可畫域灰紗 21.6 秒 → 0.11 秒（UV 網格索引；SHIPPED-自驗）

**起點＝user 一句問話**：「按下右鍵後不可畫區域多久會出現？」——程式註解寫著
「未擬合完（入鎖 ~0.5s 內）退回活解算 reach」（`NiceInkCharacter.cpp:2670`），
但那是 07-29 寫下的**設計意圖，從來沒有人量過**。去量了。

### 一、量測（robo_veiltime.py，新儀器；2-client listen PIE、背景節流已關）

| 鎖點 | 入鎖→灰紗 | ticks | 擬合本身 | 橢圓 A×B |
|---|---|---|---|---|
| 肚頂 | **21,833 ms** | 1431 | 0.07 ms | 58.5 × 8.9 |
| 腹中 | **21,533 ms** | 1428 | 0.11 ms | 33.0 × 11.2 |

註解差 **43 倍**。擬合完全不是瓶頸；4096 樣本花掉 1431 個 tick ＝ **每 tick 只跑得完
2.9 個樣本**（3ms 預算 ÷ 每樣本 ~1.46ms）。三段分解一刀見骨：
**resolve 5978ms／trace 53ms／feas 18ms ⇒ UV 解算佔 98.8%**。

### 二、真兇＝tri-cache 是 20 萬三角形，而程式以為是 23k

`robo_tricount.py`（新儀器）實測 `DebugResolveBodyUV`：**cache tris = 204,398**。
`ResolveUVToWorldWithNormal` 是 `for (const FCachedTri& Tri : CachedTris)` **線性全掃**
——註解裡「sumo 23k tris 實測」（`InkBodyComponent.cpp:706`）是舊世界的數字，
**褌戰役的細分把皮膚一起帶上去了，而沒有任何東西在看著這個數字**。
1.46ms × 4096 ＝ 21.6 秒，帳完全對得起來。

### 三、修法＝把既有的 UV 網格接上去（不是新結構）

`FindTriAtUV`（UV bbox 撒格、`UvGridDim=256`）**早就存在**且語義相同，只是
`ResolveUVToWorldWithNormal` 沒在用它——全掃版是被遺留下來的那條路。改接：

- **語義等價是構造保證、不是近似**：任何含此 UV 點的三角形，其 UV bbox 必蓋到該點
  所在的格 ⇒ 格內候選是全掃的**超集**；格內清單依 T 遞增插入 ⇒ 命中的是同一個**最小
  索引**三角形；含點判準（±0.001 重心容差）兩邊逐字相同。
- **查無此點就是確定答案，不准退回全掃**——落在圖集空白的樣本（本例 4096 點裡
  **1633 點**）正好是最貴的那一類。只有網格建不起來（BuildSeamData 失敗）才保底全掃。

### 四、驗收

| 鎖點 | 修前 | 修後 | ticks | resolve |
|---|---|---|---|---|
| 肚頂 | 21,833 ms | **190 ms** | 1431→10 | 5978→75 ms（含 BuildSeamData 一次性建置） |
| 腹中 | 21,533 ms | **123 ms** | 1428→10 | 5914→2 ms |
| 肚頂重鎖 | 21,692 ms | **106 ms** | 1439→9 | →2 ms |

**115 倍。而且答案逐位相同**：uvMiss=1633／occl=1716／feas=524／infeas=223／
A=58.5／B=8.9 三個鎖點全部與修前一字不差＝「同一個答案、快 115 倍」的活體證明
（這是本修唯一可接受的驗收形式——可畫域的大小是承重量，不准因為快而變）。
現在的成本結構＝feas 17~20ms＋trace 5~6ms＋resolve 2ms ≈ 25ms CPU，攤在 9~10 個
tick（3ms 預算）＝**30fps 下也 ≤0.35s**。
**迴歸**：`robo_directdraw_test` 58 PASS / 7 FAIL，七個失敗名稱與帳本記錄的工作樹
既有失敗集（追記：ghosts／shader row-metered／palette／far-reach ×4）**逐字相同**
⇒ 零迴歸。

### 五、還沒修的同族（下一刀，待 user 裁決）

**`ResolveBodyUV`（世界→UV）也是 204k 線性全掃**（`InkBodyComponent.cpp:751-812`），
而且傳 `PreferNearUV`（縫區遲滯＝正常作畫路徑）時**掃兩遍**——這條在**每一個筆劃點**
上。python 端量到 3.15ms/次。追記79 補七把「筆跡卡住」歸因成可畫域邊界＋無聲失敗，
那條成立，但**這裡還有第二個因，而且是每點都在付**。修法＝本地空間 3D 均勻網格
（同一套「候選是超集＋距離上界剪枝」的等價論證），`BuildSurfacePatch` 的種子搜尋
（同樣是 204k 全掃）順帶一起修。**未動＝不屬本批。**

### 六、鐵則沉澱

- **程式註解裡的數字不是量測值**。「~0.5s」「23k tris」兩句註解各自在寫下的當天
  都是對的，之後被別的戰役悄悄推翻——**凡是「規模常數」寫進註解而沒有活體儀器
  看著它，它遲早會變成謊言**。本案兩句一起過期，複利成 43 倍。
- **效能修的驗收＝輸出逐位相同**，不是「看起來一樣快很多」。A/B 的分類計數與擬合
  結果全等，才有資格說「只改了速度」。
- 儀器：`Tools/RoboTest/robo_veiltime.py`（入鎖→灰紗三段分解）、`robo_tricount.py`
  （tri-cache 規模＋世界→UV 單次牆鐘）。兩支都**唯讀、不掛 ini**；C++ 側計時器是
  臨時碼，量完已 `git checkout` 拆除。

## 2026-08-25 追記81：作畫延遲全鏈戰役（八刀；SHIPPED-自驗，手感待 viewport）

**起點＝user 指令**：「把所有延遲優化到最低延遲，**也要注意玩家的操作到真實筆移動
之間的延遲，而不能只看筆移動到筆跡出現之間的延遲**」——後半句是本批的主軸：
過去所有延遲工作（07-26 網路遲鈍根治、08-02 稿筆純輸入）都盯著「筆→墨」那一段，
**「手→筆」那一段從來沒有人量過**。量了，裡面有兩隻。

### 一、把整條鏈拆開量（分段，含此前沒人看的兩段）

| 段 | 現況 | 定罪方式 |
|---|---|---|
| ①手→引擎拿到 delta | 引擎滑鼠平滑**開著**（`bEnableMouseSmoothing=True`） | 讀 UE 源碼 `UPlayerInput::SmoothMouse` |
| ②引擎 delta→角色讀到 | 角色與 PC 同在 `TG_PrePhysics`、**同組順序不保證** | 讀 `APlayerController::PlayerTick`→`ProcessInputStack` |
| ③aim→筆/墨 | 機器工具仍吃 One Euro，τ 40~160ms | 讀 `EffectiveDrawAz` |
| ④落墨→自己畫面 | client 同幀；**listen 主機 0~50ms 跳格** | 讀 `bLocalEcho = !HasAuthority()` |
| ⑤幀時間本身 | 每一針 **7.10ms**（世界→UV 全掃 ×2） | `DebugUvGridBench`（新儀器） |
| ⑥落墨→他端 | 50ms 批次＋30Hz/0.5° 死區 | 讀碼 |

**①的機理（兩件事，第二件是架構級的）**：`SmoothMouse` 除了把 delta 跨幀重分配
（純滯後），還有一條 `aMouse == 0` 分支——**手停下後它會繼續吐 `SmoothedMouse ×
dt/取樣間隔`**。而本作程式裡寫著的承重不變量是「滑鼠 delta=0 ⇒ aim 靜止 ⇒ P 走快取
⇒ **靜止不抖由構造保證**」（`EffectiveDrawAz` 註解，08-02 定案）。
**那條保證在引擎平滑開著的時候是假的。** 07-31 六版 user 逐字定案的「放滑鼠自由，
僅接收資訊而不控制滑鼠的走向」，引擎在上游就違反了。

**②的機理**：`GetInputMouseDelta()` 讀的是 `KeyState.Value`，而它只在
`ProcessInputStack`（住 `APlayerController::PlayerTick`）才刷新。同 tick group 內
actor 順序不保證 ⇒ 角色先跑的那一半機率下讀到的是**上一幀**的滑鼠量＝白送一整幀。
（相機不受影響：`UWorld::Tick` 明文「Update cameras last, after all actors have been
ticked」⇒ 眼錨定相機本來就是同幀。）

**⑤的真值比追記80 §5 估的還糟**：那裡記 3.15ms（`DebugResolveBodyUV` 單遍）。
但作畫路徑恆帶 `PreferNearUV`（縫區遲滯）＝**掃兩遍**，實測 **7.10ms/針**。
成本正比於「每秒畫多長」而非幀率——針距 1.5mm ⇒ 手速 30cm/s＝200 針/s＝
**1.42 秒 CPU／秒**。這就是「慢慢描不卡、一加速就頓」的形狀。

### 二、八刀

| # | 刀 | 前 → 後 |
|---|---|---|
| ① | 引擎滑鼠平滑關閉（`DefaultInput.ini`） | 跨幀重分配＋停手後合成位移 → 無 |
| ② | 角色 tick 掛 `AddTickPrerequisiteActor(PC)` | 不確定 0~1 幀 → **確定 0 幀** |
| ③ | One Euro 預設關（`bDrawAimFilterEnabled=false`） | τ 40~160ms → 0 |
| ④ | 落墨批次改每 tick 送 | 主機自己的墨 0~50ms 跳格 → 同幀 |
| ⑤ | **世界→UV 空間索引**（`ResolveBodyUV`＋`BuildSurfacePatch` 種子） | **7.10ms → 0.012ms／針** |
| ⑥ | 作畫 aim 上報 30Hz/0.5° → 60Hz/0.1°、追趕 K20→K30 | 他端筆 τ50ms→33ms、慢畫時有效更新率 ~15Hz→60Hz |
| ⑦ | 沉睡臉指向、站立俯仰同樣 30Hz/0.5° → 60Hz/0.1°、K20→K30 | 同上 |
| ⑧ | （記帳）`bEnableFOVScaling` 雙重補償——**未動**，見四 | — |

**③的論證**（不是拍腦袋）：`fc = MinCutoff + Beta·|角速|` ⇒ 30°/s 時 τ≈84ms。
稿筆 08-02 就是為這段滯後（user「有人在干擾滑鼠」）拆掉的，同一論證原封不動適用：
Liner 的濾波源本來就是**針 aim**，而針已被 `v_max` 守恆式限速＝天生平滑（原註解
自己寫「追趕本身已是平滑器」），再過一層低通只剩滯後；Shader 是手擁有速度，滯後
直接被手感覺到，收益只有壓掉 ~0.5mm 手抖（對公尺級身體姿勢不可見，08-02 已量）。
**③依賴①**：沒有①，「delta=0 ⇒ aim 靜止」是假的，拆濾波會把引擎合成的位移放行。
旋鈕留著，一鍵回舊行為。

**⑤的等價性是構造保證，不是近似**：三角形登記進自己 AABB 蓋到的每一格 ⇒ 某格盒
沒登記到的三角形必整個落在盒外 ⇒ 距離下界＝查詢點到盒面距離；掃完 Chebyshev 殼 R
就拿到這個下界。平手規則沿用全掃的「嚴格 < ⇒ 最小索引勝」。兩條路現在走**同一個**
`ResolveBodyUVImpl`，只差候選來源（`bUseGrid`）——差異被關進一個布林。

### 三、驗收

**A. 等價性＋加速比**（`robo_uvgrid.py`＋`DebugUvGridBench`，新儀器；2-client listen PIE）

| 樣本 | 全掃參考 | 空間索引 | 倍率 | mismatch |
|---|---|---|---|---|
| 作畫點 n=200（容差 0.15cm＋PreferNearUV） | 7.116 ms | 0.0185 ms | **385×** | **0/200** |
| 作畫點 n=2000 | 7.097 ms | 0.0119 ms | **598×** | **0/2000** |
| 遠距打空 n=64 | 3.23 ms | ~0 ms | — | — |

網格＝100×52×88、cell 2cm、items 450,910、**一次性建置 13.5ms**。
**mismatch=0／2200 ⇒ 「同一個答案、快 600 倍」**（追記80 定下的唯一驗收形式）。

**B. 迴歸**（`robo_directdraw_test`）：執行 65 個檢查，**6 FAIL**，名稱＝
ghosts／shader row-metered／palette／far-reach ×3 ——**全部落在追記80 同日、同一個
工作樹記錄的既有失敗集（7 個）之內，零新增失敗名稱**；執行檢查數 65 與當時
（58 PASS＋7 FAIL）逐字相同＝同一個既有中止點（`samepoint` 階段 `find_char` 回 None，
**與本批無關、本來就在**）。少掉的那一個 far-reach 現在 PASS——**未追查是真修好還是
既知 flake，記帳不宣稱。**

### 四、未動（記帳＋待 user 裁決）

1. **`bEnableFOVScaling` 雙重補償（讀碼發現，未修）**：引擎會把滑鼠乘上
   `FOVScale(0.01111) × FOV` ⇒ 站姿 FOV90 ×1.0、鎖定 FOV36 **×0.40**；而
   `DrawAimSensitivity()` 又自己乘了一次開鏡定律 `tan18/tan45 ≈ 0.325`
   ⇒ **鎖定作畫的實際增益是 0.130，不是設計的 0.325**。關掉它游標會快 2.5 倍——
   **速度是 user 的口味域**（08-01 血價：「太敏感」的主詞不是速度、0.4 版誤修已還原），
   沒點名不准動。要改＝先量再裁。
2. **`r.OneFrameThreadLag`（預設 1）＝還剩一整幀**。關掉會把 CPU/GPU 從
   `max(CPU,GPU)` 變成 `CPU+GPU`：3060@1440p 估 12.8ms→~18ms（78→55fps），
   但總延遲 25.6ms→18ms＝**延遲降、幀率也降**。這是真取捨、且 iGPU 那台是 GPU-bound，
   所以**不自己拍板**。要試＝`Config/DefaultEngine.ini` 的 `[/Script/Engine.RendererSettings]`
   加 `r.OneFrameThreadLag=0` 一行。
3. **第三人稱墨線的欠取樣**：三張 RT 都是 `bAutoGenerateMips=false`＝只有 mip0，
   而 2.3m 外 3.0mm 墨線只有 1.25~1.67 螢幕像素 ⇒ 結構上必然欠取樣（銳化在此
   自動退場：`k=clamp(1/fwidth,1,32)` 遠看 →1）。**尚未有任何儀器看著它**，
   也還沒被 user 點名。要修＝開 mips（VRAM +33%）或改 SDF，都是量級跳躍。

### 五、鐵則沉澱

- **「延遲」不是一個數字，是一條鏈；沒被量過的那一段就是最貴的那一段。** 本批
  兩隻新蟲（引擎滑鼠平滑、tick 順序）都住在「手→筆」，而過去三個月的延遲工作
  全部盯著「筆→墨」。user 一句話點出的就是這件事。
- **引擎預設值會偷偷推翻你寫在程式裡的不變量。** 「滑鼠 delta=0 ⇒ 靜止不抖是構造
  保證」這句註解是真心的，但 `bEnableMouseSmoothing` 讓它變成假的——而註解與 ini
  之間沒有任何東西在對賬。（同族：追記80 的「23k tris」。）
- **同組 tick 順序不保證＝隨機一幀延遲**，而它不會出現在任何 profiler 上。
  凡是「A 讀 B 這一幀算出來的東西」，就要有 prerequisite，不能靠註冊順序。
- **加速結構的第一版量測要包含「最壞的那類查詢」**：本批首版在遠距＋寬容差下會走完
  45 萬個空格＝17.9ms，**比它取代的全掃還慢 5 倍**——而作畫路徑完全量不到這件事。
  修法＝格數預算超過就退回全掃（**保證永不比舊路徑慢**）。
  「新結構在熱路徑快 600 倍」和「新結構在所有路徑都不比舊的慢」是兩個命題，要分別驗。
- 儀器：`Tools/RoboTest/robo_uvgrid.py`＋`UInkBodyComponent::DebugUvGridBench`
  （等價性＋加速比，全在 C++ 內量、無 python 開銷）；`DebugResolveBodyUV` 現在
  **同時跑兩條路並自報 match**——這個對照組永遠留在程式裡，不准拆。

---

## 2026-08-25 追記82：風扇狂叫與「Out of video memory」（SHIPPED-自驗）

user 帶著兩張截圖問「為什麼我的遊戲跑起來如此吃力（整個電腦誇張地作響）以及
頻繁地出問題」。查完是**兩個互不相關的病**，而且崩潰視窗上寫的那句話在誤導人。

### 一、風扇：引擎預設沒有幀率上限

`Saved/Config/WindowsEditor/GameUserSettings.ini` 是 `FrameRateLimit=0.000000`
＋`bUseVSync=False`；整個 `Source/` 與 `Config/` grep `t.MaxFPS`／
`SetFrameRateLimit`／`bSmoothFrameRate` **零命中**。從來沒有人踩過煞車。

同機位（道場大廳面壁）、同儀器（`stat unit`）、同解析度 1280×720 的 A/B：

| | 無上限（對照） | 120 上限 |
|---|---|---|
| Frame | 1.94 ms＝**515 fps** | 8.33 ms＝120 fps |
| GPU Time | 1.04 ms | 1.77 ms |
| GPU 利用率 | **66~68%** | **37~40%** |
| 功耗 | **92 W** | **30 W** |
| SM 時脈 | **1942 MHz 釘死** | ~1050 MHz |
| 溫度 | 62~63°C | 50~51°C |
| Input（管線深度） | 1.04 ms | 3.27 ms |

**每幀只要 1~2ms 的場景，被拉到滿速去畫每秒 515 張。** 一個視窗就這樣，
`play_*` 的四視窗就是四倍——實測崩潰那場的幀率在 390 與 **11**（p10）之間亂跳，
就是 user 感覺到的「一下順一下卡」。

修法：
- `UNiceInkSettingsSave::PerfDefaultsVersion`＝一次性遷移標記。首次啟動（含舊存檔
  升上來）若 `GetFrameRateLimit()<=0` 就寫 **120fps**；玩家在設定頁動過之後永不再介入。
- 設定頁新增兩列（即點即套即存，與視窗模式同一責任邊界＝正本住引擎
  `GameUserSettings`、不進本作偏好存檔也不上雲）：**幀率上限**（60/90/120/144/165/240/
  無上限）與 **垂直同步**（開/關）。新增 5 個 loc 鍵 ×13 語。
- `play_ingame` / `play_full_flow` / `play_full_flow_4p` 三支 bat 一律掛
  `-ExecCmds="t.MaxFPS 60"`。

**踩到的坑**：第一版用 `GUS->ApplySettings(false)`，它會**連解析度一起套**——
把命令列的 `-resx/-resy` 蓋掉（實測 1280×720 的測試視窗被縮回存檔裡的 960×540，
害我差點拿兩個不同解析度的樣本做 A/B）。正解是 `ApplyNonResolutionSettings()`。

**誠實記帳**：`stat unit` 的 `Input` 不是「滑鼠到畫面」，`FInputLatencyTimer::GameThreadTick`
是**定期取樣** game thread→present 的管線深度，與玩家有沒有動手完全無關。
**⚠ 本表的 Input 欄已於追記83 作廢**：該計時器每 2 秒才取樣一次、再套 20 秒時間
常數的 EMA，在 40 秒的測試窗裡讀到的全是未收斂平均的雜訊（本欄的 1.04／3.27 與
調查初期那個沒重現的 31.67 都是同一個毛病）。其餘欄（Frame／GPU 利用率／功耗／
時脈／溫度）是逐幀或瞬時量測，不受影響。

### 二、崩潰：報的是顯存，倒的是系統記憶體＋刺青畫布

崩潰行程自己傾印的帳（`Saved_P3/Crashes/.../NiceInk.log`）：

```
Local Budget:  11347.00 MB      ← 顯存額度
Local Used:     1592.77 MB      ← 只用了 1.6GB，還剩 9.7GB
Physical Memory: 30615 MB used, 1914 MB free / 32530 MB total
```

**顯存還有九成沒用。** D3D12 在系統記憶體枯竭時配置失敗，UE 一律印成
"Out of video memory"——這句話是紅鯡魚。四個未 cook 的編輯器行程（單行程峰值
5,126 MB）把 32GB 吃到只剩 1.9GB。05:56 那次崩潰簽名逐字相同。

但那 1.6GB 裡面有 **1,034.75 MB 是 Render Target 2D**（另一次 842.75 MB），
而引擎自己 pooled 的 RT 只有 12 MB ⇒ **那 1GB 全部是刺青畫布**。

源頭：`UInkCanvasComponent::BeginPlay` 無條件配 Marker/Tattoo/Mist 三張 4096²
RGBA8（**64 MiB／張**）、淡化時再加第四張 ScratchRT，而 `InkCanvas` 是
`CreateDefaultSubobject` 掛在**每一個** `ANiceInkCharacter` 上 ⇒ **每個 client 都
持有房裡所有人的整套**。崩潰那個行程裡有 6 個 character actor。
對帳：1024 MB＝16 張×64 MiB，加 960×540 場景 RT 的 10.75 MB＝1034.75 MB，逐位吻合。

**現制＝需求驅動的惰性配置**（`ComputeLayerNeeds()` 從 Works——唯一真相——算出
哪幾層真的有東西要畫）：
- 線層／霧層：分別看有沒有非 Shader／Shader 筆劃；刺青層：有沒有非 Marker 作品；
  ScratchRT：有沒有被雷射淡化的作品。
- 缺席的層綁 **4×4 全透明替身**（`GetEmptyInkTexture()`）——**絕不能傳 nullptr**：
  材質參數設 null 會退回材質預設貼圖（引擎預設是灰格），整具身體會被塗成一片灰。
  常數的雙線性內插還是同一個常數 ⇒ 與「配好卻整張清成透明的 4096」**取樣逐位相同**，
  惰性配置對畫面是恆等變換。
- 釋放只放掉自己的指標、**不手動 `ReleaseResource()`**：材質／脖子 MID 可能還指著它，
  交給 UObject 生命週期收。手動釋放會在「已釋放卻仍被取樣」的那一幀出事。
- 受害者入睡＝`PrewarmDrawLayers()` 釘住 Marker+Mist（第一針才配 2×64MB 會當場掉一幀，
  而第一針正是手感最敏感的時刻）；甦醒解釘，之後存活只由 Works 決定。
- **兩個消費者都要跟**：身體 MID 由 `UInkBodyComponent` 訂閱 `OnLayersChanged` 自己重綁；
  伸縮脖是「抄一份身體 MID」的第二個消費者，而它的重抄門檻是 SkinTone 變動——墨層換
  貼圖不動 SkinTone ⇒ 必須由角色顯式 `NeckStretch->ForceMaterialResync()` 踢它。
  漏掉任一個，畫面上就是脖子的墨與身體對不上。
- `ExportLayersToPng` 先補齊三層再傾印＝robo 契約恆得到三個檔、語義不變。
- `NiSpotMap` 的還原路徑同修（原本會傳 nullptr）。

**實測**（單一 client、道場大廳、`stat rhi`）：**Render Target 2D Memory = 83.75 MB**
（＝1280×720 的場景 RT，墨層配置數 **0**）。大廳階段沒有任何人畫過一筆，本來就不該
有 4096 存在。四視窗全部進道場：VRAM 總量 5,012 MiB（桌面基線 2,439）⇒
**每行程 ~643 MiB**，對照崩潰現場自報的 1,592/1,661 MB＝**~2.5×**；
系統記憶體 23,842/32,530 MB used（**剩 8,688 MB**），對照崩潰現場的剩 1,914 MB。
四視窗同時跑的 GPU＝47~50%／78W，**比修前「單一視窗」的 67%／93W 還低**。

### 三、量了但**沒有**動的（記帳，別讓它變成下一個過期常數）

- **角色網格 306,486 三角形／176,478 頂點、`num_lods=1`**（headless 實測 SM_Sumo；
  SK_Sumo 頂點數相同＝同拓樸）。選單一個跳舞力士 Prims 628.1K ≈ 306K×2（深度預渲染
  ＋base pass）＋脖子程序網格。
  **⚠ 別把這個數字與 tri-cache 的 204,398 混用**：後者是 `BuildTriCache` 跳過褌
  section 之後的**皮膚三角形**（墨水數學只認皮膚），差額 102,088 就是布。
  306,486＝GPU 真的在畫的量、204,398＝墨水解算看得到的量，兩個都對、範圍不同。
  **沒有加 LOD**：量到的 GPU 成本是 1.05ms/幀＝不是瓶頸，
  而自動減面會直接毀掉 08-16~24 兩個戰役打下來的褌可見線與脖子縫環。
  要動＝先有「六人同框」的量測，再決定，且必須是人工減面。
- **開發四視窗的系統記憶體**：每行程 ~2.7GB（大廳）到 5.1GB（局中）是編輯器二進位
  ＋未 cook 資產的固有成本，惰性 RT 省的是 VRAM 不是 RAM。要真正解決＝客戶端改用
  cook 過的打包版。**目前沒有可量的最新打包版**（PackagedShipping 停在 07-17、
  Packaged 停在 08-07）⇒ 08-11 那句「打包版待實測」到今天仍未兌現。

### 四、驗證

- `robo_directdraw_test`：**60 PASS / 5 FAIL、執行檢查數 65**。五個失敗名稱
  （ghosts／palette／far-reach ×3）**全部落在追記80/81 記錄的既有失敗集內、零新增**；
  且比追記81 記錄的 59/6 多一個通過。
- 效能 A/B：上表全部同機位同儀器實測。
- **編譯全綠、ini 未帶 robo 行**。手感（120 上限的體感、VSync 的取捨）待 user viewport。

### 五、鐵則沉澱

- **引擎預設值是「沒有人做過決定」，不是「有人決定了預設值」。** `FrameRateLimit=0`
  這件事在專案裡活了整整一年，沒有任何一行程式碼、註解或帳本提過它。
  （同族：追記81 的 `bEnableMouseSmoothing`、追記80 的「23k tris」。）
- **崩潰訊息會指錯地方。** "Out of video memory" 的旁邊就印著 "Local Used 1592 / Budget
  11347"——顯存還剩九成。**看訊息之前先看它自己傾印的數字。**
- **每人一份的常數乘以人數就是規模**：4096² RGBA8 是 64 MiB，看起來還好；
  ×4 層 ×6 人 ×每個 client 都有一份＝1GB。**「每個角色都配一份」的東西要當成
  規模設計題，不是初始化細節。**
- **惰性配置的替身必須是明確的空值，不能是 nullptr**——材質參數的 null 語義是
  「用材質預設」，不是「什麼都沒有」。
- **同一份資源有幾個消費者，就要有幾條更新路徑**（身體 MID／脖子抄本），
  而且第二個消費者的更新門檻常常和你改的東西無關（SkinTone vs 貼圖）。

---

## 2026-08-25 追記83：撕裂（水平橫條）——追記82 的幀率上限造成的（SHIPPED-自驗）

追記82 上線幾小時後 user 回報「在遊戲房間內任何時候都常常出現破圖，也就是橫向的
像素錯位，感覺像畫面中出現水平橫條」。**是我上一刀造成的。**

### 一、機理與定罪

實測這台機器：**兩台螢幕都是 60Hz**（MSI 2560×1440@60、ViewSonic 1920×1080@59，
後者 Min/Max=50/100 ⇒ 支援 VRR），而追記82 把上限設成 **120＝剛好 2 倍**、
`bUseVSync=False`。

- 無上限（515fps）：每 refresh 有 ~8 條撕裂線，每條的位移只有 1.94ms 的相機運動
  ⇒ 讀感是「輕微閃爍」，不會被指認成破圖。
- 120fps／60Hz：每 refresh **恰好 2 幀**，撕裂線的相位幾乎不漂移 ⇒ **一條固定高度
  的橫線**，而且位移是 8.33ms 的相機運動＝4.3 倍大。整數比例是所有配置裡最糟的。
- 為什麼視窗模式也會撕裂：Win11 的「視窗化遊戲最佳化」（22H2 起預設開）會給視窗
  遊戲 flip model／independent flip，VSync 關時允許撕裂——DWM 合成不再保護你。

### 二、修法

`ApplyPerfDefaultsOnce` 加 **v2 遷移＝VSync 預設開**。上限只管功耗，畫面完整性
是 VSync 的工作，兩者不能互相代班。

同批把「玩家動過沒有」從版次裡拆出來成獨立旗標 `bPerfTouchedByPlayer`：
**版次不能兼任這個旗標**——自動遷移也會推進版次，混用就分不出「系統設的」與
「玩家選的」，下一次遷移就會覆寫玩家的選擇。

`Config/DefaultEngine.ini` 加 `[SystemSettings] r.GTSyncType=2`（引擎為
「VSync 開但要低延遲」內建的模式）。

### 三、實測（同機位同儀器，道場大廳 1280×720）

| | 無上限 | 120 上限（追記82） | VSync 開（現制） |
|---|---|---|---|
| Frame | 1.94 ms＝515 fps | 8.33 ms＝120 fps | **16.68 ms＝60 fps** |
| GPU 利用率 | 66~68% | 37~40% | 39~40% |
| 功耗 | 92 W | 30 W | **18.7 W** |
| SM 時脈 | 1942 MHz 釘死 | ~1050 MHz | **~550 MHz** |
| 溫度 | 63°C | 51°C | **49°C** |
| 撕裂 | 幾乎看不出 | **固定橫條** | **無** |

功耗從 92W 一路降到 18.7W（−80%），而且撕裂消失。

### 四、一隻量測血價：**我拿一支取樣週期比測試窗還長的儀器做了 A/B**

為了量 VSync 的延遲代價，我用 `stat unit` 的 `Input` 跑了五個組態，得到
96.73／98.65／81.04／65.35／98.91ms，並據此宣稱「`r.GTSyncType 2` 把延遲從 96.73
降到 81.04」。**那個結論是假的。**

查引擎源碼：`FInputLatencyTimer GInputLatencyTimer(2.0f)`——**每 2 秒才取樣一次**，
而 `UnrealClient.cpp:391` 再套 `0.9*old + 0.1*raw`＝時間常數約 20 秒。我在 t=40s
拍的單張截圖大約只吃到 15 個樣本、離收斂還很遠，**那串數字是同一個未收斂平均的
雜訊，不是組態差異**。（旁證：同為 GTSyncType 0 的兩次量到 96.73 與 98.65，
而同為 GTSyncType 2 的兩次量到 81.04 與 98.91——組內散度大於組間差異。）

後續想改用 t=150s 收斂後再拍，但**遊戲視窗失焦後被節流到近乎停擺**
（10 分鐘只累積 46 CPU 秒、log 完全靜止、NiShot 的 150 秒 ticker 從未觸發），
這條路也死了。**所以：VSync 的延遲代價方向確定（present 佇列變深），量級未知；
`r.GTSyncType=2` 的收益未經驗證**——它留在 ini 裡的理由只有「引擎文件指定的正解
＋幀率無可量代價（Frame 仍 16.69ms/60fps）」，不是任何我量到的數字。要拆隨時可拆。

順帶更正追記82：那裡寫的「調查初期 `Input: 31.67 ms` 沒有重現」現在有解釋了
——**同一支儀器的同一個毛病**，31.67 也好 1.04 也好，都是未收斂的平均值。
追記82 表格裡的 Input 欄應視為無效欄位；其餘欄（Frame／GPU 利用率／功耗／時脈／
溫度）都是逐幀或瞬時量測，不受影響。

### 五、鐵則沉澱

- **修一個效能問題可能製造一個畫面問題。** 上限管功耗、VSync 管畫面完整性，
  是兩個正交的責任；只裝其中一個，另一個就會以你沒預期的形式出現。
- **整數倍的幀率／更新率比例是撕裂最顯眼的情況**，不是最不顯眼的。
  120/60＝2 讓撕裂線停在原地；515/60＝8.6 讓它糊成雜訊。
- **用儀器之前先查它的取樣週期。** 取樣週期（2s）＋平滑常數（20s）大於測試窗（40s）
  ＝那支儀器在這個實驗裡沒有解析度，讀出來的差異全是雜訊。
  （這是追記80「註解裡的規模常數會過期」的同族：**沒被查證的儀器特性**。）
- **「版次」不能兼任「玩家動過沒有」**：自動遷移也會推進版次，混用等於下一次
  遷移必定覆寫玩家的選擇。
- 已知未解：**遊戲視窗失焦後被節流到近乎停擺**（`-game`，非編輯器），
  任何需要長時間掛機的自駕量測都會被它吃掉——下次要量之前得先解決這件事。

## 2026-08-27 追記84：乳頭／肚臍加深（BUILT-自驗，外觀待 viewport）

user：「有沒有辦法把人體的乳頭和肚臍顏色稍微加深一點」。

### 通道是現成的、而且是空的
`M_InkBodyChar` 的 BaseColor＝`SkinTone × lerp(1, chroma×2, ChromaStrength)`，
`T_BodyChroma` 就是「皮膚 albedo 的乘法變化場」——而它自 08-22 起是 **32×32 全等 1.0**
的中和圖（追記74 塊斑症的治法，追記79 覆核過）。所以整條通道空著：在它上面畫解剖色調
＝**零材質改動、零 C++**，強度用既有旋鈕 `ChromaStrength`（實查預設 **0.6**）。
#37「皮膚零烘焙陰影」仍成立（這是 albedo 色素）；肚臍那顆是折衷，真實肚臍的暗一半來自遮蔽。

### 出貨
- `Tools/AssetPrep/sumo_anat_tint.py`（Blender headless）：2048 色調圖；產物
  `SourceAssets/body_anat_tint.png`（純色調）＋`body_chroma_anat.png`（出貨內容，值=factor/2）。
  `ANAT_BASE=` 可乘在未來重烘的血色場上。**定位法與出貨值見下面的「續」**（本節初版
  用的錨點法已退役）。
- `Tools/AssetPrep/ue_import_anat_tint.py`：匯入 `T_BodyChroma`（**BC7**、非 sRGB、
  SimpleAverage、TEXTUREGROUP_World）。改壓縮的理由＝原設定 TC_VectorDisplacementmap
  是未壓縮，32×32 時無所謂，2048 就是 21MB 常駐（追記82 的帳）；平滑低頻場用 BC7 是 5.3MB。
- 引擎端實際變暗量＝`1 − 0.6 × dark`（再過 SkinDesat 0.15）。出貨值見「續」。

### 量到的事實（做這件事必須先知道）
- UV0 局部密度 **0.30~0.33 px/mm @1024**（1 texel≈3.1mm；與 CLAUDE.md「4096 下 1 texel=0.81mm」
  一致，「0.617 px/mm」那句是舊值）。全身 3D 面積 6.42 m²、UV 佔用率 59.9%。
- **乳頭／肚臍是獨立殼**：SumoRetopo 有 6 個連通元件——主體 99045／肚臍島 2304／
  乳暈島 266×2／乳頭島 170×2（retopo 保護島，邊界不共點，島頂點到主體最近 0.43~0.61mm）。
- **UV0 有 193 個 chart**（中位數 88 tris/chart），解剖區被切成 9~12 個小 chart。
- 乳頭比周邊皮膚高 **19.8mm**；26mm 半徑柱體內的皮膚面積是投影圓的 **4 倍**。

### 三個踩過的坑（**都是錨點法時代的**，現制已不走這條路，留著是因為規則本身可搬）
1. **測地距離在這裡是錯的尺**：乳頭島 19mm 寬但島內測地距離跑到 44mm ⇒ 乳暈只剩一顆 9mm 黑點。
   改用**沿局部表面法線的柱面投影半徑**（把圓「蓋章」在皮膚上），半徑才等於解剖尺寸。
2. **法線閘會在凸起正中央挖洞**：乳頭島 40~45% 三角形法線朝內，凸起側壁 |dot| 本來就小
   ⇒ 不論有號或絕對值都會挖出一顆沒上色的亮盤（ray_cast 對賬：相隔 4mm 的兩個面，
   一個 0.754 一個 0.995）。已整條拆除，擋乳下摺的工作交給**軸向窗**
   （相對周邊皮膚平面，凸起 [-12,+45]mm／凹陷 [-45,+12]mm）。
3. **面積不是好契約**：起伏會把面積放大 4 倍，收緊就誤殺、放寬就沒守到。真正該守的是
   **「沒有一滴顏色落在特徵以外」**＝上色紋素離原點的最大 3D 距離（實測 22.7~24.5mm，上限 54~56mm）。

### 已知殘留
- 乳頭尖那顆小 chart 只有幾個紋素寬，雙線性會把邊緣紋素跟鄰居(=1.0)混掉 ⇒ 尖端比設計值淡
  （Blender 預覽 nearest vs linear 差很多）。要根治得動 UV 佈局，不在本次範圍。
- **我沒有在引擎內拍過任何一張**；Blender 預覽只證明位置與範圍。深度是 user 在 viewport
  逐級裁決的（極值→2/5→一半），範圍也是 user 看極值版確認的。
- `T_BodyChroma` 從 32×32（~4KB）變成 2048 BC7（≈5.3MB＋mips），全域共用一份。

### 追記84 續：定位改制——位置與範圍不是手放的，是檔案裡就有的（user 實測打回）

user 看了極端版（暗 0.95）：「乳頭和肚臍定位都有問題，你去翻 blender 檔無法找到這些東西的
確切位置與範圍嗎？」——**可以，而且一直都在**。SumoRetopo 有 7 個連通元件，其中五顆小島
就是 retopo 的「保護島」＝解剖特徵本體：

| 島 | 頂點 | 表面積 | 中心 | 尺寸 |
|---|---|---|---|---|
| 乳頭 ×2 | 170 | 10.71 cm² | (±0.3052, −0.2952, 1.1326) | 1.9×1.9×1.9 cm |
| 乳暈 ×2 | 266 | 94.15 cm² | (±0.2980, −0.2845, 1.1247) | 6.6×5.2×6.5 cm |
| 肚臍 | 2304 | 35.81 cm² | (+0.0004, −0.4722, 0.7230) | 3.8×2.0×3.8 cm |

（另兩個是主體 93098 與頭 6103。島塗色渲染核對過形狀＝乳暈橢圓＋乳頭圓點＋肚臍圓盤。）

**我原本用的是 `sumo_bump_scan.py` 的 ANATOMY 排除球**——那是「掃描缺陷排名時要排除的
真解剖」的**排除球**，球心刻意埋在皮下（實測離乳頭尖 12.4mm），本來就不是特徵中心。
再疊上 seed 吸附跳到凸起頂、柱面投影軸、法線閘，三層近似疊出可見的偏移。
**現制＝島內全量、島外沿距離淡出**，錨點／半徑／投影軸／法線閘全部退役。

**兩條新規則**：
- **暗度算在頂點上再重心內插，不要逐紋素算距離**：乳暈島 266 頂點鋪 94 cm²（間距 ~2cm），
  逐紋素取「到最近島頂點的距離」會讓島內部也被 6mm 淡出帶咬到 ⇒ 邊緣鋸成星芒、核心變方塊
  （實拍）。頂點層用**點到島三角形**的真距離（島內＝0）＋線性內插＝平滑，且島內恆為全量。
- **契約的容差要把內插算進去**：頂點場在大三角形上線性內插，會把 >2% 的邊界多帶出
  一個三角形邊長（navel 實測 35.1mm vs 島半徑 19.2＋淡出 6）。上限＝島半徑＋淡出＋最大邊長。

**出貨值（user 在 viewport 逐級裁決）**：先看極值 0.95（確認範圍對）→ 要 2/5 → 定在
**極值的一半**＝暗度 **0.475／0.45／0.475**、淡出 **1.5mm**、**無偏紅**（`ANAT_CHW=1,1,1`）
⇒ 引擎端（ChromaStrength 0.6）實際變暗 **28.5%**。**這組值已寫成腳本預設**：
不帶任何環境變數重跑，產物與出貨的 `body_chroma_anat.png` **逐位相同**（md5 對賬過）。
偏紅權重 (0.82,1.00,1.08) 與較軟的邊緣（6mm）留成旋鈕，未採用。
