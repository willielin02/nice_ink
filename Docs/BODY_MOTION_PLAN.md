# 骨骼身體常駐＋摺り足步態＋軟肉彈跳 — 工作帳本

> 起案：2026-08-04，user 全權委託（離機）。原話拆三條：
> ①「先把除了沉睡者外的所有階段都換成有骨頭的」
> ②「用程序盡可能模擬力士腳不離地的移動方式」
> ③「用程序的方式讓玩家乳房、肚子、屁股等部位移動時會真實地影響該部位的跳動」
> （前情：user 先問五根 Jiggle 骨、追問「為什麼站著走路時沒有骨頭」——
> 我承認「畫墨需要靜態身體」只綁真相載體不綁顯示層，站走用雕像純屬歷史慣性。）

## 架構定案（2026-08-04）

### ① 顯示層換軌：BowBody 常駐
- 站立/走路（＝不睡、不鎖定的一切相位：大廳/作畫旁觀/巡禮/指認/終局）顯示
  **BowBody（SK_Sumo 骨骼）**；靜態 `Body` 永久隱形、**碰撞與 UV 解算職責原樣**
  （lean 起手射線/畫墨/噴射全打隱形雕像＝真相載體，沉睡替身早已是同一模式）。
- SK ref pose 與 SM_Sumo 同源同形＝換軌零視差；sleep（`UpdateSleepBodyDouble`）與
  lean（`ApplyBowPose`）的顯示接管原樣不動，`UpdateWalkAnim` 讓位即可。
- 骨骼資產缺席＝退回舊制雕像搖擺（`UpdateLegacyStatueWalk`，07-17 版原樣保留）。
- 皮膚 MID 晚綁防護：每 tick 指標比對補綁（非 Fundoshi 槽）；**ghost 佔用中讓路**
  （新旗標 `bGhostMaterialApplied`：ApplyGhostView 設、ReapplyCanonicalMaterials 清
  ——不然站姿防護每 tick 把 ghost 半透明洗回皮膚）。

### ② 摺り足步態（程序化、零動畫資產）
- **核心守恆式：步幅＝速度／步頻** ⇒ 撐地腳的本地後移速率恰＝移動速度＝世界釘住；
  滑步腳沿地面滑行。**雙腳 Z 恆＝ref 地面高＝「腳不離地」是構造保證**，不是調出來的。
- 三角波每腳相位錯 0.5：支撐半程線性後移（釘地）、滑步半程線性前滑（摺り足的「滑」）。
- 屈膝沉腰＝速度斜坡帶入（`GaitStanceDropCm` 10cm、0.17s 級進出蹲）；骨盆近水平
  （`GaitBobCm` 0.7＝摺り足紀律，微量只為彈跳激勵）；重心橫移壓向撐地腳
  （`GaitWeightShiftCm` 4.5、sin 相位）；上身向行進向微前傾＋手臂反相小擺。
- 腿＝解析二骨 IK（大腿/小腿段向 FindBetweenNormals 保 ref 扭轉；膝極向＝行進向
  ＋外側 0.7 混合＝保留掃描體的外弓膝讀感；腳掌旋轉恆 ref＝腳底貼平）。
- 寫入走既有 `WriteBowPoseConverged`（poseable 快取陷阱的收斂迴圈；驗證骨含最深鏈尾）。
- 方向自由：滑步方向＝速度向量進 CS 平滑追隨——後退/橫移自動變倒滑/蟹步。

### ③ 軟肉彈跳（五骨阻尼彈簧）
- `Jiggle_Belly`（Spine 下）/`Jiggle_Chest_L/R`（Spine1）/`Jiggle_Butt_L/R`（Hips）＝
  **世界空間**質點彈簧追錨點（錨＝姿勢層本 tick 寫完的骨位）；輸出＝骨位平移偏移
  ×`JiggleGain`(1.7)、鉗 `JiggleMaxCm`(8)。頻率分部位：肚 2.1 / 胸 2.7 / 臀 3.2 Hz、
  阻尼比 0.20（可見 2~4 次過衝）。半隱式歐拉＋子步（h≤1/90s）。
- 生效域＝BowBody 可見的一切狀態：站走步點、入鎖/起身硬切、沉睡替身被翻身、
  甦醒抬升——**世界空間錨點＝角色平移/姿勢硬切全都自然激勵**，不用逐狀態接線。
- 傳送尖峰防護：單 tick 錨點位移 >100cm＝彈簧直接貼齊（入座/入睡傳送不當激勵）。
- 姿勢層 dirty-check 相容：追蹤「上次寫入值」判斷姿勢層本 tick 是否重寫——重寫＝
  讀值即新基準、沒重寫＝上次寫值扣回偏移（lean/sleep 的髒檢查跳寫不會污染基準）。
- 純視覺、各端本地模擬零複製（姿勢輸入本來就同步）。
- Tick 順序：睡姿替身 → 站走步態 → lean 姿 → **jiggle** → 筆 → 伸縮脖（讀骨
  消費者拿到的都是含彈跳的最終姿勢；NeckStretch 的 Jiggle_Belly 權重環自動跟晃）。

## 旋鈕（全 UPROPERTY，viewport 即時調）
`bSkeletalStandEnabled` / `GaitStanceDropCm` / `GaitStepsPerSecBase|Max` /
`GaitMaxStrideCm` / `GaitWeightShiftCm` / `GaitBobCm` / `GaitTorsoLeanDeg` /
`GaitArmSwingDeg`；`bJiggleEnabled` / `JiggleGain` / `JiggleMaxCm` / `JiggleDamping` /
`JiggleBellyHz|ChestHz|ButtHz`。舊 `bWalkAnimEnabled` 仍是總開關（含步態）。

## 驗收儀器
- `Tools/RoboTest/robo_gait_probe.py`＝六契約：c1 顯示換軌（bodyVis=0/bowVis=1）、
  c2 雙腳恆貼地（≤rest+3cm）、c3 撐地腳世界釘住（慢腳速<體速half、佔比≥75%）、
  c4 沉腰（髖低 ≥5cm）、c5 彈跳激勵（走路肚偏移峰 ≥0.4cm）、c6 停步收斂
  （3s 內肚<0.3cm＋髖回位）＋側視截圖矩陣（`DebugRoboSideView` 相機、折返走連拍）。
- 新 debug 鉤子：`DebugRoboWalk(dirX,dirY,secs)`（PollMove 消化＝與真鍵同一入口）、
  `DebugRoboGaitStats()`（機讀摘要）、`DebugRoboSideView(bool)`。
- 探針教訓：道場 X 域 ~440cm——直走 >2s 必撞牆＝速度歸零假樣本；
  走向一律「朝/背房間中心」由當下位置實算＋折返，並每 0.5s log 原始數據自報告。

## 血價教訓（2026-08-04 首兩輪探針）
1. **背景節流三犯**：新探針又忘了關 bThrottleCPUWhenNotForeground——症狀簽名＝
   多個「moving」樣本的 phase/腳速**完全相同**（每次都是 reset 後第一個巨量 dt 幀
   的混疊）。任何新 robo 探針的 boot 段一律先關節流（樣板已含）。
2. **彈簧阻尼的參考系＝設計決定**：絕對速度阻尼在等速移動有穩態拖尾 2ζv/ω
   （250cm/s×ζ0.2/ω13≈7.6cm→×增益恆撞鉗位＝「肚子被風吹住」恆偏一側）；
   **相對速度阻尼（質點−錨點）＝等速零偏移、只有加速度激勵**——步點/硬切/翻身
   才晃。探針 c7 防飽和契約（釘鉗位樣本佔比<50%）＝此病的迴歸籠。

## 驗證紀錄
- 2026-08-04 robo_gait_probe **7/0 DONE-PASS**（節流修＋相對阻尼修＋調參後第四輪）：
  c1 顯示換軌 bodyVis=0/bowVis=1；c2 貼地 maxFootZ 8.4 vs rest 8.3（231 樣本）；
  c3 撐地腳釘住佔比 0.84（撐 20~60 vs 滑 490cm/s＝清楚的交替滑步簽名）；
  c4 沉腰 65.7→55.6；c5 肚峰 8.0；c6 停步 3s 收斂 belly 0.02＋髖回位 65.5；
  c7 防飽和 satFrac 0.20（峰值只在折返/急停碰頂）。
- 側視截圖矩陣（gait_0..4＋InkQA/gait_zoom_*）自查：屈膝寬步/雙臂微張/肚腹讀感
  成立；髮髻/褌/臉貼圖/膚色正確；站立剪影與雕像版一致；無斷肢/穿地。
- 調參定案（穩態擺幅進鉗位內、衝擊照樣碰頂）：JiggleGain 1.7→1.25、
  JiggleDamping 0.20→0.32、JiggleChestHz 2.7→3.4（原值正中全速步頻 2.6Hz）、
  GaitWeightShiftCm 4.5→3.5。
- 迴歸全綠（2026-08-04）：directdraw **71/72**（唯一 FAIL＝既知記帳 flake
  「cruise tipSpd」同簽名 2.05/gain 1.01，與本改動無因果——步態/彈跳不碰巡航鏈）、
  orbit **24/0**、feign **26/0**、trace **22/0**、stencilcursor **20/0**。
  ini robo 行已拆、play 模式已切回 party。
- **手感（步態節奏/彈跳浮誇度/首人稱體感）＝user viewport 總驗收，未過門。**
