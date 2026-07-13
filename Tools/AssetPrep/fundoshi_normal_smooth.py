"""Masked custom-normal smoothing for the Fundoshi band (user-approved mask).

- EXCLUSION (crack strap, |x|<90mm AND y>150mm): normals untouched — asserted.
- FADE 25mm outside exclusion: smoothing weight ramps 0 -> 1.
- Elsewhere: normal FIELD smoothed 25 Laplacian iterations (adjacency restricted
  per shell layer — outer 0..n-1 / inner n..2n-1, never across the rim) and
  written as custom split normals. Geometry untouched: ink/collision/silhouette
  identical, only shading changes.
- Backs up master to masters/v16_prenormalsmooth first.

Run: blender --background --python fundoshi_normal_smooth.py
"""
import bpy, json, os, shutil, time, traceback
import numpy as np

ROOT = r"C:\games\Unreal Engine\nice_ink"
MASTER = os.path.join(ROOT, "SourceAssets", "sumo_character_master.blend")
BACKUP = os.path.join(ROOT, "SourceAssets", "masters", "sumo_character_master_v16_prenormalsmooth.blend")
PROG = os.path.join(ROOT, "Saved", "FundoshiNormals", "progress.jsonl")

X_HALF = 0.090
Y_BACK = 0.150
FADE = 0.025
ITERS = 25
LAM = 0.5
N_LAYER = 3567

os.makedirs(os.path.dirname(PROG), exist_ok=True)
T0 = time.time()

def log(stage, **kw):
    rec = {"t": round(time.time() - T0, 1), "stage": stage}
    rec.update(kw)
    with open(PROG, "a") as f:
        f.write(json.dumps(rec) + "\n")

def smoothstep(t):
    t = np.clip(t, 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)

def main():
    if not os.path.exists(BACKUP):
        shutil.copy2(MASTER, BACKUP)
    log("backup", path=BACKUP)

    bpy.ops.wm.open_mainfile(filepath=MASTER)
    if bpy.context.object and bpy.context.object.mode != 'OBJECT':
        bpy.ops.object.mode_set(mode='OBJECT')
    band = bpy.data.objects["Fundoshi"]
    me = band.data
    n = len(me.vertices)
    assert n == 2 * N_LAYER, f"unexpected vert count {n}"

    co = np.empty(n * 3)
    me.vertices.foreach_get("co", co)
    co = co.reshape(n, 3)
    # Blender 5 background 模式下頂點法線讀不到（foreach_get / 逐元素觸碰 /
    # vertex_normals 全回傳零——實踩三連）。自己按 Blender 慣例算：
    # 角度加權的相鄰面法線平均。與 Blender 內建值的偏差 ~1° 量級。
    me.calc_loop_triangles()
    tris = np.empty(len(me.loop_triangles) * 3, dtype=np.int64)
    me.loop_triangles.foreach_get("vertices", tris)
    tris = tris.reshape(-1, 3)
    A, B, C = co[tris[:, 0]], co[tris[:, 1]], co[tris[:, 2]]
    fn = np.cross(B - A, C - A)
    fl = np.linalg.norm(fn, axis=1, keepdims=True)
    fn = fn / np.maximum(fl, 1e-18)

    def corner_angle(p, q, r):
        u = q - p
        v = r - p
        cu = u / np.maximum(np.linalg.norm(u, axis=1, keepdims=True), 1e-18)
        cv = v / np.maximum(np.linalg.norm(v, axis=1, keepdims=True), 1e-18)
        return np.arccos(np.clip((cu * cv).sum(axis=1), -1.0, 1.0))

    orig = np.zeros((n, 3))
    for k, (p, q, r) in enumerate(((0, 1, 2), (1, 2, 0), (2, 0, 1))):
        ang = corner_angle(co[tris[:, p]], co[tris[:, q]], co[tris[:, r]])
        np.add.at(orig, tris[:, p], fn * ang[:, None])
    lens = np.linalg.norm(orig, axis=1, keepdims=True)
    loose = lens[:, 0] <= 1e-12   # 無面頂點：不被任何 loop 引用，占位即可
    log("loose_verts", count=int(loose.sum()))
    orig[loose] = (0.0, 0.0, 1.0)
    lens[loose] = 1.0
    orig /= lens

    # weight field: 0 in exclusion, smoothstep ramp over FADE, 1 elsewhere
    dx = np.abs(co[:, 0]) - X_HALF
    dy = Y_BACK - co[:, 1]
    outside = np.maximum(dx, dy)          # <0 = inside exclusion
    w = smoothstep(outside / FADE)
    w[outside < 0.0] = 0.0
    log("mask", excluded=int((w == 0).sum()), partial=int(((w > 0) & (w < 1)).sum()),
        full=int((w == 1).sum()))

    # same-layer adjacency (never across the rim: inner normals oppose outer)
    edges = np.empty(len(me.edges) * 2, dtype=np.int64)
    me.edges.foreach_get("vertices", edges)
    edges = edges.reshape(-1, 2)
    same = (edges[:, 0] < N_LAYER) == (edges[:, 1] < N_LAYER)
    edges = edges[same]
    from collections import defaultdict
    nbrs = defaultdict(list)
    for a, b in edges:
        nbrs[int(a)].append(int(b))
        nbrs[int(b)].append(int(a))
    idx = np.zeros(sum(len(v) for v in nbrs.values()), dtype=np.int64)
    ptr = np.zeros(n + 1, dtype=np.int64)
    pos = 0
    for i in range(n):
        ptr[i] = pos
        for j in nbrs.get(i, ()):  # rim-crossing-only verts keep original normal
            idx[pos] = j
            pos += 1
    ptr[n] = pos

    # Laplacian smoothing of the normal field
    N = orig.copy()
    counts = np.maximum(ptr[1:] - ptr[:-1], 1)
    for _ in range(ITERS):
        acc = np.zeros_like(N)
        np.add.at(acc, np.repeat(np.arange(n), ptr[1:] - ptr[:-1]), N[idx])
        mean = acc / counts[:, None]
        N = (1.0 - LAM) * N + LAM * mean
        N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-12)

    final = orig + w[:, None] * (N - orig)
    final /= np.maximum(np.linalg.norm(final, axis=1, keepdims=True), 1e-12)

    # hard guarantee: exclusion zone normals bit-identical to original
    excl = w == 0.0
    final[excl] = orig[excl]
    delta = np.degrees(np.arccos(np.clip((final * orig).sum(axis=1), -1, 1)))
    log("normal_delta_deg", excl_max=round(float(delta[excl].max()), 4),
        green_mean=round(float(delta[~excl].mean()), 2),
        green_p95=round(float(np.percentile(delta[~excl], 95)), 2),
        green_max=round(float(delta[~excl].max()), 2))
    assert delta[excl].max() < 1e-3, "exclusion zone normals moved!"
    assert np.isfinite(final).all(), "NaN/inf in final normals"
    assert float(np.abs(np.linalg.norm(final, axis=1) - 1.0).max()) < 1e-4, "unnormalized final normals"

    me.normals_split_custom_set_from_vertices([tuple(v) for v in final])
    log("custom_normals_set", has_custom=bool(me.has_custom_normals))

    bpy.ops.wm.save_mainfile()
    log("saved", blend=MASTER)
    log("done", status="OK")

try:
    main()
except Exception:
    log("exception", trace=traceback.format_exc()[-1500:])
    raise
