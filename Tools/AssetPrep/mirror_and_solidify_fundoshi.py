"""v102/v103: mirror the user-edited fundoshi band X>0 -> X<0, then bake thickness.

Input : sumo_avatar_v100_pairpants_rebuild.blend  (user hand-flattened base; v100 untouched)
Output: sumo_avatar_v102_pairpants_mirrorbase.blend  (mirrored base, no thickness)
        sumo_avatar_v103_pairpants_solid.blend       (mirrored + Solidify 2mm baked)

Method: the band topology is not left/right symmetric (Tutte+CDT from hand-drawn
curves), so no vertex-pair copy exists. Instead build a BVH of the whole band
mirrored across X and snap every X<0 vertex to its nearest point on that shadow
surface. A 20mm smoothstep blend around the midline keeps the crack strap and
front joint seamless. X>0 vertices are never touched. Body is REPORT-ONLY.
"""
import bpy, json, os, time, traceback
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

ROOT = r"C:\games\Unreal Engine\nice_ink"
SRC_BLEND = os.path.join(ROOT, "SourceAssets", "previews", "sumo_avatar_v100_pairpants_rebuild.blend")
OUT_BASE = os.path.join(ROOT, "SourceAssets", "previews", "sumo_avatar_v102_pairpants_mirrorbase.blend")
OUT_SOLID = os.path.join(ROOT, "SourceAssets", "previews", "sumo_avatar_v103_pairpants_solid.blend")
OUT_DIR = os.path.join(ROOT, "Saved", "V102Mirror")
PROG = os.path.join(OUT_DIR, "progress.jsonl")

SURF = "FundoshiSurface_Rebuild"
BODY = "SumoRetopo"
SOLIDIFY_MM = 2.0
CLAMP = 2.0
BLEND_MM = 20.0   # midline blend zone half-width

os.makedirs(OUT_DIR, exist_ok=True)
T0 = time.time()

def log(stage, **kw):
    rec = {"t": round(time.time() - T0, 1), "stage": stage}
    rec.update(kw)
    with open(PROG, "a") as f:
        f.write(json.dumps(rec) + "\n")

def world_arrays(obj, dg=None):
    me = obj.evaluated_get(dg).to_mesh() if dg else obj.data
    me.calc_loop_triangles()
    n = len(me.vertices)
    co = np.empty(n * 3)
    me.vertices.foreach_get("co", co)
    tris = np.empty(len(me.loop_triangles) * 3, dtype=np.int64)
    me.loop_triangles.foreach_get("vertices", tris)
    mat = np.array(obj.matrix_world)
    return co.reshape(n, 3) @ mat[:3, :3].T + mat[:3, 3], tris.reshape(-1, 3)

def signed_mm(P, bvh):
    out = np.empty(len(P))
    for i, p in enumerate(P):
        loc, nrm, _, _ = bvh.find_nearest(Vector(p))
        d = (Vector(p) - loc).length * 1000.0
        out[i] = d if (Vector(p) - loc).dot(nrm) >= 0 else -d
    return out

def smoothstep(t):
    t = np.clip(t, 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)

def main():
    bpy.ops.wm.open_mainfile(filepath=SRC_BLEND)
    log("open", file=SRC_BLEND)
    obj = bpy.data.objects[SURF]
    mat = np.array(obj.matrix_world)
    S, Stris = world_arrays(obj)

    # shadow surface: whole band mirrored across X
    M = S.copy()
    M[:, 0] *= -1.0
    bvh_mirror = BVHTree.FromPolygons(M.tolist(), Stris.tolist(), all_triangles=True)

    # snap X<0 verts onto the shadow, blended near the midline
    w = smoothstep(-S[:, 0] * 1000.0 / BLEND_MM)   # 0 at x>=0, 1 below -20mm
    moved = 0
    disp = np.zeros(len(S))
    NEW = S.copy()
    for i in range(len(S)):
        if w[i] <= 0.0:
            continue
        loc, _, _, _ = bvh_mirror.find_nearest(Vector(S[i]))
        target = np.array(loc)
        NEW[i] = S[i] + w[i] * (target - S[i])
        disp[i] = np.linalg.norm(NEW[i] - S[i]) * 1000.0
        if disp[i] > 0.01:
            moved += 1
    log("mirror_snap", affected=int((w > 0).sum()), moved_over_10um=moved,
        max_disp_mm=round(float(disp.max()), 3),
        mean_disp_mm=round(float(disp[disp > 0.01].mean()), 3) if moved else 0.0)

    # write back (convert world -> local)
    inv = np.linalg.inv(mat)
    local = NEW @ inv[:3, :3].T + inv[:3, 3]
    obj.data.vertices.foreach_set("co", local.ravel())
    obj.data.update()

    # residual asymmetry check: every X<0 vert vs shadow surface distance
    resid = np.empty(len(S))
    for i in range(len(S)):
        loc, _, _, _ = bvh_mirror.find_nearest(Vector(NEW[i]))
        resid[i] = (Vector(NEW[i]) - loc).length * 1000.0
    far = NEW[:, 0] < -BLEND_MM / 1000.0
    log("symmetry_residual", verts_checked=int(far.sum()),
        max_mm=round(float(resid[far].max()), 3) if far.any() else 0.0,
        mean_mm=round(float(resid[far].mean()), 3) if far.any() else 0.0)

    # report-only: base vs body
    body = bpy.data.objects[BODY]
    dg = bpy.context.evaluated_depsgraph_get()
    body_co, body_tris = world_arrays(body, dg)
    bvh_body = BVHTree.FromPolygons(body_co.tolist(), body_tris.tolist(), all_triangles=True)
    base_d = signed_mm(NEW, bvh_body)
    log("base_body_check", inside=int((base_d < 0).sum()),
        min_mm=round(float(base_d.min()), 2), note="report-only, nothing moved")

    bpy.ops.wm.save_as_mainfile(filepath=OUT_BASE)
    log("saved_base", blend=OUT_BASE)

    # base vertex normals (needed to re-clamp runaway solidify verts later)
    nrm_base = np.empty(len(obj.data.vertices) * 3)
    obj.data.vertices.foreach_get("normal", nrm_base)
    nrm_base = (nrm_base.reshape(-1, 3) @ mat[:3, :3].T)
    nrm_base /= np.linalg.norm(nrm_base, axis=1, keepdims=True)

    # solidify outward and bake
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

    # Solidify offset=+1 MOVES the original verts outward; the appended copy
    # stays at the base positions as the inner layer. Outer layer = verts 0..n-1.
    n = len(S)
    shell_co, shell_tris = world_arrays(obj)
    disp = np.linalg.norm(shell_co[:n] - NEW, axis=1) * 1000.0
    bad = np.where(disp > 4.5)[0]   # even-offset legit max = clamp*thickness = 4mm
    if len(bad):
        fixed = NEW[bad] + nrm_base[bad] * (SOLIDIFY_MM / 1000.0)
        local_fix = fixed @ inv[:3, :3].T + inv[:3, 3]
        for k, vi in enumerate(bad):
            obj.data.vertices[vi].co = local_fix[k]
        obj.data.update()
        shell_co, shell_tris = world_arrays(obj)
    log("despike", runaway_verts=int(len(bad)),
        worst_disp_mm=round(float(disp.max()), 1))

    # report-only: true outer layer + shell self-intersections
    outer_d = signed_mm(shell_co[:n], bvh_body)
    bvh_shell = BVHTree.FromPolygons(shell_co.tolist(), shell_tris.tolist(), all_triangles=True)
    self_pairs = [(a, b) for a, b in bvh_shell.overlap(bvh_shell)
                  if a < b and not set(shell_tris[a]) & set(shell_tris[b])]
    log("validate", outer_inside=int((outer_d < 0).sum()),
        outer_min_mm=round(float(outer_d.min()), 2),
        outer_inside_deeper_1mm=int((outer_d < -1.0).sum()),
        self_intersections=len(self_pairs), note="report-only; base verts final")

    bpy.ops.wm.save_as_mainfile(filepath=OUT_SOLID)
    log("saved_solid", blend=OUT_SOLID)

    # tinted renders
    obj.color = (0.75, 0.12, 0.10, 1.0)
    body.color = (0.72, 0.68, 0.62, 1.0)
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.light = "STUDIO"
    sc.display.shading.color_type = "OBJECT"
    sc.render.resolution_x = sc.render.resolution_y = 1400
    import mathutils
    center = mathutils.Vector((0, 0, 1.0))
    cam_data = bpy.data.cameras.new("ProbeCam")
    cam = bpy.data.objects.new("ProbeCam", cam_data)
    sc.collection.objects.link(cam)
    sc.camera = cam
    views = {"front": (0, -3.2, 1.0), "back": (0, 3.2, 1.0),
             "left": (3.2, 0, 1.0), "right": (-3.2, 0, 1.0)}
    for name, pos in views.items():
        cam.location = pos
        d = center - cam.location
        cam.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
        sc.render.filepath = os.path.join(OUT_DIR, name + ".png")
        bpy.ops.render.render(write_still=True)
        log("render", view=name)

    log("done", status="OK")

try:
    main()
except Exception:
    log("exception", trace=traceback.format_exc()[-1500:])
    raise
