# 唯讀導出：master 現有 HairUV（島版）之髮面＋帶面三角表（烘焙一律以存檔後的
# master 重新導出——當機重試曾留下 json 與 master 來自不同輪展開的分裂狀態）
import bpy, json, math

MASTER = r"C:\games\Unreal Engine\nice_ink\SourceAssets\sumo_character_master.blend"
OUT = r"C:\Users\willi\AppData\Local\Temp\claude\c--games-Unreal-Engine-nice-ink\00d214a0-ef56-4e54-9786-cb494e647707\scratchpad\hair_tris_hairuv.json"

bpy.ops.wm.open_mainfile(filepath=MASTER)
body = bpy.data.objects["SumoRetopo"]
me = body.data
mw = body.matrix_world
g = body.vertex_groups["MARK_Hair"]
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
    zs_ = [(mw @ me.vertices[v].co).z for v in comp]
    if len(comp) < 400 and min(zs_) > 1.40:
        marked.update(comp)
for it in range(60):
    zipped = [v for v in list(unmarked - marked)
              if adj.get(v) and (mw @ me.vertices[v].co).z > 1.45
              and sum(1 for nb in adj[v] if nb in marked) / len(adj[v]) >= 0.70]
    if not zipped:
        break
    marked.update(zipped)
face_ids = {p.index for p in me.polygons
            if all(me.loops[li].vertex_index in marked for li in p.loop_indices)}
edge_to_faces = {}
for p in me.polygons:
    for ek in p.edge_keys:
        edge_to_faces.setdefault(tuple(sorted(ek)), []).append(p.index)
bverts = set()
for ek, fids in edge_to_faces.items():
    flags = [fi in face_ids for fi in fids]
    if any(flags) and not all(flags):
        bverts.add(ek[0]); bverts.add(ek[1])
bpos = [mw @ me.vertices[i].co for i in bverts]
band = set()
for p in me.polygons:
    if p.index in face_ids:
        continue
    for li in p.loop_indices:
        w = mw @ me.vertices[me.loops[li].vertex_index].co
        if any((w - b).length < 0.030 for b in bpos):
            band.add(p.index)
            break
sel_ids = face_ids | band

huv = me.uv_layers["HairUV"]
me.calc_loop_triangles()
tris = []
for tri in me.loop_triangles:
    if tri.polygon_index not in sel_ids:
        continue
    uvr, pr = [], []
    for li in tri.loops:
        u, v = huv.data[li].uv
        uvr += [u, v]
        p = mw @ me.vertices[me.loops[li].vertex_index].co
        pr += [p.x, p.y, p.z]
    tris.append({"uv": uvr, "pos": pr})
print(f"DUMPED tris={len(tris)} (hair={len(face_ids)} band={len(band)})")
with open(OUT, "w") as f:
    json.dump({"tris": tris}, f)
