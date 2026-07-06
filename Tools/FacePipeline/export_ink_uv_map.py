"""Export the face-island UVMap(UV0) <-> FaceUV(UV1) triangle table from char17.

The ink atlas (marker/tattoo RTs) lives in UV0; the face texture (and the
eye masks the pipeline produces) live in FaceUV. bake_eye_ink_mask.py uses
this table to transfer the per-player eye mask from FaceUV space into the
ink-atlas space, producing the no-draw mask the body material multiplies
into the ink layers.

Run:
  blender --background --python export_ink_uv_map.py

Output: data/char17_ink_uv_map.json
  {"tris": [[u0,v0, u0,v0, u0,v0,  u1,v1, u1,v1, u1,v1], ...]}
  (three corners in UVMap coords, then the same three corners in FaceUV
   coords; raw Blender V-up convention - the baker flips to image space)
"""
import bpy
import json
from pathlib import Path

BASE = Path(__file__).resolve().parent
CHAR_BLEND = r"C:\games\Unreal Engine\nice_ink\Content\玩家\nice_ink_player_character17.blend"
OUT_JSON = BASE / "data" / "char17_ink_uv_map.json"
FACEMASK_MIN_R = 0.5   # face-island polys carry FaceMask corner color R=1

bpy.ops.wm.open_mainfile(filepath=CHAR_BLEND)

if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')

body = None
for ob in bpy.data.objects:
    if ob.type == 'MESH' and 'FaceUV' in ob.data.uv_layers:
        body = ob
        break
if body is None:
    raise RuntimeError("no mesh with a FaceUV layer found")

# UV0 均勻密度重排——與網格導出腳本共用同一套 UV（確定性；見 uv0_uniform.py）
_uv_src = (BASE.parent / "AssetPrep" / "uv0_uniform.py").read_text(encoding="utf-8")
exec(compile(_uv_src, "uv0_uniform.py", "exec"))
reunwrap_uv0_uniform(body)

me = body.data
uv0 = me.uv_layers['UVMap'].data
uv1 = me.uv_layers['FaceUV'].data
fm = me.color_attributes.get('FaceMask')
if fm is None:
    raise RuntimeError("FaceMask color attribute missing")
if fm.domain != 'CORNER':
    raise RuntimeError(f"FaceMask domain {fm.domain}, expected CORNER")

tris = []
n_polys = 0
for poly in me.polygons:
    loops = list(poly.loop_indices)
    if not all(fm.data[li].color[0] >= FACEMASK_MIN_R for li in loops):
        continue
    n_polys += 1
    # fan-triangulate the polygon on its loop order
    for k in range(1, len(loops) - 1):
        li = (loops[0], loops[k], loops[k + 1])
        row = []
        for l in li:
            row += [uv0[l].uv[0], uv0[l].uv[1]]
        for l in li:
            row += [uv1[l].uv[0], uv1[l].uv[1]]
        tris.append([round(v, 6) for v in row])

OUT_JSON.parent.mkdir(parents=True, exist_ok=True)
with open(OUT_JSON, "w", encoding="utf-8") as f:
    json.dump({"source": Path(CHAR_BLEND).name, "object": body.name,
               "face_polys": n_polys, "tris": tris}, f)
print(f"exported {n_polys} face polys -> {len(tris)} tris -> {OUT_JSON}")
