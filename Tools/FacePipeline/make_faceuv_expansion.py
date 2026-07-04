"""
Offline: derive the runtime constants for the expanded FaceUV island.

Input:  faceuv_expansion_src.json  (from prepare_faceuv_char13.py)
        faceuv_mask.png            (OLD oval - defines the frozen feature layout)

Output: faceuv_mask_coverage.png   (new island coverage: alpha/fades territory)
        faceuv_expansion_data.json (runtime constants:
          new_targets   : W-mapped 64 TPS boundary targets (px)
          anchor_old_px : interior anchor positions in OLD texture space; the
                          runtime evaluates the OLD inverse TPS there to get
                          per-player selfie sources
          anchor_new_px : the same anchors W-mapped to NEW texture space
          old_targets   : the old 64 targets (so runtime needs no old mask))

W is the exact old-UV -> new-UV map defined per corner of the 62 core polys
(barycentric inside, nearest-triangle affine extrapolation outside). Feature
content lands on identical mesh surface points as with the old island.
"""
import json
import cv2
import numpy as np
from pathlib import Path

PIPE = Path(r"C:\games\Unreal Engine\nice_ink_face_pipeline")
SRC_JSON = PIPE / "faceuv_expansion_src.json"
OLD_MASK = PIPE / "faceuv_mask.png"
OUT_MASK = PIPE / "faceuv_mask_coverage.png"
OUT_JSON = PIPE / "faceuv_expansion_data.json"
DEBUG_PNG = PIPE / "debug_expansion_layout.png"

TEX = 2048
N_PTS = 64
FACEUV_Y_OFFSET = 30
ANCHOR_ERODE = 60        # keep anchors this far inside the old oval
ANCHOR_STEP = 100        # anchor grid spacing (px)


def uv_to_px(uv):
    uv = np.asarray(uv, dtype=np.float64)
    return np.stack([uv[..., 0] * TEX, (1.0 - uv[..., 1]) * TEX], axis=-1)


# ---- old-target reproduction (identical logic to the runtime pipeline) ----

def shoelace_area(c):
    xs, ys = c[:, 0], c[:, 1]
    js = np.arange(1, len(c) + 1) % len(c)
    return np.sum(xs * ys[js] - xs[js] * ys)


def resample_contour(contour, n_pts):
    top_idx = np.argmin(contour[:, 1])
    contour = np.roll(contour, -top_idx, axis=0)
    closed = np.vstack([contour, contour[0:1]])
    seg = np.linalg.norm(np.diff(closed, axis=0), axis=1)
    cum = np.concatenate([[0], np.cumsum(seg)])
    dists = np.linspace(0, cum[-1], n_pts, endpoint=False)
    out = np.zeros((n_pts, 2))
    for i, d in enumerate(dists):
        idx = min(np.searchsorted(cum, d, side="right") - 1, len(contour) - 1)
        s = cum[idx + 1] - cum[idx]
        t = 0 if s < 1e-8 else (d - cum[idx]) / s
        out[i] = closed[idx] * (1 - t) + closed[idx + 1] * t
    return out


def old_targets_from_mask():
    mask = cv2.imread(str(OLD_MASK), cv2.IMREAD_GRAYSCALE)
    _, binary = cv2.threshold(mask, 127, 255, cv2.THRESH_BINARY)
    n, labels, stats, _ = cv2.connectedComponentsWithStats(binary)
    if n > 2:
        largest = 1 + np.argmax(stats[1:, cv2.CC_STAT_AREA])
        binary = np.where(labels == largest, np.uint8(255), np.uint8(0))
    cs, _ = cv2.findContours(binary, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_NONE)
    contour = max(cs, key=cv2.contourArea).reshape(-1, 2).astype(np.float64)
    if shoelace_area(contour) < 0:
        contour = contour[::-1].copy()
    pts = resample_contour(contour, N_PTS)
    pts[:, 1] += FACEUV_Y_OFFSET
    return pts, binary


# ---- W mapping from core-poly corner correspondence ------------------------

class WMap:
    def __init__(self, core_polys):
        tris_old, tris_new = [], []
        for pd in core_polys:
            o = uv_to_px(np.array(pd['old_uvs']))
            n = uv_to_px(np.array(pd['new_uvs']))
            for tri in ([0, 1, 2], [0, 2, 3]) if len(o) == 4 else ([list(range(len(o)))[:3]]):
                tris_old.append(o[tri])
                tris_new.append(n[tri])
        self.to_ = np.array(tris_old)   # (T,3,2)
        self.tn_ = np.array(tris_new)
        self.cent = self.to_.mean(axis=1)

    def _bary(self, tri, p):
        a, b, c = tri
        m = np.array([[b[0] - a[0], c[0] - a[0]], [b[1] - a[1], c[1] - a[1]]])
        try:
            w = np.linalg.solve(m, p - a)
        except np.linalg.LinAlgError:
            return None
        return np.array([1 - w[0] - w[1], w[0], w[1]])

    def map_point(self, p):
        # containing triangle first
        for i in range(len(self.to_)):
            bc = self._bary(self.to_[i], p)
            if bc is not None and (bc >= -1e-9).all():
                return self.tn_[i].T @ bc
        # nearest triangle, affine extrapolation
        i = int(np.argmin(np.linalg.norm(self.cent - p, axis=1)))
        bc = self._bary(self.to_[i], p)
        return self.tn_[i].T @ bc

    def map_points(self, pts):
        return np.array([self.map_point(p) for p in pts])


def main():
    with open(SRC_JSON) as f:
        src = json.load(f)

    # coverage mask from the 90 projected polys (only those - body faces whose
    # FaceUV is a UVMap copy must not leak in)
    cov = np.zeros((TEX, TEX), dtype=np.uint8)
    for pd in src['coverage_polys']:
        px = uv_to_px(np.array(pd['new_uvs'])).astype(np.int32)
        cv2.fillPoly(cov, [px], 255)
    cov = cv2.morphologyEx(cov, cv2.MORPH_CLOSE, np.ones((5, 5), np.uint8))
    cv2.imwrite(str(OUT_MASK), cov)

    old_targets, old_binary = old_targets_from_mask()
    w = WMap(src['core_polys'])
    new_targets = w.map_points(old_targets)

    # interior anchors on a grid well inside the old oval
    inner = cv2.erode(old_binary, np.ones((3, 3), np.uint8), iterations=ANCHOR_ERODE)
    ys, xs = np.mgrid[0:TEX:ANCHOR_STEP, 0:TEX:ANCHOR_STEP]
    grid = np.stack([xs.ravel(), ys.ravel()], axis=1).astype(np.float64)
    keep = [p for p in grid if inner[int(p[1]), int(p[0])] > 0]
    anchor_old = np.array(keep)
    anchor_new = w.map_points(anchor_old)
    print(f"targets: 64, interior anchors: {len(anchor_old)}")

    with open(OUT_JSON, 'w') as f:
        json.dump({
            'old_targets': old_targets.tolist(),
            'new_targets': new_targets.tolist(),
            'anchor_old_px': anchor_old.tolist(),
            'anchor_new_px': anchor_new.tolist(),
        }, f)
    print(f"-> {OUT_JSON}")
    print(f"-> {OUT_MASK} (coverage {np.count_nonzero(cov)} px, "
          f"{100 * np.count_nonzero(cov) / TEX ** 2:.1f}%)")

    # debug: coverage + old oval (W-mapped) + targets + anchors
    dbg = cv2.cvtColor(cov, cv2.COLOR_GRAY2BGR)
    dbg[old_binary > 0] = (dbg[old_binary > 0] * 0.7 + np.array((60, 30, 30)) * 0.3).astype(np.uint8)
    for p in new_targets:
        cv2.circle(dbg, (int(p[0]), int(p[1])), 6, (0, 0, 255), -1)
    for p in anchor_new:
        cv2.circle(dbg, (int(p[0]), int(p[1])), 5, (0, 255, 0), -1)
    cv2.imwrite(str(DEBUG_PNG), dbg)
    print(f"-> {DEBUG_PNG}")


if __name__ == "__main__":
    main()
