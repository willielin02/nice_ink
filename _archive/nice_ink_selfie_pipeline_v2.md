# Nice Ink — Selfie-to-Character Pipeline

## 設計理念

不做捏臉系統。玩家上傳一張自拍照，系統自動生成角色：照片貼臉、膚色校準全身、髮型匹配 groom、髮色還原（含挑染/布丁頭/雙色）。全部裸體，陰部馬賽克。角色使用 We're GenZ 專案的胖體型人偶。

---

## 模型清單

全部 ONNX、全部本地、零訓練、零雲端。

| 模型 | 用途 | 大小 | 推論速度 (CPU) | 授權 |
|------|------|------|---------------|------|
| 3DDFA_V2 | 臉部偵測 + 3DMM fit + UV 正面化 + landmarks + 頭部姿態 | ~12MB | ~1.35ms | MIT |
| BiSeNet face parsing | 19 類臉部語義分割（skin, hair, eyes, nose, mouth...） | ~50MB | ~10ms | MIT |
| DINOv2 ViT-S/14 | 圖對圖髮型匹配（selfie hair crop vs groom 渲染參考圖） | ~86MB | ~15ms | Apache 2.0 |

**合計：~148MB，處理一張自拍 < 200ms。**

---

## 完整 Pipeline

```
┌─────────────────────────────┐
│       玩家上傳自拍照          │
└──────────────┬──────────────┘
               │
               ▼
┌──────────────────────────────┐
│  ① 3DDFA_V2                  │
│  ─ FaceBoxes 偵測臉          │
│  ─ 3DMM 參數回歸             │
│  ─ 頭部姿態估計               │
│    └→ |yaw| > 25° → 提示重拍  │
│  ─ UV texture mapping        │
│    └→ 正面化臉部紋理          │
│  ─ 68 landmarks 座標         │
└──────────────┬──────────────┘
               │
               ▼
┌──────────────────────────────┐
│  ② BiSeNet face parsing      │
│  ─ 輸出 19 類像素級 mask      │
│  ─ 提取 skin_mask            │
│  ─ 提取 hair_mask            │
└──────────────┬──────────────┘
               │
     ┌─────────┼─────────┬──────────────┐
     ▼         ▼         ▼              ▼
  ③ 貼臉    ④ 膚色    ⑤ 髮色         ⑥ 髮型
```

---

### ③ 臉部貼圖

```
輸入：3DDFA_V2 的 UV texture map + 68 landmarks

處理：
  1. UV 紋理圖已自動正面化（3DMM 幾何投影，非 GAN）
  2. 橢圓 mask 裁切（排除邊緣失真區域）
  3. 用 landmarks（眼、鼻、嘴）做 affine warp
     對齊至人偶的臉部 UV 五官座標
  4. 寫入 Dynamic Material Instance → 人偶臉部材質

輸出：人偶臉上貼著玩家的正面照
```

### ④ 膚色校準

```
輸入：BiSeNet 的 skin_mask + 原始照片像素

處理：
  1. 提取 skin_mask 內的像素
  2. 轉 LAB 色空間
  3. 取中位數 RGB（比平均值更抗極端值）
  4. 設定人偶全身材質的 skin tone 參數

輸出：人偶全身膚色與玩家臉部膚色一致
```

### ⑤ 髮色偵測（含挑染 / 布丁頭 / 雙色）

```
輸入：BiSeNet 的 hair_mask + 原始照片像素

處理：
  1. 垂直分區：hair bounding box 上半 = root zone，下半 = tip zone
  2. 各區做 K-means k=2 取主色（LAB 空間，只用 a/b 通道避免光照干擾）
  3. 全區做 K-means k=3 找所有主色及比例

判斷邏輯：
  ─ 全區色彩方差低
    → SOLID（單色）
    → 參數：base_color

  ─ root 主色 vs tip 主色 LAB(a,b) 距離 > 閾值
    → OMBRE（布丁頭 / 漸層）
    → 參數：root_color, tip_color, root_amount（從分界線位置推算）

  ─ 全區有 2+ 群且空間分佈交錯
    → HIGHLIGHTS（挑染 / 雙色）
    → 參數：base_color, highlight_color, highlight_ratio

輸出：groom Dynamic Material Instance 的色彩參數
  ─ SOLID:      設 base melanin/color
  ─ OMBRE:      設 root_color + tip_color + root_amount
  ─ HIGHLIGHTS: 設 base_color + highlight_color + probability mask 灰度值
```

**UE5 材質端支援：**
- Root-to-tip 色彩梯度（groom 材質內建 root UV）
- Probability Mask 技術（per-strand 色彩決策，非梯度插值）
- Highlight mask 疊加

### ⑥ 髮型匹配（DINOv2 圖對圖）

```
開發時（做一次）：
  每個 groom asset → Editor 裡正面渲染截圖
  → DINOv2 encode → 存為 embedding 向量
  → 所有 groom 的向量存成一個 array（groom_embeddings.npy）

Runtime：
  分支判斷：
    A. hair_mask 面積 ≈ 0
       → 光頭，跳過 DINOv2，無 groom

    B. hair_mask 呈現束髮特徵（面積異常小 + 頂部收攏痕跡）
       → 彈出 UI 讓玩家從散髮候選裡手動選擇

    C. 正常散髮
       → 裁出頭髮區域（hair bounding box + padding）
       → DINOv2 encode → 得到一個向量
       → 與 groom_embeddings.npy 做 cosine similarity
       → 取最高分 → 對應的 groom asset
       → attach groom to head bone

  玩家手動修正 UI：
    結果頁面顯示匹配結果，玩家可滑動瀏覽所有 groom 預覽縮圖，
    點選正確的那一個覆蓋 AI 的選擇。
```

**DINOv2 vs CLIP：**
- 圖對圖直接比對視覺特徵，不經過語言描述的有損轉換
- 參考對象是實際的 groom 渲染圖，不是抽象的文字 prompt
- 加新 groom = 渲染一張截圖 + encode，不需要想文字描述
- 模型更小（86MB vs 350MB）

---

## Groom 資產

### 來源

| 資產包 | 內容 | 價格 |
|--------|------|------|
| HairBuilder (Fab) | 128 個模組件，可組裝任意髮型 | $39.99 |
| PixelHair (按件) | ~200 個獨立寫實 groom，補充特殊髮質 | 按件 $10-25 |
| MetaHuman 免費 groom | Starter Kit + 社群免費 groom | $0 |

### 工作流程

```
1. 從資產包中挑選 / 組裝 groom
2. 每個 groom 在 Editor 裡正面渲染一張參考截圖
3. 批次跑 DINOv2 encode，產生 groom_embeddings.npy
4. 隨遊戲打包：groom assets + groom_embeddings.npy + 預覽縮圖
```

加減 groom 只需要重跑步驟 2-3，不需要重新訓練任何模型。

---

## 錯誤處理與回退機制

| 情境 | 處理方式 |
|------|---------|
| 偵測不到臉 | 提示「偵測不到臉，請上傳正面自拍」 |
| 頭部旋轉 > 25° | 提示「請面向鏡頭正面拍攝」 |
| hair_mask ≈ 0 但非光頭 | 判定為束髮 → UI 手動選散髮型 |
| DINOv2 髮型猜錯 | 玩家在結果頁滑動瀏覽所有 groom 手動選擇 |
| 膚色因光照偏差 | LAB 空間取中位數 + 忽略 L 通道極端值 |
| 髮色因光照假梯度 | 只用 LAB a/b 通道做 root vs tip 比對 |

---

## 部署規格

- **模型總大小**：~148MB ONNX，隨遊戲打包
- **Runtime 依賴**：onnxruntime (CPU)、numpy、opencv（或 C++ 等效）
- **處理時間**：< 200ms / 張自拍
- **雲端依賴**：零
- **訓練需求**：零
- **營運成本**：零
