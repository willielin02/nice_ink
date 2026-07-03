# Nice Ink — Selfie-to-Character Pipeline 完整規格

## 設計理念

不做捏臉系統。玩家上傳一張自拍照，系統自動生成角色：照片貼臉、膚色校準全身、髮型匹配 groom、髮色還原（含挑染/布丁頭/雙色）。全部裸體，陰部馬賽克。角色使用 We're GenZ 專案的胖體型人偶。

---

## 模型清單（全部 ONNX、全部本地、零訓練、零雲端）

| 模型 | 用途 | 大小 | 推論速度 (CPU) | 授權 |
|------|------|------|---------------|------|
| 3DDFA_V2 | 臉部偵測 + 3DMM fit + UV 正面化 + landmarks + 頭部姿態 | ~12MB | ~1.35ms | MIT |
| BiSeNet face parsing | 19 類臉部語義分割（skin, hair, eyes, nose, mouth...） | ~50MB | ~10ms | MIT |
| CLIP ViT-B/32 (OpenCLIP) | Zero-shot 髮型分類 | ~350MB | ~50ms | MIT |

**合計：~412MB，處理一張自拍 < 200ms。**

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
輸入：3DDFA_V2 的 UV texture map（正面化紋理）+ 68 landmarks
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

### ⑤ 髮色偵測（含挑染/布丁頭/雙色）

```
輸入：BiSeNet 的 hair_mask + 原始照片像素
處理：
  1. 垂直分區：hair bounding box 上半 = root zone，下半 = tip zone
  2. 各區做 K-means k=2 取主色（LAB 空間，只用 a/b 通道比對避免光照干擾）
  3. 全區做 K-means k=3 找所有主色及比例

判斷邏輯：
  ─ 全區色彩方差低 → SOLID（單色）
    └→ 參數：base_color
  ─ root 主色 vs tip 主色 LAB(a,b) 距離 > 閾值 → OMBRE（布丁頭/漸層）
    └→ 參數：root_color, tip_color, root_amount（從分界線位置推算）
  ─ 全區有 2+ 群且空間分佈交錯 → HIGHLIGHTS（挑染/雙色）
    └→ 參數：base_color, highlight_color, highlight_ratio

輸出：groom Dynamic Material Instance 的色彩參數
  ─ SOLID:      設 base melanin/color
  ─ OMBRE:      設 root_color + tip_color + root_amount
  ─ HIGHLIGHTS: 設 base_color + highlight_color + probability mask 灰度值
```

**UE5 材質端支援：**
- Root-to-tip 色彩梯度（groom 材質內建 root UV）
- Probability Mask 技術（per-strand 色彩決策，非梯度插值）
- Highlight mask 疊加

### ⑥ 髮型分類

```
輸入：BiSeNet 的 hair_mask + 原始照片 + CLIP 模型

分支判斷：
  A. hair_mask 面積 ≈ 0
     └→ 光頭，跳過 CLIP，直接指定光頭 groom（或無 groom）

  B. hair_mask 呈現束髮特徵（面積異常小但頂部有收攏痕跡）
     └→ 彈出 UI：
        「看起來你綁了頭髮！遊戲裡的角色頭髮會放下來，
         你的頭髮放下來最像哪一種？」
        [長直] [中直] [長波浪] [中捲] [讓我重拍]
        候選從 50 種裡縮小到 4-5 個最可能選項

  C. 正常散髮
     └→ 裁出含頭髮的區域（hair bounding box + padding）
     └→ CLIP encode image
     └→ 與 50 個髮型 prompt 的 text embeddings 做 cosine similarity
     └→ 取最高分 → 對應的 groom asset
     └→ attach groom to head bone

CLIP prompt 格式範例：
  "a person with a broccoli haircut"
  "a person with curtain bangs medium straight hair"
  "a person with a short fade haircut"

玩家手動修正 UI：
  如果 CLIP 猜錯，玩家可以在結果頁面
  滑動瀏覽所有 50 個 groom 的預覽縮圖，點選正確的那一個。
```

---

## 髮型 Groom 資產

### 來源與成本

| 資產包 | 用途 | 價格 |
|--------|------|------|
| HairBuilder (Fab) | 128 個模組件，預組裝大部分髮型 | $39.99 |
| PixelHair 補件 (5-6 個) | Afro / dreadlocks / braids 等 HairBuilder 覆蓋不到的 | ~$75 |
| MetaHuman 免費 groom 包 | Starter Kit / 免費社群 groom 補充 | $0 |
| **合計** | | **~$115** |

### 髮型清單（50 種，男女各 25）

來源：多家理髮店報告的 2025-2026 最常被要求髮型 + 時尚媒體趨勢文章交叉比對。
無法驗證的全球人口佔比數據不列入。

**男性（25 種）：**
花椰菜頭、逗號瀏海/韓式 two block、短 fade、crew cut、buzz cut、
光頭、側分短髮、紋理剪裁 textured crop、quiff/前蓬、pompadour、
後梳油頭、中分窗簾髮、Edgar cut、mullet、two block、
自然中長直、自然中長捲、長直髮、長捲髮、man bun、
短 afro、high top fade、短 dreadlocks、中長 dreadlocks、cornrows

**女性（25 種）：**
長直中分、長直側分、長直齊瀏海、長直窗簾瀏海、長直八字瀏海、
長波浪、長捲髮、長密捲/big hair、wolf cut/層次剪、公主切 hime cut、
lob 長 bob、經典 bob、波浪 bob、短 bob、pixie cut、
短捲女、中長窗簾瀏海波浪、中長自然捲、afro 女、box braids、
cornrows 女、長 locs 女、French bob + 瀏海、bixie、層次長捲

**清單的已知限制：**
- 來源是趨勢文章和理髮店自述，不是人口普查
- 流行髮型隨時間變動，清單應定期更新
- 可隨時增刪——加一個 groom asset + 加一行 CLIP prompt 就行

---

## 錯誤處理與回退機制

| 情境 | 處理方式 |
|------|---------|
| 照片中偵測不到臉 | 提示「偵測不到臉，請上傳正面自拍」 |
| 頭部旋轉角度 > 25° | 提示「請面向鏡頭正面拍攝」 |
| hair_mask 面積 ≈ 0 但非光頭 | 判定為束髮 → UI 選擇散髮類型 |
| CLIP 髮型猜錯 | 玩家可在結果頁滑動瀏覽 50 個 groom 手動選擇 |
| 膚色因光照偏差 | LAB 空間取中位數 + 忽略 L 通道極端值 |
| 髮色因光照造成假梯度 | 只用 LAB a/b 通道做 root vs tip 比對，忽略 L 通道 |

---

## 部署規格

- **模型總大小**：~412MB ONNX 檔，隨遊戲打包
- **Runtime 依賴**：onnxruntime (CPU)、numpy、opencv-python（或 C++ 等效）
- **處理時間**：< 200ms / 張自拍
- **雲端依賴**：零
- **訓練需求**：零
- **營運成本**：零
