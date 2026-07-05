"""Bake the per-player no-draw eye mask from FaceUV space into ink-atlas (UV0) space.

Input : eye_mask.png (2048^2, FaceUV image space; white = eye opening)
Output: eye_mask_ink.png (1024^2, UV0 atlas image space; WHITE = ink allowed,
        BLACK = eye opening). M_InkBodyChar samples it with UV0 and multiplies
        it into the marker/tattoo premultiplied color+alpha - masking tape:
        strokes crossing the eye simply deposit nothing there.

Per triangle of data/char17_ink_uv_map.json (UVMap<->FaceUV, Blender V-up):
affine-warp the FaceUV eye image into the UV0 raster, clipped to the
triangle. 158 tris, trivial cost.

Usage: python bake_eye_ink_mask.py <eye_mask.png> <out.png> [size=1024]
"""
import sys
import json
import cv2
import numpy as np
from pathlib import Path

BASE = Path(__file__).resolve().parent
UV_MAP_JSON = BASE / "data" / "char17_ink_uv_map.json"


def bake(eye_mask_path, out_path, size=1024):
    eye = cv2.imread(str(eye_mask_path), cv2.IMREAD_GRAYSCALE)
    if eye is None:
        raise SystemExit(f"cannot read {eye_mask_path}")
    src_size = eye.shape[0]
    with open(UV_MAP_JSON, encoding="utf-8") as f:
        table = json.load(f)

    acc = np.zeros((size, size), np.uint8)
    if eye.max() > 0:                      # all-black mask -> pure white output
        for row in table["tris"]:
            uv0 = np.array(row[:6], np.float64).reshape(3, 2)
            uv1 = np.array(row[6:], np.float64).reshape(3, 2)
            # Blender V-up -> image row-down (the same flip face_texture uses)
            dst = np.stack([uv0[:, 0] * size, (1.0 - uv0[:, 1]) * size], axis=1)
            src = np.stack([uv1[:, 0] * src_size, (1.0 - uv1[:, 1]) * src_size], axis=1)
            # skip degenerate triangles (2D cross via determinant; numpy 2.x
            # dropped 2D np.cross)
            e1, e2 = dst[1] - dst[0], dst[2] - dst[0]
            if abs(e1[0] * e2[1] - e1[1] * e2[0]) < 1e-3:
                continue
            src_eye = eye  # sample region check: skip tris whose bbox has no eye px
            x0, y0 = np.floor(src.min(axis=0)).astype(int)
            x1, y1 = np.ceil(src.max(axis=0)).astype(int)
            x0, y0 = max(x0, 0), max(y0, 0)
            x1, y1 = min(x1, src_size), min(y1, src_size)
            if x1 <= x0 or y1 <= y0 or src_eye[y0:y1, x0:x1].max() == 0:
                continue
            M = cv2.getAffineTransform(src.astype(np.float32), dst.astype(np.float32))
            warped = cv2.warpAffine(eye, M, (size, size), flags=cv2.INTER_LINEAR)
            tri_mask = np.zeros((size, size), np.uint8)
            cv2.fillConvexPoly(tri_mask, np.round(dst).astype(np.int32), 255)
            np.maximum(acc, cv2.bitwise_and(warped, tri_mask), out=acc)

    out = 255 - acc                       # white = allowed, black = eye
    cv2.imwrite(str(out_path), out)
    n_black = int((out < 128).sum())
    print(f"baked {out_path}: eye px {n_black} ({100 * n_black / size / size:.2f}%)")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    bake(sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 1024)
