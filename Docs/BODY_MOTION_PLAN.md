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

## 二輪：雙軌制摺り足＋防穿終案（2026-08-04 深夜，user 打回後重建）

**user 打回原話拆解**：「摺り足的腳從頭到尾向兩側張開、維持蹲馬步；現在的腳
統一朝左右擺＝根本不是摺り足＋誇張穿膜；靜態姿勢就是馬步」。診斷出兩隻結構 bug：
1. **腳沿速度向量滑**＝橫移/斜走/邊走邊轉的暫態橫向分量讓雙腳橫掃越中線（穿膜
   真兇之一）→ 改**雙軌制**：速度分解成身體前後/左右分量——前後=各腳在自己側
   軌道上滑（守恆式釘地照舊）、左右=步幅張開/收攏；**側帶鉗位**（左腳 X 恆
   [20,52]、右腳鏡像）＝不越中線是構造保證（GaitLatBandMin/MaxCm 旋鈕）。
2. **膝極向用「行進向+外側」混合**＝橫移時一側膝內塌（穿膜真兇之二；rest 膝
   X=±58.7 的馬步外弓被拆）→ 改**極向從 rest 幾何導出**（rest 膝相對髖踝軸的
   偏移方向）——任何行進方向下馬步外弓角全程保留。
3. 碎步化：GaitMaxStrideCm 46→30、StepsPerSecMax 5.2→7.0（摺り足=小步快滑；
   全速頂步幅上限=輕微滑冰的知情取捨）。

**探針 v2＝十契約 10/0**（四段身體相對走法：前走/右橫移/斜走/邊走邊轉 120°/s；
每段先傳送回房中心）：新增 **c8 雙腳不越側帶**（全樣本 minFootLx=20.0/
maxFootRx=−20.0＝鉗位精確咬住）、**c9 膝恆外開**（52.8~63.6 vs rest 58.7）、
**c10 左右大腿骨段間距**（恆 67.1＝與 rest 完全相同——兩腿根本不互相靠近）；
c3 釘地率升 0.84→0.95（前後軌守恆式更純、只評前走段）。
stats 補 footLx/footRx/kneeLx/kneeRx/thighGap；新鉤子 DebugRoboViewFrom。
截圖矩陣＝side_fwd/front_lat/front_turn/back_settle 四向自查全乾淨
（正面橫移雙腳各守己側、轉身深蹲滑步、背面無腿間穿插）。
鏡位血價：房中心+前方 230cm＝相機塞進座位角色體內（front 系全滅）——
截圖一律南牆機位＋「讓角色面向鏡頭再橫移」拍正面。
二輪迴歸全綠（directdraw 71/72 同 flake、orbit 24/0、feign 26/0、trace 22/0、
stencil 20/0）。**手感待 user viewport。**

## 三輪：背跳權重手術（2026-08-05，user 抓「移動時背部也會跳、不合理」）

user 假設臀骨權重沒刷好——**審計洗清臀骨**（100% 臀帶+大腿上段、背腰零權重）、
胸骨乾淨（100% 前側）；**真兇＝Jiggle_Belly 權重繞到背側**：身體 ~13% 質量在
背側（後腰 107 個 w≥0.3 頂點、最高 z=1.26）＋**褌背帶 42.5%**（腰帶跟肚骨晃）。
- 手術＝`Tools/AssetPrep/sumo_jiggle_belly_backfade.py`：SumoRetopo＋Fundoshi 的
  Jiggle_Belly 權重乘 y 淡出（前半身 y≤0 全保留、y≥0.08 歸零、間線性過渡）；
  權重歸零頂點回落 Spine/Hips（逐頂點正規化）。術後真背側 0.00%/0.00%。
  褌臀骨權重（100% 背側=丁字帶蓋臀）＝正確不動。
- 縫安全：NeckSeamData 烘焙環的肚骨權重全在前側（審計 zmaxBack=1.26 < 環 z1.29）
  ——手術不觸環頂點、header 免重生。UV 版面不動（匯出密度 0.617 與現狀逐字同）。
- 管線＝backfade → build_sumo_skeletal_fbx → headless SK-only 重匯入（骨樹不變＝
  replace_existing 安全不刪 Skeleton；**mtime 必查**——首跑被 4 隻殭屍編輯器鎖死
  無聲失敗，mtime 停在 7/17 抓包，清殭屍重跑才成功）。
- 驗證：gait 10/0（新 SK）＋背面截圖無變形無權重坑＋orbit 24/0（伸縮脖相容）＋
  feign 26/0＋directdraw 71/72（同 flake 三連）。
- 鐵則：**彈跳骨的權重域＝彈跳的解剖域**——彈簧調得再對，權重刷過界就會晃錯
  部位；查「XX 部位不該動卻在動」一律先跑權重審計腳本分區量測，不猜哪根骨。

## 四輪：頭燈斷層＋晃動法線（2026-08-05，user 三連抓：胸斷層/乳頭不顯影/晃動陰影更糟）

**斷層定罪鏈（四段量測、兩個假設被證偽）**：
1. A/B 探針（robo_headlight_probe：同機位 骨骼/關頭燈/雕像 三張）→ 關頭燈全平
   ＝著色層病 ✓、雕像無硬環＝資產分歧 ✓。
2. 假設一「SK=IMPORT_NORMALS vs SM=重算」→ 改 COMPUTE 重匯入**零變化**＝證偽；
   讀資產 import data 實錘 **Interchange 管線**（FbxImportUI 選項被整批忽略——
   5.7 鐵坑：AssetImportTask+FbxImportUI 的法線選項對 Interchange 無效）。
3. 假設二「master custom split normals 殘留」→ 審計 has_custom_normals=True 清掉
   →**環仍在**＝證偽。
4. 幾何剖面探針終錘：**環是真幾何**——乳尖 +13mm、乳暈溝 −8mm（1.5cm 半徑內
   2cm 落差）。SK LOD0 忠實渲染；**SM 靜態管線把法線抹軟＝雕像數週來是「美化的
   說謊者」**＝user 驗收慣的讀感基準。
- **終案＝柔化法線轉印**（sumo_soft_normals_bake.py）：平滑副本（SMOOTH iter12/
  factor0.5）的 loop normals 經 DataTransfer TOPOLOGY 映射烘回 body+褌 的 custom
  normals——幾何一頂點不動、著色軟化到雕像同款；A/B 復拍實錘乳暈環對比對齊雕像。
- **乳頭不顯影答案**：非刻意排除（無遮罩）——頭燈是視角依賴單訊號（凸起只有對稱
  rim 微暗、無「亮暗對」線索）＋頂點法線內插抹平公分級凸起＝構造性盲區；柔化後
  乳頭讀感=雕像原樣（近看有軟環、中距淡出）。
- **晃動陰影更糟＝純平移不轉法線（真 bug）**：GPU 蒙皮只用骨旋轉轉法線——骨純
  平移時形狀在動、明暗凍結＝肉在「滑」不在「滾」。修＝**彈簧偏移→旋轉耦合**：
  切向分量換成繞體內樞軸的旋轉（肚=繞脊椎 65cm 臂、胸=胸壁內 18cm、臀=骨盆內
  15cm；角度=切向/力臂、鉗 25°）＋徑向殘餘留平移；法線隨骨轉、明暗隨肉滾。
  探針統計面改讀彈簧等效位移（LastSpringCm——骨位移歸零後 jBelly 的量測面）。
- 驗證：headlight A/B 對齊＋gait 10/0＋orbit 24/0＋feign 26/0。
- 鐵坑三條：**5.7 的 FBX 匯入走 Interchange＝FbxImportUI 選項無效**（改法線設定
  要動 Interchange pipeline 或在 DCC 端解決——DCC 端=本次解法）；**「A 比 B 好看」
  先問是不是 B 在說謊**（雕像的柔和=管線抹軟，不是骨骼版壞掉）；**視覺類彈跳
  該配旋轉不該配平移**（平移不轉法線=著色凍結）。
- **手感（步態節奏/彈跳浮誇度/首人稱體感）＝user viewport 總驗收，未過門。**

## 2026-08-18 停下＝回原位（user 抓「暫停一切動作時肥肉定格在非原位」）

- **症狀**：一切動作停止後（玩家自己不動、或遊戲流程硬中斷），五顆 Jiggle 骨停在
  非 rest 的位置，且**永不回來**。實測（robo_jiggle_rest，走 1.1s→停 5s ×3）：
  肚 **2.18 / 3.58 / 2.24 cm ＋ 最大 3.35°**、胸 0.58~0.98cm、臀 0.69~1.03cm；
  對照組 pose 骨（Hips/Spine2）0.000cm ⇒ 病灶純屬彈跳層。胸/臀是**逐位凍結**
  （t=1.25s~5.0s 數值完全不變），肚會慢爬且第二圈**爬離** rest（3.336→3.583）＝棘輪。
- **真因（不是彈簧，是記帳）**：相對速度阻尼的解必定收斂到錨點——壞的是錨點。
  彈跳層每 tick 要問「pose 層的 rest 在哪」，判準是「讀到的骨值＝我上次寫的值
  ⇒ 扣回我自己的偏移」。但省寫優化（逐幀變化 <0.02cm 就不寫骨）**照樣把「本來想
  寫的值」記進 LastWrittenCS** ⇒ 跳過寫入後讀值≠記帳值 ⇒ 扣回分支不成立 ⇒
  **當下歪掉的骨位被收編成新的 rest**。省寫最容易在擺盪**折返點**觸發（該幀速度≈0）
  ＝位移最大處，所以凍結值是擺幅頂端而非零頭。
- **修**（NiceInkCharacter.cpp）：
  1. **記帳記實**：`LastWritten*`/`LastOffset*` 分兩路——真的寫了就記 NewLoc、
     省寫了就記「骨頭裡實際留著什麼」。省寫從此無害。
  2. **收斂終止**（`JiggleSettleEpsCm`，預設 0.02）：偏移與**相對**速度同時進門檻
     ⇒ 貼齊錨點、寫入**精確 rest**。指數衰減是漸近的（永不到零），
     「等它自己衰完」不是保證——明確終止才是構造保證。
  3. 終止那一次**必須突破省寫門檻**（否則 0.02cm/0.17° 的最後一步被吞掉，殘 0.05°
     還在）；收斂態門檻 1e-3cm/1e-4rad＝比殘留小兩級、比浮點回讀噪音大兩級。
  4. `SettleJiggleNow()` 取代散落的 `bValid=false`——後者只在同 tick 剛好有
     ResetBowBodyBones 時才安全（隱形順序依賴）。呼叫點＝入睡／換網格／隱藏。
     **沉睡者要的「穩定不動姿勢」自此是總規則的呼叫點，不是特例。**
- **驗證**：robo_jiggle_rest **7/7**（殘留 0.000cm/0.000°；下限契約 was_excited
  峰值 9~13cm）＋回原位耗時實測 **1.20s**；沉睡受害者五骨與站姿 rest **逐位相同**。
  迴歸：gait **10/10**（c5 maxBelly=8.00＝彈跳照樣滿激勵）、ceremony **11/11**
  （首輪 c8_circle_formed 204cm FAIL＝走位卡住的位置性 flake，二輪 11.9cm 通過；
  數值跨輪不同＝非決定性）。robo_collapse_jiggle 兩次都在 PIE 起步撞
  **D3D12 E_OUTOFMEMORY**（同簽名同幀）＝環境問題，**未取得該診斷數據**。
- **記帳**：pose 層自己間歇留 0~0.14cm 的 Hips 共模殘留（步態層另一筆帳，1.4mm 級、
  與本案無關）；探針的 `own_dev` 已把兩層分開，別再混記。
- **鐵則**：①**省寫優化只能省 I/O，不能省記帳**——凡「我寫過什麼」被拿來當下一幀
  的推論前提，記的就必須是**實際落地值**而非意圖值。②**漸近收斂不是「回到原位」**，
  要有明確終止才有契約。③既有 c6_settle 量的是彈簧內部狀態＝空洞契約
  （彈簧讀 0 而骨頭歪 3.6cm 照樣 PASS）——**量測面要落在使用者看得到的那個量上**。
