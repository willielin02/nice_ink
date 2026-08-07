"""M4 對賬：python refs vs C++ native 產物逐工件 diff。

Usage: <venv python> compare_parity.py <refs_root> <native_root> [name...]
指標：face_open/face_closed＝alpha 聯集域的 RGB mean/p95 abs diff（0-255）＋
alpha mean diff；eye_mask_ink＝逐像素一致率（二值 127 閾）；skin_color＝RGB delta；
thumb＝mean abs diff。顆粒層（fill grain/眼閉 regrain）RNG 不同源——診斷時參考
p95 而非 max。
"""
import json
import sys
from pathlib import Path

import cv2
import numpy as np


def img_diff(a_path, b_path, use_alpha):
    a = cv2.imread(str(a_path), cv2.IMREAD_UNCHANGED)
    b = cv2.imread(str(b_path), cv2.IMREAD_UNCHANGED)
    if a is None or b is None:
        return None
    # native thumb 帶不透明 alpha（ImageWrapper PNG 恆 RGBA）——去 alpha 對齊
    if a.ndim == 3 and b.ndim == 3 and a.shape[2] != b.shape[2] and not use_alpha:
        if a.shape[2] == 4:
            a = a[:, :, :3]
        if b.shape[2] == 4:
            b = b[:, :, :3]
    if a.shape != b.shape:
        return {"shape_mismatch": f"{a.shape} vs {b.shape}"}
    out = {}
    if use_alpha and a.ndim == 3 and a.shape[2] == 4:
        sel = (a[:, :, 3] > 0) | (b[:, :, 3] > 0)
        d = np.abs(a[:, :, :3].astype(np.float32) - b[:, :, :3].astype(np.float32))[sel]
        da = np.abs(a[:, :, 3].astype(np.float32) - b[:, :, 3].astype(np.float32))
        out["rgb_mean"] = round(float(d.mean()), 2) if d.size else 0.0
        out["rgb_p95"] = round(float(np.percentile(d, 95)), 2) if d.size else 0.0
        out["alpha_mean"] = round(float(da.mean()), 3)
    else:
        d = np.abs(a.astype(np.float32) - b.astype(np.float32))
        out["mean"] = round(float(d.mean()), 2)
        out["p95"] = round(float(np.percentile(d, 95)), 2)
    return out


def main():
    refs = Path(sys.argv[1])
    native = Path(sys.argv[2])
    names = sys.argv[3:] or sorted(d.name for d in native.iterdir() if d.is_dir())
    for name in names:
        r, n = refs / name, native / name
        print(f"=== {name}")
        ref_exit = (r / "ref_exit.txt").read_text().strip() if (r / "ref_exit.txt").exists() else "?"
        n_ok = (n / "BAKE_RESULT.txt").read_text().strip() if (n / "BAKE_RESULT.txt").exists() else "MISSING"
        print(f"    py exit={ref_exit} | native={n_ok}")
        if ref_exit != "0" or not n_ok.startswith("DONE"):
            both_fail = ref_exit != "0" and not n_ok.startswith("DONE")
            print(f"    -> {'BOTH REJECT (behavior parity)' if both_fail else 'DISAGREE!'}")
            continue
        for f, use_a in (("face_open.png", True), ("face_closed.png", True), ("thumb.png", False)):
            d = img_diff(r / f, n / f, use_a)
            print(f"    {f:<16} {d}")
        # eye_mask_ink：二值一致率
        a = cv2.imread(str(r / "eye_mask_ink.png"), cv2.IMREAD_GRAYSCALE)
        b = cv2.imread(str(n / "eye_mask_ink.png"), cv2.IMREAD_GRAYSCALE)
        if a is not None and b is not None and a.shape == b.shape:
            agree = float(((a > 127) == (b > 127)).mean())
            print(f"    eye_mask_ink     binary agreement {agree * 100:.3f}%")
        sa = json.load(open(r / "skin_color.json"))["srgb_rgb_255"]
        sb = json.load(open(n / "skin_color.json"))["srgb_rgb_255"]
        print(f"    skin_color       py {sa} native {sb} (d={[abs(x - y) for x, y in zip(sa, sb)]})")
    print("COMPARE_DONE")


if __name__ == "__main__":
    main()
