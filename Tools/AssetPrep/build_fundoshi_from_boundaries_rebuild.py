"""V68 pair-of-pants rebuild — flatten-and-retriangulate method (fresh implementation).

Inputs (only): SumoRetopo body surface + the three cyclic EdgeTexel boundary
curves inside sumo_avatar_v67_gutterclean.blend.  No legacy Fundoshi mesh is
loaded, inspected, or reused.

Method A variant that removes the failure mode of the previous rebuild
(collar/zipper between an extracted patch boundary and the exact curves):

1. Audit the three input loops (closure, self-crossing, length, body distance).
2. Extract the pelvis region of SumoRetopo bounded by the three curves
   (barrier-face flood fill) plus a safety margin.
3. Flatten the patch with a Tutte embedding (waist loop -> unit circle,
   leg loops -> interior circles placed from a free harmonic pre-solve).
4. Map the exact curves into the parameter domain, simplify with RDP 0.1 mm
   (kept vertices are original curve points -> boundary is exact by
   construction), and run constrained Delaunay triangulation with
   3D-uniform Steiner points.  The CDT boundary IS the input curve chain:
   there is no zipper step at all.
5. Lift interior vertices back onto the body surface, fair with pinned
   boundaries (Taubin + clearance floor pushes), then add a low-frequency
   outward bulge along body normals with per-vertex medial-axis freezing.
6. Validate with the coplanar-safe triangle machinery in
   fundoshi_geometry_validation.py, save the blend, render check images.

Run:
  blender --background --python build_fundoshi_from_boundaries_rebuild.py
"""

import bpy
import json
import math
import os
import sys
import time
import traceback
from collections import defaultdict
from heapq import heappush, heappop

import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from mathutils.kdtree import KDTree
from mathutils.geometry import delaunay_2d_cdt

ROOT = r"C:\games\Unreal Engine\nice_ink"
SRC_BLEND = os.path.join(ROOT, "SourceAssets", "previews", "sumo_avatar_v67_gutterclean.blend")
OUT_BLEND = os.path.join(ROOT, "SourceAssets", "previews", "sumo_avatar_v70_pairpants_solid.blend")
OUT_DIR = os.path.join(ROOT, "Saved", "V70Solid")
APPLY_SOLIDIFY = True     # bake thickness into real vertices (user-editable)
sys.path.insert(0, os.path.join(ROOT, "Tools", "AssetPrep"))
import fundoshi_geometry_validation as fgv  # verification instrument only

MM = 1000.0
RDP_TOL = 0.10            # mm certified boundary simplification
TARGET_EDGE = 10.0        # mm interior Steiner spacing
STEINER_CLEAR = 4.0       # mm keep-out around boundary curves
FLOOR_BASE = 1.1          # mm interior clearance floor amplitude (1.8/5.0 tried: no lump improvement, transition band worsens)
FLOOR_RAMP = 12.0         # mm ramp length for the clearance floor
BULGE_RAMPS = ((1.35, 40.0), (1.5, 85.0))  # (amplitude mm, ramp mm)
INTERIOR_BAND = 3.0       # mm boundary band excluded from interior gates
PROXY_TAUBIN_PAIRS = 20   # low-pass strength of the proxy obstacle (35 tried: no visible gain, boundary transition slightly worse)
COMP_SMOOTH_ITERS = 60    # shrink-compensation field smoothing
SOLIDIFY_MM = 2.0         # outward cloth thickness (live modifier)

os.makedirs(OUT_DIR, exist_ok=True)
PROGRESS = os.path.join(OUT_DIR, "rebuild_progress.jsonl")
T0 = time.time()


def log(stage, **kw):
    rec = {"t": round(time.time() - T0, 1), "stage": stage}
    rec.update(kw)
    line = json.dumps(rec, default=str)
    print("[rebuild]", line, flush=True)
    with open(PROGRESS, "a", encoding="utf-8") as fh:
        fh.write(line + "\n")


def smoothstep(t):
    t = np.clip(t, 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


# ---------------------------------------------------------------------------
# Stage 1: audit


def load_curve_mm(obj):
    if obj.type != "CURVE":
        raise RuntimeError(f"{obj.name} is not a curve")
    splines = obj.data.splines
    info = {"object": obj.name, "splines": len(splines)}
    if len(splines) != 1:
        raise RuntimeError(f"{obj.name}: expected a single spline, found {len(splines)}")
    sp = splines[0]
    info["type"] = sp.type
    info["cyclic"] = bool(sp.use_cyclic_u)
    mat = obj.matrix_world
    pts = np.array([(mat @ Vector(p.co[:3]))[:] for p in sp.points], dtype=np.float64) * MM
    info["points"] = len(pts)
    seg = np.roll(pts, -1, axis=0) - pts
    seglen = np.linalg.norm(seg, axis=1)
    info["length_mm"] = float(seglen.sum())
    info["bbox_mm"] = [pts.min(axis=0).tolist(), pts.max(axis=0).tolist()]
    info["centroid_mm"] = pts.mean(axis=0).tolist()
    info["min_edge_mm"] = float(seglen.min())
    info["max_edge_mm"] = float(seglen.max())
    return pts, info


def polyline_self_crossing(pts, hard=0.02, warn=0.30):
    """Exact segment-segment min distance between non-neighbouring segments."""
    n = len(pts)
    mids = (pts + np.roll(pts, -1, axis=0)) * 0.5
    kd = KDTree(n)
    for i, m in enumerate(mids):
        kd.insert(Vector(m), i)
    kd.balance()
    hard_pairs, warn_pairs, min_d = [], 0, math.inf
    q = np.roll(pts, -1, axis=0)
    for i in range(n):
        for (_, j, _) in kd.find_range(Vector(mids[i]), 2.5):
            if j <= i:
                continue
            gap = min(j - i, n - (j - i))
            if gap <= 2:
                continue
            d = math.sqrt(fgv.segment_segment_distance2(pts[i], q[i], pts[j], q[j]))
            min_d = min(min_d, d)
            if d < hard:
                hard_pairs.append((i, j, d))
            elif d < warn:
                warn_pairs += 1
    return hard_pairs, warn_pairs, min_d


def audit(report):
    body = bpy.data.objects["SumoRetopo"]
    mat = np.array(body.matrix_world)
    mesh = body.data
    nv = len(mesh.vertices)
    co = np.empty(nv * 3)
    mesh.vertices.foreach_get("co", co)
    co = co.reshape(-1, 3)
    co = (co @ mat[:3, :3].T + mat[:3, 3]) * MM
    faces = [tuple(p.vertices) for p in mesh.polygons]

    # The EdgeTexel curves were produced by UV->3D resolution on the fan
    # triangulation of these quads.  BVHTree's internal quad split uses the
    # other diagonal on some warped quads (up to ~1 cm apart), so every BVH
    # here is built from the explicit fan triangulation instead.
    body_tris, tri_owner = triangulate_faces(faces)
    bvh = BVHTree.FromPolygons([tuple(v) for v in co],
                               [tuple(int(i) for i in t) for t in body_tris], all_triangles=True)

    # orientation / closedness of the body (needed for signed clearances)
    edge_count = defaultdict(int)
    for f in faces:
        for a, b in zip(f, f[1:] + f[:1]):
            edge_count[(a, b) if a < b else (b, a)] += 1
    open_edges = sum(1 for c in edge_count.values() if c == 1)

    import bmesh
    bm = bmesh.new()
    bm.from_mesh(mesh)
    vol = bm.calc_volume(signed=True)
    bm.free()

    curves = {}
    infos = []
    for name in ("EdgeTexel0", "EdgeTexel1", "EdgeTexel2", "EdgeTexel3"):
        if name not in bpy.data.objects:
            infos.append({"object": name, "present": False})
            continue
        pts, info = load_curve_mm(bpy.data.objects[name])
        hard, warn, min_d = polyline_self_crossing(pts)
        info["self_cross_hard"] = hard
        info["near_touch_pairs_lt_0p3mm"] = warn
        info["min_nonadjacent_segment_distance_mm"] = min_d
        d = np.empty(len(pts))
        for i, p in enumerate(pts):
            hit = bvh.find_nearest(Vector(p))
            d[i] = (np.linalg.norm(np.array(hit[0]) - p)) if hit[0] is not None else math.inf
        info["body_distance_mm"] = {
            "median": float(np.median(d)), "p99": float(np.quantile(d, 0.99)), "max": float(d.max())}
        curves[name] = pts
        infos.append(info)
        log("audit_curve", **{k: v for k, v in info.items() if k != "self_cross_hard"},
            hard_crossings=len(hard))

    # loop identification: three long loops are the boundaries; anything tiny
    # (like the 92 mm EdgeTexel3 helper fragment) is excluded.
    long_loops = {n: c for n, c in curves.items() if len(c) > 200}
    if len(long_loops) != 3:
        raise RuntimeError(f"expected 3 long closed loops, got {list(long_loops)}")
    cents = {n: c.mean(axis=0) for n, c in long_loops.items()}
    lens = {n: infos[[i["object"] for i in infos].index(n)]["length_mm"] for n in long_loops}
    waist = max(lens, key=lens.get)
    legs = sorted((n for n in long_loops if n != waist), key=lambda n: cents[n][0])
    roles = {"waist": waist, "leg_negx": legs[0], "leg_posx": legs[1]}

    # Sub-texel near-touches (tens of nm .. tens of um) are marching-squares
    # staircase artifacts, orders of magnitude below the 0.1 mm RDP scale.
    # They are recorded here; the load-bearing simplicity gate is the RDP
    # polygon simplicity check in UV before CDT.

    report["audit"] = {
        "body": {"vertices": nv, "faces": len(faces), "open_boundary_edges": open_edges,
                 "signed_volume_m3": vol, "modifiers": [m.type for m in body.modifiers]},
        "curves": infos, "roles": roles,
    }
    if vol <= 0:
        raise RuntimeError("SumoRetopo signed volume is not positive; outward orientation assumption fails")
    log("audit_done", roles=roles, body_open_edges=open_edges, volume=round(vol, 4))
    return co, faces, body_tris, tri_owner, bvh, {r: long_loops[n] for r, n in roles.items()}, roles


# ---------------------------------------------------------------------------
# Stage 2: pelvis patch extraction


def extract_patch(co, faces, bvh, tri_owner, loops):
    nf = len(faces)
    edge_faces = defaultdict(list)
    for fi, f in enumerate(faces):
        for a, b in zip(f, f[1:] + f[:1]):
            edge_faces[(a, b) if a < b else (b, a)].append(fi)
    fadj = [[] for _ in range(nf)]
    for fs in edge_faces.values():
        for i in range(len(fs)):
            for j in range(i + 1, len(fs)):
                fadj[fs[i]].append(fs[j])
                fadj[fs[j]].append(fs[i])

    barrier = {}
    for role, pts in loops.items():
        s = set()
        for p in pts:
            hit = bvh.find_nearest(Vector(p))
            s.add(tri_owner[hit[2]])
        barrier[role] = s
    barrier_all = set().union(*barrier.values())

    comp = np.full(nf, -1)
    cid = 0
    for seed in range(nf):
        if comp[seed] != -1 or seed in barrier_all:
            continue
        stack = [seed]
        comp[seed] = cid
        while stack:
            f = stack.pop()
            for g in fadj[f]:
                if comp[g] == -1 and g not in barrier_all:
                    comp[g] = cid
                    stack.append(g)
        cid += 1

    touch = defaultdict(set)   # component -> roles touched
    for role, bs in barrier.items():
        for f in bs:
            for g in fadj[f]:
                if comp[g] != -1:
                    touch[comp[g]].add(role)
    # The fundoshi region is a narrow band: barrier faces split it into many
    # pockets.  Every component adjacent to at least two distinct boundary
    # curves lies between them, so the region is the union of those pockets.
    pelvis_comps = [c for c, r in touch.items() if len(r) >= 2]
    if not pelvis_comps:
        raise RuntimeError(f"no component touches two boundary curves: touch={dict(touch)}")
    region = set()
    for c in pelvis_comps:
        region.update(np.flatnonzero(comp == c).tolist())
    log("region_pockets", pockets=len(pelvis_comps), faces=len(region), barrier=len(barrier_all))

    patch = region | barrier_all
    # margin: two vertex-adjacency dilations so the curves sit strictly inside
    vfaces = defaultdict(list)
    for fi, f in enumerate(faces):
        for v in f:
            vfaces[v].append(fi)
    for _ in range(2):
        grow = set()
        for f in patch:
            for v in faces[f]:
                grow.update(vfaces[v])
        patch |= grow
    # fill enclosed single-face holes
    for _ in range(5):
        added = 0
        pverts = set(v for f in patch for v in faces[f])
        for fi in range(nf):
            if fi in patch:
                continue
            if all(v in pverts for v in faces[fi]) and sum(g in patch for g in fadj[fi]) >= 2:
                patch.add(fi)
                added += 1
        if not added:
            break

    # boundary shave: one-face-wide tabs/bridges on the ragged margin put
    # loop-distant boundary vertices into a single face, which breaks the
    # circle embedding later.  Remove faces whose vertices ALL lie on the
    # patch boundary (never a barrier face carrying the curves).
    for _ in range(10):
        edge_use = defaultdict(int)
        for f in patch:
            ff = faces[f]
            for a, bb in zip(ff, ff[1:] + ff[:1]):
                edge_use[(a, bb) if a < bb else (bb, a)] += 1
        bverts = set()
        for (a, bb), cnt in edge_use.items():
            if cnt == 1:
                bverts.add(a)
                bverts.add(bb)
        shave = [f for f in patch if f not in barrier_all and all(v in bverts for v in faces[f])]
        if not shave:
            break
        patch -= set(shave)
    log("shaved_patch", faces=len(patch))

    patch = sorted(patch)
    pv = sorted(set(v for f in patch for v in faces[f]))
    g2l = {g: i for i, g in enumerate(pv)}
    pfaces = [tuple(g2l[v] for v in faces[f]) for f in patch]
    pco = co[pv]
    topo = fgv.topology_report(len(pv), pfaces)
    log("patch", faces=len(pfaces), verts=len(pv), loops=topo["boundary_loops"],
        components=topo["components"], nonmanifold=topo["nonmanifold_internal_edges"])
    if topo["components"] != 1 or topo["boundary_loops"] != 3 or topo["nonmanifold_internal_edges"]:
        raise RuntimeError(f"patch topology unsuitable: {topo['components']} comps, "
                           f"{topo['boundary_loops']} loops, {topo['nonmanifold_internal_edges']} nonmanifold")
    return pco, pfaces, topo["boundary_loop_vertex_indices"], [faces[f] for f in patch]


# ---------------------------------------------------------------------------
# Stage 3: Tutte embedding


def triangulate_faces(pfaces):
    tris, owner = [], []
    for fi, f in enumerate(pfaces):
        for k in range(1, len(f) - 1):
            tris.append((f[0], f[k], f[k + 1]))
            owner.append(fi)
    return np.array(tris), owner


def build_adjacency(n, pfaces):
    nbrs = [set() for _ in range(n)]
    for f in pfaces:
        for a, b in zip(f, f[1:] + f[:1]):
            nbrs[a].add(b)
            nbrs[b].add(a)
    return [sorted(s) for s in nbrs]


def solve_tutte(n, nbrs, fixed_uv):
    """fixed_uv: dict vert -> (u, v). Uniform-weight harmonic solve, dense."""
    free = [i for i in range(n) if i not in fixed_uv]
    idx = {v: i for i, v in enumerate(free)}
    m = len(free)
    A = np.zeros((m, m))
    B = np.zeros((m, 2))
    for v in free:
        i = idx[v]
        deg = len(nbrs[v])
        A[i, i] = deg
        for w in nbrs[v]:
            if w in fixed_uv:
                B[i] += fixed_uv[w]
            else:
                A[i, idx[w]] -= 1.0
    X = np.linalg.solve(A, B)
    uv = np.zeros((n, 2))
    for v, val in fixed_uv.items():
        uv[v] = val
    for v in free:
        uv[v] = X[idx[v]]
    return uv


def arc_angles(loop, pco):
    P = pco[loop]
    seg = np.linalg.norm(np.roll(P, -1, axis=0) - P, axis=1)
    total = seg.sum()
    s = np.concatenate([[0.0], np.cumsum(seg[:-1])])
    return s / total * 2.0 * math.pi


def loop_winding(uv_pts, center):
    d = uv_pts - center
    ang = np.arctan2(d[:, 1], d[:, 0])
    inc = np.diff(np.concatenate([ang, ang[:1]]))
    inc = (inc + math.pi) % (2 * math.pi) - math.pi
    return 1.0 if inc.sum() >= 0 else -1.0


def smart_triangulate(pfaces, boundary_set):
    """Fan every polygon from an interior vertex, so no triangle has all of
    its vertices pinned on the domain boundary (those invert at reflex
    corners under any convex boundary embedding — unfixable otherwise)."""
    face_tris = []
    tris = []
    for f in pfaces:
        if len(f) == 3 or all(v in boundary_set for v in f):
            t = [tuple(f[k] for k in (0, i, i + 1)) for i in range(1, len(f) - 1)]
            if len(f) > 3:
                raise RuntimeError("polygon with every vertex on the boundary survived shaving")
        else:
            k = next(i for i, v in enumerate(f) if v not in boundary_set)
            ff = f[k:] + f[:k]
            t = [(ff[0], ff[i], ff[i + 1]) for i in range(1, len(ff) - 1)]
        face_tris.append(t)
        tris.extend(t)
    return np.array(tris), face_tris


def flatten_patch(pco, pfaces, bloops, curve_loops):
    n = len(pco)
    nbrs = build_adjacency(n, pfaces)
    boundary_set = set(v for loop in bloops for v in loop)
    tris, face_tris = smart_triangulate(pfaces, boundary_set)

    # assign patch boundary loops to curve roles by proximity
    kd = {}
    for role, pts in curve_loops.items():
        k = KDTree(len(pts))
        for i, p in enumerate(pts):
            k.insert(Vector(p), i)
        k.balance()
        kd[role] = k
    assign = {}
    for li, loop in enumerate(bloops):
        means = {}
        for role in curve_loops:
            ds = [kd[role].find(Vector(pco[v]))[2] for v in loop[::max(1, len(loop) // 40)]]
            means[role] = float(np.mean(ds))
        assign[min(means, key=means.get)] = li
    if len(assign) != 3:
        raise RuntimeError(f"patch boundary loops could not be assigned uniquely: {assign}")
    waist_loop = bloops[assign["waist"]]
    leg_loops = [bloops[assign["leg_negx"]], bloops[assign["leg_posx"]]]

    # solve 1: waist on unit circle, everything else free
    th = arc_angles(waist_loop, pco)
    fixed = {v: (math.cos(a), math.sin(a)) for v, a in zip(waist_loop, th)}
    uv1 = solve_tutte(n, nbrs, fixed)

    def leg_circle(loop, radius_scale, centers):
        c = uv1[loop].mean(axis=0)
        return c

    c1 = uv1[leg_loops[0]].mean(axis=0)
    c2 = uv1[leg_loops[1]].mean(axis=0)
    base_r = np.linalg.norm(c1 - c2) * 0.30

    fixed_set = set(waist_loop) | set(leg_loops[0]) | set(leg_loops[1])

    def signed_areas(uv):
        a, b, cc = uv[tris[:, 0]], uv[tris[:, 1]], uv[tris[:, 2]]
        return (b[:, 0] - a[:, 0]) * (cc[:, 1] - a[:, 1]) - (b[:, 1] - a[:, 1]) * (cc[:, 0] - a[:, 0])

    def untangle(uv, max_it=300):
        for _ in range(max_it):
            area2 = signed_areas(uv)
            bad = np.flatnonzero(area2 <= 0)
            if len(bad) == 0:
                return 0
            bad_verts = set()
            for t in tris[bad]:
                bad_verts.update(int(v) for v in t)
            ring = set(bad_verts)
            for v in bad_verts:
                ring.update(nbrs[v])
            movable = [v for v in ring if v not in fixed_set]
            if not movable:
                return len(bad)
            for v in movable:
                uv[v] = np.mean(uv[list(nbrs[v])], axis=0)
        return int(np.sum(signed_areas(uv) <= 0))

    def full_solve(scale):
        fixed2 = dict(fixed)
        for loop, c in ((leg_loops[0], c1), (leg_loops[1], c2)):
            r = max(0.05, min(base_r * scale, 0.45 * (1.0 - np.linalg.norm(c))))
            th = arc_angles(loop, pco)
            w = loop_winding(uv1[loop], c)
            th = th * w
            free_ang = np.arctan2(uv1[loop][:, 1] - c[1], uv1[loop][:, 0] - c[0])
            d = free_ang - th
            phase = math.atan2(np.sin(d).mean(), np.cos(d).mean())
            for v, a in zip(loop, th):
                fixed2[v] = (c[0] + r * math.cos(a + phase), c[1] + r * math.sin(a + phase))
        uv = solve_tutte(n, nbrs, fixed2)
        area2 = signed_areas(uv)
        if np.median(area2) < 0:
            uv[:, 0] *= -1.0
            area2 = signed_areas(uv)
        bad = np.flatnonzero(area2 <= 0)
        fixed_counts = [int(sum(v in fixed_set for v in t)) for t in tris[bad]]
        log("tutte_flip_diag", flips=len(bad),
            by_fixed_verts={k: fixed_counts.count(k) for k in range(4)})
        flips = untangle(uv)
        return uv, flips

    for scale in (1.0, 0.55, 1.6):
        uv, flips = full_solve(scale)
        log("tutte", radius_scale=scale, flipped_after_untangle=flips)
        if flips == 0:
            return uv, tris, face_tris, waist_loop, leg_loops
    raise RuntimeError(f"Tutte embedding kept {flips} flipped triangles after retries")


# ---------------------------------------------------------------------------
# Stage 4: curves -> UV, RDP, Steiner, CDT


def tri_bary(p, a, b, c):
    v0, v1, v2 = b - a, c - a, p - a
    d00, d01, d11 = v0 @ v0, v0 @ v1, v1 @ v1
    d20, d21 = v2 @ v0, v2 @ v1
    den = d00 * d11 - d01 * d01
    if abs(den) < 1e-18:
        return None
    v = (d11 * d20 - d01 * d21) / den
    w = (d00 * d21 - d01 * d20) / den
    return np.array([1.0 - v - w, v, w])


def face_uv_of_point(p, ftris, pco, uv):
    """point p known to lie on/near patch face -> UV via sub-triangle barycentric."""
    best, best_min = None, -1e9
    for ids in ftris:
        bar = tri_bary(p, pco[ids[0]], pco[ids[1]], pco[ids[2]])
        if bar is None:
            continue
        mn = bar.min()
        if mn > best_min:
            best_min, best = mn, (ids, bar)
    ids, bar = best
    bar = np.clip(bar, 0.0, None)
    bar /= bar.sum()
    return bar[0] * uv[ids[0]] + bar[1] * uv[ids[1]] + bar[2] * uv[ids[2]]


def rdp_closed(pts, tol):
    n = len(pts)
    far = int(np.argmax(np.linalg.norm(pts - pts[0], axis=1)))
    keep = {0, far}

    def seg_dist(p, a, b):
        ab = b - a
        t = np.clip(((p - a) @ ab) / max(ab @ ab, 1e-30), 0.0, 1.0)
        return np.linalg.norm(p - (a + t * ab))

    stack = [(0, far), (far, n)]  # second range wraps via modulo end==n meaning index 0
    while stack:
        i, j = stack.pop()
        a, b = pts[i % n], pts[j % n]
        worst, wd = -1, tol
        for k in range(i + 1, j):
            d = seg_dist(pts[k % n], a, b)
            if d > wd:
                wd, worst = d, k
        if worst >= 0:
            keep.add(worst % n)
            stack.append((i, worst))
            stack.append((worst, j))
    return sorted(keep)


def polygon_crossings(poly):
    """poly (k,2). Return list of (i, j) segment pairs that properly cross."""
    k = len(poly)
    a = poly
    b = np.roll(poly, -1, axis=0)
    hits = []
    for i in range(k):
        js = np.arange(i + 2, k if i > 0 else k - 1)
        if len(js) == 0:
            continue
        p, r = a[i], b[i] - a[i]
        q, s = a[js], b[js] - a[js]
        denom = r[0] * s[:, 1] - r[1] * s[:, 0]
        qp = q - p
        t_num = qp[:, 0] * s[:, 1] - qp[:, 1] * s[:, 0]
        u_num = qp[:, 0] * r[1] - qp[:, 1] * r[0]
        with np.errstate(divide="ignore", invalid="ignore"):
            t = t_num / denom
            u = u_num / denom
        cross = (np.abs(denom) > 1e-18) & (t > 1e-12) & (t < 1 - 1e-12) & (u > 1e-12) & (u < 1 - 1e-12)
        hits.extend((i, int(j)) for j in js[cross])
    return hits


def build_cdt_input(curve_loops, pco, pfaces, face_tris, uv,
                    roles_order=("waist", "leg_negx", "leg_posx")):
    # BVH from the fan triangulation (matches the curves' source surface);
    # UV evaluation uses the interior-apex triangulation of the embedding.
    ptris, powner = triangulate_faces(pfaces)
    pbvh = BVHTree.FromPolygons([tuple(v) for v in pco],
                                [tuple(int(i) for i in t) for t in ptris], all_triangles=True)

    boundary_uv, boundary_3d, chains = [], [], []
    polys_uv = {}
    for role in roles_order:
        pts = curve_loops[role]
        kept = rdp_closed(pts, RDP_TOL)
        uv_pts = []
        for ki in kept:
            p = pts[ki]
            hit = pbvh.find_nearest(Vector(p))
            uv_pts.append(face_uv_of_point(np.array(hit[0]), face_tris[powner[hit[2]]], pco, uv))
        uv_pts = np.array(uv_pts)
        # micro staircase artifacts can make the simplified polygon cross
        # itself over sub-texel spans; drop one endpoint of a crossing pair
        # (micron-scale position change, far below the 0.25 mm budget)
        removed = 0
        for _ in range(40):
            hits = polygon_crossings(uv_pts)
            if not hits:
                break
            i, j = hits[0]
            seg_i = np.linalg.norm(uv_pts[(i + 1) % len(uv_pts)] - uv_pts[i])
            seg_j = np.linalg.norm(uv_pts[(j + 1) % len(uv_pts)] - uv_pts[j])
            drop = (i + 1) % len(uv_pts) if seg_i <= seg_j else (j + 1) % len(uv_pts)
            uv_pts = np.delete(uv_pts, drop, axis=0)
            kept = kept[:drop] + kept[drop + 1:]
            removed += 1
        else:
            raise RuntimeError(f"{role}: UV polygon still self-crosses after 40 removals")
        # prune locally near-collinear survivors (micron-scale) that would
        # otherwise force zero-altitude CDT slivers against far constraints
        while True:
            m = len(kept)
            drop = None
            P3 = pts[kept]
            for k in range(m):
                a, b, c = P3[k - 1], P3[k], P3[(k + 1) % m]
                ab = c - a
                t = np.clip(((b - a) @ ab) / max(ab @ ab, 1e-30), 0.0, 1.0)
                if np.linalg.norm(b - (a + t * ab)) < 0.003:
                    drop = k
                    break
            if drop is None:
                break
            kept = kept[:drop] + kept[drop + 1:]
            uv_pts = np.delete(uv_pts, drop, axis=0)
            removed += 1
        start = len(boundary_uv)
        boundary_uv.extend(uv_pts.tolist())
        boundary_3d.extend(pts[kept].tolist())
        chains.append(list(range(start, start + len(kept))))
        polys_uv[role] = uv_pts
        log("rdp", role=role, kept=len(kept), of=len(pts), crossing_fix_removed=removed)

    # Steiner points: 3D-uniform lattice on patch faces, kept clear of curves
    allc = np.concatenate([curve_loops[r] for r in roles_order])
    ckd = KDTree(len(allc))
    for i, p in enumerate(allc):
        ckd.insert(Vector(p), i)
    ckd.balance()

    seen = set()
    st_uv, st_3d = [], []

    def try_add(p3, puv):
        key = tuple(np.round(p3 / (TARGET_EDGE * 0.45)).astype(int))
        if key in seen:
            return
        if ckd.find(Vector(p3))[2] < STEINER_CLEAR:
            return
        seen.add(key)
        st_uv.append(puv)
        st_3d.append(p3)

    for v in range(len(pco)):
        try_add(pco[v], uv[v])
    for ft in face_tris:
        for ids in ft:
            A, B, C = pco[ids[0]], pco[ids[1]], pco[ids[2]]
            area = 0.5 * np.linalg.norm(np.cross(B - A, C - A))
            kdiv = int(round(math.sqrt(max(area, 1.0) * 2.0) / TARGET_EDGE))
            for i in range(1, kdiv):
                for j in range(1, kdiv - i):
                    w = np.array([1.0 - (i + j) / kdiv, i / kdiv, j / kdiv])
                    p3 = w[0] * A + w[1] * B + w[2] * C
                    puv = w[0] * uv[ids[0]] + w[1] * uv[ids[1]] + w[2] * uv[ids[2]]
                    try_add(p3, puv)
    log("steiner", count=len(st_uv), boundary_pts=len(boundary_uv))
    return boundary_uv, boundary_3d, chains, st_uv, st_3d, polys_uv


def winding_inside(points, poly):
    out = np.zeros(len(points), dtype=bool)
    x0, y0 = poly[:, 0], poly[:, 1]
    x1, y1 = np.roll(x0, -1), np.roll(y0, -1)
    for s in range(0, len(points), 4096):
        P = points[s:s + 4096]
        x, y = P[:, 0][:, None], P[:, 1][:, None]
        cond = ((y0 <= y) & (y1 > y)) | ((y1 <= y) & (y0 > y))
        with np.errstate(divide="ignore", invalid="ignore"):
            t = (y - y0) / (y1 - y0)
            xi = x0 + t * (x1 - x0)
        cross = np.sum(cond & (xi > x), axis=1)
        out[s:s + len(P)] = (cross % 2) == 1
    return out


def run_cdt(boundary_uv, boundary_3d, chains, st_uv, st_3d, polys_uv):
    verts = [Vector(p) for p in boundary_uv] + [Vector(p) for p in st_uv]
    edges = []
    for ch in chains:
        edges.extend([(ch[i], ch[(i + 1) % len(ch)]) for i in range(len(ch))])
    out_v, out_e, out_f, ov, oe, of_ = delaunay_2d_cdt(verts, edges, [], 0, 1e-12)

    n_in = len(verts)
    owner = [None] * len(out_v)
    for oi, src in enumerate(ov):
        for si in src:
            if si < n_in:
                if owner[oi] is None or si < owner[oi]:
                    owner[oi] = si
    # 3D lift per output vertex
    all3d = boundary_3d + st_3d
    lift = np.zeros((len(out_v), 3))
    unmapped = []
    for oi in range(len(out_v)):
        if owner[oi] is not None:
            lift[oi] = all3d[owner[oi]]
        else:
            unmapped.append(oi)
    if unmapped:
        raise RuntimeError(f"CDT created {len(unmapped)} vertices not among the inputs "
                           f"(constraint polygons must have intersected)")

    uv_out = np.array([v[:] for v in out_v])
    cent = np.array([(uv_out[f[0]] + uv_out[f[1]] + uv_out[f[2]]) / 3.0 for f in out_f])
    inside = winding_inside(cent, np.array(polys_uv["waist"]))
    for leg in ("leg_negx", "leg_posx"):
        inside &= ~winding_inside(cent, np.array(polys_uv[leg]))
    tris = []
    for fi, f in enumerate(out_f):
        if not inside[fi]:
            continue
        a, b, c = (uv_out[f[0]], uv_out[f[1]], uv_out[f[2]])
        if (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]) < 0:
            f = (f[0], f[2], f[1])
        tris.append(tuple(f))

    used = sorted(set(v for f in tris for v in f))
    remap = {v: i for i, v in enumerate(used)}
    V = lift[used]
    T = np.array([[remap[a], remap[b], remap[c]] for a, b, c in tris])
    # boundary vertex ids (exact curve points) in new indexing
    bset = set()
    for oi in used:
        if owner[oi] is not None and owner[oi] < len(boundary_3d):
            bset.add(remap[oi])
    log("cdt", out_verts=len(V), out_tris=len(T), boundary_verts=len(bset))
    return V, T, bset


def build_proxy(co, faces, body_tris, bvh_true):
    """Low-pass obstacle: the frequency content of a contact-constrained
    surface is dictated by its obstacle, so clearance floors and bulges are
    computed against a Taubin-smoothed body plus a smoothed shrink
    compensation field (restores the low-frequency silhouette that pure
    smoothing sinks in convex regions).  The TRUE body remains the hard
    non-penetration authority downstream."""
    n = len(co)
    nbrs = [set() for _ in range(n)]
    for f in faces:
        for a, b in zip(f, f[1:] + f[:1]):
            nbrs[a].add(b)
            nbrs[b].add(a)
    idx = np.concatenate([np.fromiter(s, int) for s in nbrs])
    ptr = np.concatenate([[0], np.cumsum([len(s) for s in nbrs])])

    cs = co.copy()
    for _ in range(PROXY_TAUBIN_PAIRS):
        for lam in (0.5, -0.53):
            avg = neighbor_average(cs, idx, ptr)
            cs += lam * (avg - cs)

    tv = cs[body_tris]
    fn = np.cross(tv[:, 1] - tv[:, 0], tv[:, 2] - tv[:, 0])
    vn = np.zeros_like(cs)
    for k in range(3):
        np.add.at(vn, body_tris[:, k], fn)
    vn = vn / np.maximum(np.linalg.norm(vn, axis=1, keepdims=True), 1e-30)

    d = np.sum((co - cs) * vn, axis=1)
    for _ in range(COMP_SMOOTH_ITERS):
        avg = np.add.reduceat(d[idx], ptr[:-1]) / np.maximum(ptr[1:] - ptr[:-1], 1)
        d = 0.5 * d + 0.5 * avg
    proxy = cs + vn * d[:, None]

    # upper-envelope lift: wherever the proxy sank below the TRUE body, lift
    # it smoothly back above (max-propagation keeps the lift a broad mound,
    # not a spike), so floors computed against the proxy already imply
    # true-body clearance and the downstream exact heal has nothing to tent
    for _ in range(3):
        s_true = np.empty(len(proxy))
        for i, p in enumerate(proxy):
            co_, n_, fi_, d_ = bvh_true.find_nearest(Vector(p))
            q = np.array(co_)
            dd = np.linalg.norm(p - q)
            s_true[i] = dd if (p - q) @ np.array(n_) >= 0 else -dd
        lift = np.maximum(0.0, 0.3 - s_true)
        if lift.max() <= 1e-3:
            break
        for _ in range(4):
            nb_max = np.maximum.reduceat(lift[idx], ptr[:-1])
            nb_max[ptr[1:] - ptr[:-1] == 0] = 0.0
            lift = np.maximum(lift, 0.75 * nb_max)
        for _ in range(3):
            avg = np.add.reduceat(lift[idx], ptr[:-1]) / np.maximum(ptr[1:] - ptr[:-1], 1)
            lift = 0.5 * lift + 0.5 * avg
        proxy += vn * lift[:, None]
        log("proxy_envelope_lift", below=int(np.sum(s_true < 0.3)), max_lift=float(lift.max()))

    resid = np.linalg.norm(proxy - co, axis=1)
    log("proxy", taubin_pairs=PROXY_TAUBIN_PAIRS,
        offset_mm={"median": float(np.median(resid)), "p90": float(np.quantile(resid, 0.9)),
                   "max": float(resid.max())})
    return proxy


def true_body_vertex_floor(X, interior, bvh_true, rounds=4):
    """Proxy smoothing cuts sharp true-body peaks; any vertex left inside or
    grazing the TRUE body is pushed just outside it (cloth rests on peaks,
    spans valleys smoothly)."""
    total = 0
    for _ in range(rounds):
        s, nrm, near = clearances(X, bvh_true)
        low = interior & (s < 0.05)
        if not low.any():
            break
        X[low] = near[low] + nrm[low] * 0.15
        total += int(low.sum())
    log("true_vertex_floor", pushed=total)


def drop_degenerate_ears(V, T, bset):
    """Remove near-zero-altitude ear triangles wedged inside switchback
    spikes of the input curves.  Deleting an ear whose altitude is a few
    nanometres moves the boundary by that same altitude — no-op vs the
    0.25 mm budget — while a pinned sliver can never be repaired by moving
    interior vertices."""
    for _ in range(6):
        coords = V[T]
        e0 = coords[:, 1] - coords[:, 0]
        e1 = coords[:, 2] - coords[:, 0]
        twice_area = np.linalg.norm(np.cross(e0, e1), axis=1)
        longest = np.maximum(np.maximum(
            np.linalg.norm(e0, axis=1), np.linalg.norm(e1, axis=1)),
            np.linalg.norm(coords[:, 2] - coords[:, 1], axis=1))
        alt = np.divide(twice_area, longest, out=np.zeros_like(twice_area), where=longest > 0)
        bad = np.flatnonzero(alt < 1e-5)
        if len(bad) == 0:
            return V, T, bset
        edge_faces = defaultdict(list)
        for fi, t in enumerate(T):
            for a, b in ((t[0], t[1]), (t[1], t[2]), (t[2], t[0])):
                edge_faces[(a, b) if a < b else (b, a)].append(fi)
        drop = []
        for fi in bad:
            t = T[fi]
            nb = sum(1 for a, b in ((t[0], t[1]), (t[1], t[2]), (t[2], t[0]))
                     if len(edge_faces[(a, b) if a < b else (b, a)]) == 1)
            if nb >= 1:
                # removal reroutes the boundary along the sliver's other
                # edges, which lie within its altitude (nanometres) of the
                # original boundary edge
                drop.append(int(fi))
        if not drop:
            log("degenerate_interior_sliver", faces=[int(f) for f in bad], altitudes=alt[bad].tolist())
            return V, T, bset
        # safety: dropping must not create a pinched boundary vertex
        trial = np.ones(len(T), dtype=bool)
        trial[drop] = False
        deg = defaultdict(int)
        for fi in np.flatnonzero(trial):
            t = T[fi]
            for a, b in ((t[0], t[1]), (t[1], t[2]), (t[2], t[0])):
                deg[(a, b) if a < b else (b, a)] += 1
        vdeg = defaultdict(int)
        for e, c in deg.items():
            if c == 1:
                vdeg[e[0]] += 1
                vdeg[e[1]] += 1
        if any(c != 2 for c in vdeg.values() if c):
            log("ear_drop_would_pinch", faces=drop)
            return V, T, bset
        keep = np.ones(len(T), dtype=bool)
        keep[drop] = False
        T = T[keep]
        used = sorted(set(int(v) for t in T for v in t))
        remap = {v: i for i, v in enumerate(used)}
        V = V[used]
        bset = {remap[v] for v in bset if v in remap}
        T = np.array([[remap[a], remap[b], remap[c]] for a, b, c in T])
        log("dropped_degenerate_ears", count=len(drop))
    return V, T, bset


# ---------------------------------------------------------------------------
# Stage 5: fairing, clearance floor, bulge


def csr_adjacency(n, T):
    nbrs = [set() for _ in range(n)]
    for a, b, c in T:
        nbrs[a].update((b, c))
        nbrs[b].update((a, c))
        nbrs[c].update((a, b))
    idx = np.concatenate([np.fromiter(s, int) for s in nbrs])
    ptr = np.concatenate([[0], np.cumsum([len(s) for s in nbrs])])
    return idx, ptr


def neighbor_average(X, idx, ptr):
    sums = np.add.reduceat(X[idx], ptr[:-1], axis=0)
    deg = (ptr[1:] - ptr[:-1])
    empty = deg == 0
    deg = np.maximum(deg, 1)
    avg = sums / deg[:, None]
    avg[empty] = X[empty]
    return avg


def taubin(X, interior, idx, ptr, pairs, lam=0.5, mu=-0.53):
    for _ in range(pairs):
        for f in (lam, mu):
            avg = neighbor_average(X, idx, ptr)
            X[interior] += f * (avg[interior] - X[interior])
    return X


def clearances(X, bvh):
    """BVH must be built in millimetre coordinates."""
    s = np.empty(len(X))
    nrm = np.empty((len(X), 3))
    near = np.empty((len(X), 3))
    for i, p in enumerate(X):
        co_, n_, fi_, d_ = bvh.find_nearest(Vector(p))
        q = np.array(co_)
        n = np.array(n_)
        near[i] = q
        nrm[i] = n
        d = np.linalg.norm(p - q)
        s[i] = d if (p - q) @ n >= 0 else -d
    return s, nrm, near


def boundary_geodesic(X, idx, ptr, boundary):
    n = len(X)
    dist = np.full(n, np.inf)
    h = []
    for b in boundary:
        dist[b] = 0.0
        heappush(h, (0.0, b))
    while h:
        d, v = heappop(h)
        if d > dist[v] + 1e-12:
            continue
        for w in idx[ptr[v]:ptr[v + 1]]:
            nd = d + np.linalg.norm(X[v] - X[w])
            if nd < dist[w]:
                dist[w] = nd
                heappush(h, (nd, int(w)))
    return dist


def smooth_scalar(vals, interior, idx, ptr, iters):
    v = vals.copy()
    for _ in range(iters):
        avg_all = np.add.reduceat(v[idx], ptr[:-1]) / np.maximum(ptr[1:] - ptr[:-1], 1)
        v[interior] = 0.5 * v[interior] + 0.5 * avg_all[interior]
    return v


def shape_surface(V, T, bset, bvh):
    n = len(V)
    interior = np.ones(n, dtype=bool)
    interior[list(bset)] = False
    idx, ptr = csr_adjacency(n, T)
    X = V.copy()

    b = boundary_geodesic(X, idx, ptr, bset)
    floor = 0.05 + FLOOR_BASE * smoothstep(b / FLOOR_RAMP)
    floor[~interior] = 0.0

    # envelope: fair with pinned boundary, then keep outside the clearance floor
    for rnd in range(5):
        taubin(X, interior, idx, ptr, pairs=6)
        s, nrm, near = clearances(X, bvh)
        low = interior & (s < floor)
        X[low] = near[low] + nrm[low] * floor[low][:, None]
        log("envelope", round=rnd, pushed=int(low.sum()),
            min_s=float(s[interior].min()), max_s=float(s[interior].max()))

    # low-frequency bulge along body-outward normals with medial-axis freeze
    s, nrm, near = clearances(X, bvh)
    target = floor.copy()
    for amp, ramp in BULGE_RAMPS:
        target += amp * smoothstep(b / ramp)
    target[~interior] = 0.0
    delta = np.maximum(0.0, target - s)
    delta[~interior] = 0.0
    delta = smooth_scalar(delta, interior, idx, ptr, 40)
    remaining = delta.copy()
    frozen = np.zeros(n, dtype=bool)
    step = 0.7
    for it in range(12):
        active = np.flatnonzero(interior & ~frozen & (remaining > 1e-4))
        if len(active) == 0:
            break
        moved = 0
        for i in active:
            p = X[i]
            co_, n_, fi_, d_ = bvh.find_nearest(Vector(p))
            q = np.array(co_)
            nn = np.array(n_)
            d0 = np.linalg.norm(p - q)
            if (p - q) @ nn < 0:
                frozen[i] = True
                continue
            mv = min(step, remaining[i])
            cand = p + nn * mv
            co2, n2, f2, d2 = bvh.find_nearest(Vector(cand))
            q2 = np.array(co2)
            s2 = np.linalg.norm(cand - q2)
            if (cand - q2) @ np.array(n2) < 0 or s2 < d0 + 0.4 * mv:
                frozen[i] = True    # crossing toward another body part: stop here
                continue
            X[i] = cand
            remaining[i] -= mv
            moved += 1
        log("bulge_step", it=it, moved=moved, frozen=int(frozen.sum()))
    # gentle final smoothing that cannot dip below the floor
    for _ in range(2):
        taubin(X, interior, idx, ptr, pairs=1, lam=0.2, mu=-0.21)
        s, nrm, near = clearances(X, bvh)
        low = interior & (s < floor)
        X[low] = near[low] + nrm[low] * floor[low][:, None]

    # --- final healing: alternate chord-sag lifting and fold relaxation ---
    epairs = defaultdict(list)
    for fi, t in enumerate(T):
        for a_, b_ in ((t[0], t[1]), (t[1], t[2]), (t[2], t[0])):
            epairs[(a_, b_) if a_ < b_ else (b_, a_)].append(fi)
    int_pairs = np.array([fs for fs in epairs.values() if len(fs) == 2])
    n_all0 = None
    # only faces the body-intersection gate actually tests: boundary-band
    # faces have pinned vertices (their chords legitimately hug the crease
    # the user drew through) and chasing them just churns fold seeds
    gate_face = np.array([all(interior[v] and b[v] > INTERIOR_BAND for v in t) for t in T])

    def sample_lift(iters, cap, req_scale, req_min, tag):
        """Vertex clearance alone cannot stop triangle chords sagging into
        convex body bumps between vertices: sample centroid and edge
        midpoints and push adjacent movable vertices out by the deficit,
        spread over the 1-ring so no single-vertex fold seeds form."""
        for it in range(iters):
            tv = X[T]
            samples = np.concatenate([tv.mean(axis=1),
                                      (tv[:, 0] + tv[:, 1]) * 0.5,
                                      (tv[:, 1] + tv[:, 2]) * 0.5,
                                      (tv[:, 2] + tv[:, 0]) * 0.5])
            s_smp, _, _ = clearances(samples, bvh)
            nf = len(T)
            floor_face = np.minimum(np.minimum(floor[T[:, 0]], floor[T[:, 1]]), floor[T[:, 2]])
            req = np.maximum(floor_face * req_scale, req_min)
            worst = s_smp.reshape(4, nf).min(axis=0)
            deficit = np.where(gate_face, np.maximum(0.0, req - worst), 0.0)
            bad_faces = np.flatnonzero(deficit > 1e-3)
            if len(bad_faces) == 0:
                return True
            push = np.zeros(n)
            for fi in bad_faces:
                for v in T[fi]:
                    if interior[v]:
                        push[v] = max(push[v], deficit[fi])
            for spread in (0.6, 0.6):
                nb_max = np.maximum.reduceat(push[idx], ptr[:-1])
                nb_max[ptr[1:] - ptr[:-1] == 0] = 0.0
                push = np.where(interior, np.maximum(push, spread * nb_max), 0.0)
            ids = np.flatnonzero(push > 0)
            if len(ids) == 0:
                return False
            _, n_v, _ = clearances(X[ids], bvh)
            X[ids] += n_v * np.minimum(push[ids], cap)[:, None]
            log(tag, it=it, faces=int(len(bad_faces)), max_deficit=float(deficit.max()))
        return False

    def fold_seeds():
        tv = X[T]
        fnl = np.cross(tv[:, 1] - tv[:, 0], tv[:, 2] - tv[:, 0])
        fnl = fnl / np.maximum(np.linalg.norm(fnl, axis=1, keepdims=True), 1e-30)
        dd = np.sum(fnl[int_pairs[:, 0]] * fnl[int_pairs[:, 1]], axis=1)
        bad_pairs = int_pairs[dd < -0.05]
        sd = set(int(v) for fp in bad_pairs for fi in fp for v in T[fi])
        return sd, int(np.sum(dd < -0.2))

    def fold_relax(rounds, tag):
        """Floor pushes along the opposing wall normals of a deep body
        crease zigzag the strip into genuine folds: relax fold
        neighbourhoods (2-ring) with a full Laplacian and re-enforce a
        softened floor along a SMOOTHED normal field so adjacent vertices
        move coherently."""
        nonlocal n_all0
        seeds, folds = fold_seeds()
        log(tag, round=-1, fold_edges=folds, seed_verts=len(seeds))
        if not folds:
            return 0
        if n_all0 is None:
            _, n_all0, _ = clearances(X, bvh)
            for _ in range(6):
                n_all0 = n_all0 + neighbor_average(n_all0, idx, ptr)
                n_all0 = n_all0 / np.maximum(np.linalg.norm(n_all0, axis=1, keepdims=True), 1e-30)
        soft = np.maximum(0.3, floor * 0.5)
        for rnd in range(rounds):
            ring = set(seeds)
            for _ in range(2):
                grow = set(ring)
                for v in ring:
                    grow.update(int(w) for w in idx[ptr[v]:ptr[v + 1]])
                ring = grow
            mov = np.array([v for v in ring if interior[v]], dtype=int)
            if len(mov) == 0:
                break
            for _ in range(4):
                X[mov] = neighbor_average(X, idx, ptr)[mov]
                s_m, _, _ = clearances(X[mov], bvh)
                low = np.flatnonzero(s_m < soft[mov])
                if len(low):
                    X[mov[low]] += n_all0[mov[low]] * ((soft[mov[low]] - s_m[low]) * 1.2)[:, None]
                s_m, n_m, q_m = clearances(X[mov], bvh)
                pen = np.flatnonzero(s_m < 0.05)
                if len(pen):
                    X[mov[pen]] = q_m[pen] + n_m[pen] * 0.15
            seeds, folds = fold_seeds()
            log(tag, round=rnd, fold_edges=folds, seed_verts=len(seeds))
            if folds == 0:
                break
        return folds

    sample_lift(10, 0.6, 0.75, 0.05, "face_lift")
    fold_relax(6, "fold_relax")
    # body-intersection gate has priority: run the lift to full convergence
    # (the wide spread turns the stubborn ridge crossing into a smooth mound
    # instead of a tent)
    sample_lift(24, 0.6, 0.75, 0.05, "heal_lift")
    _, folds = fold_seeds()
    log("residual_folds", fold_edges=folds)
    return X, b, interior


def gate_guided_heal(X, T, bset, b, interior, bvh, body_co, body_tris, idx, ptr, rounds=6):
    """Sampling-based lifting can miss a body vertex poking through a cloth
    triangle between sample points.  Use the acceptance test itself (exact
    tri-tri) to find offending pairs and push exactly those triangles out."""
    tri_interior = np.array([all((v not in bset) and b[v] > INTERIOR_BAND for v in t) for t in T])
    Ti = T[tri_interior]
    body_tris_t = [tuple(int(i) for i in t) for t in body_tris]
    for rnd in range(rounds):
        ci = fgv.cross_intersection_report(X, [tuple(int(v) for v in t) for t in Ti],
                                           body_co, body_tris_t)
        log("gate_heal", round=rnd, pairs=ci["intersection_pairs"])
        if ci["intersection_pairs"] == 0:
            return True
        push = np.zeros(len(X))
        for i, j in ci["intersection_pair_indices"]:
            for v in Ti[i]:
                if interior[v]:
                    push[v] = max(push[v], 0.45)
        nb_max = np.maximum.reduceat(push[idx], ptr[:-1])
        nb_max[ptr[1:] - ptr[:-1] == 0] = 0.0
        push = np.where(interior, np.maximum(push, 0.6 * nb_max), 0.0)
        ids = np.flatnonzero(push > 0)
        if len(ids) == 0:
            return False
        _, n_v, _ = clearances(X[ids], bvh)
        X[ids] += n_v * push[ids][:, None]
    return False


# ---------------------------------------------------------------------------
# Stage 6: validation


def validate(X, T, bset, b, curve_loops, body_co, body_tris, report):
    gates = {}
    topo = fgv.topology_report(len(X), [tuple(t) for t in T])
    gates["topology"] = {k: topo[k] for k in
                         ("vertices", "edges", "faces", "components", "boundary_loops",
                          "euler_characteristic", "genus", "nonmanifold_internal_edges",
                          "isolated_vertices", "pinched_vertices", "orientation_conflict_edges")}
    # altitude epsilon 1e-5 mm: the input curves themselves contain ~4e-5 mm
    # switchback spikes (audit: min non-adjacent segment distance), and a
    # triangle wedged inside such a spike is faithful boundary reproduction,
    # not a construction artifact.
    quality = fgv.triangle_quality_report(X, T, altitude_epsilon=1e-5)
    gates["degenerate_faces"] = quality["degenerate_faces"]
    gates["min_altitude_mm"] = quality["minimum_altitude"]

    si = fgv.self_intersection_report(X, [tuple(t) for t in T])
    gates["self_intersections"] = si["self_intersections"]
    gates["self_intersection_samples"] = si["sample_triangle_pairs"]

    out_loops = [X[np.array(loop)] for loop in topo["boundary_loop_vertex_indices"]]
    in_loops = [curve_loops[r] for r in ("waist", "leg_negx", "leg_posx")]
    bm = fgv.match_boundary_loops(out_loops, in_loops, tolerance=0.25)
    gates["boundary_match"] = {
        "bijection": bm["bijection"], "passed": bm["passed"],
        "max_certified_upper_mm": bm.get("maximum_certified_upper"),
        "max_sampled_lower_mm": bm.get("maximum_sampled_lower")}

    body_bvh = BVHTree.FromPolygons([tuple(v) for v in body_co],
                                    [tuple(t) for t in body_tris], all_triangles=True)
    s, nrm, near = clearances(X, body_bvh)
    interior_v = np.array([i for i in range(len(X)) if i not in bset and b[i] > INTERIOR_BAND])
    band_v = np.array([i for i in range(len(X)) if i not in bset and b[i] <= INTERIOR_BAND])
    gates["clearance_mm"] = {
        "interior_min": float(s[interior_v].min()) if len(interior_v) else None,
        "interior_median": float(np.median(s[interior_v])),
        "interior_p90": float(np.quantile(s[interior_v], 0.90)),
        "interior_max": float(s[interior_v].max()),
        "boundary_band_min": float(s[band_v].min()) if len(band_v) else None,
        "boundary_vertex_absmax": float(np.abs(s[sorted(bset)]).max()),
    }
    gates["inside_interior_vertices"] = int(np.sum(s[interior_v] < 0.0))

    # inverted faces: cloth normal vs nearest body normal
    tv = X[T]
    fn = np.cross(tv[:, 1] - tv[:, 0], tv[:, 2] - tv[:, 0])
    fl = np.linalg.norm(fn, axis=1, keepdims=True)
    fn = fn / np.maximum(fl, 1e-30)
    cents = tv.mean(axis=1)
    sc, ncb, _ = clearances(cents, body_bvh)
    dots = np.sum(fn * ncb, axis=1)
    if np.median(dots) < 0:
        T[:] = T[:, ::-1]
        fn *= -1.0
        dots *= -1.0
    # normals of sub-micron slivers (forced by switchback spikes in the input
    # curves) are numerical noise; judge orientation on real-area faces and
    # report micro-sliver count separately
    areas = 0.5 * fl[:, 0]
    real = areas >= 1e-4
    bd_neg = np.flatnonzero((dots <= 0.0) & real)
    gates["body_dot_negative_faces"] = int(len(bd_neg))
    gates["body_dot_negative_centroids_m"] = (cents[bd_neg] / MM).round(4).tolist()
    gates["face_body_dot_min_real_area"] = float(dots[real].min())

    # true inversion/fold test: winding is globally consistent (see topology
    # orientation_conflict_edges) and a genuine fold/tent shows as adjacent
    # faces with opposing normals.  Crease-wall faces legitimately disagree
    # with the nearest-body normal without being folded.
    epairs = defaultdict(list)
    for fi, t in enumerate(T):
        for a_, b_ in ((t[0], t[1]), (t[1], t[2]), (t[2], t[0])):
            epairs[(a_, b_) if a_ < b_ else (b_, a_)].append(fi)
    min_dot, folds, soft = 1.0, 0, 0
    for fs in epairs.values():
        if len(fs) == 2 and real[fs[0]] and real[fs[1]]:
            d = float(np.dot(fn[fs[0]], fn[fs[1]]))
            min_dot = min(min_dot, d)
            if d < -0.2:
                folds += 1
            if d < 0.0:
                soft += 1
    gates["fold_edges"] = folds
    gates["adjacent_normal_negative_dot_edges"] = soft
    gates["adjacent_normal_min_dot"] = min_dot

    # cross intersection with the body, interior triangles only (boundary sits
    # ON the skin by specification, so its chords graze the surface trivially)
    tri_interior = np.array([all((v not in bset) and b[v] > INTERIOR_BAND for v in t) for t in T])
    Ti = T[tri_interior]
    ci = fgv.cross_intersection_report(X, [tuple(t) for t in Ti], body_co, [tuple(t) for t in body_tris])
    gates["interior_body_intersections"] = ci["intersection_pairs"]
    ca = fgv.cross_intersection_report(X, [tuple(t) for t in T[~tri_interior]], body_co,
                                       [tuple(t) for t in body_tris])
    gates["boundary_band_body_contacts"] = ca["intersection_pairs"]

    gates["bulge_mm"] = {
        "median": float(np.median(s[interior_v])),
        "p90": float(np.quantile(s[interior_v], 0.90)),
        "max": float(s[interior_v].max()),
    }
    report["gates"] = gates

    core_pass = (
        gates["topology"]["components"] == 1
        and gates["topology"]["boundary_loops"] == 3
        and gates["topology"]["euler_characteristic"] == -1
        and gates["topology"]["genus"] == 0
        and gates["topology"]["nonmanifold_internal_edges"] == 0
        and gates["topology"]["isolated_vertices"] == 0
        and gates["degenerate_faces"] == 0
        and gates["self_intersections"] == 0
        and gates["boundary_match"]["passed"]
        and gates["inside_interior_vertices"] == 0
        and gates["interior_body_intersections"] == 0
        and gates["topology"]["orientation_conflict_edges"] == 0
    )
    hard_pass = core_pass and gates["fold_edges"] == 0
    report["core_pass"] = bool(core_pass)
    report["hard_pass"] = bool(hard_pass)
    log("validate", hard_pass=hard_pass, core_pass=core_pass, **{k: v for k, v in gates.items()
        if k in ("self_intersections", "degenerate_faces", "inside_interior_vertices",
                 "interior_body_intersections", "fold_edges", "body_dot_negative_faces",
                 "adjacent_normal_min_dot")})
    return core_pass, s


def repair_self_intersections(X, T, bset, bvh, envelope_X, max_rounds=3):
    """Local fallback: pull offending vertices back toward the validated envelope."""
    for rnd in range(max_rounds):
        si = fgv.self_intersection_report(X, [tuple(t) for t in T])
        if si["self_intersections"] == 0:
            return True
        bad = set()
        for i, j in si["sample_triangle_pairs"]:
            bad.update(T[i])
            bad.update(T[j])
        bad = [v for v in bad if v not in bset]
        t = 0.5 if rnd == 0 else 0.0
        for v in bad:
            X[v] = envelope_X[v] * (1 - t) + X[v] * t if rnd == 0 else envelope_X[v]
        log("repair", round=rnd, pairs=si["self_intersections"], reverted=len(bad))
    si = fgv.self_intersection_report(X, [tuple(t) for t in T])
    return si["self_intersections"] == 0


# ---------------------------------------------------------------------------
# Stage 7: scene output


def build_object(X, T):
    mesh = bpy.data.meshes.new("FundoshiSurface_Rebuild")
    mesh.from_pydata([tuple(v) for v in (X / MM)], [], [tuple(int(i) for i in t) for t in T])
    mesh.update()
    obj = bpy.data.objects.new("FundoshiSurface_Rebuild", mesh)
    bpy.context.scene.collection.objects.link(obj)
    obj.color = (0.92, 0.42, 0.10, 1.0)
    for p in mesh.polygons:
        p.use_smooth = True
    # outward thickness: the validated base surface stays the INNER face, so
    # every non-penetration guarantee is preserved; modifier stays live for
    # viewport tuning
    mod = obj.modifiers.new("Solidify", "SOLIDIFY")
    mod.thickness = SOLIDIFY_MM / 1000.0
    mod.offset = 1.0
    mod.use_even_offset = True
    mod.use_quality_normals = True
    mod.use_rim = True
    # clamp the even-thickness amplification at sharp creases so residual
    # fold spots do not shoot spikes into the (soon real) outer shell
    mod.thickness_clamp = 2.0
    if APPLY_SOLIDIFY:
        bpy.context.view_layer.objects.active = obj
        obj.select_set(True)
        bpy.ops.object.modifier_apply(modifier=mod.name)
        log("solidify_applied", verts=len(obj.data.vertices), faces=len(obj.data.polygons))
    return obj


def hide_legacy():
    keep_visible = {"SumoRetopo", "FundoshiSurface_Rebuild",
                    "EdgeTexel0", "EdgeTexel1", "EdgeTexel2"}
    for o in bpy.data.objects:
        if o.name in keep_visible:
            o.hide_viewport = False
            o.hide_render = False
        else:
            o.hide_viewport = True
            o.hide_render = True


def render_views(obj):
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.light = "STUDIO"
    sc.display.shading.color_type = "OBJECT"
    sc.display.render_aa = "8"
    sc.render.resolution_x = sc.render.resolution_y = 1400
    body = bpy.data.objects["SumoRetopo"]
    body.color = (0.82, 0.80, 0.78, 1.0)

    bb = np.array([obj.matrix_world @ Vector(c) for c in obj.bound_box])
    center = Vector(((bb.min(axis=0) + bb.max(axis=0)) / 2.0))
    extent = float((bb.max(axis=0) - bb.min(axis=0)).max())

    cam_data = bpy.data.cameras.new("V68Cam")
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = extent * 1.35
    cam_data.clip_end = 100.0
    cam = bpy.data.objects.new("V68Cam", cam_data)
    sc.collection.objects.link(cam)
    sc.camera = cam

    views = {
        "front": (Vector((0, -3, 0)), (math.pi / 2, 0, 0)),
        "back": (Vector((0, 3, 0)), (math.pi / 2, 0, math.pi)),
        "side": (Vector((3, 0, 0)), (math.pi / 2, 0, math.pi / 2)),
    }
    for name, (off, rot) in views.items():
        cam.location = center + off
        cam.rotation_euler = rot
        sc.render.filepath = os.path.join(OUT_DIR, f"{name}.png")
        bpy.ops.render.render(write_still=True)
        log("render", view=name)

    wire = obj.copy()
    wire.data = obj.data.copy()
    wire.name = "V68WireTmp"
    sc.collection.objects.link(wire)
    mod = wire.modifiers.new("Wire", "WIREFRAME")
    mod.thickness = 0.0008
    wire.color = (0.05, 0.05, 0.05, 1.0)
    cam.location = center + Vector((0, -3, 0))
    cam.rotation_euler = (math.pi / 2, 0, 0)
    sc.render.filepath = os.path.join(OUT_DIR, "wireframe.png")
    bpy.ops.render.render(write_still=True)
    log("render", view="wireframe")
    bpy.data.objects.remove(wire)
    bpy.data.objects.remove(cam)
    bpy.data.cameras.remove(cam_data)


# ---------------------------------------------------------------------------


def main():
    report = {"method": "A (flatten + constrained Delaunay, no zipper)", "attempts": []}
    try:
        bpy.ops.wm.open_mainfile(filepath=SRC_BLEND)
        log("open", file=SRC_BLEND)

        body_co, body_faces, body_tris, tri_owner, bvh, curve_loops, roles = audit(report)

        pco, pfaces, bloops, _ = extract_patch(body_co, body_faces, bvh, tri_owner, curve_loops)
        uv, ptris, face_tris, waist_loop, leg_loops = flatten_patch(pco, pfaces, bloops, curve_loops)

        bnd_uv, bnd_3d, chains, st_uv, st_3d, polys_uv = build_cdt_input(
            curve_loops, pco, pfaces, face_tris, uv)
        V, T, bset = run_cdt(bnd_uv, bnd_3d, chains, st_uv, st_3d, polys_uv)
        V, T, bset = drop_degenerate_ears(V, T, bset)

        proxy_co = build_proxy(body_co, body_faces, body_tris, bvh)
        bvh_proxy = BVHTree.FromPolygons([tuple(v) for v in proxy_co],
                                         [tuple(int(i) for i in t) for t in body_tris],
                                         all_triangles=True)
        X, b, interior = shape_surface(V, T, bset, bvh_proxy)
        idx_a, ptr_a = csr_adjacency(len(X), T)
        true_body_vertex_floor(X, interior, bvh)
        gate_guided_heal(X, T, bset, b, interior, bvh, body_co, body_tris, idx_a, ptr_a, rounds=10)
        envelope_X = X.copy()   # bulge already applied; used as local repair target

        hard_pass, s = validate(X, T, bset, b, curve_loops, body_co, body_tris, report)
        if not hard_pass and report["gates"]["self_intersections"] > 0:
            # revert offenders toward pre-bulge/envelope positions and re-gate
            pre = V.copy()
            ok = repair_self_intersections(X, T, bset, bvh, pre)
            hard_pass, s = validate(X, T, bset, b, curve_loops, body_co, body_tris, report)

        obj = build_object(X, T)
        hide_legacy()
        bpy.ops.wm.save_as_mainfile(filepath=OUT_BLEND)
        log("saved", blend=OUT_BLEND)

        if hard_pass:
            render_views(obj)
            if report.get("hard_pass"):
                report["status"] = "PASSED"
            else:
                report["status"] = "PASSED_CORE_WITH_SMOOTHNESS_RESIDUAL"
        else:
            report["status"] = "FAILED_GATES"
        report["output_blend"] = OUT_BLEND
        report["elapsed_s"] = round(time.time() - T0, 1)
    except Exception:
        report["status"] = "EXCEPTION"
        report["traceback"] = traceback.format_exc()
        log("exception", tb=report["traceback"])
    finally:
        with open(os.path.join(OUT_DIR, "report.json"), "w", encoding="utf-8") as fh:
            json.dump(report, fh, indent=2, default=str)
        log("done", status=report.get("status"))


main()
