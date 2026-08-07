# Nice Ink — Claude 操作手冊

這份文件是為每一個接手本專案的 Claude 寫的操作手冊。先讀完這份，再動任何東西。
深度背景在記憶資料夾（`~/.claude/projects/c--games-Unreal-Engine-nice-ink/memory/`）——
**開工前把 `project_nice_ink_v3_build.md` 和 `project_nice_ink_v2.md` 整份讀完**，裡面是所有踩過的坑和定案史。

## 專案是什麼

派對遊戲：相撲力士（被協會禁止刺青、羨慕極道的刺青）在道場喝酒，醉倒的人閉眼沉睡
（沉睡＝**醉夢描圖小遊戲**：割糖餅式沿線描、描出線重來、描完甦醒；作畫者可花錢
搖他的夢拖時間），其他人用刺青機在他身上畫畫；醒來後巡禮指認作者，
猜錯的畫變成真刺青。UE 5.7 C++，無 Blueprint/UMG 資產；局內 HUD 用 canvas 畫、
輸入用輪詢；**主選單自 08-06 起＝Slate C++ 直寫（仍零編輯器資產；user 裁決——
canvas 做不到毛玻璃半透明）**。
**設計的唯一權威是 `SPEC.md`**（v4.0b——2026-08-03 描圖圖案池正推八式終定案；
v4.0=08-02 甦醒小遊戲改制描圖＋搖晃攻擊＋噴射拳腳移出核心循環）。上架衝刺（主選單/配對/大廳/音效/打包）的
工作帳本與待使用者項在 `Docs/SHIP_PLAN.md`。

## 鐵律（違反任何一條都是嚴重事故）

1. **使用者的 viewport 是唯一驗收閘門。** 你的截圖只能證明幾何成立，永遠不能宣稱「效果很好」。
   robo 測試通過 ≠ 完成；說「已通過幾何驗證，最終手感以你的 viewport 為準」。
2. **設計捕捉一字不改。** 使用者第一次講機制時逐字記錄；不要問他的話裡已經回答的問題；他不會重講。
3. **「給我洞見/分析」= 只分析，不寫 SPEC。** 只有使用者明說「更新進專案」才動 SPEC。
   使用者沒說完的半句話不是設計決定。
4. **不要 hedge-and-propose。** 給一個明確推薦＋理由，不要列選項清單讓他選你不敢選的。
   提案前先對照 SPEC 的承重不變量（判讀排序、賭注加權、皮膚=跨場資源、美術語言）。
5. **不要重提 SPEC 已有的東西當新點子。** 提案前搜尋 SPEC。
6. **誠實報告。** 測試失敗就說失敗並附輸出；跳過的步驟明說；不確定的標注不確定。

## 構建與測試（照抄，不要自創流程）

- **編譯前必須先關編輯器**（Live Coding 會擋 Build.bat）：
  ```powershell
  Get-Process UnrealEditor -ErrorAction SilentlyContinue | Stop-Process -Force -Confirm:$false; Start-Sleep -Seconds 4
  & "C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" NiceInkEditor Win64 Development -Project="c:\games\Unreal Engine\nice_ink\NiceInk.uproject" -WaitMutex 2>&1 | Select-String "error C|error LNK|Result:"
  ```
- **開發試玩（2026-07-17 起）**：`Tools/play_full_flow.bat`＝4 視窗 -game 從主選單跑
  完整 happy path（真 ServerTravel/LAN 搜房）；`Tools/play_ingame.bat`＝4 視窗直連道場
  跳過選單（1 listen server＋3 client 錯開直連）。都用編輯器二進位跑未 cook 資產——**重編譯前要先關掉這些遊戲視窗**。
  正式流程＝大廳主機 ENTER 開局；PIE 自動開局只服務 robo。
- **自駕測試（robo-test）**：`Tools/RoboTest/robo_leanlock_test.py` 是範本＋README。流程：
  0. **先切 robo play 模式**：`Tools/RoboTest/play_mode_robo.ps1`（編輯器關閉時跑）——
     道場啟動圖＋3 客戶端 listen 單行程 PIE；測完用 `play_mode_party.ps1` 切回
     開發試玩模式（主選單啟動圖＋Play=4 獨立視窗）。
  1. 把 `+StartupScripts=<腳本絕對路徑>` 掛進 `Config/DefaultEngine.ini` 的
     `[/Script/PythonScriptPlugin.PythonScriptPluginSettings]`（**用 Edit 精準替換，
     禁用 PS 的 -replace|Set-Content 重寫整檔——ASCII 編碼會毀掉中文註解**）；
  2. 啟動 UnrealEditor.exe（GUI 版）前**先刪 Saved/Autosaves**（PackageRestoreData
     殘留＝Restore 對話框擋死一切，含 headless），腳本自動開 PIE → tick 狀態機驅動
     → 寫結果檔（一律寫到專案 Saved/，別寫 session scratchpad——會過期）；
  3. 用 `until [ -f 結果檔 ] && grep -qE "DONE|EXC|FAIL"` 的背景迴圈等結果；
  4. **測完把 ini 那行移除——提交的 config 永遠不能帶著它；UAT 打包期間 ini 也絕不能
     帶著它（staging 會把它打進包裡）。**
- **資產重匯入用一次性 headless session**：`UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript=<腳本>`。
  **重匯入前必須把 robo StartupScripts 行註解掉**，否則 harness 空轉堵死整個 session
  （症狀：編輯器 40% CPU 燒著、log 死寂、永不完成）。
- 測試 API：GameMode 上有 `DebugRoboStroke/DebugRoboKick/DebugRoboAccuse(bool)/DebugRoboSpray` 等
  timer-deferred hooks（0.1s timer 讓 RPC 逃出 python 執行 guard）——robo 測 RPC 流程一律走這些。
- log 位置：`Saved/Logs/NiceInk.log`；robo 結果檔寫在腳本裡指定的絕對路徑。

## 除錯方法論（弱項補強，照著走）

1. **量測，不要猜。** 卡住時第一步是把現場數字抓出來（座標、距離、狀態布林），
   不是盯著程式碼想。寫探針腳本（參考 robo 範本的 snapshot 模式：before/t+0.5/t+1.5/t+3）。
2. **無聲失敗必須先開口。** 一條判定鏈沒反應時，給每個早退路徑加
   `GEngine->AddOnScreenDebugMessage`（用固定 key 避免洗版），跑一次讓失敗自己報名，修完拆掉。
3. **伺服器判決要送回客戶端看。** 主機視窗≠伺服器視窗時，加臨時 `UFUNCTION(Client, Reliable)`
   把拒絕原因送回請求者畫面＋UE_LOG 進檔（你自己能 grep）。
4. **分清三種病**：客戶端沒送出／伺服器拒絕／伺服器同意但客戶端視覺沒跟上——診斷訊息要能區分這三段。
5. 修好後**必須重跑同一個驗證**（robo 或使用者），不要「應該好了」。

## UE 陷阱年鑑（每一條都吃過虧，症狀→原因→解法）

- **PoseableMesh 讀到上一幀**：`SetBoneTransform` 後立刻 `GetBoneTransform` 拿到的是舊快取
  → 每個「寫姿勢→讀骨骼」之間插 `RefreshBoneTransforms()`；元件的 `AddWorldOffset` 會跨次累積
  → 重擺前先 `SetRelativeLocationAndRotation` 還原。
- **Listen server 的 Server RPC 同幀執行**：主機按鍵觸發的 RPC 當場生效，同一次
  `WasInputKeyJustPressed` 會被同 Tick 後面的輪詢再讀一次（進鎖鍵被當成起身鍵）
  → 狀態切換後設 0.25s 寬限期再受理反向輸入。遠端客戶端因 RPC 延遲天然免疫——
  **「只有主機視窗壞」幾乎都是這類同幀問題。**
- **判斷哪個視窗是伺服器**：Client RPC 的 log 和 Server log 同幀出現＝該玩家就是主機本人
  （Outliner 標籤「Client 0」不可信，那是 PIE instance 0）。
- **FEditorScriptExecutionGuard**：MCP/python 執行中所有 RPC 發送被強制本地化
  → robo 測 RPC 一律走 GameMode 的 timer-deferred Debug* hooks。
- **編輯器開著跑 PIE 時用 MCP execute_script 會 crash 編輯器**——現場探針一律用
  StartupScripts tick harness，不要用 MCP python。
- **quit_editor 在 PIE 中會 assert crash**：先 stop_pie、分開呼叫、再 quit。
- **HighResShot 只有聚焦的 PIE 視窗會處理**；同檔名不覆蓋（小心看到舊圖）。
- **編輯器失焦被「Use Less CPU in Background」節流到 3~6fps**（user 用機時 robo
  編輯器永遠在背景）→ 牆鐘×頻率敏感的探針假 FAIL（superfast 混疊成慢爬實錘）。
  防法＝robo harness 啟動時 python 直設 CDO（find_object Default__EditorPerformanceSettings、
  屬性名要用原始 **bThrottleCPUWhenNotForeground**——snake 名 5.7 解析失敗；此類
  config=EditorSettings 住 Saved/Config/WindowsEditor/EditorSettings.ini，寫
  EditorPerProjectUserSettings 白跑）；診斷法＝log 幀計數器差÷時間戳差先驗幀率。
- **骨骼 FBX 重匯入會綁回舊骨架**（一骨陷阱）→ 匯入前先刪 SK＋Skeleton 資產。
- **Blender 存檔在 Pose Mode＋use_selection 匯出會悄悄丟 armature** → 全場景匯出＋先回 Object Mode。
- **Canvas SE_BLEND_Translucent 不寫 dest alpha** → 墨水章用 SE_BLEND_AlphaComposite＋預乘紋理。
- **「墨水落錯位置」先查 actor/component scale**（序列化的舊 scale 會蓋過 ctor 修正）。
- **python 的 `unreal.Rotator(roll, pitch, yaw)` 參數順序**；GameMode CDO 改了不會進 PIE 實例
  （開 PIE 後改實例屬性）。
- **PIE 多人視窗z順序**：點主編輯器會把浮動客戶端視窗蓋到後面——沒消失，工作列叫回來。
- **Git Bash 跑 `-ExecutePythonScript` 引號兩種死法**：外層單引號→UE 收到 `""路徑""`＝空值→
  編輯器無事可做**永遠空轉燒 CPU**；`\\` 反斜線被吃光。解法＝用 PowerShell 工具＋把腳本
  複製到無空白路徑（scratchpad）再跑；開跑後 grep log 的 `LogInit: Command Line:` 驗證解析。
- **level `duplicate_asset` 後同 session `load_level` 該關卡＝fatal crash**（World Memory Leaks
  GC assert）。另存關卡的正解：load 原關卡→記憶體改→`EditorLoadingAndSavingUtils.save_map(world, 新路徑)`。
- **換房間網格後先跑地板探針再跑 PIE**：對席位做向下射線（從頭頂高 z≈240 打，順便抓低空障礙），
  「席位懸空」在探針裡一眼看穿，比 PIE 掉出世界好查（道場實例：建物西段無地板，整體西移才救回）。
- **殭屍編輯器鎖 umap＝關卡存檔無聲失敗**：GUI 編輯器 crash 後 CrashReportClient 可能把它
  重新拉起來，Stop-Process 撲空也照樣印成功。之後每個 headless session 的 save_current_level/
  save_map 全部靜默失敗（log 裡只有一行 LogSavePackage Warning: Failed to move ... to temp）、
  delete_asset 回 True 但檔案還在。防法：**關卡存檔一律檢查回傳值＋存完立刻比對 umap mtime**；
  懷疑時 `Get-Process UnrealEditor` 清點＋用 Rename-Item 測檔案鎖。
- **headless 匯入 FBX 部件（combine=False）頂點烘進 FBX 世界空間**（含 Y 翻轉）：
  所有部件 actor 擺同一個基準點即可原樣重現；Y 翻不翻用非對稱特徵射線實測，不要猜。
- **引擎網路預設值＝同步遲鈍元凶**：NetServerMaxTickRate 30（所有 server→client 33ms
  量化）、GameStateBase 10Hz、**PlayerState 1Hz**——相位切換頓/罰酒慢一秒的隱形真兇
  （07-26 已調：ini 60Hz＋ctor SetNetUpdateFrequency）。「主機視窗手感好、客戶端鈍」
  ＝缺本地預測的簽名（listen server RPC 同幀本地執行）。

## 技術地圖

- `Source/NiceInk/`：`NiceInkCharacter`（輸入輪詢/貼臉鎖定/**直接畫制**（2026-07-20 起：
  眼錨定 FP 相機 FOV36、螢幕中心=針尖（機器工具）、剛臂 3-DOF 解筆尖觸膚、2D viewmodel 筆、
  ghost 穿透、**鎖定靈敏度 FOV 縮放**（07-24 開鏡定律 ×0.33＋DrawSensitivity 旋鈕——
  不縮放=游標三倍速））/
  **刺青機伸縮針**（LMB=伸針=墨流出因果、伸長量針/握管分帳）/**三工具制**（07-25 打稿制：
  滾輪三檔 **Stencil 麥克筆（預設）**→Liner→Shader；Stencil=結晶紫 #703593 稿線
  （手速自由直畫、甦醒收束 EnterTour 全洗=不進巡禮不可指認、旁人 3D 拉伸筆+本人
  2D 貼圖筆 T_UI_MarkerPen；**07-31 六版制＝自由滑鼠＋凍結相機**（帳本=DIRECT_DRAW_PLAN
  07-31 各節、user 六輪逐字定案）：滑鼠→aim 純積分零否決（游標類輸入不做「狀態機+
  否決」——tilt 牆/掃程拒收/斜坡拒收三連教訓）、P/筆/墨=每 tick 讀出的導出量、
  相機恆凍結+**邊緣推擠**（貼邊 0.92+本 tick 外推才讓位 min(推量,溢出)、LMB 按住
  視野鎖死）、臉=gaze 惰性追隨（τ0.22）＝2D 筆/小點/✕ 錨 P 投影；旋鈕
  StencilCamEdgeFrac/DrawGazeTauS；儀器=robo_stencilcursor_probe＋DebugRoboMouse；
  **08-01 出墨層修＝拆 Shader 遺留移動閘＋首針即點**（3°/s 閘=慢畫整段無墨、
  單擊無點——閘關 tick 路徑不記帳=永久丟棄；稿筆墨=P 無此閘要防的噪聲源、
  Shader 閘照舊；probe 補 c7 單擊守恆/c8 慢速域契約=14/0＋directdraw 72/72；
  **08-04~05 骨骼身體常駐＋摺り足＋軟肉彈跳（user 委託+三輪打回迭代、四輪
  SHIPPED 已提交 6b7b0b6..4047a95）**＝站立/走路顯示全換 BowBody 骨骼（靜態
  Body 退居真相載體恆隱形、碰撞/UV 解算原樣；睡/鎖接管路徑不動；MID 晚綁防護
  讓路 ghost 旗標）＋摺り足**雙軌制**（二輪 user 抓「腳統一朝左右擺+穿膜」：
  速度分解前後/左右、各腳自己側軌道滑＋側帶鉗位=不越中線構造保證、膝極向
  rest 導出=馬步外弓保留；步幅=速度/步頻守恆式=撐地腳釘住、雙腳恆貼地=構造
  保證）＋五 Jiggle 骨世界彈簧（**阻尼必用相對速度**=等速拖尾實錘、驅動頻率
  避彈簧共振；三輪權重手術=Jiggle_Belly 繞背 13%+褌背帶 42.5% 背側淡出=背不跳；
  四輪**旋轉耦合**=平移不轉法線實錘、切向偏移換繞體內樞軸旋轉=明暗隨肉滾）
  ＋**柔化法線轉印**（胸斷層四段定罪=真幾何+SM 靜態管線抹軟法線=雕像是美化
  說謊者；**5.7 Interchange 無視 FbxImportUI 選項**鐵坑；sumo_soft_normals_bake
  =著色對齊雕像讀感）；儀器=robo_gait_probe 10 契約（含 c8 不越帶/c9 膝外開/
  c10 大腿間距）+robo_headlight_probe A/B+DebugRoboWalk/GaitStats/ViewFrom；
  四輪全綠；帳本=Docs/BODY_MOTION_PLAN.md、手感待 viewport；
  二輪增益 0.4=誤修已還原（「太敏感」主詞是視野觸發非游標——體感形容詞先問
  主詞；StencilCursorGain 旋鈕留、預設 1.0）；三輪=**邊緣帶真兇雙修**（儀器
  robo_fovaxis_probe：引擎維持垂直 FOV、「水平半角 18°」只在 16:9 成立→半角
  改讀投影矩陣；貼邊主詞四段演化＝生 aim→濾波 aim（領先可見物）→筆網格尖
  （被身體幾何卡住=往左觸發往右永不觸發）→**游標標記終錨**（✕/小點=恆跟
  滑鼠的可見記號、四邊統一像素帶＝離邊框恆等距；鐵則=貼邊主詞必須同時
  「可見」且「恆隨手」）；可視畫布<可畫橢圓=結構事實、下一刀 FOV/眼距待裁；
  **08-02 稿筆純輸入（已提交 264bfe8）**＝user 抓「有人在干擾滑鼠」→定罪
  07-20 One Euro 濾波殘留（畫面歸針時代防姿勢抖、07-31 自由游標制後=慢速
  ~0.16s 果凍感）→EffectiveDrawAz/Tilt 單點分流：稿筆本人=**生 aim**（游標/
  墨/姿勢全鏈同源零延遲、靜止不抖=構造保證）、機器工具照舊濾波、他端複製
  追趕自帶平滑；同批：拉繩穩定器（Lazy Mouse）入庫**預設 0=關**（旋鈕
  StencilLazyRadiusCm；user 驗收「鈍」——手抖僅筆寬一成+上游已濾=收益不可感）；
  錨點曲線制同日建又刪（user 終裁、原始碼零殘留）——全史+教訓（交付說明
  白話走查/指定式輸入殺畫畫體感/濾波層在架構改版時重審服務對象）=帳本
  08-02 各節；probe 15 檢查（c8 拆雙段=按住墨在流+收筆守恆）＋游標增益
  量級鐵則 0.52°/單位入 RoboTest README）；Liner=**自由游標追趕**（08-04 皮繩鉗
  退役＝user 擊穿「方向 vs 目標點互斥」誤診：游標純積分無鉗可放很遠（TattooCursorAz/
  Tilt）、針以 v_max 追到游標為止——甩遠=持續走、貼針=貼手精描、遠距橫移=方向細調；
  aim 恆=針=**畫面歸針**原樣；行進蟻虛線=針→游標完整待走路徑（逐步追模擬、終點
  釘游標命中點=與十字重合）＋游標十字 HUD；中途方向舵制一日夭折=瞬時位移當方向
  =像素量化+過敏感、鐵則=方向從位置差讀不從微分讀）＋守恆式 v_max=k·d·f＋浮雕跨越
  ＋**壓稿線 1.5cm 內=沿稿自動走**（手勢歸打稿、慢工歸機器；動滑鼠=取消、沿稿
  結束=游標收攏停針）；Shader=**填色
  收斂制**（平頂＋線性羽化剖面=塗均勻構造保證、單趟 55% 疊趟收斂實墨、軟橢圓
  章 COLA 疊平、暈開烘製鏈=高解烘→高斯→箱式下取樣 1:1；流量恆定、
  FInkStroke.PointFlow byte 鏈保留）＋**Crayola 官方十色調色盤**（user 驗收
  定案；1-9,0 換色即時生效＋HUD 常駐色票列））/程式化走路/ESC 系統選單）、
  `InkCanvasComponent`（筆劃=真相、**三層 RT 快取**：線層 4096+Valve 銳化／霧層 4096
  軟半透明（銳化不咬）／刺青層；作者 ID/碳黑/雷射/洗掉；批次蓋章＋預烘 stipple 條帶＋
  縫區表面補丁逐點落墨）、`InkBodyComponent`（世界↔UV 雙向解算、tri-cache＋焊接拓樸、
  FInkSurfacePatch 表面攤平、縫資料層（近縫旗標+UV 網格索引）、換睡姿網格、眼睛開閉）、
  GameMode（回合狀態機＋PreLogin/Logout 斷線防護＋AbortRound）、GameState（相位/受害者/計時）、
  `DreamTrace`/`DreamTraceComponent`（**醉夢描圖 v4.0**：割糖餅式沿線描
  （自交避讓 2.6×帶半寬鐵律）＋割線機制 2D 移植（**08-04 與割線完全同制＝user
  鐵則「不准機制分岔」**：自由游標 v_max 追趕/無皮繩無域牆（bbox+4 防跑飛牆=
  皮繩殘留被抓拆除）/FP 刺青機貼圖 T_UI_TattooPen+行進蟻虛線+游標十字/越線
  重來）＋盤面=**包圍盒貼合可用矩形**（08-03「圖太小」三輪放大：圓貼合退役+
  進度併提示行+姿勢面板路線級真撞才讓；佔高 72~78%）＋
  搖晃攻擊（G 鍵花錢 500/冷卻/受害者顯名/**睜眼照收照扣＝無聲甦醒零洩漏**）；
  **08-03 圖案池＝正推八式終定案**（user 逐輪裁決：扇/雙浪/糰子｜折鶴/蛇/龜｜
  櫻/鳥居、分杯 3/3/2 帶寬×**2.4/2.2/2.0**（08-04 user 定值，原 2.0/1.8/1.6）；
  正推三環=相撲神事/和彫憧憬/酒宴、
  內線制退役=純外框、龍/鯉/燈籠/軍配淘選戰報與鐵則全在帳本；源=Twemoji＋
  Wikimedia 折鶴（**BY-SA、ATTRIBUTION 已更新**）；管線=背景板過濾/FATTEN 充氣/
  CLOSE_FRAC/compose_parts 同源組合；**svg2paths2 不吃 <g transform>＝變換要烘進
  d 字串**）；帳本=Docs/DREAM_TRACE_PLAN.md；robo_trace_test 22 檢查）、
  `DreamMaze`/`DreamMazeComponent`（醉夢圓形迷宮：**v4.0 退役封存**——元件/RPC/
  套件全保留永不啟動；噴射拳腳=GNiceInkSprayEnabled/GNiceInkKickEnabled 雙閘封存
  ＝SPEC #51 未來更新）。
- **前端與配對（2026-07-17 上架衝刺；08-05 EOS 上線＋選單 v2；08-06 Slate 白卡制
  ＋跳舞舞台）**：`NiceInkMenuGameMode/PlayerController/HUD`＋`NiceInkMenuWidget`
  （SNiMenu Slate 本體）＋`NiceInkMenuStage`。
  **08-05 選單 v2＝房間碼前門**：建房生成 4 字母房碼（NICODE/NIPUB 廣告屬性、剔
  I/L/O）→大廳大字顯示（GameState.RoomCode 複製）、加入頁輸碼直達＋公開房列表退居
  陌生人房、invite only（預設）/public 二選、開發內臟全拆（連線模式恆自動跟
  IsOnlineServiceConfigured）；robo 鉤子=`NiMenuAutoHost/NiMenuShowJoin/NiMenuJoinCode`
  （-ExecCmds 直呼、逗號分隔——`|` 不是分隔符；**user 在機時禁焦點/輸入注入自動化、
  截圖=topmost+NOACTIVATE＋SetProcessDPIAware**）；記帳=join-by-code E2E 併 B4
  雙機驗收（本機防火牆無 UnrealEditor inbound 規則擋 LAN beacon）。
  **08-06 Slate 白卡制（user 兩連打回 canvas 深棕版後定案）**：素色半透明＝
  `SBackgroundBlur` 毛玻璃白卡（Paper 0.78）＋墨字＋酒金主鈕；SNiMenu 全 C++ 零
  資產（Visibility lambda 輪詢換頁、SButton IsFocusable(false) 防搶鍵盤焦點）；
  字體=**複合 UFont**（裸 FontFace 餵 FSlateFontInfo=豆腐字鐵坑；MenuHUD 建持
  GC）；canvas 選單退役（DrawRoundedBox/白卡 helper 留局內 HUD 用）。
  **08-06 字體制＝Zen Old Mincho 主聲部＋六文字系統矩陣**（user 質感審計定案：
  圓體全站=軟成一團病根→明朝體標題/動作、圓體降級標籤；tagline/金劃刪、
  licenses 併 settings；Zen 拉丁+日文｜源流明體=繁中（user 打回「繁中怎會沒
  古風」後換裝、Noto TC 留庫備用）｜Noto SC/KR/Serif/Naskh＝簡中/韓/西里爾+
  擴展拉丁/阿拉伯；SubTypeface 按碼域+culture 分流、fallback=源流；搭配三關=
  同屬/字重齊/同屏混拉丁；儀器=NiMenuFontSample 取樣行；全 OFL 已入授權頁）。
  **08-06 本地化第一階段＝13 語（user 定案照抄 Meccha 清單）**：NiceInkLocText
  自管字串表（43 鍵×13 語零資產；譯文=Claude 初稿待母語校對）＋語言設定
  （SettingsSave 持久化/OS 偵測 zh 變體分繁簡/-culture= 跟隨/ApplyLanguage 必同步
  SetCurrentCulture=字體繁簡分流開關）＋session 錯誤鍵化＋大廳四句＋
  **「文A」語言頁**（迷路窘境解：底列文A 鈕→13 母語名網格全列、點選即套用
  重建；箭頭循環退役）；robo=NiMenuLang/NiMenuShowLang；**08-07 AR 全鏡像
  SHIPPED-自驗**（user 指令「一併處理好整個版面鏡像」）＝Slate 選單一行制
  `SetFlowDirectionPreference(Culture)`（整樹自動鏡像含命中；房碼格釘回
  LeftToRight=拉丁記號不逆序）＋canvas HUD **原語層一次性鏡像**（座標恆以
  LTR 邏輯空間書寫、FlipX/FlipXW 只在 DrawTok/RoundedBox/FaceTok/IconTok/
  BigTitle+Button 命中判定發生；DrawFaceTok 內部 TGuardValue 掛起防雙重鏡像）
  ＋**遊戲幾何豁免**（描圖盤/轉盤/準星/調色盤鍵序=TGuardValue 掛起；筆
  viewmodel 走 AHUD::DrawTexture 天然不鏡）；bRTLLayout=DrawHUD 每幀跟文化；
  實測=ar 選單鈕序反轉/chip 靠右/版本戳翻左＋大廳 seat 靠右現金靠左（阿拉伯
  數字 ١٠،٠٠٠=FText::AsNumber 文化紅利免費送）＋zht 迴歸零變動；
  剩=局內 HUD 字串翻譯（另一量級）。
  **08-06 身分系統臉制（SPEC v4.0d #52）→ 同日 v4.0e 修訂＝名字＋臉雙載體**：
  （v4.0d 原案名字全退場；user 隨後裁決「辨識靠名字+臉部照片icon」）——
  大廳=席位+臉像+**名字**+現金、揭曉/指認/頂欄=臉像+名字並列；
  DrawFaceTok=FaceIconCache+紙框+**FaceUV 版面 UV 裁切 (0.30,0.22)+(0.40,0.40)**
  （整張畫=膚色方塊鐵坑、外圈透明疊膚色底）；底層唯一鍵仍=隱形 PUID（名字=顯示
  層可重複）。**08-06 個人檔案頁 BUILT-自驗**＝主選單第六頁（名字欄回歸+現金
  （雲端資產視圖）+上傳自拍+眉毛鐵律提示 BrowHint）；**自拍→臉 runtime 接入
  （開發機版）**＝intake_selfie.py（selfie_to_face_texture+sumo 眼罩單人烘焙）
  →PersonaSubsystem 背景行程輪詢（FTSTicker）→FImageUtils 匯入三貼圖（眼罩
  SRGB=false）+skin_color.json→Saved/PlayerFace/ 本機正本、開機自載；
  InkBodyComponent::ApplyCustomAvatar；舞台力士=DressDancerFromPersona（換臉+
  穿雲端刺青，Tick 輪詢冪等）；**選單靜默登入**=TrySilentLogin（persistentauth-
  only、失敗不開 portal 不進 Failed UI；靜默中按 Host/Join=併回 portal 路）；
  robo 鉤子=NiMenuShowProfile/
  NiMenuSelfie/**NiMenuShot（Shot showui——HighResShot 不含 Slate UI 鐵坑）**。
  **08-07 五連（user viewport 裁決）**：名字開放 Unicode（Sanitize 黑名單制）＋
  檔案對話框=**IFileOpenDialog COM 自接**（DesktopPlatform=GetOpenFileNameW
  古典模板高 DPI 糊=退役、Shipping 可用）＋處理秒數上狀態列（實測 61~107s/張）＋
  **臉庫**（library/<時間戳>/四工件+thumb=HUD 裁切框膚色打底；active.txt 指針；
  ThumbCache=UPROPERTY=Slate brush 不保 GC 的貼圖錨；選單縮圖列點選即換、
  舊平鋪檔自動遷移 legacy）＋已有臉→「重新上傳自拍」＋**六文字系統矩陣抽共用**
  （BuildCompositeUiFont 住基底 ANiceInkHUD、選單/局內同座——此前局內只掛
  M+ 兩面=韓/阿/非日系漢字豆腐；大廳「力士の墨한글」實測全渲染）＋
  **canvas 整形路**（DrawTok/MeasureTok：RTL/呈現形碼域命中→
  ShapeBidirectionalText+FCanvasShapedTextItem；「الحبر」連寫+RTL 實測正確；
  拉丁/CJK 原快路零變動；AR 殘項只剩版面鏡像）＋robo 鉤子 NiMenuSetName/
  NiMenuShot 改 core ticker
  （world timer 死在 ServerTravel；Shot 必走 PC->ConsoleCommand）；
  **#52 三選一已裁（08-07 user「當然是1遊戲內建」）＝C++/ONNX 內建
  →同日 M0~M5 全站 SHIPPED-自驗（帳本=SHIP_PLAN C1 含全數字）**：
  `Source/NiceInk/Private/Face/`（NiceInkFaceOnnx=NNE 推理殼、
  NiceInkFaceLandmarks=MediaPipe 全鏈重現、NiceInkFaceBakery{,Core,Warp}=
  selfie_to_face_texture v7＋intake_selfie 全移植）；模型=raw .onnx 在
  **Content/FaceBakery/models（gitignore、NonUFS staging）**＋data/ 常數檔
  （入 git）；Persona 內建路優先、`-facevenv`=venv 對照組；金樣本對賬 8 張
  =rgb mean 2~3/alpha 全等/眼罩 99.997%/emma 同文案拒收；暖 88s 冷 156s、
  模型常駐快取。鐵坑：UE opencv_world455 **無 jpeg 無平行化**（IO 走
  ImageWrapper、BlurF 自寫 ParallelFor 高斯、TPS map 一次 remap ×5）、
  NNE 輸出形狀要符號 fallback、**NNE ORT 非編輯器預設單線程**（ini 已設
  IntraOp=0）、TArray Add 自身元素=擴容斷言、PS1 中文註解無 BOM=cp950 吞
  換行；task 解包=兩顆 tflite 純標準 op→tf2onnx 同權重直轉（索引零重標）；
  對賬儀器=mp_onnx_landmarks/gen_gold_refs/compare_parity.py＋NiFaceBake
  console 指令；遺留=fp16 量化/EXIF 方向/打包版實測；venv 路=對照組保留；
  **待做=自訂臉雲端儲存+房內分發**（bytes 列車擴大到 MB 級；沒有它房內他人
  仍看名冊臉）。
  **選單舞台（user 定案：半透明按鈕後面=自己的力士跟 BGM 跳舞）**＝
  `NiceInkMenuStage` 全程式生成（隱形地板+相機+無影平行光×2+完整
  ANiceInkCharacter 替身）：墨水 RT 壓 512 省 VRAM、臉=SetupAsMenuDummy 直指
  avatar（繞過 PlayerState）、AIController MoveToLocation 直驅（無 navmesh）→
  現有摺り足步態+Jiggle 自然發生；對拍=GameInstance.GetBgmStartAudioTime＋
  BeatSec 旋鈕（0.62 待耳測校準）；每 2 拍換邊橫移、每 16 拍轉圈。
  鐵坑：mesh 前向=actor -X（面向鏡頭=yaw 180）、UE FOV=水平角（36°@16:9=望遠
  壓臉）、橫移角色必關 MotionBlur、雙平行光要設 ForwardShadingPriority、
  BGM 喚起原掛在 ANiceInkHUD::BeginPlay（MenuHUD 改繼承 AHUD 後要自己叫）。
  設定/授權頁照舊（Slate 版）、
  `NiceInkGameInstance`（偏好持久化 NiceInk_Settings 槽＋斷線回選單＋BGM 播放層＋
  NiVoice 語音探針）、
  `NiceInkSessionSubsystem`（UI 狀態機＋?Name=?Avatar= 上服＋**EOS 登入閂**：
  persistentauth 靜默→失敗自接 accountportal 開瀏覽器——引擎 fallback 只掛
  AutoLogin 路徑）、`NiceInkAudio`
  （11 個合成音；**閉眼沉睡全域靜音＝感官規格、無任何甦醒音**；BGM/語音不經此
  ＝照播照聽）、
  `NiceInkUiTokens.h`（HUD 調色盤共用）。**連線層（08-05 SHIPPED 單機實測全通）＝
  照抄 Meccha Chameleon 架構**：listen server over EOS P2P（NAT 失敗自動走 Epic
  免費中繼）＋EOS lobby（建房即開 RTC 語音房、成員自動入房）＋Epic 帳號登入
  （桌面原生 OSS 無 device-id 顯名＝編譯期關閉、Epic 帳號制是唯一穩路；scope
  恆為 basic+friends+presence+offline_access、AuthScopeFlags 縮不掉→Portal EAS
  三許可全開對齊＝唯一解；配方=Docs/EOS_SETUP.md）；LAN 照舊（選單切換、
  IsOnlineServiceConfigured 全程式自動跟隨）；**ClientSecret 在 ini＝repo 必須
  private**；**08-06 B3 雲端隨身 BUILT-自驗**＝`NiceInkPersonaSubsystem`
  （EOS PlayerDataStorage：現金+碳黑/永久刺青+偏好跟帳號走——存檔鍵改
  `NiceInk_P_<PUID>`（NetId「EAS|PUID」後半＝Connect 層、Steam 票證同路零改動；
  LAN/PIE 無 PUID＝舊名字+席位鍵原路）；進房上行=client 分塊 RPC 16KB+CRC→host
  驗證套用（bAssetsRestored 防雙還原、12s 逾時走主機本機槽熱備）、結算下行=每
  Persist 點+碳黑誕生點 host 回傳本人寫自己雲端（私人保險箱不可代寫）；偏好=
  Revision 比帳高者贏；**Epic PUID≠Steam PUID＝B5 切換時開發資產不搬家屬預期**；
  雲端 E2E 併 B4 雙機驗收）；
  待辦=雙機驗收/Steam 票證登入（正式終態）/品牌驗證（帳本=SHIP_PLAN B4~B6）。
  打包＝RunUAT BuildCookRun（cook 白名單在
  DefaultGame.ini；**只被 C++ 字串路徑引用的資產要 AlwaysCook**）。
- 場地＝`L_Dojo`（道場獨立部件在 /Game/Dojo/Parts，基準點 (430.7,40.9,0)；07-30~31 場景修：
  兩門洞各四片拉門重排（軌距=板厚→任兩板零重疊=共面閃爍歸零）＋四張武道布掛軸
  （日の丸/空手道/大日本/柔術）拆除——流派錯棚+戰前讀感=平台風險，網格留 Parts 可逆，
  現 21 個部件 actor；門楣松鶴/富士框畫保留=正確扁額位；L_Sauna 保留）；
  光照＝fullbright 均勻環境光（環境亮度旋鈕=SaunaSkyLight Intensity，曝光 bias 5.2 是承重值）；
  皮膚質感＝M_InkBodyChar 材質內假光（SPEC 定案 #37：全啞光＋頭燈假光＋掃描色度血色場；
  旋鈕全是 Scalar Parameter：Headlight*/SkinBrightness/SkinDesat/ChromaStrength/SkinSpecular；
  血色場再生=Tools/AssetPrep/sumo_body_chroma_*；**皮膚零烘焙陰影鐵律不破，勿再提案皮膚陰影/AO**）。
- 墨水圖集 UV0＝**均勻紋素密度**（sumo 實測 0.617 px/mm；RT 解析度 4096＝筆寬 4.7px
  ＋線層 Valve alpha 銳化）；筆寬 `MarkerUvRadius 0.000584`＝3.8mm 全身一致；線的跨縫
  ＝點刺制逐點解算天然安全；**排針（面積章）的跨縫＝InkBody 縫資料層＋表面攤平補丁
  逐點落墨（縫 2.5cm 內自動切換）**。**改 UV0 排布＝舊存檔刺青座標全部作廢。**
- 臉部管線：`Tools/FacePipeline`（自拍→臉貼圖 v7、閉眼變體、眼球禁畫遮罩烘焙）；
  臉照片走 FaceUV（通道1），墨水走 UV0（通道0），互不影響。
  python 環境：`C:\games\Unreal Engine\nice_ink_face_pipeline\venv\Scripts\python.exe`。
- Blender 5.1：`"C:\Program Files\Blender Foundation\Blender 5.1\blender.exe" --background <blend> --python <腳本>`；
  角色源正本＝`SourceAssets/sumo_character_master.blend`（char17 已退役；迭代檔 previews/masters/retopo
  已 gitignore 留本地）。
- 下一批已知工作：~~噴射出口與褌的視覺（待定 #11）~~（v4.0 移未來更新籃）、
  平台小號實測（待定 #12）、
  開場動畫場景改寫（待定 #14）、~~RMB 瞄準切分（待定 #15）~~（v4.0 關閉）、鎖定畫布空間感
  ——眼位 20–25cm＋游標 A5 鉗位提案（待定 #16）、**內容動機＋投票經濟設計
  （2026-08-01~02 討論中未定案：秘密題目制＝待定 #17、巡禮投票經濟＝待定 #18、
  帳本=Docs/CONTENT_ECON_PLAN.md 含死案墓場；題庫生產管線（LLM 量產＋keep/kill
  裁決工具）待開工）**、~~EOS 憑證＋語音接入~~（**08-05 SHIPPED 單機全通**；
  剩雙機驗收＋Steam 登入切換＋品牌驗證＝SHIP_PLAN B4~B6）、
  上架待使用者項全清單見 Docs/SHIP_PLAN.md。
  已實作待 viewport 驗收：**醉夢描圖＋搖晃攻擊 v4.0（2026-08-02 user 定案後全權
  委託實作；robo_trace_test 21/0＋feign/orbit/directdraw/stencilcursor 迴歸綠；
  **08-03 圖案池正推八式終定案 SHIPPED**（trace 22/0＋feign 26/0＋orbit 24/0）；
  **08-04 五連 SHIPPED**：盤面包圍盒放大＋描圖割線同制自由游標（皮繩/域牆/
  十字游標舊制全退役、FP 刺青機貼圖+行進蟻進夢）＋帶寬 2.4/2.2/2.0（user 定值）
  ＋甦醒本人端朝向競態修（ClientSyncPoseTransform 補控制器 yaw；robo_wakefov_probe
  +seat1 斷言入庫）＋甦醒 FOV 調查=零變動實錘 72；
  帳本=Docs/DREAM_TRACE_PLAN.md 含補位設計待追認清單）**、
  走路動畫（08-04 摺り足制實裝取代雕像搖擺=技術地圖 08-04~05 條、待 viewport）、
  音效組（MasterVolume）、
  **刺青作畫全制（2026-07-20~24，帳本=Docs/DIRECT_DRAW_PLAN.md 逐版全史：直接畫制
  →刺青機伸縮針→液線巡航手感→雙針制→打霧十一版（細針點排、冷墨底色、跨縫
  表面補丁）→**十二~十六版（07-24：欠取樣 aliasing 根治（暈開烘製鏈）→軟橢圓
  章 COLA→填色收斂制＝user 定性「打霧=塗色」：平頂+羽化+疊趟收斂實墨；
  鐘形/手速濃淡/5 趟近實心均退役）＋調色盤全戰役（換色即時生效/色票列/
  三輪公式選色落選→Crayola 官方十色 user 驗收定案；選色儀器=Tools/AssetPrep/
  palette_pick.py+canonical_palette.py+Tools/RoboTest/robo_palette_matrix.py）**；
  長跪作畫姿/雙臂 IK/平面畫布全退役）**＋**操作優化與轉印打稿制（07-24~25：割線
  「難操作」診斷鏈=靈敏度三倍速（FOV 縮放修）→拉桿→皮繩追趕（畫面歸針）→真病根
  =手勢矛盾（恆速針殺手勢肌肉記憶）→**轉印打稿制**（現實刺青工作流：紫麥克筆
  打稿→機器沿稿上墨；user 定案預設紫筆+滾輪三檔）；robo_directdraw_test.py=**72
  檢查**常駐套件（含真人手速探針＋流量恆定＋紅墨像素＋稿線/沿稿契約），迴歸組
  =orbit/feign/maze；**編輯器背景節流=假 FAIL 元凶**（user 用機時編輯器失焦被壓到
  3~6fps——harness 已自動關 bThrottleCPUWhenNotForeground，見陷阱年鑑）**。
  SPEC 敘事對齊：**08-01 user 授權「更新至與現狀對齊」＝SPEC v3.9 已收斂**
  （三工具制 #45／嚴格橢圓 #46／稿筆操作 #47／Crayola #48＋#19 註記＋#44 勘誤）。
  **＋07-26 網路同步遲鈍根治＋lean-lock 脖子修（皆已提交）**：墨線本地預測（客戶端當幀
  上屏、StrokeSeq 回播對消——robo/GameMode 直呼恆傳 0 不對消）＋net tick 60Hz＋
  GameState/PlayerState 複製頻率＋aim 上報 30Hz/追趕 K20；NeckStretch **壓縮域直紋面**
  （lean-lock 埋頭＝弦長 1.6cm＋彎 30° 在管面解算器設計域外→喉摺 111°/後頸凸；弦長
  4~12cm 交叉回管面＝沉睡 46cm 零改動）＋隱藏判定改縫寬（環心距誤殺＝後頸破洞）；
  探針=robo_neckdraw_probe.py；directdraw 72/0＋orbit/feign/maze 迴歸綠；
  **＋07-26~28 六輪修（BUILT-自驗未提交待 viewport）**：抖動根治（owner 端就跳=解算層；
  髖跳 23→2.4cm）＋稿筆骨軸 19cm（user 逐字定案）＋首鎖 aim 播種＋稿筆墨=P（凹凸修）；
  儀器=robo_remotejitter_probe.py/robo_reachmap_probe.py；directdraw 72/0＋全迴歸綠。
  **其後的眉心相機工程（相機還原→VOR→姿勢主導→P-游標→穩定化）四輪交付四輪被
  user 打回＝全批退回動工前（07-28 終局裁決）**：實驗全史+分析資產在分支
  `wip/brow-camera-experiments`（c6abc56）——重啟前必讀（視差搖機理/迭代震盪/
  robo 橋接鐵則，全文=記憶 project_drawpose_jitter_fix.md）。**現行相機=07-20
  眼錨定（入鎖凍結）＝user 知情選擇；**SPEC #44 已於 v3.9（08-01）勘誤收斂**。**
  **＋07-28~29 可畫域全戰役（已提交；帳本=DIRECT_DRAW_PLAN.md 07-28~29 各節）**：
  「莫名畫不到」三連改制（回捲牆→輸入鉗位→顯示制）收斂到 user 逐字終案＝
  **嚴格最大橢圓制**：入鎖 64² 採樣（眼錨可見性 trace＋FLeanSolveCtx 可行性，
  解算核心已抽出共用＝活解算/烘焙同源）→鎖點切面擬合「內部無不可解樣本」的
  最大面積橢圓（無下限無人工放大，太小=誠實訊息玩家自己重鎖；實測肚頂 R100
  15.3cm≥1.6×A5）→**單一裁判**：墨閘/筆視覺收筆/HUD 提示/筆尖紅 ✕ 全查同一條
  IsInsideReachEllipse；顯示=veil 殼（本人 client 專屬、M_ReachVeil 材質內解析
  橢圓公式＋fwidth 螢幕恆寬裁切亮邊線）＝零貼圖零斑。**veil 外觀=07-30~31 覆膜戰役
  終案（帳本=DIRECT_DRAW_PLAN.md 該節）：柔灰暖膚「靜音層」（desat 0.5+近中性微暗
  +α0.88=膚色保留，霜白版=睡臉屍白、焦外模糊版=TAA 閃爍均打回）＋固定公分寬對角
  柔帶（StripePeriodCm 3.5 user 定值——橢圓歸一化條紋會隨鎖點縮放=已退役孤兒）；
  旋鈕全材質參數：VeilStrength/StripeAmp/StripePeriodCm/RingOpacity。**儀器=
  robo_veilshot（三角度+重鎖段）/robo_veilflicker（隔秒差分抓時域閃爍+雙鎖點驗
  條紋恆寬）/robo_reachradius/DebugRoboReachStats。
  鐵則沉澱（血價，全文=記憶 project_drawpose_jitter_fix 十四~二十四輪）：輸入
  裝置所有權歸玩家（系統只動回饋面）、掃射式狀態設定對晚誕生元件必出 bug
  （OwnerNoSee 二鎖實錘）、皮膚上的暗線必被讀成身體特徵、掃描網格繞向/法線
  不可信一律射線可見性、換 robo 探針先 grep 確認 ini 只一行 StartupScripts。
  記帳：directdraw「cruise tipSpd」契約帶邊 flake（基準 2.60 壓線、環境漂移、
  烘焙全關 A/B 排除因果；07-31 再見兩次同簽名 2.62~2.86/gain 0.98~1.01）——
  乾淨機器重跑對照待辦；髖/踝鉗位加寬第二刀未開。
  **＋07-31 稿筆操作全戰役（六版鏈、已提交；帳本=DIRECT_DRAW_PLAN.md 07-31 各節）**：
  起點=user「打稿依然難畫」→參考系診斷（中央鎖定=FPS tracking 非繪畫）→六輪
  user 逐字定案迭代：游標制→凍結相機→平滑置中→切面步進→**自由滑鼠制（滑鼠
  純積分、系統只讀不控）→邊緣推擠相機（貼邊外推多少給多少、LMB 按住視野鎖死）**；
  臉=gaze 惰性追筆（第三人稱讀感）、複製=gaze+P 雙載體。鐵則：**游標類輸入不做
  「狀態機+否決」——自由積分+讀取式落點+回饋面=「輸入所有權歸玩家」的架構義**；
  取景不做連續慢追混合態（要嘛真靜止要嘛明確地動）。驗證=robo_stencilcursor_probe
  12/0×5＋directdraw 71/72×3；**手感待 user viewport 總驗收**。
  **＋08-02 稿筆純輸入 SHIPPED 提交 push（264bfe8）**：One Euro 果凍感定罪拆除
  （稿筆=生 aim 全鏈同源零延遲；機器工具/他端照舊）＋拉繩穩定器休眠入庫
  （預設 0）＋錨點曲線制建又刪全史（帳本 08-02 各節）；probe 15/0＋directdraw
  71/72（既知 flake）；**純輸入手感待 user viewport**。

## 收尾紀律

- 提交訊息末尾：`Co-Authored-By: Claude <noreply@anthropic.com>`。
- 每個工作段落結束：更新記憶資料夾（新教訓寫進對應檔案＋MEMORY.md 索引行）——
  這是跨 session 的生命線，寫得越具體，下一個你越強。
- 提交前檢查：ini 沒帶 robo 行、診斷碼已拆、SPEC 版本註記正確。
