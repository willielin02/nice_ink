# Sumo 骨骼網格「脖子縫合版」匯出（2026-08-15；user 定案：站立/作畫的脖子回歸蒙皮
# 網格本體、程序化伸縮脖只留給甦醒者升起 46cm）。
#
# 非破壞：master.blend（切開版＝甦醒者資產的正本）一字不動；本腳本在記憶體裡
#   ① 焊回 cut_seam_head3 的 84 對重合頂點（split_edges 的精確逆運算：只焊邊界對、
#      零新增幾何、UV loop 原樣、其餘拓樸原樣）
#   ② 焊縫兩側自訂法線平均（兩殼各自 data-transfer 過的法線在縫上不連續＝頭燈假光
#      下一條明暗線；平均後連續）
#   ③ 頸帶蒙皮權重：沿 seam 平面法線的有號距離 s（+朝頭）在 [-BAND,+BAND] 內
#      Head 0→1 smoothstep、Neck 帳篷峰在 s=0、身體群（Spine1/肩/臂）等比縮至剩餘
#      —— 程序化初值；美學細修屬 Blender 筆刷主場（user 鐵則）
#   ④ 匯出 sumo_skeletal_whole.fbx（同 build_sumo_skeletal_fbx 匯出設定）
# 引擎：SK_Sumo_Whole（站立/作畫 BowBody 常駐）；SK_Sumo（切開版）只在甦醒替身 swap。
# Run: blender --background --python build_sumo_skeletal_whole_fbx.py
import bpy, bmesh, collections, math
import numpy as np
from mathutils import Vector

MASTER = r"C:\games\Unreal Engine\nice_ink\SourceAssets\sumo_character_master.blend"
OUT_FBX = r"C:\games\Unreal Engine\nice_ink\SourceAssets\sumo_skeletal_whole.fbx"
BAND = 0.06   # 頸帶半寬（m）：焊縫上下各 6cm 參與 Neck/Head 漸變
NECK_PEAK = 0.45  # Neck 帳篷峰值（s=0）；其餘給 Head/身體

bpy.ops.wm.open_mainfile(filepath=MASTER)
if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')
body = bpy.data.objects["SumoRetopo"]; arm = bpy.data.objects["Skeleton_Plus-size"]; me = body.data
mw = body.matrix_world
n0 = len(me.vertices)

# ---------- ① 焊縫 ----------
face_count = collections.Counter()
for p in me.polygons:
    for e in p.edge_keys: face_count[e] += 1
bverts = set()
for e in me.edges:
    if face_count[tuple(sorted((e.vertices[0], e.vertices[1])))] == 1:
        bverts.update(e.vertices)
assert len(bverts) == 168, f"boundary verts {len(bverts)} != 168 (expect 84 seam pairs)"
seam_pts = [mw @ me.vertices[i].co for i in bverts]

bm = bmesh.new(); bm.from_mesh(me); bm.verts.ensure_lookup_table()
sel = [bm.verts[i] for i in bverts]
bmesh.ops.remove_doubles(bm, verts=sel, dist=1e-5)
bm.to_mesh(me); bm.free(); me.update()
n1 = len(me.vertices)
assert n0 - n1 == 84, f"welded {n0-n1} verts, expected 84"
# 焊後不得再有邊界
face_count = collections.Counter()
for p in me.polygons:
    for e in p.edge_keys: face_count[e] += 1
assert not any(c == 1 for c in face_count.values()), "boundary edges remain after weld"
print("WELD ok:", n0, "->", n1)

# ---------- ② 焊縫法線平均 ----------
# seam 平面（Newell）與有號距離函式
c = Vector((0,0,0))
for p in seam_pts: c += p
c /= len(seam_pts)
# 用焊後最近點找 seam 頂點：焊後 seam 頂點 = 與 seam_pts 重合者
from mathutils.kdtree import KDTree
kd = KDTree(len(me.vertices))
for v in me.vertices: kd.insert(mw @ v.co, v.index)
kd.balance()
seam_idx = set()
for p in seam_pts:
    co, idx, d = kd.find(p)
    if d < 1e-5: seam_idx.add(idx)
assert len(seam_idx) == 84, f"seam verts after weld {len(seam_idx)}"
# 法線：Newell 用有序環太麻煩——用 PCA 最小主軸
P = np.array([[p.x, p.y, p.z] for p in seam_pts])
P -= P.mean(axis=0)
_, _, vt = np.linalg.svd(P, full_matrices=False)
nrm = Vector(vt[-1]); 
if nrm.z < 0: nrm = -nrm   # 朝頭側
print("seam normal", tuple(round(x,3) for x in nrm), "centroid", tuple(round(x,3) for x in c))

if me.has_custom_normals or True:
    me.calc_normals_split() if hasattr(me, "calc_normals_split") else None
    loops_n = [Vector(l.normal) for l in me.loops]
    # 每個焊縫頂點：其所有 loop 的法線平均後回寫
    vert_loops = collections.defaultdict(list)
    for li, l in enumerate(me.loops):
        if l.vertex_index in seam_idx: vert_loops[l.vertex_index].append(li)
    for vi, lis in vert_loops.items():
        avg = Vector((0,0,0))
        for li in lis: avg += loops_n[li]
        if avg.length > 1e-6:
            avg.normalize()
            for li in lis: loops_n[li] = avg
    me.normals_split_custom_set([tuple(n) for n in loops_n])
    print("seam normals averaged on", len(vert_loops), "verts")

# ---------- ③ 頸帶權重 ----------
def group(name):
    g = body.vertex_groups.get(name)
    return g if g else body.vertex_groups.new(name=name)
gHead = group("Head"); gNeck = group("Neck")
gi_name = {g.index: g.name for g in body.vertex_groups}
BODY_GROUPS = {"Spine1", "Spine2", "LeftShoulder", "RightShoulder", "LeftArm", "RightArm", "Spine",
               "Jiggle_Belly", "Jiggle_Chest_L", "Jiggle_Chest_R"}  # 身側質量含胸/肚彈跳骨（等比縮、保相對份額）
def smooth(t): 
    t = max(0.0, min(1.0, t)); return t*t*(3-2*t)
changed = 0
for v in me.vertices:
    s = (mw @ v.co - c).dot(nrm)
    if abs(s) > BAND: continue
    t = (s + BAND) / (2*BAND)          # 0 身側 → 1 頭側
    head_w = smooth(t)
    neck_w = NECK_PEAK * (1.0 - abs(2*t - 1.0))   # 帳篷
    # 現有非 Head/Neck/MARK/Jiggle 群 = 身體群，等比縮到 rem
    rem = max(0.0, 1.0 - head_w - neck_w)
    cur = {gi_name[g.group]: g.weight for g in v.groups}
    body_tot = sum(w for n, w in cur.items() if n in BODY_GROUPS)
    for n, w in cur.items():
        if n in BODY_GROUPS:
            body.vertex_groups[n].add([v.index], (w / body_tot * rem) if body_tot > 1e-6 else 0.0, 'REPLACE')
    if body_tot <= 1e-6:
        # 頭側原本 Head=1：把 Head 讓一部分給 Neck（保總和 1）
        head_w = 1.0 - neck_w
    gHead.add([v.index], head_w, 'REPLACE')
    gNeck.add([v.index], neck_w, 'REPLACE')
    changed += 1
print("neck band weights written on", changed, "verts")
# 歸一化檢查：只查頸帶（變形群=骨架真骨；MARK_*/UV 群不算）
bone_names = {b.name for b in arm.data.bones}
bad = 0; worst = 0.0
for v in me.vertices:
    s = (mw @ v.co - c).dot(nrm)
    if abs(s) > BAND: continue
    tot = sum(g.weight for g in v.groups if gi_name[g.group] in bone_names)
    if abs(tot - 1.0) > 0.02: bad += 1
    worst = max(worst, abs(tot - 1.0))
print(f"band verts with bone-weight-sum off by >0.02: {bad} (worst dev {worst:.3f})")
_diag = collections.Counter()
for v in me.vertices:
    s = (mw @ v.co - c).dot(nrm)
    if abs(s) > BAND: continue
    tot = sum(g.weight for g in v.groups if gi_name[g.group] in bone_names)
    if abs(tot - 1.0) > 0.02:
        _diag[tuple(sorted(gi_name[g.group] for g in v.groups if g.weight > 0.01 and gi_name[g.group] in bone_names))] += 1
print("off-sum group combos:", _diag.most_common(6))

# ---------- ④ 材質槽合併＋匯出（同原腳本） ----------
if len(body.material_slots) > 1:
    mi = np.zeros(len(me.polygons), np.int32)
    me.polygons.foreach_set("material_index", mi)
    bpy.context.view_layer.objects.active = body; body.select_set(True)
    with bpy.context.temp_override(object=body, active_object=body):
        while len(body.material_slots) > 1:
            body.active_material_index = len(body.material_slots) - 1
            bpy.ops.object.material_slot_remove()
for o in bpy.context.view_layer.objects: o.select_set(True)
bpy.ops.export_scene.fbx(filepath=OUT_FBX, use_selection=False, object_types={'ARMATURE','MESH'},
    apply_unit_scale=True, apply_scale_options='FBX_SCALE_NONE', path_mode='STRIP',
    use_mesh_modifiers=False, add_leaf_bones=False, colors_type='SRGB',
    armature_nodetype='NULL', use_armature_deform_only=False)
print("EXPORTED", OUT_FBX)
