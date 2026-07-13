"""One-pass constrained low-frequency fairing for the validated fundoshi."""

from __future__ import annotations

import os
import sys

import bpy
import numpy as np
from mathutils import Vector


ROOT = r"C:\games\Unreal Engine\nice_ink"
sys.path.insert(0, os.path.join(ROOT, "Tools", "AssetPrep"))
import build_fundoshi_pairpants as base  # noqa: E402
import build_fundoshi_smooth_bulge as qc  # noqa: E402


OUT_BLEND = os.path.join(
    ROOT,
    "SourceAssets",
    "previews",
    "sumo_avatar_v72_pairpants_smooth_fair.blend",
)
SHOT_DIR = os.path.join(ROOT, "Saved", "V72SmoothFair")
FAIR_ITERS = 6
FAIR_LAMBDA = 0.25
MAX_MOVE_MM = 4.0


def main() -> None:
    obj = bpy.data.objects.get(base.OUT_NAME)
    body = bpy.data.objects.get(base.BODY_NAME)
    proxy = bpy.data.objects.get(base.SMOOTH_PROXY_NAME)
    if not obj or obj.type != "MESH" or not body or not proxy:
        raise RuntimeError("Validated fair mesh, body, or smooth proxy is missing")

    mesh = obj.data
    topology_before = base.topology_audit(mesh)
    if (
        topology_before["components"] != 1
        or topology_before["boundary_loops"] != 3
        or topology_before["euler"] != -1
        or base.count_self_overlaps(mesh) != 0
    ):
        raise RuntimeError("Input fair mesh failed its geometry gate")

    verts = np.array([tuple(vertex.co) for vertex in mesh.vertices], dtype=np.float64)
    edges = np.asarray([tuple(edge.vertices) for edge in mesh.edges], dtype=np.int32)
    boundary_ids = np.asarray(
        [index for loop in base.ordered_boundary_loops(mesh) for index in loop],
        dtype=np.int32,
    )
    fixed = np.zeros(len(verts), dtype=bool)
    fixed[boundary_ids] = True
    boundary_before = verts[boundary_ids].copy()

    proxy_bvh = base.build_object_bvh_in_body_space(proxy, body)
    candidate = base.envelope_relax(
        verts,
        edges,
        fixed,
        verts,
        proxy_bvh,
        FAIR_ITERS,
        FAIR_LAMBDA,
    )

    delta = candidate - verts
    move = np.linalg.norm(delta, axis=1)
    limit = MAX_MOVE_MM / 1000.0
    scale = np.minimum(1.0, limit / np.maximum(move, 1.0e-12))
    result = verts + delta * scale[:, None]
    result[boundary_ids] = boundary_before

    body_bvh, body_eval_mesh = base.build_body_bvh(body)
    result, projected, _worst = base.project_outside(
        result,
        body_bvh,
        base.BOUNDARY_CLEAR_MM / 1000.0,
        fixed,
    )
    result[boundary_ids] = boundary_before

    for index, vertex in enumerate(mesh.vertices):
        vertex.co = Vector(tuple(result[index]))
    mesh.update()
    base.ensure_outward_normals(mesh, body_bvh)

    boundary_error = np.linalg.norm(result[boundary_ids] - boundary_before, axis=1)
    overlaps = base.count_self_overlaps(mesh)
    topology_after = base.topology_audit(mesh)
    actual_move = np.linalg.norm(result - verts, axis=1) * 1000.0
    base.log(
        "FAIR",
        f"projected={projected} move_mm p50={np.median(actual_move):.2f} "
        f"p90={np.percentile(actual_move, 90):.2f} max={actual_move.max():.2f}",
    )
    base.log(
        "BOUNDARY",
        f"rms={np.sqrt(np.mean(boundary_error**2))*1000:.6f}mm "
        f"max={boundary_error.max()*1000:.6f}mm",
    )
    base.log("SELF-OVERLAP", str(overlaps))
    if boundary_error.max() > 1.0e-9 or overlaps or topology_after != topology_before:
        raise RuntimeError("Constrained fairing failed a geometry hard gate")

    for polygon in mesh.polygons:
        polygon.use_smooth = True
    for modifier_name in ("FundoshiSubdivision", "FundoshiThickness"):
        old = obj.modifiers.get(modifier_name)
        if old:
            obj.modifiers.remove(old)
    base.add_render_modifiers(obj)
    bpy.data.meshes.remove(body_eval_mesh)

    bpy.ops.wm.save_as_mainfile(filepath=OUT_BLEND, compress=True)
    base.log("SAVED", OUT_BLEND)
    qc.SHOT_DIR = SHOT_DIR
    qc.render_three(body)
    bpy.ops.wm.save_as_mainfile(filepath=OUT_BLEND, compress=True)
    base.log("DONE", OUT_BLEND)


if __name__ == "__main__":
    main()
