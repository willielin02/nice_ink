# 髮區三角表導出：UVMap 座標＋3D 位置＋髻中心/頭中心（給 venv python 烘髮色貼圖）
import bpy, json
from mathutils import Vector

MASTER = r"C:\games\Unreal Engine\nice_ink\SourceAssets\sumo_character_master.blend"
OUT = r"C:\Users\willi\AppData\Local\Temp\claude\c--games-Unreal-Engine-nice-ink\00d214a0-ef56-4e54-9786-cb494e647707\scratchpad\hair_tris.json"

bpy.ops.wm.open_mainfile(filepath=MASTER)
body = bpy.data.objects["SumoRetopo"]
me = body.data
# UV0 已在上一輪 reunwrap 並存進 master（最終版），這裡直接用，不再重展
mw = body.matrix_world
g = body.vertex_groups["MARK_Hair"]
marked = set()
for v in me.vertices:
    for ge in v.groups:
        if ge.group == g.index and ge.weight > 0.5:
            marked.add(v.index)

uv0 = me.uv_layers["UVMap"]
me.calc_loop_triangles()

# 頂點級補洞：user 標記留下的裂縫＝一串漏標頂點（每個漏標頂點讓周圍整圈
# 四邊形出局，面級補洞的「≥3 鄰面在髮區」永遠達不到）。
# 未標頂點做連通分量：最大分量＝全身皮膚；其餘＝被髮區包圍的針孔 → 收編。
adj = {}
for e in me.edges:
    a, b = e.vertices
    adj.setdefault(a, []).append(b)
    adj.setdefault(b, []).append(a)
unmarked = set(range(len(me.vertices))) - marked
comps = []
seen = set()
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
print(f"UNMARKED comps={len(comps)} sizes(top10)={[len(c) for c in comps[:10]]}")
for comp in comps[1:]:
    zs_ = [(mw @ me.vertices[v].co).z for v in comp]
    tag = "FILL" if (len(comp) < 400 and min(zs_) > 1.40) else "SKIP(guard)"
    print(f"  comp size={len(comp)} z[{min(zs_):.3f},{max(zs_):.3f}] {tag}")
    if tag == "FILL":
        marked.update(comp)

# 拉鏈式補縫：主分量裡伸進髮區的細裂縫（一端連著皮膚的漏標頂點鏈）。
# 未標頂點若鄰居 ≥70% 已標 → 收編；端點先被吃掉後沿鏈傳播。限 z>1.45 護體。
for it in range(60):
    zipped = []
    for v in list(unmarked - marked):
        nbs = adj.get(v, ())
        if not nbs or (mw @ me.vertices[v].co).z <= 1.45:
            continue
        n_marked = sum(1 for nb in nbs if nb in marked)
        if n_marked / len(nbs) >= 0.70:
            zipped.append(v)
    if not zipped:
        print(f"ZIPPER done at pass{it}")
        break
    marked.update(zipped)
    print(f"ZIPPER pass{it}: +{len(zipped)}")

face_set = set()
for p in me.polygons:
    if all(me.loops[li].vertex_index in marked for li in p.loop_indices):
        face_set.add(p.index)
edge_to_faces = {}
for p in me.polygons:
    for ek in p.edge_keys:
        edge_to_faces.setdefault(tuple(sorted(ek)), []).append(p.index)

tris = []
hair_pts = []
for tri in me.loop_triangles:
    if tri.polygon_index not in face_set:
        continue
    row_uv = []
    row_p = []
    for li in tri.loops:
        u, v = uv0.data[li].uv
        row_uv += [u, v]
        p = mw @ me.vertices[me.loops[li].vertex_index].co
        row_p += [p.x, p.y, p.z]
        hair_pts.append((p.x, p.y, p.z))
    tris.append({"uv": row_uv, "pos": row_p})

# 髻中心＝髮區最高 10cm 帶的質心；頭中心＝整個髮區質心
zs = [p[2] for p in hair_pts]
z_top = max(zs)
bun = [p for p in hair_pts if p[2] > z_top - 0.10]
bun_c = [sum(c[i] for c in bun) / len(bun) for i in range(3)]
head_c = [sum(c[i] for c in hair_pts) / len(hair_pts) for i in range(3)]
print(f"HAIR tris={len(tris)} bun_center={[round(v,3) for v in bun_c]} "
      f"head_center={[round(v,3) for v in head_c]}")

# 髮區的真實邊界線段（3D）：一側在髮區、一側不在的邊——羽化只該發生在這裡
boundary_segs = []
for ek, fids in edge_to_faces.items():
    flags = [fi in face_set for fi in fids]
    if any(flags) and not all(flags):
        a = mw @ me.vertices[ek[0]].co
        b = mw @ me.vertices[ek[1]].co
        boundary_segs.append([a.x, a.y, a.z, b.x, b.y, b.z])
print(f"BOUNDARY segs={len(boundary_segs)}")

# 髮際帶面（非髮、距邊界頂點 <30mm）：遮罩的平滑髮際線要溢出到這些面上，
# 需要它們的 UV0 位置＋3D 位置（帶符號距離的膚側）
bverts = set()
for ek, fids in edge_to_faces.items():
    flags = [fi in face_set for fi in fids]
    if any(flags) and not all(flags):
        bverts.add(ek[0]); bverts.add(ek[1])
bpos = [mw @ me.vertices[i].co for i in bverts]
band_faces = set()
for p in me.polygons:
    if p.index in face_set:
        continue
    for li in p.loop_indices:
        w = mw @ me.vertices[me.loops[li].vertex_index].co
        if any((w - b).length < 0.030 for b in bpos):
            band_faces.add(p.index)
            break
band_tris = []
for tri in me.loop_triangles:
    if tri.polygon_index not in band_faces:
        continue
    uvr, pr = [], []
    for li in tri.loops:
        u, v = uv0.data[li].uv
        uvr += [u, v]
        p = mw @ me.vertices[me.loops[li].vertex_index].co
        pr += [p.x, p.y, p.z]
    band_tris.append({"uv": uvr, "pos": pr})
print(f"BAND faces={len(band_faces)} tris={len(band_tris)}")

# 全身非髮三角形的 UV（烘焙端建禁區圖：外擴不得寫入別的島）
other_uv = []
for tri in me.loop_triangles:
    if tri.polygon_index in face_set:
        continue
    row = []
    for li in tri.loops:
        u, v = uv0.data[li].uv
        row += [u, v]
    other_uv.append(row)
print(f"OTHER tris={len(other_uv)}")

with open(OUT, "w") as f:
    json.dump({"tris": tris, "bun_center": bun_c, "head_center": head_c,
               "boundary_segs": boundary_segs, "other_uv": other_uv,
               "band_tris": band_tris}, f)
print("DUMPED", OUT)
