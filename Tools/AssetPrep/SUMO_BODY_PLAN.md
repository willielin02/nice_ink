# char17 → 相撲體型改造工序（v3.2 資產工作）

目標：把 char17 的體型程式化拉到 `SourceAssets/Sumo wrestler/sumo_wrestler_2k.glb`
（Sketchfab "Sumo wrestler" by 3dUVpro，CC Attribution——**上市前 credits 必須署名**）的水準：
巨大懸垂肚腩＋肥肉皺褶。**只改頂點位置與密度，拓撲結構化加密，UV/骨骼/屬性全保。**

## 原則（違反任何一條就會弄壞管線）

1. **絕不替換網格**：char17 拓撲上掛著 FaceUV 自拍島、FaceMask 頂點色、Scalp/BeardRoot、
   骨骼權重、UV0 墨水圖集。相撲模型只當「體型參考目標」。
2. **Simple 細分（不是 Catmull-Clark）**：新頂點落在原表面，形狀零改變；兩級 → 5.3k→~85k tris。
3. **NoDeform 保護區（權重 0）**：兩個乳頭、肚臍、整張臉（FaceUV 島全部）、雙手、雙腳。
   程式化選取：球形範圍（乳頭/肚臍座標從網格量）＋ FaceMask 頂點色（臉）。
4. **變形分兩路**：軀幹＝對齊後 shrinkwrap 吸附相撲表面（皺褶直接複製）；
   四肢＝參數化粗細縮放（相撲的腿是彎的不能吸，量圍度比例套用）。
5. 改完存回 `nice_ink_player_character17.blend` 的 body 物件（建議先另存 char18 驗收，
   使用者點頭後才轉正）——四套匯出腳本與眼罩管線自動繼承。

## 工序

1. Blender 開 char17 blend → import glb（相撲）→ 對齊：縮放到同身高、軀幹中軸重合、
   骨盆高度對齊（相撲姿勢僅軀幹直立可用）。
2. 量測參考數據：肚腩最大圍/身高比、胸深、肚腩下垂 Z、大腿/上臂圍——print 出來記錄。
3. char17 body：建 NoDeform 頂點組（乳頭 r≈2.5cm ×2、肚臍 r≈2cm、FaceMask>0.5 的臉區、
   手腕以下、腳踝以下）→ 反轉成 Deform 權重組，邊界羽化（頂點組 smooth 數次）。
4. Simple Subdivision ×2 modifier → apply（先於變形，讓皺褶有面可用）。
5. 軀幹 Shrinkwrap（mode: NEAREST_SURFACEPOINT，vertex group = Deform ∩ 軀幹範圍）
   → apply；肚臍島因周圍外膨自然深陷（正確效果）。
6. 四肢：per-bone 軸向徑向縮放到量測比例（腿粗、臂粗）。
7. 渲染前後對比＋相撲參考三視圖 → **使用者 viewport 驗收（唯一閘門）**。
8. 驗收過 → 跑四套匯出（uv0_uniform 自動重平衡密度）→ 眼罩重烘 → 重匯入 session
   （照 CLAUDE.md 程序，記得拔 robo StartupScripts）→ robo 驗收。
9. **工程盯哨**：InkBodyComponent tri-cache 線性掃描 ×16 面數——筆刷解算若變卡，
   升級空間雜湊（週邊格子桶）。
10. credits 記錄：Sauna by local.yany（CC BY 4.0，已有）＋ Sumo wrestler by 3dUVpro
    （CC Attribution）都要進遊戲 credits。

## 實戰結果（2026-07-07 深夜，v1 幾何完成待驗收）

**Shrinkwrap 全滅，正解＝徑向剖面場轉移**（腳本：scratchpad/sumo_step2c_radial.py，驗收後應收進 Tools/AssetPrep）：
沿兩個模型「各自的軀幹軸」射線取样 R(z,θ) 輪廓場（姿勢差異被軸歸一化），char17 頂點按
R_sumo/R_char 比例徑向縮放。踩過的坑（每一條都吃過虧）：

1. **body 物件帶 90°X 旋轉**——所有幾何邏輯必須過 matrix_world，local 座標的 z 是世界前後不是高度。
2. **軸線不能用頂點質心**：char17 背面中線有 576 頂點的精雕密集區（肛門），質心被拖到貼背壁
   → 用「帶內包圍盒中點」（密度免疫）。
3. **射線要取第一命中**（軸心在軀幹內，第一命中＝軀幹壁；手臂在外層永遠打不到）；
   穿透式取最外層會抓到手臂/拳頭。
4. **場要逐列中位數消毒＋θ/z 平滑**：他張腿的縫隙、拳頭會產生野值列。
5. **方向性增益是和固定 A-pose 共存的關鍵**：正面 1.0（肚/胸＝主讀感）、背 0.85、
   側 0.45 且腋下帶再收——否則軀幹側向膨脹會吞掉手臂。正面圍裙獨立下探（z0.68-0.82 漸入），
   側/背在臀上緣（z0.82-0.98）收，臀部留給褌／腿另一路。
6. **保護島不要剛體騎乘**（環平均跟不上場梯度→圓盤浮雕），用「島內係數一致化」：
   島內頂點的縮放因子拉向島心因子＝均勻縮放、結構保真、無邊界台階。
   島清單：肛門（自動偵測：背面中線密集區）、肚臍、左右胸（乳頭候選對各併一座）。
7. **他的兜襠布前垂片會污染正面低處的場**（射線打到布不是肚皮）——已試「由上向下 max 傳播」
   未完全解決；肚臍下中線凹痕 open issue（嫌犯：網格中線縫或場殘差）。
8. Simple 細分 ×2（5.3k→85k tris）加密不變形；最後對動過的頂點跑 2 輪 λ=0.4 拉普拉斯
   （島核心除外）。腿＝獨立參數化增粗（×1.42 徑向、對每側腿軸）。

**Open issues（驗收後決定要不要修）**：肚臍下中線凹痕；腋下側向過渡的階梯感；
臀部完全未變（給褌讓路，故意的）；InkBodyComponent tri-cache ×16 面數效能未實測。

## 已知量測（起點）

- char17：2668 verts / ~5316 tris / 身高 1.744m（feet origin）
- 相撲 glb：182.8k tris / 91.4k verts（2k 貼圖版：sumo_wrestler_2k.glb）
- 微細節（毛孔級）＝法線貼圖工作，選配：從相撲表面烘法線到 char17 UV0（材質要加 Normal 通道）
