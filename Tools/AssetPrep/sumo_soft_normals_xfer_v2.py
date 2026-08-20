# 柔化法線轉印 v2（2026-08-21）——細分帶身體專用。
# v1 配方（sumo_soft_normals_bake）在「細分帶身體」上有帶界摺：平滑代理=同拓樸副本
# 過均勻 Laplacian，細帶（1.5cm）與帶外（3cm）平滑量不同 ⇒ 轉印法線在帶界摺一條線
# ＝著色上一條沿布邊的「溝」（user「大腿內側莫名凹槽」的強嫌疑）。
# v2＝法線來源改 **v23 粗網格**（密度均勻＝無帶界）：焊縫→SMOOTH 12×0.5（原配方
# 同款讀感）→ POLYINTERP_NEAREST 轉印到細分身體。Fundoshi 照舊不烘（分區制）。
# Run: blender --background --python sumo_soft_normals_xfer_v2.py
import bpy, os, math

ROOT = r"C:\games\Unreal Engine\nice_ink"
MASTER = os.path.join(ROOT, "SourceAssets", "sumo_character_master.blend")
V23 = os.path.join(ROOT, "SourceAssets", "masters", "sumo_character_master_v23_prebandrefine.blend")
SMOOTH_ITER = 12
SMOOTH_FACTOR = 0.5

bpy.ops.wm.open_mainfile(filepath=MASTER)
if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')

body = bpy.data.objects["SumoRetopo"]

# ---- 來源代理＝v23 粗網格（append）----
with bpy.data.libraries.load(V23, link=False) as (_df, _dt):
    _dt.objects = ["SumoRetopo"]
src = _dt.objects[0]
src.name = "NormalSrcV23"
bpy.context.collection.objects.link(src)
for m in list(src.modifiers): src.modifiers.remove(m)
src.parent = None
if src.data.has_custom_normals:
    with bpy.context.temp_override(object=src, active_object=src):
        bpy.ops.mesh.customdata_custom_splitnormals_clear()
# 焊縫（08-16 鐵則：切開網格平滑前先焊、否則縫兩側各自內捲）
import bmesh
bm = bmesh.new(); bm.from_mesh(src.data)
nv0 = len(bm.verts)
bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-4)
print(f"src weld {nv0} -> {len(bm.verts)}", flush=True)
bm.to_mesh(src.data); bm.free(); src.data.update()
sm = src.modifiers.new("Soften", 'SMOOTH')
sm.iterations = SMOOTH_ITER; sm.factor = SMOOTH_FACTOR
with bpy.context.temp_override(object=src, active_object=src):
    bpy.ops.object.modifier_apply(modifier=sm.name)

# ---- 轉印（拓樸不同＝POLYINTERP_NEAREST）----
if body.data.has_custom_normals:
    with bpy.context.temp_override(object=body, active_object=body):
        bpy.ops.mesh.customdata_custom_splitnormals_clear()
dt = body.modifiers.new("NormalXfer", 'DATA_TRANSFER')
dt.object = src
dt.use_loop_data = True
dt.data_types_loops = {'CUSTOM_NORMAL'}
dt.loop_mapping = 'POLYINTERP_NEAREST'
bpy.context.view_layer.objects.active = body
with bpy.context.temp_override(object=body, active_object=body):
    bpy.ops.object.modifier_apply(modifier=dt.name)
bpy.data.objects.remove(src, do_unlink=True)
assert body.data.has_custom_normals, "normal transfer failed"
print(f"normals transferred from v23 smooth proxy (iter={SMOOTH_ITER} f={SMOOTH_FACTOR})", flush=True)

# ---- 頸縫連續性驗證（同 v1 檢查）----
from collections import defaultdict
me = body.data
me.update()
buckets = defaultdict(list)
for v in me.vertices:
    key = (round(v.co.x / 1e-4), round(v.co.y / 1e-4), round(v.co.z / 1e-4))
    buckets[key].append(v.index)
pairs = [tuple(b) for b in buckets.values() if len(b) == 2]
loops_of_vert = defaultdict(list)
for poly in me.polygons:
    for li in poly.loop_indices:
        loops_of_vert[me.loops[li].vertex_index].append(li)
import mathutils
def vn(vi):
    n = mathutils.Vector((0, 0, 0))
    for li in loops_of_vert[vi]:
        n += me.corner_normals[li].vector
    return n.normalized() if n.length > 0 else n
worst = 0.0
for a, b in pairs:
    d = max(-1.0, min(1.0, vn(a).dot(vn(b))))
    worst = max(worst, math.degrees(math.acos(d)))
print(f"seam pairs={len(pairs)} normal gap max={worst:.2f}deg", flush=True)
assert len(pairs) >= 80 and worst < 3.0, f"seam normals discontinuous ({worst:.1f}deg, n={len(pairs)})"
assert any(m.type == 'ARMATURE' for m in body.modifiers), "armature lost"
bpy.ops.wm.save_mainfile(filepath=MASTER)
print("SOFT NORMALS V2 DONE (master saved)", flush=True)
