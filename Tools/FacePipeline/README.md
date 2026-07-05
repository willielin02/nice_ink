# FacePipeline — 自拍 → 光頭 avatar（黑道桑拿版）

> 2026-07-04 起的生產管線。前身是 `nice_ink_face_pipeline` repo（已退役封存，
> 完整髮型/鬍子時代狀態在該 repo 的 snapshot 與本 repo `v1-hair-era` branch）。
> 端到端設計說明見舊 repo 的 `AVATAR_PIPELINE.md`（A 節貼圖管線仍準確；
> B 髮型比對 / D 鬍子管線已隨題材轉向擱置）。

## 兩條命令

```bash
# 1. 自拍 → 臉貼圖 + 膚色（v4.8 LaMa 路線；頭髮+落腮鬍移除 = 光頭素臉）
<python> selfie_to_face_texture.py test_selfies/<selfie>.jpg
#   輸出到 out/：face_texture.png (2048² RGBA)、face_texture_eyes_closed.png
#   （閉眼睡臉變體：眼眶 inpaint 回眼瞼皮膚＋沿下眼瞼畫閉眼線；遊戲中昏睡
#   狀態用，UE 端以 FaceTex 參數切換）、eye_mask.png（眼球開口區，FaceUV 空間）、
#   skin_color.json、hair_color.json、beard_color.json、face_flags.json、
#   seam_qa.*、debug_*

# 1b. 禁畫眼球遮罩：FaceUV 空間 → 墨水圖集 UV0 空間（白=可畫、黑=眼球）
#     M_InkBodyChar 以 InkEyeMask 參數把它乘入 marker/tattoo 層（膠帶語義）
#     對應表 data/char17_ink_uv_map.json 由 export_ink_uv_map.py（Blender）產出，
#     char17 網格不變就不用重跑
<python> bake_eye_ink_mask.py out/eye_mask.png out/eye_mask_ink.png

# 2. 組裝光頭 avatar（char16 + 臉貼圖 + 分層皮膚材質；圖像全打包）
blender --background --python make_avatar_blend.py -- avatar_previews/<name>.blend

# （選配）QA 渲染——僅供參考，驗收以用戶 viewport 為準
blender --background avatar_previews/<name>.blend --python render_avatar_preview.py -- <絕對路徑前綴>
```

- `<python>`：暫用舊 repo 的 venv：`..\..\..\nice_ink_face_pipeline\venv\Scripts\python.exe`
  （torch / mediapipe / onnxruntime / face_parsing 都裝在那裡；未來要嘛建本地
  requirements，要嘛等 UE NNE 移植後淘汰）。
- Blender：`C:\Program Files\Blender Foundation\Blender 5.1\blender.exe`。
- 角色源：`Content/玩家/nice_ink_player_character17.blend`（全專案唯一角色源，
  = char16 + 屁股 mesh 修形 2026-07-05；未來換黑道體型時 FaceUV 島 +
  FaceMask 邏輯保留）。

## 目錄

| 目錄 | 內容 |
|---|---|
| `data/` | 管線常數：FaceUV 擴張島 mapping、皮膚 tiles、zone tint（**不隨玩家變**） |
| `models/` | ML 權重（不進 git）：BiSeNet `79999_iter.pth`、MediaPipe、LaMa ONNX |
| `out/` | 每次執行的輸出（下一位玩家會覆蓋；avatar blend 打包後不受影響） |
| `test_selfies/` | 測試自拍：`7AF4...` = 本人、`d74d...` = CaseOh（鬍子基準） |
| `avatar_previews/` | 組裝出的 avatar .blend + QA 渲圖 |

注：`data/` 內的 `skin_color.json`、`seam_qa.*`、`content_mask.png` 是遷移時
誤帶進來的舊輸出副本，管線不讀它們（真輸出在 `out/`）。

## v7（2026-07-04）：光頭版契約修正——鬍子保留、去光照、曝光正規化、填充區淨化

> v8「鬍域固化」（閉合＋擴張＋LaMa 續毛＋色調承諾）已實作並在 2026-07-05
> 依用戶裁決整組退回（用戶偏好 v7 的鬍子觀感）。完整設計與教訓見專案記憶。

對應用戶三大問題（臉周斑駁、鬍子被移除、粉白照片）：

1. **入口曝光正規化＋白平衡守則**（步驟 4 內）：皮膚灰階中值出 [115,152] 帶
   → 線性增益拉回 132；皮膚中值 B≥G（洋紅色偏，違反「皮膚恆 G>B」物理不變量）
   → B 通道增益修正。全部在任何取樣之前。
2. **落腮鬍保留入貼圖**（`BEARD_KEEP_IN_TEXTURE=True`，規格變更）：光頭遊戲沒有
   鬍子 mesh——v4「mesh-borne 刮除」契約作廢。BiSeNet 標成 hair 的鬍子像素
   grafted 回 head mask；填充取樣環**不再排除**鬍色（那正是鬍子向外擴散的機制）；
   seam ring 豁免鬍鬚領地（否則 LaMa 會把鬍緣重生掉）。v4.9 刮鬍重生路徑僅
   `BEARD_KEEP_IN_TEXTURE=False` 時活著（A/B 用）。
3. **填充區平滑化**（步驟 8c）：內容邊界外 16px 起 σ8 低通——填充環的
   TPS 放大照片雜訊（臉周斑駁本體）出局，皮膚質感由材質 tiles 提供。
   QA：fill-zone high-freq energy（目標 <0.8，修前實測 2.5）。
4. **皮膚探針 flat-field 去光照**（步驟 8d，MeInGame/Make-A-Character 神經
   delighting 的古典等價）：從 skin-label 探針估計平滑光照場，線性空間除掉、
   目標 = 身體常數（指數 0.9、比值場按探針均值重對中到 T——中值目標 vs 均值
   探針的偏差曾把臉壓暗）。乘法＋平滑 → 鬍/眉/毛孔的局部對比構造性不變。
   QA：face-core dE vs body。
   seam QA 側差增加皮膚標籤特徵豁免（鬍緣/眉尾穿越不算髒縫）。

## v6（2026-07-04）：Poisson 膜縫合 + 眉毛鏡像補全

- **步驟 10b Poisson 膜**（`poisson_membrane_to_skin`）：調和偏移場讓 island 邊界
  逐位元等於 `skin_color` 常數（= 身體材質渲染值）——臉身界線構造性消失；內部梯度
  （鬍渣場、眉毛、毛孔）零改動。邊界值只取「填充環段」（底部內容穿越段會注入
  假目標）；粗網格解完向外延拓再上採樣（否則邊界值被外側 0 稀釋）；最外 5px 精確
  貼齊常數。QA：`island-edge dE`（mean <1 / max ~3 = 達標）。
- **眉毛鏡像**（單側遮擋 ≥0.25 且另一側乾淨時觸發）：只移植「眉毛筆畫」像素
  （整框貼補會把供體側光照搬過去）；MediaPipe 兩條眉鏈方向相反，配對必須反轉
  受體鏈並加眼角點（共線鏈的垂直方向欠約束——第一版畫出漂浮斷筆）。
- seam QA 新增特徵穿越豁免（內側環亮度 <0.9× 外側 = 眉尾等特徵，不計入皮膚色差）。
- v5 flatten（把低頻拉平）已停用保留：它會漂白鬍渣場——鬍渣是辨識度內容。

## 設計鐵律（違反任何一條都實際炸過）

1. **五官相對位置神聖不可扭曲**——只做剛性對齊，永不 landmark warp。
2. **臉是貼圖、頭髮/落腮鬍移除**——v4 契約；人中鬍、眉毛、皺紋留在貼圖。
3. **LaMa 三鐵律**——只跑在無髮填充圖上；遮罩全覆蓋目標；保護區用事後貼回。
4. **驗收者是玩家的眼睛**——每階段產 QA 產物；接縫指標看不見區內品質。

## 接縫 QA 基準（v4.8，遷移後 2026-07-04 復測一致）

| 自拍 | side>10 | line>5 |
|---|---|---|
| 7AF4 | 4% | 5% |
| CaseOh | 0% | 2% |
