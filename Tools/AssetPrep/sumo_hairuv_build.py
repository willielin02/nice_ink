# HairUV v3：髮面＋髮際帶面 一起 smart_project 成島（逐紋素烘焙用）
# 解析圓柱 UV 的教訓：φ 場非線性，面內線性內插在髮旋/髻頸錯位超過一絲寬=內部面級斷裂。
# 逐紋素烘焙場值=面內精確；島縫由烘焙端一階外插 gutter 解決。
import bpy
import math

MASTER = r"C:\games\Unreal Engine\nice_ink\SourceAssets\sumo_character_master.blend"
MASTER_SNAP = r"C:\games\Unreal Engine\nice_ink\SourceAssets\masters\sumo_character_master_v10_hairuv_islands.blend"

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

# 髮際帶面（羽化溢出區也要有島＋內容）
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
print(f"HAIR faces={len(face_ids)} BAND={len(band)} TOTAL={len(sel_ids)}")

huv = me.uv_layers.get("HairUV") or me.uv_layers.new(name="HairUV")
me.uv_layers.active = huv
for o in bpy.context.view_layer.objects:
    o.select_set(False)
body.select_set(True)
bpy.context.view_layer.objects.active = body
bpy.context.scene.tool_settings.use_uv_select_sync = True
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.reveal()
bpy.ops.mesh.select_mode(type='FACE')
bpy.ops.mesh.select_all(action='DESELECT')
bpy.ops.object.mode_set(mode='OBJECT')
for p in me.polygons:
    p.select = p.index in sel_ids
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.uv.smart_project(angle_limit=math.radians(66.0), island_margin=0.008,
                         correct_aspect=True, scale_to_bounds=False)
bpy.ops.uv.average_islands_scale()
bpy.ops.uv.pack_islands(margin=0.010)
bpy.ops.object.mode_set(mode='OBJECT')

def a2d(a, b, c):
    return abs((b[0]-a[0])*(c[1]-a[1]) - (c[0]-a[0])*(b[1]-a[1])) / 2
me.calc_loop_triangles()
uv_area = area3d = 0.0
for tri in me.loop_triangles:
    if tri.polygon_index not in sel_ids:
        continue
    uvs = [tuple(huv.data[l].uv) for l in tri.loops]
    ps = [mw @ me.vertices[me.loops[l].vertex_index].co for l in tri.loops]
    uv_area += a2d(*uvs)
    area3d += ((ps[1]-ps[0]).cross(ps[2]-ps[0])).length / 2
dens = 2048 * math.sqrt(uv_area / area3d) / 1000
print(f"HAIRUV density={dens:.2f} px/mm uv_frac={uv_area:.3f}")

me.uv_layers.active = me.uv_layers["UVMap"]
bpy.context.scene.render.engine = 'BLENDER_EEVEE'
bpy.ops.wm.save_as_mainfile(filepath=MASTER, compress=True)
import shutil
shutil.copy(MASTER, MASTER_SNAP)
print("MASTER_SAVED + SNAPSHOT v10")
