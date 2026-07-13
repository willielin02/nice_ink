"""v105: mirror-sync the user's v104 SOLID fundoshi shell, X>0 -> X<0.

The shell has two layers (Solidify baked in v103: verts 0..n-1 = outer,
n..2n-1 = inner at base positions). Snapping must be per-layer or an inner
vertex could grab the mirrored OUTER surface 2mm away. Rim faces (mixed
indices) are excluded from both shadow BVHs. 20mm midline smoothstep blend
keeps the crack strap seamless. X>0 vertices are never touched. v104 stays
untouched; output is v105. Body is REPORT-ONLY.
"""
import bpy, json, os, time, traceback
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

ROOT = r"C:\games\Unreal Engine\nice_ink"
SRC_BLEND = os.path.join(ROOT, "SourceAssets", "previews", "sumo_avatar_v104_pairpants_solid.blend")
OUT_BLEND = os.path.join(ROOT, "SourceAssets", "previews", "sumo_avatar_v105_pairpants_solid.blend")
OUT_DIR = os.path.join(ROOT, "Saved", "V105Mirror")
PROG = os.path.join(OUT_DIR, "progress.jsonl")

SURF = "FundoshiSurface_Rebuild"
BODY = "SumoRetopo"
N_BASE = 3567          # verts per layer (v68 base count; verified for v103/v104)
BLEND_MM = 20.0

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

def smoothstep(t):
    t = np.clip(t, 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)

def main():
    bpy.ops.wm.open_mainfile(filepath=SRC_BLEND)
    obj = bpy.data.objects[SURF]
    if obj.mode != "OBJECT":
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.mode_set(mode="OBJECT")
    log("open", file=SRC_BLEND, verts=len(obj.data.vertices))
    if len(obj.data.vertices) != 2 * N_BASE:
        raise RuntimeError(f"unexpected vert count {len(obj.data.vertices)}")

    mat = np.array(obj.matrix_world)
    S, Stris = world_arrays(obj)
    n = N_BASE

    # per-layer shadow BVHs from the mirrored shell (rim faces excluded)
    M = S.copy()
    M[:, 0] *= -1.0
    tri_max = Stris.max(axis=1)
    tri_min = Stris.min(axis=1)
    outer_tris = Stris[tri_max < n]
    inner_tris = Stris[tri_min >= n]
    log("layers", outer_tris=int(len(outer_tris)), inner_tris=int(len(inner_tris)),
        rim_tris=int(len(Stris) - len(outer_tris) - len(inner_tris)))
    bvh_outer = BVHTree.FromPolygons(M.tolist(), outer_tris.tolist(), all_triangles=True)
    bvh_inner = BVHTree.FromPolygons(M.tolist(), inner_tris.tolist(), all_triangles=True)

    # band-edge verts must snap to the mirrored BOUNDARY polyline, not the
    # surface interior — nearest-point-to-surface pulls the edge outline
    # inward and leaves the silhouette asymmetric (v105 first attempt: all
    # 110 residual verts sat on the +X band edge).
    from collections import defaultdict
    def boundary_of(layer_tris):
        cnt = defaultdict(int)
        for t in layer_tris:
            for a, b in ((t[0], t[1]), (t[1], t[2]), (t[2], t[0])):
                cnt[(min(a, b), max(a, b))] += 1
        segs = np.array([k for k, v in cnt.items() if v == 1], dtype=np.int64)
        return set(segs.ravel().tolist()), segs

    def nearest_on_polyline(p, A, B):
        AB = B - A
        L2 = (AB * AB).sum(axis=1)
        t = np.clip(((p - A) * AB).sum(axis=1) / np.maximum(L2, 1e-18), 0.0, 1.0)
        C = A + AB * t[:, None]
        d2 = ((p - C) ** 2).sum(axis=1)
        k = int(np.argmin(d2))
        return C[k]

    bset_o, segs_o = boundary_of(outer_tris)
    bset_i, segs_i = boundary_of(inner_tris)
    segA_o, segB_o = M[segs_o[:, 0]], M[segs_o[:, 1]]
    segA_i, segB_i = M[segs_i[:, 0]], M[segs_i[:, 1]]
    log("boundary", outer_edge_verts=len(bset_o), inner_edge_verts=len(bset_i))

    w = smoothstep(-S[:, 0] * 1000.0 / BLEND_MM)
    NEW = S.copy()
    disp = np.zeros(len(S))
    for i in range(len(S)):
        if w[i] <= 0.0:
            continue
        if i < n:
            target = (nearest_on_polyline(S[i], segA_o, segB_o) if i in bset_o
                      else np.array(bvh_outer.find_nearest(Vector(S[i]))[0]))
        else:
            target = (nearest_on_polyline(S[i], segA_i, segB_i) if i in bset_i
                      else np.array(bvh_inner.find_nearest(Vector(S[i]))[0]))
        NEW[i] = S[i] + w[i] * (target - S[i])
        disp[i] = np.linalg.norm(NEW[i] - S[i]) * 1000.0
    moved = disp > 0.01
    log("mirror_snap", moved=int(moved.sum()),
        max_disp_mm=round(float(disp.max()), 3),
        mean_disp_mm=round(float(disp[moved].mean()), 3) if moved.any() else 0.0)

    inv = np.linalg.inv(mat)
    local = NEW @ inv[:3, :3].T + inv[:3, 3]
    obj.data.vertices.foreach_set("co", local.ravel())
    obj.data.update()

    # verify: residual asymmetry of the result (per-layer, beyond blend zone)
    M2 = NEW.copy()
    M2[:, 0] *= -1.0
    bvh_o2 = BVHTree.FromPolygons(M2.tolist(), outer_tris.tolist(), all_triangles=True)
    bvh_i2 = BVHTree.FromPolygons(M2.tolist(), inner_tris.tolist(), all_triangles=True)
    resid = np.empty(len(NEW))
    for i in range(len(NEW)):
        bvh = bvh_o2 if i < n else bvh_i2
        loc, _, _, _ = bvh.find_nearest(Vector(NEW[i]))
        resid[i] = (Vector(NEW[i]) - loc).length * 1000.0
    far = np.abs(NEW[:, 0]) > BLEND_MM / 1000.0
    log("symmetry_after", max_mm=round(float(resid[far].max()), 3),
        mean_mm=round(float(resid[far].mean()), 3),
        over_1mm=int((resid[far] > 1.0).sum()))

    # report-only: outer layer vs body + shell self-intersections
    body = bpy.data.objects[BODY]
    dg = bpy.context.evaluated_depsgraph_get()
    bco, btris = world_arrays(body, dg)
    bvh_body = BVHTree.FromPolygons(bco.tolist(), btris.tolist(), all_triangles=True)
    od = np.empty(n)
    for i in range(n):
        loc, nrm, _, _ = bvh_body.find_nearest(Vector(NEW[i]))
        d = (Vector(NEW[i]) - loc).length * 1000.0
        od[i] = d if (Vector(NEW[i]) - loc).dot(nrm) >= 0 else -d
    bvh_shell = BVHTree.FromPolygons(NEW.tolist(), Stris.tolist(), all_triangles=True)
    self_pairs = [(a, b) for a, b in bvh_shell.overlap(bvh_shell)
                  if a < b and not set(Stris[a]) & set(Stris[b])]
    log("validate", outer_inside=int((od < 0).sum()),
        outer_min_mm=round(float(od.min()), 2),
        outer_inside_deeper_1mm=int((od < -1.0).sum()),
        self_intersections=len(self_pairs), note="report-only")

    bpy.ops.wm.save_as_mainfile(filepath=OUT_BLEND)
    log("saved", blend=OUT_BLEND)

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
    cam_data = bpy.data.cameras.new("Cam105")
    cam = bpy.data.objects.new("Cam105", cam_data)
    sc.collection.objects.link(cam)
    sc.camera = cam
    for name, pos in {"front": (0, -3.2, 1.0), "back": (0, 3.2, 1.0),
                      "left": (3.2, 0, 1.0), "right": (-3.2, 0, 1.0)}.items():
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
