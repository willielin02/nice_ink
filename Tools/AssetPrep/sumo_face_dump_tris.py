# 臉區三角表導出（UV0 空間平滑臉罩用）：MARK_Face 面＋髮際帶面＋邊界段＋禁區
import bpy, json
from mathutils import Vector

MASTER = r"C:\games\Unreal Engine\nice_ink\SourceAssets\sumo_character_master.blend"
OUT = r"C:\Users\willi\AppData\Local\Temp\claude\c--games-Unreal-Engine-nice-ink\00d214a0-ef56-4e54-9786-cb494e647707\scratchpad\face_tris.json"

bpy.ops.wm.open_mainfile(filepath=MASTER)
body = bpy.data.objects["SumoRetopo"]
me = body.data
mw = body.matrix_world
g = body.vertex_groups["MARK_Face"]
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
print(f"FACE faces={len(face_set)}")

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
