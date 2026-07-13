# 髮簾鋪圖：hair_strip.png 沿髮流座標場鋪進 UV0 髮區
# - 圓頂：繞髻方位角 φ → 簾 x（水平可平鋪，無縫）；z → 簾 y
# - 髻球：世界 x → 簾 x（辮向）；世界 y → 簾 y
# - mip 金字塔逐紋素 LOD：收束處取樣頻率發散 → 取更模糊層級（極點自動柔化成暗旋）
# - 4096 超採樣 → INTER_AREA 2048
import json
import numpy as np
import cv2
from pathlib import Path

S = Path(r"C:\Users\willi\AppData\Local\Temp\claude\c--games-Unreal-Engine-nice-ink\00d214a0-ef56-4e54-9786-cb494e647707\scratchpad")
SIZE = 2048
SS = 2
HI = SIZE * SS
W_M = 0.628                  # 簾寬對應頭圍（φ 一圈）
Z_TOP, Z_HAIRLINE = 1.745, 1.39
BUN_Z0, BUN_BAND = 1.645, 0.015
PAD = 8

data = json.loads((S / "hair_tris.json").read_text())
bun = np.array(data["bun_center"])
head = np.array(data["head_center"])
axis = bun - head
axis /= (np.linalg.norm(axis) + 1e-9)
tmp = np.array([0.0, 0.0, 1.0]) if abs(axis[2]) < 0.9 else np.array([1.0, 0.0, 0.0])
e1 = np.cross(axis, tmp); e1 /= np.linalg.norm(e1)
e2 = np.cross(axis, e1)

strip = cv2.imread(str(S / "hair_strip.png"))
SH, SW = strip.shape[:2]
BAKE_PPM = 0.617 * SS * 1000          # 烘焙空間 px/m
RATIO_MIN = (SW / W_M) / BAKE_PPM     # 簾解析度/烘焙解析度 的基準比

# mip 金字塔（每層水平 wrap padding，垂直 clamp）
levels = []
img = strip.astype(np.float32)
for k in range(7):
    pad = np.hstack([img[:, -PAD:], img, img[:, :PAD]])
    levels.append(pad)
    img = cv2.resize(img, (max(img.shape[1] // 2, 8), max(img.shape[0] // 2, 8)),
                     interpolation=cv2.INTER_AREA)

# ---- 光柵化髮區：pos_map ----
pos_map = np.zeros((HI, HI, 3), np.float32)
cover = np.zeros((HI, HI), bool)
for t in data["tris"]:
    uv = np.array(t["uv"], np.float64).reshape(3, 2)
    pos = np.array(t["pos"], np.float64).reshape(3, 3)
    dst = np.stack([uv[:, 0] * HI, (1.0 - uv[:, 1]) * HI], axis=1)
    x0 = max(int(np.floor(dst[:, 0].min())) - 1, 0)
    y0 = max(int(np.floor(dst[:, 1].min())) - 1, 0)
    x1 = min(int(np.ceil(dst[:, 0].max())) + 1, HI)
    y1 = min(int(np.ceil(dst[:, 1].max())) + 1, HI)
    if x1 <= x0 or y1 <= y0:
        continue
    xs, ys = np.meshgrid(np.arange(x0, x1) + 0.5, np.arange(y0, y1) + 0.5)
    d = dst
    det = (d[1,1]-d[2,1])*(d[0,0]-d[2,0]) + (d[2,0]-d[1,0])*(d[0,1]-d[2,1])
    if abs(det) < 1e-9:
        continue
    w0 = ((d[1,1]-d[2,1])*(xs-d[2,0]) + (d[2,0]-d[1,0])*(ys-d[2,1])) / det
    w1 = ((d[2,1]-d[0,1])*(xs-d[2,0]) + (d[0,0]-d[2,0])*(ys-d[2,1])) / det
    w2 = 1.0 - w0 - w1
    inside = (w0 >= -0.001) & (w1 >= -0.001) & (w2 >= -0.001)
    if not inside.any():
        continue
    P = (w0[..., None] * pos[0] + w1[..., None] * pos[1] + w2[..., None] * pos[2])
    sub_c = cover[y0:y1, x0:x1]
    sel = inside & ~sub_c
    pos_map[y0:y1, x0:x1][sel] = P[sel].astype(np.float32)
    sub_c[inside] = True

iy, ix = np.nonzero(cover)
P = pos_map[iy, ix].astype(np.float64)
rel = P - bun
a1 = rel @ e1
a2 = rel @ e2
phi = np.arctan2(a2, a1)
r_ax = np.sqrt(a1 * a1 + a2 * a2)
z = P[:, 2]
x_w = P[:, 0]
y_w = P[:, 1]

# 圓頂簾座標
sx_d = (phi + np.pi) / (2 * np.pi) * SW
t_d = np.clip((Z_TOP - z) / (Z_TOP - Z_HAIRLINE), 0.0, 1.0)
sy_d = t_d * (SH - 1)
# 髻球簾座標（辮向沿 y）
sx_b = np.mod(x_w / W_M * SW, SW)
bun_sel = z > BUN_Z0 - 0.01
yb = y_w[bun_sel]
yb_min, yb_max = (yb.min(), yb.max()) if bun_sel.any() else (0, 1)
sy_b = np.clip((y_w - yb_min) / max(yb_max - yb_min, 1e-6), 0, 1) * (SH - 1)
w_bun = np.clip((z - BUN_Z0) / BUN_BAND, 0.0, 1.0)

# LOD：φ 方向取樣比隨半徑發散
ratio = np.maximum((SW / (2 * np.pi * np.maximum(r_ax, 1e-4))) / BAKE_PPM, RATIO_MIN)
lod_d = np.clip(np.round(np.log2(ratio)), 0, 6).astype(int)
lod_b = int(np.clip(round(np.log2(RATIO_MIN)), 0, 6))


def sample(level_k, sx, sy, mask):
    # remap 的 map 單維上限 32767 → 攤成近方形網格
    lv = levels[level_k]
    s = 2.0 ** level_k
    n = len(sx)
    hgrid = 1024
    wgrid = (n + hgrid - 1) // hgrid
    mx = np.zeros(hgrid * wgrid, np.float32)
    my = np.zeros(hgrid * wgrid, np.float32)
    mx[:n][mask] = (sx[mask] / s) + PAD - 0.5     # wrap pad 偏移
    my[:n][mask] = np.clip(sy[mask] / s, 0, lv.shape[0] - 1.001) - 0.5
    out = cv2.remap(lv, mx.reshape(hgrid, wgrid), my.reshape(hgrid, wgrid),
                    cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)
    return out.reshape(-1, 3)[:n]


N = len(ix)
col_d = np.zeros((N, 3), np.float32)
for k in range(7):
    m = lod_d == k
    if not m.any():
        continue
    # pad 是原尺度 8px，各層 pad 同為 8px（各層獨立 pad）→ 直接 +PAD
    col_d[m] = sample(k, sx_d, sy_d, m)[m]
col_b = sample(lod_b, sx_b, sy_b, np.ones(N, bool))
col = col_d * (1 - w_bun[:, None]) + col_b * w_bun[:, None]

tint = np.zeros((HI, HI, 3), np.float32)
tint[iy, ix] = col

# HI 端 gutter 填充 → 降採樣 → 2K 外擴
k3 = np.ones((3, 3), np.uint8)
for it in range(8):
    grown = cv2.dilate(tint, k3)
    empty = (tint.sum(axis=2) == 0)
    tint[empty] = grown[empty]
tint2 = cv2.resize(tint, (SIZE, SIZE), interpolation=cv2.INTER_AREA)
for it in range(12):
    grown = cv2.dilate(tint2, k3)
    empty = (tint2.sum(axis=2) == 0)
    tint2[empty] = grown[empty]

# 對比曲線：黑髮的「黑」= 吸光；漫射渲染下深灰會發亮 → 底壓黑、亮絲保住
BP, WP = 0.15, 0.85
tint2 = np.clip((tint2 / 255.0 - BP) / (WP - BP), 0, 1) * 255.0
cv2.imwrite(str(S / "hair_tint.png"), np.clip(tint2, 0, 255).astype(np.uint8))
import shutil
shutil.copy(S / "hair_tint.png", r"C:\games\Unreal Engine\nice_ink\SourceAssets\hair_tint.png")
print(f"MAPPED texels={N} lod_hist={[int((lod_d==k).sum()) for k in range(7)]} bun_px={int((w_bun>0.5).sum())}")
