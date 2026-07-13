# 布緣衍生烘焙：從手繪 fundoshi_mask.png（正源，不動）產出
#   fundoshi_mask_sharp.png  —— 銳化遮罩（過渡 ±SHARP_MM），材質取代軟遮罩
#   fundoshi_edge_shadow.png —— 邊緣接觸陰影（布側快衰減=捲邊、皮側慢衰減=壓肉接觸影）
# UV 空間距離運算的跨島安全：先用 label-fill 把各島內容延伸進溝（各取各的、不互染），
# 距離特徵只有 ~4px 深，溝內延伸 GUT=24px 足夠隔離（承 v48 gutter 鐵律）。
# argv: [mask.png] [field.json(供全身覆蓋圖)] 皆有預設
import sys
import numpy as np
import cv2, json
from pathlib import Path

SA = Path(r"C:\games\Unreal Engine\nice_ink\SourceAssets")
S = Path(r"C:\Users\willi\AppData\Local\Temp\claude\c--games-Unreal-Engine-nice-ink\00d214a0-ef56-4e54-9786-cb494e647707\scratchpad")
MASK = sys.argv[1] if len(sys.argv) > 1 else str(SA / "fundoshi_mask.png")
FIELD = sys.argv[2] if len(sys.argv) > 2 else str(S / "fundoshi_field.json")
SHARP_MM = 0.75      # 銳化過渡半寬
SH_DEPTH = 0.25      # 接觸陰影最深（邊界處乘 0.75）
SH_IN_MM = 0.5       # 布側衰減（捲邊細線）
SH_OUT_MM = 1.5      # 皮側衰減（接觸影）
GUT = 24

m = cv2.imread(MASK, cv2.IMREAD_GRAYSCALE).astype(np.float32) / 255.0
SIZE = m.shape[0]
PXMM = (0.617 * SIZE / 2048)   # px per mm（uv0_uniform 實測密度等比）

# 全身覆蓋圖（島內=True）：field json 的 tris+other_uv=全部面
data = json.loads(Path(FIELD).read_text())
cover = np.zeros((SIZE, SIZE), np.uint8)
def rast(uvrows):
    for row in uvrows:
        uv = np.array(row, np.float64).reshape(3, 2)
        pts = np.stack([uv[:, 0] * SIZE, (1.0 - uv[:, 1]) * SIZE], axis=1).astype(np.int32)
        cv2.fillPoly(cover, [pts], 1)
rast([t["uv"] for t in data["tris"]])
rast(data["other_uv"])
cover = cover.astype(bool)
print(f"COVER {int(cover.sum())} texels")

# 島內容延伸進溝（label-fill，各島各取各的）
inv = (~cover).astype(np.uint8)
dist_g, labels = cv2.distanceTransformWithLabels(inv, cv2.DIST_L2, 3,
                                                 labelType=cv2.DIST_LABEL_PIXEL)
lut = np.zeros(int(labels.max()) + 1, np.float32)
lut[labels[cover]] = m[cover]
m_ext = np.where(cover, m, np.where(dist_g <= GUT, lut[labels], 0.0)).astype(np.float32)

# 帶符號距離（mm，布內為正）
M = (m_ext >= 0.5).astype(np.uint8)
din = cv2.distanceTransform(M, cv2.DIST_L2, 3)
dout = cv2.distanceTransform(1 - M, cv2.DIST_L2, 3)
sd = np.where(M > 0, din, -dout) / PXMM

# 銳化遮罩
t = np.clip((sd + SHARP_MM) / (2 * SHARP_MM), 0.0, 1.0)
sharp = t * t * (3 - 2 * t)
# 接觸陰影：邊界最深、兩側各自衰減
ring = np.where(sd >= 0, np.exp(-sd / SH_IN_MM), np.exp(sd / SH_OUT_MM))
shadow = 1.0 - SH_DEPTH * ring

# 溝外歸零/歸一（超出 GUT 的無主區）
sharp = np.where(cover | (dist_g <= GUT), sharp, 0.0)
shadow = np.where(cover | (dist_g <= GUT), shadow, 1.0)

cv2.imwrite(str(SA / "fundoshi_mask_sharp.png"), (np.clip(sharp, 0, 1) * 255).astype(np.uint8))
cv2.imwrite(str(SA / "fundoshi_edge_shadow.png"), (np.clip(shadow, 0, 1) * 255).astype(np.uint8))
print(f"BAKED sharp+edge_shadow  (SHARP_MM={SHARP_MM} SH_DEPTH={SH_DEPTH})")
