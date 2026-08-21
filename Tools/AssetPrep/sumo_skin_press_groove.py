# 布埋入線壓溝（2026-08-22 鋸齒排終解）：牆×多面體皮膚交線=鋸齒（捲邊在隧道被防撞
# 縮徑=遮不住）。解=沿「布埋入皮下的結構」壓出 2.2mm 溝（7mm 羽化）＝交線沉入溝內
# 任何角度不可見；開放邊的溝被捲邊蓋住=外觀零變化。牆底 p50 3.3mm > 溝 2.2mm=照樣密封。
import bpy, os, shutil
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from mathutils.kdtree import KDTree

ROOT = r"C:\games\Unreal Engine\nice_ink"
MASTER = os.path.join(ROOT, "SourceAssets", "sumo_character_master.blend")
BK = os.path.join(ROOT, "SourceAssets", "masters", "sumo_character_master_v30_pregroove.blend")
GROOVE = 0.0022
RADIUS = 0.007

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
N = len(me.vertices)
bco = np.array([v.co[:] for v in me.vertices])
me.calc_loop_triangles()
sbvh = BVHTree.FromPolygons([Vector(c) for c in bco], [tuple(t.vertices) for t in me.loop_triangles], all_triangles=True)

# 布的「埋入結構」＝簽章距離 < -0.5mm 的布頂點
fco = np.array([v.co[:] for v in fund.data.vertices])
buried = []
for c in fco:
    loc, nor, idx, dd = sbvh.find_nearest(Vector(c))
    if loc is not None and (Vector(c) - Vector(loc)).dot(Vector(nor)) < -0.0005:
        buried.append(c)
buried = np.array(buried)
P(f"buried cloth verts: {len(buried)}")
kd = KDTree(len(buried))
for i, c in enumerate(buried):
    kd.insert(Vector(c), i)
kd.balance()

vn = np.zeros((N, 3))
for poly in me.polygons:
    n_ = np.array(poly.normal)
    for vi in poly.vertices:
        vn[vi] += n_
l_ = np.linalg.norm(vn, axis=1); l_[l_ == 0] = 1
vn /= l_[:, None]

moved = 0
amps = []
for i in range(N):
    hit = kd.find(Vector(bco[i]))
    if hit[0] is None or hit[2] > RADIUS:
        continue
    t = 1.0 - hit[2] / RADIUS
    w = t * t * (3 - 2 * t)
    me.vertices[i].co = Vector(bco[i] - vn[i] * (GROOVE * w))
    moved += 1
    amps.append(GROOVE * w)
me.update()
amps = np.array(amps) * 1000
P(f"groove: moved {moved} verts depth p50 {np.percentile(amps,50):.2f} max {amps.max():.2f} mm")
assert any(m.type == 'ARMATURE' for m in body.modifiers)
bpy.ops.wm.save_mainfile(filepath=MASTER)
P("GROOVE SAVED")
