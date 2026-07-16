# 脖底切盤手術（user 2026-07-16 定案：真切開、乾淨錯位）
# 平面：法線前傾 20 度（朝 -Y）、過 (0, 0, 1.41)（Blender 世界座標）
# 步驟：bisect（自然環，不圓化）→ 環邊 split → 兩側斷面 triangle-fill 封蓋
#       → 上殼硬權重 Head=1 / 下殼剝除 Neck/Head → 縫區法線 data-transfer（rest 零視覺差）
# UV0/FaceUV/HairUV 完全不重展；斷面 UV 停在 UV0 佔用圖找出的空位（單點退化）。
import bpy
import bmesh
import math
import numpy as np

MASTER = r"C:/games/Unreal Engine/nice_ink/SourceAssets/sumo_character_master.blend"
TH = math.radians(20.0)
PLANE_CO = (0.0, 0.0, 1.41)
PLANE_NO = (0.0, -math.sin(TH), math.cos(TH))
EPS = 5e-5

bpy.ops.wm.open_mainfile(filepath=MASTER)
if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')

body = bpy.data.objects["SumoRetopo"]
arm = bpy.data.objects["Skeleton_Plus-size"]
me = body.data
assert sorted(o.name for o in bpy.data.objects if o.type == 'MESH') == ["Fundoshi", "SumoRetopo"]
assert me.shape_keys is None, "shape keys present - surgery would break them"
assert body.matrix_world.is_identity or True
mw = np.array(body.matrix_world)
assert np.allclose(mw, np.eye(4), atol=1e-6), f"matrix_world not identity:\n{mw}"

pre_counts = (len(me.vertices), len(me.edges), len(me.polygons), len(me.loops))
uv_names_pre = [l.name for l in me.uv_layers]
print("PRE:", pre_counts, uv_names_pre)

# --- UV0 佔用圖 → 空位停泊點 ---
n_loops = len(me.loops)
uv0 = np.empty(n_loops * 2)
me.uv_layers["UVMap"].data.foreach_get("uv", uv0)
uv0 = uv0.reshape(-1, 2)
G = 256
occ = np.zeros((G, G), bool)
ij = np.clip((uv0 * G).astype(int), 0, G - 1)
occ[ij[:, 1], ij[:, 0]] = True
# 膨脹 2 格
for _ in range(2):
    o2 = occ.copy()
    o2[1:, :] |= occ[:-1, :]; o2[:-1, :] |= occ[1:, :]
    o2[:, 1:] |= occ[:, :-1]; o2[:, :-1] |= occ[:, 1:]
    occ = o2
empty = np.argwhere(~occ)
assert len(empty) > 0, "no empty UV cell found"
# 取最靠近 (0,0) 角落的空位（角落最不可能被筆劃波及）
k = np.argmin(empty.sum(1))
PARK_UV0 = ((empty[k][1] + 0.5) / G, (empty[k][0] + 0.5) / G)
PARK_OTHER = (0.998, 0.002)
print(f"PARK_UV0 = ({PARK_UV0[0]:.4f}, {PARK_UV0[1]:.4f})")

# --- 術前複本（法線來源）---
src = body.copy()
src.data = body.data.copy()
src.name = "NormalSrcTemp"
bpy.context.scene.collection.objects.link(src)
src.modifiers.clear()
src.parent = None

# --- bmesh 手術 ---
bm = bmesh.new()
bm.from_mesh(me)
bm.verts.ensure_lookup_table()

def sdist(v):
    return ((v.co.x - PLANE_CO[0]) * PLANE_NO[0] +
            (v.co.y - PLANE_CO[1]) * PLANE_NO[1] +
            (v.co.z - PLANE_CO[2]) * PLANE_NO[2])

# 術前島數
def count_islands():
    seen = set()
    n = 0
    for v in bm.verts:
        if v.index in seen:
            continue
        n += 1
        stack = [v]
        seen.add(v.index)
        while stack:
            cur = stack.pop()
            for e in cur.link_edges:
                o = e.other_vert(cur)
                if o.index not in seen:
                    seen.add(o.index)
                    stack.append(o)
    return n

bm.verts.index_update()
isl_pre = count_islands()
print("islands pre:", isl_pre)

ret = bmesh.ops.bisect_plane(
    bm, geom=list(bm.verts) + list(bm.edges) + list(bm.faces),
    plane_co=PLANE_CO, plane_no=PLANE_NO, dist=1e-5,
    use_snap_center=False, clear_outer=False, clear_inner=False)
cut_edges = [g for g in ret["geom_cut"] if isinstance(g, bmesh.types.BMEdge)]
cut_verts = [g for g in ret["geom_cut"] if isinstance(g, bmesh.types.BMVert)]
print(f"cut ring: {len(cut_verts)} verts, {len(cut_edges)} edges")
assert len(cut_edges) >= 30, "cut ring unexpectedly small"

# 環必須是單一連通圈
ring_seen = set()
stack = [cut_verts[0]]
ring_seen.add(cut_verts[0])
while stack:
    cur = stack.pop()
    for e in cur.link_edges:
        if e in cut_edges or (e.verts[0] in ring_seen and False):
            pass
    for e in [ee for ee in cur.link_edges if ee in set(cut_edges)]:
        o = e.other_vert(cur)
        if o not in ring_seen:
            ring_seen.add(o)
            stack.append(o)
assert len(ring_seen) == len(cut_verts), f"ring not single loop: {len(ring_seen)}/{len(cut_verts)}"

# 切開
bmesh.ops.split_edges(bm, edges=cut_edges)

# 邊界邊 = 只掛一面的邊；按面重心側分上下
boundary = [e for e in bm.edges if len(e.link_faces) == 1]
def face_side(f):
    c = f.calc_center_median()
    return ((c.x - PLANE_CO[0]) * PLANE_NO[0] + (c.y - PLANE_CO[1]) * PLANE_NO[1] +
            (c.z - PLANE_CO[2]) * PLANE_NO[2])
up_edges = [e for e in boundary if face_side(e.link_faces[0]) > 0]
dn_edges = [e for e in boundary if face_side(e.link_faces[0]) <= 0]
print(f"boundary edges: up {len(up_edges)} dn {len(dn_edges)} (total {len(boundary)})")
assert len(up_edges) > 20 and len(dn_edges) > 20
assert len(boundary) == len(up_edges) + len(dn_edges)

# 封蓋
pre_faces = set(f.index for f in bm.faces)
bm.faces.index_update()
cap_up = bmesh.ops.triangle_fill(bm, use_beauty=True, use_dissolve=False, edges=up_edges)
cap_dn = bmesh.ops.triangle_fill(bm, use_beauty=True, use_dissolve=False, edges=dn_edges)
cap_up_faces = [g for g in cap_up["geom"] if isinstance(g, bmesh.types.BMFace)]
cap_dn_faces = [g for g in cap_dn["geom"] if isinstance(g, bmesh.types.BMFace)]
print(f"caps: up {len(cap_up_faces)} tris, dn {len(cap_dn_faces)} tris")
assert cap_up_faces and cap_dn_faces, "cap fill failed"
for f in cap_up_faces + cap_dn_faces:
    f.material_index = 0

# 每殼重算法線方向（閉殼向外）
bm.verts.index_update()
def islands_faces():
    seen = set()
    out = []
    for v in bm.verts:
        if v.index in seen:
            continue
        comp_v = []
        stack = [v]
        seen.add(v.index)
        while stack:
            cur = stack.pop()
            comp_v.append(cur)
            for e in cur.link_edges:
                o = e.other_vert(cur)
                if o.index not in seen:
                    seen.add(o.index)
                    stack.append(o)
        fs = set()
        for vv in comp_v:
            for f in vv.link_faces:
                fs.add(f)
        out.append((comp_v, list(fs)))
    return out

comps = islands_faces()
print("islands post:", len(comps))
assert len(comps) == isl_pre + 1, f"expected {isl_pre + 1} islands, got {len(comps)}"
for comp_v, comp_f in comps:
    bmesh.ops.recalc_face_normals(bm, faces=comp_f)

# 上殼判定：含最高頂點（髮髻）的那個殼
top_z = max(c[0][0].co.z for c in comps)  # placeholder
def comp_max_z(comp_v):
    return max(v.co.z for v in comp_v)
comps_sorted = sorted(comps, key=lambda c: comp_max_z(c[0]), reverse=True)
upper_verts = set(v.index for v in comps_sorted[0][0])
print(f"upper shell verts {len(upper_verts)}, max z {comp_max_z(comps_sorted[0][0]):.3f}, "
      f"min z {min(v.co.z for v in comps_sorted[0][0]):.3f}")
# 上殼不得含身體：下界檢查（環最低點 z≈1.32）
assert min(v.co.z for v in comps_sorted[0][0]) > 1.28, "upper shell leaks below ring"

cap_up_vset = set()
for f in cap_up_faces:
    for v in f.verts:
        cap_up_vset.add(v.index)
cap_dn_vset = set()
for f in cap_dn_faces:
    for v in f.verts:
        cap_dn_vset.add(v.index)

# UV／頂點色：蓋面 loop 停泊
bm.verts.index_update()
uv_layers = {nm: bm.loops.layers.uv.get(nm) for nm in ("UVMap", "FaceUV", "HairUV")}
col_layer = bm.loops.layers.color.get("FaceMask") or bm.loops.layers.float_color.get("FaceMask")
for f in cap_up_faces + cap_dn_faces:
    for loop in f.loops:
        loop[uv_layers["UVMap"]].uv = PARK_UV0
        if uv_layers["FaceUV"]:
            loop[uv_layers["FaceUV"]].uv = PARK_OTHER
        if uv_layers["HairUV"]:
            loop[uv_layers["HairUV"]].uv = PARK_OTHER
        if col_layer:
            loop[col_layer] = (0.0, 0.0, 0.0, 1.0)

bm.to_mesh(me)
bm.free()
me.update()
post_counts = (len(me.vertices), len(me.edges), len(me.polygons), len(me.loops))
print("POST:", post_counts)

# --- 權重重整 ---
deform_names = set(b.name for b in arm.data.bones)
gidx = {g.name: g.index for g in body.vertex_groups}
deform_idx = set(gidx[n] for n in deform_names if n in gidx)
head_g = body.vertex_groups["Head"]
spine1_g = body.vertex_groups["Spine1"]
neck_i, head_i, spine1_i = gidx["Neck"], gidx["Head"], gidx["Spine1"]

n_upper_fixed = n_lower_stripped = 0
for v in me.vertices:
    vi = v.index
    if vi in upper_verts:
        # 上殼（含上蓋）：剛體 Head=1，其餘 deform 群組清空（MARK_* 不動）
        for g in list(v.groups):
            if g.group in deform_idx and g.group != head_i:
                body.vertex_groups[g.group].remove([vi])
        head_g.add([vi], 1.0, 'REPLACE')
        n_upper_fixed += 1
    else:
        # 下殼：剝除 Neck/Head，餘量歸一化；無餘量 → Spine1
        w_neck = 0.0
        w_rest = 0.0
        for g in v.groups:
            if g.group in (neck_i, head_i):
                w_neck += g.weight
            elif g.group in deform_idx:
                w_rest += g.weight
        if w_neck > 1e-6:
            body.vertex_groups[neck_i].remove([vi])
            body.vertex_groups[head_i].remove([vi])
            if w_rest > 1e-6:
                s = (w_rest + w_neck) / w_rest
                for g in v.groups:
                    if g.group in deform_idx:
                        body.vertex_groups[g.group].add([vi], g.weight * s, 'REPLACE')
            else:
                spine1_g.add([vi], 1.0, 'REPLACE')
            n_lower_stripped += 1
print(f"weights: upper rigid {n_upper_fixed}, lower stripped {n_lower_stripped}")

# --- 縫區法線移植（rest 姿勢零視覺差；蓋面除外）---
seam_g = body.vertex_groups.new(name="TMP_NeckSeamN")
cap_all = cap_up_vset | cap_dn_vset
seam_ids = []
for v in me.vertices:
    if v.index in cap_all:
        continue
    dd = ((v.co.x - PLANE_CO[0]) * PLANE_NO[0] + (v.co.y - PLANE_CO[1]) * PLANE_NO[1] +
          (v.co.z - PLANE_CO[2]) * PLANE_NO[2])
    if abs(dd) < 0.04:
        seam_ids.append(v.index)
seam_g.add(seam_ids, 1.0, 'REPLACE')
print(f"seam normal verts: {len(seam_ids)}")

bpy.context.view_layer.objects.active = body
mod = body.modifiers.new("SeamNormalXfer", 'DATA_TRANSFER')
mod.object = src
mod.use_loop_data = True
mod.data_types_loops = {'CUSTOM_NORMAL'}
mod.loop_mapping = 'POLYINTERP_NEAREST'
mod.vertex_group = "TMP_NeckSeamN"
bpy.ops.object.modifier_move_to_index(modifier=mod.name, index=0)
bpy.ops.object.modifier_apply(modifier=mod.name)
body.vertex_groups.remove(body.vertex_groups["TMP_NeckSeamN"])

# 刪除術前複本
bpy.data.objects.remove(src, do_unlink=True)

# --- 收尾驗證 ---
assert [l.name for l in me.uv_layers] == uv_names_pre
assert sorted(o.name for o in bpy.data.objects if o.type == 'MESH') == ["Fundoshi", "SumoRetopo"]
bpy.ops.wm.save_mainfile()
print("SURGERY_DONE saved", MASTER)
print(f"SUMMARY ring_verts={len(cut_verts)} caps_up={len(cap_up_faces)} caps_dn={len(cap_dn_faces)} "
      f"park_uv0=({PARK_UV0[0]:.4f},{PARK_UV0[1]:.4f}) plane_co={PLANE_CO} theta_deg=20")
