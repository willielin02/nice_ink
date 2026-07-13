"""Build the fundoshi as one connected pair-of-pants surface.

The three texture-exact curves already exist in the input blend as
EdgeTexel0/1/2.  The hidden FundoshiShell supplies only the *connectivity*: it
is a single genus-0 mesh with three boundary loops.  This script deliberately does
not recreate the later band+strip construction, because two overlapping
patches can never have curvature continuity at the crotch joins.

Pipeline
--------
1. Copy FundoshiShell (one component, three boundaries) and regularize its
   historical geometry against the hidden smooth body shell.
2. Triangulate only after that cleanup, then arc-length map all boundaries.
3. Solve one positive-weight harmonic *deformation field* with all three
   curves fixed.  The discrete maximum principle prevents the narrow crotch
   strip from overshooting and folding back through itself.
4. Push the inner cloth surface outside the body and give it a small outward
   bias.
5. Run one native Blender Cloth relaxation with pinned boundaries, angular
   bending, body collision, and a tiny negative Shrinking Factor (grow).
6. Bake the result, add outward-only thickness, audit, render, and save v72.

Run:
  blender --background SourceAssets/previews/sumo_avatar_v71_sewn.blend \
    --python Tools/AssetPrep/build_fundoshi_pairpants.py

Pass -- --no-cloth to stop after the harmonic/obstacle initialization.
"""

from __future__ import annotations

import bmesh
import bpy
import heapq
import math
import os
import sys
from collections import defaultdict

import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree


ROOT = r"C:\games\Unreal Engine\nice_ink"
PREVIEWS = os.path.join(ROOT, "SourceAssets", "previews")
OUT_BLEND = os.path.join(PREVIEWS, "sumo_avatar_v72_pairpants_bulged.blend")
FAIR_BLEND = os.path.join(PREVIEWS, "sumo_avatar_v72_pairpants_fair.blend")
SHOT_DIR = os.path.join(ROOT, "Saved", "V72")

BODY_NAME = "SumoRetopo"
# FundoshiShell and FundoshiBandFace have the same correct pair-of-pants
# topology.  The earlier shell has substantially healthier edges/faces
# (measured before implementation: 77 vs 523 triangles under 2 degrees after
# triangulation), so it is the safer connectivity seed.  Its geometry is still
# discarded/regularized below.
SEED_NAME = "FundoshiShell"
SMOOTH_PROXY_NAME = "ClothShellS"
OUT_NAME = "FundoshiPairPants"
CURVE_NAMES = ("EdgeTexel0", "EdgeTexel1", "EdgeTexel2")

# Blender 5.1 hard-clamps both cloth and collider distance to >= 1 mm.
# Their sum is the effective separation, so pin the inner fabric surface 2 mm
# away from the skin.  Solidify grows only outward afterward.
COLLIDER_MM = 1.0
CLOTH_COLLISION_MM = 1.0
BOUNDARY_CLEAR_MM = COLLIDER_MM + CLOTH_COLLISION_MM
SOLIDIFY_MM = 2.0

# The body collision already blocks the inward buckling branch.  A previous
# per-vertex body-normal bias created 86 new triangle intersections in the
# narrow crotch strip, so the deterministic start stays unbiased.
INITIAL_BULGE_MM = 0.0
NO_GROW_COLLAR_MM = 30.0
GROW_FACTOR = -0.0015

SIM_END_FRAME = 80
CG_TOL = 1.0e-8
CG_MAX = 12000

PRE_RELAX_ITERS = 28
# The harmonic result is already intersection-free.  Further unconstrained
# envelope averaging was measured to introduce seven intersections, so do not
# smooth it a second time before collision.
POST_RELAX_ITERS = 0
RELAX_LAMBDA = 0.42

DO_CLOTH = "--no-cloth" not in sys.argv
DO_RENDER = "--no-render" not in sys.argv


def log(tag: str, text: str) -> None:
    print(f"[{tag}] {text}", flush=True)


def as_np(v: Vector) -> np.ndarray:
    return np.array((v.x, v.y, v.z), dtype=np.float64)


def smoothstep01(x: np.ndarray) -> np.ndarray:
    x = np.clip(x, 0.0, 1.0)
    return x * x * (3.0 - 2.0 * x)


def mesh_object_local_to_body(obj: bpy.types.Object, body: bpy.types.Object) -> np.ndarray:
    """Return object vertices expressed in body-local coordinates."""
    xform = body.matrix_world.inverted() @ obj.matrix_world
    return np.array([tuple(xform @ v.co) for v in obj.data.vertices], dtype=np.float64)


def curve_points_body_local(obj: bpy.types.Object, body: bpy.types.Object) -> np.ndarray:
    assert obj.type == "CURVE" and len(obj.data.splines) == 1
    sp = obj.data.splines[0]
    xform = body.matrix_world.inverted() @ obj.matrix_world
    return np.array([tuple(xform @ Vector((p.co.x, p.co.y, p.co.z))) for p in sp.points], dtype=np.float64)


def resample_closed(points: np.ndarray, count: int) -> np.ndarray:
    seg = np.linalg.norm(np.roll(points, -1, axis=0) - points, axis=1)
    total = float(seg.sum())
    assert total > 1.0e-9
    arc = np.concatenate(([0.0], np.cumsum(seg)))
    t = np.linspace(0.0, total, count, endpoint=False)
    idx = np.searchsorted(arc, t, side="right") - 1
    idx = np.clip(idx, 0, len(points) - 1)
    frac = (t - arc[idx]) / np.maximum(seg[idx], 1.0e-12)
    nxt = (idx + 1) % len(points)
    return points[idx] * (1.0 - frac[:, None]) + points[nxt] * frac[:, None]


def sample_closed_fractions(points: np.ndarray, fractions: np.ndarray) -> np.ndarray:
    seg = np.linalg.norm(np.roll(points, -1, axis=0) - points, axis=1)
    total = float(seg.sum())
    arc = np.concatenate(([0.0], np.cumsum(seg)))
    t = np.mod(fractions, 1.0) * total
    idx = np.searchsorted(arc, t, side="right") - 1
    idx = np.clip(idx, 0, len(points) - 1)
    local = (t - arc[idx]) / np.maximum(seg[idx], 1.0e-12)
    nxt = (idx + 1) % len(points)
    return points[idx] * (1.0 - local[:, None]) + points[nxt] * local[:, None]


def polyline_length_closed(points: np.ndarray) -> float:
    return float(np.linalg.norm(np.roll(points, -1, axis=0) - points, axis=1).sum())


def ordered_boundary_loops(mesh: bpy.types.Mesh) -> list[list[int]]:
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bm.verts.ensure_lookup_table()
    bm.edges.ensure_lookup_table()
    boundary = {e for e in bm.edges if len(e.link_faces) == 1}
    loops: list[list[int]] = []

    while boundary:
        first = next(iter(boundary))
        boundary.remove(first)
        start = first.verts[0]
        current = first.verts[1]
        previous = first
        loop = [start.index]

        guard = 0
        while current != start:
            loop.append(current.index)
            candidates = [
                e for e in current.link_edges
                if e != previous and e in boundary and len(e.link_faces) == 1
            ]
            if len(candidates) != 1:
                bm.free()
                raise RuntimeError(
                    f"Boundary is not a simple loop at vertex {current.index}: "
                    f"{len(candidates)} candidates"
                )
            edge = candidates[0]
            boundary.remove(edge)
            previous = edge
            current = edge.other_vert(current)
            guard += 1
            if guard > len(mesh.edges):
                bm.free()
                raise RuntimeError("Boundary traversal guard tripped")

        loops.append(loop)

    bm.free()
    return loops


def classify_loops(loops: list[list[int]], verts: np.ndarray) -> dict[str, list[int]]:
    stats = []
    for loop in loops:
        p = verts[np.asarray(loop, dtype=np.int32)]
        stats.append((loop, polyline_length_closed(p), p.mean(axis=0)))
    assert len(stats) == 3, f"Expected three boundary loops, got {len(stats)}"

    waist = max(stats, key=lambda item: item[1])
    legs = [item for item in stats if item is not waist]
    neg = min(legs, key=lambda item: item[2][0])
    pos = max(legs, key=lambda item: item[2][0])

    log(
        "SEED-LOOPS",
        "; ".join(
            f"n={len(loop)} len={length:.4f}m center=({c[0]:.3f},{c[1]:.3f},{c[2]:.3f})"
            for loop, length, c in stats
        ),
    )
    return {"waist": waist[0], "neg": neg[0], "pos": pos[0]}


def classify_curves(curves: list[np.ndarray]) -> dict[str, np.ndarray]:
    stats = [(p, polyline_length_closed(p), p.mean(axis=0)) for p in curves]
    waist = max(stats, key=lambda item: item[1])
    legs = [item for item in stats if item is not waist]
    neg = min(legs, key=lambda item: item[2][0])
    pos = max(legs, key=lambda item: item[2][0])
    log(
        "TARGET-CURVES",
        "; ".join(
            f"pts={len(p)} len={length:.4f}m center=({c[0]:.3f},{c[1]:.3f},{c[2]:.3f})"
            for p, length, c in stats
        ),
    )
    return {"waist": waist[0], "neg": neg[0], "pos": pos[0]}


def best_cyclic_alignment(seed: np.ndarray, target_curve: np.ndarray) -> tuple[np.ndarray, float, float, int, int]:
    # Preserve the seed loop's non-uniform vertex spacing.  Uniformly sampling
    # the target by point count caused 45-50 mm phase errors on the historical
    # boundary even though its geometric nearest error was only ~5-16 mm.
    seed_seg = np.linalg.norm(np.roll(seed, -1, axis=0) - seed, axis=1)
    seed_fraction = np.concatenate(([0.0], np.cumsum(seed_seg[:-1]))) / max(seed_seg.sum(), 1.0e-12)
    best = None
    for direction in (1, -1):
        signed_fraction = seed_fraction if direction == 1 else -seed_fraction
        # A dense phase search is cheap (<= 539 vertices) and avoids assuming
        # raw curve indices are proportional to arc length.
        phase_steps = 2048
        for shift in range(phase_steps):
            phase = shift / phase_steps
            candidate = sample_closed_fractions(target_curve, signed_fraction + phase)
            dist = np.linalg.norm(seed - candidate, axis=1)
            cost = float(np.dot(dist, dist))
            if best is None or cost < best[0]:
                best = (cost, candidate.copy(), float(np.sqrt(np.mean(dist * dist))), float(dist.max()), direction, shift)
    assert best is not None
    return best[1], best[2], best[3], best[4], best[5]


def build_body_bvh(body: bpy.types.Object) -> tuple[BVHTree, bpy.types.Mesh]:
    depsgraph = bpy.context.evaluated_depsgraph_get()
    evaluated = body.evaluated_get(depsgraph)
    mesh = bpy.data.meshes.new_from_object(evaluated, preserve_all_data_layers=False, depsgraph=depsgraph)
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bvh = BVHTree.FromBMesh(bm)
    bm.free()
    return bvh, mesh


def build_object_bvh_in_body_space(obj: bpy.types.Object, body: bpy.types.Object) -> BVHTree:
    """Build a BVH after expressing an object's mesh in body-local space."""
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bm.transform(body.matrix_world.inverted() @ obj.matrix_world)
    bvh = BVHTree.FromBMesh(bm)
    bm.free()
    return bvh


def nearest_body(bvh: BVHTree, point: np.ndarray) -> tuple[np.ndarray, np.ndarray, float]:
    loc, normal, _face, distance = bvh.find_nearest(Vector(tuple(point)))
    if loc is None:
        raise RuntimeError("Body BVH nearest query failed")
    p = Vector(tuple(point))
    sign = 1.0 if (p - loc).dot(normal) >= 0.0 else -1.0
    return as_np(loc), as_np(normal).astype(np.float64), sign * float(distance)


def offset_targets_outside(target: np.ndarray, bvh: BVHTree, clear_m: float) -> np.ndarray:
    out = target.copy()
    for i, p in enumerate(target):
        loc, normal, _sd = nearest_body(bvh, p)
        out[i] = loc + normal * clear_m
    return out


def graph_biharmonic_system(vertex_count: int, edges: np.ndarray):
    """Geometry-independent first solve.

    FundoshiBandFace has the right topology but deliberately bad historical
    geometry (folded n-gons and near-zero edges).  Cotangents computed on that
    geometry are unusable, so first untangle the connectivity with a uniform
    graph bi-Laplacian.  Cotangents are rebuilt only after this pass.
    """
    ei, ej = edges[:, 0], edges[:, 1]
    degree = np.zeros(vertex_count, dtype=np.float64)
    np.add.at(degree, ei, 1.0)
    np.add.at(degree, ej, 1.0)

    def lap(y: np.ndarray) -> np.ndarray:
        one_dim = y.ndim == 1
        yy = y[:, None] if one_dim else y
        out = degree[:, None] * yy
        np.add.at(out, ei, -yy[ej])
        np.add.at(out, ej, -yy[ei])
        return out[:, 0] if one_dim else out

    def biharmonic(y: np.ndarray) -> np.ndarray:
        return lap(lap(y))

    # diag(L^2) = degree^2 + degree for an unweighted simple graph.
    diagonal = np.maximum(degree * degree + degree, 1.0)
    return biharmonic, diagonal


def graph_laplacian_system(vertex_count: int, edges: np.ndarray):
    """Positive uniform graph Laplacian for a maximum-principle deformation.

    The inherited triangulation has a small number of skinny triangles.  A
    uniform positive stencil is less metric-accurate than cotangents, but it is
    robust here and, crucially, cannot create a displacement outside the range
    prescribed on the three boundary curves.
    """
    ei, ej = edges[:, 0], edges[:, 1]
    degree = np.zeros(vertex_count, dtype=np.float64)
    np.add.at(degree, ei, 1.0)
    np.add.at(degree, ej, 1.0)
    degree = np.maximum(degree, 1.0)

    def lap(y: np.ndarray) -> np.ndarray:
        one_dim = y.ndim == 1
        yy = y[:, None] if one_dim else y
        out = degree[:, None] * yy
        np.add.at(out, ei, -yy[ej])
        np.add.at(out, ej, -yy[ei])
        return out[:, 0] if one_dim else out

    return lap, degree


def triangle_quality(verts: np.ndarray, faces: np.ndarray, tag: str) -> dict[str, float]:
    min_angles = []
    areas = []
    aspects = []
    for tri in faces:
        p = verts[tri]
        lengths = np.array(
            [
                np.linalg.norm(p[1] - p[0]),
                np.linalg.norm(p[2] - p[1]),
                np.linalg.norm(p[0] - p[2]),
            ]
        )
        area2 = np.linalg.norm(np.cross(p[1] - p[0], p[2] - p[0]))
        area = 0.5 * area2
        areas.append(area)
        aspects.append(float(lengths.max() / max(lengths.min(), 1.0e-12)))
        angles = []
        for i in range(3):
            a = p[(i + 1) % 3] - p[i]
            b = p[(i + 2) % 3] - p[i]
            cosine = np.dot(a, b) / max(np.linalg.norm(a) * np.linalg.norm(b), 1.0e-18)
            angles.append(math.degrees(math.acos(float(np.clip(cosine, -1.0, 1.0)))))
        min_angles.append(min(angles))
    ma = np.asarray(min_angles)
    ar = np.asarray(areas)
    asp = np.asarray(aspects)
    result = {
        "min_angle": float(ma.min()),
        "p01_angle": float(np.percentile(ma, 1)),
        "under_2deg": int((ma < 2.0).sum()),
        "min_area_mm2": float(ar.min() * 1.0e6),
        "max_aspect": float(asp.max()),
    }
    log(
        "TRI-QUALITY",
        f"{tag}: minAngle={result['min_angle']:.3f}deg "
        f"p01={result['p01_angle']:.3f}deg under2={result['under_2deg']} "
        f"minArea={result['min_area_mm2']:.4f}mm2 maxAspect={result['max_aspect']:.1f}",
    )
    return result


def cotangent_system(verts: np.ndarray, faces: np.ndarray):
    weights: dict[tuple[int, int], float] = defaultdict(float)
    mass = np.zeros(len(verts), dtype=np.float64)

    for tri in faces:
        i, j, k = map(int, tri)
        a, b, c = verts[i], verts[j], verts[k]
        cross = np.cross(b - a, c - a)
        twice_area = float(np.linalg.norm(cross))
        if twice_area < 1.0e-14:
            continue
        area = 0.5 * twice_area
        mass[[i, j, k]] += area / 3.0

        def cot_at(p: np.ndarray, q: np.ndarray, r: np.ndarray) -> float:
            u, v = q - p, r - p
            den = float(np.linalg.norm(np.cross(u, v)))
            return float(np.dot(u, v) / max(den, 1.0e-14))

        weights[tuple(sorted((j, k)))] += 0.5 * cot_at(a, b, c)
        weights[tuple(sorted((k, i)))] += 0.5 * cot_at(b, c, a)
        weights[tuple(sorted((i, j)))] += 0.5 * cot_at(c, a, b)

    mass = np.maximum(mass, 1.0e-12)
    edges = np.asarray(list(weights.keys()), dtype=np.int32)
    w_raw = np.asarray([weights[tuple(edge)] for edge in edges], dtype=np.float64)
    # A positive cotan Laplacian is much safer for an inherited, non-Delaunay
    # triangulation.  Negative weights are replaced only after the uniform
    # untangling pass, so this is a stability guard rather than the old
    # geometry dictating the result.
    w = np.maximum(w_raw, 1.0e-6)
    ei, ej = edges[:, 0], edges[:, 1]
    degree = np.zeros(len(verts), dtype=np.float64)
    np.add.at(degree, ei, w)
    np.add.at(degree, ej, w)

    def lap(y: np.ndarray) -> np.ndarray:
        one_dim = y.ndim == 1
        yy = y[:, None] if one_dim else y
        out = np.zeros_like(yy)
        diff = (yy[ei] - yy[ej]) * w[:, None]
        np.add.at(out, ei, diff)
        np.add.at(out, ej, -diff)
        return out[:, 0] if one_dim else out

    def biharmonic(y: np.ndarray) -> np.ndarray:
        ly = lap(y)
        return lap(ly / (mass if ly.ndim == 1 else mass[:, None]))

    # Exact diagonal of L M^-1 L for a Jacobi preconditioner.
    diag = degree * degree / mass
    np.add.at(diag, ei, (w * w) / mass[ej])
    np.add.at(diag, ej, (w * w) / mass[ei])
    diag = np.maximum(diag, 1.0e-12)

    log(
        "COTAN",
        f"edges={len(edges)} raw min={w_raw.min():.4g} max={w_raw.max():.4g} "
        f"negative_clamped={(w_raw < 0).sum()}",
    )
    return edges, lap, biharmonic, diag


def pcg(
    apply_a,
    rhs: np.ndarray,
    initial: np.ndarray,
    precond_diag: np.ndarray,
    tol: float,
    max_iter: int,
) -> tuple[np.ndarray, int, float]:
    x = initial.copy()
    r = rhs - apply_a(x)
    z = r / precond_diag[:, None]
    p = z.copy()
    rz = float(np.sum(r * z))
    r0 = max(float(np.linalg.norm(r)), 1.0e-30)
    rel = 1.0

    for it in range(max_iter):
        ap = apply_a(p)
        denom = float(np.sum(p * ap))
        if denom <= 1.0e-30:
            log("PCG", f"non-positive denominator at iter {it}: {denom:.3e}")
            return x, it, rel
        alpha = rz / denom
        x += alpha * p
        r -= alpha * ap
        rel = float(np.linalg.norm(r)) / r0
        if rel < tol:
            return x, it + 1, rel
        z = r / precond_diag[:, None]
        rz_new = float(np.sum(r * z))
        beta = rz_new / max(rz, 1.0e-30)
        p = z + beta * p
        rz = rz_new
    return x, max_iter, rel


def solve_dirichlet_field(
    seed: np.ndarray,
    boundary_ids: np.ndarray,
    boundary_targets: np.ndarray,
    apply_operator,
    diagonal: np.ndarray,
    tag: str,
) -> np.ndarray:
    fixed = np.zeros(len(seed), dtype=bool)
    fixed[boundary_ids] = True
    free = ~fixed
    constrained = np.zeros_like(seed)
    constrained[boundary_ids] = boundary_targets
    rhs = -apply_operator(constrained)[free]

    def apply_free(x_free: np.ndarray) -> np.ndarray:
        full = np.zeros_like(seed)
        full[free] = x_free
        return apply_operator(full)[free]

    result_free, iters, rel = pcg(
        apply_free,
        rhs,
        seed[free],
        diagonal[free],
        CG_TOL,
        CG_MAX,
    )
    result = constrained.copy()
    result[free] = result_free
    log(tag, f"free={free.sum()} iterations={iters} relative_residual={rel:.3e}")
    return result


def project_outside(
    verts: np.ndarray,
    bvh: BVHTree,
    clear_m: float,
    fixed: np.ndarray,
) -> tuple[np.ndarray, int, float]:
    out = verts.copy()
    moved = 0
    worst = 1.0e9
    for i, p in enumerate(verts):
        loc, normal, signed = nearest_body(bvh, p)
        worst = min(worst, signed)
        if not fixed[i] and signed < clear_m:
            out[i] = loc + normal * clear_m
            moved += 1
    return out, moved, worst


def envelope_relax(
    verts: np.ndarray,
    edges: np.ndarray,
    fixed: np.ndarray,
    fixed_targets: np.ndarray,
    proxy_bvh: BVHTree,
    iterations: int,
    lam: float,
    proxy_clear_m: float = 0.0,
) -> np.ndarray:
    """Globally smooth while treating the low-pass body shell as a floor.

    This is deliberately a convex neighbor average (no Taubin negative step),
    so it cannot oscillate.  Only candidates that fall below the already-
    smooth proxy are projected back out; points above it remain free to bridge
    butt/crotch concavities.
    """
    ei, ej = edges[:, 0], edges[:, 1]
    degree = np.zeros(len(verts), dtype=np.float64)
    np.add.at(degree, ei, 1.0)
    np.add.at(degree, ej, 1.0)
    degree = np.maximum(degree, 1.0)
    out = verts.copy()

    for iteration in range(iterations):
        neighbor_sum = np.zeros_like(out)
        np.add.at(neighbor_sum, ei, out[ej])
        np.add.at(neighbor_sum, ej, out[ei])
        average = neighbor_sum / degree[:, None]
        candidate = out.copy()
        candidate[~fixed] = out[~fixed] + lam * (average[~fixed] - out[~fixed])

        projected = 0
        for i in np.where(~fixed)[0]:
            loc, normal, signed = nearest_body(proxy_bvh, candidate[i])
            if signed < proxy_clear_m:
                candidate[i] = loc + normal * proxy_clear_m
                projected += 1
        if len(fixed_targets) != len(candidate):
            raise RuntimeError("envelope_relax fixed_targets must be a full vertex array")
        candidate[fixed] = fixed_targets[fixed]
        out = candidate
        if iteration == 0 or (iteration + 1) % 5 == 0 or iteration + 1 == iterations:
            log("ENVELOPE", f"iter={iteration+1}/{iterations} projected={projected}")
    return out


def graph_distance_to_boundary(verts: np.ndarray, edges: np.ndarray, boundary_ids: np.ndarray) -> np.ndarray:
    adjacency: list[list[tuple[int, float]]] = [[] for _ in range(len(verts))]
    for i, j in edges:
        length = float(np.linalg.norm(verts[i] - verts[j]))
        adjacency[int(i)].append((int(j), length))
        adjacency[int(j)].append((int(i), length))

    dist = np.full(len(verts), np.inf, dtype=np.float64)
    heap: list[tuple[float, int]] = []
    for v in boundary_ids:
        dist[int(v)] = 0.0
        heapq.heappush(heap, (0.0, int(v)))

    while heap:
        d, v = heapq.heappop(heap)
        if d != dist[v]:
            continue
        for n, w in adjacency[v]:
            nd = d + w
            if nd < dist[n]:
                dist[n] = nd
                heapq.heappush(heap, (nd, n))
    return dist


def face_array(mesh: bpy.types.Mesh) -> np.ndarray:
    faces = np.asarray([[v for v in p.vertices] for p in mesh.polygons], dtype=np.int32)
    assert faces.ndim == 2 and faces.shape[1] == 3, "Mesh must be triangulated"
    return faces


def ensure_outward_normals(mesh: bpy.types.Mesh, bvh: BVHTree) -> None:
    verts = np.array([tuple(v.co) for v in mesh.vertices], dtype=np.float64)
    faces = face_array(mesh)
    dots = []
    for tri in faces[::max(1, len(faces) // 500)]:
        a, b, c = verts[tri]
        normal = np.cross(b - a, c - a)
        length = np.linalg.norm(normal)
        if length < 1.0e-12:
            continue
        normal /= length
        center = (a + b + c) / 3.0
        _loc, body_normal, _signed = nearest_body(bvh, center)
        dots.append(float(np.dot(normal, body_normal)))
    mean_dot = float(np.mean(dots)) if dots else 0.0
    if mean_dot < 0.0:
        bm = bmesh.new()
        bm.from_mesh(mesh)
        bmesh.ops.reverse_faces(bm, faces=list(bm.faces))
        bm.to_mesh(mesh)
        bm.free()
        mesh.update()
        log("NORMALS", f"reversed all faces (sample mean dot {mean_dot:.3f})")
    else:
        log("NORMALS", f"orientation already outward (sample mean dot {mean_dot:.3f})")


def topology_audit(mesh: bpy.types.Mesh) -> dict[str, int]:
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bm.verts.ensure_lookup_table()
    bm.edges.ensure_lookup_table()
    bm.faces.ensure_lookup_table()

    seen = set()
    components = 0
    for face in bm.faces:
        if face in seen:
            continue
        components += 1
        stack = [face]
        seen.add(face)
        while stack:
            current = stack.pop()
            for edge in current.edges:
                for linked in edge.link_faces:
                    if linked not in seen:
                        seen.add(linked)
                        stack.append(linked)

    boundary_edges = {e for e in bm.edges if len(e.link_faces) == 1}
    remaining = set(boundary_edges)
    boundary_loops = 0
    while remaining:
        boundary_loops += 1
        edge = remaining.pop()
        stack = [edge]
        while stack:
            current = stack.pop()
            for vert in current.verts:
                for linked in vert.link_edges:
                    if linked in remaining and len(linked.link_faces) == 1:
                        remaining.remove(linked)
                        stack.append(linked)

    nonmanifold = sum(1 for e in bm.edges if len(e.link_faces) not in (1, 2))
    result = {
        "vertices": len(bm.verts),
        "edges": len(bm.edges),
        "faces": len(bm.faces),
        "components": components,
        "boundary_loops": boundary_loops,
        "nonmanifold_edges": nonmanifold,
        "euler": len(bm.verts) - len(bm.edges) + len(bm.faces),
    }
    bm.free()
    log("TOPOLOGY", " ".join(f"{k}={v}" for k, v in result.items()))
    return result


def signed_clearance_samples(mesh: bpy.types.Mesh, bvh: BVHTree) -> np.ndarray:
    verts = np.array([tuple(v.co) for v in mesh.vertices], dtype=np.float64)
    samples = [verts]
    samples.append(np.array([(verts[e.vertices[0]] + verts[e.vertices[1]]) * 0.5 for e in mesh.edges]))
    samples.append(np.array([verts[list(p.vertices)].mean(axis=0) for p in mesh.polygons]))
    all_points = np.concatenate(samples, axis=0)
    distances = np.empty(len(all_points), dtype=np.float64)
    for i, point in enumerate(all_points):
        _loc, _normal, distances[i] = nearest_body(bvh, point)
    return distances


def self_overlap_pairs(mesh: bpy.types.Mesh) -> list[tuple[int, int]]:
    verts = [tuple(v.co) for v in mesh.vertices]
    faces = [tuple(p.vertices) for p in mesh.polygons]
    bvh = BVHTree.FromPolygons(verts, faces, all_triangles=True)
    vertex_sets = [set(f) for f in faces]
    pairs = []
    for a, b in bvh.overlap(bvh):
        if a >= b or vertex_sets[a].intersection(vertex_sets[b]):
            continue
        pairs.append((a, b))
    return sorted(pairs)


def count_self_overlaps(mesh: bpy.types.Mesh) -> int:
    return len(self_overlap_pairs(mesh))


def count_array_self_overlaps(verts: np.ndarray, faces: np.ndarray) -> int:
    bvh = BVHTree.FromPolygons(
        [tuple(p) for p in verts],
        [tuple(map(int, face)) for face in faces],
        all_triangles=True,
    )
    vertex_sets = [set(map(int, face)) for face in faces]
    count = 0
    for a, b in bvh.overlap(bvh):
        if a >= b or vertex_sets[a].intersection(vertex_sets[b]):
            continue
        count += 1
    return count


def beautify_triangulation(mesh: bpy.types.Mesh) -> None:
    """Flip only interior diagonals using the final 3D geometry.

    The seed is triangulated before boundary deformation so the solver has a
    fixed stencil.  A few triangles can become needle-thin afterward; those
    are numerically dangerous for Cloth collision.  Beauty-fill preserves all
    vertices and the three boundary loops while choosing healthier diagonals.
    """
    bm = bmesh.new()
    bm.from_mesh(mesh)
    before_edges = len(bm.edges)
    result = bmesh.ops.beautify_fill(
        bm,
        faces=list(bm.faces),
        edges=[edge for edge in bm.edges if len(edge.link_faces) == 2],
        use_restrict_tag=False,
        method="AREA",
    )
    bm.to_mesh(mesh)
    bm.free()
    mesh.update()
    log("BEAUTIFY", f"input_edges={before_edges} changed_geom={len(result.get('geom', []))}")


def local_triangle_quality(verts: np.ndarray, triangles: list[np.ndarray]) -> tuple[float, float]:
    min_angle = 180.0
    max_aspect = 0.0
    for triangle in triangles:
        points = verts[np.asarray(triangle, dtype=np.int32)]
        lengths = np.array(
            [
                np.linalg.norm(points[1] - points[0]),
                np.linalg.norm(points[2] - points[1]),
                np.linalg.norm(points[0] - points[2]),
            ]
        )
        max_aspect = max(max_aspect, float(lengths.max() / max(lengths.min(), 1.0e-12)))
        for i in range(3):
            a = points[(i + 1) % 3] - points[i]
            b = points[(i + 2) % 3] - points[i]
            cosine = np.dot(a, b) / max(np.linalg.norm(a) * np.linalg.norm(b), 1.0e-18)
            min_angle = min(
                min_angle,
                math.degrees(math.acos(float(np.clip(cosine, -1.0, 1.0)))),
            )
    return min_angle, max_aspect


def untangle_with_edge_rotations(mesh: bpy.types.Mesh, max_flips: int = 32) -> int:
    """Remove post-Cloth crossings by strictly improving interior edge flips.

    No vertex moves, so the exact three curves, baked bulge, skin clearance,
    and vertex animation correspondence remain unchanged.  UV/custom loop data
    are retained by BMesh's native rotate operation.
    """
    flips = 0
    for _pass in range(max_flips):
        overlaps = self_overlap_pairs(mesh)
        if not overlaps:
            return flips

        verts = np.array([tuple(vertex.co) for vertex in mesh.vertices], dtype=np.float64)
        faces = face_array(mesh)
        edge_faces: dict[tuple[int, int], list[int]] = defaultdict(list)
        for face_index, triangle in enumerate(faces):
            for i in range(3):
                edge = tuple(sorted((int(triangle[i]), int(triangle[(i + 1) % 3]))))
                edge_faces[edge].append(face_index)

        candidates: set[tuple[int, int]] = set()
        for face_a, face_b in overlaps:
            for face_index in (face_a, face_b):
                triangle = faces[face_index]
                for i in range(3):
                    edge = tuple(sorted((int(triangle[i]), int(triangle[(i + 1) % 3]))))
                    if len(edge_faces[edge]) == 2:
                        candidates.add(edge)

        best = None
        current_count = len(overlaps)
        for edge in sorted(candidates):
            linked = edge_faces[edge]
            if len(linked) != 2:
                continue
            face_0, face_1 = linked
            u, v = edge
            opposite_0 = next((int(x) for x in faces[face_0] if int(x) not in edge), None)
            opposite_1 = next((int(x) for x in faces[face_1] if int(x) not in edge), None)
            if opposite_0 is None or opposite_1 is None or opposite_0 == opposite_1:
                continue
            new_edge = tuple(sorted((opposite_0, opposite_1)))
            if new_edge in edge_faces:
                continue

            trial_faces = faces.copy()
            triangle_0 = np.asarray((opposite_0, opposite_1, u), dtype=np.int32)
            triangle_1 = np.asarray((opposite_1, opposite_0, v), dtype=np.int32)
            trial_faces[face_0] = triangle_0
            trial_faces[face_1] = triangle_1
            trial_count = count_array_self_overlaps(verts, trial_faces)
            if trial_count >= current_count:
                continue
            min_angle, max_aspect = local_triangle_quality(
                verts,
                [triangle_0, triangle_1],
            )
            score = (trial_count, -min_angle, max_aspect, edge)
            if best is None or score < best[0]:
                best = (score, edge, min_angle, max_aspect)

        if best is None:
            raise RuntimeError(
                f"Self-overlap cleanup stalled at {current_count} pairs; no improving edge flip"
            )

        _score, edge, min_angle, max_aspect = best
        bm = bmesh.new()
        bm.from_mesh(mesh)
        bm.verts.ensure_lookup_table()
        bm.edges.ensure_lookup_table()
        bm_edge = bm.edges.get((bm.verts[edge[0]], bm.verts[edge[1]]))
        if bm_edge is None or len(bm_edge.link_faces) != 2:
            bm.free()
            raise RuntimeError(f"Selected edge {edge} is no longer rotatable")
        result = bmesh.ops.rotate_edges(bm, edges=[bm_edge], use_ccw=False)
        if not result.get("edges"):
            bm.free()
            raise RuntimeError(f"BMesh failed to rotate selected edge {edge}")
        bm.to_mesh(mesh)
        bm.free()
        mesh.update()

        new_count = count_self_overlaps(mesh)
        if new_count >= current_count:
            raise RuntimeError(
                f"Edge flip {edge} did not strictly improve overlaps: {current_count}->{new_count}"
            )
        flips += 1
        log(
            "UNTANGLE",
            f"flip={edge} overlaps={current_count}->{new_count} "
            f"localMinAngle={min_angle:.2f}deg maxAspect={max_aspect:.2f}",
        )

    remaining = count_self_overlaps(mesh)
    raise RuntimeError(f"Self-overlap cleanup exceeded {max_flips} flips; remaining={remaining}")


def configure_cloth(
    obj: bpy.types.Object,
    body: bpy.types.Object,
    boundary_ids: np.ndarray,
    grow_field: np.ndarray,
) -> bpy.types.Modifier:
    pin = obj.vertex_groups.new(name="FundoshiBoundaryPin")
    pin.add([int(v) for v in boundary_ids], 1.0, "REPLACE")

    # Blender interpolates shrink_min at group weight 0 and shrink_max at 1.
    # Negative values mean grow, therefore use 1-grow_field: boundary=1/no grow,
    # broad interior=0/full grow.
    grow = obj.vertex_groups.new(name="FundoshiNoGrowBoundary")
    for i, weight in enumerate(1.0 - grow_field):
        grow.add([i], float(weight), "REPLACE")

    cloth = obj.modifiers.new("FundoshiClothRelax", "CLOTH")
    settings = cloth.settings
    settings.vertex_group_mass = pin.name
    settings.pin_stiffness = 50.0
    settings.vertex_group_shrink = grow.name
    settings.shrink_min = GROW_FACTOR
    settings.shrink_max = 0.0
    settings.bending_model = "ANGULAR"
    settings.quality = 10
    settings.mass = 0.30
    settings.tension_stiffness = 35.0
    settings.compression_stiffness = 35.0
    settings.shear_stiffness = 15.0
    settings.bending_stiffness = 12.0
    settings.tension_damping = 10.0
    settings.compression_damping = 10.0
    settings.shear_damping = 10.0
    settings.bending_damping = 10.0
    settings.air_damping = 3.0
    settings.effector_weights.gravity = 0.0

    collision = cloth.collision_settings
    collision.use_collision = True
    collision.collision_quality = 8
    collision.distance_min = CLOTH_COLLISION_MM / 1000.0
    collision.friction = 5.0
    collision.damping = 0.5
    collision.use_self_collision = True
    collision.self_distance_min = SOLIDIFY_MM / 1000.0
    collision.self_friction = 5.0

    old = body.modifiers.get("FundoshiCollision")
    if old:
        body.modifiers.remove(old)
    body.modifiers.new("FundoshiCollision", "COLLISION")
    assert body.collision is not None
    body.collision.use = True
    body.collision.thickness_outer = COLLIDER_MM / 1000.0
    body.collision.thickness_inner = COLLIDER_MM / 1000.0
    body.collision.cloth_friction = 5.0
    body.collision.damping = 0.5

    cloth.point_cache.frame_start = 1
    cloth.point_cache.frame_end = SIM_END_FRAME
    return cloth


def simulate_and_bake(obj: bpy.types.Object, cloth: bpy.types.Modifier) -> None:
    scene = bpy.context.scene
    scene.frame_start = 1
    scene.frame_end = SIM_END_FRAME
    scene.frame_set(1)
    depsgraph = bpy.context.evaluated_depsgraph_get()

    for frame in range(1, SIM_END_FRAME + 1):
        scene.frame_set(frame)
        depsgraph.update()
        if frame == 1 or frame % 10 == 0 or frame == SIM_END_FRAME:
            log("CLOTH", f"evaluated frame {frame}/{SIM_END_FRAME}")

    evaluated = obj.evaluated_get(depsgraph)
    baked = bpy.data.meshes.new_from_object(
        evaluated,
        preserve_all_data_layers=True,
        depsgraph=depsgraph,
    )
    if len(baked.vertices) != len(obj.data.vertices):
        raise RuntimeError(
            f"Cloth bake changed vertex count: {len(obj.data.vertices)} -> {len(baked.vertices)}"
        )
    old_mesh = obj.data
    obj.modifiers.clear()
    obj.data = baked
    bpy.data.meshes.remove(old_mesh)
    scene.frame_set(1)
    log("CLOTH", f"baked frame {SIM_END_FRAME} into {len(baked.vertices)} vertices")


def add_render_modifiers(obj: bpy.types.Object) -> None:
    sub = obj.modifiers.new("FundoshiSubdivision", "SUBSURF")
    sub.subdivision_type = "CATMULL_CLARK"
    sub.levels = 1
    sub.render_levels = 1
    sub.show_only_control_edges = True

    solid = obj.modifiers.new("FundoshiThickness", "SOLIDIFY")
    solid.thickness = SOLIDIFY_MM / 1000.0
    solid.offset = 1.0
    solid.use_rim = True
    solid.use_even_offset = True


def render_shots(body: bpy.types.Object, focus_obj: bpy.types.Object) -> None:
    os.makedirs(SHOT_DIR, exist_ok=True)
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x = 1280
    scene.render.resolution_y = 960
    scene.render.resolution_percentage = 100

    for obj in list(bpy.data.objects):
        if obj.name.startswith("V72_QC"):
            bpy.data.objects.remove(obj, do_unlink=True)

    camera = bpy.data.objects.new("V72_QC_Camera", bpy.data.cameras.new("V72_QC_Camera"))
    scene.collection.objects.link(camera)
    scene.camera = camera
    sun = bpy.data.objects.new("V72_QC_Sun", bpy.data.lights.new("V72_QC_Sun", "SUN"))
    sun.data.energy = 3.0
    scene.collection.objects.link(sun)
    fill = bpy.data.objects.new("V72_QC_Fill", bpy.data.lights.new("V72_QC_Fill", "AREA"))
    fill.data.energy = 600.0
    fill.data.shape = "DISK"
    fill.data.size = 4.0
    scene.collection.objects.link(fill)

    world_bbox = [body.matrix_world @ Vector(corner) for corner in body.bound_box]
    zmin = min(v.z for v in world_bbox)
    zmax = max(v.z for v in world_bbox)
    cx = sum(v.x for v in world_bbox) / 8.0
    cy = sum(v.y for v in world_bbox) / 8.0
    hip = Vector((cx, cy, zmin + (zmax - zmin) * 0.40))
    crotch = Vector((cx, cy, zmin + (zmax - zmin) * 0.28))

    def shot(location, target, filename):
        camera.location = Vector(location)
        direction = (Vector(target) - camera.location).normalized()
        camera.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
        sun.rotation_euler = camera.rotation_euler
        fill.location = camera.location * 0.8 + Vector(target) * 0.2
        fill.rotation_euler = camera.rotation_euler
        scene.render.filepath = os.path.join(SHOT_DIR, filename)
        bpy.ops.render.render(write_still=True)
        log("SHOT", scene.render.filepath)

    shot((cx, cy - 2.0, hip.z), hip, "v72_front.png")
    shot((cx, cy + 2.0, hip.z + 0.12), hip, "v72_back.png")
    shot((cx - 2.0, cy, hip.z), hip, "v72_side_negx.png")
    shot((cx + 2.0, cy, hip.z), hip, "v72_side_posx.png")
    shot((cx, cy - 0.9, zmin - 0.5), crotch, "v72_below.png")
    shot((cx, cy + 0.85, hip.z - 0.05), Vector((cx, cy, hip.z - 0.08)), "v72_back_close.png")


def main() -> None:
    body = bpy.data.objects.get(BODY_NAME)
    seed_obj = bpy.data.objects.get(SEED_NAME)
    assert body and body.type == "MESH", BODY_NAME
    assert seed_obj and seed_obj.type == "MESH", SEED_NAME
    curves = [bpy.data.objects.get(name) for name in CURVE_NAMES]
    assert all(obj and obj.type == "CURVE" for obj in curves), CURVE_NAMES

    # Remove a stale output object only inside this newly generated preview.
    stale = bpy.data.objects.get(OUT_NAME)
    if stale:
        bpy.data.objects.remove(stale, do_unlink=True)

    mesh = seed_obj.data.copy()
    mesh.name = f"{OUT_NAME}Mesh"
    obj = bpy.data.objects.new(OUT_NAME, mesh)
    bpy.context.scene.collection.objects.link(obj)

    # Express copied seed coordinates in body-local space, then share the body transform.
    seed_body_local = mesh_object_local_to_body(seed_obj, body)
    for i, vertex in enumerate(mesh.vertices):
        vertex.co = Vector(tuple(seed_body_local[i]))
    obj.matrix_world = body.matrix_world.copy()

    topo = topology_audit(mesh)
    if topo["components"] != 1 or topo["boundary_loops"] != 3 or topo["euler"] != -1:
        raise RuntimeError(f"Seed is not pair-of-pants topology: {topo}")

    verts_seed = np.array([tuple(v.co) for v in mesh.vertices], dtype=np.float64)
    loops = classify_loops(ordered_boundary_loops(mesh), verts_seed)
    target_curves = classify_curves([curve_points_body_local(c, body) for c in curves])

    proxy_obj = bpy.data.objects.get(SMOOTH_PROXY_NAME)
    assert proxy_obj and proxy_obj.type == "MESH", SMOOTH_PROXY_NAME
    body_bvh, body_eval_mesh = build_body_bvh(body)
    proxy_bvh = build_object_bvh_in_body_space(proxy_obj, body)
    clear_m = BOUNDARY_CLEAR_MM / 1000.0
    boundary_ids_parts = []
    boundary_targets_parts = []
    boundary_curve_parts = []

    for key in ("waist", "neg", "pos"):
        ids = np.asarray(loops[key], dtype=np.int32)
        aligned, rms, maximum, direction, shift = best_cyclic_alignment(
            verts_seed[ids], target_curves[key]
        )
        offset = offset_targets_outside(aligned, body_bvh, clear_m)
        boundary_ids_parts.append(ids)
        boundary_targets_parts.append(offset)
        boundary_curve_parts.append(aligned)
        log(
            "BOUNDARY-MAP",
            f"{key}: n={len(ids)} dir={direction:+d} shift={shift} "
            f"seed->curve rms={rms*1000:.2f}mm max={maximum*1000:.2f}mm",
        )

    boundary_ids = np.concatenate(boundary_ids_parts)
    boundary_targets = np.concatenate(boundary_targets_parts)
    fixed = np.zeros(len(mesh.vertices), dtype=bool)
    fixed[boundary_ids] = True
    if fixed.sum() != len(boundary_ids):
        raise RuntimeError("Boundary loops unexpectedly share vertices")
    boundary_target_full = verts_seed.copy()
    boundary_target_full[boundary_ids] = boundary_targets

    mesh_edges = np.asarray([tuple(edge.vertices) for edge in mesh.edges], dtype=np.int32)

    # Geometry regularization comes before triangulation/cotangents.  The old
    # quads/n-gons encode the right topology but some historical positions are
    # folded or almost coincident.  Smooth against the existing low-pass body
    # shell while keeping the old boundary in place.
    reference = envelope_relax(
        verts_seed,
        mesh_edges,
        fixed,
        verts_seed,
        proxy_bvh,
        PRE_RELAX_ITERS,
        RELAX_LAMBDA,
    )

    for i, vertex in enumerate(mesh.vertices):
        vertex.co = Vector(tuple(reference[i]))
    mesh.update()

    # Freeze a quality triangulation only after the historical geometry has
    # been regularized.  Triangulating the folded seed created near-zero
    # diagonals and poisoned the cotangent matrix in the first probe.
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.triangulate(
        bm,
        faces=list(bm.faces),
        quad_method="BEAUTY",
        ngon_method="BEAUTY",
    )
    bm.to_mesh(mesh)
    bm.free()
    mesh.update()

    faces = face_array(mesh)
    edges = np.asarray([tuple(edge.vertices) for edge in mesh.edges], dtype=np.int32)
    triangle_quality(reference, faces, "proxy-envelope reference")
    log("SELF-OVERLAP", f"proxy-envelope reference={count_array_self_overlaps(reference, faces)}")

    # Transfer the exact three-boundary displacement with a positive harmonic
    # field.  Unlike a biharmonic field, this has no overshoot in any coordinate
    # and therefore cannot create the measured accordion fold merely from the
    # boundary correction.
    laplacian, diagonal = graph_laplacian_system(len(reference), edges)
    boundary_displacement = boundary_targets - reference[boundary_ids]
    deformation = solve_dirichlet_field(
        np.zeros_like(reference),
        boundary_ids,
        boundary_displacement,
        laplacian,
        diagonal,
        "HARMONIC",
    )
    fair = reference + deformation
    log("SELF-OVERLAP", f"harmonic displacement={count_array_self_overlaps(fair, faces)}")
    triangle_quality(fair, faces, "harmonic displacement")

    # A second shorter envelope relaxation removes any fold introduced by the
    # large historical boundary correction.  Only the real three curves are
    # fixed; all former front/back "join" vertices remain ordinary interior
    # vertices so curvature crosses them.
    fair = envelope_relax(
        fair,
        edges,
        fixed,
        boundary_target_full,
        proxy_bvh,
        POST_RELAX_ITERS,
        RELAX_LAMBDA,
    )
    triangle_quality(fair, faces, "harmonic displacement + envelope")
    log("SELF-OVERLAP", f"harmonic + envelope={count_array_self_overlaps(fair, faces)}")

    fair, projected, worst = project_outside(fair, body_bvh, clear_m, fixed)
    log(
        "OBSTACLE-INIT",
        f"projected={projected} pre-project worst_signed={worst*1000:.2f}mm",
    )
    log("SELF-OVERLAP", f"after body projection={count_array_self_overlaps(fair, faces)}")

    # Re-evaluate triangle diagonals only after the exact boundary deformation
    # and body projection.  Vertex indices remain unchanged, so pin IDs and
    # exact curve correspondence are preserved.
    for i, vertex in enumerate(mesh.vertices):
        vertex.co = Vector(tuple(fair[i]))
    mesh.update()
    beautify_triangulation(mesh)
    fair = np.array([tuple(vertex.co) for vertex in mesh.vertices], dtype=np.float64)
    faces = face_array(mesh)
    edges = np.asarray([tuple(edge.vertices) for edge in mesh.edges], dtype=np.int32)
    triangle_quality(fair, faces, "post-boundary beauty triangulation")
    log("SELF-OVERLAP", f"post-beautify={count_array_self_overlaps(fair, faces)}")

    geodesic = graph_distance_to_boundary(fair, edges, boundary_ids)
    grow_field = smoothstep01(geodesic / (NO_GROW_COLLAR_MM / 1000.0))
    log(
        "GROW-FIELD",
        f"min={grow_field.min():.3f} p50={np.median(grow_field):.3f} "
        f"p90={np.percentile(grow_field,90):.3f} max={grow_field.max():.3f}",
    )

    for i, p in enumerate(fair):
        if fixed[i] or grow_field[i] <= 0.0:
            continue
        _loc, normal, _signed = nearest_body(body_bvh, p)
        fair[i] += normal * (INITIAL_BULGE_MM / 1000.0) * grow_field[i]
    fair, projected2, worst2 = project_outside(fair, body_bvh, clear_m, fixed)
    log(
        "BULGE-INIT",
        f"secondary projected={projected2} worst_signed={worst2*1000:.2f}mm",
    )
    initialization_overlaps = count_array_self_overlaps(fair, faces)
    log("SELF-OVERLAP", f"cloth initialization={initialization_overlaps}")
    if initialization_overlaps:
        raise RuntimeError(
            f"Refusing to start Cloth from {initialization_overlaps} self-overlap pairs"
        )

    for i, vertex in enumerate(mesh.vertices):
        vertex.co = Vector(tuple(fair[i]))
    mesh.update()
    ensure_outward_normals(mesh, body_bvh)

    # Hide every superseded construction; preserve them in the file for audit.
    for old_name in (
        "FundoshiPlate",
        "FundoshiLoft",
        "FundoshiBandFace",
        "FundoshiShell",
        "ClothShellS",
    ):
        old = bpy.data.objects.get(old_name)
        if old:
            old.hide_viewport = True
            old.hide_render = True
    for curve in curves:
        curve.hide_viewport = True
        curve.hide_render = True
    obj.hide_viewport = False
    obj.hide_render = False

    # Save the deterministic geometry stage before physics so failures are inspectable.
    bpy.ops.wm.save_as_mainfile(filepath=FAIR_BLEND, compress=True)
    log("SAVED-FAIR", FAIR_BLEND)

    fair_before_cloth = np.array([tuple(v.co) for v in mesh.vertices], dtype=np.float64)
    if DO_CLOTH:
        cloth = configure_cloth(obj, body, boundary_ids, grow_field)
        simulate_and_bake(obj, cloth)
        collision_mod = body.modifiers.get("FundoshiCollision")
        if collision_mod:
            body.modifiers.remove(collision_mod)
    else:
        log("CLOTH", "skipped by --no-cloth")

    overlaps_before_cleanup = count_self_overlaps(obj.data)
    if overlaps_before_cleanup:
        flips = untangle_with_edge_rotations(obj.data)
        log(
            "UNTANGLE",
            f"completed flips={flips} overlaps={overlaps_before_cleanup}->0",
        )

    ensure_outward_normals(obj.data, body_bvh)
    topology = topology_audit(obj.data)
    if topology["components"] != 1 or topology["boundary_loops"] != 3 or topology["euler"] != -1:
        raise RuntimeError(f"Output topology failed: {topology}")

    final_verts = np.array([tuple(v.co) for v in obj.data.vertices], dtype=np.float64)
    boundary_actual = final_verts[boundary_ids]
    boundary_pin_error = np.linalg.norm(boundary_actual - boundary_targets, axis=1)
    log(
        "BOUNDARY-ERROR",
        f"pin rms={np.sqrt(np.mean(boundary_pin_error**2))*1000:.3f}mm "
        f"max={boundary_pin_error.max()*1000:.3f}mm",
    )

    displacement = final_verts - fair_before_cloth
    outward_delta = []
    for i, p in enumerate(fair_before_cloth):
        if fixed[i]:
            continue
        _loc, normal, _signed = nearest_body(body_bvh, p)
        outward_delta.append(float(np.dot(displacement[i], normal)))
    outward_delta = np.asarray(outward_delta)
    log(
        "CLOTH-BULGE",
        f"normal displacement mm p10={np.percentile(outward_delta,10)*1000:.2f} "
        f"p50={np.median(outward_delta)*1000:.2f} "
        f"p90={np.percentile(outward_delta,90)*1000:.2f} "
        f"max={outward_delta.max()*1000:.2f}",
    )

    clearance = signed_clearance_samples(obj.data, body_bvh)
    log(
        "CLEARANCE",
        f"samples={len(clearance)} min={clearance.min()*1000:.2f}mm "
        f"p01={np.percentile(clearance,1)*1000:.2f}mm "
        f"p10={np.percentile(clearance,10)*1000:.2f}mm "
        f"median={np.median(clearance)*1000:.2f}mm "
        f"negative={(clearance < -1e-5).sum()}",
    )
    overlaps = count_self_overlaps(obj.data)
    log("SELF-OVERLAP", f"non-adjacent triangle overlap pairs={overlaps}")
    if overlaps:
        raise RuntimeError(f"Output still contains {overlaps} self-overlap pairs")

    for polygon in obj.data.polygons:
        polygon.use_smooth = True
    add_render_modifiers(obj)

    # The evaluated body mesh was only needed for local-space BVH queries.
    bpy.data.meshes.remove(body_eval_mesh)

    bpy.ops.wm.save_as_mainfile(filepath=OUT_BLEND, compress=True)
    log("SAVED", OUT_BLEND)
    if DO_RENDER:
        render_shots(body, obj)
    else:
        log("RENDER", "skipped by --no-render")
    bpy.ops.wm.save_as_mainfile(filepath=OUT_BLEND, compress=True)
    log("DONE", OUT_BLEND)


if __name__ == "__main__":
    main()
