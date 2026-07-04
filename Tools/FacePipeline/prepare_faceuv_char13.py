"""
One-time asset prep for the expanded FaceUV island (character13).

Run:  blender --background <character13.blend> --python prepare_faceuv_char13.py

- Detects the re-projected poly set (FaceUV != UVMap copy: the face + ring)
- Verifies projection orientation (forehead v > chin v)
- Uniformly rescales those polys' FaceUV to recover texel density
- Saves the blend
- Exports faceuv_expansion_src.json: new UVs for the 90 projected polys and
  the old-vs-new corner correspondence for the 62 core polys (old UVs come
  from char11_face.json extracted from character11, whose FaceUV carried the
  original mapping before it was deleted in 13)

This is OFFLINE tooling; the runtime pipeline only consumes the constants
derived from the exported JSON (see make_faceuv_expansion.py).
"""
import bpy
import json
import numpy as np
from pathlib import Path

PIPE = Path(r"C:\games\Unreal Engine\nice_ink_face_pipeline")
CHAR11_JSON = Path(r"C:\Users\willi\AppData\Local\Temp\claude\c--games-Unreal-Engine-nice-ink\0766f453-cbdd-4192-b58b-f7850c45308a\scratchpad\char11_face.json")
OUT_JSON = PIPE / "faceuv_expansion_src.json"

MESH_NAME = "PlusSize_Male_Body_01"
TARGET_V_EXTENT = 0.42      # rescale island so its v span matches the old oval's
TARGET_CENTER = (0.5, 0.52)


def main():
    with open(CHAR11_JSON) as f:
        old = json.load(f)

    obj = bpy.data.objects[MESH_NAME]
    me = obj.data
    assert not me.is_editmode, "save the blend in Object Mode first"
    n_loops = len(me.loops)
    assert (len(me.vertices), len(me.polygons)) == (old['counts']['verts'], old['counts']['polys']), \
        "topology differs from character11 - correspondence impossible"

    fbuf = np.zeros(n_loops * 2, dtype=np.float32)
    me.attributes['FaceUV'].data.foreach_get('vector', fbuf)
    fuv = fbuf.reshape(-1, 2)
    bbuf = np.zeros(n_loops * 2, dtype=np.float32)
    me.attributes['UVMap'].data.foreach_get('vector', bbuf)
    buv = bbuf.reshape(-1, 2)
    corner_vert = np.zeros(n_loops, dtype=np.int64)
    me.attributes['.corner_vert'].data.foreach_get('value', corner_vert)

    diff = np.linalg.norm(fuv - buv, axis=1)
    proj_polys = [p.index for p in me.polygons
                  if all(diff[li] > 1e-5
                         for li in range(p.loop_start, p.loop_start + p.loop_total))]
    old_island = set(old['island_polys'])
    assert old_island.issubset(set(proj_polys)), "old island not fully re-projected"
    print(f"projected set: {len(proj_polys)} polys (core {len(old_island)}, "
          f"ring {len(proj_polys) - len(old_island)})")

    # --- orientation check: forehead (max mesh y) must have higher v than chin
    co = np.zeros(len(me.vertices) * 3, dtype=np.float32)
    me.vertices.foreach_get('co', co)
    co = co.reshape(-1, 3)
    island_verts = sorted({int(corner_vert[li]) for pi in old_island
                           for li in range(me.polygons[pi].loop_start,
                                           me.polygons[pi].loop_start + me.polygons[pi].loop_total)})
    vert_new_uv = {}
    for pi in proj_polys:
        p = me.polygons[pi]
        for li in range(p.loop_start, p.loop_start + p.loop_total):
            vert_new_uv[int(corner_vert[li])] = li
    top_v = max(island_verts, key=lambda v: co[v, 1])
    bot_v = min(island_verts, key=lambda v: co[v, 1])
    v_top = fuv[vert_new_uv[top_v]][1]
    v_bot = fuv[vert_new_uv[bot_v]][1]
    print(f"orientation: forehead v={v_top:.3f}, chin v={v_bot:.3f} "
          f"({'OK, v-up' if v_top > v_bot else 'FLIPPED'})")
    assert v_top > v_bot, "projection is v-flipped; flip it in the UV editor first"

    # --- uniform rescale of the projected set to recover texel density
    proj_loops = [li for pi in proj_polys
                  for li in range(me.polygons[pi].loop_start,
                                  me.polygons[pi].loop_start + me.polygons[pi].loop_total)]
    ext = fuv[proj_loops]
    cur_center = (ext.min(axis=0) + ext.max(axis=0)) / 2
    cur_v_extent = ext[:, 1].max() - ext[:, 1].min()
    scale = TARGET_V_EXTENT / cur_v_extent
    print(f"rescale: x{scale:.3f} (v extent {cur_v_extent:.3f} -> {TARGET_V_EXTENT})")
    fuv[proj_loops] = (fuv[proj_loops] - cur_center) * scale + np.array(TARGET_CENTER)
    me.attributes['FaceUV'].data.foreach_set('vector', fuv.ravel())

    # --- export correspondence + new layout
    def poly_corners(pi):
        p = me.polygons[pi]
        ls = list(range(p.loop_start, p.loop_start + p.loop_total))
        return ([int(corner_vert[li]) for li in ls],
                [[float(fuv[li][0]), float(fuv[li][1])] for li in ls])

    core = []
    for pi in sorted(old_island):
        verts, new_uvs = poly_corners(pi)
        od = old['poly_data'][str(pi)]
        assert od['verts'] == verts, f"corner order changed on poly {pi}"
        core.append({'poly': pi, 'verts': verts, 'old_uvs': od['uvs'], 'new_uvs': new_uvs})

    coverage = []
    for pi in sorted(proj_polys):
        verts, new_uvs = poly_corners(pi)
        coverage.append({'poly': pi, 'verts': verts, 'new_uvs': new_uvs})

    with open(OUT_JSON, 'w') as f:
        json.dump({
            'core_polys': core,
            'coverage_polys': coverage,
            'old_boundary_loop_verts': old['boundary_loop_verts'],
        }, f)
    print(f"exported {OUT_JSON}")

    bpy.ops.wm.save_mainfile()
    print(f"Saved: {bpy.data.filepath}")


if __name__ == "__main__":
    main()
