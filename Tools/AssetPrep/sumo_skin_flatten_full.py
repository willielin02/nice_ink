# 全身皮膚整平（2026-08-22 user 判定「整個大腿前側都是」＝區域劃錯的總修正）：
# 整平區=z 0.06~1.28 全身（臉/頭排除；腳底排除），膜=零收縮 Taubin ×200（同面判準
# dot>0.3=臀縫/指縫不互抹）；簇分類：峰深 ≤2mm 全拉平、2~4mm 羽化、≥4mm 設計整簇不動；
# cap 2mm。此前整平只做褲周 8~10cm（依據=只量褲周的普查=循環論證）＝大腿前側原始
# 掃描坑 0.5~3mm 從未被碰過＝user 貼臉看到的整片凹凸。
import bpy
import os
import shutil
import numpy as np
from collections import defaultdict, deque
from mathutils import Vector

ROOT = r"C:\games\Unreal Engine\nice_ink"
MASTER = os.path.join(ROOT, "SourceAssets", "sumo_character_master.blend")
BK = os.path.join(ROOT, "SourceAssets", "masters", "sumo_character_master_v31_prefullflat.blend")
Z_LO, Z_HI = 0.06, 1.28
PAIRS = 200
FLAG_D = 0.0008
KEEP_LO = 0.002
KEEP_HI = 0.004
CAP = 0.002

def P(*a):
    print(*a, flush=True)

bpy.ops.wm.open_mainfile(filepath=MASTER)
if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')
if not os.path.exists(BK):
    shutil.copy2(MASTER, BK)
    P("backup ->", BK)
body = bpy.data.objects["SumoRetopo"]
me = body.data
N = len(me.vertices)
co = np.array([v.co[:] for v in me.vertices])
in_reg = (co[:, 2] > Z_LO) & (co[:, 2] < Z_HI)
# 邊界羽化（4cm）
w_reg = np.clip(np.minimum(co[:, 2] - Z_LO, Z_HI - co[:, 2]) / 0.04, 0.0, 1.0)
w_reg = np.where(in_reg, w_reg, 0.0)
w_reg = w_reg * w_reg * (3 - 2 * w_reg)
P(f"verts={N} region={int((w_reg>0).sum())} full={int((w_reg>=0.999).sum())}")

vn = np.zeros((N, 3))
for poly in me.polygons:
    n_ = np.array(poly.normal)
    for vi in poly.vertices:
        vn[vi] += n_
l_ = np.linalg.norm(vn, axis=1); l_[l_ == 0] = 1
vn /= l_[:, None]
pr = []
for e in me.edges:
    a, b = e.vertices
    if np.dot(vn[a], vn[b]) > 0.3:
        pr.append((a, b)); pr.append((b, a))
pr = np.array(pr, dtype=np.int64)
dst, src = pr[:, 0], pr[:, 1]
deg = np.zeros(N)
np.add.at(deg, dst, 1.0)
x = co.copy()
for _it in range(PAIRS):
    for lam in (0.5, -0.53):
        s_ = np.zeros_like(x)
        np.add.at(s_, dst, x[src])
        avg = s_ / np.maximum(deg, 1.0)[:, None]
        dx = avg - x
        dx[deg == 0] = 0.0
        x = x + lam * (w_reg[:, None] * dx)
mem = x
rvec = co - mem
depth = np.linalg.norm(rvec, axis=1)
flag = (depth > FLAG_D) & (w_reg > 0.5)
adj = defaultdict(list)
for a, b in pr[::2]:
    adj[int(a)].append(int(b)); adj[int(b)].append(int(a))
cluster = np.full(N, -1, dtype=np.int64)
clusters = []
for seed in np.where(flag)[0]:
    if cluster[seed] >= 0:
        continue
    cid = len(clusters)
    q = deque([int(seed)]); cluster[seed] = cid; mem_ = [int(seed)]
    while q:
        u = q.popleft()
        for v2 in adj[u]:
            if flag[v2] and cluster[v2] < 0:
                cluster[v2] = cid; q.append(v2); mem_.append(v2)
    clusters.append(mem_)
factor = np.ones(N)
protected = 0
for cid, mm_ in enumerate(clusters):
    mm_ = np.array(mm_)
    pk = depth[mm_].max()
    if pk >= KEEP_HI:
        f = 0.0; protected += 1
    elif pk <= KEEP_LO:
        f = 1.0
    else:
        t = (KEEP_HI - pk) / (KEEP_HI - KEEP_LO)
        f = t * t * (3 - 2 * t)
    factor[mm_] = np.minimum(factor[mm_], f)
P(f"clusters {len(clusters)} protected(峰≥4mm) {protected}")
for _ in range(6):
    s_ = np.zeros(N)
    np.add.at(s_, dst, factor[src])
    avg = s_ / np.maximum(deg, 1.0)
    avg[deg == 0] = factor[deg == 0]
    factor = np.minimum(factor, 0.5 * factor + 0.5 * avg)
w = w_reg * factor
disp = (mem - co) * w[:, None]
dm = np.linalg.norm(disp, axis=1)
over = dm > CAP
disp[over] *= (CAP / dm[over])[:, None]
new_co = co + disp
moved = np.linalg.norm(new_co - co, axis=1)
mv = moved[moved > 1e-5] * 1000
P(f"flatten: moved {len(mv)} |d| p50 {np.percentile(mv,50):.3f} p90 {np.percentile(mv,90):.3f} max {mv.max():.3f} mm")
assert np.abs(new_co[w_reg == 0] - co[w_reg == 0]).max() == 0.0
for i in np.where(moved > 1e-7)[0]:
    me.vertices[int(i)].co = Vector(new_co[i])
me.update()
assert any(m.type == 'ARMATURE' for m in body.modifiers)
unweighted = sum(1 for v in me.vertices if not v.groups)
assert unweighted == 0
bpy.ops.wm.save_mainfile(filepath=MASTER)
P("FULL FLATTEN SAVED")
