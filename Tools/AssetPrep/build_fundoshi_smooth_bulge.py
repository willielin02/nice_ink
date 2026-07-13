"""Add one deterministic low-frequency bulge to the clean fundoshi fair mesh.

Input is the already validated v72 fair blend.  This script intentionally has
no Cloth, collision simulation, parameter search, or retry loop.
"""

from __future__ import annotations

import os
import sys

import bpy
import numpy as np
from mathutils import Vector


ROOT = r"C:\games\Unreal Engine\nice_ink"
sys.path.insert(0, os.path.join(ROOT, "Tools", "AssetPrep"))
import build_fundoshi_pairpants as base  # noqa: E402


OUT_BLEND = os.path.join(
    ROOT,
    "SourceAssets",
    "previews",
    "sumo_avatar_v72_pairpants_smooth_bulge.blend",
)
SHOT_DIR = os.path.join(ROOT, "Saved", "V72SmoothBulge")
BULGE_MM = 4.0
BULGE_RAMP_MM = 30.0
NORMAL_SMOOTH_ITERS = 8


def smooth_vertex_normals(
    verts: np.ndarray,
    faces: np.ndarray,
    edges: np.ndarray,
) -> np.ndarray:
    normals = np.zeros_like(verts)
    for triangle in faces:
        p0, p1, p2 = verts[triangle]
        area_normal = np.cross(p1 - p0, p2 - p0)
        normals[triangle] += area_normal
    lengths = np.linalg.norm(normals, axis=1)
    normals /= np.maximum(lengths[:, None], 1.0e-12)

    ei, ej = edges[:, 0], edges[:, 1]
    degree = np.zeros(len(verts), dtype=np.float64)
    np.add.at(degree, ei, 1.0)
    np.add.at(degree, ej, 1.0)
    degree = np.maximum(degree, 1.0)
    for _ in range(NORMAL_SMOOTH_ITERS):
        neighbor_sum = np.zeros_like(normals)
        np.add.at(neighbor_sum, ei, normals[ej])
        np.add.at(neighbor_sum, ej, normals[ei])
        average = neighbor_sum / degree[:, None]
        normals = normals * 0.65 + average * 0.35
        normals /= np.maximum(np.linalg.norm(normals, axis=1)[:, None], 1.0e-12)
    return normals


def render_three(body: bpy.types.Object) -> None:
    os.makedirs(SHOT_DIR, exist_ok=True)
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x = 1280
    scene.render.resolution_y = 960
    scene.render.resolution_percentage = 100

    for obj in list(bpy.data.objects):
        if obj.name.startswith("SmoothBulge_QC"):
            bpy.data.objects.remove(obj, do_unlink=True)

    camera = bpy.data.objects.new(
        "SmoothBulge_QC_Camera",
        bpy.data.cameras.new("SmoothBulge_QC_Camera"),
    )
    scene.collection.objects.link(camera)
    scene.camera = camera
    sun = bpy.data.objects.new(
        "SmoothBulge_QC_Sun",
        bpy.data.lights.new("SmoothBulge_QC_Sun", "SUN"),
    )
    sun.data.energy = 3.0
    scene.collection.objects.link(sun)
    fill = bpy.data.objects.new(
        "SmoothBulge_QC_Fill",
        bpy.data.lights.new("SmoothBulge_QC_Fill", "AREA"),
    )
    fill.data.energy = 600.0
    fill.data.shape = "DISK"
    fill.data.size = 4.0
    scene.collection.objects.link(fill)

    world_bbox = [body.matrix_world @ Vector(corner) for corner in body.bound_box]
    zmin = min(point.z for point in world_bbox)
    zmax = max(point.z for point in world_bbox)
    cx = sum(point.x for point in world_bbox) / 8.0
    cy = sum(point.y for point in world_bbox) / 8.0
    hip = Vector((cx, cy, zmin + (zmax - zmin) * 0.40))
    crotch = Vector((cx, cy, zmin + (zmax - zmin) * 0.28))

    def shot(location, target, filename):
        camera.location = Vector(location)
        camera.rotation_euler = (
            Vector(target) - camera.location
        ).normalized().to_track_quat("-Z", "Y").to_euler()
        sun.rotation_euler = camera.rotation_euler
        fill.location = camera.location * 0.8 + Vector(target) * 0.2
        fill.rotation_euler = camera.rotation_euler
        scene.render.filepath = os.path.join(SHOT_DIR, filename)
        bpy.ops.render.render(write_still=True)
        base.log("SHOT", scene.render.filepath)

    shot((cx, cy - 2.0, hip.z), hip, "smooth_front.png")
    shot((cx, cy + 2.0, hip.z + 0.12), hip, "smooth_back.png")
    shot((cx, cy - 0.9, zmin - 0.5), crotch, "smooth_below.png")


def main() -> None:
    obj = bpy.data.objects.get(base.OUT_NAME)
    body = bpy.data.objects.get(base.BODY_NAME)
    if obj is None or obj.type != "MESH" or body is None or body.type != "MESH":
        raise RuntimeError("Validated fair mesh or body is missing")

    mesh = obj.data
    topology = base.topology_audit(mesh)
    if (
        topology["components"] != 1
        or topology["boundary_loops"] != 3
        or topology["euler"] != -1
    ):
        raise RuntimeError(f"Fair input topology changed: {topology}")
    if base.count_self_overlaps(mesh) != 0:
        raise RuntimeError("Fair input is not intersection-free")

    verts = np.array([tuple(vertex.co) for vertex in mesh.vertices], dtype=np.float64)
    faces = base.face_array(mesh)
    edges = np.asarray([tuple(edge.vertices) for edge in mesh.edges], dtype=np.int32)
    boundary_ids = np.asarray(
        [index for loop in base.ordered_boundary_loops(mesh) for index in loop],
        dtype=np.int32,
    )
    boundary_before = verts[boundary_ids].copy()

    distance = base.graph_distance_to_boundary(verts, edges, boundary_ids)
    weight = base.smoothstep01(distance / (BULGE_RAMP_MM / 1000.0))
    normals = smooth_vertex_normals(verts, faces, edges)
    displacement = normals * weight[:, None] * (BULGE_MM / 1000.0)
    result = verts + displacement
    result[boundary_ids] = boundary_before

    for index, vertex in enumerate(mesh.vertices):
        vertex.co = Vector(tuple(result[index]))
    mesh.update()
    base.ensure_outward_normals(mesh, base.build_body_bvh(body)[0])

    boundary_error = np.linalg.norm(result[boundary_ids] - boundary_before, axis=1)
    overlaps = base.count_self_overlaps(mesh)
    topology_after = base.topology_audit(mesh)
    base.log(
        "BULGE",
        f"mm p50={np.median(weight)*BULGE_MM:.2f} "
        f"p90={np.percentile(weight, 90)*BULGE_MM:.2f} max={weight.max()*BULGE_MM:.2f}",
    )
    base.log(
        "BOUNDARY",
        f"rms={np.sqrt(np.mean(boundary_error**2))*1000:.6f}mm "
        f"max={boundary_error.max()*1000:.6f}mm",
    )
    base.log("SELF-OVERLAP", str(overlaps))
    if boundary_error.max() > 1.0e-9 or overlaps != 0 or topology_after != topology:
        raise RuntimeError("Deterministic bulge failed a geometry hard gate")

    for polygon in mesh.polygons:
        polygon.use_smooth = True
    for modifier_name in ("FundoshiSubdivision", "FundoshiThickness"):
        old = obj.modifiers.get(modifier_name)
        if old:
            obj.modifiers.remove(old)
    base.add_render_modifiers(obj)

    bpy.ops.wm.save_as_mainfile(filepath=OUT_BLEND, compress=True)
    base.log("SAVED", OUT_BLEND)
    render_three(body)
    bpy.ops.wm.save_as_mainfile(filepath=OUT_BLEND, compress=True)
    base.log("DONE", OUT_BLEND)


if __name__ == "__main__":
    main()
