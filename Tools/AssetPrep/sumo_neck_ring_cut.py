# 頭身切開手術 v4（user 2026-07-17 二度重標：分界線改 cut_seam_head3）
# 切線＝user 手標 vertex group "cut_seam_head3"（sumo_retopo_base16.blend，84 頂點閉環，
# 前緣壓下顎垂肉下 z≈1.29、後緣 z≈1.55）。重標目的＝整條線壓到髮域之下：
# 環上 MARK_Hair 頂點 0 個、線下實繪髮罩頂點 0 個（探針實測）——黑髮殘留與
# 端色採髮條灰邊的白汙染兩個問題在結構上同時消滅。其餘機制與 v2/v3 完全相同。
# 與 v1（sumo_neck_cut.py 平面 bisect）的差異：
#   - 不 bisect：沿 seam 的 N 條既有邊 split_edges，零新增幾何、UV 完全不動
#   - 不封蓋：頭身之間由運行時每幀生成的脖子區（UNeckStretch）永遠銜接，內部永不露出
#   - 身殼權重：Neck/Head 的質量「轉移」給 Spine1（不是歸一化——歸一化會讓縫區被肩膀支配）
#   - 輸出邊界環資料 SourceAssets/neck_seam_rings.json（引擎 mini-skin ＋端色烘焙的原料）
# 前置：master.blend 必須是術前版本（git checkout e7802a2~1 -- 該檔；11847 頂點）。
# Run: blender --background --python sumo_neck_ring_cut.py
import bpy
import bmesh
import json
import collections
import numpy as np
from mathutils import Vector
from mathutils.kdtree import KDTree

SEAM_BLEND = r"C:/games/Unreal Engine/nice_ink/SourceAssets/sumo_retopo_base16.blend"
SEAM_GROUP = "cut_seam_head3"
SEAM_N = 84  # 探針量測定值（2026-07-17：84 頂點完美閉環、全 w=1.0、每點恰 2 鄰）
MASTER = r"C:/games/Unreal Engine/nice_ink/SourceAssets/sumo_character_master.blend"
OUT_JSON = r"C:/games/Unreal Engine/nice_ink/SourceAssets/neck_seam_rings.json"
NORMAL_BAND = 0.04  # 縫區法線移植帶（距 seam 環 4cm）

# ---------- 1) base15：取有序 seam 環（世界座標） ----------
bpy.ops.wm.open_mainfile(filepath=SEAM_BLEND)
t14 = bpy.data.objects["SumoRetopo"]
me14 = t14.data
mw14 = t14.matrix_world
gi14 = t14.vertex_groups[SEAM_GROUP].index
S14 = set()
for v in me14.vertices:
    for g in v.groups:
        if g.group == gi14:
            S14.add(v.index)
assert len(S14) == SEAM_N, f"seam group size {len(S14)} != {SEAM_N}"
adj14 = collections.defaultdict(list)
for e in me14.edges:
    a, b = e.vertices[0], e.vertices[1]
    if a in S14 and b in S14:
        adj14[a].append(b)
        adj14[b].append(a)
start = next(iter(S14))
order14 = [start]
prev, cur = None, start
while True:
    nxt = [w for w in adj14[cur] if w != prev]
    if not nxt or nxt[0] == start:
        break
    prev, cur = cur, nxt[0]
    order14.append(cur)
assert len(order14) == SEAM_N, "seam ring walk incomplete"
seam_world = [tuple(mw14 @ me14.vertices[i].co) for i in order14]

# seam 最佳擬合平面法線（Newell，翻到朝頭側 z>0）——定向與 json 記錄用
_newell = Vector((0.0, 0.0, 0.0))
for k in range(SEAM_N):
    p = Vector(seam_world[k])
    q = Vector(seam_world[(k + 1) % SEAM_N])
    _newell.x += (p.y - q.y) * (p.z + q.z)
    _newell.y += (p.z - q.z) * (p.x + q.x)
    _newell.z += (p.x - q.x) * (p.y + q.y)
n_plane = _newell.normalized()
if n_plane.z < 0:
    n_plane = -n_plane
print(f"seam plane n = ({n_plane.x:.4f}, {n_plane.y:.4f}, {n_plane.z:.4f})")

# ---------- 2) master：前置斷言 ----------
bpy.ops.wm.open_mainfile(filepath=MASTER)
if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')
body = bpy.data.objects["SumoRetopo"]
arm = bpy.data.objects["Skeleton_Plus-size"]
me = body.data
assert sorted(o.name for o in bpy.data.objects if o.type == 'MESH') == ["Fundoshi", "SumoRetopo"]
assert me.shape_keys is None
mw = np.array(body.matrix_world)
assert np.allclose(mw, np.eye(4), atol=1e-6), "matrix_world not identity"
assert len(me.vertices) == 11847, f"master not pre-cut: {len(me.vertices)} verts (expect 11847)"
uv_names_pre = [l.name for l in me.uv_layers]
assert set(uv_names_pre) >= {"UVMap", "FaceUV", "HairUV"}
pre_counts = (len(me.vertices), len(me.edges), len(me.polygons), len(me.loops))
print("PRE:", pre_counts)

# seam 環 → master 頂點索引（逐點位精確對應）
kt = KDTree(len(me.vertices))
for i, v in enumerate(me.vertices):
    kt.insert(v.co, i)
kt.balance()
ring_m = []
for c in seam_world:
    co, idx, d = kt.find(Vector(c))
    assert d < 1e-6, f"seam vert {c} unmatched (d={d})"
    ring_m.append(idx)
assert len(set(ring_m)) == SEAM_N

# ---------- 3) 術前複本（法線來源） ----------
src = body.copy()
src.data = body.data.copy()
src.name = "NormalSrcTemp"
bpy.context.scene.collection.objects.link(src)
src.modifiers.clear()
src.parent = None

# ---------- 4) bmesh：沿 seam 環 split ----------
bm = bmesh.new()
bm.from_mesh(me)
bm.verts.ensure_lookup_table()
ring_edges = []
for k in range(SEAM_N):
    a = bm.verts[ring_m[k]]
    b = bm.verts[ring_m[(k + 1) % SEAM_N]]
    e = next((ee for ee in a.link_edges if ee.other_vert(a) is b), None)
    assert e is not None, f"ring edge {k} missing"
    ring_edges.append(e)
assert len(set(ring_edges)) == SEAM_N

def count_islands(bm_):
    bm_.verts.index_update()
    seen = set()
    n = 0
    for v in bm_.verts:
        if v.index in seen:
            continue
        n += 1
        stack = [v]
        seen.add(v.index)
        while stack:
            cur_ = stack.pop()
            for e_ in cur_.link_edges:
                o = e_.other_vert(cur_)
                if o.index not in seen:
                    seen.add(o.index)
                    stack.append(o)
    return n

isl_pre = count_islands(bm)
print("islands pre:", isl_pre)
bmesh.ops.split_edges(bm, edges=ring_edges)
assert len(bm.verts) == 11847 + SEAM_N, f"split did not duplicate ring: {len(bm.verts)}"
isl_post = count_islands(bm)
print("islands post:", isl_post)
assert isl_post == isl_pre + 1, f"expected {isl_pre + 1} islands, got {isl_post}"
bm.to_mesh(me)
bm.free()
me.update()
post_counts = (len(me.vertices), len(me.edges), len(me.polygons), len(me.loops))
print("POST:", post_counts)
assert post_counts[0] == pre_counts[0] + SEAM_N
assert post_counts[2] == pre_counts[2]          # 面數不變（無封蓋）
assert post_counts[3] == pre_counts[3]          # loop 數不變 ⇒ UV 逐 loop 原樣

# ---------- 5) 殼判定（頭殼＝含最高頂點的島） ----------
vadj = collections.defaultdict(list)
for e in me.edges:
    a, b = e.vertices[0], e.vertices[1]
    vadj[a].append(b)
    vadj[b].append(a)
comp_id = {}
cid = 0
for s0 in range(len(me.vertices)):
    if s0 in comp_id:
        continue
    q = [s0]
    comp_id[s0] = cid
    while q:
        u = q.pop()
        for w in vadj[u]:
            if w not in comp_id:
                comp_id[w] = cid
                q.append(w)
    cid += 1
top_vert = max(range(len(me.vertices)), key=lambda i: me.vertices[i].co.z)
head_cid = comp_id[top_vert]
head_set = set(i for i, c in comp_id.items() if c == head_cid)
print(f"head shell: {len(head_set)} verts, z[{min(me.vertices[i].co.z for i in head_set):.4f}, "
      f"{max(me.vertices[i].co.z for i in head_set):.4f}]")
# 頭殼語義不變量（v3：環數不再 72，改用結構檢查取代硬編頂點數）：
#   - 不得洩漏到 seam 最低點以下（前緣 z≈1.293）
#   - 臉部（MARK_Face 權重 >0.5）必須整組在頭殼——臉是頭殼的定義性內容
assert min(me.vertices[i].co.z for i in head_set) > 1.28, "head shell leaks below seam"
ktr_seam = KDTree(SEAM_N)
for i in range(SEAM_N):
    ktr_seam.insert(Vector(seam_world[i]), i)
ktr_seam.balance()
gi_face = body.vertex_groups["MARK_Face"].index
face_verts = set()
for v in me.vertices:
    for g in v.groups:
        if g.group == gi_face and g.weight > 0.5:
            face_verts.add(v.index)
assert face_verts, "MARK_Face group empty?"
# 例外：seam 環頂點 split 後身側複本保留原頂點組——縫若壓過臉權重羽化帶，
# 其身側複本合法落在身殼；非環位置的臉頂點仍必須全在頭殼。
outside = face_verts - head_set
for vi in outside:
    co, idx, d = ktr_seam.find(me.vertices[vi].co)
    assert d < 1e-6, f"non-seam face vert {vi} outside head shell (d={d})"
print(f"face verts: {len(face_verts)} total, {len(outside)} body-side seam duplicates (ok)")
assert 1000 < len(head_set) < 2500, f"head shell size implausible: {len(head_set)}"

# ---------- 6) 權重：頭殼剛體 Head=1；身殼 Neck/Head 質量→Spine1 ----------
deform_names = set(b.name for b in arm.data.bones)
gidx = {g.name: g.index for g in body.vertex_groups}
deform_idx = set(gidx[n] for n in deform_names if n in gidx)
head_g = body.vertex_groups["Head"]
spine1_g = body.vertex_groups["Spine1"]
neck_i, head_i, spine1_i = gidx["Neck"], gidx["Head"], gidx["Spine1"]
n_upper = n_lower = 0
for v in me.vertices:
    vi = v.index
    if vi in head_set:
        for g in list(v.groups):
            if g.group in deform_idx and g.group != head_i:
                body.vertex_groups[g.group].remove([vi])
        head_g.add([vi], 1.0, 'REPLACE')
        n_upper += 1
    else:
        w_nh = 0.0
        w_sp = 0.0
        for g in v.groups:
            if g.group in (neck_i, head_i):
                w_nh += g.weight
            elif g.group == spine1_i:
                w_sp = g.weight
        if w_nh > 1e-6:
            body.vertex_groups[neck_i].remove([vi])
            body.vertex_groups[head_i].remove([vi])
            spine1_g.add([vi], w_sp + w_nh, 'REPLACE')
            n_lower += 1
print(f"weights: head-rigid {n_upper}, body Neck/Head->Spine1 transferred {n_lower}")

# 驗證：身殼 Neck/Head 權重歸零、全頂點權重和=1
for v in me.vertices:
    tot = 0.0
    for g in v.groups:
        if g.group in deform_idx:
            tot += g.weight
        if v.index not in head_set:
            assert g.group not in (neck_i, head_i) or g.weight == 0.0, \
                f"body vert {v.index} still has Neck/Head weight"
    assert abs(tot - 1.0) < 1e-3, f"vert {v.index} weight sum {tot}"

# ---------- 7) 縫區法線移植（rest 零視覺差） ----------
ring_pos = [Vector(seam_world[k]) for k in range(SEAM_N)]
ktr = KDTree(SEAM_N)
for i, p in enumerate(ring_pos):
    ktr.insert(p, i)
ktr.balance()
band_ids = []
for v in me.vertices:
    co, idx, d = ktr.find(v.co)
    if d < NORMAL_BAND:
        band_ids.append(v.index)
tmp_g = body.vertex_groups.new(name="TMP_NeckSeamN")
tmp_g.add(band_ids, 1.0, 'REPLACE')
print(f"normal band verts: {len(band_ids)}")
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
bpy.data.objects.remove(src, do_unlink=True)

# ---------- 8) 邊界環資料輸出 ----------
me.update()
# 邊界邊＝只掛一面的邊
efc = collections.Counter()
for p in me.polygons:
    for ek in p.edge_keys:
        efc[tuple(sorted(ek))] += 1
badj = collections.defaultdict(list)
nb = 0
for (a, b), c in efc.items():
    if c == 1:
        badj[a].append(b)
        badj[b].append(a)
        nb += 1
assert nb == 2 * SEAM_N, f"boundary edges {nb} != {2 * SEAM_N}"

def walk_ring(members):
    s0 = min(members)
    out = [s0]
    prev, cur = None, s0
    while True:
        nxt = [w for w in badj[cur] if w != prev and w in members]
        if not nxt or nxt[0] == s0:
            break
        prev, cur = cur, nxt[0]
        out.append(cur)
    assert len(out) == len(members), f"ring walk {len(out)}/{len(members)}"
    return out

bverts = set(badj.keys())
head_ring = walk_ring([i for i in bverts if i in head_set])
body_ring = walk_ring([i for i in bverts if i not in head_set])
assert len(head_ring) == SEAM_N and len(body_ring) == SEAM_N

# 定向：以 seam 最佳擬合平面法線 n（朝頭側）看下去 CCW；起點＝最前緣（min y）
def orient(ring):
    n = len(ring)
    k0 = min(range(n), key=lambda k: me.vertices[ring[k]].co.y)
    ring = ring[k0:] + ring[:k0]
    newell = Vector((0.0, 0.0, 0.0))
    for k in range(n):
        p = me.vertices[ring[k]].co
        q = me.vertices[ring[(k + 1) % n]].co
        newell.x += (p.y - q.y) * (p.z + q.z)
        newell.y += (p.z - q.z) * (p.x + q.x)
        newell.z += (p.x - q.x) * (p.y + q.y)
    if newell.dot(n_plane) < 0:
        ring = [ring[0]] + ring[1:][::-1]
    return ring

head_ring = orient(head_ring)
body_ring = orient(body_ring)
# 配對：body_ring 重排到與 head_ring 逐點同位（rest 時兩環重合）
pos2body = {tuple(round(c, 7) for c in me.vertices[i].co): i for i in body_ring}
body_ring = [pos2body[tuple(round(c, 7) for c in me.vertices[i].co)] for i in head_ring]
for k in range(SEAM_N):
    assert (me.vertices[head_ring[k]].co - me.vertices[body_ring[k]].co).length < 1e-9

# 每頂點：own-side loop 的 UV0/HairUV（可能跨島→列全部 distinct）＋corner normal 平均
poly_side = {}
for p in me.polygons:
    poly_side[p.index] = (comp_id[me.loops[p.loop_start].vertex_index] == head_cid)
uv0_l = me.uv_layers["UVMap"].data
uvh_l = me.uv_layers["HairUV"].data
vert_loops = collections.defaultdict(list)   # vert -> [(loop_idx, is_head_side)]
for p in me.polygons:
    for li in range(p.loop_start, p.loop_start + p.loop_total):
        vert_loops[me.loops[li].vertex_index].append((li, poly_side[p.index]))
cn = me.corner_normals

def gw(vi, name):
    g = body.vertex_groups.get(name)
    if g is None:
        return 0.0
    for gg in me.vertices[vi].groups:
        if gg.group == g.index:
            return gg.weight
    return 0.0

def ring_data(ring, is_head):
    out = []
    for vi in ring:
        loops = [li for (li, side) in vert_loops[vi] if side == is_head]
        assert loops, f"ring vert {vi} has no own-side loops"
        nrm = Vector((0.0, 0.0, 0.0))
        uvset = {}
        for li in loops:
            nrm += Vector(cn[li].vector)
            key = (round(uv0_l[li].uv[0], 5), round(uv0_l[li].uv[1], 5))
            uvset.setdefault(key, [uv0_l[li].uv[:], uvh_l[li].uv[:], 0])
            uvset[key][2] += 1
        nrm.normalize()
        wlist = []
        for gg in me.vertices[vi].groups:
            nm = {g.index: g.name for g in body.vertex_groups}[gg.group]
            if nm in deform_names and gg.weight > 1e-4:
                wlist.append([nm, round(gg.weight, 6)])
        wlist.sort(key=lambda x: -x[1])
        co = me.vertices[vi].co
        out.append({
            "v": vi,
            "pos": [round(co.x, 7), round(co.y, 7), round(co.z, 7)],
            "nrm": [round(nrm.x, 6), round(nrm.y, 6), round(nrm.z, 6)],
            "w": wlist,
            "uv": [{"uv0": [round(u[0][0], 6), round(u[0][1], 6)],
                    "hairuv": [round(u[1][0], 6), round(u[1][1], 6)],
                    "n": u[2]} for u in uvset.values()],
            "hairW": round(gw(vi, "MARK_Hair"), 4),
            "faceW": round(gw(vi, "MARK_Face"), 4),
        })
    return out

data = {
    "note": "neck seam rings (Blender mesh space, meters, char front=-Y). "
            "UE component space = (x, -y, z) * 100.",
    "seam_source": f"{SEAM_GROUP} @ {SEAM_BLEND.split('/')[-1]}",
    "count": SEAM_N,
    "plane_n": [round(n_plane.x, 4), round(n_plane.y, 4), round(n_plane.z, 4)],
    "head_ring": ring_data(head_ring, True),
    "body_ring": ring_data(body_ring, False),
}
with open(OUT_JSON, "w", encoding="utf-8") as f:
    json.dump(data, f, indent=1)
print("JSON ->", OUT_JSON)

# ---------- 9) 收尾 ----------
assert [l.name for l in me.uv_layers] == uv_names_pre
assert sorted(o.name for o in bpy.data.objects if o.type == 'MESH') == ["Fundoshi", "SumoRetopo"]
bpy.ops.wm.save_mainfile()
zs = [me.vertices[i].co.z for i in head_ring]
print(f"SURGERY_DONE verts={len(me.vertices)} head_shell={len(head_set)} "
      f"ring_z[{min(zs):.3f},{max(zs):.3f}] normal_band={len(band_ids)}")
