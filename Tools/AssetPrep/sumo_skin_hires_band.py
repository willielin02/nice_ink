# 布鄰帶皮膚高解析化＋緊膜整平（2026-08-22 user 定案「動工」）：
# 真掠射 A/B 定罪＝帶邊鋸齒＝皮膚殘餘 0.2~0.5mm 微起伏 × 掠射遮擋放大 30~60x（布無罪）。
# 修＝離布 10cm 帶內：①細分一級（1.5cm→0.75cm、bmesh grid-fill、UV/權重/vcol 內插）
#     ②緊膜整平（零收縮 Taubin 膜 ×200 對、同面判準、全拉、cap 1.5mm）
# 目標＝帶內對膜殘餘 p99 < 0.1mm ⇒ 齒縮 3~5 倍。帶外逐位不動（斷言）。
# 布不重建：皮膚移動 ≤1mm << 布淨空 3mm（頂 3.06mm/腳埋 2.5mm 都吃得下）。
# Run: blender --background --python sumo_skin_hires_band.py（之後 sumo_soft_normals_bake.py）
import bpy
import bmesh
import os
import shutil
import numpy as np
from collections import defaultdict
from mathutils import Vector
from mathutils.kdtree import KDTree

ROOT = r"C:\games\Unreal Engine\nice_ink"
MASTER = os.path.join(ROOT, "SourceAssets", "sumo_character_master.blend")
BK = os.path.join(ROOT, "SourceAssets", "masters", os.environ.get("BAND_BK", "sumo_character_master_v28_prebandhires.blend"))

REGION_FULL = float(os.environ.get("BAND_FULL", "0.10"))
REGION_ZERO = float(os.environ.get("BAND_ZERO", "0.12"))
TAUBIN_PAIRS = 200
CAP = 0.0015

def P(*a):
    print(*a, flush=True)

bpy.ops.wm.open_mainfile(filepath=MASTER)
if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')
if not os.path.exists(BK):
    shutil.copy2(MASTER, BK)
    P("backup ->", BK)

body = bpy.data.objects["SumoRetopo"]
fund = bpy.data.objects["Fundoshi"]
me = body.data
P(f"before: verts={len(me.vertices)} tris={sum(len(p.vertices)-2 for p in me.polygons)}")

# ---- 1) 細分：離布 10cm 內的邊全切一刀（grid-fill＝quad 規則細分）----
fkd = KDTree(len(fund.data.vertices))
for _v in fund.data.vertices:
    fkd.insert(_v.co, _v.index)
fkd.balance()
co0 = np.array([v.co[:] for v in me.vertices])
cd0 = np.array([fkd.find(Vector(c))[2] for c in co0])
bm = bmesh.new()
bm.from_mesh(me)
bm.verts.ensure_lookup_table()
sel = [e for e in bm.edges if cd0[e.verts[0].index] < REGION_ZERO and cd0[e.verts[1].index] < REGION_ZERO]
P(f"subdividing edges: {len(sel)}")
bmesh.ops.subdivide_edges(bm, edges=sel, cuts=1, use_grid_fill=True)
bm.to_mesh(me)
bm.free()
me.update()
N = len(me.vertices)
P(f"after subdiv: verts={N} tris={sum(len(p.vertices)-2 for p in me.polygons)}")

# ---- 2) 緊膜整平 ----
co = np.array([v.co[:] for v in me.vertices])
cd = np.array([fkd.find(Vector(c))[2] for c in co])
w_reg = np.clip((REGION_ZERO - cd) / (REGION_ZERO - REGION_FULL), 0.0, 1.0)
w_reg = w_reg * w_reg * (3 - 2 * w_reg)
in_reg = w_reg > 0
P(f"region verts: {int(in_reg.sum())} full: {int((w_reg >= 0.999).sum())}")
vn = np.zeros((N, 3))
for poly in me.polygons:
    n_ = np.array(poly.normal)
    for vi in poly.vertices:
        vn[vi] += n_
l_ = np.linalg.norm(vn, axis=1)
l_[l_ == 0] = 1
vn = vn / l_[:, None]
pr = []
for e in me.edges:
    a, b = e.vertices
    if np.dot(vn[a], vn[b]) > 0.3:   # 同面判準（臀縫兩壁不互抹）
        pr.append((a, b))
        pr.append((b, a))
pr = np.array(pr, dtype=np.int64)
dst, src = pr[:, 0], pr[:, 1]
deg = np.zeros(N)
np.add.at(deg, dst, 1.0)
x = co.copy()
for _it in range(TAUBIN_PAIRS):
    for lam in (0.5, -0.53):
        s_ = np.zeros_like(x)
        np.add.at(s_, dst, x[src])
        avg = s_ / np.maximum(deg, 1.0)[:, None]
        dx = avg - x
        dx[deg == 0] = 0.0
        x = x + lam * (w_reg[:, None] * dx)
mem = x
disp = (mem - co) * w_reg[:, None]
dm = np.linalg.norm(disp, axis=1)
over = dm > CAP
disp[over] *= (CAP / dm[over])[:, None]
new_co = co + disp
moved = np.linalg.norm(new_co - co, axis=1)
mv = moved[moved > 1e-5] * 1000
P(f"flatten: moved>0.01mm {len(mv)} |d| p50 {np.percentile(mv,50):.3f} p90 {np.percentile(mv,90):.3f} max {mv.max():.3f} mm")
assert np.abs(new_co[~in_reg] - co[~in_reg]).max() == 0.0, "outside-region vertex moved"
res = np.linalg.norm(new_co - mem, axis=1)
sel_r = in_reg & (w_reg >= 0.999)
P(f"帶內對膜殘餘: p50 {np.percentile(res[sel_r],50)*1000:.4f} p90 {np.percentile(res[sel_r],90)*1000:.4f} p99 {np.percentile(res[sel_r],99)*1000:.4f} max {res[sel_r].max()*1000:.4f} mm")
for i in np.where(moved > 1e-7)[0]:
    me.vertices[int(i)].co = Vector(new_co[i])
me.update()

# ---- 3) 斷言 ----
assert set(l.name for l in me.uv_layers) >= {"UVMap", "FaceUV", "HairUV"}, "UV layers lost"
assert me.color_attributes.get("FaceMask") is not None, "FaceMask lost"
unweighted = sum(1 for v in me.vertices if not v.groups)
P(f"unweighted={unweighted}")
assert unweighted == 0, "weights lost"
assert any(m.type == 'ARMATURE' for m in body.modifiers), "armature lost"

bpy.ops.wm.save_mainfile(filepath=MASTER)
P("BAND HIRES + TIGHT FLATTEN SAVED")
