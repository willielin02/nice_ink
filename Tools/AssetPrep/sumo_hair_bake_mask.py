# 髮區遮罩 v2：平滑髮際線（帶符號距離）
# user 的標記界線是面級鋸齒（振幅 ~2cm）——舊羽化只暈值不改形，鋸齒原樣可見。
# 正解：邊界線段串成迴圈 → 拉普拉斯平滑 → 遮罩 = smoothstep(帶符號距離/過渡寬)
#   髮側(+d)、帶面/膚側(−d)；等值線=平滑曲線，鋸齒兩側各自收/溢。
import json
import numpy as np
import cv2
from pathlib import Path

S = Path(r"C:\Users\willi\AppData\Local\Temp\claude\c--games-Unreal-Engine-nice-ink\00d214a0-ef56-4e54-9786-cb494e647707\scratchpad")
SIZE = 2048
F_HALF = 0.008      # 過渡半寬 8mm（全寬 16mm，跨鋸齒兩側）
SMOOTH_PAIRS = 12   # Taubin λ/µ 對數：只消面級鋸齒（1~2 段邊尺度），
                    # 保留 user 標記的髮際結構（60 輪純拉普拉斯曾磨成圓，退貨）
T_LAM, T_MU = 0.5, -0.53

data = json.loads((S / "hair_tris.json").read_text())

# ---- 邊界段 → 有序迴圈 → 平滑 ----
segs_raw = np.array(data["boundary_segs"], np.float64).reshape(-1, 2, 3)
key = lambda p: tuple(np.round(p, 5))
adj = {}
for a, b in segs_raw:
    adj.setdefault(key(a), []).append(tuple(b))
    adj.setdefault(key(b), []).append(tuple(a))
visited = set()
loops = []
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
        if nxt == start:
            break
        if nxt in visited:
            break
        loop.append(np.array(nxt))
        visited.add(nxt)
        prev, cur = cur, nxt
    if len(loop) >= 4:
        loops.append(np.array(loop))
print(f"LOOPS n={len(loops)} sizes={[len(l) for l in loops]}")

# 內向提示：每個迴圈頂點指向附近髮面質心（判側用——符號絕不能用面類別：
# 凸出平滑曲線的鋸齒髮面會永遠全不透明，尖角原樣活下來）
tri_cent = np.array([np.array(t["pos"], np.float64).reshape(3, 3).mean(axis=0)
                     for t in data["tris"]])
smooth_segs = []
seg_hints = []
for lp in loops:
    P = lp.copy()
    H = np.zeros_like(P)
    for i, v in enumerate(P):
        dd = np.linalg.norm(tri_cent - v, axis=1)
        near = tri_cent[np.argsort(dd)[:8]]
        h = near.mean(axis=0) - v
        H[i] = h / (np.linalg.norm(h) + 1e-9)
    for it in range(SMOOTH_PAIRS):
        for lam in (T_LAM, T_MU):   # Taubin：λ 收、µ 放——去鋸齒不收縮
            P = P + lam * ((np.roll(P, 1, axis=0) + np.roll(P, -1, axis=0)) / 2 - P)
            H = H + lam * ((np.roll(H, 1, axis=0) + np.roll(H, -1, axis=0)) / 2 - H)
    H /= (np.linalg.norm(H, axis=1, keepdims=True) + 1e-9)
    for i in range(len(P)):
        j = (i + 1) % len(P)
        smooth_segs.append([P[i], P[j]])
        seg_hints.append([H[i], H[j]])
segs = np.array(smooth_segs, np.float32)
hints = np.array(seg_hints, np.float32)   # (n,2,3) 段兩端的內向提示
A = segs[:, 0]; B = segs[:, 1]
AB = B - A
ab2 = (AB * AB).sum(axis=1) + 1e-12
print(f"SMOOTH segs={len(segs)}")

# ---- 光柵化：髮面(+1)＋帶面(−1) ----
pos_map = np.zeros((SIZE, SIZE, 3), np.float32)
cls_map = np.zeros((SIZE, SIZE), np.int8)


def raster(tris, cls):
    for t in tris:
        uv = np.array(t["uv"], np.float64).reshape(3, 2)
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
        P = (w0[..., None] * pos[0] + w1[..., None] * pos[1] + w2[..., None] * pos[2])
        sub_c = cls_map[y0:y1, x0:x1]
        sel = inside & (sub_c == 0)
        pos_map[y0:y1, x0:x1][sel] = P[sel].astype(np.float32)
        sub_c[sel] = cls


raster(data["tris"], 1)
raster(data["band_tris"], -1)
print(f"RASTER hair={int((cls_map==1).sum())} band={int((cls_map==-1).sum())}")

iy, ix = np.nonzero(cls_map != 0)
Ppix = pos_map[iy, ix]
min_d = np.full(len(Ppix), 1e9, np.float32)
side = np.zeros(len(Ppix), np.float32)
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
            hj = hints[j][0][None, :] * (1 - tt[upd, None]) + hints[j][1][None, :] * tt[upd, None]
            h_best[upd] = hj
    min_d[i:i+CHUNK] = d_seg
    side[i:i+CHUNK] = np.sign(((Pc - q_best) * h_best).sum(axis=1))
sd = side * min_d
mask_v = np.clip(0.5 + sd / (2 * F_HALF), 0.0, 1.0)
mask = np.zeros((SIZE, SIZE), np.float32)
mask[iy, ix] = mask_v

# gutter：向「無人佔用」處外擴；純禁區（非帶面的其他島）不可寫
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
written = cls_map != 0
keep = written | (forbid & ~written)
k3 = np.ones((3, 3), np.uint8)
for it in range(6):
    grown = cv2.dilate(mask, k3)
    mask = np.where(keep, mask, np.maximum(mask, grown))

cv2.imwrite(str(S / "hair_mask.png"), (np.clip(mask, 0, 1) * 255).astype(np.uint8))
import shutil
shutil.copy(S / "hair_mask.png", r"C:\games\Unreal Engine\nice_ink\SourceAssets\hair_mask.png")
print(f"BAKED mask2  px={(mask>0.5).sum()}")
