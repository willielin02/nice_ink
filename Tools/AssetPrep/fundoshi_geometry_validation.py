"""Deterministic validation primitives for the V68 pair-of-pants rebuild.

All numeric geometry routines operate in the caller's units.  The intended
caller converts evaluated Blender world coordinates to millimetres first.

Important: Blender 5.1 BVHTree.overlap() omits coplanar triangle overlaps, so
the routines below use their own AABB tree and an explicit coplanar narrow
phase.  They do not mutate Blender data.
"""

from __future__ import annotations

from collections import Counter, defaultdict
from dataclasses import dataclass
from heapq import heappop, heappush
from itertools import combinations, permutations
import math

import numpy as np


# ---------------------------------------------------------------------------
# Mesh topology


def _edge(a, b):
    a, b = int(a), int(b)
    return (a, b) if a < b else (b, a)


def _count_components(nodes, adjacency):
    remaining = set(nodes)
    count = 0
    while remaining:
        count += 1
        stack = [remaining.pop()]
        while stack:
            v = stack.pop()
            for w in adjacency.get(v, ()):
                if w in remaining:
                    remaining.remove(w)
                    stack.append(w)
    return count


def _ordered_boundary_loops(boundary_edges):
    adjacency = defaultdict(list)
    for a, b in boundary_edges:
        adjacency[a].append(b)
        adjacency[b].append(a)
    bad_degree = {int(v): len(n) for v, n in adjacency.items() if len(n) != 2}
    if bad_degree:
        return [], bad_degree

    unused = {tuple(e) for e in boundary_edges}
    loops = []
    while unused:
        start_edge = min(unused)
        start, cur = start_edge
        prev = start
        loop = [start]
        unused.remove(start_edge)
        guard = 0
        while cur != start:
            loop.append(cur)
            nxts = adjacency[cur]
            nxt = nxts[0] if nxts[0] != prev else nxts[1]
            e = _edge(cur, nxt)
            if e not in unused:
                return [], {int(cur): -1}  # repeated/broken walk
            unused.remove(e)
            prev, cur = cur, nxt
            guard += 1
            if guard > len(boundary_edges):
                return [], {int(cur): -2}
        loops.append(loop)
    loops.sort(key=lambda x: (len(x), x[0]), reverse=True)
    return loops, {}


def topology_report(vertex_count, faces, explicit_edges=None):
    """Validate a polygon surface before triangulation.

    `faces` is an iterable of ordered vertex-index cycles.  `explicit_edges`
    should be supplied from Blender mesh.edges so loose/wire edges are seen.
    """

    faces = [tuple(map(int, f)) for f in faces]
    edge_faces = defaultdict(list)
    edge_directions = defaultdict(list)
    vertex_faces = defaultdict(list)
    face_adjacency = defaultdict(set)
    used_face_vertices = set()

    for fi, face in enumerate(faces):
        used_face_vertices.update(face)
        for v in face:
            vertex_faces[v].append(fi)
        if len(face) < 2:
            continue
        for a, b in zip(face, face[1:] + face[:1]):
            key = _edge(a, b)
            edge_faces[key].append(fi)
            edge_directions[key].append((a, b))

    surface_edges = set(edge_faces)
    all_edges = set(surface_edges if explicit_edges is None else (_edge(*e) for e in explicit_edges))
    wire_edges = sorted(all_edges - surface_edges)
    boundary_edges = sorted(e for e, fs in edge_faces.items() if len(fs) == 1)
    internal_nonmanifold_edges = sorted(e for e, fs in edge_faces.items() if len(fs) > 2)
    orientation_conflict_edges = []
    for e, dirs in edge_directions.items():
        if len(dirs) == 2 and dirs[0] == dirs[1]:
            orientation_conflict_edges.append(e)
        fs = edge_faces[e]
        for a, b in combinations(fs, 2):
            face_adjacency[a].add(b)
            face_adjacency[b].add(a)

    vertex_adjacency = defaultdict(set)
    for a, b in all_edges:
        vertex_adjacency[a].add(b)
        vertex_adjacency[b].add(a)
    used_edge_vertices = set(vertex_adjacency)
    isolated = sorted(set(range(int(vertex_count))) - used_edge_vertices)
    surface_components = _count_components(used_face_vertices, vertex_adjacency) if used_face_vertices else 0
    face_edge_components = _count_components(range(len(faces)), face_adjacency) if faces else 0

    loops, bad_boundary_degrees = _ordered_boundary_loops(boundary_edges)

    # A manifold vertex has one connected fan of incident faces.  At a valid
    # boundary vertex exactly two incident edges are boundary edges.
    boundary_degree = Counter(v for e in boundary_edges for v in e)
    pinched_vertices = []
    for v, incident in vertex_faces.items():
        local = defaultdict(set)
        for e, fs in edge_faces.items():
            if v not in e:
                continue
            for a, b in combinations(fs, 2):
                local[a].add(b)
                local[b].add(a)
        fan_count = _count_components(incident, local)
        if fan_count != 1 or boundary_degree.get(v, 0) not in (0, 2):
            pinched_vertices.append(int(v))

    chi = len(used_face_vertices) - len(surface_edges) + len(faces)
    topology_valid_for_genus = (
        surface_components == 1
        and face_edge_components == 1
        and not wire_edges
        and not isolated
        and not internal_nonmanifold_edges
        and not bad_boundary_degrees
        and not pinched_vertices
        and not orientation_conflict_edges
    )
    genus = None
    if topology_valid_for_genus:
        g = (2 - len(loops) - chi) / 2.0
        genus = int(round(g)) if abs(g - round(g)) < 1e-9 else g

    return {
        "vertices": int(vertex_count),
        "surface_vertices": len(used_face_vertices),
        "edges": len(all_edges),
        "surface_edges": len(surface_edges),
        "faces": len(faces),
        "components": surface_components,
        "face_edge_components": face_edge_components,
        "boundary_loops": len(loops) if not bad_boundary_degrees else None,
        "boundary_loop_vertex_indices": loops,
        "boundary_bad_degrees": bad_boundary_degrees,
        "euler_characteristic": chi,
        "genus": genus,
        "nonmanifold_internal_edges": len(internal_nonmanifold_edges),
        "wire_edges": len(wire_edges),
        "isolated_vertices": len(isolated),
        "pinched_vertices": len(pinched_vertices),
        "orientation_conflict_edges": len(orientation_conflict_edges),
        "samples": {
            "nonmanifold_edges": internal_nonmanifold_edges[:20],
            "wire_edges": wire_edges[:20],
            "isolated_vertices": isolated[:20],
            "pinched_vertices": pinched_vertices[:20],
            "orientation_conflict_edges": orientation_conflict_edges[:20],
        },
    }


# ---------------------------------------------------------------------------
# AABB tree (coplanar-safe broad phase)


@dataclass
class _AABBNode:
    lo: np.ndarray
    hi: np.ndarray
    left: "_AABBNode | None" = None
    right: "_AABBNode | None" = None
    indices: np.ndarray | None = None

    @property
    def leaf(self):
        return self.indices is not None


class TriangleAABBTree:
    def __init__(self, vertices, triangles, leaf_size=8):
        self.vertices = np.asarray(vertices, dtype=np.float64)
        self.triangles = np.asarray(triangles, dtype=np.int64)
        self.coords = self.vertices[self.triangles]
        self.lo = self.coords.min(axis=1)
        self.hi = self.coords.max(axis=1)
        self.centres = (self.lo + self.hi) * 0.5
        self.leaf_size = int(leaf_size)
        self.root = self._build(np.arange(len(self.triangles), dtype=np.int64))

    def _build(self, indices):
        lo = self.lo[indices].min(axis=0)
        hi = self.hi[indices].max(axis=0)
        if len(indices) <= self.leaf_size:
            return _AABBNode(lo, hi, indices=indices)
        spread = np.ptp(self.centres[indices], axis=0)
        axis = int(np.argmax(spread))
        order = indices[np.argsort(self.centres[indices, axis], kind="mergesort")]
        mid = len(order) // 2
        return _AABBNode(lo, hi, self._build(order[:mid]), self._build(order[mid:]))


def _boxes_overlap(a, b, eps):
    return bool(np.all(a.lo <= b.hi + eps) and np.all(b.lo <= a.hi + eps))


def _bbox_distance2_to_box(lo1, hi1, lo2, hi2):
    gap = np.maximum(np.maximum(lo1 - hi2, lo2 - hi1), 0.0)
    return float(gap @ gap)


def self_aabb_candidates(tree, eps=0.0):
    """Yield every i<j pair whose triangle AABBs touch/overlap."""

    stack = [(tree.root, tree.root)]
    while stack:
        a, b = stack.pop()
        if not _boxes_overlap(a, b, eps):
            continue
        if a is b:
            if a.leaf:
                for i, j in combinations(map(int, a.indices), 2):
                    i0, j0 = (i, j) if i < j else (j, i)
                    if np.all(tree.lo[i0] <= tree.hi[j0] + eps) and np.all(tree.lo[j0] <= tree.hi[i0] + eps):
                        yield i0, j0
            else:
                stack.extend(((a.left, a.left), (a.right, a.right), (a.left, a.right)))
        elif a.leaf and b.leaf:
            for i in map(int, a.indices):
                for j in map(int, b.indices):
                    i0, j0 = (i, j) if i < j else (j, i)
                    if i0 != j0 and np.all(tree.lo[i0] <= tree.hi[j0] + eps) and np.all(tree.lo[j0] <= tree.hi[i0] + eps):
                        yield i0, j0
        elif a.leaf:
            stack.extend(((a, b.left), (a, b.right)))
        elif b.leaf:
            stack.extend(((a.left, b), (a.right, b)))
        else:
            stack.extend(((a.left, b.left), (a.left, b.right), (a.right, b.left), (a.right, b.right)))


def cross_aabb_candidates(tree_a, tree_b, eps=0.0):
    stack = [(tree_a.root, tree_b.root)]
    while stack:
        a, b = stack.pop()
        if not _boxes_overlap(a, b, eps):
            continue
        if a.leaf and b.leaf:
            for i in map(int, a.indices):
                for j in map(int, b.indices):
                    if np.all(tree_a.lo[i] <= tree_b.hi[j] + eps) and np.all(tree_b.lo[j] <= tree_a.hi[i] + eps):
                        yield i, j
        elif a.leaf:
            stack.extend(((a, b.left), (a, b.right)))
        elif b.leaf:
            stack.extend(((a.left, b), (a.right, b)))
        else:
            # Split the larger-volume node first to limit the stack.
            va = float(np.prod(a.hi - a.lo))
            vb = float(np.prod(b.hi - b.lo))
            if va >= vb:
                stack.extend(((a.left, b), (a.right, b)))
            else:
                stack.extend(((a, b.left), (a, b.right)))


# ---------------------------------------------------------------------------
# Exact triangle intersection, including coplanar triangles


def _tri_plane_data(tri, eps):
    n = np.cross(tri[1] - tri[0], tri[2] - tri[0])
    length = float(np.linalg.norm(n))
    if length <= eps:
        return None
    return n / length


def _project_2d(tri, normal):
    drop = int(np.argmax(np.abs(normal)))
    return np.delete(tri, drop, axis=1)


def _coplanar_sat(a2, b2, eps):
    for tri in (a2, b2):
        for p, q in zip(tri, np.roll(tri, -1, axis=0)):
            e = q - p
            axis = np.array([-e[1], e[0]], dtype=np.float64)
            norm = float(np.linalg.norm(axis))
            if norm <= eps:
                continue
            axis /= norm
            pa, pb = a2 @ axis, b2 @ axis
            if pa.max() < pb.min() - eps or pb.max() < pa.min() - eps:
                return False
    return True


def _plane_cut_interval(tri, distances, direction, eps):
    points = []
    for i in range(3):
        if abs(float(distances[i])) <= eps:
            points.append(tri[i])
        j = (i + 1) % 3
        di, dj = float(distances[i]), float(distances[j])
        if (di < -eps and dj > eps) or (di > eps and dj < -eps):
            t = di / (di - dj)
            points.append(tri[i] + t * (tri[j] - tri[i]))
    unique = []
    for p in points:
        if not any(np.linalg.norm(p - q) <= eps for q in unique):
            unique.append(p)
    if not unique:
        return None
    values = [float(p @ direction) for p in unique]
    return min(values), max(values)


def triangle_intersection_detail(a, b, eps=1e-8):
    """Return hit/coplanar and the non-coplanar intersection-line interval."""

    a = np.asarray(a, dtype=np.float64)
    b = np.asarray(b, dtype=np.float64)
    na, nb = _tri_plane_data(a, eps), _tri_plane_data(b, eps)
    if na is None or nb is None:
        return {"hit": False, "degenerate": True, "coplanar": False}
    db = (b - a[0]) @ na
    da = (a - b[0]) @ nb
    if np.all(db > eps) or np.all(db < -eps) or np.all(da > eps) or np.all(da < -eps):
        return {"hit": False, "degenerate": False, "coplanar": False}

    line = np.cross(na, nb)
    line_len = float(np.linalg.norm(line))
    if line_len <= 1e-10:
        coplanar = max(float(np.max(np.abs(db))), float(np.max(np.abs(da)))) <= eps
        if not coplanar:
            return {"hit": False, "degenerate": False, "coplanar": False}
        a2, b2 = _project_2d(a, na), _project_2d(b, na)
        return {
            "hit": _coplanar_sat(a2, b2, eps),
            "degenerate": False,
            "coplanar": True,
            "a2": a2,
            "b2": b2,
        }

    line /= line_len
    ia = _plane_cut_interval(a, da, line, eps)
    ib = _plane_cut_interval(b, db, line, eps)
    if ia is None or ib is None:
        return {"hit": False, "degenerate": False, "coplanar": False}
    lo, hi = max(ia[0], ib[0]), min(ia[1], ib[1])
    return {
        "hit": lo <= hi + eps,
        "degenerate": False,
        "coplanar": False,
        "interval": (lo, hi),
        "line_direction": line,
    }


def triangles_intersect(a, b, eps=1e-8):
    return bool(triangle_intersection_detail(a, b, eps)["hit"])


def _orient2(a, b, c):
    return float((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]))


def _point_in_tri2_strict(p, tri, eps):
    s = [_orient2(tri[i], tri[(i + 1) % 3], p) for i in range(3)]
    return (all(v > eps for v in s) or all(v < -eps for v in s))


def _segment_intersection_points_2d(a, b, c, d, eps):
    """Return representative points of a 2-D segment intersection."""

    r, s = b - a, d - c
    den = _orient2(np.zeros(2), r, s)
    ca = c - a
    if abs(den) > eps:
        t = _orient2(np.zeros(2), ca, s) / den
        u = _orient2(np.zeros(2), ca, r) / den
        if -eps <= t <= 1 + eps and -eps <= u <= 1 + eps:
            return [a + np.clip(t, 0.0, 1.0) * r]
        return []
    if abs(_orient2(a, b, c)) > eps:
        return []
    rr = float(r @ r)
    if rr <= eps * eps:
        return [a] if np.linalg.norm(a - c) <= eps or np.linalg.norm(a - d) <= eps else []
    t0, t1 = float((c - a) @ r / rr), float((d - a) @ r / rr)
    lo, hi = max(0.0, min(t0, t1)), min(1.0, max(t0, t1))
    if lo > hi + eps:
        return []
    return [a + lo * r, a + hi * r]


def _coplanar_extra_beyond_shared_vertex(a2, b2, shared_point, eps):
    for i in range(3):
        for j in range(3):
            pts = _segment_intersection_points_2d(
                a2[i], a2[(i + 1) % 3], b2[j], b2[(j + 1) % 3], eps
            )
            if any(np.linalg.norm(p - shared_point) > eps * 4 for p in pts):
                return True
    for p in a2:
        if np.linalg.norm(p - shared_point) > eps * 4 and _point_in_tri2_strict(p, b2, eps):
            return True
    for p in b2:
        if np.linalg.norm(p - shared_point) > eps * 4 and _point_in_tri2_strict(p, a2, eps):
            return True
    # Detect overlapping angular wedges when no full vertex lies inside.
    min_edge = min(
        np.linalg.norm(a2[(i + 1) % 3] - a2[i]) for i in range(3)
    )
    delta = max(eps * 16, min_edge * 1e-6)
    for tri, other in ((a2, b2), (b2, a2)):
        for p in tri:
            direction = p - shared_point
            length = float(np.linalg.norm(direction))
            if length > eps * 4:
                probe = shared_point + direction / length * min(delta, length * 0.25)
                if _point_in_tri2_strict(probe, other, eps):
                    return True
    return False


def illegal_self_intersection(vertices, tri_a, tri_b, eps=1e-8):
    """Ignore only the topologically expected shared vertex/edge contact."""

    tri_a = tuple(map(int, tri_a))
    tri_b = tuple(map(int, tri_b))
    shared = sorted(set(tri_a).intersection(tri_b))
    a = np.asarray(vertices, dtype=np.float64)[list(tri_a)]
    b = np.asarray(vertices, dtype=np.float64)[list(tri_b)]
    detail = triangle_intersection_detail(a, b, eps)
    if not detail["hit"]:
        return False
    if len(shared) == 0:
        return True
    if len(shared) >= 3:
        return True
    if len(shared) == 2:
        # Non-coplanar adjacent faces meet only on their shared edge.  For
        # coplanar faces, their opposite vertices must lie on opposite sides.
        if not detail["coplanar"]:
            return False
        a2, b2 = detail["a2"], detail["b2"]
        pa = {v: a2[i] for i, v in enumerate(tri_a)}
        pb = {v: b2[i] for i, v in enumerate(tri_b)}
        p, q = pa[shared[0]], pa[shared[1]]
        ua = next(pa[v] for v in tri_a if v not in shared)
        ub = next(pb[v] for v in tri_b if v not in shared)
        sa, sb = _orient2(p, q, ua), _orient2(p, q, ub)
        return sa * sb >= -(eps * eps)

    # Exactly one shared vertex.
    shared_v = shared[0]
    if not detail["coplanar"]:
        lo, hi = detail["interval"]
        p_t = float(np.asarray(vertices)[shared_v] @ detail["line_direction"])
        return lo < p_t - eps or hi > p_t + eps
    a2, b2 = detail["a2"], detail["b2"]
    p2 = a2[tri_a.index(shared_v)]
    return _coplanar_extra_beyond_shared_vertex(a2, b2, p2, eps)


def self_intersection_report(vertices, triangles, eps=1e-8, sample_limit=50):
    tree = TriangleAABBTree(vertices, triangles)
    pairs = []
    count = 0
    candidate_count = 0
    seen = set()
    for i, j in self_aabb_candidates(tree, eps):
        if (i, j) in seen:
            continue
        seen.add((i, j))
        candidate_count += 1
        if illegal_self_intersection(vertices, triangles[i], triangles[j], eps):
            count += 1
            if len(pairs) < sample_limit:
                pairs.append((int(i), int(j)))
    return {
        "self_intersections": count,
        "broad_phase_candidates": candidate_count,
        "sample_triangle_pairs": pairs,
    }


def cross_intersection_report(vertices_a, triangles_a, vertices_b, triangles_b, eps=1e-8, sample_limit=50):
    ta = TriangleAABBTree(vertices_a, triangles_a)
    tb = TriangleAABBTree(vertices_b, triangles_b)
    count = 0
    pairs = []
    all_pairs = []
    hit_a = set()
    seen = set()
    candidate_count = 0
    for i, j in cross_aabb_candidates(ta, tb, eps):
        if (i, j) in seen:
            continue
        seen.add((i, j))
        candidate_count += 1
        if triangles_intersect(ta.coords[i], tb.coords[j], eps):
            count += 1
            hit_a.add(i)
            all_pairs.append((int(i), int(j)))
            if len(pairs) < sample_limit:
                pairs.append((int(i), int(j)))
    return {
        "intersection_pairs": count,
        "intersecting_a_triangles": len(hit_a),
        "intersecting_a_triangle_indices": sorted(map(int, hit_a)),
        "intersection_pair_indices": all_pairs,
        "broad_phase_candidates": candidate_count,
        "sample_triangle_pairs": pairs,
    }


# ---------------------------------------------------------------------------
# Exact distances and signed clearance


def closest_point_on_triangle(p, a, b, c):
    # Ericson, Real-Time Collision Detection, section 5.1.5.
    ab, ac, ap = b - a, c - a, p - a
    d1, d2 = float(ab @ ap), float(ac @ ap)
    if d1 <= 0.0 and d2 <= 0.0:
        return a
    bp = p - b
    d3, d4 = float(ab @ bp), float(ac @ bp)
    if d3 >= 0.0 and d4 <= d3:
        return b
    vc = d1 * d4 - d3 * d2
    if vc <= 0.0 and d1 >= 0.0 and d3 <= 0.0:
        v = d1 / (d1 - d3)
        return a + v * ab
    cp = p - c
    d5, d6 = float(ab @ cp), float(ac @ cp)
    if d6 >= 0.0 and d5 <= d6:
        return c
    vb = d5 * d2 - d1 * d6
    if vb <= 0.0 and d2 >= 0.0 and d6 <= 0.0:
        w = d2 / (d2 - d6)
        return a + w * ac
    va = d3 * d6 - d5 * d4
    if va <= 0.0 and (d4 - d3) >= 0.0 and (d5 - d6) >= 0.0:
        w = (d4 - d3) / ((d4 - d3) + (d5 - d6))
        return b + w * (c - b)
    denom = 1.0 / (va + vb + vc)
    v, w = vb * denom, vc * denom
    return a + ab * v + ac * w


def segment_segment_distance2(p1, q1, p2, q2, eps=1e-20):
    d1, d2, r = q1 - p1, q2 - p2, p1 - p2
    a, e, f = float(d1 @ d1), float(d2 @ d2), float(d2 @ r)
    if a <= eps and e <= eps:
        return float(r @ r)
    if a <= eps:
        s, t = 0.0, np.clip(f / e, 0.0, 1.0)
    else:
        c = float(d1 @ r)
        if e <= eps:
            t, s = 0.0, np.clip(-c / a, 0.0, 1.0)
        else:
            b = float(d1 @ d2)
            denom = a * e - b * b
            s = np.clip((b * f - c * e) / denom, 0.0, 1.0) if abs(denom) > eps else 0.0
            t = (b * s + f) / e
            if t < 0.0:
                t, s = 0.0, np.clip(-c / a, 0.0, 1.0)
            elif t > 1.0:
                t, s = 1.0, np.clip((b - c) / a, 0.0, 1.0)
    c1, c2 = p1 + d1 * s, p2 + d2 * t
    d = c1 - c2
    return float(d @ d)


def triangle_triangle_distance2(a, b, eps=1e-8):
    if triangles_intersect(a, b, eps):
        return 0.0
    best = math.inf
    for p in a:
        q = closest_point_on_triangle(p, b[0], b[1], b[2])
        best = min(best, float((p - q) @ (p - q)))
    for p in b:
        q = closest_point_on_triangle(p, a[0], a[1], a[2])
        best = min(best, float((p - q) @ (p - q)))
    for i in range(3):
        for j in range(3):
            best = min(best, segment_segment_distance2(a[i], a[(i + 1) % 3], b[j], b[(j + 1) % 3]))
    return best


def nearest_triangle_to_point(tree, point):
    p = np.asarray(point, dtype=np.float64)
    best2, best_i, best_q = math.inf, None, None
    heap = []
    serial = 0

    def push(node):
        nonlocal serial
        gap = np.maximum(np.maximum(node.lo - p, p - node.hi), 0.0)
        heappush(heap, (float(gap @ gap), serial, node))
        serial += 1

    push(tree.root)
    while heap:
        lower, _, node = heappop(heap)
        if lower >= best2:
            continue
        if node.leaf:
            for i in map(int, node.indices):
                tri = tree.coords[i]
                q = closest_point_on_triangle(p, tri[0], tri[1], tri[2])
                d2 = float((p - q) @ (p - q))
                if d2 < best2:
                    best2, best_i, best_q = d2, i, q
        else:
            push(node.left)
            push(node.right)
    return best_i, best_q, math.sqrt(best2)


def nearest_triangle_to_triangle(tree, tri, eps=1e-8):
    tri = np.asarray(tri, dtype=np.float64)
    qlo, qhi = tri.min(axis=0), tri.max(axis=0)
    best2, best_i = math.inf, None
    heap = []
    serial = 0

    def push(node):
        nonlocal serial
        d2 = _bbox_distance2_to_box(qlo, qhi, node.lo, node.hi)
        heappush(heap, (d2, serial, node))
        serial += 1

    push(tree.root)
    while heap:
        lower, _, node = heappop(heap)
        if lower >= best2:
            continue
        if node.leaf:
            for i in map(int, node.indices):
                d2 = triangle_triangle_distance2(tri, tree.coords[i], eps)
                if d2 < best2:
                    best2, best_i = d2, i
        else:
            push(node.left)
            push(node.right)
    return best_i, math.sqrt(best2)


def signed_clearances(points, body_tree, eps=1e-8):
    """Nearest oriented-body distance; positive is outside.

    This is valid when the body is closed, consistently wound, and outward
    oriented.  Those preconditions are true for all six SumoRetopo shells in
    v67 and should still be asserted by the caller.
    """

    signed, nearest_indices, nearest_points = [], [], []
    for p in np.asarray(points, dtype=np.float64):
        i, q, distance = nearest_triangle_to_point(body_tree, p)
        tri = body_tree.coords[i]
        normal = np.cross(tri[1] - tri[0], tri[2] - tri[0])
        normal /= np.linalg.norm(normal)
        dot = float((p - q) @ normal)
        sign = 0.0 if distance <= eps else (1.0 if dot >= 0.0 else -1.0)
        signed.append(sign * distance)
        nearest_indices.append(int(i))
        nearest_points.append(q)
    return np.asarray(signed), np.asarray(nearest_indices), np.asarray(nearest_points)


def exact_surface_min_distance(surface_vertices, surface_triangles, body_tree, eps=1e-8):
    best = math.inf
    pair = None
    per_triangle = []
    coords = np.asarray(surface_vertices)[np.asarray(surface_triangles, dtype=np.int64)]
    for i, tri in enumerate(coords):
        j, d = nearest_triangle_to_triangle(body_tree, tri, eps)
        per_triangle.append(d)
        if d < best:
            best, pair = d, (int(i), int(j))
    return {"minimum": best, "triangle_pair": pair, "per_surface_triangle": np.asarray(per_triangle)}


# ---------------------------------------------------------------------------
# Boundary matching with a certified sampling upper bound


def closed_polyline_length(loop):
    loop = np.asarray(loop, dtype=np.float64)
    return float(np.linalg.norm(np.roll(loop, -1, axis=0) - loop, axis=1).sum())


def sample_closed_polyline(loop, max_spacing):
    loop = np.asarray(loop, dtype=np.float64)
    samples = []
    worst_gap = 0.0
    for a, b in zip(loop, np.roll(loop, -1, axis=0)):
        length = float(np.linalg.norm(b - a))
        if length == 0.0:
            continue
        n = max(1, int(math.ceil(length / max_spacing)))
        gap = length / n
        worst_gap = max(worst_gap, gap)
        t = np.arange(n, dtype=np.float64)[:, None] / n
        samples.append(a[None, :] + t * (b - a)[None, :])
    return np.concatenate(samples, axis=0), worst_gap


def min_distances_to_polyline(points, loop, batch=512):
    points = np.asarray(points, dtype=np.float64)
    a = np.asarray(loop, dtype=np.float64)
    b = np.roll(a, -1, axis=0)
    ab = b - a
    denom = np.sum(ab * ab, axis=1)
    valid = denom > 0.0
    a, ab, denom = a[valid], ab[valid], denom[valid]
    out = np.empty(len(points), dtype=np.float64)
    for start in range(0, len(points), batch):
        p = points[start : start + batch]
        pa = p[:, None, :] - a[None, :, :]
        t = np.sum(pa * ab[None, :, :], axis=2) / denom[None, :]
        t = np.clip(t, 0.0, 1.0)
        delta = pa - t[:, :, None] * ab[None, :, :]
        out[start : start + len(p)] = np.sqrt(np.min(np.sum(delta * delta, axis=2), axis=1))
    return out


def _min_distances_to_polyline_grid(points, loop, initial_radius):
    """Exact point/segment distances with a tolerance-grown spatial hash.

    The grid indexes segment AABBs expanded by `radius`.  If a point has no
    segment within the current radius, the radius doubles and the query is
    repeated.  This is fast for the real 3k--5k-point EdgeTexel loops while
    remaining exact for a failed/misaligned candidate.
    """

    points = np.asarray(points, dtype=np.float64)
    loop = np.asarray(loop, dtype=np.float64)
    a, b = loop, np.roll(loop, -1, axis=0)
    ab = b - a
    lengths = np.linalg.norm(ab, axis=1)
    valid = lengths > 0.0
    a, b, ab, lengths = a[valid], b[valid], ab[valid], lengths[valid]
    if len(a) == 0:
        return np.full(len(points), math.inf)

    extent = np.ptp(np.vstack((points, loop)), axis=0)
    max_radius = max(float(np.linalg.norm(extent)) * 2.0, initial_radius)
    radius = max(float(initial_radius), 1e-9)
    while True:
        cell = max(radius * 4.0, float(np.quantile(lengths, 0.75)), 1e-9)
        grid = defaultdict(list)
        lo = np.floor((np.minimum(a, b) - radius) / cell).astype(np.int64)
        hi = np.floor((np.maximum(a, b) + radius) / cell).astype(np.int64)
        for si in range(len(a)):
            for ix in range(lo[si, 0], hi[si, 0] + 1):
                for iy in range(lo[si, 1], hi[si, 1] + 1):
                    for iz in range(lo[si, 2], hi[si, 2] + 1):
                        grid[(int(ix), int(iy), int(iz))].append(si)

        out = np.full(len(points), math.inf)
        keys = np.floor(points / cell).astype(np.int64)
        unresolved = False
        for pi, (p, key) in enumerate(zip(points, keys)):
            candidates = grid.get(tuple(map(int, key)))
            if not candidates:
                unresolved = True
                continue
            ids = np.asarray(candidates, dtype=np.int64)
            seg_a, seg_ab = a[ids], ab[ids]
            denom = np.sum(seg_ab * seg_ab, axis=1)
            t = np.clip(np.sum((p - seg_a) * seg_ab, axis=1) / denom, 0.0, 1.0)
            delta = p - (seg_a + t[:, None] * seg_ab)
            out[pi] = math.sqrt(float(np.min(np.sum(delta * delta, axis=1))))
            if out[pi] > radius:
                unresolved = True
        if not unresolved or radius >= max_radius:
            return out
        radius *= 2.0


def _cyclic_correspondence_bound(loop_a, loop_b, candidate_starts=8):
    """Certified upper bound when loops have the same vertex count."""

    a, b = np.asarray(loop_a, dtype=np.float64), np.asarray(loop_b, dtype=np.float64)
    if len(a) != len(b) or len(a) == 0:
        return None
    nearest = np.argsort(np.sum((b - a[0]) ** 2, axis=1))[: min(candidate_starts, len(b))]
    best = math.inf
    best_meta = None
    for reverse in (False, True):
        base = b[::-1] if reverse else b
        for original_start in nearest:
            start = len(b) - 1 - int(original_start) if reverse else int(original_start)
            aligned = np.roll(base, -start, axis=0)
            maximum = float(np.linalg.norm(a - aligned, axis=1).max(initial=0.0))
            if maximum < best:
                best = maximum
                best_meta = {"reverse": reverse, "start": int(original_start)}
    return best, best_meta


def certified_polyline_hausdorff(loop_a, loop_b, spacing=0.05, search_radius=0.25):
    """Symmetric Hausdorff lower/upper bounds in the loops' units.

    Distance-to-a-set is 1-Lipschitz.  Sampling every segment with maximum
    gap h therefore underestimates the directed Hausdorff distance by at most
    h/2.  This gives a real acceptance bound rather than an unqualified dense
    sample estimate.
    """

    correspondence = _cyclic_correspondence_bound(loop_a, loop_b)
    if correspondence is not None:
        upper, meta = correspondence
        # Endpoint-to-opposite-polyline distances provide a useful lower
        # bound; the paired-segment interpolation proves the upper bound.
        da = _min_distances_to_polyline_grid(np.asarray(loop_a), loop_b, search_radius)
        db = _min_distances_to_polyline_grid(np.asarray(loop_b), loop_a, search_radius)
        lower = max(float(da.max(initial=0.0)), float(db.max(initial=0.0)))
        return {
            "sampled_lower": lower,
            "certified_upper": upper,
            "a_to_b_sampled": float(da.max(initial=0.0)),
            "b_to_a_sampled": float(db.max(initial=0.0)),
            "sample_spacing": None,
            "samples_a": len(loop_a),
            "samples_b": len(loop_b),
            "method": "cyclic_vertex_correspondence",
            "correspondence": meta,
        }

    sa, gap_a = sample_closed_polyline(loop_a, spacing)
    sb, gap_b = sample_closed_polyline(loop_b, spacing)
    da = _min_distances_to_polyline_grid(sa, loop_b, search_radius)
    db = _min_distances_to_polyline_grid(sb, loop_a, search_radius)
    directed_a = float(da.max(initial=0.0))
    directed_b = float(db.max(initial=0.0))
    lower = max(directed_a, directed_b)
    upper = max(directed_a + gap_a * 0.5, directed_b + gap_b * 0.5)
    return {
        "sampled_lower": lower,
        "certified_upper": upper,
        "a_to_b_sampled": directed_a,
        "b_to_a_sampled": directed_b,
        "sample_spacing": spacing,
        "samples_a": len(sa),
        "samples_b": len(sb),
        "method": "lipschitz_dense_sampling",
    }


def match_boundary_loops(output_loops, input_loops, tolerance=0.25, coarse_spacing=1.0):
    if len(output_loops) != len(input_loops):
        return {"bijection": None, "reason": "loop_count_mismatch"}
    n = len(output_loops)
    costs = np.empty((n, n), dtype=np.float64)
    for i, out in enumerate(output_loops):
        for j, src in enumerate(input_loops):
            # Assignment needs only a discriminating cost, not a proof.  Use
            # ~128 samples per direction and brute vectorized point/segment
            # distances; this avoids tolerance-doubling spatial hashes for
            # the six deliberately wrong loop pairs.
            spacing_out = max(closed_polyline_length(out) / 128.0, coarse_spacing)
            spacing_src = max(closed_polyline_length(src) / 128.0, coarse_spacing)
            so, _ = sample_closed_polyline(out, spacing_out)
            ss, _ = sample_closed_polyline(src, spacing_src)
            costs[i, j] = max(
                float(min_distances_to_polyline(so, src).max(initial=0.0)),
                float(min_distances_to_polyline(ss, out).max(initial=0.0)),
            )
    perm = min(permutations(range(n)), key=lambda p: (max(costs[i, p[i]] for i in range(n)), sum(costs[i, p[i]] for i in range(n))))

    matches = []
    all_pass = True
    for i, j in enumerate(perm):
        spacing = 0.05
        while True:
            h = certified_polyline_hausdorff(output_loops[i], input_loops[j], spacing, search_radius=tolerance)
            if h["sampled_lower"] > tolerance or h["certified_upper"] <= tolerance or spacing <= 0.005:
                break
            spacing *= 0.5
        passed = h["certified_upper"] <= tolerance
        all_pass &= passed
        matches.append({
            "output_loop": i,
            "input_loop": int(j),
            "passed": bool(passed),
            "hausdorff": h,
            "output_length": closed_polyline_length(output_loops[i]),
            "input_length": closed_polyline_length(input_loops[j]),
        })
    return {
        "bijection": [int(x) for x in perm],
        "coarse_cost_matrix": costs.tolist(),
        "matches": matches,
        "maximum_sampled_lower": max(m["hausdorff"]["sampled_lower"] for m in matches),
        "maximum_certified_upper": max(m["hausdorff"]["certified_upper"] for m in matches),
        "passed": bool(all_pass),
    }


# ---------------------------------------------------------------------------
# Degeneracy, orientation, and bulge summaries


def weighted_quantile(values, weights, q):
    values, weights = np.asarray(values), np.asarray(weights)
    order = np.argsort(values)
    values, weights = values[order], weights[order]
    cdf = np.cumsum(weights)
    if len(values) == 0 or cdf[-1] <= 0:
        return math.nan
    return float(values[np.searchsorted(cdf, q * cdf[-1], side="left")])


def triangle_quality_report(vertices, triangles, altitude_epsilon=1e-5):
    coords = np.asarray(vertices, dtype=np.float64)[np.asarray(triangles, dtype=np.int64)]
    e0 = np.linalg.norm(coords[:, 1] - coords[:, 0], axis=1)
    e1 = np.linalg.norm(coords[:, 2] - coords[:, 1], axis=1)
    e2 = np.linalg.norm(coords[:, 0] - coords[:, 2], axis=1)
    twice_area = np.linalg.norm(np.cross(coords[:, 1] - coords[:, 0], coords[:, 2] - coords[:, 0]), axis=1)
    longest = np.maximum(np.maximum(e0, e1), e2)
    min_altitude = np.divide(twice_area, longest, out=np.zeros_like(twice_area), where=longest > 0)
    denom = e0 * e0 + e1 * e1 + e2 * e2
    quality = np.divide(2.0 * math.sqrt(3.0) * twice_area, denom, out=np.zeros_like(twice_area), where=denom > 0)
    degenerate = np.flatnonzero(min_altitude <= altitude_epsilon)
    return {
        "degenerate_faces": int(len(degenerate)),
        "degenerate_triangle_indices": degenerate[:50].astype(int).tolist(),
        "minimum_altitude": float(min_altitude.min(initial=math.inf)),
        "minimum_quality": float(quality.min(initial=math.inf)),
        "quality_p01": float(np.quantile(quality, 0.01)) if len(quality) else math.nan,
        "quality_median": float(np.median(quality)) if len(quality) else math.nan,
        "areas": twice_area * 0.5,
        "normals": np.divide(
            np.cross(coords[:, 1] - coords[:, 0], coords[:, 2] - coords[:, 0]),
            twice_area[:, None],
            out=np.zeros_like(coords[:, 0]),
            where=twice_area[:, None] > 0,
        ),
        "centroids": coords.mean(axis=1),
    }


def body_relative_report(surface_vertices, surface_triangles, boundary_vertex_indices, body_vertices, body_triangles, eps=1e-8):
    body_tree = TriangleAABBTree(body_vertices, body_triangles)
    quality = triangle_quality_report(surface_vertices, surface_triangles)
    centroids, normals, areas = quality["centroids"], quality["normals"], quality["areas"]
    vertex_signed, _, _ = signed_clearances(surface_vertices, body_tree, eps)
    centroid_signed, nearest_body, _ = signed_clearances(centroids, body_tree, eps)

    body_normals = []
    for i in nearest_body:
        tri = body_tree.coords[int(i)]
        n = np.cross(tri[1] - tri[0], tri[2] - tri[0])
        body_normals.append(n / np.linalg.norm(n))
    body_normals = np.asarray(body_normals)
    normal_dot = np.sum(normals * body_normals, axis=1)
    inverted = np.flatnonzero(normal_dot <= 0.0)

    boundary_set = set(map(int, boundary_vertex_indices))
    interior_vertices = np.array([i for i in range(len(surface_vertices)) if i not in boundary_set], dtype=np.int64)
    boundary_vertices = np.array(sorted(boundary_set), dtype=np.int64)
    interior_tri_mask = np.array([not any(int(v) in boundary_set for v in tri) for tri in surface_triangles], dtype=bool)
    bulge_values = centroid_signed[interior_tri_mask]
    bulge_weights = areas[interior_tri_mask]

    exact_distance = exact_surface_min_distance(surface_vertices, surface_triangles, body_tree, eps)
    intersections = cross_intersection_report(surface_vertices, surface_triangles, body_vertices, body_triangles, eps)
    result = {
        "body_triangle_intersections": intersections,
        "exact_surface_minimum_distance": exact_distance["minimum"],
        "exact_minimum_triangle_pair": exact_distance["triangle_pair"],
        "inside_vertices": int(np.sum(vertex_signed < -eps)),
        "inside_face_centroids": int(np.sum(centroid_signed < -eps)),
        "contact_vertices": int(np.sum(np.abs(vertex_signed) <= eps)),
        "minimum_signed_vertex_clearance": float(vertex_signed.min(initial=math.inf)),
        "minimum_signed_face_centroid_clearance": float(centroid_signed.min(initial=math.inf)),
        "minimum_interior_vertex_clearance": float(vertex_signed[interior_vertices].min(initial=math.inf)),
        "minimum_boundary_vertex_clearance": float(vertex_signed[boundary_vertices].min(initial=math.inf)),
        "inverted_faces": int(len(inverted)),
        "inverted_triangle_indices": inverted[:50].astype(int).tolist(),
        "surface_to_body_normal_dot_min": float(normal_dot.min(initial=math.inf)),
        "surface_to_body_normal_dot_p01": float(np.quantile(normal_dot, 0.01)) if len(normal_dot) else math.nan,
        "bulge_area_weighted": {
            "median": weighted_quantile(bulge_values, bulge_weights, 0.5),
            "p90": weighted_quantile(bulge_values, bulge_weights, 0.9),
            "maximum_centroid": float(bulge_values.max(initial=-math.inf)),
            "minimum_centroid": float(bulge_values.min(initial=math.inf)),
        },
        "vertex_signed_clearance": vertex_signed,
        "centroid_signed_clearance": centroid_signed,
    }
    return result
