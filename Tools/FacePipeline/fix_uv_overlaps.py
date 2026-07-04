"""
Detect and fix overlapping UV islands in the body UVMap (tattoo-RT premise:
paint must land on exactly one surface region).

Run:  blender --background <character.blend> --python fix_uv_overlaps.py

Islands are moved by TRANSLATION ONLY (texel density preserved), which is
safe for the current material: skin tiles are phase-invariant, zone tint
uses Generated coords, the face texture uses FaceUV. Self-overlapping
islands (internal folds) cannot be fixed by translation and are reported
for manual re-unwrap.
"""
import bpy
import numpy as np

MESH_NAME = "PlusSize_Male_Body_01"
UV_NAME = "UVMap"
SIZE = 1024          # raster resolution for overlap analysis
MARGIN = 4           # px padding kept around islands when repacking
MIN_OVERLAP_PX = 20  # ignore tiny raster-noise contacts


def fill_polygon(mask, pts):
    """Conservative half-open scanline fill (no shared-edge double count)."""
    x0 = max(int(np.floor(pts[:, 0].min())), 0)
    x1 = min(int(np.ceil(pts[:, 0].max())), SIZE - 1)
    y0 = max(int(np.floor(pts[:, 1].min())), 0)
    y1 = min(int(np.ceil(pts[:, 1].max())), SIZE - 1)
    n = len(pts)
    for y in range(y0, y1 + 1):
        yc = y + 0.5
        xs = []
        for k in range(n):
            xa, ya = pts[k]
            xb, yb = pts[(k + 1) % n]
            if (ya <= yc < yb) or (yb <= yc < ya):
                t = (yc - ya) / (yb - ya)
                xs.append(xa + t * (xb - xa))
        xs.sort()
        for j in range(0, len(xs) - 1, 2):
            a = int(np.ceil(xs[j] - 0.5))
            b = int(np.floor(xs[j + 1] - 0.5))
            if b >= a:
                mask[y, max(a, 0):min(b, SIZE - 1) + 1] = True


def dilate(mask, it):
    m = mask.copy()
    for _ in range(it):
        p = np.pad(m, 1)
        m = (p[1:-1, 1:-1] | p[:-2, 1:-1] | p[2:, 1:-1] | p[1:-1, :-2]
             | p[1:-1, 2:] | p[:-2, :-2] | p[:-2, 2:] | p[2:, :-2] | p[2:, 2:])
    return m


def erode(mask, it):
    return ~dilate(~mask, it)


def main():
    obj = bpy.data.objects[MESH_NAME]
    me = obj.data
    n_loops = len(me.loops)
    buf = np.zeros(n_loops * 2, dtype=np.float32)
    me.attributes[UV_NAME].data.foreach_get('vector', buf)
    uvs = buf.reshape(-1, 2).astype(np.float64)
    corner_vert = np.zeros(n_loops, dtype=np.int64)
    me.attributes['.corner_vert'].data.foreach_get('value', corner_vert)

    polys = [(p.index, list(range(p.loop_start, p.loop_start + p.loop_total)))
             for p in me.polygons]

    # ---- island detection: union-find over polys sharing a mesh edge whose
    # UVs agree on both sides
    parent = list(range(len(polys)))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[rb] = ra

    edge_map = {}
    for pi, loops in polys:
        n = len(loops)
        for k in range(n):
            va, vb = corner_vert[loops[k]], corner_vert[loops[(k + 1) % n]]
            uva, uvb = uvs[loops[k]], uvs[loops[(k + 1) % n]]
            key = (min(va, vb), max(va, vb))
            rec = (pi, (uva, uvb) if va < vb else (uvb, uva))
            if key in edge_map:
                qi, (qa, qb) = edge_map[key]
                if (np.linalg.norm(qa - rec[1][0]) < 1e-5
                        and np.linalg.norm(qb - rec[1][1]) < 1e-5):
                    union(pi, qi)
            else:
                edge_map[key] = rec

    islands = {}
    for pi, loops in polys:
        islands.setdefault(find(pi), []).append(pi)
    poly_loops = dict(polys)
    print(f"islands: {len(islands)}")

    # ---- rasterize each island + self-overlap check
    def to_px(uv):
        return np.stack([uv[:, 0] * SIZE, (1.0 - uv[:, 1]) * SIZE], axis=1)

    island_masks = {}
    for root, plist in islands.items():
        m = np.zeros((SIZE, SIZE), dtype=bool)
        area_sum = 0
        for pi in plist:
            pts = to_px(uvs[poly_loops[pi]])
            pm = np.zeros((SIZE, SIZE), dtype=bool)
            fill_polygon(pm, pts)
            area_sum += pm.sum()
            m |= pm
        island_masks[root] = m
        if area_sum > m.sum() * 1.15 and area_sum - m.sum() > MIN_OVERLAP_PX:
            print(f"  WARNING: island {root} self-overlaps "
                  f"({area_sum - m.sum()}px folded) - needs manual re-unwrap")

    # ---- pairwise overlaps
    roots = sorted(islands, key=lambda r: island_masks[r].sum())
    moved = 0
    for i, ra in enumerate(roots):
        for rb in roots[i + 1:]:
            inter = erode(island_masks[ra], 1) & erode(island_masks[rb], 1)
            if inter.sum() < MIN_OVERLAP_PX:
                continue
            # move the smaller island (ra, since roots sorted by area)
            print(f"overlap {inter.sum()}px between islands {ra}({island_masks[ra].sum()}px) "
                  f"and {rb}({island_masks[rb].sum()}px) -> moving {ra}")
            occupancy = np.zeros((SIZE, SIZE), dtype=bool)
            for r, m in island_masks.items():
                if r != ra:
                    occupancy |= m
            occupancy = dilate(occupancy, MARGIN)
            mask = island_masks[ra]
            ys, xs = np.where(mask)
            bx0, bx1, by0, by1 = xs.min(), xs.max(), ys.min(), ys.max()
            tile = dilate(mask, MARGIN)[by0 - MARGIN:by1 + MARGIN + 1,
                                        bx0 - MARGIN:bx1 + MARGIN + 1]
            th, tw = tile.shape
            best = None
            cands = []
            for oy in range(0, SIZE - th, 8):
                for ox in range(0, SIZE - tw, 8):
                    cands.append((abs(ox - (bx0 - MARGIN)) + abs(oy - (by0 - MARGIN)), ox, oy))
            cands.sort()
            for _, ox, oy in cands:
                if not (occupancy[oy:oy + th, ox:ox + tw] & tile).any():
                    best = (ox, oy)
                    break
            if best is None:
                print("  ERROR: no free space found")
                continue
            dx_px = best[0] - (bx0 - MARGIN)
            dy_px = best[1] - (by0 - MARGIN)
            du, dv = dx_px / SIZE, -dy_px / SIZE
            for pi in islands[ra]:
                for li in poly_loops[pi]:
                    uvs[li] += (du, dv)
            new_mask = np.zeros((SIZE, SIZE), dtype=bool)
            for pi in islands[ra]:
                fill_polygon(new_mask, to_px(uvs[poly_loops[pi]]))
            island_masks[ra] = new_mask
            moved += 1
            print(f"  moved by ({du:+.3f}, {dv:+.3f}) UV")

    # ---- verify
    total = np.zeros((SIZE, SIZE), dtype=np.int32)
    for m in island_masks.values():
        total += erode(m, 1).astype(np.int32)
    remaining = int((total > 1).sum())
    print(f"moved {moved} islands; remaining inter-island overlap: {remaining}px")

    if moved:
        me.attributes[UV_NAME].data.foreach_set('vector', uvs.astype(np.float32).ravel())
        bpy.ops.wm.save_mainfile()
        print(f"Saved: {bpy.data.filepath}")
    else:
        print("nothing moved, not saving")


if __name__ == "__main__":
    main()
