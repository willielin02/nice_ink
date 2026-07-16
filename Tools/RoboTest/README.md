# Robo-test harness（自駕 PIE 測試）

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

## 出口死鎖迴歸（2026-07-15）

- `robo_exit_seating_repro.py`＝「入座階段衝到出口→Drawing 推門」迴歸：
  重現 2026-07-15 user 抓到的死鎖（Seating 停門檻＋RailNavigate 端點無 Exit 分支
  ＝永遠卡死）；期望 VERDICT: EXITED。跑前把腳本裡 OUT 路徑改到可寫位置。
  儀器：DebugPlaceAtExitCell（傳送不觸發）＋DebugRoboNavTo（強制導航走真實
  RailNavigate 路徑——robo 無法注入滑鼠/左鍵，自然出場只有這條驗法）。

## 甦醒視線制（2026-07-16）

- `robo_sleepgaze_test.py`＝眉心廣角視線制的端到端驗證（22 項）：睜眼→零假破綻→FOV 102→
  DebugRoboSleepLook 設世界視線（睜眼語義＝視線 yaw/pitch；閉眼語義＝盲瞄 twist/bend）
  →相機朝向=視線且 roll=0→頭部姿態追趕經真實 ServerUpdateSleepLook 鏈上 server
  →演出性微抬頭（看自己肚子）→翻身趴姿抬頭檔→現身 FOV 還原 90。
  受害者 seat1（RPC 走真網路）；結果檔 Saved/robo_sleepgaze_result.txt。
- 陷阱補充：CameraComponent 世界旋轉 python 沒有 get_component_rotation——
  用 get_socket_rotation("None")；PC 取 pawn＝get_controlled_pawn()。
