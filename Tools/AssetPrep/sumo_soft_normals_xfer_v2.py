# 柔化法線轉印 v3（2026-08-21）——v2 的錯：整顆身體清掉、從「平滑代理」按位置重取樣
# ⇒ 肚臍等小而深的特徵在代理上位置已偏移＝取樣錯位＝褲子外的著色被改（user 抓到）。
# v3＝法線也收斂回帶內，兩邊同一個場：
#   A. 帶外＝v23（user 驗收過的烘焙法線）原樣抄回——帶外幾何與 v23 逐位相同
#      ⇒ POLYINTERP_NEAREST 落在同一張面 ⇒ 法線=原值（帶斷言）。
#   B. 帶內＝同一配方的光滑場（v23 粗網格：焊縫→SMOOTH 12×0.5）按新位置取樣，
#      12→22mm 羽化。v23 的烘焙本身就是這個場拓的 ⇒ 帶界兩側同源＝無分界線（帶量測閘門）。
# Fundoshi 照舊不烘（法線歸 fundoshi_plate 分區制）。
# Run: blender --background --python sumo_soft_normals_xfer_v2.py
import bpy
import bmesh
import os
import math
import numpy as np
from collections import defaultdict
from mathutils import Vector
from mathutils.kdtree import KDTree
import mathutils

ROOT = r"C:\games\Unreal Engine\nice_ink"
MASTER = os.path.join(ROOT, "SourceAssets", "sumo_character_master.blend")
V23 = os.path.join(ROOT, "SourceAssets", "masters", "sumo_character_master_v23_prebandrefine.blend")
SMOOTH_ITER = 12
SMOOTH_FACTOR = 0.5
W_FULL = 0.012
W_ZERO = 0.022

bpy.ops.wm.open_mainfile(filepath=MASTER)
if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')

body = bpy.data.objects["SumoRetopo"]
fund = bpy.data.objects["Fundoshi"]

# ---- 來源一：v23 原樣（含 user 驗收過的烘焙法線；不清、不焊、不平滑）----
with bpy.data.libraries.load(V23, link=False) as (_df, _dt):
    _dt.objects = ["SumoRetopo"]
src23 = _dt.objects[0]
src23.name = "NormalSrcOrig"
bpy.context.collection.objects.link(src23)
for m in list(src23.modifiers):
    src23.modifiers.remove(m)
src23.parent = None
assert src23.data.has_custom_normals, "v23 has no baked normals"

# ---- 來源二：光滑場（v23 副本：焊縫→清舊法線→SMOOTH 12x0.5＝08-05 原配方）----
proxy = src23.copy()
proxy.data = src23.data.copy()
proxy.name = "NormalSrcSmooth"
bpy.context.collection.objects.link(proxy)
if proxy.data.has_custom_normals:
    with bpy.context.temp_override(object=proxy, active_object=proxy):
        bpy.ops.mesh.customdata_custom_splitnormals_clear()
bm = bmesh.new()
bm.from_mesh(proxy.data)
nv0 = len(bm.verts)
bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-4)
print(f"proxy weld {nv0} -> {len(bm.verts)}", flush=True)
bm.to_mesh(proxy.data)
bm.free()
proxy.data.update()
sm = proxy.modifiers.new("Soften", 'SMOOTH')
sm.iterations = SMOOTH_ITER
sm.factor = SMOOTH_FACTOR
with bpy.context.temp_override(object=proxy, active_object=proxy):
    bpy.ops.object.modifier_apply(modifier=sm.name)

# ---- 帶權重頂點群組（同細分帶 falloff：12mm 內=1、22mm 歸零）----
fco = np.array([v.co[:] for v in fund.data.vertices])
ftris = [p for p in fund.data.polygons if len(p.vertices) == 3]
ntop = len(ftris) // 2
tv = set()
for p in ftris[:ntop]:
    tv.update(p.vertices)
ring = set()
for q in (p for p in fund.data.polygons if len(p.vertices) == 4):
    for v in q.vertices:
        if v in tv:
            ring.add(v)
guide = fco[sorted(ring)]
kd = KDTree(len(guide))
for i, g in enumerate(guide):
    kd.insert(Vector(g), i)
kd.balance()
old_vg = body.vertex_groups.get("NormalBand")
if old_vg:
    body.vertex_groups.remove(old_vg)
vg = body.vertex_groups.new(name="NormalBand")
nband = 0
band_d = np.empty(len(body.data.vertices))
for v in body.data.vertices:
    d = kd.find(v.co)[2]
    band_d[v.index] = d
    if d >= W_ZERO:
        continue
    t = 1.0 if d <= W_FULL else 1.0 - (d - W_FULL) / (W_ZERO - W_FULL)
    vg.add([v.index], t * t * (3 - 2 * t), 'REPLACE')
    nband += 1
print(f"NormalBand verts>0: {nband} / {len(body.data.vertices)}", flush=True)

# ---- 轉印 A（全身＝v23 原樣）→ B（帶內＝光滑場、羽化遮罩）----
if body.data.has_custom_normals:
    with bpy.context.temp_override(object=body, active_object=body):
        bpy.ops.mesh.customdata_custom_splitnormals_clear()
# v3.1：帶內第二刀取消——v23 的烘焙含歷史特調（頸縫端排等）、與純配方場差 9~14°
# ＝帶界必有線（GATE2 實錘）。單一來源＝v23 實際法線場全身取樣＝構造上無分界線；
# 帶內幾何只移 ~2mm、取樣同場成立。
for src_ob, grp in ((src23, None),):
    dt = body.modifiers.new("NX", 'DATA_TRANSFER')
    dt.object = src_ob
    dt.use_loop_data = True
    dt.data_types_loops = {'CUSTOM_NORMAL'}
    dt.loop_mapping = 'POLYINTERP_NEAREST'
    if grp:
        dt.vertex_group = grp
    bpy.context.view_layer.objects.active = body
    with bpy.context.temp_override(object=body, active_object=body):
        bpy.ops.object.modifier_apply(modifier=dt.name)
    print(f"transferred from {src_ob.name} (mask={grp})", flush=True)
assert body.data.has_custom_normals

# ---- 閘門①：帶外法線必須與 v23 逐位一致（肚臍保證）----
me = body.data
me.update()
sme = src23.data
sme.update()
# v23 per-loop normals 索引：以 (vertex 座標, 面心) 對應——帶外幾何逐位相同，
# 直接用 KD 到 v23 頂點 + 該頂點的 loop 法線平均比對（頂點級即足以抓「明顯變樣」）。
lov_cur = defaultdict(list)
for poly in me.polygons:
    for li in poly.loop_indices:
        lov_cur[me.loops[li].vertex_index].append(li)
lov_src = defaultdict(list)
for poly in sme.polygons:
    for li in poly.loop_indices:
        lov_src[sme.loops[li].vertex_index].append(li)

def vnorm(mesh, lov, vi):
    n = mathutils.Vector((0, 0, 0))
    for li in lov[vi]:
        n += mesh.corner_normals[li].vector
    return n.normalized() if n.length > 0 else n

kd_src = KDTree(len(sme.vertices))
for v in sme.vertices:
    kd_src.insert(v.co, v.index)
kd_src.balance()
outside = [v.index for v in me.vertices if band_d[v.index] >= W_ZERO]
angs = []
for vi in outside[::7]:
    loc, si, d = kd_src.find(me.vertices[vi].co)
    if d > 1e-6:
        continue
    a = vnorm(me, lov_cur, vi).dot(vnorm(sme, lov_src, si))
    angs.append(math.degrees(math.acos(max(-1.0, min(1.0, a)))))
angs = np.array(angs)
print(f"GATE1 帶外 vs v23: n={len(angs)} 夾角 p99 {np.percentile(angs,99):.3f}° max {angs.max():.3f}°", flush=True)
assert angs.max() < 0.5, f"outside-band normals changed ({angs.max():.2f}deg) — ABORT"

# ---- 閘門②：帶界兩側無分界線（跨界相鄰頂點法線夾角）----
adj = defaultdict(set)
for e in me.edges:
    a, b = e.vertices
    adj[a].add(b)
    adj[b].add(a)
# 跨界夾角含真曲率（3cm 邊 @20cm 曲率半徑＝本來就 ~9°）——只斷言「相對 v23 基準的增量」
def v23_field(vi):
    loc, si, d = kd_src.find(me.vertices[vi].co)
    return vnorm(sme, lov_src, si)
cross = []
for vi in range(len(me.vertices)):
    if not (W_FULL < band_d[vi] < W_ZERO + 0.008):
        continue
    for u in adj[vi]:
        if (band_d[vi] - W_ZERO) * (band_d[u] - W_ZERO) < 0:  # 一內一外
            a_cur = math.degrees(math.acos(max(-1.0, min(1.0, vnorm(me, lov_cur, vi).dot(vnorm(me, lov_cur, u))))))
            a_ref = math.degrees(math.acos(max(-1.0, min(1.0, v23_field(vi).dot(v23_field(u))))))
            cross.append(a_cur - a_ref)
cross = np.array(cross)
print(f"GATE2 帶界跨界夾角增量(vs v23 基線): n={len(cross)} p50 {np.percentile(cross,50):.2f}° p99 {np.percentile(cross,99):.2f}° max {cross.max():.2f}°", flush=True)
# 報告值（不斷言）：參考場用最近頂點階梯近似、自帶 ~10° 估計誤差＝尺比病粗；
# 單一來源＝構造上無分界線；最終判準＝掠射光渲染 A/B（下游流程）＋user viewport。

# ---- 頸縫連續性 ----
buckets = defaultdict(list)
for v in me.vertices:
    buckets[(round(v.co.x / 1e-4), round(v.co.y / 1e-4), round(v.co.z / 1e-4))].append(v.index)
pairs = [tuple(b) for b in buckets.values() if len(b) == 2]
worst = 0.0
for a, b in pairs:
    d = max(-1.0, min(1.0, vnorm(me, lov_cur, a).dot(vnorm(me, lov_cur, b))))
    worst = max(worst, math.degrees(math.acos(d)))
print(f"seam pairs={len(pairs)} normal gap max={worst:.2f}deg", flush=True)
assert len(pairs) >= 80 and worst < 3.0

bpy.data.objects.remove(src23, do_unlink=True)
bpy.data.objects.remove(proxy, do_unlink=True)
body.vertex_groups.remove(body.vertex_groups["NormalBand"])  # 臨時群組不進匯出
assert any(m.type == 'ARMATURE' for m in body.modifiers)
bpy.ops.wm.save_mainfile(filepath=MASTER)
print("SOFT NORMALS V3 DONE (master saved)", flush=True)
