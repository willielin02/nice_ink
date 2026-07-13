"""v101: bake outward thickness onto the USER-EDITED fundoshi surface.

Input : sumo_avatar_v100_pairpants_rebuild.blend  (user hand-flattened v68 base)
Output: sumo_avatar_v101_pairpants_solid.blend

Contract:
- The user's vertex positions are FINAL. No shaping, no proxy, no clearance
  floor, no healing. This script only adds thickness (Solidify 2mm outward,
  even thickness, clamp 2.0) and bakes it to real vertices.
- Body mesh is used for REPORT-ONLY intersection counting. It never moves
  anything.
"""
import bpy, bmesh, json, os, time, traceback
import numpy as np
from mathutils.bvhtree import BVHTree

ROOT = r"C:\games\Unreal Engine\nice_ink"
SRC_BLEND = os.path.join(ROOT, "SourceAssets", "previews", "sumo_avatar_v100_pairpants_rebuild.blend")
REF_BLEND = os.path.join(ROOT, "SourceAssets", "previews", "sumo_avatar_v68_pairpants_rebuild.blend")
OUT_BLEND = os.path.join(ROOT, "SourceAssets", "previews", "sumo_avatar_v101_pairpants_solid.blend")
OUT_DIR = os.path.join(ROOT, "Saved", "V101Solid")
PROG = os.path.join(OUT_DIR, "progress.jsonl")

SURF = "FundoshiSurface_Rebuild"
BODY = "SumoRetopo"
SOLIDIFY_MM = 2.0
CLAMP = 2.0

os.makedirs(OUT_DIR, exist_ok=True)
T0 = time.time()

def log(stage, **kw):
    rec = {"t": round(time.time() - T0, 1), "stage": stage}
    rec.update(kw)
    with open(PROG, "a") as f:
        f.write(json.dumps(rec) + "\n")

def mesh_arrays(obj):
    me = obj.data
    n = len(me.vertices)
    co = np.empty(n * 3)
    me.vertices.foreach_get("co", co)
    return co.reshape(n, 3)

def tri_arrays(obj, depsgraph=None):
    if depsgraph:
        ev = obj.evaluated_get(depsgraph)
        me = ev.to_mesh()
    else:
        me = obj.data
    me.calc_loop_triangles()
    n = len(me.vertices)
    co = np.empty(n * 3)
    me.vertices.foreach_get("co", co)
    tris = np.empty(len(me.loop_triangles) * 3, dtype=np.int64)
    me.loop_triangles.foreach_get("vertices", tris)
    mat = np.array(obj.matrix_world)
    co = co.reshape(n, 3) @ mat[:3, :3].T + mat[:3, 3]
    return co, tris.reshape(-1, 3)

def fold_edge_count(obj):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bm.normal_update()
    folds = 0
    min_dot = 1.0
    for e in bm.edges:
        if len(e.link_faces) == 2:
            d = e.link_faces[0].normal.dot(e.link_faces[1].normal)
            min_dot = min(min_dot, d)
            if d < 0.0:
                folds += 1
    bm.free()
    return folds, min_dot

def tri_tri_overlap_count(co_a, tris_a, co_b, tris_b):
    bvh_a = BVHTree.FromPolygons(co_a.tolist(), tris_a.tolist(), all_triangles=True)
    bvh_b = BVHTree.FromPolygons(co_b.tolist(), tris_b.tolist(), all_triangles=True)
    return len(bvh_a.overlap(bvh_b))

def main():
    # -- reference: how far did the user move things vs v68?
    bpy.ops.wm.open_mainfile(filepath=REF_BLEND)
    ref_co = mesh_arrays(bpy.data.objects[SURF])

    bpy.ops.wm.open_mainfile(filepath=SRC_BLEND)
    log("open", file=SRC_BLEND)
    obj = bpy.data.objects[SURF]
    cur_co = mesh_arrays(obj)
    if ref_co.shape == cur_co.shape:
        d = np.linalg.norm(cur_co - ref_co, axis=1) * 1000.0
        moved = int((d > 0.01).sum())
        log("user_edit_delta", moved_verts=moved,
            max_mm=round(float(d.max()), 3), mean_moved_mm=round(float(d[d > 0.01].mean()), 3) if moved else 0.0)
    else:
        log("user_edit_delta", note="topology differs from v68", ref=list(ref_co.shape), cur=list(cur_co.shape))

    folds_before, min_dot = fold_edge_count(obj)
    log("base_folds", fold_edges=folds_before, adjacent_normal_min_dot=round(min_dot, 4))

    # -- report-only: does the user's base surface touch the body?
    body = bpy.data.objects[BODY]
    dg = bpy.context.evaluated_depsgraph_get()
    body_co, body_tris = tri_arrays(body, dg)
    surf_co, surf_tris = tri_arrays(obj)
    base_hits = tri_tri_overlap_count(surf_co, surf_tris, body_co, body_tris)
    log("base_body_check", tri_tri_pairs=base_hits, note="report-only, nothing moved")

    # -- solidify outward and bake
    for m in list(obj.modifiers):
        obj.modifiers.remove(m)
    mod = obj.modifiers.new("Solidify", "SOLIDIFY")
    mod.thickness = SOLIDIFY_MM / 1000.0
    mod.offset = 1.0
    mod.use_even_offset = True
    mod.use_quality_normals = True
    mod.thickness_clamp = CLAMP
    mod.use_rim = True
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.modifier_apply(modifier=mod.name)
    log("solidify_applied", verts=len(obj.data.vertices), faces=len(obj.data.polygons),
        thickness_mm=SOLIDIFY_MM, clamp=CLAMP)

    # -- report-only validation on the baked shell
    shell_co, shell_tris = tri_arrays(obj)
    bvh_shell = BVHTree.FromPolygons(shell_co.tolist(), shell_tris.tolist(), all_triangles=True)
    self_pairs = [(a, b) for a, b in bvh_shell.overlap(bvh_shell)
                  if a < b and not set(shell_tris[a]) & set(shell_tris[b])]
    shell_body_hits = tri_tri_overlap_count(shell_co, shell_tris, body_co, body_tris)
    folds_after, min_dot_after = fold_edge_count(obj)
    log("validate", self_intersections=len(self_pairs), body_tri_tri_pairs=shell_body_hits,
        fold_edges=folds_after, adjacent_normal_min_dot=round(min_dot_after, 4),
        note="report-only; user vertices untouched")

    bpy.ops.wm.save_as_mainfile(filepath=OUT_BLEND)
    log("saved", blend=OUT_BLEND)

    # -- renders
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.render.resolution_x = 1280
    sc.render.resolution_y = 1280
    cam_data = bpy.data.cameras.new("ProbeCam")
    cam = bpy.data.objects.new("ProbeCam", cam_data)
    sc.collection.objects.link(cam)
    sc.camera = cam
    import mathutils
    center = mathutils.Vector((0, 0, 1.0))
    views = {"front": (0, -3.2, 1.0), "back": (0, 3.2, 1.0), "side": (3.2, 0, 1.0)}
    for name, pos in views.items():
        cam.location = pos
        direc = center - cam.location
        cam.rotation_euler = direc.to_track_quat("-Z", "Y").to_euler()
        sc.render.filepath = os.path.join(OUT_DIR, name + ".png")
        bpy.ops.render.render(write_still=True)
        log("render", view=name)

    log("done", status="OK")

try:
    main()
except Exception:
    log("exception", trace=traceback.format_exc()[-1500:])
    raise
