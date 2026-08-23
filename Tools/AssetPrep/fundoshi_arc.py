"""褌＝弓形實體 v2（2026-08-23，user 定案）：
「不需要布上緣：以兩邊的接觸線為邊界，形成一個有厚度、側面看來是圓弧狀的褲子實體。」
「接觸線會在新的皮膚上重新取」——v2：裁切域＝**整形後的皮膚本身**（SumoRetopo，同一套 UV0），
等值線直接切在皮膚三角形上＝線天生在皮膚上、UV 精確、零籠零投影（v1 經舊粗籠最近點投影＝位置偏）。

構造：
  邊界＝手繪遮罩（UV0）σ15mm 模糊場的 0.5 等值線（marching triangles on skin）→ 沿環 σ15 平滑後重投影。
  剖面＝測地距 d 與局部帶寬 w：圓弧 h(d)，矢高 s=min(S_MAX, K·w_eff)，w_eff=min(w, 2·W0)（寬區圓弧邊＋平頂）；
        邊界 h→0 並沉 SINK_EDGE。
  實體＝頂面 skin+h·n；底面 skin−UNDER·n；邊界封牆；單網格、水密；無上緣、無牆。
旋鈕（環境變數）：
  ARC_SMAX 中央矢高上限 m（預設 0.008＝布厚）／ARC_K 矢高=K·帶寬／ARC_W0 半寬上限（寬區＝圓弧邊+平頂）
  ARC_WIDEN_MM 遮罩每邊膨脹 mm（預設 0；帶寬 ×1.25 ⇒ 13，**整形腳印必須用同一值重跑
                sumo_skin_band_fair.py，否則接觸線會跑進未整平的衰減帶＝凹凸復發**）
  ARC_SYM=1 遮罩鏡射平均（布左右嚴格對稱；預設 0＝照手繪原樣）
  FUNDOSHI_MASK 遮罩路徑（預設手繪正源；fundoshi_mask_sharp_v2.png＝凹槽位移版，見 fundoshi_mask_warp.py）
  ARC_XFIT 可見交線整形輪數（預設 3＝出貨組態；0＝復原 08-24 前）／ARC_XFIT_SIGMA 弧長平滑 σ
  ARC_XFIT_FALL 鄰圈跟隨衰減長度／ARC_XFIT_CAP 單輪位移上限／ARC_XFIT_RELAX 搬完鬆弛輪數
  ARC_VIS_P90_MAX 可見線契約門檻 mm（預設 0.60）

**2026-08-24 起本腳本的預設組態＝出貨組態**（`ARC_XFIT=3`），直接跑就重現引擎裡那顆布；
08-23 記過的「出貨版 v38 與腳本有五條落差」已作廢——那五條改良實測對**可見線**毫無作用
（純重跑 1.5~5cm 側向 p90 0.684→0.674），此後以 XFIT 版出貨。

**看得見的邊不是接觸線（08-24 定罪，本檔最重要的一句）**：`SINK_EDGE` 把接觸線壓在皮膚下
0.8mm——實測 3706/3706 個布緣頂點 sd 全等 −0.800mm、露出皮膚的 0 個，整片牆一起埋著。
玩家看到的黑／膚交界是 **h=0 的等值線**，位在布緣內側 d\*=1.18~2.44mm，且 d\* 由局部帶寬
決定（corr 0.916）。**改任何「讓邊更直」的東西之前，先確認你動的是 h=0 那條線**，
不是接觸線；量測用 `fundoshi_visible_edge.py`，姿勢穩健性用 `fundoshi_pose_check.py`。
Run: blender --background --python fundoshi_arc.py
"""
import bpy, os, shutil
import numpy as np
from collections import defaultdict, deque
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from mathutils.kdtree import KDTree

ROOT = r"C:\games\Unreal Engine\nice_ink"
MASTER = os.path.join(ROOT, "SourceAssets", "sumo_character_master.blend")
# 遮罩來源＝手繪正源（fundoshi_mask_sharp.png，永不覆蓋）。凹槽位移版 fundoshi_mask_sharp_v2.png
# 經 user 指示回溯，不當預設；要用它以 FUNDOSHI_MASK 指定。
MASK = os.environ.get("FUNDOSHI_MASK", os.path.join(ROOT, "SourceAssets", "fundoshi_mask_sharp.png"))
REPORT = os.path.join(ROOT, "Saved", "fundoshi_arc_report.txt")
CONTOUR_SIGMA = 0.015
TILE_M = 0.208
MIN_ISLAND = 800     # 皮膚 4× 密；帶內小洞/小島（<800 頂點≈<10cm²）一律翻掉，合法環都是數千頂點

rep = []
def P(*a):
    s = " ".join(str(x) for x in a); print(s, flush=True); rep.append(s)

bpy.ops.wm.open_mainfile(filepath=MASTER)
if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')
body = bpy.data.objects["SumoRetopo"]; body_render = body
fund = bpy.data.objects["Fundoshi"]
bme = body.data
assert max(abs(a-b) for ra,rb in zip(body.matrix_world,fund.matrix_world) for a,b in zip(ra,rb)) < 1e-5, "body/fundoshi transform mismatch"

def arr(me):
    co = np.empty(len(me.vertices) * 3); me.vertices.foreach_get("co", co); return co.reshape(-1, 3)

def face_normals_and_vn(co, polys):
    vn = np.zeros_like(co)
    for vs in polys:
        k = len(vs)
        for i in range(k):
            a = co[vs[i]]; b = co[vs[(i + 1) % k]]; c = co[vs[i - 1]]
            e1 = b - a; e2 = c - a; n = np.cross(e1, e2)
            l1 = np.linalg.norm(e1); l2 = np.linalg.norm(e2); ln = np.linalg.norm(n)
            if ln < 1e-18 or l1 < 1e-12 or l2 < 1e-12: continue
            ang = np.arccos(np.clip(np.dot(e1, e2) / (l1 * l2), -1, 1))
            vn[vs[i]] += n / ln * ang
    l = np.linalg.norm(vn, axis=1); l[l == 0] = 1
    return vn / l[:, None]

# ---------------- 0) 皮膚＝裁切域 ----------------
co = arr(bme)
polys = [list(p.vertices) for p in bme.polygons]
U_vn = face_normals_and_vn(co, polys)
dg = bpy.context.evaluated_depsgraph_get()
skin_bvh = BVHTree.FromObject(body_render, dg)
uvl = bme.uv_layers["UVMap"].data
luv = np.empty(len(uvl) * 2); uvl.foreach_get("uv", luv); luv = luv.reshape(-1, 2)
lvi = np.empty(len(bme.loops), np.int64); bme.loops.foreach_get("vertex_index", lvi)
vg_index_name = {g.index: g.name for g in body.vertex_groups}
dv = [[(vg_index_name[g.group], g.weight) for g in v.groups] for v in bme.vertices]
P(f"skin verts={len(co)} faces={len(polys)}")

# ---------------- 1) 遮罩＋模糊場 ----------------
img = bpy.data.images.load(MASK, check_existing=True)
W, H = img.size
px = np.empty(W * H * 4, np.float32); img.pixels.foreach_get(px)
mask = px.reshape(H, W, 4)[:, :, 0].astype(np.float32)
mask_raw = mask.copy()
# 08-23 user「稍微加寬到 1.25×」：遮罩在 UV 做圓盤膨脹（ARC_WIDEN_MM，每邊；帶寬 44mm × 1.25 ⇒ 每邊 5.5mm）
WIDEN_MM = float(os.environ.get("ARC_WIDEN_MM", "0"))
if WIDEN_MM > 0:
    _r = int(round(WIDEN_MM * 0.617 * W / 4096.0))
    _d = mask_raw.copy()
    for dy in range(-_r, _r + 1):
        for dx in range(-_r, _r + 1):
            if dx * dx + dy * dy > _r * _r: continue
            _d = np.maximum(_d, np.roll(np.roll(mask_raw, dy, 0), dx, 1))
    P(f"mask widened by {WIDEN_MM}mm/side ({_r}px): white {(mask_raw>0.5).mean():.4f} -> {(_d>0.5).mean():.4f}")
    mask_raw = _d; mask = _d.copy()
def sample_mask(uv):
    u = np.mod(uv[:, 0], 1.0) * (W - 1); v = np.mod(uv[:, 1], 1.0) * (H - 1)
    x0 = np.floor(u).astype(int); y0 = np.floor(v).astype(int)
    x1 = np.minimum(x0 + 1, W - 1); y1 = np.minimum(y0 + 1, H - 1)
    fx = u - x0; fy = v - y0
    return (mask[y0, x0] * (1 - fx) * (1 - fy) + mask[y0, x1] * fx * (1 - fy)
            + mask[y1, x0] * (1 - fx) * fy + mask[y1, x1] * fx * fy)
PX_PER_MM = 0.617 * W / 4096.0
SIG_PX = CONTOUR_SIGMA * 1000.0 * PX_PER_MM
valid = np.zeros((H, W), np.float32)
for p_ in bme.polygons:
    uvs = luv[p_.loop_start:p_.loop_start + p_.loop_total] * [W, H]
    x0 = max(int(np.floor(uvs[:, 0].min())) - 1, 0); x1 = min(int(np.ceil(uvs[:, 0].max())) + 1, W - 1)
    y0 = max(int(np.floor(uvs[:, 1].min())) - 1, 0); y1 = min(int(np.ceil(uvs[:, 1].max())) + 1, H - 1)
    if x1 < x0 or y1 < y0: continue
    valid[y0:y1 + 1, x0:x1 + 1] = 1.0       # 皮膚 4× 密（面 ~2px）：bbox 填充即夠（+1px 邊距）
def gauss_blur_fft(im, sig):
    ky = np.fft.fftfreq(im.shape[0]); kx = np.fft.rfftfreq(im.shape[1])
    G = np.exp(-2 * (np.pi ** 2) * (sig ** 2) * (ky[:, None] ** 2 + kx[None, :] ** 2))
    return np.fft.irfft2(np.fft.rfft2(im) * G, s=im.shape).astype(np.float32)
num = gauss_blur_fft(mask_raw * valid, SIG_PX); den = gauss_blur_fft(valid, SIG_PX)
mask_blur = np.where(den > 1e-3, num / np.maximum(den, 1e-3), mask_raw)
mask = mask_blur
P(f"cut field: σ={SIG_PX:.1f}px ({CONTOUR_SIGMA*1000:.0f}mm), uv coverage {valid.mean()*100:.1f}%")
loop_uv = luv

# ---------------- 2) 頂點遮罩值＋小島清理 ----------------
mval_loop = sample_mask(luv)
msum = np.zeros(len(co)); mcnt = np.zeros(len(co))
np.add.at(msum, lvi, mval_loop); np.add.at(mcnt, lvi, 1)
mval = msum / np.maximum(mcnt, 1)
# 08-23 user「褲子不是左右對稱」：遮罩在 3D 鏡射平均（自己與 x 鏡射點的最近頂點）＝布左右嚴格對稱
if os.environ.get("ARC_SYM", "0") == "1":
    _kd = KDTree(len(co))
    for _i, _p in enumerate(co): _kd.insert(Vector(_p), _i)
    _kd.balance()
    _mir = np.array([_kd.find(Vector((-_p[0], _p[1], _p[2])))[1] for _p in co])
    mval = 0.5 * (mval + mval[_mir])
    P("mask symmetrized (mirror-average)")
inside = (mval > 0.5) & (co[:, 2] < 1.3)
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
    small_in = [c for c in ci if len(c) < MIN_ISLAND]; small_out = [c for c in co_ if len(c) < MIN_ISLAND]
    for c in small_in: inside[c] = False
    for c in small_out: inside[c] = True
    P(f"components: inside {len(ci)} (flipped {len(small_in)} small), outside {len(co_)} (flipped {len(small_out)} small)")
mval_c = np.where(inside, np.maximum(mval, 0.5 + 1e-4), np.minimum(mval, 0.5 - 1e-4))
face_in = np.array([inside[vs].any() for vs in polys])

# ---------------- 3) marching triangles 裁切（在皮膚上）----------------
tris = []
for vs in polys:
    for i in range(1, len(vs) - 1):
        tris.append((vs[0], vs[i], vs[i + 1]))
tris = np.array(tris)
tris = tris[inside[tris].any(1)]
lin_co = list(co); vert_dv = list(dv); vert_n = list(U_vn)
edge_cut = {}; SNAP_T = 0.15; snapped = {}
def cut_point(a, b):
    key = (a, b) if a < b else (b, a)
    if key in edge_cut: return edge_cut[key]
    ma, mb = mval_c[a], mval_c[b]
    t = float(np.clip((0.5 - ma) / (mb - ma), 0.0, 1.0))
    if t < SNAP_T and a not in snapped:
        snapped[a] = True; lin_co[a] = co[a] * (1 - t) + co[b] * t; edge_cut[key] = a; return a
    if t > 1 - SNAP_T and b not in snapped:
        snapped[b] = True; lin_co[b] = co[a] * (1 - t) + co[b] * t; edge_cut[key] = b; return b
    if a in snapped and t < SNAP_T: edge_cut[key] = a; return a
    if b in snapped and t > 1 - SNAP_T: edge_cut[key] = b; return b
    pos = co[a] * (1 - t) + co[b] * t
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
    ins = [inside[a], inside[b], inside[c]]; k = sum(ins); vs = [a, b, c]
    if k == 3: plate_faces.append((a, b, c)); continue
    if k == 1:
        i = ins.index(True); p = vs[i]; q = vs[(i + 1) % 3]; r = vs[(i + 2) % 3]
        plate_faces.append((p, cut_point(p, q), cut_point(p, r)))
    else:
        i = ins.index(False); p = vs[i]; q = vs[(i + 1) % 3]; r = vs[(i + 2) % 3]
        cq = cut_point(p, q); cr = cut_point(p, r)
        plate_faces.append((q, r, cr)); plate_faces.append((q, cr, cq))
plate_faces = np.array([f for f in plate_faces if len(set(f)) == 3])
lin_co = np.array(lin_co); vert_n = np.array(vert_n)
used = np.unique(plate_faces)
P(f"plate faces {len(plate_faces)} verts used {len(used)} (cut points {len(edge_cut)}, snapped {len(snapped)})")
plate_adj = defaultdict(set)
for f in plate_faces:
    for i in range(3):
        a, b = int(f[i]), int(f[(i + 1) % 3]); plate_adj[a].add(b); plate_adj[b].add(a)

# ---------------- 4) 輪廓迴圈 ----------------
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
assert len(loops) == 3, f"expected 3 loops, got {len(loops)}"

# ======================= 弓形實體 =======================
import heapq
S_MAX = float(os.environ.get("ARC_SMAX", "0.008"))
K_SAG = float(os.environ.get("ARC_K", "0.20"))
W0 = float(os.environ.get("ARC_W0", "0.045"))
UNDER = 0.0015
SINK_EDGE = 0.0008
# 皮膚就是域：位置＝皮膚上的點（切點在皮膚邊上）、法線＝皮膚法線
skin_co = {int(v): lin_co[int(v)].copy() for v in used}
skin_n = {int(v): vert_n[int(v)].copy() for v in used}
P(f"domain = skin itself: {len(used)} verts (no projection)")

# ---- A2) 接觸線沿環 σ15mm 平滑 → 重投影皮膚（切點碎三角的缺口/階梯消除；高度稍後由測地距重算，線可安全移動）----
def reproject(p):
    loc, nor, idx_, dist = skin_bvh.find_nearest(Vector(p))
    nor = np.array(nor); nor /= np.linalg.norm(nor)
    return np.array(loc), nor
for li, l in enumerate(loops):
    c = np.array([skin_co[int(v)] for v in l]); n_ = len(l)
    seg = np.linalg.norm(np.roll(c, -1, 0) - c, axis=1); s_ = np.concatenate([[0], np.cumsum(seg)])[:-1]; L_ = seg.sum()
    sm = np.empty_like(c)
    for i in range(n_):
        d = np.abs(s_ - s_[i]); d = np.minimum(d, L_ - d); wgt = np.exp(-0.5 * (d / 0.015) ** 2); sm[i] = (wgt[:, None] * c).sum(0) / wgt.sum()
    # 位移上限 3mm（>3mm 的搬線會把第 1 圈頂點翻到線的另一側＝折面）
    dmv = sm - c; mvl = np.linalg.norm(dmv, axis=1); scl = np.minimum(1.0, 0.003 / np.maximum(mvl, 1e-9))
    sm = c + dmv * scl[:, None]
    mv = np.linalg.norm(sm - c, axis=1) * 1000
    for i, v in enumerate(l):
        skin_co[int(v)], skin_n[int(v)] = reproject(sm[i])
    P(f"loop {li}: contour σ6 smooth move p50 {np.percentile(mv,50):.2f} p90 {np.percentile(mv,90):.2f} max {mv.max():.2f} mm")
# ---- A3) 內圈 1~3 切向鬆弛（均勻 Laplacian → 重投影皮膚；輪廓固定）：碎三角攤勻 ----
contour_set0 = set(int(v) for l in loops for v in l)
ring0 = {v: 0 for v in contour_set0}; frontier = list(ring0)
for depth in range(1, 4):
    nxt = []
    for v in frontier:
        for u in plate_adj[v]:
            if u not in ring0: ring0[u] = depth; nxt.append(u)
    frontier = nxt
relax = [v for v, d_ in ring0.items() if 1 <= d_ <= 3]
for _it in range(20):
    newp = {}
    for v in relax:
        nb = list(plate_adj[v]); m_ = np.mean([skin_co[u] for u in nb], axis=0)
        newp[v] = skin_co[v] + 0.5 * (m_ - skin_co[v])
    for v, p_ in newp.items():
        skin_co[v], skin_n[v] = reproject(p_)
P(f"relaxed {len(relax)} ring1-3 verts")

# ---- A4) 抬升法線場平滑（08-23）：凹槽（腰窩/屁溝）兩側法線對沖，沿原法線抬 7mm 會交叉成折痕；
#         布的抬升方向用鄰域平均 ×15 的平滑法線＝布面跨橋不折（接觸線仍用原法線沉入）
skin_n_raw = {v: n_.copy() for v, n_ in skin_n.items()}
def build_lift_normals():
    ln = dict(skin_n)
    for _it in range(15):
        nn = {}
        for v in ln:
            nb = plate_adj[v]
            acc_ = ln[v] * 0.5 + sum((ln[u] for u in nb), np.zeros(3)) / max(len(nb), 1) * 0.5
            nn[v] = acc_ / max(np.linalg.norm(acc_), 1e-9)
        ln = nn
    return ln
lift_n = build_lift_normals()
P("lift normals smoothed x15")

# ---- B/C/D) 場：測地距 → 帶寬 → 弓形高度（抽成函式；X 段的迭代要重算）----
contour_set = set(int(v) for l in loops for v in l)
def arc_h(d, w):
    we = min(w, 2 * W0); s = min(S_MAX, K_SAG * we)
    if s < 1e-5: return 0.0
    R = (we * we / 4 + s * s) / (2 * s)
    x = we / 2 - d
    if x <= 0: return s
    return float(np.sqrt(max(R * R - x * x, 0.0)) - (R - s))

def compute_fields(verbose=True):
    # B) 測地距到邊界（Dijkstra，邊長＝現行皮膚位置）＋最近邊界頂點
    dist_b = {int(v): np.inf for v in used}; nearest_c = {}
    pq = []
    for c in contour_set:
        dist_b[c] = 0.0; nearest_c[c] = c; heapq.heappush(pq, (0.0, c))
    while pq:
        d0, u = heapq.heappop(pq)
        if d0 > dist_b[u]: continue
        for w_ in plate_adj[u]:
            nd = d0 + float(np.linalg.norm(skin_co[w_] - skin_co[u]))
            if nd < dist_b[w_]:
                dist_b[w_] = nd; nearest_c[w_] = nearest_c[u]; heapq.heappush(pq, (nd, w_))
    # C) 帶寬：每個邊界頂點→對面邊界（他環、或同環弧距 >15cm 的最近點），沿環 σ20mm 平滑
    ct_pts = []; ct_meta = []
    for li, l in enumerate(loops):
        c = np.array([skin_co[int(v)] for v in l])
        seg = np.linalg.norm(np.roll(c, -1, 0) - c, axis=1); s_ = np.concatenate([[0], np.cumsum(seg)])[:-1]
        for j, v in enumerate(l): ct_pts.append(skin_co[int(v)]); ct_meta.append((li, s_[j], float(seg.sum())))
    kd_c = KDTree(len(ct_pts))
    for i, p in enumerate(ct_pts): kd_c.insert(Vector(p), i)
    kd_c.balance()
    width_c = {}
    k = 0
    for li, l in enumerate(loops):
        for j, v in enumerate(l):
            li0, s0, L0 = ct_meta[k]; wv = None
            for (loc, idx2, dd) in kd_c.find_n(Vector(ct_pts[k]), 400):
                lj, sj, Lj = ct_meta[idx2]
                if lj != li0: wv = dd; break
                da = abs(sj - s0); da = min(da, L0 - da)
                if da > 0.15: wv = dd; break
            width_c[int(v)] = wv if wv is not None else 2 * W0
            k += 1
    for li, l in enumerate(loops):
        n_ = len(l); c = np.array([skin_co[int(v)] for v in l])
        seg = np.linalg.norm(np.roll(c, -1, 0) - c, axis=1); s_ = np.concatenate([[0], np.cumsum(seg)])[:-1]; L_ = seg.sum()
        wraw = np.array([width_c[int(v)] for v in l]); wsm = np.empty(n_)
        for i in range(n_):
            d = np.abs(s_ - s_[i]); d = np.minimum(d, L_ - d); wgt = np.exp(-0.5 * (d / 0.02) ** 2); wsm[i] = (wgt * wraw).sum() / wgt.sum()
        for i, v in enumerate(l): width_c[int(v)] = float(wsm[i])
        if verbose:
            P(f"loop {li}: width p5 {np.percentile(wsm,5)*1000:.1f} p50 {np.percentile(wsm,50)*1000:.1f} p95 {np.percentile(wsm,95)*1000:.1f} mm")
    # D) 弓形高度
    hmap = {}
    for v in used:
        v = int(v)
        hmap[v] = arc_h(dist_b[v], width_c[nearest_c[v]]) - SINK_EDGE
    if verbose:
        hs = np.array([hmap[int(v)] for v in used]) * 1000
        P(f"arc height: p5 {np.percentile(hs,5):.2f} p50 {np.percentile(hs,50):.2f} p95 {np.percentile(hs,95):.2f} max {hs.max():.2f} mm (edge = -{SINK_EDGE*1000:.1f})")
    return dist_b, nearest_c, width_c, hmap

dist_b, nearest_c, width_c, hmap = compute_fields()

# ======================= X) 可見交線整形（2026-08-24）=======================
# 定罪：布緣（接觸線）被 SINK_EDGE 壓在皮膚下 0.8mm——實測 3706/3706 個布緣頂點
# sd 全等於 −0.800mm、露出皮膚的 0 個。**玩家看到的黑／膚交界不是它**，而是
# 「布頂面穿出皮膚」的等值線 h=0，位在布緣內側 d*=1.18~2.44mm，且 d* 由局部帶寬
# 決定（corr 0.916：帶寬 35mm→d* 1.26mm，帶寬 >90mm→d* 2.41mm）。
# 此前管線對可見線零處理：沒被切出來、沒平滑、沒閘門；所有 σ15mm 平滑與 3mm 上限
# 都作用在看不見的布緣上（而 log 顯示 p90 搬動需求＝3.00mm＝整條頂在上限，
# 也就是布緣連自己那道平滑都沒真的吃到）。
# 本段＝把專案對布緣的那套配方（弧長 σ 平滑 → 位移上限 → 重投影皮膚 → 鄰圈跟隨）
# 原封不動施加在**可見線**上，布緣降級為致動器（它看不見，只需保持被埋住）。
# 迭代：移布緣 → 場重算 → 可見線重抽，2~3 輪收斂。
XFIT_ITERS = int(os.environ.get("ARC_XFIT", "3"))   # 預設＝出貨組態；設 0 復原 08-24 前行為
XFIT_SIGMA = float(os.environ.get("ARC_XFIT_SIGMA", "0.015"))   # 與布緣同一個 σ15mm
XFIT_FALL = float(os.environ.get("ARC_XFIT_FALL", "0.008"))     # 鄰圈跟隨衰減長度
XFIT_CAP = float(os.environ.get("ARC_XFIT_CAP", "0.008"))       # 單輪位移上限
XFIT_RELAX = int(os.environ.get("ARC_XFIT_RELAX", "6"))          # 搬完的鄰圈鬆弛輪數

def extract_visible_loops(hmap_, min_len=50):
    """h=0 等值線＝布頂面穿出皮膚處。h=0 時頂點恰在皮膚上，故直接用 skin_co 內插。"""
    nodes = []; node_of = {}; links = []
    def nd(a, b):
        key = (a, b) if a < b else (b, a)
        if key in node_of: return node_of[key]
        ha, hb = hmap_[a], hmap_[b]
        t = ha / (ha - hb)
        node_of[key] = len(nodes); nodes.append(skin_co[a] * (1 - t) + skin_co[b] * t)
        return node_of[key]
    for f in plate_faces:
        a, b, c = int(f[0]), int(f[1]), int(f[2])
        fl = [hmap_[a] >= 0, hmap_[b] >= 0, hmap_[c] >= 0]; k = sum(fl)
        if k == 0 or k == 3: continue
        vs = [a, b, c]; i = fl.index(True) if k == 1 else fl.index(False)
        p_, q_, r_ = vs[i], vs[(i + 1) % 3], vs[(i + 2) % 3]
        links.append((nd(p_, q_), nd(p_, r_)))
    adjx = defaultdict(list)
    for a, b in links: adjx[a].append(b); adjx[b].append(a)
    seen = set(); out = []
    for s0 in list(adjx):
        if s0 in seen or len(adjx[s0]) != 2: continue
        loop = [s0]; seen.add(s0); prev, cur = None, s0
        while True:
            nb = [x for x in adjx[cur] if x != prev and x not in seen]
            if not nb: break
            nxt = nb[0]; loop.append(nxt); seen.add(nxt); prev, cur = cur, nxt
        if len(loop) >= min_len: out.append(np.array([nodes[i] for i in loop]))
    return out

def _resample_1mm(c0):
    cc = np.vstack([c0, c0[:1]])
    seg = np.linalg.norm(np.diff(cc, axis=0), axis=1); s_ = np.concatenate([[0], np.cumsum(seg)])
    L = float(s_[-1]); n = max(int(round(L / 0.001)), 32)
    q = np.linspace(0, L, n, endpoint=False)
    return np.stack([np.interp(q, s_, cc[:, k]) for k in range(3)], 1), L

def _lp_closed(c, L, sig):
    n = len(c); kf = np.fft.rfftfreq(n, d=L / n)
    G = np.exp(-2 * (np.pi ** 2) * (sig ** 2) * (kf ** 2))
    return np.fft.irfft(np.fft.rfft(c, axis=0) * G[:, None], n=n, axis=0)

def visible_gate(hmap_, tag):
    """可見線的驗收量：1.5~5cm 帶通的側向偏差（＝user 肉眼抱怨的那個尺度與方向）。"""
    xl = extract_visible_loops(hmap_)
    if not xl:
        P(f"  [{tag}] 抽不到可見線"); return None
    accB = []; accN = []; tot = 0.0
    for c0 in xl:
        c, L = _resample_1mm(c0); tot += L
        base = _lp_closed(c, L, 0.015)
        T = np.roll(base, -1, 0) - np.roll(base, 1, 0); T /= np.maximum(np.linalg.norm(T, axis=1), 1e-12)[:, None]
        N = np.array([reproject(p)[1] for p in base])
        N -= np.einsum('ij,ij->i', N, T)[:, None] * T
        N /= np.maximum(np.linalg.norm(N, axis=1), 1e-12)[:, None]
        B = np.cross(T, N)
        D = _lp_closed(c, L, 0.004) - base
        accB.append(np.abs(np.einsum('ij,ij->i', D, B)) * 1000)
        accN.append(np.abs(np.einsum('ij,ij->i', D, N)) * 1000)
    b = np.concatenate(accB); n_ = np.concatenate(accN)
    P(f"  [{tag}] 可見線 {len(xl)} 環 {tot*100:.1f}cm ; 1.5~5cm 側向 p50 {np.percentile(b,50):.3f} "
      f"p90 {np.percentile(b,90):.3f} p99 {np.percentile(b,99):.3f} | 法向 p90 {np.percentile(n_,90):.3f} mm")
    return float(np.percentile(b, 90))

if XFIT_ITERS > 0:
    P(f"X) 可見交線整形：{XFIT_ITERS} 輪，σ{XFIT_SIGMA*1000:.0f}mm 衰減{XFIT_FALL*1000:.0f}mm 上限{XFIT_CAP*1000:.0f}mm")
    gate0 = visible_gate(hmap, "before")
    for _it in range(XFIT_ITERS):
        xl = extract_visible_loops(hmap)
        pts = []; dls = []
        for c0 in xl:
            c, L = _resample_1mm(c0)
            d = _lp_closed(c, L, XFIT_SIGMA) - c
            m_ = np.linalg.norm(d, axis=1); sc = np.minimum(1.0, XFIT_CAP / np.maximum(m_, 1e-12))
            pts.append(c); dls.append(d * sc[:, None])
        pts = np.vstack(pts); dls = np.vstack(dls)
        kdx = KDTree(len(pts))
        for i, p in enumerate(pts): kdx.insert(Vector(p), i)
        kdx.balance()
        # 位移先落到布緣、沿環 σ4mm 平滑，再散到內圈。
        # 08-24 血價：讓每個頂點各自去找最近的可見線取樣點，對應關係在帶子變窄處會跳，
        # 相鄰布緣頂點拿到不連續的位移 ⇒ 細長三角形 ⇒ ring0 二面角劣化 ⇒ **可見線上的
        # 著色法線 p99 +41%、max ×3.8**（形狀變好但著色尾巴變糟）。位移場本身必須先平滑。
        delta_c = {}
        for l in loops:
            raw = np.empty((len(l), 3))
            for j, v in enumerate(l):
                loc, i, dd = kdx.find(Vector(skin_co[int(v)])); raw[j] = dls[i]
            c = np.array([skin_co[int(v)] for v in l])
            seg = np.linalg.norm(np.roll(c, -1, 0) - c, axis=1)
            s_ = np.concatenate([[0], np.cumsum(seg)])[:-1]; L_ = seg.sum()
            for j, v in enumerate(l):
                d = np.abs(s_ - s_[j]); d = np.minimum(d, L_ - d)
                wg = np.exp(-0.5 * (d / 0.004) ** 2)
                delta_c[int(v)] = (wg[:, None] * raw).sum(0) / wg.sum()
        moved = []
        for v in used:
            v = int(v)
            db = dist_b[v]
            if db > XFIT_FALL: continue
            t_ = db / XFIT_FALL; wgt = 1.0 - (t_ * t_ * (3 - 2 * t_))
            dv = delta_c[nearest_c[v]] * wgt      # 測地對應（Dijkstra 已算），不是 KD 最近點
            skin_co[v], skin_n[v] = reproject(skin_co[v] + dv)
            moved.append(np.linalg.norm(dv) * 1000)
        # 鄰圈鬆弛（同 A3 配方，輪廓固定）：搬完把三角形攤回去
        for _r in range(XFIT_RELAX):
            newp = {}
            for v in relax:
                nb = list(plate_adj[v]); m2 = np.mean([skin_co[u] for u in nb], axis=0)
                newp[v] = skin_co[v] + 0.5 * (m2 - skin_co[v])
            for v, p_ in newp.items(): skin_co[v], skin_n[v] = reproject(p_)
        mvv = np.array(moved) if moved else np.zeros(1)
        P(f"  round {_it+1}: 搬動 {len(moved)} 頂點 p50 {np.percentile(mvv,50):.3f} p90 {np.percentile(mvv,90):.3f} max {mvv.max():.3f} mm")
        dist_b, nearest_c, width_c, hmap = compute_fields(verbose=False)
        visible_gate(hmap, f"after r{_it+1}")
    lift_n = build_lift_normals()          # 皮膚位置動過，抬升法線場要跟著重算
    hs = np.array([hmap[int(v)] for v in used]) * 1000
    P(f"X) 完成；arc height p50 {np.percentile(hs,50):.2f} max {hs.max():.2f} mm")

# ---- E) 組裝：頂面 + 底面 + 邊界封牆 ----
plate_v_idx = used
remap = {int(v): i for i, v in enumerate(plate_v_idx)}
nV = len(plate_v_idx)
top_co = np.array([skin_co[int(v)] + hmap[int(v)] * (lift_n[int(v)] if hmap[int(v)] > 0 else skin_n[int(v)]) for v in plate_v_idx])
# 頂面輕拋光（08-23）：接觸線局部小折角→高度場小脊＝帶面小凹痕；Taubin ×20、只動離輪廓 ≥2 圈、cap 2mm、不低於皮膚+0.5mm
_ring = {int(v): 0 for v in contour_set}; _fr = list(_ring)
for _d in range(1, 3):
    _nx = []
    for _v in _fr:
        for _u in plate_adj[_v]:
            if _u not in _ring: _ring[_u] = _d; _nx.append(_u)
    _fr = _nx
_pol = [i for i, v in enumerate(plate_v_idx) if int(v) not in _ring]
_nb = {i: [remap[u] for u in plate_adj[int(plate_v_idx[i])]] for i in _pol}
_p0 = top_co.copy(); _x = top_co.copy()
for _it in range(20):
    for _lam in (0.5, -0.53):
        _d2 = {}
        for i in _pol:
            nb = _nb[i]
            if len(nb) < 3: continue
            _d2[i] = _lam * (np.mean(_x[nb], axis=0) - _x[i])
        for i, dv in _d2.items(): _x[i] = _x[i] + dv
for i in _pol:
    dv = _x[i] - _p0[i]; m_ = np.linalg.norm(dv)
    if m_ > 0.002: dv *= 0.002 / m_
    q = _p0[i] + dv
    loc, nor, idx_, dist = skin_bvh.find_nearest(Vector(q))
    sd_ = np.dot(q - np.array(loc), np.array(nor))
    if sd_ < 0.0005: q = q + (0.0005 - sd_) * np.array(nor)
    top_co[i] = q
_pm = np.array([np.linalg.norm(top_co[i] - _p0[i]) for i in _pol]) * 1000
P(f"top polish: {len(_pol)} verts |d| p50 {np.percentile(_pm,50):.2f} p90 {np.percentile(_pm,90):.2f} max {_pm.max():.2f} mm")
und_co = np.array([skin_co[int(v)] - UNDER * skin_n[int(v)] for v in plate_v_idx])
top_faces = np.vectorize(remap.get)(plate_faces)
F = []; flipped = 0
for f in top_faces:
    a, b, c = f
    nrm_ = np.cross(top_co[b] - top_co[a], top_co[c] - top_co[a])
    nn = skin_n[int(plate_v_idx[a])] + skin_n[int(plate_v_idx[b])] + skin_n[int(plate_v_idx[c])]
    if np.dot(nrm_, nn) < 0: f = (a, c, b); flipped += 1
    F.append(tuple(int(x) for x in f))
top_faces = np.array(F); n_top_f = len(top_faces)
V = np.vstack([top_co, und_co])
faces = [tuple(f) for f in top_faces]
wall_faces = []
for l in loops:
    for i in range(len(l)):
        a = remap[int(l[i])]; b = remap[int(l[(i + 1) % len(l)])]
        wall_faces.append((a, b, nV + b)); wall_faces.append((a, nV + b, nV + a))
third = {}
for f in top_faces:
    for i in range(3):
        a, b, c = f[i], f[(i + 1) % 3], f[(i + 2) % 3]
        third[(a, b)] = c; third[(b, a)] = c
wf2 = []; wflip = 0
for (p, q, r) in wall_faces:
    a, b = (p, q) if q < nV else (p, r - nV)
    c = third.get((a, b), third.get((b, a)))
    nrm_ = np.cross(V[q] - V[p], V[r] - V[p])
    out_dir = (V[a] + V[b]) / 2 - V[c] if c is not None else nrm_
    if np.dot(nrm_, out_dir) < 0: (p, q, r) = (p, r, q); wflip += 1
    wf2.append((p, q, r))
n_wall_f = len(wf2); faces += wf2
under_faces = [(nV + f[0], nV + f[2], nV + f[1]) for f in top_faces]
faces += under_faces
P(f"assembled verts {len(V)} faces {len(faces)} (top {n_top_f} wall {n_wall_f} under {len(under_faces)}); top flipped {flipped}, wall flipped {wflip}")

# ---- F) 圓柱 UV ----
cx, cy = float(V[:, 0].mean()), float(V[:, 1].mean())
theta = np.arctan2(V[:, 1] - cy, V[:, 0] - cx)
Ravg = float(np.sqrt((V[:, 0] - cx) ** 2 + (V[:, 1] - cy) ** 2).mean())
Mrep = max(1, round(2 * np.pi * Ravg / TILE_M))
UVv = np.stack([(theta + np.pi) / (2 * np.pi) * (Mrep / 12.0), V[:, 2] / (12.0 * TILE_M)], 1)

# ---- G) 建 mesh、分區法線、權重、替換 ----
me2 = bpy.data.meshes.new("FundoshiArc")
me2.from_pydata([tuple(v) for v in V], [], [tuple(int(i) for i in f) for f in faces])
me2.update(); me2.calc_loop_triangles()
uvlayer = me2.uv_layers.new(name="UVMap")
lvi2 = np.empty(len(me2.loops), np.int64); me2.loops.foreach_get("vertex_index", lvi2)
lo_uv = UVv[lvi2].copy(); per = Mrep / 12.0
for p in me2.polygons:
    ls = list(range(p.loop_start, p.loop_start + p.loop_total)); us = lo_uv[ls, 0]
    if us.max() - us.min() > per * 0.5: lo_uv[ls, 0] = np.where(us < per * 0.5, us + per, us)
uvlayer.data.foreach_set("uv", lo_uv.ravel())
ca = me2.color_attributes.new("FaceMask", 'BYTE_COLOR', 'CORNER')
ca.data.foreach_set("color", np.tile(np.array([0, 0, 0, 1.0], np.float32), len(me2.loops)))
for p in me2.polygons: p.use_smooth = True
def face_region(fi):
    if fi < n_top_f: return 0
    if fi < n_top_f + n_wall_f: return 1
    return 2
fnorm_all = np.empty(len(me2.polygons) * 3); me2.polygons.foreach_get("normal", fnorm_all); fnorm_all = fnorm_all.reshape(-1, 3)
farea_all = np.empty(len(me2.polygons)); me2.polygons.foreach_get("area", farea_all)
acc = defaultdict(lambda: np.zeros(3))
for poly in me2.polygons:
    r = face_region(poly.index); w_ = max(farea_all[poly.index], 1e-12)
    for v in poly.vertices: acc[(v, r)] += fnorm_all[poly.index] * w_
loop_normals = np.empty((len(me2.loops), 3))
for poly in me2.polygons:
    r = face_region(poly.index)
    for lo in range(poly.loop_start, poly.loop_start + poly.loop_total):
        nv = acc[(int(lvi2[lo]), r)]; ln = np.linalg.norm(nv)
        loop_normals[lo] = nv / ln if ln > 1e-12 else fnorm_all[poly.index]
me2.normals_split_custom_set([tuple(x) for x in loop_normals])
me2.materials.append(bpy.data.materials["M_Fundoshi"])
DV = [vert_dv[int(v)] for v in plate_v_idx] * 2
vg_names = [vg.name for vg in fund.vertex_groups]
old = fund.data; fund.data = me2; bpy.data.meshes.remove(old)
if len(fund.vertex_groups) != len(vg_names):
    fund.vertex_groups.clear()
    for nm in vg_names: fund.vertex_groups.new(name=nm)
for i, wd in enumerate(DV):
    tot = sum(w for g, w in wd) or 1.0
    for g, w in wd:
        if w > 1e-4: fund.vertex_groups[g].add([i], w / tot, 'REPLACE')
unweighted = sum(1 for v in me2.vertices if not v.groups)
P(f"unweighted={unweighted}"); assert unweighted == 0
assert any(m.type == 'ARMATURE' for m in fund.modifiers)

# ---- H) 出貨閘 ----
ecnt_all = defaultdict(int)
for p_ in me2.polygons:
    for ek in p_.edge_keys: ecnt_all[tuple(sorted(ek))] += 1
open_edges = sum(1 for c in ecnt_all.values() if c == 1)
P(f"open edges (must be 0) = {open_edges}"); assert open_edges == 0
sd = []
for i, v in enumerate(plate_v_idx):
    loc, nor, idx_, dist = skin_bvh.find_nearest(Vector(top_co[i]))
    sd.append(np.dot(top_co[i] - np.array(loc), np.array(nor)) * 1000)
sd = np.array(sd); interior = np.array([int(v) not in contour_set for v in plate_v_idx])
P(f"top vs skin: interior min {sd[interior].min():.2f} p5 {np.percentile(sd[interior],5):.2f} mm (n<0: {(sd[interior] < 0).sum()}); contour p50 {np.percentile(sd[~interior],50):.2f} mm")
ring = {int(v): 0 for v in contour_set}; frontier = list(ring)
for depth in range(1, 4):
    nxt = []
    for v in frontier:
        for u in plate_adj[v]:
            if u not in ring: ring[u] = depth; nxt.append(u)
    frontier = nxt
ef = defaultdict(list)
for fi, f in enumerate(top_faces):
    for i in range(3):
        a, b = f[i], f[(i + 1) % 3]; ef[(min(a, b), max(a, b))].append(fi)
fn_top = fnorm_all[:n_top_f]
_ring_f = [min(ring.get(int(plate_v_idx[k]), 99) for k in f) for f in top_faces]
_by = defaultdict(list)
for fs in ef.values():
    if len(fs) != 2: continue
    a, b = fs; _by[min(_ring_f[a], _ring_f[b], 3)].append(np.degrees(np.arccos(np.clip(np.dot(fn_top[a], fn_top[b]), -1, 1))))
for r_ in sorted(_by):
    d_ = np.array(_by[r_]); tag_ = "+" if r_ == 3 else ""
    P(f"GATE ring{r_}{tag_} dihedral p50 {np.percentile(d_,50):.2f} p90 {np.percentile(d_,90):.2f} p99 {np.percentile(d_,99):.2f} n>15deg {int((d_>15).sum())}")
# 可見線＝玩家真正看到的那條（獨立儀器＝fundoshi_visible_edge.py）。
# 這是**真契約不是印出來就算**：08-18~24 的血價一半來自「閘門量不到 user 看得到的量」。
# 門檻＝回歸護欄（達成值 0.512mm ＋ 餘裕），不是設計目標；改構造導致它上升必須是自覺的決定。
_vis_p90 = visible_gate(hmap, "GATE visible")
VIS_P90_MAX = float(os.environ.get("ARC_VIS_P90_MAX", "0.60"))
assert _vis_p90 is not None, "可見線抽不出來＝布沒有露出皮膚，構造壞了"
assert _vis_p90 <= VIS_P90_MAX, (
    f"可見線回歸：1.5~5cm 側向 p90 {_vis_p90:.3f}mm > 門檻 {VIS_P90_MAX}mm"
    "（出貨前基準 0.674；要放寬請明示 ARC_VIS_P90_MAX）")
P(f"FINAL verts={len(me2.vertices)} tris={len(me2.polygons)}")

# ---- I) 衍生遮罩＝布覆蓋（接觸線內＝模糊場 ≥0.5）；骨盆區以外（元結帶）＝手繪原樣 ----
region_px = np.zeros((H, W), bool)
face_ring = face_in.copy()
vert_in_face = np.zeros(len(co), bool)
for fi in np.where(face_in)[0]: vert_in_face[polys[fi]] = True
for p_ in bme.polygons:
    if vert_in_face[list(p_.vertices)].any(): face_ring[p_.index] = True
for fi in np.where(face_ring)[0]:
    poly = bme.polygons[fi]; uvs = loop_uv[poly.loop_start:poly.loop_start + poly.loop_total]
    x0 = int(np.floor(uvs[:, 0].min() * W)); x1 = int(np.ceil(uvs[:, 0].max() * W))
    y0 = int(np.floor(uvs[:, 1].min() * H)); y1 = int(np.ceil(uvs[:, 1].max() * H))
    region_px[max(y0, 0):min(y1 + 1, H), max(x0, 0):min(x1 + 1, W)] = True
derived = np.where(region_px, (mask_blur > 0.5).astype(np.uint8) * 255, (mask_raw * 255).astype(np.uint8))
hand_bin = mask_raw > 0.5; der_bin = derived > 127; xor = (hand_bin != der_bin) & region_px
P(f"derived vs hand (pelvis region): xor {int(xor.sum())} px ({xor.sum()/max(1,(hand_bin&region_px).sum())*100:.2f}% of hand white)")
out_img = bpy.data.images.new("fundoshi_mask_final", W, H, alpha=True)
rgba = np.empty((H, W, 4), np.float32); rgba[..., :3] = (derived / 255.0)[..., None]; rgba[..., 3] = 1.0
out_img.pixels.foreach_set(rgba.ravel())
out_img.filepath_raw = os.path.join(ROOT, "SourceAssets", "fundoshi_mask_final.png"); out_img.file_format = 'PNG'; out_img.save()
P("WROTE SourceAssets/fundoshi_mask_final.png")
bpy.ops.wm.save_mainfile(filepath=MASTER); P("SAVED master")
os.makedirs(os.path.dirname(REPORT), exist_ok=True)
open(REPORT, "w", encoding="utf-8").write("\n".join(rep) + "\nDONE\n")
