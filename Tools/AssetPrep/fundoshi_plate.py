"""褌＝光滑板（2026-08-21）——「布貼著皮膚做」前提退役。

**病根（量測）**：身體在褌區是 3cm 邊長的多面體（edge p50 30.8mm、dihedral p90 17.7°、
面片矢高 1~2.3mm、最糟 7mm）。舊構造「貼身面抬回體表→細分 3mm→沿法線位移」把每條
稜線忠實採樣再搬上去＝殼比皮膚更粗（環向 HF std 1.4~1.9mm vs 皮膚 0.6~1.0mm）。
十四輪都在修邊，沒有一輪動過「底面＝皮膚」這個前提。

**新構造＝三件事拆開**：
  形狀 U：身體 cage 每頂點沿法線外推 o_v（只在褌區 + 2 圈），取 Catmull-Clark
          極限面（level 3）＝自己光滑（C2）；o_v 由迭代求最小值使 U ≥ 皮膚多面體 + ε。
          布下皮膚無消費者，只剩這條不等式。
  裁切 C：手繪遮罩（UV0，手繪正源）在 U 上的 0.5 等值線（marching triangles），
          σ4mm 微平滑（≤1mm）——線只裁切、不決定幾何，不再與牆高相乘。
  接觸  ：頂板 = U + T·n；牆 = 頂緣沿 −n 直落到皮膚（零面內偏移＝轉角永不交叉）；
          牆腳釘在皮膚上的 C（再沉 1mm 防 z-fight）＝布蓋住的範圍 ≡ 你畫的範圍。
  單一網格密度（~4mm）；雙密度 FD_TIER 退役（_hi/_lo 同檔）。

Run: blender --background --python fundoshi_plate.py
"""
import bpy, bmesh, os, shutil, sys
import numpy as np
from collections import defaultdict, deque
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from mathutils.kdtree import KDTree

ROOT = r"C:\games\Unreal Engine\nice_ink"
MASTER = os.path.join(ROOT, "SourceAssets", "sumo_character_master.blend")
BK = os.path.join(ROOT, "SourceAssets", "masters", "sumo_character_master_v22_preplate.blend")
MASK = os.path.join(ROOT, "SourceAssets", "fundoshi_mask_sharp.png")
REPORT = os.path.join(ROOT, "Saved", "fundoshi_plate_report.txt")

T_CLOTH = 0.005      # 布厚 5mm（08-18 定值）
EPS = 0.0005         # 底面離皮最小間隙
SINK = 0.003         # 牆腳沉入皮膚（牆垂直於皮膚＝沉深不移動可見線；吃掉 quad 非平面/三角化歧義 ≤ 數 mm）
SUBD = 3             # CC level（~4mm）
CONTOUR_SIGMA = 0.015    # 手繪線的 cm 級抖動（3~5px 幅度/3~6cm 週期）要 σ≈15mm 才平；布與禁畫層共用同一條線
GAP_MAX = 0.012     # 抬升量上限：U 離線性孿生最多 o_max(8.6mm)+矢高；超過＝射線打到對面（大腿/臀）的 U
TILE_M = 0.208       # 沿用 fundoshi_shell 貼圖密度（織紋 2.31mm @TileScale 1）
RINGS = 2            # 允許外推的 cage 圈數（褌區外）

rep = []
def P(*a):
    s = " ".join(str(x) for x in a); print(s, flush=True); rep.append(s)

bpy.ops.wm.open_mainfile(filepath=MASTER)
if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')
if not os.path.exists(BK):
    shutil.copy2(MASTER, BK); P("backup ->", BK)

body = bpy.data.objects["SumoRetopo"]
fund = bpy.data.objects["Fundoshi"]
arm = bpy.data.objects["Skeleton_Plus-size"]
bme = body.data
assert max(abs(a-b) for ra,rb in zip(body.matrix_world,fund.matrix_world) for a,b in zip(ra,rb)) < 1e-5, "body/fundoshi transform mismatch"

def arr(me):
    co = np.empty(len(me.vertices) * 3); me.vertices.foreach_get("co", co); return co.reshape(-1, 3)

def face_normals_and_vn(co, polys):
    """角度加權頂點法線（Blender 5 headless 讀不到 normal）；全 quad/全 tri 時向量化"""
    ks = set(len(vs) for vs in polys)
    if len(ks) == 1:
        k = ks.pop(); F = np.asarray(polys, dtype=np.int64)
        vn = np.zeros_like(co)
        for i in range(k):
            a = co[F[:, i]]; b = co[F[:, (i + 1) % k]]; c = co[F[:, i - 1]]
            e1 = b - a; e2 = c - a; n = np.cross(e1, e2)
            l1 = np.linalg.norm(e1, axis=1); l2 = np.linalg.norm(e2, axis=1); ln = np.linalg.norm(n, axis=1)
            okm = (ln > 1e-18) & (l1 > 1e-12) & (l2 > 1e-12)
            ang = np.zeros(len(F)); ang[okm] = np.arccos(np.clip(np.einsum('ij,ij->i', e1[okm], e2[okm]) / (l1[okm] * l2[okm]), -1, 1))
            contrib = np.zeros_like(n); contrib[okm] = n[okm] / ln[okm, None] * ang[okm, None]
            np.add.at(vn, F[:, i], contrib)
        l = np.linalg.norm(vn, axis=1); l[l == 0] = 1
        return vn / l[:, None]
    vn = np.zeros_like(co)
    for vs in polys:
        k = len(vs)
        for i in range(k):
            a = co[vs[i]]; b = co[vs[(i + 1) % k]]; c = co[vs[i - 1]]
            e1 = b - a; e2 = c - a
            n = np.cross(e1, e2)
            l1 = np.linalg.norm(e1); l2 = np.linalg.norm(e2); ln = np.linalg.norm(n)
            if ln < 1e-18 or l1 < 1e-12 or l2 < 1e-12:
                continue
            ang = np.arccos(np.clip(np.dot(e1, e2) / (l1 * l2), -1, 1))
            vn[vs[i]] += n / ln * ang
    l = np.linalg.norm(vn, axis=1); l[l == 0] = 1
    return vn / l[:, None]

# ---------------- 0) cage / 多面體 ----------------
cage_co = arr(bme)
cage_polys = [list(p.vertices) for p in bme.polygons]
cage_vn = face_normals_and_vn(cage_co, cage_polys)
dg = bpy.context.evaluated_depsgraph_get()
skin_bvh = BVHTree.FromObject(body, dg)   # 多面體＝遊戲裡真正渲染的皮膚
P(f"cage verts={len(cage_co)} faces={len(cage_polys)}")

# ---------------- 1) 遮罩（UV0 手繪正源）----------------
img = bpy.data.images.load(MASK, check_existing=True)
W, H = img.size
px = np.empty(W * H * 4, np.float32); img.pixels.foreach_get(px)
mask = px.reshape(H, W, 4)[:, :, 0].astype(np.float32)   # R；bottom-up 行序，與 UV v 同向
def sample_mask(uv):
    u = np.mod(uv[:, 0], 1.0) * (W - 1); v = np.mod(uv[:, 1], 1.0) * (H - 1)
    x0 = np.floor(u).astype(int); y0 = np.floor(v).astype(int)
    x1 = np.minimum(x0 + 1, W - 1); y1 = np.minimum(y0 + 1, H - 1)
    fx = u - x0; fy = v - y0
    return (mask[y0, x0] * (1 - fx) * (1 - fy) + mask[y0, x1] * fx * (1 - fy)
            + mask[y1, x0] * (1 - fx) * fy + mask[y1, x1] * fx * fy)
P(f"mask {W}x{H} white frac {(mask > 0.5).mean():.4f}")
mask_raw = mask.copy()

# ---- 1b) 裁切場＝UV 空間高斯模糊（σ=CONTOUR_SIGMA）的手繪遮罩 ----
# 等值線在「模糊場」上切＝天生平滑（不必事後搬頂點＝零 zigzag/零重疊牆）。
# 跨 UV 島邊界用覆蓋率歸一化（blur(mask·valid)/blur(valid)），不讓島外黑吃掉島緣。
PX_PER_MM = 0.617 * W / 4096.0
SIG_PX = CONTOUR_SIGMA * 1000.0 * PX_PER_MM
valid = np.zeros((H, W), np.float32)
_uvl = bme.uv_layers["UVMap"].data
_luv = np.empty(len(_uvl) * 2); _uvl.foreach_get("uv", _luv); _luv = _luv.reshape(-1, 2)
# 三角掃描線填充（純 numpy：逐面 bbox 內重心測試；UV 面小、總量 ~12k）
for p_ in bme.polygons:
    uvs = _luv[p_.loop_start:p_.loop_start + p_.loop_total] * [W, H]
    x0 = max(int(np.floor(uvs[:, 0].min())) - 1, 0); x1 = min(int(np.ceil(uvs[:, 0].max())) + 1, W - 1)
    y0 = max(int(np.floor(uvs[:, 1].min())) - 1, 0); y1 = min(int(np.ceil(uvs[:, 1].max())) + 1, H - 1)
    if x1 < x0 or y1 < y0: continue
    xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
    inside_f = np.zeros(xs.shape, bool)
    for i in range(1, len(uvs) - 1):
        a_, b_, c_ = uvs[0], uvs[i], uvs[i + 1]
        den = (b_[1] - c_[1]) * (a_[0] - c_[0]) + (c_[0] - b_[0]) * (a_[1] - c_[1])
        if abs(den) < 1e-12: continue
        w0 = ((b_[1] - c_[1]) * (xs - c_[0]) + (c_[0] - b_[0]) * (ys - c_[1])) / den
        w1 = ((c_[1] - a_[1]) * (xs - c_[0]) + (a_[0] - c_[0]) * (ys - c_[1])) / den
        w2 = 1 - w0 - w1
        inside_f |= (w0 >= -0.02) & (w1 >= -0.02) & (w2 >= -0.02)
    valid[y0:y1 + 1, x0:x1 + 1] = np.maximum(valid[y0:y1 + 1, x0:x1 + 1], inside_f)
# 再膨脹 2px（島緣保底）
vpad = valid.copy()
vpad[1:, :] = np.maximum(vpad[1:, :], valid[:-1, :]); vpad[:-1, :] = np.maximum(vpad[:-1, :], valid[1:, :])
vpad[:, 1:] = np.maximum(vpad[:, 1:], valid[:, :-1]); vpad[:, :-1] = np.maximum(vpad[:, :-1], valid[:, 1:])
valid = vpad
def gauss_blur_fft(img, sig):
    ky = np.fft.fftfreq(img.shape[0]); kx = np.fft.rfftfreq(img.shape[1])
    G = np.exp(-2 * (np.pi ** 2) * (sig ** 2) * (ky[:, None] ** 2 + kx[None, :] ** 2))
    return np.fft.irfft2(np.fft.rfft2(img) * G, s=img.shape).astype(np.float32)
num = gauss_blur_fft(mask_raw * valid, SIG_PX); den = gauss_blur_fft(valid, SIG_PX)
mask_blur = np.where(den > 1e-3, num / np.maximum(den, 1e-3), mask_raw)
mask = mask_blur            # sample_mask 之後全走模糊場（裁切/inside）；衍生遮罩以 mask_raw 為底
P(f"cut field: gaussian σ={SIG_PX:.1f}px ({CONTOUR_SIGMA*1000:.0f}mm), uv coverage {valid.mean()*100:.1f}%, blurred white frac {(mask_blur>0.5).mean():.4f} (raw {(mask_raw>0.5).mean():.4f})")

# ---------------- 2) 褌區 cage 頂點（允許外推集合）----------------
uvl = bme.uv_layers["UVMap"].data
loop_uv = np.empty(len(uvl) * 2); uvl.foreach_get("uv", loop_uv); loop_uv = loop_uv.reshape(-1, 2)
face_in = np.zeros(len(cage_polys), bool)
for p in bme.polygons:
    uvs = loop_uv[p.loop_start:p.loop_start + p.loop_total]
    # 面內 5x5 取樣（雙線性 UV）——任何取樣點在遮罩內＝該面碰到褌區
    if p.loop_total == 4:
        s = np.linspace(0, 1, 5)
        gu, gv = np.meshgrid(s, s)
        a, b, c, d = uvs
        pts = ((1 - gu) * (1 - gv))[..., None] * a + (gu * (1 - gv))[..., None] * b + (gu * gv)[..., None] * c + ((1 - gu) * gv)[..., None] * d
        pts = pts.reshape(-1, 2)
    else:
        pts = np.vstack([uvs, uvs.mean(0, keepdims=True)])
    face_in[p.index] = (sample_mask(pts) > 0.5).any()
cage_adj = defaultdict(set)
for e in bme.edges:
    a, b = e.vertices; cage_adj[a].add(b); cage_adj[b].add(a)
movable = np.zeros(len(cage_co), bool)
for fi in np.where(face_in)[0]:
    movable[cage_polys[fi]] = True
core = movable.copy()
for _ in range(RINGS):
    nxt = movable.copy()
    for v in np.where(movable)[0]:
        nxt[list(cage_adj[v])] = True
    movable = nxt
P(f"cage faces in region {face_in.sum()}  movable verts {movable.sum()} (core {core.sum()})")

# ---------------- 3) 外推量 o_v 迭代：U = CC(cage + o·n) ≥ skin + EPS ----------------
work = body.copy(); work.data = body.data.copy(); work.name = "FundoshiWork"
bpy.context.collection.objects.link(work)
for m in list(work.modifiers):
    work.modifiers.remove(m)
work.parent = None
mod = work.modifiers.new("ss", 'SUBSURF'); mod.levels = SUBD; mod.render_levels = SUBD
mod.uv_smooth = 'NONE'          # UV 線性內插＝遮罩的 UV 座標與遊戲裡身體的線性 UV 同義
mod.boundary_smooth = 'ALL'

def evaluate(o):
    work.data.vertices.foreach_set("co", (cage_co + o[:, None] * cage_vn).ravel())
    work.data.update()
    dgx = bpy.context.evaluated_depsgraph_get()
    ev = work.evaluated_get(dgx)
    me = ev.to_mesh()
    co = arr(me)
    polys = [list(p.vertices) for p in me.polygons]
    uvd = me.uv_layers["UVMap"].data
    luv = np.empty(len(uvd) * 2); uvd.foreach_get("uv", luv); luv = luv.reshape(-1, 2)
    lvi = np.empty(len(me.loops), np.int64); me.loops.foreach_get("vertex_index", lvi)
    # 頂點群組（subsurf 內插 dvert）
    dv = [[(g.group, g.weight) for g in v.groups] for v in me.vertices]
    return me, ev, co, polys, luv, lvi, dv

o = np.zeros(len(cage_co))
kd_cage = KDTree(int(movable.sum()))
mov_idx = np.where(movable)[0]
for j, i in enumerate(mov_idx):
    kd_cage.insert(Vector(cage_co[i]), int(i))
kd_cage.balance()

def signed_to_skin(pts):
    out = np.empty(len(pts))
    for k, p in enumerate(pts):
        loc, nor, idx, d = skin_bvh.find_nearest(Vector(p))
        s = np.dot(np.array(p) - np.array(loc), np.array(nor))
        out[k] = d if s >= 0 else -d
    return out

me, ev, co, polys, luv, lvi, dv = evaluate(o)
# 取樣點＝細分頂點中「遮罩內 + 12mm 邊距」者（只約束布會存在的地方；
# 側向的其他身體部位（大腿內側相貼）不算＝沿自身法線射線判定）
mval_loop0 = sample_mask(luv)
msum0 = np.zeros(len(co)); mcnt0 = np.zeros(len(co))
np.add.at(msum0, lvi, mval_loop0); np.add.at(mcnt0, lvi, 1)
mval0 = msum0 / np.maximum(mcnt0, 1)
inside0 = (mval0 > 0.5) & (co[:, 2] < 1.3)      # 元結帶（頭頂）排除
kd_in = KDTree(int(inside0.sum()))
for i in np.where(inside0)[0]: kd_in.insert(Vector(co[i]), int(i))
kd_in.balance()
samp = np.array([kd_in.find(Vector(p))[2] < 0.012 for p in co])
near_cage = np.array([kd_cage.find(Vector(p))[1] for p in co])
P(f"subdiv verts={len(co)} faces={len(polys)}; constraint samples={samp.sum()} (inside {inside0.sum()})")
U_vn0 = face_normals_and_vn(co, polys)
ev.to_mesh_clear()

def deficit_top_nearest(pts, nrm):
    """頂板點 (U+T·n) 的最近皮膚 signed 距離 < 0 且該皮膚點在布下 → 要抬 −sd"""
    out = np.zeros(len(pts))
    for k in range(len(pts)):
        q = pts[k] + nrm[k] * T_CLOTH
        loc, nor, idx, d = skin_bvh.find_nearest(Vector(q), T_CLOTH + 0.002)
        if loc is None: continue
        sdv = np.dot(q - np.array(loc), np.array(nor))
        if sdv < 0.0006 and kd_in.find(loc)[2] < 0.012:
            out[k] = 0.0006 - sdv
    return out

def deficit_along_normal(pts, nrm):
    """沿自身法線：上方 15mm 內有正向皮膚 → 要抬 t+EPS；下方 t 內有皮膚 → 要抬 EPS−t；否則 0"""
    out = np.zeros(len(pts))
    for k in range(len(pts)):
        p = Vector(pts[k]); n = Vector(nrm[k])
        hit = skin_bvh.ray_cast(p - n * 0.0002, n, 0.015)
        if hit[0] is not None and hit[1].dot(n) > 0:
            out[k] = hit[3] - 0.0002 + EPS; continue
        hit = skin_bvh.ray_cast(p + n * 0.0002, -n, 0.03)
        if hit[0] is not None and hit[1].dot(n) > 0:
            t = hit[3] - 0.0002
            out[k] = max(0.0, EPS - t)
    return out

for it in range(20):
    me, ev, co, polys, luv, lvi, dv = evaluate(o)
    vn_it = U_vn0 if it == 0 else face_normals_and_vn(co, polys)
    deficit = deficit_along_normal(co[samp], vn_it[samp])   # 股溝側壁約束（deficit_top_nearest）會發散：倒懸側壁下沿法線抬＝越抬越深（08-21 實測 o 跑到 61mm）
    raise_v = defaultdict(float)
    for k, vi in enumerate(near_cage[samp]):
        if deficit[k] > 0:
            raise_v[vi] = max(raise_v[vi], deficit[k])
    if it == 0:
        P(f"U0 (CC limit, o=0): violators {(deficit>0).sum()} deficit p50 {np.percentile(deficit[deficit>0],50)*1000:.2f} max {deficit.max()*1000:.2f} mm")
    if not raise_v:
        P(f"iter {it}: converged"); ev.to_mesh_clear(); break
    for vi, d in raise_v.items():
        o[vi] += min(d * 1.1 + 0.0001, 0.003)
    if it % 3 == 2:
        o2 = o.copy()
        for v in mov_idx:
            nb = [u for u in cage_adj[v] if movable[u]]
            if nb: o2[v] = max(o[v], float(np.mean(o[nb])))
        o = o2
    P(f"iter {it}: violators {(deficit>0).sum()} max deficit {deficit.max()*1000:.2f}mm  o: max {o.max()*1000:.2f} p50(core) {np.percentile(o[core],50)*1000:.2f}")
    ev.to_mesh_clear()
me, ev, co, polys, luv, lvi, dv = evaluate(o)
U_vn = face_normals_and_vn(co, polys)
deficit = deficit_along_normal(co[samp], U_vn[samp])
P(f"U final: violators {(deficit>0).sum()} max deficit {deficit.max()*1000:.2f}mm; o max {o.max()*1000:.2f}mm mean(core) {o[core].mean()*1000:.2f}mm")

# ---- 線性孿生：同拓樸、同 UV、位置＝多面體上的雙線性點（遊戲裡皮膚的 UV↔3D 就是這個）----
# CC 極限面的參數化相對多面體有切向漂移（不均勻 cage 可達數 mm）⇒ 等值線必須在線性孿生上切，
# 再沿平滑法線抬到 U。牆腳＝線性孿生上的等值線點＝嚴格落在手繪線上。
lin = body.copy(); lin.data = body.data.copy(); lin.name = "FundoshiLin"
bpy.context.collection.objects.link(lin)
for m in list(lin.modifiers): lin.modifiers.remove(m)
lin.parent = None
ml = lin.modifiers.new("ss", 'SUBSURF'); ml.levels = SUBD; ml.render_levels = SUBD
ml.subdivision_type = 'SIMPLE'; ml.uv_smooth = 'NONE'
dgl = bpy.context.evaluated_depsgraph_get()
evl = lin.evaluated_get(dgl); mel = evl.to_mesh()
co_lin = arr(mel)
lvi_l = np.empty(len(mel.loops), np.int64); mel.loops.foreach_get("vertex_index", lvi_l)
uvd_l = mel.uv_layers["UVMap"].data
luv_l = np.empty(len(uvd_l) * 2); uvd_l.foreach_get("uv", luv_l); luv_l = luv_l.reshape(-1, 2)
assert len(co_lin) == len(co) and np.array_equal(lvi_l, lvi), "linear twin topology mismatch"
assert np.abs(luv_l - luv).max() < 1e-6, f"linear twin UV mismatch {np.abs(luv_l-luv).max()}"
sd_lin = signed_to_skin(co_lin[samp])
P(f"linear twin: verts {len(co_lin)}; on-skin check |d| max {np.abs(sd_lin).max()*1000:.3f}mm")
evl.to_mesh_clear()
U_bvh = BVHTree.FromPolygons([tuple(p) for p in co], [tuple(p) for p in polys])
def lift_to_U(p, n):
    """從線性點沿平滑法線找 U（先往上再往下），回 (pos, ok)"""
    cands = []
    h1 = U_bvh.ray_cast(Vector(p), Vector(n), 0.02)
    if h1[0] is not None and h1[1].dot(Vector(n)) > 0: cands.append((h1[3], np.array(h1[0])))
    h2 = U_bvh.ray_cast(Vector(p), Vector(-n), 0.02)
    if h2[0] is not None and h2[1].dot(Vector(n)) > 0: cands.append((h2[3], np.array(h2[0])))
    if cands:
        cands.sort(key=lambda c: c[0]); return cands[0][1], True
    loc, nor, idx, dist = U_bvh.find_nearest(Vector(p), 0.02)
    if loc is not None: return np.array(loc), False
    return np.array(p), False

# ---------------- 4) 頂點遮罩值（per-loop → per-vertex）＋小島清理 ----------------
mval_loop = sample_mask(luv)
msum = np.zeros(len(co)); mcnt = np.zeros(len(co))
np.add.at(msum, lvi, mval_loop); np.add.at(mcnt, lvi, 1)
mval = msum / np.maximum(mcnt, 1)
# 縫兩側不一致檢查
mmin = np.full(len(co), 9.0); mmax = np.full(len(co), -9.0)
np.minimum.at(mmin, lvi, mval_loop); np.maximum.at(mmax, lvi, mval_loop)
disagree = ((mmin < 0.5) & (mmax > 0.5)).sum()
P(f"verts straddling 0.5 across UV seam: {disagree}")
inside = (mval > 0.5) & (co[:, 2] < 1.3)   # 元結帶排除
# 連通分量：翻掉小島（<40 頂點 ≈ 6cm²）
sub_adj = defaultdict(set)
for vs in polys:
    k = len(vs)
    for i in range(k):
        a, b = vs[i], vs[(i + 1) % k]; sub_adj[a].add(b); sub_adj[b].add(a)
def components(flag):
    seen = np.zeros(len(flag), bool); comps = []
    for s in np.where(flag)[0]:
        if seen[s]: continue
        q = deque([s]); seen[s] = True; comp = [s]
        while q:
            x = q.popleft()
            for y in sub_adj[x]:
                if flag[y] and not seen[y]:
                    seen[y] = True; q.append(y); comp.append(y)
        comps.append(comp)
    return comps
for _round in range(2):
    ci = components(inside); co_ = components(~inside)
    small_in = [c for c in ci if len(c) < 40]; small_out = [c for c in co_ if len(c) < 40]
    for c in small_in: inside[c] = False
    for c in small_out: inside[c] = True
    P(f"components: inside {len(ci)} (flipped {len(small_in)} small), outside {len(co_)} (flipped {len(small_out)} small)")
mval_c = np.where(inside, np.maximum(mval, 0.5 + 1e-4), np.minimum(mval, 0.5 - 1e-4))

# ---------------- 5) marching triangles 裁切 → 頂板（U 上）----------------
tris = []
for vs in polys:
    for i in range(1, len(vs) - 1):
        tris.append((vs[0], vs[i], vs[i + 1]))
tris = np.array(tris)
# 只處理有 inside 頂點的三角形
keep_tri = inside[tris].any(1)
tris = tris[keep_tri]
P(f"plate candidate tris {len(tris)}")

lin_co = list(co_lin)         # 線性孿生座標（會追加裁切點）
vert_dv = list(dv)
vert_n = list(U_vn)
edge_cut = {}
SNAP_T = 0.15                 # 裁切點太靠近既有頂點 → 把頂點本人搬到等值線上（避免細長三角）
snapped = {}
def cut_point(a, b):
    key = (a, b) if a < b else (b, a)
    if key in edge_cut: return edge_cut[key]
    ma, mb = mval_c[a], mval_c[b]
    t = float(np.clip((0.5 - ma) / (mb - ma), 0.0, 1.0))
    if t < SNAP_T and a not in snapped:
        snapped[a] = True; lin_co[a] = co_lin[a] * (1 - t) + co_lin[b] * t; edge_cut[key] = a; return a
    if t > 1 - SNAP_T and b not in snapped:
        snapped[b] = True; lin_co[b] = co_lin[a] * (1 - t) + co_lin[b] * t; edge_cut[key] = b; return b
    if a in snapped and t < SNAP_T: edge_cut[key] = a; return a
    if b in snapped and t > 1 - SNAP_T: edge_cut[key] = b; return b
    pos = co_lin[a] * (1 - t) + co_lin[b] * t
    n = vert_n[a] * (1 - t) + vert_n[b] * t; n = n / np.linalg.norm(n)
    wd = defaultdict(float)
    for g, w in vert_dv[a]: wd[g] += w * (1 - t)
    for g, w in vert_dv[b]: wd[g] += w * t
    idx = len(lin_co)
    lin_co.append(pos); vert_n.append(n); vert_dv.append(sorted(wd.items()))
    edge_cut[key] = idx
    return idx
plate_faces = []
for a, b, c in tris:
    ins = [inside[a], inside[b], inside[c]]
    k = sum(ins)
    if k == 3:
        plate_faces.append((a, b, c)); continue
    vs = [a, b, c]
    if k == 1:
        i = ins.index(True); p = vs[i]; q = vs[(i + 1) % 3]; r = vs[(i + 2) % 3]
        plate_faces.append((p, cut_point(p, q), cut_point(p, r)))
    else:  # k == 2
        i = ins.index(False); p = vs[i]; q = vs[(i + 1) % 3]; r = vs[(i + 2) % 3]
        cq = cut_point(p, q); cr = cut_point(p, r)
        plate_faces.append((q, r, cr)); plate_faces.append((q, cr, cq))
plate_faces = np.array([f for f in plate_faces if len(set(f)) == 3])
lin_co = np.array(lin_co); vert_n = np.array(vert_n)
used = np.unique(plate_faces)
P(f"plate faces {len(plate_faces)} verts used {len(used)} (cut points {len(edge_cut)}, snapped {len(snapped)})")
# 所有板頂點：線性點 → 抬到 U（沿平滑法線）＝每頂點一個抬升量 h（沿 n），
# 1-ring 中值濾掉離群（射線打到摺疊處的另一片 U）→ 頂點 = lin + h·n
def lift_h(pp, nn):
    q, ok = lift_to_U(pp, nn)
    return float(np.dot(q - pp, nn)), ok
hmap = {}
miss = 0
for v in used:
    h, ok = lift_h(lin_co[v], vert_n[v]); miss += (not ok)
    hmap[int(v)] = h if (ok and -0.002 < h < GAP_MAX) else np.nan
plate_adj = defaultdict(set)
for f in plate_faces:
    for i in range(3):
        a, b = int(f[i]), int(f[(i + 1) % 3]); plate_adj[a].add(b); plate_adj[b].add(a)
def ring_median(v, hm, depth=0):
    vals = [hm[u] for u in plate_adj[v] if not np.isnan(hm.get(u, np.nan))]
    if len(vals) >= 2: return float(np.median(vals))
    if depth < 3:
        vals = [ring_median(u, hm, depth + 1) for u in plate_adj[v]]
        vals = [x for x in vals if not np.isnan(x)]
        if vals: return float(np.median(vals))
    return np.nan
fixed_out = 0
for _pass in range(3):
    hm2 = dict(hmap)
    for v in used:
        v = int(v); med = ring_median(v, hmap)
        if np.isnan(hmap[v]) or (not np.isnan(med) and abs(hmap[v] - med) > 0.003):
            hm2[v] = med if not np.isnan(med) else hmap[v]; fixed_out += 1
    hmap = hm2
nan_left = sum(1 for v in used if np.isnan(hmap[int(v)]))
glob_med = float(np.nanmedian([hmap[int(v)] for v in used]))
for v in used:
    if np.isnan(hmap[int(v)]): hmap[int(v)] = glob_med
new_co = lin_co.copy()
for v in used:
    new_co[v] = lin_co[v] + hmap[int(v)] * vert_n[v]
lh = np.array([hmap[int(v)] for v in used]) * 1000
P(f"lift to U: ray misses {miss}/{len(used)}; outliers fixed {fixed_out}; nan left {nan_left}; h p5 {np.percentile(lh,5):.2f} p50 {np.percentile(lh,50):.2f} p95 {np.percentile(lh,95):.2f} max {lh.max():.2f} mm")

# ---------------- 6) 輪廓迴圈（邊界邊）＋ σ 微平滑 ----------------
ecount = defaultdict(int)
for f in plate_faces:
    for i in range(3):
        a, b = f[i], f[(i + 1) % 3]; ecount[(a, b) if a < b else (b, a)] += 1
bedges = [e for e, c in ecount.items() if c == 1]
badj = defaultdict(list)
for a, b in bedges: badj[a].append(b); badj[b].append(a)
assert all(len(v) == 2 for v in badj.values()), "non-manifold boundary"
loops = []; seen = set()
for s in badj:
    if s in seen: continue
    loop = [s]; seen.add(s); prev = None; cur = s
    while True:
        nb = badj[cur]; nxt = nb[0] if nb[0] != prev else nb[1]
        if nxt == s: break
        loop.append(nxt); seen.add(nxt); prev, cur = cur, nxt
    loops.append(loop)
loops.sort(key=len, reverse=True)
P("contour loops:", [len(l) for l in loops])
def loop_len(l):
    c = new_co[l]; return float(np.linalg.norm(np.roll(c, -1, 0) - c, axis=1).sum())
P("loop lengths m:", [round(loop_len(l), 3) for l in loops])
assert len(loops) == 3, f"expected 3 loops (waist + 2 legs), got {len(loops)}"

raw_contour = {i: lin_co[l].copy() for i, l in enumerate(loops)}
attach_co_all = {}; roll_co_all = {}

# ---- 6a) 公分級走向去噪（08-21 user 授權「公分級的走向也去噪、後腰窩 V 凹除外」）----
# 沿弧長 σ=SIGMA_ARC 高斯平滑輪廓位置；保護區（後腰窩 縦褌 T 交接、user 刻意設計）
# 權重=1 不動、外圍 4cm 軟過渡。平滑後沿法線射線貼回皮膚（射線方向一致＝零 zigzag；
# nearest-point 貼回會在非平面 quad 側跳 5.7mm＝舊 zigzag 真兇，不用）。
SIGMA_ARC = 0.040
def protect_w(pts):
    """1=保護（不平滑）。後腰窩=back(-y) 中央柱：|x|<0.11、y<-0.10；4cm 軟過渡"""
    dx = np.maximum(np.abs(pts[:, 0]) - 0.11, 0.0)
    dy = np.maximum(pts[:, 1] - (-0.10), 0.0)   # 往前（+y）超出 -0.10 的距離
    d = np.sqrt(dx ** 2 + dy ** 2)
    t = np.clip(d / 0.04, 0.0, 1.0)
    return 1.0 - t * t * (3 - 2 * t)
for li, l in enumerate(loops):
    c = lin_co[l]; n = len(l)
    seg = np.linalg.norm(np.roll(c, -1, 0) - c, axis=1)
    s = np.concatenate([[0], np.cumsum(seg)])[:-1]; L = s[-1] + seg[-1]
    out = np.empty_like(c); out12 = np.empty_like(c)
    for i in range(n):
        d = np.abs(s - s[i]); d = np.minimum(d, L - d)
        w = np.exp(-0.5 * (d / SIGMA_ARC) ** 2); w /= w.sum()
        out[i] = (w[:, None] * c).sum(0)
        w2 = np.exp(-0.5 * (d / 0.012) ** 2); w2 /= w2.sum()
        out12[i] = (w2[:, None] * c).sum(0)
    # 保護區＝σ12 輕平滑（V 形狀半徑 5~8cm >> 12mm＝形狀保留、噪聲照除）；其餘 σ40
    pw = protect_w(c)
    out = pw[:, None] * out12 + (1 - pw[:, None]) * out
    # **不貼回皮膚**（08-21 血價：平滑曲線射回 3cm 多面體＝重新量化成面片段＝頂緣階梯平台）。
    # 平滑點離皮 ≤~4mm（弦差）；頂緣=抬到光滑 U 上（下一段）、牆腳=另用射線落皮（藏皮下）。
    for i in range(n):
        lin_co[l[i]] = out[i]
    dev = np.linalg.norm(lin_co[l] - raw_contour[li], axis=1) * 1000
    P(f"loop {li}: arc-smooth σ{SIGMA_ARC*1000:.0f} dev p50 {np.percentile(dev,50):.2f} p90 {np.percentile(dev,90):.2f} max {dev.max():.2f} mm; protected frac {(pw>0.5).mean()*100:.1f}%")

# ---- 6a2) 邊界位移＝形變場傳入內部（08-21 血價：只搬邊界頂點＝12.8mm 位移壓過
# 2~4mm 的第一圈三角形＝邊緣帶手風琴摺片（渲染=頂緣平台/階梯）。位移用 σ12mm
# 高斯內插、離邊 25mm smoothstep 衰減＝三角形跟著整帶平滑變形；內部頂點再重新抬回 U。----
contour_set = set(int(v) for l in loops for v in l)
disp_pts = []; disp_vec = []
for li, l in enumerate(loops):
    for j, v in enumerate(l):
        disp_pts.append(raw_contour[li][j]); disp_vec.append(lin_co[v] - raw_contour[li][j])
disp_pts = np.array(disp_pts); disp_vec = np.array(disp_vec)
kd_disp = KDTree(len(disp_pts))
for i_, p_ in enumerate(disp_pts): kd_disp.insert(Vector(p_), i_)
kd_disp.balance()
WARP_R = 0.025
warp_n = 0; relift_miss = 0
for v in used:
    v = int(v)
    if v in contour_set: continue
    hits = kd_disp.find_n(Vector(lin_co[v]), 8)
    d0 = hits[0][2]
    if d0 > WARP_R: continue
    wsum = 0.0; acc = np.zeros(3)
    for loc_, i_, dd in hits:
        w_ = np.exp(-0.5 * (dd / 0.012) ** 2)
        acc += w_ * disp_vec[i_]; wsum += w_
    t_ = 1.0 - d0 / WARP_R
    fall = t_ * t_ * (3 - 2 * t_)
    lin_co[v] = lin_co[v] + (acc / max(wsum, 1e-12)) * fall
    h_, ok_ = lift_h(lin_co[v], vert_n[v])
    if ok_ and -0.006 < h_ < GAP_MAX:
        hmap[v] = h_
    else:
        relift_miss += 1          # 沿用舊 h（U 平滑、誤差 ≤ 弦差）
    new_co[v] = lin_co[v] + hmap[v] * vert_n[v]
    warp_n += 1
P(f"edge-band warp: {warp_n} interior verts followed the boundary (relift miss {relift_miss})")

for li, l in enumerate(loops):
    c = lin_co[l]; n = len(l)
    seg = np.linalg.norm(np.roll(c, -1, 0) - c, axis=1)
    s = np.concatenate([[0], np.cumsum(seg)])[:-1]; L = s[-1] + seg[-1]
    out = c.copy()
    hs = np.empty(n)
    for i in range(n):
        # 不再貼回三角化皮膚（quad 非平面處 ≤5.7mm 的側跳＝頂緣 zigzag 真兇）；牆腳＝雙線性孿生上的等值線本人，
        # 與遊戲三角化的差距由 SINK 吃掉（牆垂直＝可見線不動）
        h, ok = lift_h(lin_co[l[i]], vert_n[l[i]])
        hs[i] = h if (ok and -0.006 < h < GAP_MAX) else np.nan
    # 補 nan（鄰近內插）→ 中值 5 → 高斯 **σ25mm**（08-21 user 抓「只平滑單一方向？」：
    # σ40 平的是輪廓位置（面內走向），**離皮高度剖面**先前只 σ6＝邊緣沿線上下起伏殘留；
    # 高度也上公分級。V 保護區照 protect_w 豁免（凹處高度變化是設計的一部分）。
    idxs = np.arange(n); good = ~np.isnan(hs)
    if (~good).any(): hs[~good] = np.interp(idxs[~good], idxs[good], hs[good], period=n)
    hs_m = np.array([np.median(hs[[(i + k) % n for k in (-2, -1, 0, 1, 2)]]) for i in range(n)])
    # 頂緣高度＝貼死 U＋σ8 去噪（08-21：σ25 獨立平滑 ⇒ 邊高度 ≠ 內部布面高度 ⇒ 縫邊捲瘤；
    # 頂緣與內部同在 U 上＝無捲、起伏=身體真形狀）
    hs_s = np.empty(n)
    for i in range(n):
        d = np.abs(s - s[i]); d = np.minimum(d, L - d)
        w2 = np.exp(-0.5 * (d / 0.008) ** 2); w2 /= w2.sum(); hs_s[i] = (w2 * hs_m).sum()
    hs_s = np.maximum(hs_s, -0.004)   # 平滑線可高於 U 一點（頂緣=U 上點＝仍在皮外）
    topc = np.array([lin_co[l[i]] + hs_s[i] * vert_n[l[i]] for i in range(n)])
    # 頂緣本人沿迴圈 σ6mm 平滑：牆腳躺在多面體的稜線上（每 3cm 一個 7° 摺角）；
    # 頂緣若只是牆腳的平移就會複製這些摺角（渲染＝每 3cm 一個小凹痕/小耳朵）。
    # 可見布邊＝頂緣 ⇒ 頂緣自己平滑；牆變成「平滑頂緣→有摺角牆腳」的直紋面（≤1~2mm 扭轉，不可見）。
    topS = np.empty_like(topc)
    for i in range(n):
        d = np.abs(s - s[i]); d = np.minimum(d, L - d)
        w = np.exp(-0.5 * (d / 0.006) ** 2); w /= w.sum(); topS[i] = (w[:, None] * topc).sum(0)
    def kinkstat(pts):
        K = 5; n_ = len(pts); out_ = np.empty(n_)
        for i_ in range(n_):
            a_ = pts[(i_ - K) % n_]; b_ = pts[(i_ + K) % n_]; q_ = pts[i_]
            ab_ = b_ - a_; t_ = np.dot(q_ - a_, ab_) / max(np.dot(ab_, ab_), 1e-12)
            out_[i_] = np.linalg.norm(q_ - a_ - np.clip(t_, 0, 1) * ab_)
        return np.percentile(out_, 99) * 1000, out_.max() * 1000
    k_lin = kinkstat(lin_co[l]); k_topc = kinkstat(topc); k_topS = kinkstat(topS)
    P(f"loop {li}: kink p99/max  lin {k_lin[0]:.2f}/{k_lin[1]:.2f}  topc {k_topc[0]:.2f}/{k_topc[1]:.2f}  topS {k_topS[0]:.2f}/{k_topS[1]:.2f} mm")
    tdev = np.linalg.norm(topS - topc, axis=1) * 1000
    # 皮膚淨空回夾（只准抬不准沉）：高度剖面平滑會在皮膚凸點處把邊緣壓到皮下——
    # 凸點處局部讓步（抬到淨空 1.5mm），其餘維持平滑剖面
    # 皮膚淨空回夾＝**錐形攤開**（08-21 定罪：逐點抬升＝邊上 2mm 尖峰缺口（p99 kink 1.5mm、
    # 熱點=髖側=user 截圖）。需求抬量沿環以 1:12 坡度 morphological cone-max 攤開＝平滑脊、
    # 約束照樣點點滿足）
    need = np.zeros(n)
    for i in range(n):
        nvec = vert_n[l[i]]
        hit = skin_bvh.ray_cast(Vector(topS[i] + nvec * 0.0002), Vector(-nvec), 0.02)
        dcl = hit[3] + 0.0002 if hit[0] is not None else None
        if dcl is None:
            hit2 = skin_bvh.ray_cast(Vector(topS[i] - nvec * 0.0002), Vector(nvec), 0.01)
            if hit2[0] is not None and hit2[1].dot(Vector(nvec)) > 0:
                need[i] = hit2[3] + 0.0015
        elif dcl < 0.0015:
            need[i] = 0.0015 - dcl
    SLOPE = 1.0 / 12.0
    seg2 = np.linalg.norm(np.roll(topS, -1, 0) - topS, axis=1)
    for _cyc in range(2):
        for i in range(1, 2 * n):
            a_, b_ = i % n, (i - 1) % n
            need[a_] = max(need[a_], need[b_] - SLOPE * seg2[b_])
        for i in range(2 * n - 1, 0, -1):
            a_, b_ = (i - 1) % n, i % n
            need[a_] = max(need[a_], need[b_] - SLOPE * seg2[a_])
    clamp_n = int((need > 1e-6).sum())
    for i in range(n):
        if need[i] > 1e-6:
            topS[i] = topS[i] + np.array(vert_n[l[i]]) * need[i]
    if clamp_n: P(f"loop {li}: clearance cone-clamp touched {clamp_n} verts (max raise {need.max()*1000:.2f}mm)")
    for i in range(n):
        new_co[l[i]] = topS[i]
    # 牆腳＝從頂緣沿 -n 射線落到皮膚；**深度沿環 σ8 平滑**（08-21：腳深逐點抄皮膚面片
    # ＝牆面一條條豎紋扭）＋「至少埋 0.5mm」回夾（平滑不可把腳抬出皮膚）
    miss_ft = 0
    depth_ray = np.empty(n); ns_ray = np.empty((n, 3))
    for i in range(n):
        v = l[i]; nvec = vert_n[v]
        hit = skin_bvh.ray_cast(Vector(topS[i] + nvec * 0.001), Vector(-nvec), 0.08)
        if hit[0] is not None:
            depth_ray[i] = float(np.dot(topS[i] - np.array(hit[0]), nvec)); ns_ray[i] = np.array(hit[1])
        else:
            loc, _n2, _i2, _d2 = skin_bvh.find_nearest(Vector(topS[i]))
            depth_ray[i] = float(np.dot(topS[i] - np.array(loc), nvec)); ns_ray[i] = np.array(_n2); miss_ft += 1
    depth_s = np.empty(n)
    for i in range(n):
        d = np.abs(s - s[i]); d = np.minimum(d, L - d)
        w2 = np.exp(-0.5 * (d / 0.008) ** 2); w2 /= w2.sum(); depth_s[i] = (w2 * depth_ray).sum()
    depth_f = np.maximum(depth_s, depth_ray + 0.0005)
    for i in range(n):
        v = l[i]
        lin_co[v] = topS[i] - vert_n[v] * depth_f[i]
    # ---- 裾邊唇（08-21 終兇＝牆×多面體皮膚的交線鋸齒（皮膚 3cm 面片、±1~2mm/3cm 週期）：
    # 布側平滑已打完、交線是皮膚解析度的鏡子。構造解＝布唇：牆在皮上 1mm 處接一圈 5mm 寬
    # 布唇壓在皮膚上、外緣埋皮下 1.5mm ⇒ 可見邊界＝布唇的光滑邊、鋸齒交線藏唇下。----
    # ---- 捲邊圓筒（08-21 終構造）：任何「逐點貼皮」的邊特徵都繼承皮膚 3cm 面片噪聲
    # （平唇被面片吞吐＝破布條實錘）。捲邊＝截面圓弧掃過光滑頂緣框架、**完全不投影皮膚**
    # ＝可見輪廓構造性光滑；最低點埋皮下（名目 ~5mm、面片 ±2mm 也埋住）；
    # 面片交線發生在筒腹下側＝被筒身自己遮住。真實廻し布邊＝捲的。
    ROLL_R = 0.004
    ROLL_ANGLES = [25.0, 60.0, 95.0, 130.0, 165.0]
    o_arr = np.empty((n, 3))
    for i in range(n):
        v = l[i]; nvec = vert_n[v]
        nb_in = [u for u in plate_adj[v] if u not in contour_set]
        if nb_in:
            din = np.mean([new_co[u] for u in nb_in], axis=0) - topS[i]
        else:
            din = np.cross(topS[(i + 1) % n] - topS[(i - 1) % n], nvec)
        o = -(din - nvec * np.dot(din, nvec))
        ol = np.linalg.norm(o)
        if ol < 1e-9:
            o = np.cross(topS[(i + 1) % n] - topS[(i - 1) % n], nvec); ol = np.linalg.norm(o)
        o_arr[i] = o / max(ol, 1e-12)
    o_sm = np.empty_like(o_arr)
    for i in range(n):
        d = np.abs(s - s[i]); d = np.minimum(d, L - d)
        w2 = np.exp(-0.5 * (d / 0.008) ** 2); w2 /= w2.sum(); o_sm[i] = (w2[:, None] * o_arr).sum(0)
    o_sm /= np.maximum(np.linalg.norm(o_sm, axis=1), 1e-12)[:, None]
    for i in range(n):
        v = l[i]; nvec = vert_n[v]
        attach = topS[i] - nvec * max(depth_s[i] - 0.003, 0.0)   # 光滑深度＝光滑起捲線（皮上名目 3mm）
        attach_co_all[v] = attach
        rings = []
        for th_deg in ROLL_ANGLES:
            th = np.radians(th_deg)
            rings.append(attach + ROLL_R * np.sin(th) * o_sm[i] - ROLL_R * (1.0 - np.cos(th)) * nvec)
        roll_co_all[v] = rings
    if miss_ft: P(f"loop {li}: foot ray misses {miss_ft} (fell back to nearest)")
    P(f"loop {li}: foot depth ray p50 {np.percentile(depth_ray,50)*1000:.1f} max {depth_ray.max()*1000:.1f} mm; smoothed extra burial p90 {np.percentile(depth_f-depth_ray,90)*1000:.2f} mm")
    P(f"loop {li}: top-ring smoothing move p50 {np.percentile(tdev,50):.2f} p90 {np.percentile(tdev,90):.2f} max {tdev.max():.2f} mm")
    dev = np.linalg.norm(lin_co[l] - raw_contour[li], axis=1) * 1000
    P(f"loop {li}: (no reprojection) move p50 {np.percentile(dev,50):.2f} p90 {np.percentile(dev,90):.2f} max {dev.max():.2f} mm; gap p50 {np.percentile(hs_s,50)*1000:.2f} max {hs_s.max()*1000:.2f}")

# ---- 6b) 邊緣帶高度場連續化：輪廓高度（迴圈濾波）與內部（射線抬升）在 4 圈內調和混合 ----
hcur = {int(v): float(np.dot(new_co[v] - lin_co[v], vert_n[v])) for v in used}
ring = {int(v): 0 for l in loops for v in l}
frontier = list(ring.keys())
for depth in range(1, 6):
    nxt = []
    for v in frontier:
        for u in plate_adj[v]:
            if u not in ring: ring[u] = depth; nxt.append(u)
    frontier = nxt
band = [v for v, d in ring.items() if 1 <= d <= 5]
for _it in range(12):
    h2 = dict(hcur)
    for v in band:
        nb = [hcur[u] for u in plate_adj[v] if u in hcur]
        if not nb: continue
        w_ = 1.0 - (ring[v] - 1) / 5.0          # 第 1 圈全調和 → 第 5 圈回到射線值
        h2[v] = (1 - w_) * hcur[v] + w_ * float(np.mean(nb))
    hcur = h2
for v in band:
    new_co[v] = lin_co[v] + hcur[v] * vert_n[v]
P(f"edge band harmonic blend: {len(band)} verts (≤5 rings)")

# ---------------- 7) 牆腳（皮膚上）＋頂板外推 ----------------
plate_v_idx = used
remap = {int(v): i for i, v in enumerate(plate_v_idx)}
contour_set = set(int(v) for l in loops for v in l)
# 股溝（兩臀相貼的深 V）：布不可能以 5mm 厚度存在於 <10mm 的縫裡；實物＝被夾住。
# 這裡容許頂板在 V 底與側壁皮膚相交（藏在兩臀之間、無人可見），不做局部抬升
# （08-21 實測：抬升在倒懸的側壁下只會越抬越深＝尖刺）。
# ---- 7b) 股溝隧道擠壓（08-21 破洞修）：布側邊陷在大腿皮膚下＝黑帶中間露膚「破洞」
# （站立時夾在兩腿間看不見、睡姿張腿全露）。真布行為＝被大腿夾住貼著推——
# 陷入的頂點沿「最近皮膚的法線」推到皮膚外 0.8mm（頂/底同 delta＝厚度不變），
# delta 場 2 輪 1-ring 平滑＝無新皺。輪廓頂點不動（牆腳＝手繪線鐵約束）。
PRESS_EPS = 0.0008
delta = {int(v): np.zeros(3) for v in plate_v_idx}
press_n = 0
for _pass in range(6):
    moved = 0
    for v in plate_v_idx:
        v = int(v)
        if v in contour_set: continue
        q = new_co[v] + delta[v] + vert_n[v] * T_CLOTH
        loc, nor, idx_, dd = U_bvh.find_nearest(Vector(q), 0.03)   # 壓在光滑 U 上（U≥皮膚）——壓多面體皮膚＝面片瘤實錘（08-21）
        if loc is None: continue
        nor = np.array(nor); sd_ = float(np.dot(q - np.array(loc), nor))
        if sd_ < PRESS_EPS - 1e-5:
            delta[v] = delta[v] + (PRESS_EPS - sd_) * nor; moved += 1
    press_n = max(press_n, moved)
    if moved == 0: break
# delta 場平滑（含零值＝自然衰減到未動區）
for _sm in range(2):
    d2 = dict(delta)
    for v in plate_v_idx:
        v = int(v)
        if v in contour_set: continue
        nb = [delta[u] for u in plate_adj[v] if u in delta]
        if nb: d2[v] = 0.5 * delta[v] + 0.5 * np.mean(nb, axis=0)
    delta = d2
# 平滑會把矯正拉回皮下——平滑後再補壓到收斂（終態必須滿足約束）
for _pass in range(6):
    moved = 0
    for v in plate_v_idx:
        v = int(v)
        if v in contour_set: continue
        q = new_co[v] + delta[v] + vert_n[v] * T_CLOTH
        loc, nor, idx_, dd = U_bvh.find_nearest(Vector(q), 0.03)
        if loc is None: continue
        nor = np.array(nor); sd_ = float(np.dot(q - np.array(loc), nor))
        if sd_ < PRESS_EPS - 1e-5:
            delta[v] = delta[v] + (PRESS_EPS - sd_) * nor; moved += 1
    if moved == 0: break
dmag = np.array([np.linalg.norm(delta[int(v)]) for v in plate_v_idx]) * 1000
P(f"crotch press-out: verts moved {int((dmag>0.05).sum())} |d| p95 {np.percentile(dmag,95):.2f} max {dmag.max():.2f} mm (final pass moved={moved})")
for v in plate_v_idx:
    new_co[int(v)] = new_co[int(v)] + delta[int(v)]
top_co = new_co[plate_v_idx] + T_CLOTH * vert_n[plate_v_idx]
top_faces = np.vectorize(remap.get)(plate_faces)
top_n_idx = plate_v_idx
foot_co_all = {}
gap_stats = []
for li, l in enumerate(loops):
    for vi in l:
        p = new_co[vi]; n = vert_n[vi]; f = lin_co[vi]
        gap_stats.append(np.dot(p - f, n))
        foot_co_all[vi] = f - n * SINK
gap_stats = np.array(gap_stats) * 1000
P(f"underside gap at contour: p50 {np.percentile(gap_stats,50):.2f} p90 {np.percentile(gap_stats,90):.2f} max {gap_stats.max():.2f} min {gap_stats.min():.2f} mm  (wall height = gap + {T_CLOTH*1000:.0f}mm + sink)")

# ---------------- 8) 組裝：頂板 + 牆 + 底板 ----------------
V = [tuple(v) for v in top_co]
VN_src = list(vert_n[plate_v_idx])
DV = [vert_dv[int(v)] for v in plate_v_idx]
# 頂板繞向：cross 與 U 法線同向
F = []
flipped = 0
for f in top_faces:
    a, b, c = f
    nrm = np.cross(top_co[b] - top_co[a], top_co[c] - top_co[a])
    if np.dot(nrm, VN_src[a] + VN_src[b] + VN_src[c]) < 0:
        f = (a, c, b); flipped += 1
    F.append(tuple(int(x) for x in f))
top_faces = np.array(F)
P(f"top faces flipped to outward: {flipped}/{len(F)}")
# 邊界邊 → 對面內部頂點（牆外向判定）
third = {}
for f in top_faces:
    for i in range(3):
        a, b, c = f[i], f[(i + 1) % 3], f[(i + 2) % 3]
        third[(a, b) if a < b else (b, a)] = c
# 牆（兩段：頂緣→attach、attach→腳）＋裾邊唇（attach→hem 上下兩面楔＝水密）
foot_idx = {}; attach_idx = {}; roll_idx = {}
F_wall = []; F_hem = []
for li, l in enumerate(loops):
    for vi in l:
        foot_idx[vi] = len(V); V.append(tuple(foot_co_all[vi])); VN_src.append(vert_n[vi]); DV.append(vert_dv[vi])
        attach_idx[vi] = len(V); V.append(tuple(attach_co_all[vi])); VN_src.append(vert_n[vi]); DV.append(vert_dv[vi])
        roll_idx[vi] = []
        for ring_pt in roll_co_all[vi]:
            roll_idx[vi].append(len(V)); V.append(tuple(ring_pt)); VN_src.append(vert_n[vi]); DV.append(vert_dv[vi])
    n = len(l)
    votes = 0
    for i in range(n):
        a, b = l[i], l[(i + 1) % n]
        ra, rb = remap[a], remap[b]
        c = third[(ra, rb) if ra < rb else (rb, ra)]
        mid = 0.5 * (top_co[ra] + top_co[rb]); e = top_co[rb] - top_co[ra]
        outdir = mid - top_co[c]; outdir -= e * (outdir @ e) / max(e @ e, 1e-18)
        if np.linalg.norm(outdir) < 1e-4: continue          # 細長三角＝棄票
        pa, pb, pc = top_co[ra], np.array(V[foot_idx[a]]), np.array(V[foot_idx[b]])
        votes += 1 if np.dot(np.cross(pb - pa, pc - pa), outdir) >= 0 else -1
    flip_loop = votes < 0
    P(f"wall loop {li}: orientation votes {votes} (flip={flip_loop})")
    for i in range(n):
        a, b = l[i], l[(i + 1) % n]
        ra, rb = remap[a], remap[b]
        chain_a = [attach_idx[a]] + roll_idx[a] + [foot_idx[a]]
        chain_b = [attach_idx[b]] + roll_idx[b] + [foot_idx[b]]
        if flip_loop:
            F_wall.append((ra, rb, attach_idx[b], attach_idx[a]))
            F_wall.append((attach_idx[a], attach_idx[b], foot_idx[b], foot_idx[a]))
            for k in range(len(chain_a) - 1):
                F_hem.append((chain_a[k], chain_b[k], chain_b[k + 1], chain_a[k + 1]))
        else:
            F_wall.append((ra, attach_idx[a], attach_idx[b], rb))
            F_wall.append((attach_idx[a], foot_idx[a], foot_idx[b], attach_idx[b]))
            for k in range(len(chain_a) - 1):
                F_hem.append((chain_a[k], chain_a[k + 1], chain_b[k + 1], chain_b[k]))
# 唇上面朝外檢查（與皮膚法線同向）：每迴圈多數決一次翻
F.extend(tuple(int(x) for x in q) for q in F_wall)
F.extend(tuple(int(x) for x in q) for q in F_hem)
# 底板（U 本身，反向＝朝皮膚）。**輪廓頂點共用牆腳頂點**（08-21 破洞 bug：底板整圈停在
# 布面高度、牆腳沉在皮膚下＝2~9mm 開縫看得到布內側；焊到牆腳＝水密構造保證）
under_idx = []
for v in plate_v_idx:
    v = int(v)
    if v in foot_idx:
        under_idx.append(foot_idx[v])
    else:
        under_idx.append(len(V))
        V.append(tuple(new_co[v])); VN_src.append(vert_n[v]); DV.append(vert_dv[v])
for f in top_faces:
    F.append((int(under_idx[f[2]]), int(under_idx[f[1]]), int(under_idx[f[0]])))
V = np.array(V)
def tri_area(f):
    if len(f) == 3:
        a, b, c = V[f[0]], V[f[1]], V[f[2]]; return 0.5 * np.linalg.norm(np.cross(b - a, c - a))
    a, b, c, d = V[f[0]], V[f[1]], V[f[2]], V[f[3]]
    return 0.5 * (np.linalg.norm(np.cross(b - a, c - a)) + np.linalg.norm(np.cross(c - a, d - a)))
# 退化面不刪（封閉實體上刪面＝開洞、水密斷言會炸）：微推頂點 0.03mm 使面積非零
nudged = 0
for _pass in range(4):
    bad = [f for f in F if tri_area(f) <= 1e-10]
    if not bad: break
    for f in bad:
        vi_ = int(f[1])
        V[vi_] = V[vi_] + np.asarray(VN_src[vi_]) * 3e-5
        nudged += 1
P(f"degenerate faces nudged: {nudged}")
P(f"assembled verts {len(V)} faces {len(F)} (top {len(top_faces)} wall {sum(len(l) for l in loops)} under {len(top_faces)})")

# ---------------- 9) 圓柱 UV（沿用密度）----------------
cx, cy = float(V[:, 0].mean()), float(V[:, 1].mean())
theta = np.arctan2(V[:, 1] - cy, V[:, 0] - cx)
Ravg = float(np.sqrt((V[:, 0] - cx) ** 2 + (V[:, 1] - cy) ** 2).mean())
Mrep = max(1, round(2 * np.pi * Ravg / TILE_M))
uu = (theta + np.pi) / (2 * np.pi) * (Mrep / 12.0)
vv = V[:, 2] / (12.0 * TILE_M)
UVv = np.stack([uu, vv], 1)
P(f"CYL UV R={Ravg*100:.1f}cm periods={Mrep}")

# ---------------- 10) 建 mesh、繞向、法線、權重、替換 Fundoshi ----------------
me2 = bpy.data.meshes.new("FundoshiPlate")
me2.from_pydata([tuple(v) for v in V], [], [tuple(int(i) for i in f) for f in F])
me2.update()
me2.calc_loop_triangles()
fn_chk = np.array([np.dot(np.array(p.normal), VN_src[p.vertices[0]]) for p in me2.polygons[:len(top_faces)]])
P(f"top faces outward {(fn_chk>0).mean()*100:.1f}%")
# 接縫處 theta 跳變的面（u 跨 0/1）：UV 用 per-loop 修正
uvlayer = me2.uv_layers.new(name="UVMap")
lo_uv = np.empty((len(me2.loops), 2))
lvi2 = np.empty(len(me2.loops), np.int64); me2.loops.foreach_get("vertex_index", lvi2)
lo_uv[:] = UVv[lvi2]
per = Mrep / 12.0
for p in me2.polygons:
    ls = list(range(p.loop_start, p.loop_start + p.loop_total))
    us = lo_uv[ls, 0]
    if us.max() - us.min() > per * 0.5:
        lo_uv[ls, 0] = np.where(us < per * 0.5, us + per, us)
uvlayer.data.foreach_set("uv", lo_uv.ravel())
# FaceMask 頂點色（黑）＝沿用舊契約
ca = me2.color_attributes.new("FaceMask", 'BYTE_COLOR', 'CORNER')
ca.data.foreach_set("color", np.tile(np.array([0, 0, 0, 1.0], np.float32), len(me2.loops)))
for p in me2.polygons: p.use_smooth = True
# ---- 分區自訂法線（08-21 牆面肋紋定罪）：牆/頂板共用頂點＋切割邊碎三角形
# ＝頂點法線把兩區混平均＝牆上 ~5mm 間距著色肋。頂板/牆/底板各自區內平滑、
# 區界=銳邊（真布摺邊本來就銳）。面序契約：top_faces → 牆 → 底板。
n_top_f = len(top_faces); n_wall_f = (2 + 6) * sum(len(l) for l in loops)   # 兩段牆＋捲邊 6 條帶＝同一平滑組（布連續捲過）
def face_region(fi):
    if fi < n_top_f: return 0
    if fi < n_top_f + n_wall_f: return 1
    return 2
me2.calc_loop_triangles()
fnorm_all = np.empty(len(me2.polygons) * 3); me2.polygons.foreach_get("normal", fnorm_all)
fnorm_all = fnorm_all.reshape(-1, 3)
farea_all = np.empty(len(me2.polygons)); me2.polygons.foreach_get("area", farea_all)
acc = defaultdict(lambda: np.zeros(3))
for poly in me2.polygons:
    r = face_region(poly.index)
    w_ = max(farea_all[poly.index], 1e-12)
    for v in poly.vertices:
        acc[(v, r)] += fnorm_all[poly.index] * w_
loop_normals = np.empty((len(me2.loops), 3))
lvi3 = np.empty(len(me2.loops), np.int64); me2.loops.foreach_get("vertex_index", lvi3)
for poly in me2.polygons:
    r = face_region(poly.index)
    for lo in range(poly.loop_start, poly.loop_start + poly.loop_total):
        v = int(lvi3[lo])
        nv = acc[(v, r)]
        ln = np.linalg.norm(nv)
        loop_normals[lo] = nv / ln if ln > 1e-12 else fnorm_all[poly.index]
me2.normals_split_custom_set([tuple(x) for x in loop_normals])
P("split normals per region set (top/wall/under)")
# 頂板/底板法線＝U 解析法線；牆＝讓 Blender 算（頂緣共享頂點→自然圓肩著色）
me2.materials.append(bpy.data.materials["M_Fundoshi"])

vg_names = [vg.name for vg in fund.vertex_groups]
old = fund.data
fund.data = me2
bpy.data.meshes.remove(old)
if len(fund.vertex_groups) != len(vg_names):
    fund.vertex_groups.clear()
    for nm in vg_names: fund.vertex_groups.new(name=nm)
for i, wd in enumerate(DV):
    tot = sum(w for g, w in wd) or 1.0
    for g, w in wd:
        if w > 1e-4: fund.vertex_groups[g].add([i], w / tot, 'REPLACE')
unweighted = sum(1 for v in me2.vertices if not v.groups)
P(f"unweighted={unweighted}")
assert unweighted == 0
assert any(m.type == 'ARMATURE' for m in fund.modifiers)
# 清 work
bpy.data.objects.remove(work, do_unlink=True)
bpy.data.objects.remove(lin, do_unlink=True)

# ---------------- 11) 驗收數字 ----------------
# 水密驗證：全網格零開放邊（開縫 bug 的構造性排除）
ecnt_all = defaultdict(int)
for p_ in me2.polygons:
    for ek in p_.edge_keys: ecnt_all[tuple(sorted(ek))] += 1
open_edges = sum(1 for c in ecnt_all.values() if c == 1)
P(f"open edges (must be 0) = {open_edges}")
assert open_edges == 0, f"mesh not watertight: {open_edges} open edges"
me2.calc_loop_triangles()
tt = np.empty(len(me2.loop_triangles) * 3, np.int64); me2.loop_triangles.foreach_get("vertices", tt); tt = tt.reshape(-1, 3)
cc = arr(me2) * 1000
ar = 0.5 * np.linalg.norm(np.cross(cc[tt[:, 1]] - cc[tt[:, 0]], cc[tt[:, 2]] - cc[tt[:, 0]]), axis=1)
P(f"FINAL verts={len(me2.vertices)} tris={len(tt)} zero_area(<1e-6mm2)={(ar<1e-6).sum()}")
# 頂板 dihedral（3~4mm 邊）
fnorm = {p.index: np.array(p.normal) for p in me2.polygons}
ef = defaultdict(list)
for p in me2.polygons:
    if p.index >= len(top_faces): break
    for ek in p.edge_keys: ef[tuple(sorted(ek))].append(p.index)
dih = np.array([np.degrees(np.arccos(np.clip(np.dot(fnorm[a], fnorm[b]), -1, 1))) for fs in ef.values() if len(fs) == 2 for a, b in [fs]])
P(f"top plate dihedral p50 {np.percentile(dih,50):.2f} p90 {np.percentile(dih,90):.2f} p99 {np.percentile(dih,99):.2f} max {dih.max():.1f} (old shell p50 5.07 p90 29.84)")
# 穿刺：頂板/底板頂點 signed distance to skin
def signed_to_skin_facing(pts, nrm):
    """回 (signed, facing)：facing=最近皮膚法線與布法線同向（真穿出）；否則＝側壁（股溝夾縫）"""
    sdv = np.empty(len(pts)); fac = np.zeros(len(pts), bool)
    for k, p_ in enumerate(pts):
        loc, nor, idx, d = skin_bvh.find_nearest(Vector(p_))
        sv = np.dot(np.array(p_) - np.array(loc), np.array(nor))
        sdv[k] = d if sv >= 0 else -d
        fac[k] = np.dot(np.array(nor), nrm[k]) > 0.5
    return sdv, fac
sd_top, fac_top = signed_to_skin_facing(top_co, vert_n[plate_v_idx]); sd_un = signed_to_skin(new_co[plate_v_idx])
pen_face = (sd_top < 0) & fac_top; pen_lat = (sd_top < 0) & ~fac_top
P(f"top penetration: facing(真穿出) n={pen_face.sum()} min {sd_top[pen_face].min()*1000 if pen_face.any() else 0:.2f}mm | lateral(股溝側壁) n={pen_lat.sum()} min {sd_top[pen_lat].min()*1000 if pen_lat.any() else 0:.2f}mm")
P(f"top vs skin min {sd_top.min()*1000:.2f}mm (n<0: {(sd_top<0).sum()} at {top_co[np.argmin(sd_top)]}); underside vs skin min {sd_un.min()*1000:.2f} (n<0: {(sd_un<0).sum()}) p50 {np.percentile(sd_un,50)*1000:.2f} p95 {np.percentile(sd_un,95)*1000:.2f} mm")
# 牆腳 vs 手繪線：牆腳點投影到皮膚 → 取 UV → 遮罩值；用鄰近梯度換算 mm
foot_pts = np.array([foot_co_all[vi] + vert_n[vi] * SINK for l in loops for vi in l])
# 以 skin BVH 找面 → 重心 → UV（多邊形用 loop_triangles）
bme.calc_loop_triangles()
tri_v = np.empty(len(bme.loop_triangles) * 3, np.int64); bme.loop_triangles.foreach_get("vertices", tri_v); tri_v = tri_v.reshape(-1, 3)
tri_l = np.empty(len(bme.loop_triangles) * 3, np.int64); bme.loop_triangles.foreach_get("loops", tri_l); tri_l = tri_l.reshape(-1, 3)
tri_poly = np.empty(len(bme.loop_triangles), np.int64); bme.loop_triangles.foreach_get("polygon_index", tri_poly)
tri_bvh = BVHTree.FromPolygons([tuple(p) for p in cage_co], [tuple(int(i) for i in t) for t in tri_v])
def foot_uv(p):
    loc, nor, ti, d = tri_bvh.find_nearest(Vector(p))
    a, b, c = cage_co[tri_v[ti]]
    # barycentric
    v0 = b - a; v1 = c - a; v2 = np.array(loc) - a
    d00 = v0 @ v0; d01 = v0 @ v1; d11 = v1 @ v1; d20 = v2 @ v0; d21 = v2 @ v1
    den = d00 * d11 - d01 * d01
    w1 = (d11 * d20 - d01 * d21) / den; w2 = (d00 * d21 - d01 * d20) / den; w0 = 1 - w1 - w2
    uvs = loop_uv[tri_l[ti]]
    return w0 * uvs[0] + w1 * uvs[1] + w2 * uvs[2], d
fuv = []; fd = []
for p in foot_pts:
    u, d = foot_uv(p); fuv.append(u); fd.append(d)
fuv = np.array(fuv); fd = np.array(fd) * 1000
mv = sample_mask(fuv)
# 遮罩梯度量級：在 UV 上取 ±1px 差分 → 每 mm 的值變化（1px≈1.6mm）
px_mm = 0.617 * W / 4096.0
dpx = 1.0 / W
gx = (sample_mask(fuv + [dpx, 0]) - sample_mask(fuv - [dpx, 0])) / 2
gy = (sample_mask(fuv + [0, dpx]) - sample_mask(fuv - [0, dpx])) / 2
g = np.sqrt(gx ** 2 + gy ** 2) * px_mm   # 值/mm
ok = g > 0.02
dev_mm = np.abs(mv - 0.5) / np.maximum(g, 1e-6)
P(f"foot on skin: dist to skin p50 {np.percentile(fd,50):.2f} max {fd.max():.2f} mm; mask value at foot p50 {np.percentile(mv,50):.3f}; "
  f"|foot - mask 0.5 isoline| (grad-valid n={ok.sum()}/{len(ok)}) p50 {np.percentile(dev_mm[ok],50):.2f} p90 {np.percentile(dev_mm[ok],90):.2f} p99 {np.percentile(dev_mm[ok],99):.2f} mm; "
  f"foot inside-mask frac {(mv>0.5).mean()*100:.1f}%")

# ---------------- 12) 衍生遮罩＝布實際覆蓋（單一來源：布＝禁畫）----------------
# 褌區 cage 面（face_in + 1 圈）的 UV 像素：雙線性反推 → 多面體上的點 → 沿法線射線打到布（頂板/牆）＝覆蓋
cloth_bvh = BVHTree.FromPolygons([tuple(v) for v in V], [tuple(int(i) for i in f) for f in F[:len(top_faces) + 8 * sum(len(l) for l in loops)]])
face_ring = face_in.copy()
vert_in_face = np.zeros(len(cage_co), bool)
for fi in np.where(face_in)[0]: vert_in_face[cage_polys[fi]] = True
for p_ in bme.polygons:
    if vert_in_face[list(p_.vertices)].any(): face_ring[p_.index] = True
derived = (mask_raw * 255).astype(np.uint8).copy()
touched = 0; covered_px = 0
def inv_bilinear_vec(uvq, a, b, c, d):
    """向量化 Newton：解 (s,t) 使 bilerp=uvq；uvq (N,2)"""
    N = len(uvq); st = np.full((N, 2), 0.5)
    for _ in range(10):
        sx = st[:, 0:1]; ty = st[:, 1:2]
        f = (1 - sx) * (1 - ty) * a + sx * (1 - ty) * b + sx * ty * c + (1 - sx) * ty * d - uvq
        dfs = -(1 - ty) * a + (1 - ty) * b + ty * c - ty * d
        dft = -(1 - sx) * a - sx * b + sx * c + (1 - sx) * d
        det = dfs[:, 0] * dft[:, 1] - dft[:, 0] * dfs[:, 1]
        det = np.where(np.abs(det) < 1e-18, 1e-18, det)
        ds = (f[:, 0] * dft[:, 1] - dft[:, 0] * f[:, 1]) / det
        dt = (dfs[:, 0] * f[:, 1] - f[:, 0] * dfs[:, 1]) / det
        st = st - np.stack([ds, dt], 1)
    return st
for fi in np.where(face_ring)[0]:
    poly = bme.polygons[fi]
    uvs = loop_uv[poly.loop_start:poly.loop_start + poly.loop_total]
    vs = cage_polys[fi]
    x0 = int(np.floor(uvs[:, 0].min() * W)); x1 = int(np.ceil(uvs[:, 0].max() * W))
    y0 = int(np.floor(uvs[:, 1].min() * H)); y1 = int(np.ceil(uvs[:, 1].max() * H))
    xs, ys = np.meshgrid(np.arange(max(x0, 0), min(x1 + 1, W)), np.arange(max(y0, 0), min(y1 + 1, H)))
    xs = xs.ravel(); ys = ys.ravel()
    if len(xs) == 0: continue
    uvq = np.stack([(xs + 0.5) / W, (ys + 0.5) / H], 1)
    if len(vs) == 4:
        st = inv_bilinear_vec(uvq, *uvs)
        okm = (st[:, 0] >= -1e-3) & (st[:, 0] <= 1 + 1e-3) & (st[:, 1] >= -1e-3) & (st[:, 1] <= 1 + 1e-3)
        st = np.clip(st, 0, 1); sx = st[:, 0]; ty = st[:, 1]
        wts = np.stack([(1 - sx) * (1 - ty), sx * (1 - ty), sx * ty, (1 - sx) * ty], 1)
    else:
        a_, b_, c_ = uvs[:3]
        den = (b_[1] - c_[1]) * (a_[0] - c_[0]) + (c_[0] - b_[0]) * (a_[1] - c_[1])
        if abs(den) < 1e-18: continue
        w0 = ((b_[1] - c_[1]) * (uvq[:, 0] - c_[0]) + (c_[0] - b_[0]) * (uvq[:, 1] - c_[1])) / den
        w1 = ((c_[1] - a_[1]) * (uvq[:, 0] - c_[0]) + (a_[0] - c_[0]) * (uvq[:, 1] - c_[1])) / den
        w2 = 1 - w0 - w1
        okm = (np.minimum(np.minimum(w0, w1), w2) >= -1e-3)
        wts = np.stack([w0, w1, w2], 1)
    cv = cage_co[vs[:wts.shape[1]]]; cn = cage_vn[vs[:wts.shape[1]]]
    pos = wts @ cv; nn = wts @ cn; nn /= np.maximum(np.linalg.norm(nn, axis=1), 1e-12)[:, None]
    for k in np.where(okm)[0]:
        hit = cloth_bvh.ray_cast(Vector(pos[k] - nn[k] * 0.003), Vector(nn[k]), 0.04)
        cov = hit[0] is not None
        derived[ys[k], xs[k]] = 255 if cov else 0
        touched += 1; covered_px += cov
P(f"derived coverage mask: pixels touched {touched} covered {covered_px}; hand-mask white in same pixels: (see diff)")
# 與手繪差異
diff_px = 0; hand_px = 0
for fi in np.where(face_ring)[0]:
    pass
hand_bin = (mask_raw > 0.5); der_bin = derived > 127
region_px = np.zeros_like(hand_bin)
# region pixels = those we touched → approximate by recomputing bbox union
for fi in np.where(face_ring)[0]:
    poly = bme.polygons[fi]; uvs = loop_uv[poly.loop_start:poly.loop_start + poly.loop_total]
    x0 = int(np.floor(uvs[:, 0].min() * W)); x1 = int(np.ceil(uvs[:, 0].max() * W))
    y0 = int(np.floor(uvs[:, 1].min() * H)); y1 = int(np.ceil(uvs[:, 1].max() * H))
    region_px[max(y0, 0):min(y1 + 1, H), max(x0, 0):min(x1 + 1, W)] = True
xor = (hand_bin != der_bin) & region_px
P(f"derived vs hand (pelvis region): hand white {int((hand_bin&region_px).sum())} px, derived white {int((der_bin&region_px).sum())} px, xor {int(xor.sum())} px ({xor.sum()/max(1,(hand_bin&region_px).sum())*100:.2f}% of hand white; 1px≈1.6mm)")
out_img = bpy.data.images.new("fundoshi_mask_final", W, H, alpha=True)
rgba = np.empty((H, W, 4), np.float32); rgba[..., :3] = (derived / 255.0)[..., None]; rgba[..., 3] = 1.0
out_img.pixels.foreach_set(rgba.ravel())
out_img.filepath_raw = os.path.join(ROOT, "SourceAssets", "fundoshi_mask_final.png"); out_img.file_format = 'PNG'
out_img.save()
P("WROTE SourceAssets/fundoshi_mask_final.png (= 布實際覆蓋 ∪ 手繪其餘區域（元結帶）)")

bpy.ops.wm.save_mainfile(filepath=MASTER)
P("SAVED master")
os.makedirs(os.path.dirname(REPORT), exist_ok=True)
open(REPORT, "w", encoding="utf-8").write("\n".join(rep) + "\nDONE\n")
