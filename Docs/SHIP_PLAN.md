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
