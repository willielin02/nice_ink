# 褲子周圍皮膚「普查＋整平」（2026-08-21 user 定案）：
# 局部膜（零收縮 Taubin 只跑布 8cm 帶、邊界羽化錨定）＝理想光滑版；
# 普查＝逐頂點量「實際皮膚−膜」的深度與正負、連通聚類造冊；
# 修＝往膜靠攏，但**聚類峰深 ≤3mm 全拉平、3~6mm 羽化、>6mm（設計特徵）整簇不動**；
# 臀縫/大腿縫由同面判準（法線 dot>0.3 才相鄰）構造性保留。
# 帶外（w_region==0）逐位不動（斷言）。膜只當尺不上場。
# Run: FLATTEN_WRITE=0 普查only（不存檔）；=1 套用＋存 master＋重存 cage_smooth_v3
import bpy
import os
import shutil
import numpy as np
from collections import defaultdict, deque
from mathutils import Vector
from mathutils.kdtree import KDTree

ROOT = r"C:\games\Unreal Engine\nice_ink"
MASTER = os.path.join(ROOT, "SourceAssets", "sumo_character_master.blend")
BK = os.path.join(ROOT, "SourceAssets", "masters", "sumo_character_master_v27_preflatten.blend")
CAGE_SMOOTH = os.path.join(ROOT, "SourceAssets", "masters", "cage_smooth_v3.blend")
WRITE = os.environ.get("FLATTEN_WRITE", "0") == "1"

REGION_FULL = 0.080   # 離布 8cm 內＝全權重
REGION_ZERO = 0.100   # 10cm 歸零（羽化）
TAUBIN_PAIRS = 200
FLAG_D = 0.0015       # 普查登記門檻 1.5mm
KEEP_LO = 0.003       # 峰深 ≤3mm ＝噪聲全拉平
KEEP_HI = 0.006       # 峰深 ≥6mm ＝設計特徵整簇不動
CAP = 0.004           # 單點位移硬上限（保險）

def P(*a):
    print(*a, flush=True)

bpy.ops.wm.open_mainfile(filepath=MASTER)
if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')
if WRITE and not os.path.exists(BK):
    shutil.copy2(MASTER, BK)
    P("backup ->", BK)

body = bpy.data.objects["SumoRetopo"]
fund = bpy.data.objects["Fundoshi"]
me = body.data
N = len(me.vertices)
co0 = np.array([v.co[:] for v in me.vertices])
P(f"body verts={N} tris={sum(len(p.vertices)-2 for p in me.polygons)}")

# ---- 區域權重（離布距離）----
fkd = KDTree(len(fund.data.vertices))
for _v in fund.data.vertices:
    fkd.insert(_v.co, _v.index)
fkd.balance()
cd = np.array([fkd.find(Vector(c))[2] for c in co0])
w_region = np.clip((REGION_ZERO - cd) / (REGION_ZERO - REGION_FULL), 0.0, 1.0)
w_region = w_region * w_region * (3 - 2 * w_region)
in_reg = w_region > 0
P(f"region verts (w>0): {in_reg.sum()}  full-weight: {(w_region >= 0.999).sum()}")

# ---- 頂點法線＋同面相鄰表（臀縫兩壁不互抹）----
vn = np.zeros((N, 3))
for poly in me.polygons:
    n_ = np.array(poly.normal)
    for vi_ in poly.vertices:
        vn[vi_] += n_
l_ = np.linalg.norm(vn, axis=1)
l_[l_ == 0] = 1
vn = vn / l_[:, None]
pr = []
for e in me.edges:
    a, b = e.vertices
    if np.dot(vn[a], vn[b]) > 0.3:
        pr.append((a, b))
        pr.append((b, a))
pr = np.array(pr, dtype=np.int64)
dst, src = pr[:, 0], pr[:, 1]
deg = np.zeros(N)
np.add.at(deg, dst, 1.0)
P(f"same-side edges: {len(pr)//2}  deg==0: {(deg == 0).sum()}")

# ---- 膜：零收縮 Taubin（只動區域、邊界羽化錨定）----
x = co0.copy()
for _it in range(TAUBIN_PAIRS):
    for lam in (0.5, -0.53):
        s = np.zeros_like(x)
        np.add.at(s, dst, x[src])
        avg = s / np.maximum(deg, 1.0)[:, None]
        dx = avg - x
        dx[deg == 0] = 0.0
        x = x + lam * (w_region[:, None] * dx)
mem = x

# ---- 普查：深度、正負、聚類 ----
rvec = co0 - mem
depth = np.linalg.norm(rvec, axis=1)
sgn = np.einsum('ij,ij->i', rvec, vn)      # + 凸 / − 凹
flag = (depth > FLAG_D) & in_reg
adj = defaultdict(list)
for a, b in pr[::2]:
    adj[int(a)].append(int(b))
    adj[int(b)].append(int(a))
cluster = np.full(N, -1, dtype=np.int64)
clusters = []
for seed in np.where(flag)[0]:
    if cluster[seed] >= 0:
        continue
    cid = len(clusters)
    q = deque([int(seed)])
    cluster[seed] = cid
    members = [int(seed)]
    while q:
        u = q.popleft()
        for v2 in adj[u]:
            if flag[v2] and cluster[v2] < 0:
                cluster[v2] = cid
                q.append(v2)
                members.append(v2)
    clusters.append(members)
P(f"\n== 普查 ==  region 深度 p50 {np.percentile(depth[in_reg],50)*1000:.2f} p90 {np.percentile(depth[in_reg],90)*1000:.2f} p99 {np.percentile(depth[in_reg],99)*1000:.2f} max {depth[in_reg].max()*1000:.2f} mm")
P(f"登記聚類（|深|>{FLAG_D*1000:.1f}mm）: {len(clusters)} 簇 / {int(flag.sum())} 頂點")
rows = []
for cid, mm_ in enumerate(clusters):
    mm_ = np.array(mm_)
    pk = depth[mm_].max()
    mean_sgn = np.sign(sgn[mm_][np.argmax(depth[mm_])])
    cen = co0[mm_].mean(0)
    rows.append((pk, cid, len(mm_), cen, mean_sgn, cd[mm_].min()))
rows.sort(reverse=True)
for pk, cid, nv, cen, ms, dmin in rows[:25]:
    kind = "凸" if ms > 0 else "凹"
    P(f"  #{cid:3d} 峰 {pk*1000:5.2f}mm {kind} n={nv:5d} 心=({cen[0]:+.3f},{cen[1]:+.3f},{cen[2]:+.3f}) 離布 {dmin*1000:5.1f}mm")

if not WRITE:
    P("\nCENSUS ONLY — nothing written")
    raise SystemExit(0)

# ---- 套用：簇級深淺分類 → 因子場空間平滑 → 拉向膜 ----
factor = np.ones(N)
protected = 0
for cid, mm_ in enumerate(clusters):
    mm_ = np.array(mm_)
    pk = depth[mm_].max()
    if pk >= KEEP_HI:
        f = 0.0
        protected += 1
    elif pk <= KEEP_LO:
        f = 1.0
    else:
        t = (KEEP_HI - pk) / (KEEP_HI - KEEP_LO)
        f = t * t * (3 - 2 * t)
    factor[mm_] = np.minimum(factor[mm_], f)
P(f"protected clusters (峰≥{KEEP_HI*1000:.0f}mm): {protected}")
for _ in range(6):   # 因子場空間平滑（σ≈格距×2）＝保護簇邊界無硬階
    s = np.zeros(N)
    np.add.at(s, dst, factor[src])
    avg = s / np.maximum(deg, 1.0)
    avg[deg == 0] = factor[deg == 0]
    factor = np.minimum(factor, 0.5 * factor + 0.5 * avg)
w = w_region * factor
disp = (mem - co0) * w[:, None]
dm = np.linalg.norm(disp, axis=1)
over = dm > CAP
disp[over] *= (CAP / dm[over])[:, None]
new_co = co0 + disp
moved = np.linalg.norm(new_co - co0, axis=1)
P(f"套用: moved>0.1mm {int((moved > 1e-4).sum())} 顆  |d| p50 {np.percentile(moved[moved>1e-4],50)*1000:.2f} p90 {np.percentile(moved[moved>1e-4],90)*1000:.2f} max {moved.max()*1000:.2f} mm")
assert np.abs(new_co[~in_reg] - co0[~in_reg]).max() == 0.0, "outside-region vertex moved — ABORT"
res2 = np.linalg.norm(new_co - mem, axis=1)
sel = in_reg & (factor > 0.999)
P(f"修後殘差（未保護區 vs 膜）: p90 {np.percentile(res2[sel],90)*1000:.2f} p99 {np.percentile(res2[sel],99)*1000:.2f} max {res2[sel].max()*1000:.2f} mm")
for i in np.where(moved > 1e-6)[0]:
    me.vertices[int(i)].co = Vector(new_co[i])
me.update()

# ---- 平滑籠重存（布的地基跟著新皮膚）＋存 master ----
cage_copy = body.copy()
cage_copy.data = me.copy()
cage_copy.name = "CageSmooth"
bpy.data.libraries.write(CAGE_SMOOTH, {cage_copy}, fake_user=True)
bpy.data.objects.remove(cage_copy, do_unlink=True)
P(f"cage_smooth re-saved -> {CAGE_SMOOTH}")
assert any(m.type == 'ARMATURE' for m in body.modifiers)
bpy.ops.wm.save_mainfile(filepath=MASTER)
P("FLATTEN APPLIED (master saved)")
