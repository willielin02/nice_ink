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
BAND = 0.05   # 頸帶頭側半寬（m）：08-16 喉部污斑定罪＝10cm 帶讓下顎底（臉貼圖陰影區）跟身體垂下露出→收回 5cm；拉伸紋真兇是縫邊硬跳（已修）不是帶寬
NECK_PEAK = 0.30  # Neck 帳篷峰值（08-16 0.45→0.30：Head↔Spine1 更直接的漸變＝更平順）

bpy.ops.wm.open_mainfile(filepath=MASTER)
if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')
body = bpy.data.objects["SumoRetopo"]; arm = bpy.data.objects["Skeleton_Plus-size"]; me = body.data
mw = body.matrix_world
n0 = len(me.vertices)

# ---------- ⓪ 快照 master 的 custom loop normals（08-16 審計：全身重烘與 master 差 p95 12°/
#            max 73°＝把 user 08-05 驗收過的法線整組換掉＝身體中線「蟹足腫」真兇）。
#            帶外一律原封抄回；帶內才用重烘值（帶緣淡入）。key=loop 的 (頂點座標, 面心) ----------
me.calc_normals_split() if hasattr(me, "calc_normals_split") else None
def _lkey(vco, fc):
    return (round(vco.x,5), round(vco.y,5), round(vco.z,5), round(fc.x,4), round(fc.y,4), round(fc.z,4))
master_loop_n = {}
for pidx, poly in enumerate(me.polygons):
    fc = poly.center
    for li in poly.loop_indices:
        l = me.loops[li]
        master_loop_n[_lkey(me.vertices[l.vertex_index].co, fc)] = Vector(l.normal)
print("master loop normals snapshot:", len(master_loop_n))

import os
STAGE = os.environ.get('NI_STAGE', 'all')   # 二分用：weld | smooth | normals | all
print('STAGE', STAGE)
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
# 08-16 鐵坑：seam 平面傾 43°（法線 (0,-0.69,0.73)）→「平面距離 < 5cm」是一片無限
# 斜板，斜著切過上背 z≈100~135（UE 傾印實錘：2017 點位移、345 點權重、背中線法線
# 全被改＝user 抓的「蟹足腫」）。帶＝**到縫環（最近縫頂點）距離**才是脖子本身；
# 平面有號距離只拿來分頭側/身側。所有帶判定一律先過 ring_dist 閘。
kd_seam = KDTree(len(seam_pts))
for _i, _p in enumerate(seam_pts): kd_seam.insert(_p, _i)
kd_seam.balance()
def ring_dist(world_co):
    return kd_seam.find(world_co)[2]
RING_MAX = 0.07   # 縫環 7cm 外＝絕對不是脖子帶（≥ max(BAND, SMOOTH_BAND)）

# ---------- ①b 頸帶幾何平滑（08-16 user 定案：脖區建模面形千奇百怪→只動 xyz、
#            UV0/拓樸/頂點數一字不動；帶外零變化＝邊界固定） ----------
SMOOTH_BAND = 0.07     # seam 平面上下各 5cm 內參與平滑
SMOOTH_FADE = 0.015    # 帶緣 1.5cm 內權重淡出到 0（邊界固定不動）
SMOOTH_ITER = 30 if STAGE != 'weld' else 0
SMOOTH_FACTOR = 0.5
gSm = body.vertex_groups.get("NeckSmooth") or body.vertex_groups.new(name="NeckSmooth")
n_in = 0
SMOOTH_BAND_HEAD = 0.025   # 08-16 喉部污斑定罪：頭側平滑 7cm 把下顎底/喉前抹凹＝臉貼圖下顎陰影糊成一坨→頭側只 2.5cm
for v in me.vertices:
    s = ring_dist(mw @ v.co)   # 到縫環距離（不是平面距離）
    band_here = SMOOTH_BAND if (mw @ v.co - c).dot(nrm) < 0.0 else SMOOTH_BAND_HEAD
    if s > band_here:
        gSm.add([v.index], 0.0, 'REPLACE'); continue
    w = 1.0 if s < band_here - SMOOTH_FADE else (band_here - s) / SMOOTH_FADE
    gSm.add([v.index], max(0.0, min(1.0, w)), 'REPLACE'); n_in += 1
sm = body.modifiers.new("NeckSmooth", 'SMOOTH')
sm.iterations = SMOOTH_ITER; sm.factor = SMOOTH_FACTOR; sm.vertex_group = "NeckSmooth"
# 平滑必須在 armature 修飾器之前作用於 rest 網格：暫時移到堆疊最前
bpy.context.view_layer.objects.active = body; body.select_set(True)
with bpy.context.temp_override(object=body, active_object=body):
    while body.modifiers.find(sm.name) > 0:
        bpy.ops.object.modifier_move_up(modifier=sm.name)
    # 記錄平滑前後位移量（自檢：只有帶內動、量級 mm 級）
    before = {v.index: v.co.copy() for v in me.vertices}
    bpy.ops.object.modifier_apply(modifier=sm.name)
# 位移鉗位：單頂點最多 SMOOTH_MAX_MM（保剪影；平滑只該抹掉 mm 級歪面，超過=在改形）
SMOOTH_MAX_MM = 20.0
clamped = 0
for i, b in before.items():
    d = me.vertices[i].co - b
    if d.length * 1000.0 > SMOOTH_MAX_MM:
        me.vertices[i].co = b + d.normalized() * (SMOOTH_MAX_MM / 1000.0); clamped += 1
disp = [(me.vertices[i].co - before[i]).length for i in before]
moved = [d for d in disp if d > 1e-6]
print(f"neck smooth: band verts={n_in} moved={len(moved)} max={(max(moved) if moved else 0)*1000:.2f}mm mean={sum(moved)/max(1,len(moved))*1000:.2f}mm clamped={clamped}")
# 自檢：帶外（|s|>SMOOTH_BAND）不得有任何位移——有=修飾器影響域漏出，硬失敗
_leak = [(i, ring_dist(mw @ before[i]), (me.vertices[i].co - before[i]).length) for i in before]
_leak = [(i, s_, d_) for i, s_, d_ in _leak if s_ > SMOOTH_BAND and d_ > 1e-6]
if _leak:
    _leak.sort(key=lambda t: -t[2])
    print("SMOOTH LEAK outside band:", len(_leak), "worst:", [(i, round(s_,3), round(d_*1000,2)) for i, s_, d_ in _leak[:8]])
    import sys; sys.stdout.flush()
    raise AssertionError(f"smooth leaked outside band on {len(_leak)} verts")
print("smooth leak check: 0 verts moved outside band")
assert len(me.vertices) == n1, "smooth changed vertex count?!"
body.vertex_groups.remove(body.vertex_groups["NeckSmooth"])  # 工作用群不進 SK（apply 後重取參照）

# ---------- ② 全身柔化法線重烘（沿用 sumo_soft_normals_bake 管線；含焊縫區） ----------
def bake_soft_normals(ob, it=12, fac=0.5):
    src = ob.copy(); src.data = ob.data.copy(); src.name = ob.name + "_NormalSrc"
    bpy.context.collection.objects.link(src)
    m = src.modifiers.new("Soften", 'SMOOTH'); m.iterations = it; m.factor = fac
    bpy.context.view_layer.objects.active = src
    with bpy.context.temp_override(object=src, active_object=src):
        # 副本上 armature 等修飾器一律先移除（只要 rest 幾何的平滑法線）
        for mm in list(src.modifiers):
            if mm.name != m.name: src.modifiers.remove(mm)
        bpy.ops.object.modifier_apply(modifier=m.name)
    dt = ob.modifiers.new("NormalXfer", 'DATA_TRANSFER')
    dt.object = src; dt.use_loop_data = True; dt.data_types_loops = {'CUSTOM_NORMAL'}; dt.loop_mapping = 'TOPOLOGY'
    bpy.context.view_layer.objects.active = ob
    with bpy.context.temp_override(object=ob, active_object=ob):
        while ob.modifiers.find(dt.name) > 0:
            bpy.ops.object.modifier_move_up(modifier=dt.name)
        bpy.ops.object.modifier_apply(modifier=dt.name)
    bpy.data.objects.remove(src, do_unlink=True)
    assert ob.data.has_custom_normals, "normal transfer failed"
bake_soft_normals(body, it=4)  # 08-16 喉部污斑：帶內法線來源 40 趟過度平滑＝下顎底向下法線的暗面被抹寬成一坨；4 趟＝法線跟著實際（已平滑）幾何走
# 帶外原封抄回 master 法線；帶內以 SMOOTH_BAND 內距離做淡入（帶緣=master、seam=重烘）
me.calc_normals_split() if hasattr(me, "calc_normals_split") else None
NBLEND_BAND = SMOOTH_BAND if STAGE not in ('weld','smooth') else -1.0
out_n = []; n_master = n_blend = n_miss = 0
for poly in me.polygons:
    fc = poly.center
    for li in poly.loop_indices:
        l = me.loops[li]; v = me.vertices[l.vertex_index]
        baked = Vector(l.normal)
        s_ = ring_dist(mw @ before.get(v.index, v.co))   # 縫環距離（平滑前位置）
        # 帶內頂點位置被平滑動過→用「平滑前」位置＋平滑前面心查 key
        pre_co = before.get(v.index, v.co)
        pre_fc = sum((before.get(me.loops[j].vertex_index, me.vertices[me.loops[j].vertex_index].co) for j in poly.loop_indices), Vector((0,0,0))) / max(1, len(poly.loop_indices))
        m_ = master_loop_n.get(_lkey(pre_co, pre_fc))
        if s_ >= NBLEND_BAND:
            if m_ is not None: out_n.append(m_); n_master += 1
            else: out_n.append(baked); n_miss += 1
        else:
            # 帶內主體＝重烘（平滑副本轉印＝逐排連續）；只在帶緣最後 20% 淡入 master
            # （08-16 抬頭側面橫紋定罪：帶內混入 master 的切殼移植法線＝逐排參考系不同）
            t = max(0.0, (s_ / NBLEND_BAND - 0.8) / 0.2)
            if m_ is not None and t > 0.0:
                mixv = (baked * (1.0 - t) + m_ * t)
                out_n.append(mixv.normalized() if mixv.length > 1e-6 else baked); n_blend += 1
            else:
                out_n.append(baked); n_blend += 1
me.normals_split_custom_set([tuple(n) for n in out_n])
print(f"normals: outside band restored from master={n_master} (miss={n_miss}), band blended={n_blend}")

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
BAND_BODY = 0.02   # 08-16 乳頭沉修：身側帶只到 seam 下 2cm（上胸頂點不進 Neck/Head 帶）
changed = 0
# 08-16 拉伸紋真兇：頭側頂點（原 Head=1、無身體群）走 head_w = 1-neck_w 分支＝
# 帶內 smoothstep 被覆寫 → 縫環兩側一條邊 Head 權重 0.07→0.9 硬跳，12° 俯仰全壓在
# 一排邊上＝抬頭時縫環處一圈剪切紋（close_up 特寫實錘）。修＝頭側缺身體群的頂點
# 從**最近身側參考頂點**抄身體群分佈×rem，smoothstep 對整帶連續生效。
_body_ref = []   # (world co, {group: normalized weight})
for v in me.vertices:
    if ring_dist(mw @ v.co) > 0.25: continue
    if (mw @ v.co - c).dot(nrm) > -BAND_BODY: continue   # 只收帶外身側
    cur = {gi_name[g.group]: g.weight for g in v.groups if gi_name[g.group] in BODY_GROUPS}
    tot = sum(cur.values())
    if tot > 1e-6: _body_ref.append((mw @ v.co, {n: w / tot for n, w in cur.items()}))
kd_body = KDTree(len(_body_ref))
for _i, (_p, _) in enumerate(_body_ref): kd_body.insert(_p, _i)
kd_body.balance()
print("body-side reference verts:", len(_body_ref))
n_transfer = 0
for v in (me.vertices if STAGE in ('all', 'all4') else []):  # all=正式（含下方 limit 4）
    if ring_dist(mw @ v.co) > RING_MAX: continue   # 縫環 10cm 外一律不碰
    s = (mw @ v.co - c).dot(nrm)
    if s > BAND or s < -BAND_BODY: continue
    t = (s + BAND_BODY) / (BAND + BAND_BODY)   # 0 身側緣 → 1 頭側緣（非對稱帶）
    head_w = smooth(t)
    neck_w = min(NECK_PEAK * (1.0 - abs(2*t - 1.0)), 1.0 - head_w)   # 帳篷（頭側頂端鉗到不超總和 1）
    # 現有非 Head/Neck/MARK/Jiggle 群 = 身體群，等比縮到 rem
    rem = max(0.0, 1.0 - head_w - neck_w)
    cur = {gi_name[g.group]: g.weight for g in v.groups}
    body_tot = sum(w for n, w in cur.items() if n in BODY_GROUPS)
    if body_tot > 1e-6:
        dist = {n: w / body_tot for n, w in cur.items() if n in BODY_GROUPS}
    else:
        _, ri, _ = kd_body.find(mw @ v.co)
        dist = _body_ref[ri][1]; n_transfer += 1
    for n in BODY_GROUPS:
        if n in dist or n in cur:
            body.vertex_groups[n].add([v.index], dist.get(n, 0.0) * rem, 'REPLACE')
    gHead.add([v.index], head_w, 'REPLACE')
    gNeck.add([v.index], neck_w, 'REPLACE')
    changed += 1
print("neck band weights written on", changed, "verts; head-side body-dist transferred:", n_transfer)
# 歸一化檢查：只查頸帶（變形群=骨架真骨；MARK_*/UV 群不算）
bone_names = {b.name for b in arm.data.bones}
bad = 0; worst = 0.0
for v in me.vertices:
    if ring_dist(mw @ v.co) > RING_MAX: continue
    s = (mw @ v.co - c).dot(nrm)
    if abs(s) > BAND: continue
    tot = sum(g.weight for g in v.groups if gi_name[g.group] in bone_names)
    if abs(tot - 1.0) > 0.02: bad += 1
    worst = max(worst, abs(tot - 1.0))
print(f"band verts with bone-weight-sum off by >0.02: {bad} (worst dev {worst:.3f})")
_diag = collections.Counter()
for v in me.vertices:
    if ring_dist(mw @ v.co) > RING_MAX: continue
    s = (mw @ v.co - c).dot(nrm)
    if abs(s) > BAND: continue
    tot = sum(g.weight for g in v.groups if gi_name[g.group] in bone_names)
    if abs(tot - 1.0) > 0.02:
        _diag[tuple(sorted(gi_name[g.group] for g in v.groups if g.weight > 0.01 and gi_name[g.group] in bone_names))] += 1
print("off-sum group combos:", _diag.most_common(6))
# ③b 下顎底臉罩淡出：閘門是 T_FaceMask 貼圖（UV0）不是 FaceMask 頂點色（頂點色歸零實測零效果）
#     → 改在 sumo_face_mask_jawfade.py 處理貼圖；本腳本不動頂點色。
# 08-16 中線疤真兇（NiShot 二分 weld/smooth/normals/all/all4 定罪）：頸帶權重把帶內頂點
# 推到 5~6 影響骨 → UE section MaxBoneInfluences 4→6 → 整個身體 section 換 8 影響 GPU 蒙皮
# 路徑 → 肚子中線鏡射縫/喉部污斑（渲染緩衝逐位相同也看得見＝路徑差不是資料差）。
# 鐵則：每頂點影響骨上限 4＝與切開版同路徑（cut section maxInfl=4）。
if STAGE in ('all', 'all4'):
    bpy.context.view_layer.objects.active = body; body.select_set(True)
    with bpy.context.temp_override(object=body, active_object=body):
        bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
        bpy.ops.object.mode_set(mode='WEIGHT_PAINT')
        bpy.ops.object.vertex_group_limit_total(group_select_mode='BONE_DEFORM', limit=4)
        bpy.ops.object.vertex_group_normalize_all(group_select_mode='BONE_DEFORM', lock_active=False)
        bpy.ops.object.mode_set(mode='OBJECT')
    print("limit_total 4 applied")
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
