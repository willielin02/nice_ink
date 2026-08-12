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
