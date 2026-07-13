# 區域三角表導出（sumo_face_dump_tris 參數化通用版）：
#   argv（-- 之後）: <MARK_群組名> <輸出json>
#   輸出＝區域面＋邊界帶面＋邊界段＋禁區 UV → 餵 sumo_bake_region_mask.py
import bpy, json, sys
from mathutils import Vector

MASTER = r"C:\games\Unreal Engine\nice_ink\SourceAssets\sumo_character_master.blend"
argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
GROUP = argv[0] if len(argv) > 0 else "MARK_Face"
OUT = argv[1] if len(argv) > 1 else r"C:\Users\willi\AppData\Local\Temp\claude\c--games-Unreal-Engine-nice-ink\00d214a0-ef56-4e54-9786-cb494e647707\scratchpad\region_tris.json"

bpy.ops.wm.open_mainfile(filepath=MASTER)
body = bpy.data.objects["SumoRetopo"]
me = body.data
mw = body.matrix_world
g = body.vertex_groups[GROUP]
marked = set()
for v in me.vertices:
    for ge in v.groups:
        if ge.group == g.index and ge.weight > 0.5:
            marked.add(v.index)
adj = {}
for e in me.edges:
    a, b = e.vertices
    adj.setdefault(a, []).append(b)
    adj.setdefault(b, []).append(a)
# 針孔補洞：未標分量若「有鄰接已標頂點」且小 → 收編（鄰接 guard 天然排除獨立殼）
unmarked = set(range(len(me.vertices))) - marked
seen = set(); comps = []
for v0 in unmarked:
    if v0 in seen:
        continue
    stack = [v0]; comp = [v0]; seen.add(v0)
    while stack:
        v = stack.pop()
        for nb in adj.get(v, ()):
            if nb in unmarked and nb not in seen:
                seen.add(nb); stack.append(nb); comp.append(nb)
    comps.append(comp)
comps.sort(key=len, reverse=True)
for comp in comps[1:]:
    touches = any(nb in marked for v in comp for nb in adj.get(v, ()))
    if len(comp) < 400 and touches:
        marked.update(comp)
for it in range(60):
    zipped = [v for v in list(unmarked - marked)
              if adj.get(v) and sum(1 for nb in adj[v] if nb in marked) / len(adj[v]) >= 0.70]
    if not zipped:
        break
    marked.update(zipped)

uv0 = me.uv_layers["UVMap"]
me.calc_loop_triangles()
face_set = {p.index for p in me.polygons
            if all(me.loops[li].vertex_index in marked for li in p.loop_indices)}
print(f"FACE faces={len(face_set)} (raw)")

# --- 面級清理（user 標記常有游離雜面＋區內漏標洞，MARK_Hair v8004 前例）---
# 面鄰接（共邊）
fadj = {}
_e2f = {}
for p in me.polygons:
    for ek in p.edge_keys:
        _e2f.setdefault(tuple(sorted(ek)), []).append(p.index)
for fids in _e2f.values():
    for a in fids:
        for b in fids:
            if a != b:
                fadj.setdefault(a, set()).add(b)

def face_comps(fset):
    seen, comps = set(), []
    for f0 in fset:
        if f0 in seen:
            continue
        stack, comp = [f0], {f0}
        seen.add(f0)
        while stack:
            f = stack.pop()
            for nb in fadj.get(f, ()):
                if nb in fset and nb not in seen:
                    seen.add(nb); stack.append(nb); comp.add(nb)
        comps.append(comp)
    return sorted(comps, key=len, reverse=True)

# 只丟真雜點（<20 面）——大分量都是區域本體（褌=腰帶+擋布兩塊面級不相連，砍大分量會砍掉半件）
comps_f = face_comps(face_set)
for c in comps_f:
    if len(c) < 20:
        face_set -= c
        print(f"STRAY dropped: {len(c)} faces")
# 橋接：未標面的鄰居橫跨 ≥2 個已標分量 → 收編（兩塊布帶之間細縫的拓樸簽名；
# 分量合一後自然收斂——不能用「已標鄰居比例」門檻，凹角外皮面同樣 2/4 會無限爬行膨脹）
while True:
    comps_now = face_comps(face_set)
    if len(comps_now) <= 1:
        break
    comp_of = {}
    for ci, c in enumerate(comps_now):
        for f in c:
            comp_of[f] = ci
    bridge = [f for f in (set(range(len(me.polygons))) - face_set)
              if len({comp_of[nb] for nb in fadj.get(f, ()) if nb in comp_of}) >= 2]
    if not bridge:
        break
    face_set.update(bridge)
    print(f"BRIDGE: +{len(bridge)} faces ({len(comps_now)} comps)")
# 區內洞吸收（橋接合體後，原本經縫隙連外的洞現在才真正被包圍）
hole_comps = face_comps({p.index for p in me.polygons} - face_set)
for comp in hole_comps[1:]:   # [0]=全身外部
    nbrs = {nb for f in comp for nb in fadj.get(f, ())} - comp
    if len(comp) < 40 and nbrs and nbrs <= face_set:   # nbrs 非空：獨立殼(乳頭等)無鄰居，不可誤收
        face_set |= comp
        print(f"HOLE absorbed: {len(comp)} faces")
print(f"FACE faces={len(face_set)} (clean)")

edge_to_faces = {}
for p in me.polygons:
    for ek in p.edge_keys:
        edge_to_faces.setdefault(tuple(sorted(ek)), []).append(p.index)
boundary_segs = []
bverts = set()
for ek, fids in edge_to_faces.items():
    flags = [fi in face_set for fi in fids]
    if any(flags) and not all(flags):
        a = mw @ me.vertices[ek[0]].co
        b = mw @ me.vertices[ek[1]].co
        boundary_segs.append([a.x, a.y, a.z, b.x, b.y, b.z])
        bverts.add(ek[0]); bverts.add(ek[1])
print(f"BOUNDARY segs={len(boundary_segs)}")
bpos = [mw @ me.vertices[i].co for i in bverts]
band = set()
for p in me.polygons:
    if p.index in face_set:
        continue
    for li in p.loop_indices:
        w = mw @ me.vertices[me.loops[li].vertex_index].co
        if any((w - b).length < 0.030 for b in bpos):
            band.add(p.index)
            break

def tri_rows(pred):
    rows = []
    for tri in me.loop_triangles:
        if not pred(tri.polygon_index):
            continue
        uvr, pr = [], []
        for li in tri.loops:
            u, v = uv0.data[li].uv
            uvr += [u, v]
            p = mw @ me.vertices[me.loops[li].vertex_index].co
            pr += [p.x, p.y, p.z]
        rows.append({"uv": uvr, "pos": pr})
    return rows

tris = tri_rows(lambda fi: fi in face_set)
band_tris = tri_rows(lambda fi: fi in band)
other_uv = []
for tri in me.loop_triangles:
    if tri.polygon_index in face_set:
        continue
    row = []
    for li in tri.loops:
        u, v = uv0.data[li].uv
        row += [u, v]
    other_uv.append(row)
print(f"BAND faces={len(band)} tris={len(band_tris)} OTHER={len(other_uv)}")
with open(OUT, "w") as f:
    json.dump({"tris": tris, "band_tris": band_tris,
               "boundary_segs": boundary_segs, "other_uv": other_uv}, f)
print("DUMPED", OUT)
