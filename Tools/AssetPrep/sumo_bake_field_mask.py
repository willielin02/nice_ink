# 指示場遮罩烘焙 v2：場給符號、等值線距離給銳度
#   sd = sign(場-0.5) × dist3D(紋素, 等值線段集) → smoothstep(±F_HALF)
#   符號=擴散場（全域一致，不可能翻）；曲線=marching triangles 等值線（多面尺度平滑）；
#   銳度=逐紋素 3D 距離（次紋素平滑，不受頂點內插面折痕影響）。
# argv: <json> <out.png> [F_HALF=0.005] [SIZE=4096]
import json, sys
import numpy as np
import cv2
from pathlib import Path

S = Path(r"C:\Users\willi\AppData\Local\Temp\claude\c--games-Unreal-Engine-nice-ink\00d214a0-ef56-4e54-9786-cb494e647707\scratchpad")
JSON_IN = sys.argv[1]
PNG_OUT = sys.argv[2]
F_HALF = float(sys.argv[3]) if len(sys.argv) > 3 else 0.005
SIZE = int(sys.argv[4]) if len(sys.argv) > 4 else 4096

data = json.loads((S / JSON_IN).read_text())
fmap = np.zeros((SIZE, SIZE), np.float32)
pos_map = np.zeros((SIZE, SIZE, 3), np.float32)
written = np.zeros((SIZE, SIZE), bool)

for t in data["tris"]:
    uv = np.array(t["uv"], np.float64).reshape(3, 2)
    fv = np.array(t["f"], np.float64)
    pos = np.array(t["pos"], np.float64).reshape(3, 3)
    dst = np.stack([uv[:, 0] * SIZE, (1.0 - uv[:, 1]) * SIZE], axis=1)
    x0 = max(int(np.floor(dst[:, 0].min())) - 1, 0)
    y0 = max(int(np.floor(dst[:, 1].min())) - 1, 0)
    x1 = min(int(np.ceil(dst[:, 0].max())) + 1, SIZE)
    y1 = min(int(np.ceil(dst[:, 1].max())) + 1, SIZE)
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
    F = w0 * fv[0] + w1 * fv[1] + w2 * fv[2]
    P = (w0[..., None] * pos[0] + w1[..., None] * pos[1] + w2[..., None] * pos[2])
    sub_w = written[y0:y1, x0:x1]
    sel = inside & ~sub_w
    fmap[y0:y1, x0:x1][sel] = F[sel].astype(np.float32)
    pos_map[y0:y1, x0:x1][sel] = P[sel].astype(np.float32)
    sub_w[sel] = True

segs = np.array(data["iso_segs"], np.float64).reshape(-1, 2, 3)
hints_raw = np.array(data["iso_hints"], np.float64)
raw_mid = segs.mean(axis=1)
# 等值線折線 Taubin 平滑：符號在管域內由每段「內側提示」判（局部、平滑後仍可靠），
# 管域外用場符號——曲線可以放開熨平，不受場等值線失配約束
SMOOTH_PAIRS = int(sys.argv[5]) if len(sys.argv) > 5 else 40
TUBE = 0.020   # 管域半徑：> 平滑位移上限＋羽化；< 帶寬的一半
if SMOOTH_PAIRS > 0 and len(segs):
    key = lambda p: tuple(np.round(p, 5))
    adj = {}
    for a, b in segs:
        adj.setdefault(key(a), []).append(tuple(b))
        adj.setdefault(key(b), []).append(tuple(a))
    visited = set()
    new_segs = []
    for start in list(adj.keys()):
        if start in visited or len(adj[start]) != 2:
            continue
        loop = [np.array(start)]
        visited.add(start)
        cur, prev = start, None
        while True:
            nxts = [n for n in adj[cur] if key(np.array(n)) != prev]
            if not nxts:
                break
            nxt = key(np.array(nxts[0]))
            if nxt == start or nxt in visited:
                break
            loop.append(np.array(nxt))
            visited.add(nxt)
            prev, cur = cur, nxt
        if len(loop) < 4:
            continue
        P = np.array(loop)
        P0 = P.copy()
        for it in range(SMOOTH_PAIRS):
            for lam in (0.5, -0.53):   # Taubin：去折痕不收縮
                P = P + lam * ((np.roll(P, 1, axis=0) + np.roll(P, -1, axis=0)) / 2 - P)
        # 位移上限=0.7×管域：保證平滑曲線仍在提示可靠範圍內
        D = P - P0
        dn = np.linalg.norm(D, axis=1, keepdims=True)
        cap = 0.7 * TUBE
        P = P0 + D * np.minimum(1.0, cap / np.maximum(dn, 1e-12))
        disp = np.linalg.norm(P - P0, axis=1)
        print(f"LOOP n={len(P)} disp p90={np.percentile(disp,90)*1000:.1f}mm max={disp.max()*1000:.1f}mm")
        for i in range(len(P)):
            new_segs.append([P[i], P[(i + 1) % len(P)]])
    if new_segs:
        segs = np.array(new_segs, np.float64)
        print(f"ISO smoothed: {len(segs)} segs (Taubin {SMOOTH_PAIRS})")
# 每段提示 = 最近原始段的提示（平滑後段序重排，用位置對回）
seg_mid = segs.mean(axis=1)
hint_of = np.empty((len(segs), 3))
for j in range(len(segs)):
    k = np.argmin(((raw_mid - seg_mid[j]) ** 2).sum(axis=1))
    hint_of[j] = hints_raw[k]
# 提示沿段序平滑（孤立壞提示→邊緣缺口/針刺；new_segs 依迴圈序排列，鄰段相鄰）
for it in range(8):
    hint_of = hint_of + 0.5 * ((np.roll(hint_of, 1, axis=0) + np.roll(hint_of, -1, axis=0)) / 2 - hint_of)
hint_of /= (np.linalg.norm(hint_of, axis=1, keepdims=True) + 1e-12)
segs = segs.astype(np.float32)
hint_of = hint_of.astype(np.float32)
A = segs[:, 0]; B = segs[:, 1]
AB = B - A
ab2 = (AB * AB).sum(axis=1) + 1e-12
iy, ix = np.nonzero(written)
Ppix = pos_map[iy, ix]
f_sign = np.where(fmap[iy, ix] >= 0.5, 1.0, -1.0).astype(np.float32)
min_d = np.full(len(Ppix), 1e9, np.float32)
side_hint = np.zeros(len(Ppix), np.float32)
CHUNK = 400000
for i in range(0, len(Ppix), CHUNK):
    Pc = Ppix[i:i+CHUNK]
    d_seg = np.full(len(Pc), 1e9, np.float32)
    q_best = np.zeros((len(Pc), 3), np.float32)
    h_best = np.zeros((len(Pc), 3), np.float32)
    for j in range(len(segs)):
        tt = np.clip(((Pc - A[j]) @ AB[j]) / ab2[j], 0.0, 1.0)
        proj = A[j] + tt[:, None] * AB[j]
        dd = np.linalg.norm(Pc - proj, axis=1)
        upd = dd < d_seg
        if upd.any():
            d_seg[upd] = dd[upd]
            q_best[upd] = proj[upd]
            h_best[upd] = hint_of[j]
    min_d[i:i+CHUNK] = d_seg
    side_hint[i:i+CHUNK] = np.sign(((Pc - q_best) * h_best).sum(axis=1))
sign = np.where(min_d < TUBE, side_hint, f_sign)
sign = np.where(sign == 0, f_sign, sign)
sd = sign * min_d
t = np.clip(0.5 + sd / (2 * F_HALF), 0.0, 1.0)
mask = np.zeros((SIZE, SIZE), np.float32)
mask[iy, ix] = (t * t * (3 - 2 * t)).astype(np.float32)
print(f"RASTER written={int(written.sum())} segs={len(segs)} white={(mask>0.5).sum()}")

# 禁區＋gutter（同 bake_region_mask）
forbid = np.zeros((SIZE, SIZE), bool)
for row in data["other_uv"]:
    uv = np.array(row, np.float64).reshape(3, 2)
    dst = np.stack([uv[:, 0] * SIZE, (1.0 - uv[:, 1]) * SIZE], axis=1)
    x0 = max(int(np.floor(dst[:, 0].min())) - 1, 0)
    y0 = max(int(np.floor(dst[:, 1].min())) - 1, 0)
    x1 = min(int(np.ceil(dst[:, 0].max())) + 1, SIZE)
    y1 = min(int(np.ceil(dst[:, 1].max())) + 1, SIZE)
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
    forbid[y0:y1, x0:x1] |= (w0 >= -0.001) & (w1 >= -0.001) & (w2 >= -0.001)
# gutter＝最近寫入紋素取值（標籤填充）：max-dilate 會把高值跨島溝擴進鄰島邊緣
# （症狀：島縫上的針刺/三角細線）——各島各取各的邊值，溝內不互染
GUT = 12 * max(1, SIZE // 2048)
inv = (~written).astype(np.uint8)
dist, labels = cv2.distanceTransformWithLabels(inv, cv2.DIST_L2, 3,
                                               labelType=cv2.DIST_LABEL_PIXEL)
lut = np.zeros(int(labels.max()) + 1, np.float32)
lut[labels[written]] = mask[written]
fill = lut[labels]
mask = np.where(written, mask,
                np.where(forbid | (dist > GUT), 0.0, fill)).astype(np.float32)

cv2.imwrite(str(S / PNG_OUT), (np.clip(mask, 0, 1) * 255).astype(np.uint8))
import shutil, os
shutil.copy(S / PNG_OUT, os.path.join(r"C:\games\Unreal Engine\nice_ink\SourceAssets", PNG_OUT))
print(f"BAKED {PNG_OUT}")
