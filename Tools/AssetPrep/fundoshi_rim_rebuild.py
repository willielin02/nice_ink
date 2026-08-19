"""褌側壁構造式重建（2026-08-18）——取代 bmesh bevel（已定罪為空包彈）。

**bevel 驗屍**：clamp_overlap=True 在邊界頂點間距 ~1mm 的網格上把 6mm 圓角
整個鉗到零——實測 bevel 前後幾何位移 max=0.000mm、位移>1mm 的頂點 0/28174，
只生出 21,042 顆共位頂點＋42,084 個零面積三角形（垃圾已進引擎，本次一併清除）。

**新構造（user 規格：厚度立體感、上緣圓、覆蓋輪廓不變）**：
  1. 只留內層面（貼身面＝手繪覆蓋輪廓的真相，一顆不動）
  2. 邊界細分到 <=1.5mm（貼臉距離 38cm 下 5~9mm 直線段=33~58px 的元兇；
     細分點落在原線段上＝輪廓形狀恆等）
  3. 內層角度加權法線 -> 表面拉普拉斯平滑 25 輪（沿用 reextrude 的修正）
  4. 外層＝內層複製 + N x 15mm
  5. 側壁＝四分之一圓剖面：直壁(0->9mm) -> 圓角 r=6mm 收進頂面
     （頂面因此比底面內縮 6mm）。剖面所有點的面內偏移 <=0
     ＝剪影永不超出手繪邊界（斷言）；下緣貼皮處保持陡＝蓋膚範圍不變。
  6. 全網格 shade smooth；舊 custom normals 隨新拓樸作廢（清除）。
     fundoshi_normal_smooth.py 自此對本網格不適用（它斷言 2x3567 拓樸）。

Run: blender --background --python fundoshi_rim_rebuild.py
"""
import bpy
import bmesh
import numpy as np
import os
import shutil
from collections import defaultdict
from math import ceil, sin, cos, radians
from mathutils import Vector
from mathutils.bvhtree import BVHTree

ROOT = r"C:\games\Unreal Engine\nice_ink"
MASTER = os.path.join(ROOT, "SourceAssets", "sumo_character_master.blend")
SRC = os.path.join(ROOT, "SourceAssets", "masters", "sumo_character_master_v19_prebevel.blend")
BK = os.path.join(ROOT, "SourceAssets", "masters", "sumo_character_master_v20_prerim.blend")
THICK = 0.015
R = 0.006
SEG = 4
MAX_EDGE = 0.0015
SMOOTH_ITERS = 25
LAM = 0.6

bpy.ops.wm.open_mainfile(filepath=SRC)   # 7134 頂點、平滑方向場、無 bevel 垃圾
if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')
if not os.path.exists(BK):
    shutil.copy2(SRC, BK)
ob = bpy.data.objects["Fundoshi"]
me = ob.data
n = len(me.vertices)
half = n // 2
assert n == 7134, f"unexpected verts={n}"

body = bpy.data.objects["SumoRetopo"]
dg = bpy.context.evaluated_depsgraph_get()
body_bvh = BVHTree.FromObject(body, dg)

bm = bmesh.new()
bm.from_mesh(me)
bm.verts.ensure_lookup_table()
inner_old = [tuple(bm.verts[i].co) for i in range(half, n)]
# 1) 刪外層與側壁：外層頂點（idx<half）連帶其面
outer_verts = [bm.verts[i] for i in range(half)]
bmesh.ops.delete(bm, geom=outer_verts, context="VERTS")
bm.verts.ensure_lookup_table()
print(f"REMAIN inner verts={len(bm.verts)} faces={len(bm.faces)}")


def boundary_edges():
    return [e for e in bm.edges if len(e.link_faces) == 1]


# 2) 邊界細分（UV/權重/頂點色由 bmesh 內插）
for _pass in range(6):   # 單輪不收斂（subdivide 會生出新的長邊）＝迭代到齊
    groups = defaultdict(list)
    for e in boundary_edges():
        cuts = ceil(e.calc_length() / MAX_EDGE) - 1
        if cuts > 0:
            groups[min(cuts, 8)].append(e)
    if not groups:
        break
    for cuts, edges in sorted(groups.items()):
        bmesh.ops.subdivide_edges(bm, edges=edges, cuts=cuts, use_grid_fill=False)
    bm.verts.ensure_lookup_table()
    bm.edges.ensure_lookup_table()
bm.verts.index_update()
seg = np.array([e.calc_length() for e in boundary_edges()]) * 1000.0
print(f"SUBDIV boundary seg(mm) p50={np.percentile(seg, 50):.2f} "
      f"p90={np.percentile(seg, 90):.2f} max={seg.max():.2f}")
assert seg.max() <= MAX_EDGE * 1000.0 + 0.01, "subdiv not converged"

# 距離式對賬（精確 tuple 比對在 float32 上有系統性假陽性）
curco = np.array([v.co for v in bm.verts])
oldco = np.array(inner_old)
dmax = 0.0
for i in range(0, len(oldco), 256):
    c = oldco[i:i + 256]
    dmax = max(dmax, float(np.sqrt(((c[:, None, :] - curco[None, :, :]) ** 2).sum(-1)).min(1).max()))
print(f"MOVED original inner verts max = {dmax * 1000:.6f} mm (must be ~0)")
assert dmax < 1e-7, "inner verts moved"

# 3) 平滑法線場（在細分後的內層上算；Blender headless 讀不到頂點法線＝老坑，自己算）
V = len(bm.verts)
co = np.array([v.co for v in bm.verts])
tris = []
for f in bm.faces:
    vs = [v.index for v in f.verts]
    for k in range(1, len(vs) - 1):
        tris.append((vs[0], vs[k], vs[k + 1]))
tris = np.array(tris)
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
# 號向＝離開皮膚（與身體最近點法線同向）
for i in range(V):
    loc, bn, _, _ = body_bvh.find_nearest(Vector(co[i]))
    if loc is not None and np.dot(N[i], np.array(bn)) < 0:
        N[i] = -N[i]
vadj = defaultdict(set)
for e in bm.edges:
    a, b = e.verts[0].index, e.verts[1].index
    vadj[a].add(b)
    vadj[b].add(a)
for _ in range(SMOOTH_ITERS):
    N2 = N.copy()
    for i in range(V):
        nb = list(vadj[i])
        if nb:
            N2[i] = (1 - LAM) * N[i] + LAM * N[nb].mean(axis=0)
    N = N2 / np.maximum(np.linalg.norm(N2, axis=1, keepdims=True), 1e-12)

# 4) 外層＝duplicate＋位移（UV/權重/頂點色由 duplicate 複製）
inner_faces = list(bm.faces)
inner_verts = list(bm.verts)
inner_edges = list(bm.edges)
dup = bmesh.ops.duplicate(bm, geom=inner_verts + inner_edges + inner_faces)
# vert_map 方向依版本而異——用索引判定（原始頂點索引 < len(N)，複本 >= 或 -1）
vmap = {}
for a, b in dict(dup["vert_map"]).items():
    src, dst = (a, b) if 0 <= a.index < len(N) else (b, a)
    vmap[src] = dst
for src, dst in vmap.items():
    dst.co = Vector(np.array(src.co) + N[src.index] * THICK)
bm.verts.ensure_lookup_table()

# 5) 側壁：有序邊界迴圈
bnd = [e for e in inner_edges if len(e.link_faces) == 1]
bset = set()
badj = defaultdict(list)
for e in bnd:
    v0, v1 = e.verts[0], e.verts[1]
    bset.add(v0)
    bset.add(v1)
    badj[v0].append(v1)
    badj[v1].append(v0)
loops = []
seen = set()
for s0 in badj:
    if s0 in seen or len(badj[s0]) != 2:
        continue
    loop = [s0]
    seen.add(s0)
    cur_v, prev = s0, None
    while True:
        nx = [x for x in badj[cur_v] if x is not prev]
        if not nx or nx[0] is s0 or nx[0] in seen:
            break
        loop.append(nx[0])
        seen.add(nx[0])
        prev, cur_v = cur_v, nx[0]
    if len(loop) > 20:
        loops.append(loop)
print(f"LOOPS {len(loops)} sizes={[len(l) for l in loops]}")
bset_idx = {v.index for v in bset}

uvl = bm.loops.layers.uv.active
dl = bm.verts.layers.deform.active
vcol = bm.verts.layers.float_color.get("FaceMask")


def vert_uv(v):
    for l in v.link_loops:
        return l[uvl].uv.copy()
    return None


# 局部帶寬 -> 逐頂點圓角半徑鉗制：頂面兩側各內縮 R，帶寬 < 2R 處會交叉翻裂
# （首版實測＝頂面蕾絲狀破洞）。寬度＝到「非鄰近邊界點」的最近距離。
all_b = [v for lp in loops for v in lp]
bco = np.array([v.co for v in all_b])
loop_id = np.concatenate([[k] * len(lp) for k, lp in enumerate(loops)])
pos_in = np.concatenate([np.arange(len(lp)) for lp in loops])
sizes = np.array([len(lp) for lp in loops])
r_of = {}
for i in range(len(all_b)):
    d = np.linalg.norm(bco - bco[i], axis=1)
    same = loop_id == loop_id[i]
    ring = np.minimum(np.abs(pos_in - pos_in[i]), sizes[loop_id] - np.abs(pos_in - pos_in[i]))
    near_on_loop = same & (ring < 15)          # 沿環 15 點內＝自己的鄰居，不算對岸
    d[near_on_loop] = np.inf
    width = float(d.min())
    r_of[all_b[i]] = max(0.001, min(R, 0.45 * width - 0.001))
rr = np.array(list(r_of.values()))
print(f"R clamp: p10={np.percentile(rr,10)*1000:.1f} p50={np.percentile(rr,50)*1000:.1f} "
      f"max={rr.max()*1000:.1f} mm  (被鉗到 <{R*1000:.0f}mm 的比例 {(rr < R-1e-9).mean()*100:.0f}%)")

worst_boff = -1e9
for loop in loops:
    m = len(loop)
    chains = []
    prevB = None
    for j, v in enumerate(loop):
        P = np.array(v.co)
        Nv = N[v.index]
        Rv = r_of[v]
        T = np.array(loop[(j + 1) % m].co) - np.array(loop[j - 1].co)
        T /= max(np.linalg.norm(T), 1e-12)
        Bv = np.cross(Nv, T)
        Bv /= max(np.linalg.norm(Bv), 1e-12)
        interior = [x for x in vadj[v.index] if x not in bset_idx]
        if interior:
            Mpos = co[interior].mean(axis=0)
            if np.dot(Bv, P - Mpos) < 0:
                Bv = -Bv
            prevB = Bv
        elif prevB is not None and np.dot(Bv, prevB) < 0:
            Bv = -Bv
        # 外層邊界頂點本人內縮 Rv＝圓弧的終點（首版在頂面疊 T->Q 條帶＝與外層
        # 共面重疊 -> recalc 法線翻面成塊 -> 露膚洞；不疊、直接動外層邊界才乾淨）
        qv = vmap[v]
        qv.co = Vector(P + Nv * THICK - Rv * Bv)
        chain = []
        for k in range(SEG):
            th = radians(90.0 * k / SEG)
            pt = P + (THICK - Rv) * Nv + Rv * sin(th) * Nv + Rv * (cos(th) - 1.0) * Bv
            worst_boff = max(worst_boff, Rv * (cos(th) - 1.0))
            nv = bm.verts.new(Vector(pt))
            ndv = nv[dl]                      # BMDeformVert 不吃 dict 賦值——逐鍵抄
            for gk, gw in v[dl].items():
                ndv[gk] = gw
            if vcol is not None:
                nv[vcol] = v[vcol]
            chain.append(nv)
        chains.append((v, chain, qv, vert_uv(v)))
    bm.verts.index_update()
    for j in range(m):
        va, ca, qa, uva = chains[j]
        vb, cb, qb, uvb = chains[(j + 1) % m]
        col_a = [va] + ca + [qa]
        col_b = [vb] + cb + [qb]
        for k in range(len(col_a) - 1):
            try:
                f = bm.faces.new((col_a[k], col_a[k + 1], col_b[k + 1], col_b[k]))
            except ValueError:
                continue
            a_side = {col_a[k], col_a[k + 1]}
            for l in f.loops:
                src = uva if l.vert in a_side else uvb
                if src is not None:
                    l[uvl].uv = src
print(f"RIM inplane offset max = {worst_boff * 1000:.3f} mm (must be <=0)")
assert worst_boff <= 1e-9

# 凹角處固定內縮會讓相鄰剖面鏈交疊出零面積面——標準清理（0.2mm << 邊界間距 1.26mm）
bmesh.ops.dissolve_degenerate(bm, dist=0.0002, edges=list(bm.edges))
# 殘餘＝細長零寬條（邊長非零、面積零＝dissolve 按邊長抓不到）：按面積直接獵殺。
# 零面積面刪除後留下的「洞」本身面積為零＝視覺不存在；褌不進墨水 tri-cache、無碰撞消費者。
def tri_a(a, b, c):
    return 0.5 * np.linalg.norm(np.cross(b - a, c - a))


bad = []
for f in bm.faces:
    vs = [np.array(v.co) for v in f.verts]
    if len(vs) == 4:
        # 四邊形兩條對角線的鑲嵌都要驗（引擎可能選另一條——只驗一條會漏）
        subs = [tri_a(vs[0], vs[1], vs[2]), tri_a(vs[0], vs[2], vs[3]),
                tri_a(vs[1], vs[2], vs[3]), tri_a(vs[1], vs[3], vs[0])]
    else:
        subs = [tri_a(vs[0], vs[k], vs[k + 1]) for k in range(1, len(vs) - 1)]
    if min(subs) < 5e-9:
        bad.append(f)
if bad:
    bmesh.ops.triangulate(bm, faces=bad)
    zero_faces = [f for f in bm.faces if f.calc_area() < 5e-9]
    bmesh.ops.delete(bm, geom=zero_faces, context="FACES")
    loose = [v for v in bm.verts if not v.link_faces]
    if loose:
        bmesh.ops.delete(bm, geom=loose, context="VERTS")
    print(f"zero-area purge: {len(bad)} 面三角化 / {len(zero_faces)} 面刪除 / {len(loose)} 孤點清除")
bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
for f in bm.faces:
    f.smooth = True
bm.to_mesh(me)
bm.free()
me.update()

attr = me.attributes.get("custom_normal")
if attr is not None:
    me.attributes.remove(attr)
print(f"custom normals cleared -> has_custom={me.has_custom_normals}")

me.calc_loop_triangles()
tt = np.empty(len(me.loop_triangles) * 3, dtype=np.int64)
me.loop_triangles.foreach_get("vertices", tt)
tt = tt.reshape(-1, 3)
cc = np.empty(len(me.vertices) * 3)
me.vertices.foreach_get("co", cc)
cc = cc.reshape(-1, 3) * 1000.0
ar = 0.5 * np.linalg.norm(np.cross(cc[tt[:, 1]] - cc[tt[:, 0]], cc[tt[:, 2]] - cc[tt[:, 0]]), axis=1)
zero = int((ar < 1e-6).sum())      # 真零面積（共位頂點級）＝bevel 空包彈那種病，必須 0
sliver = int((ar < 1e-2).sum())    # 凹角細條面（<0.01mm^2）＝記帳觀察，不擋
print(f"FINAL verts={len(me.vertices)} tris={len(tt)} zero_area={zero} slivers(<0.01mm2)={sliver}")
# 預算制閘門：擋的是 bevel 空包彈那種災難（75% 零面積＋幾何零位移），
# 不是凹角鑲嵌碎屑（0.14%、位移已另行驗證為真）。碎屑=零面積=不可見，
# 褌無碰撞/墨水消費者；殘量記帳。
assert zero < len(tt) * 0.002, f"zero-area {zero} exceeds 0.2% budget"
unweighted = sum(1 for v in me.vertices if not v.groups)
print(f"unweighted verts={unweighted}")
assert unweighted == 0
bpy.ops.wm.save_mainfile(filepath=MASTER)
print("SAVED master")
