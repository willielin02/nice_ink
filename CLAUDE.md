# Nice Ink — Claude 操作手冊

這份文件是為每一個接手本專案的 Claude 寫的操作手冊。先讀完這份，再動任何東西。
深度背景在記憶資料夾（`~/.claude/projects/c--games-Unreal-Engine-nice-ink/memory/`）——
**開工前把 `project_nice_ink_v3_build.md` 和 `project_nice_ink_v2.md` 整份讀完**，裡面是所有踩過的坑和定案史。

## 專案是什麼

派對遊戲：相撲力士（被協會禁止刺青、羨慕極道的刺青）在道場喝酒，醉倒的人閉眼沉睡
（沉睡＝醉夢圓形迷宮小遊戲），其他人用麥克筆在他身上畫畫；醒來後巡禮指認作者，
猜錯的畫變成真刺青。UE 5.7 C++，無 Blueprint/UMG 資產，輸入用輪詢、HUD 用 canvas 畫。
**設計的唯一權威是 `SPEC.md`**（v3.4）。

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
- **自駕測試（robo-test）**：`Tools/RoboTest/robo_leanlock_test.py` 是範本＋README。流程：
  1. 把 `+StartupScripts=<腳本絕對路徑>` 掛進 `Config/DefaultEngine.ini` 的
     `[/Script/PythonScriptPlugin.PythonScriptPluginSettings]`；
  2. 啟動 UnrealEditor.exe（GUI 版），腳本自動開 PIE（3 客戶端 listen server，
     設定在 EditorPerProjectUserSettings.ini，已配好）→ tick 狀態機驅動 → 寫結果檔；
  3. 用 `until [ -f 結果檔 ] && grep -qE "DONE|EXC|FAIL"` 的背景迴圈等結果；
  4. **測完把 ini 那行移除——提交的 config 永遠不能帶著它。**
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

## 技術地圖

- `Source/NiceInk/`：`NiceInkCharacter`（輸入輪詢/貼臉鎖定/彎腰/鏡頭/筆）、`InkCanvasComponent`
  （筆劃=真相、RT=快取、作者 ID/碳黑/雷射/洗掉）、`InkBodyComponent`（世界↔UV 雙向解算、
  tri-cache、換睡姿網格、眼睛開閉）、GameMode（回合狀態機）、GameState（相位/受害者/計時）、
  `DreamMaze`/`DreamMazeComponent`（醉夢圓形迷宮：決定性生成＋受害者端模擬/視錐/導航/旋轉）。
- 場地＝`L_Dojo`（道場 25 獨立部件在 /Game/Dojo/Parts，基準點 (430.7,40.9,0)；L_Sauna 保留）；
  光照＝fullbright 均勻環境光（環境亮度旋鈕=SaunaSkyLight Intensity，曝光 bias 5.2 是承重值）；
  皮膚質感＝M_InkBodyChar 材質內假光（SPEC 定案 #37：全啞光＋頭燈假光＋掃描色度血色場；
  旋鈕全是 Scalar Parameter：Headlight*/SkinBrightness/SkinDesat/ChromaStrength/SkinSpecular；
  血色場再生=Tools/AssetPrep/sumo_body_chroma_*；**皮膚零烘焙陰影鐵律不破，勿再提案皮膚陰影/AO**）。
- 墨水圖集 UV0＝**均勻紋素密度**0.898 px/mm@2048（`Tools/AssetPrep/uv0_uniform.py`，
  四個匯出腳本都會呼叫）；筆寬 `MarkerUvRadius 0.00085`＝3.8mm 全身一致；跨縫縫合＝螢幕空間
  4px 細分。**改 UV0 排布＝舊存檔刺青座標全部作廢。**
- 臉部管線：`Tools/FacePipeline`（自拍→臉貼圖 v7、閉眼變體、眼球禁畫遮罩烘焙）；
  臉照片走 FaceUV（通道1），墨水走 UV0（通道0），互不影響。
  python 環境：`C:\games\Unreal Engine\nice_ink_face_pipeline\venv\Scripts\python.exe`。
- Blender 5.1：`"C:\Program Files\Blender Foundation\Blender 5.1\blender.exe" --background <blend> --python <腳本>`；
  角色源正本＝`SourceAssets/sumo_character_master.blend`（char17 已退役；迭代檔 previews/masters/retopo
  已 gitignore 留本地）。
- 下一批已知工作：手臂握筆 IK（刻意延後）、噴射出口與褌的視覺（SPEC 待定 #11）、
  平台小號實測（待定 #12）、開場動畫場景改寫（待定 #14）、RMB 瞄準切分追認（待定 #15）、
  EOS 憑證＋語音接入、頭部轉動破綻、走路動畫。

## 收尾紀律

- 提交訊息末尾：`Co-Authored-By: Claude <noreply@anthropic.com>`。
- 每個工作段落結束：更新記憶資料夾（新教訓寫進對應檔案＋MEMORY.md 索引行）——
  這是跨 session 的生命線，寫得越具體，下一個你越強。
- 提交前檢查：ini 沒帶 robo 行、診斷碼已拆、SPEC 版本註記正確。
