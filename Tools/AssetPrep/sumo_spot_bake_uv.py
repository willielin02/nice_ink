"""把斑普查的結果烘成 UV0 貼圖（2026-08-24，唯讀輸出一張 PNG）。

用途＝**讓 user 的眼睛和我的尺共用同一個座標系**：這張圖會被當成麥克筆墨層貼到角色身上
（`NiSpotMap` 指令），玩家在自己的 viewport 走到他看到斑的地方，就能當場看到我的尺有沒有
把它標紅。褌邊那六天的教訓＝**先證明「我量的那條線＝他看的那條線」**，這張圖就是那個證明。

紅＝超過門檻（越紅越嚴重）、透明＝乾淨。UV0 與墨水同一套版面（0.617 px/mm @4096）。
輸入：Saved/SpotCensus/spots.npz（先跑 sumo_spot_census.py）
輸出：SourceAssets/spot_map.png（RGBA 2048²）
"""
import bpy
import os
import numpy as np

ROOT = r"C:\games\Unreal Engine\nice_ink"
CEN = os.path.join(ROOT, "Saved", "SpotCensus", "spots.npz")
OUTP = os.path.join(ROOT, "SourceAssets", "spot_map.png")
RES = int(os.environ.get("RES", "2048"))
BLEND = os.environ.get("BLEND", os.path.join(ROOT, "SourceAssets", "sumo_character_master.blend"))

bpy.ops.wm.open_mainfile(filepath=BLEND)
me = bpy.data.objects["SumoRetopo"].data
n = len(me.vertices)
D = np.load(CEN)
rough = D["rough"]
hot = D["hot"]
THR = float(D["thr"])

lv = np.empty(len(me.loops), np.int64)
me.loops.foreach_get("vertex_index", lv)
luv = np.empty(len(me.loops) * 2)
me.uv_layers[0].data.foreach_get("uv", luv)
luv = luv.reshape(-1, 2)

# 每個 loop 的值（用頂點值；UV 是 per-loop，所以直接對 loop 取）
val = np.clip((rough[lv] - THR) / max(THR * 2.5, 1e-6), 0, 1)
val[~hot[lv]] = 0.0

tris = []
for p in me.polygons:
    ls = list(p.loop_indices)
    for k in range(1, len(ls) - 1):
        tris.append((ls[0], ls[k], ls[k + 1]))
tris = np.array(tris, np.int64)
print("tris %d  res %d" % (len(tris), RES), flush=True)

acc = np.zeros((RES, RES), np.float32)
P = luv * np.array([RES, RES])
A, B, C = P[tris[:, 0]], P[tris[:, 1]], P[tris[:, 2]]
VA, VB, VC = val[tris[:, 0]], val[tris[:, 1]], val[tris[:, 2]]
keep = (np.maximum.reduce([VA, VB, VC]) > 0.0)
A, B, C, VA, VB, VC = A[keep], B[keep], C[keep], VA[keep], VB[keep], VC[keep]
print("triangles with signal: %d" % len(A), flush=True)

for i in range(len(A)):
    a, b, c = A[i], B[i], C[i]
    x0 = max(int(np.floor(min(a[0], b[0], c[0]))) - 1, 0)
    x1 = min(int(np.ceil(max(a[0], b[0], c[0]))) + 1, RES - 1)
    y0 = max(int(np.floor(min(a[1], b[1], c[1]))) - 1, 0)
    y1 = min(int(np.ceil(max(a[1], b[1], c[1]))) + 1, RES - 1)
    if x1 <= x0 or y1 <= y0:
        continue
    xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
    d = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
    if abs(d) < 1e-12:
        continue
    w0 = ((b[1] - c[1]) * (xs - c[0]) + (c[0] - b[0]) * (ys - c[1])) / d
    w1 = ((c[1] - a[1]) * (xs - c[0]) + (a[0] - c[0]) * (ys - c[1])) / d
    w2 = 1.0 - w0 - w1
    m = (w0 >= -0.001) & (w1 >= -0.001) & (w2 >= -0.001)
    if not m.any():
        continue
    v = w0 * VA[i] + w1 * VB[i] + w2 * VC[i]
    sub = acc[y0:y1 + 1, x0:x1 + 1]
    np.maximum(sub, np.where(m, v, 0.0).astype(np.float32), out=sub)

# 稍微擴張，讓細碎的塊在遊戲裡看得見
import math
k = max(1, RES // 512)
pad = acc.copy()
for dy in range(-k, k + 1):
    for dx in range(-k, k + 1):
        pad = np.maximum(pad, np.roll(np.roll(acc, dy, 0), dx, 1))
acc = pad

img = np.zeros((RES, RES, 4), np.float32)
img[:, :, 0] = 1.0                                  # R
img[:, :, 1] = np.clip(0.85 - acc * 0.85, 0, 1)     # 黃→紅
img[:, :, 2] = np.clip(0.15 - acc * 0.15, 0, 1)
img[:, :, 3] = np.clip(acc * 1.6, 0, 1)             # 乾淨處全透明
print("covered texels %d (%.2f%%)" % ((acc > 0).sum(), 100.0 * (acc > 0).mean()), flush=True)

out = bpy.data.images.new("spot_map", RES, RES, alpha=True)
out.pixels = img[::-1].ravel().tolist()              # Blender 影像原點在左下
out.filepath_raw = OUTP
out.file_format = 'PNG'
out.save()
print("WROTE " + OUTP, flush=True)
