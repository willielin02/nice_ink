"""褌＝平滑線實體（2026-08-20 user 拍板「結合成有厚度的」）。

**合體**：牆版的實體構造 ＋ 薄膜版的平滑邊界線。
- 牆版鋸齒病根＝「噪聲線 × 牆高」→ 線已重建平滑（中值±15mm＋弧長高斯σ25mm）＝病根移除
- 薄膜版病根＝零厚度無斷面 → 實體天生有底、有頂、有布的側面

構造：
  內層＝貼身面（平滑輪廓）整體沉入皮膚 1.5mm（牆腳埋住＝可見接觸線是牆身
        以陡角穿出皮膚，對 ±1mm 變形穩定；底面背面剔除＝無 z-fight）
  外層＝內層原位 + N×15mm（頂面高度不受沉入影響）
  側面＝垂直牆 + 上緣四分之一圓角（r=5mm、逐頂點按局部帶寬鉗制）
        圓弧終點＝外層邊界頂點本人內縮（不疊共面條帶＝舊翻車帳）
  UV＝圓柱參數化（整數週期無縫）；權重/頂點色承襲。
不細分：實體牆不吃坡道解析度；平滑曲線上 p90 5mm 弦的弦差 ~0.06mm＝不可見。

Run: blender --background --python fundoshi_solid.py
"""
import bpy
import numpy as np
import os
import shutil
from collections import defaultdict
from math import sin, cos, radians
from mathutils import Vector
from mathutils.bvhtree import BVHTree

ROOT = r"C:\games\Unreal Engine\nice_ink"
MASTER = os.path.join(ROOT, "SourceAssets", "sumo_character_master.blend")
SRC = os.path.join(ROOT, "SourceAssets", "masters", "sumo_character_master_v19_prebevel.blend")

THICK = 0.015
RTOP = 0.005          # 上緣圓角半徑
SEG = 3               # 圓角段數
SINK = 0.0015         # 內層整體沉入皮膚（牆腳埋住）
MED_WIN = 0.015       # 邊界中值窗（±15mm）
GAUSS_SIGMA = 0.025   # 邊界高斯（只留 ~8cm 以上走向）
SMOOTH_ITERS = 25
LAM = 0.6

bpy.ops.wm.open_mainfile(filepath=SRC)
if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')
ob = bpy.data.objects["Fundoshi"]
me = ob.data
n0 = len(me.vertices)
half = n0 // 2
assert n0 == 7134

body = bpy.data.objects["SumoRetopo"]
dg = bpy.context.evaluated_depsgraph_get()
body_bvh = BVHTree.FromObject(body, dg)

# ---- 1) 抽貼身面 ----
co_all = np.empty(n0 * 3)
me.vertices.foreach_get("co", co_all)
co_all = co_all.reshape(-1, 3)
uvl_src = me.uv_layers[0].data
vc = me.color_attributes["FaceMask"]
vcol_all = np.empty(len(vc.data) * 4)
vc.data.foreach_get("color", vcol_all)
vcol_all = vcol_all.reshape(-1, 4)
weights_all = [[(g.group, g.weight) for g in v.groups] for v in me.vertices]
vg_names = [vg.name for vg in ob.vertex_groups]

remap = {}
verts = []
faces = []
for p in me.polygons:
    vs = list(p.vertices)
    if min(vs) < half:
        continue
    ids = []
    for v in vs:
        if v not in remap:
            remap[v] = len(verts)
            verts.append(co_all[v])
        ids.append(remap[v])
    for k in range(1, len(ids) - 1):
        faces.append((ids[0], ids[k], ids[k + 1]))
inv = {v: k for k, v in remap.items()}
co = np.array(verts)
V = len(co)
wts = [dict(weights_all[inv[i]]) for i in range(V)]
tone = np.array([vcol_all[inv[i]] for i in range(V)])
print(f"BASE patch verts={V} tris={len(faces)}")

# ---- 2) 邊界迴圈 ----
ec = defaultdict(int)
for a, b, c in faces:
    for e in ((a, b), (b, c), (c, a)):
        ec[(min(e), max(e))] += 1
badj = defaultdict(list)
for (a, b), k in ec.items():
    if k == 1:
        badj[a].append(b)
        badj[b].append(a)
loops = []
seen = set()
for s0 in badj:
    if s0 in seen or len(badj[s0]) != 2:
        continue
    loop, cur, prev = [s0], s0, None
    seen.add(s0)
    while True:
        nx = [x for x in badj[cur] if x != prev]
        if not nx or nx[0] == s0 or nx[0] in seen:
            break
        loop.append(nx[0])
        seen.add(nx[0])
        prev, cur = cur, nx[0]
    if len(loop) > 20:
        loops.append(loop)
print(f"LOOPS {len(loops)} sizes={[len(l) for l in loops]}")
bset = {v for lp in loops for v in lp}

# ---- 3) 邊界重建為平滑曲線（中值→高斯→切向鬆弛→貼皮→帶寬鉗制→擴散內部）----


def _arc(P):
    dseg = np.linalg.norm(np.diff(np.vstack([P, P[:1]]), axis=0), axis=1)
    return np.concatenate([[0.0], np.cumsum(dseg)[:-1]]), dseg.sum()


def _median_loop(P, win):
    sarc, total = _arc(P)
    out = np.empty_like(P)
    for i in range(len(P)):
        ds = sarc - sarc[i]
        ds -= total * np.round(ds / total)
        out[i] = np.median(P[np.abs(ds) <= win], axis=0)
    return out


def _gauss_loop(P, sigma):
    sarc, total = _arc(P)
    out = np.empty_like(P)
    for i in range(len(P)):
        ds = sarc - sarc[i]
        ds -= total * np.round(ds / total)
        w = np.exp(-0.5 * (ds / sigma) ** 2)
        w[np.abs(ds) > 3 * sigma] = 0.0
        out[i] = (P * w[:, None]).sum(axis=0) / w.sum()
    return out


delta = np.zeros_like(co)
for lp in loops:
    L = np.array(lp)
    P0 = co[L].copy()
    P2 = _gauss_loop(_median_loop(P0, MED_WIN), GAUSS_SIGMA)
    for _it in range(40):                      # 切向鬆弛勻點距（防共位）
        prv = np.roll(P2, 1, axis=0)
        nxt = np.roll(P2, -1, axis=0)
        tan = nxt - prv
        tan /= np.maximum(np.linalg.norm(tan, axis=1, keepdims=True), 1e-12)
        slide = ((0.5 * (prv + nxt) - P2) * tan).sum(1)
        P2 += 0.5 * slide[:, None] * tan
    for k in range(len(L)):                    # 貼回皮膚
        loc, _, _, _ = body_bvh.find_nearest(Vector(P2[k]))
        if loc is not None:
            P2[k] = np.array(loc)
    delta[L] = P2 - P0
# 帶寬鉗制
allb = np.concatenate([np.array(lp) for lp in loops])
lp_id = np.concatenate([[k] * len(lp) for k, lp in enumerate(loops)])
pos_in = np.concatenate([np.arange(len(lp)) for lp in loops])
sz = np.array([len(lp) for lp in loops])
bpos = co[allb]
width_of = {}
for ii in range(len(allb)):
    dd = np.linalg.norm(bpos - bpos[ii], axis=1)
    ring = np.minimum(np.abs(pos_in - pos_in[ii]), sz[lp_id] - np.abs(pos_in - pos_in[ii]))
    dd[(lp_id == lp_id[ii]) & (ring < 15)] = np.inf
    width = float(dd.min())
    width_of[int(allb[ii])] = width
    cap = 0.3 * width
    vi = allb[ii]
    mag = float(np.linalg.norm(delta[vi]))
    if mag > cap > 0:
        delta[vi] *= cap / mag
dv = np.concatenate([np.linalg.norm(delta[np.array(lp)], axis=1) for lp in loops]) * 1000.0
print(f"邊界偏離手繪線(mm) p50={np.percentile(dv,50):.2f} p90={np.percentile(dv,90):.2f} max={dv.max():.2f}")
assert dv.max() < 30.0
# 擴散進內部＋貼皮
nbr = defaultdict(set)
for a, b, c in faces:
    nbr[a].update((b, c))
    nbr[b].update((a, c))
    nbr[c].update((a, b))
interior = [i for i in range(V) if i not in bset]
for _ in range(12):
    for i in interior:
        nb = list(nbr[i])
        if nb:
            delta[i] = delta[nb].mean(axis=0)
co = co + delta
for i in interior:
    loc, _, _, _ = body_bvh.find_nearest(Vector(co[i]))
    if loc is not None:
        co[i] = np.array(loc)
print("boundary rebuilt + snapped")

# ---- 4) 平滑法線場 ----
tris = np.array(faces)
A, B, C = co[tris[:, 0]], co[tris[:, 1]], co[tris[:, 2]]
fn = np.cross(B - A, C - A)
fn /= np.maximum(np.linalg.norm(fn, axis=1, keepdims=True), 1e-18)
N = np.zeros((V, 3))


def corner(p, q, r):
    u = q - p
    v = r - p
    u /= np.maximum(np.linalg.norm(u, axis=1, keepdims=True), 1e-18)
    v /= np.maximum(np.linalg.norm(v, axis=1, keepdims=True), 1e-18)
    return np.arccos(np.clip((u * v).sum(1), -1, 1))


for a, b, c in ((0, 1, 2), (1, 2, 0), (2, 0, 1)):
    w = corner(co[tris[:, a]], co[tris[:, b]], co[tris[:, c]])
    np.add.at(N, tris[:, a], fn * w[:, None])
N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-12)
for i in range(V):
    loc, bn, _, _ = body_bvh.find_nearest(Vector(co[i]))
    if loc is not None and np.dot(N[i], np.array(bn)) < 0:
        N[i] = -N[i]
idxs = np.zeros(sum(len(v) for v in nbr.values()), dtype=np.int64)
ptr = np.zeros(V + 1, dtype=np.int64)
pos = 0
for i in range(V):
    ptr[i] = pos
    for j in nbr.get(i, ()):
        idxs[pos] = j
        pos += 1
ptr[V] = pos
cnt = np.maximum(ptr[1:] - ptr[:-1], 1)
for _ in range(SMOOTH_ITERS):
    acc = np.zeros_like(N)
    np.add.at(acc, np.repeat(np.arange(V), ptr[1:] - ptr[:-1]), N[idxs])
    N = (1 - LAM) * N + LAM * (acc / cnt[:, None])
    N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-12)

# ---- 5) 實體構造 ----
# 頂點佈局：內層 0..V-1（沉入 SINK）、外層 V..2V-1（原位+THICK）、側壁鏈接續其後
inner = co - N * SINK
outer = co + N * THICK
all_v = [inner, outer]
all_w = wts + wts                     # 外層權重＝內層
all_t = [tone, tone]
solid_faces = []
# 底面（朝身體＝繞向反轉）
for a, b, c in faces:
    solid_faces.append((c, b, a))
# 頂面
for a, b, c in faces:
    solid_faces.append((V + a, V + b, V + c))

# 側壁：逐迴圈
extra_v = []
extra_w = []
extra_t = []
worst_boff = -1e9
for lp in loops:
    m = len(lp)
    P_loop = co[np.array(lp)]
    chains = []
    prevB = None
    for j, vi in enumerate(lp):
        P = co[vi]
        Nv = N[vi]
        T = co[lp[(j + 1) % m]] - co[lp[j - 1]]
        T /= max(np.linalg.norm(T), 1e-12)
        Bv = np.cross(Nv, T)
        Bv /= max(np.linalg.norm(Bv), 1e-12)
        interior_nb = [x for x in nbr[vi] if x not in bset]
        if interior_nb:
            Mpos = co[interior_nb].mean(axis=0)
            if np.dot(Bv, P - Mpos) < 0:
                Bv = -Bv
            prevB = Bv
        elif prevB is not None and np.dot(Bv, prevB) < 0:
            Bv = -Bv
        Rv = max(0.001, min(RTOP, 0.3 * width_of.get(int(vi), 1.0)))
        # 外層邊界頂點本人內縮＝圓弧終點（不疊共面條帶）
        outer[vi] = P + Nv * THICK - Rv * Bv
        # 鏈＝牆頂 + 圓弧內部點（不含終點）；牆腳＝內層邊界頂點本人（已沉 SINK）
        chain = []
        Cc = P + (THICK - Rv) * Nv - Rv * Bv
        for k in range(SEG):
            th = radians(90.0 * k / SEG)
            pt = Cc + Rv * (cos(th) * Bv + sin(th) * Nv)
            worst_boff = max(worst_boff, float(np.dot(pt - P, Bv)))
            gi = 2 * V + len(extra_v)
            extra_v.append(pt)
            extra_w.append(wts[vi])
            extra_t.append(tone[vi])
            chain.append(gi)
        chains.append((vi, chain))
    for j in range(m):
        va, ca = chains[j]
        vb, cb = chains[(j + 1) % m]
        col_a = [va] + ca + [V + va]
        col_b = [vb] + cb + [V + vb]
        for k in range(len(col_a) - 1):
            solid_faces.append((col_a[k], col_a[k + 1], col_b[k + 1]))
            solid_faces.append((col_a[k], col_b[k + 1], col_b[k]))
print(f"RIM inplane offset max = {worst_boff*1000:.3f} mm (<=0)")
assert worst_boff <= 1e-9

new_co = np.vstack([inner, outer, np.array(extra_v)])
all_wts = wts + wts + extra_w
all_tone = np.vstack([tone, tone, np.array(extra_t)])
print(f"SOLID verts={len(new_co)} faces={len(solid_faces)}")

# 濾零面積（float32 落盤捨入要蓋過）
keep = []
for fi, (a, b, c) in enumerate(solid_faces):
    ar0 = 0.5 * np.linalg.norm(np.cross(new_co[b] - new_co[a], new_co[c] - new_co[a]))
    if ar0 >= 1e-10:
        keep.append(fi)
print(f"zero-area dropped = {len(solid_faces) - len(keep)}")
solid_faces = [solid_faces[i] for i in keep]

# ---- 6) 圓柱 UV ----
cx, cy = float(new_co[:, 0].mean()), float(new_co[:, 1].mean())
theta = np.arctan2(new_co[:, 1] - cy, new_co[:, 0] - cx)
Ravg = float(np.sqrt((new_co[:, 0] - cx) ** 2 + (new_co[:, 1] - cy) ** 2).mean())
TILE_M = 0.208
Mrep = max(1, round(2 * np.pi * Ravg / TILE_M))
u = (theta + np.pi) / (2 * np.pi) * (Mrep / 12.0)
vv = new_co[:, 2] / (12.0 * TILE_M)
vert_uv = np.stack([u, vv], axis=1)
print(f"CYL UV: R={Ravg*100:.1f}cm 週期={Mrep}")

# ---- 7) 重建 mesh ----
me2 = bpy.data.meshes.new("Fundoshi_solid")
me2.from_pydata([tuple(v) for v in new_co], [], solid_faces)
me2.update()
uv_new = me2.uv_layers.new(name="UVMap")
for p in me2.polygons:
    for li in p.loop_indices:
        vi = me2.loops[li].vertex_index
        uv_new.data[li].uv = tuple(vert_uv[vi])
ca = me2.color_attributes.new(name="FaceMask", type='FLOAT_COLOR', domain='POINT')
ca.data.foreach_set("color", all_tone.ravel())
me2.materials.append(bpy.data.materials.get("M_Fundoshi"))
for p in me2.polygons:
    p.use_smooth = True
old = ob.data
ob.data = me2
bpy.data.meshes.remove(old)
if len(ob.vertex_groups) != len(vg_names):
    ob.vertex_groups.clear()
    for nm in vg_names:
        ob.vertex_groups.new(name=nm)
for i, wd in enumerate(all_wts):
    for g, w in wd.items():
        ob.vertex_groups[g].add([i], w, 'REPLACE')

# 封閉實體＝統一外向繞法（交給 recalc）
bpy.context.view_layer.objects.active = ob
for o in bpy.context.selected_objects:
    o.select_set(False)
ob.select_set(True)
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.mesh.normals_make_consistent(inside=False)
bpy.ops.object.mode_set(mode='OBJECT')

unweighted = sum(1 for v in me2.vertices if not v.groups)
me2.calc_loop_triangles()
tt = np.empty(len(me2.loop_triangles) * 3, dtype=np.int64)
me2.loop_triangles.foreach_get("vertices", tt)
tt = tt.reshape(-1, 3)
cc = np.empty(len(me2.vertices) * 3)
me2.vertices.foreach_get("co", cc)
cc = cc.reshape(-1, 3) * 1000.0
ar = 0.5 * np.linalg.norm(np.cross(cc[tt[:, 1]] - cc[tt[:, 0]], cc[tt[:, 2]] - cc[tt[:, 0]]), axis=1)
zero = int((ar < 1e-6).sum())
print(f"FINAL verts={len(me2.vertices)} tris={len(tt)} zero_area={zero} unweighted={unweighted}")
assert unweighted == 0
assert zero < len(tt) * 0.002, f"zero-area {zero}"
assert [m.name for m in me2.materials] == ["M_Fundoshi"]
assert any(m.type == 'ARMATURE' for m in ob.modifiers)
bpy.ops.wm.save_mainfile(filepath=MASTER)
print("SAVED master")
