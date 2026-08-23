"""整形後的法線重烘（2026-08-23）——只換動過的區域，其餘逐位保留

**為什麼不直接跑 sumo_soft_normals_bake.py**：那支會把**全身**法線重算一次，包括
幾何一個位元都沒動、user 已經驗收過的區域，也會蓋掉 08-21 追記60 那次 v3 的帶外
處理（帶外逐位還原 v23）。改動應該只出現在改動處。

**為什麼不能跑 sumo_soft_normals_xfer_v2.py（v3）**：它的前提是「帶外幾何與 v23
逐位相同」，而本次整形動的正好是帶外（離褌 3~15cm），前提被破壞——直接跑會把
v23 的舊法線蓋到動過的幾何上。

**做法**（v3 的同一種哲學，換一組邊界）：
  A. 用現行幾何跑一次同配方（焊縫→SMOOTH 12×0.5→DataTransfer TOPOLOGY）得到新法線
  B. 逐 loop 混合：動過的頂點用新法線，沒動的用**現有**法線，中間 12→22mm 羽化
  C. 契約：離動過區域 >22mm 的 loop 法線必須與烘焙前**逐位相同**

Run: blender --background --python sumo_form_repair_normals.py
"""
import bpy
import bmesh
import os
import math
import numpy as np
from mathutils import Vector
from mathutils.kdtree import KDTree

ROOT = r"C:\games\Unreal Engine\nice_ink"
MASTER = os.path.join(ROOT, "SourceAssets", "sumo_character_master.blend")
REPAIR = os.path.join(ROOT, "Saved", "FormRepair", "repair.npz")
SMOOTH_ITER = 12
SMOOTH_FACTOR = 0.5
FEATHER0, FEATHER1 = 0.012, 0.022


def P(*a):
    print(*a, flush=True)


def smoothstep(x, a, b):
    t = np.clip((x - a) / max(b - a, 1e-9), 0.0, 1.0)
    return t * t * (3 - 2 * t)


d = np.load(REPAIR)
co0, co1 = d["co0"], d["co1"]
moved = np.linalg.norm(co1 - co0, axis=1) > 1e-9
P(f"動過的頂點 {int(moved.sum())} / {len(moved)}")

bpy.ops.wm.open_mainfile(filepath=MASTER)
if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')
ob = bpy.data.objects["SumoRetopo"]
me = ob.data
n = len(me.vertices)
nl = len(me.loops)
co = np.empty(n * 3)
me.vertices.foreach_get("co", co)
co = co.reshape(-1, 3)
assert np.abs(co - co1).max() < 1e-6, "master 幾何與 repair.npz 的 co1 不一致——先跑 sumo_form_repair.py"

# --- 現有法線（烘焙前） ---
old = np.array([tuple(me.corner_normals[i].vector) for i in range(nl)])
P(f"loops={nl}  現有 custom normals: {me.has_custom_normals}")

# --- 對照實驗：量 Blender 儲存 custom normals 的量化誤差 ---
# custom split normals 以 16-bit 壓縮儲存，寫回再讀出必然有殘差。契約門檻要定在
# 這個地板之上，否則量到的是儲存精度不是我的混合誤差（首跑 0.0489 度 FAIL 就是它）。
me.normals_split_custom_set([tuple(v) for v in old])
me.update()
rt = np.array([tuple(me.corner_normals[i].vector) for i in range(nl)])
QUANT = float(np.degrees(np.arccos(np.clip(np.einsum('ij,ij->i', rt, old), -1, 1))).max())
P(f"[對照] 原法線寫回再讀出的量化誤差 max {QUANT:.6f} deg（儲存精度地板）")

# --- 混合權重：動過的區域 1，往外 12->22mm 羽化到 0 ---
kdm = KDTree(int(moved.sum()))
mi = np.nonzero(moved)[0]
for k, v in enumerate(mi):
    kdm.insert(Vector(co[v]), k)
kdm.balance()
dm = np.array([kdm.find(Vector(p))[2] for p in co])
wv = 1.0 - smoothstep(dm, FEATHER0, FEATHER1)
loops_v = np.empty(nl, np.int64)
me.loops.foreach_get("vertex_index", loops_v)
wl = wv[loops_v]
P(f"混合權重：w=1 的 loop {int((wl > 0.999).sum())}，0<w<1 的 {int(((wl > 1e-6) & (wl < 0.999)).sum())}，"
  f"w=0 的 {int((wl <= 1e-6).sum())}")

# --- 同配方烘一份新法線 ---
src = ob.copy()
src.data = ob.data.copy()
src.name = "SumoRetopo_NormalSrc"
bpy.context.collection.objects.link(src)
bpy.context.view_layer.objects.active = src
if src.data.has_custom_normals:
    with bpy.context.temp_override(object=src, active_object=src):
        bpy.ops.mesh.customdata_custom_splitnormals_clear()
bm = bmesh.new()
bm.from_mesh(src.data)
nv0 = len(bm.verts)
bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-4)
nv1 = len(bm.verts)
bm.to_mesh(src.data)
bm.free()
src.data.update()
P(f"平滑來源焊縫 {nv0} -> {nv1}")
assert len(src.data.loops) == nl and len(src.data.polygons) == len(me.polygons), "焊縫改了 loop/面數"
sm = src.modifiers.new("Soften", 'SMOOTH')
sm.iterations = SMOOTH_ITER
sm.factor = SMOOTH_FACTOR
with bpy.context.temp_override(object=src, active_object=src):
    bpy.ops.object.modifier_apply(modifier=sm.name)

tmp = ob.copy()
tmp.data = ob.data.copy()
tmp.name = "SumoRetopo_NewNormals"
bpy.context.collection.objects.link(tmp)
dt = tmp.modifiers.new("NormalXfer", 'DATA_TRANSFER')
dt.object = src
dt.use_loop_data = True
dt.data_types_loops = {'CUSTOM_NORMAL'}
dt.loop_mapping = 'TOPOLOGY'
bpy.context.view_layer.objects.active = tmp
with bpy.context.temp_override(object=tmp, active_object=tmp):
    bpy.ops.object.modifier_apply(modifier=dt.name)
new = np.array([tuple(tmp.data.corner_normals[i].vector) for i in range(nl)])
bpy.data.objects.remove(src, do_unlink=True)
bpy.data.objects.remove(tmp, do_unlink=True)

# --- 逐 loop 混合並寫回 ---
mix = old * (1 - wl)[:, None] + new * wl[:, None]
ln = np.linalg.norm(mix, axis=1, keepdims=True)
mix = mix / np.maximum(ln, 1e-12)
me.normals_split_custom_set([tuple(v) for v in mix])
me.update()

chk = np.array([tuple(me.corner_normals[i].vector) for i in range(nl)])
far = wl <= 1e-6
dev_far = np.degrees(np.arccos(np.clip(np.einsum('ij,ij->i', chk[far], old[far]), -1, 1)))
dev_in = np.degrees(np.arccos(np.clip(np.einsum('ij,ij->i', chk[~far], old[~far]), -1, 1)))
P(f"\n=== 契約：離動過區域 >22mm 的法線必須逐位不變 ===")
far_ok = dev_far.max() <= QUANT * 1.05
P(f"  遠端 n={int(far.sum())}  角度差 max {dev_far.max():.6f} deg "
  f"(量化地板 {QUANT:.6f}) -> {'PASS' if far_ok else 'FAIL'}")
P(f"  混合區 n={int((~far).sum())}  角度差 p50 {np.percentile(dev_in,50):.4f} "
  f"p99 {np.percentile(dev_in,99):.4f} max {dev_in.max():.4f} deg")

# 縫法線連續性（08-16 血價：切開網格平滑會讓縫兩側法線分家）
from collections import defaultdict
buckets = defaultdict(list)
for v in me.vertices:
    key = (round(v.co.x / 1e-4), round(v.co.y / 1e-4), round(v.co.z / 1e-4))
    buckets[key].append(v.index)
pairs = [tuple(b) for b in buckets.values() if len(b) == 2]
lov = defaultdict(list)
for i in range(nl):
    lov[int(loops_v[i])].append(i)
worst = 0.0
tot = 0.0
for a, b in pairs:
    na = chk[lov[a]].sum(0)
    nb = chk[lov[b]].sum(0)
    na /= max(np.linalg.norm(na), 1e-12)
    nb /= max(np.linalg.norm(nb), 1e-12)
    g = math.degrees(math.acos(max(-1.0, min(1.0, float(na @ nb)))))
    worst = max(worst, g)
    tot += g
P(f"  縫對 {len(pairs)} 組  法線落差 max {worst:.2f} deg mean {tot/max(len(pairs),1):.2f} deg -> "
  f"{'PASS' if worst < 3.0 else 'FAIL'}")

assert any(m.type == 'ARMATURE' for m in ob.modifiers), "armature modifier lost"
assert len(me.vertices) == n, "頂點數變了"
if far_ok and worst < 3.0:
    bpy.ops.wm.save_mainfile()
    P("SAVED master（法線已重烘：只換動過的區域）")
else:
    P("契約未過 -> master 未存")
P("REPAIR NORMALS DONE")
