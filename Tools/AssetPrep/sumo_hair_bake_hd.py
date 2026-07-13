# 髮貼圖逐紋素烘焙 v2（島狀 HairUV）——內部面級斷裂的根治版
# - 逐紋素精確求場值（無面內線性內插誤差——解析 UV 版的病根）
# - 兩段光柵化：嚴格內部 → 外擴容差（沿三角形平面一階外插場內容進 gutter，
#   島縫兩側次紋素連續——v15 的零階複製 gutter 病根）
# - 圓頂↔髻球逐紋素軟混合（順帶取代 v19 的硬接環）
# - 輸出 tint/rough/normal 三張（含沿絲向連續高度場）
import json
import numpy as np
import cv2
from pathlib import Path

S = Path(r"C:\Users\willi\AppData\Local\Temp\claude\c--games-Unreal-Engine-nice-ink\00d214a0-ef56-4e54-9786-cb494e647707\scratchpad")
SIZE = 2048
SS = 2
HI = SIZE * SS
W_M = 0.628
Z_TOP, Z_HAIRLINE = 1.745, 1.30
BUN_Z0, BUN_BAND = 1.645, 0.015
PAD = 8
BUMP = 2.2

data = json.loads((S / "hair_tris_hairuv.json").read_text())
old = json.loads((S / "hair_tris.json").read_text())
bun = np.array(old["bun_center"])
head = np.array(old["head_center"])
axis = bun - head
axis /= (np.linalg.norm(axis) + 1e-9)
tmp = np.array([0.0, 0.0, 1.0]) if abs(axis[2]) < 0.9 else np.array([1.0, 0.0, 0.0])
e1 = np.cross(axis, tmp); e1 /= np.linalg.norm(e1)
e2 = np.cross(axis, e1)

# 密度自算（不依賴外部參數）
uvA = pA = 0.0
for t in data["tris"]:
    uv = np.array(t["uv"]).reshape(3, 2)
    ps = np.array(t["pos"]).reshape(3, 3)
    uvA += abs((uv[1,0]-uv[0,0])*(uv[2,1]-uv[0,1]) - (uv[2,0]-uv[0,0])*(uv[1,1]-uv[0,1])) / 2
    pA += np.linalg.norm(np.cross(ps[1]-ps[0], ps[2]-ps[0])) / 2
DENSITY = SIZE * np.sqrt(uvA / pA) / 1000
BAKE_PPM = DENSITY * SS * 1000
print(f"DENSITY {DENSITY:.2f} px/mm")

strip = cv2.imread(str(S / "hair_strip.png"))
SH, SW = strip.shape[:2]
RATIO_MIN = (SW / W_M) / BAKE_PPM
luma_s = strip.astype(np.float32).mean(axis=2)
h_s = cv2.blur(luma_s, (1, 401))   # 沿絲向長模糊：高度連續（凸起從根到梢）
levels = []
img = np.dstack([strip.astype(np.float32), h_s])
for k in range(7):
    pad = np.hstack([img[:, -PAD:], img, img[:, :PAD]])
    levels.append(pad)
    img = cv2.resize(img, (max(img.shape[1] // 2, 8), max(img.shape[0] // 2, 8)),
                     interpolation=cv2.INTER_AREA)

# ---- 兩段光柵化：pos_map（嚴格→外擴外插）----
pos_map = np.zeros((HI, HI, 3), np.float32)
cover = np.zeros((HI, HI), np.uint8)   # 1=內部 2=外插 gutter


def raster(tol, mark):
    for t in data["tris"]:
        uv = np.array(t["uv"], np.float64).reshape(3, 2)
        pos = np.array(t["pos"], np.float64).reshape(3, 3)
        dst = np.stack([uv[:, 0] * HI, (1.0 - uv[:, 1]) * HI], axis=1)
        m = 4 if mark == 2 else 1
        x0 = max(int(np.floor(dst[:, 0].min())) - m, 0)
        y0 = max(int(np.floor(dst[:, 1].min())) - m, 0)
        x1 = min(int(np.ceil(dst[:, 0].max())) + m, HI)
        y1 = min(int(np.ceil(dst[:, 1].max())) + m, HI)
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
        inside = (w0 >= tol) & (w1 >= tol) & (w2 >= tol)
        if not inside.any():
            continue
        P = (w0[..., None] * pos[0] + w1[..., None] * pos[1] + w2[..., None] * pos[2])
        sub = cover[y0:y1, x0:x1]
        sel = inside & (sub == 0)
        pos_map[y0:y1, x0:x1][sel] = P[sel].astype(np.float32)
        sub[sel] = mark


raster(-0.001, 1)          # 嚴格內部
raster(-0.25, 2)           # 外插 gutter：沿面平面延伸場（一階連續）
print(f"RASTER interior={int((cover==1).sum())} gutter={int((cover==2).sum())}")

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

sx_d = (phi + np.pi) / (2 * np.pi) * SW
t_d = np.clip((Z_TOP - z) / (Z_TOP - Z_HAIRLINE), 0.0, 1.0)
sy_d = 8 + t_d * (SH - 17)
sx_b = np.mod(x_w / W_M * SW, SW)
bun_sel = z > BUN_Z0 - 0.01
yb = y_w[bun_sel]
yb_min, yb_max = (yb.min(), yb.max()) if bun_sel.any() else (0, 1)
sy_b = 8 + np.clip((y_w - yb_min) / max(yb_max - yb_min, 1e-6), 0, 1) * (SH - 17)
w_bun = np.clip((z - BUN_Z0) / BUN_BAND, 0.0, 1.0)

ratio = np.maximum((SW / (2 * np.pi * np.maximum(r_ax, 1e-4))) / BAKE_PPM, RATIO_MIN)
lod_d = np.clip(np.round(np.log2(np.maximum(ratio, 1.0))), 0, 6).astype(int)
lod_b = int(np.clip(round(np.log2(max(RATIO_MIN, 1.0))), 0, 6))


def sample(level_k, sx, sy, mask):
    lv = levels[level_k]
    s = 2.0 ** level_k
    n = len(sx)
    hgrid = 1024
    wgrid = (n + hgrid - 1) // hgrid
    mx = np.zeros(hgrid * wgrid, np.float32)
    my = np.zeros(hgrid * wgrid, np.float32)
    mx[:n][mask] = (sx[mask] / s) + PAD - 0.5
    my[:n][mask] = np.clip(sy[mask] / s, 0, lv.shape[0] - 1.001) - 0.5
    out = cv2.remap(lv, mx.reshape(hgrid, wgrid), my.reshape(hgrid, wgrid),
                    cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)
    return out.reshape(-1, 4)[:n]


N = len(ix)
col_d = np.zeros((N, 4), np.float32)
for k in range(7):
    m = lod_d == k
    if not m.any():
        continue
    col_d[m] = sample(k, sx_d, sy_d, m)[m]
col_b = sample(lod_b, sx_b, sy_b, np.ones(N, bool))
col = col_d * (1 - w_bun[:, None]) + col_b * w_bun[:, None]

tex = np.zeros((HI, HI, 4), np.float32)
tex[iy, ix] = col
k3 = np.ones((3, 3), np.uint8)
for it in range(4):
    grown = cv2.dilate(tex, k3)
    empty = (tex[..., :3].sum(axis=2) == 0)
    tex[empty] = grown[empty]
tex2 = cv2.resize(tex, (SIZE, SIZE), interpolation=cv2.INTER_AREA)
for it in range(8):
    grown = cv2.dilate(tex2, k3)
    empty = (tex2[..., :3].sum(axis=2) == 0)
    tex2[empty] = grown[empty]
h_mapped = tex2[..., 3] / 255.0
tint2 = tex2[..., :3]

# 高度 → 法線/粗糙度
lo, hi = np.percentile(h_mapped, 5), np.percentile(h_mapped, 95)
hgt = np.clip((h_mapped - lo) / max(hi - lo, 1e-6), 0, 1).astype(np.float32)
hgt = cv2.GaussianBlur(hgt, (0, 0), 1.0)
gx = cv2.Sobel(hgt, cv2.CV_32F, 1, 0, ksize=3) * BUMP
gy = cv2.Sobel(hgt, cv2.CV_32F, 0, 1, ksize=3) * BUMP
ln_ = np.sqrt(gx * gx + gy * gy + 1.0)
nrm = np.stack([-gx / ln_, gy / ln_, 1.0 / ln_], axis=2)
cv2.imwrite(str(S / "hair_normal_hd.png"),
            (((nrm * 0.5 + 0.5) * 255.0)[..., ::-1]).astype(np.uint8))
rough_map = (0.45 - hgt * 0.15) * 255.0
cv2.imwrite(str(S / "hair_rough_hd.png"), rough_map.astype(np.uint8))

# tint 黑化曲線
n = tint2 / 255.0
tint2 = (0.02 + (n ** 1.5) * 0.33) * 255.0
cv2.imwrite(str(S / "hair_tint_hd.png"), np.clip(tint2, 0, 255).astype(np.uint8))

import shutil
SA = r"C:\games\Unreal Engine\nice_ink\SourceAssets"
for f in ("hair_tint_hd.png", "hair_normal_hd.png", "hair_rough_hd.png"):
    shutil.copy(S / f, SA + "\\" + f)
print(f"BAKED_HD2 texels={N} lod_hist={[int((lod_d==k).sum()) for k in range(7)]}")
