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
- 陷阱補充：CameraComponent 世界旋轉 python 沒有 get_component_rotation——
  用 get_socket_rotation("None")；PC 取 pawn＝get_controlled_pawn()；
  debug_robo_emerge() 無參數；set_actor_location 只吃 3 參數。
- **GUI 編輯器崩潰後殘留 Saved/Autosaves/PackageRestoreData.json＝下次啟動被
  Restore 對話框擋死（StartupScripts 永不執行、log 死寂）**——robo 啟動前清 Autosaves。
- ini 的 StartupScripts 行用「git checkout 還原再 Add-Content」換腳本——直接 replace
  容易疊行（腳本會跑兩份互咬）。
