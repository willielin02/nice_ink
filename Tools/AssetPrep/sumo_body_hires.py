# 全身高解析度化（2026-08-21 user 終定案）——褌邊全戰役的地基解：
# 「粗格子地基上做毫米級的布」＝補丁循環的根源。修＝整個身體 Catmull-Clark 細分一級
# （形狀=同一個雕塑的光滑版；3cm 格→1.5cm、面片矢高 ~0.14mm=不可見）；
# 布蓋住＋邊緣帶先在粗籠做 Taubin 中頻去波（2~8cm 帶、user 定罪的皮膚波浪），
# 細分後全身光滑＝窄帶補丁路線（sumo_edge_band_refine）全部退役。
# UV0 凍結版面：subsurf uv_smooth='NONE'＝逐島線性細分、手繪遮罩座標不動。
# 代價：body tris 23,674→~94k（tri-cache 4.7×20k 預算）＝出貨前必過 directdraw 效能閘門。
# Run: blender --background --python sumo_body_hires.py（之後：fundoshi_plate.py→sumo_soft_normals_bake.py）
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
V23 = os.path.join(ROOT, "SourceAssets", "masters", "sumo_character_master_v23_prebandrefine.blend")
BK = os.path.join(ROOT, "SourceAssets", "masters", "sumo_character_master_v26_prehires.blend")
CAGE_SMOOTH = os.path.join(ROOT, "SourceAssets", "masters", "cage_smooth_v3.blend")

TAUBIN_PAIRS = 60
CAP = 0.005          # 中頻去波位移上限
REGION_D = 0.028     # 布蓋住＋邊緣帶（離布任一頂點 28mm 內）
FEATHER = 0.008

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

# ---- 0) 身體退回 v23 乾淨粗籠（窄帶細分補丁全退役）----
with bpy.data.libraries.load(V23, link=False) as (_df, _dt):
    _dt.objects = ["SumoRetopo"]
donor = _dt.objects[0]
old_me = body.data
body.data = donor.data.copy()
bpy.data.objects.remove(donor, do_unlink=True)
bpy.data.meshes.remove(old_me)
me = body.data
P(f"reset to v23: verts={len(me.vertices)} tris={sum(len(p.vertices)-2 for p in me.polygons)}")
assert len(me.vertices) == 11931

# ---- 1) 布區中頻去波（粗籠 Taubin；2~8cm 在 3cm 籠上近 Nyquist＝天生強衰減）----
fkd = KDTree(len(fund.data.vertices))
for _v in fund.data.vertices:
    fkd.insert(_v.co, _v.index)
fkd.balance()
cage_co0 = np.array([v.co[:] for v in me.vertices])
cloth_d = np.array([fkd.find(Vector(c))[2] for c in cage_co0])
w_bs = np.clip((REGION_D - cloth_d) / FEATHER, 0.0, 1.0)
w_bs = w_bs * w_bs * (3 - 2 * w_bs)
region = w_bs > 0
vn = np.zeros((len(me.vertices), 3))
for poly in me.polygons:
    n_ = np.array(poly.normal)
    for vi_ in poly.vertices:
        vn[vi_] += n_
l_ = np.linalg.norm(vn, axis=1)
l_[l_ == 0] = 1
vn = vn / l_[:, None]
adj0 = defaultdict(set)
for e in me.edges:
    a0, b0 = e.vertices
    if np.dot(vn[a0], vn[b0]) > 0.3:   # 同面判準（臀縫兩壁不互抹）
        adj0[a0].add(b0)
        adj0[b0].add(a0)
reg_idx = np.where(region)[0]
nbr0 = {int(i): np.array(sorted(adj0[int(i)]), dtype=np.int64) for i in reg_idx}
x0 = cage_co0.copy()
for _it in range(TAUBIN_PAIRS):
    for lam_ in (0.5, -0.53):
        dx = np.zeros_like(x0)
        for i in reg_idx:
            nb = nbr0[int(i)]
            if len(nb):
                dx[i] = x0[nb].mean(0) - x0[i]
        x0 = x0 + lam_ * (w_bs[:, None] * dx)
moved = 0
for i in reg_idx:
    if len(nbr0[int(i)]) < 4:          # 窄條韁繩
        x0[i] = cage_co0[i]
        continue
    dv = x0[i] - cage_co0[i]
    m_ = np.linalg.norm(dv)
    if m_ > CAP:
        x0[i] = cage_co0[i] + dv * (CAP / m_)
    if np.linalg.norm(x0[i] - cage_co0[i]) > 1e-6:
        moved += 1
for i in reg_idx:
    me.vertices[int(i)].co = Vector(x0[int(i)])
me.update()
amp = np.linalg.norm(x0[reg_idx] - cage_co0[reg_idx], axis=1) * 1000
P(f"cloth-region mid-band Taubin: moved={moved}/{len(reg_idx)} |d| p50 {np.percentile(amp,50):.2f} p90 {np.percentile(amp,90):.2f} max {amp.max():.2f} mm")

# ---- 2) 全身 Catmull-Clark 細分一級（形狀=光滑版；UV 逐島線性=手繪遮罩座標不動）----
mod = body.modifiers.new("HiRes", 'SUBSURF')
mod.levels = 1
mod.render_levels = 1
mod.subdivision_type = 'CATMULL_CLARK'
mod.uv_smooth = 'NONE'
mod.boundary_smooth = 'ALL'
# 修飾器要在 armature 之前（否則 apply 會连蒙皮一起烘）——移到第一位
while body.modifiers.find(mod.name) > 0:
    with bpy.context.temp_override(object=body, active_object=body):
        bpy.ops.object.modifier_move_up(modifier=mod.name)
bpy.context.view_layer.objects.active = body
with bpy.context.temp_override(object=body, active_object=body):
    bpy.ops.object.modifier_apply(modifier=mod.name)
me = body.data
n_tris = sum(len(p.vertices) - 2 for p in me.polygons)
P(f"after CC x1: verts={len(me.vertices)} tris={n_tris}")

# ---- 3) 斷言＋頸縫清點 ----
assert set(l.name for l in me.uv_layers) >= {"UVMap", "FaceUV", "HairUV"}, "UV layers lost"
assert me.color_attributes.get("FaceMask") is not None, "FaceMask lost"
unweighted = sum(1 for v in me.vertices if not v.groups)
P(f"unweighted={unweighted}")
assert unweighted == 0, "weights lost"
assert any(m.type == 'ARMATURE' for m in body.modifiers), "armature lost"
buckets = defaultdict(list)
for v in me.vertices:
    buckets[(round(v.co.x / 1e-4), round(v.co.y / 1e-4), round(v.co.z / 1e-4))].append(v.index)
pairs = sum(1 for b in buckets.values() if len(b) == 2)
P(f"neck seam coincident pairs: {pairs}")
assert pairs >= 80, "neck seam shells diverged after subdivision"

# ---- 4) 平滑籠另存（布的 U 從它長；plate SUBD=2 配 1.5cm 籠）----
cage_copy = body.copy()
cage_copy.data = me.copy()
cage_copy.name = "CageSmooth"
bpy.data.libraries.write(CAGE_SMOOTH, {cage_copy}, fake_user=True)
bpy.data.objects.remove(cage_copy, do_unlink=True)
P(f"cage_smooth saved -> {CAGE_SMOOTH}")

bpy.ops.wm.save_mainfile(filepath=MASTER)
P("SAVED master")
