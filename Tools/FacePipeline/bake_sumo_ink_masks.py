"""Batch-bake all six players' no-draw ink masks for the SUMO body.

Per player: FaceUV eye mask -> UV0 (via data/sumo_ink_uv_map.json) at 2048,
then premultiply the STATIC no-draw layer (hair + fundoshi, from the
hand-painted masks): allowed = min(eye_allowed, 255 - nodraw_static).
"Masking-tape" semantics preserved end-to-end — zero C++ / material changes;
the six T_EyeMaskInk_<key> textures simply carry hair+cloth+eyes together.

Usage: python bake_sumo_ink_masks.py [size=2048]
Inputs : out/players/<key>/eye_mask.png (FaceUV space, frozen canonical layout
         — valid for sumo because sumo FaceUV was least-squares aligned to the
         same canonical frame)
         SourceAssets/nodraw_static_4096.png (white = hair+fundoshi no-draw)
Outputs: out/players/<key>/eye_mask_ink_sumo.png (UV0, white = ink allowed)
"""
import sys
import json
import cv2
import numpy as np
from pathlib import Path

BASE = Path(__file__).resolve().parent
SA = Path(r"C:\games\Unreal Engine\nice_ink\SourceAssets")
UV_MAP_JSON = BASE / "data" / "sumo_ink_uv_map.json"
PLAYERS = ["7AF4", "cvd", "caseoh", "ibai", "img1", "img0"]
SIZE = int(sys.argv[1]) if len(sys.argv) > 1 else 2048

with open(UV_MAP_JSON, encoding="utf-8") as f:
    table = json.load(f)
print(f"uv map: {table['face_polys']} polys / {len(table['tris'])} tris from {table['source']}")

# 靜態禁畫＝直接從兩張手繪正源取聯集（不經中間檔——重畫遮罩後重跑本腳本即同步）
hair = cv2.imread(str(SA / "hair_mask.png"), cv2.IMREAD_GRAYSCALE)
fund = cv2.imread(str(SA / "fundoshi_mask_sharp.png"), cv2.IMREAD_GRAYSCALE)
assert hair is not None and fund is not None, "hair_mask.png / fundoshi_mask_sharp.png missing"
nodraw = np.maximum(cv2.resize(hair, (SIZE, SIZE), interpolation=cv2.INTER_LINEAR),
                    cv2.resize(fund, (SIZE, SIZE), interpolation=cv2.INTER_AREA))
static_allowed = 255 - nodraw
print(f"static no-draw px: {int((static_allowed < 128).sum())} @{SIZE}")

for key in PLAYERS:
    eye_path = BASE / "out" / "players" / key / "eye_mask.png"
    out_path = BASE / "out" / "players" / key / "eye_mask_ink_sumo.png"
    eye = cv2.imread(str(eye_path), cv2.IMREAD_GRAYSCALE)
    if eye is None:
        print(f"SKIP {key}: no eye_mask.png")
        continue
    src_size = eye.shape[0]
    acc = np.zeros((SIZE, SIZE), np.uint8)
    if eye.max() > 0:
        for row in table["tris"]:
            uv0 = np.array(row[:6], np.float64).reshape(3, 2)
            uv1 = np.array(row[6:], np.float64).reshape(3, 2)
            dst = np.stack([uv0[:, 0] * SIZE, (1.0 - uv0[:, 1]) * SIZE], axis=1)
            src = np.stack([uv1[:, 0] * src_size, (1.0 - uv1[:, 1]) * src_size], axis=1)
            e1, e2 = dst[1] - dst[0], dst[2] - dst[0]
            if abs(e1[0] * e2[1] - e1[1] * e2[0]) < 1e-3:
                continue
            x0, y0 = np.floor(src.min(axis=0)).astype(int)
            x1, y1 = np.ceil(src.max(axis=0)).astype(int)
            x0, y0 = max(x0, 0), max(y0, 0)
            x1, y1 = min(x1, src_size), min(y1, src_size)
            if x1 <= x0 or y1 <= y0 or eye[y0:y1, x0:x1].max() == 0:
                continue
            M = cv2.getAffineTransform(src.astype(np.float32), dst.astype(np.float32))
            warped = cv2.warpAffine(eye, M, (SIZE, SIZE), flags=cv2.INTER_LINEAR)
            tri_mask = np.zeros((SIZE, SIZE), np.uint8)
            cv2.fillConvexPoly(tri_mask, np.round(dst).astype(np.int32), 255)
            np.maximum(acc, cv2.bitwise_and(warped, tri_mask), out=acc)
    allowed = np.minimum(255 - acc, static_allowed)
    cv2.imwrite(str(out_path), allowed)
    n_block = int((allowed < 128).sum())
    print(f"{key}: no-draw px {n_block} ({100 * n_block / SIZE / SIZE:.2f}%) -> {out_path.name}")
print("DONE_SUMO_INK_MASKS")
