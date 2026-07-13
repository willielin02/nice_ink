"""Export the face-island UVMap(UV0) <-> FaceUV triangle table from the SUMO master.

Same schema as export_ink_uv_map.py (char17), consumed by bake_eye_ink_mask.py.

CRITICAL DIFFERENCE vs the char17 exporter: NO reunwrap_uv0_uniform() call.
The sumo master's UV0 is frozen — every hand-painted mask (fundoshi_mask.png,
hair/face masks, edge shadow) is authored against the CURRENT UVMap layout.
Re-unwrapping here would silently invalidate all of them. We export as-is.

Run:
  blender --background --python export_sumo_ink_uv_map.py

Output: data/sumo_ink_uv_map.json
"""
import bpy
import json
from pathlib import Path

BASE = Path(__file__).resolve().parent
CHAR_BLEND = r"C:\games\Unreal Engine\nice_ink\SourceAssets\sumo_character_master.blend"
OUT_JSON = BASE / "data" / "sumo_ink_uv_map.json"
FACEMASK_MIN_R = 0.5

bpy.ops.wm.open_mainfile(filepath=CHAR_BLEND)

if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')

body = bpy.data.objects["SumoRetopo"]
me = body.data
assert set(l.name for l in me.uv_layers) >= {"UVMap", "FaceUV", "HairUV"}, \
    f"expected 3 UV layers, got {[l.name for l in me.uv_layers]}"

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
