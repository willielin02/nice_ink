"""
Hair/beard parameter extraction prototype (analysis end of procedural hair).

Usage:  python analyze_hair.py <selfie> [<selfie> ...]

Per selfie, extracts the parameters that will drive the procedural hair
generator, all normalized to face width so they are resolution-invariant:

  radial length profile  r(theta), 36 bins around the head center: how far
                         the hair silhouette extends in each direction
  volume                 hair area / face_width^2
  curliness              silhouette jaggedness (raw vs smoothed perimeter)
                         + internal texture energy at normalized scale
  hair color bands       median color in three vertical bands (roots->ends)
  beard                  coverage, drape length below chin, color - via
                         per-pixel skin-vs-hair color classification in the
                         lower-face region (no extra model needed)

Writes hair_params_<stem>.json + debug_hair_<stem>.jpg
"""
import sys
import json
import cv2
import numpy as np
from pathlib import Path

from selfie_to_face_texture import (
    OUT, load_bisenet, parse_selfie, detect_landmarks, estimate_roll_deg,
    normalize_roll, extract_skin_color, extract_hair_color, srgb_to_linear,
    FACE_OVAL_ORDER, ROLL_CORRECT_MIN_DEG, ROLL_CORRECT_MAX_DEG,
    MAX_SELFIE_SIDE,
)

N_RAYS = 36


def hair_mask_fullres(parsing, w, h):
    m512 = (parsing == 17).astype(np.uint8) * 255
    mask = cv2.resize(m512, (w, h), interpolation=cv2.INTER_NEAREST)
    return cv2.morphologyEx(mask, cv2.MORPH_CLOSE, np.ones((5, 5), np.uint8))


def radial_profile(mask, center, face_width):
    """Farthest mask pixel along each of N_RAYS directions, in face widths."""
    ys, xs = np.where(mask > 0)
    if len(ys) == 0:
        return [0.0] * N_RAYS
    dx = xs - center[0]
    dy = ys - center[1]
    ang = (np.degrees(np.arctan2(-dy, dx)) + 360) % 360   # 0=right, 90=up
    dist = np.sqrt(dx ** 2 + dy ** 2) / face_width
    prof = []
    for b in range(N_RAYS):
        sel = (ang >= b * 360 / N_RAYS) & (ang < (b + 1) * 360 / N_RAYS)
        prof.append(float(dist[sel].max()) if sel.any() else 0.0)
    return prof


def curliness(mask, selfie, face_width):
    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_NONE)
    if not contours:
        return 0.0, 0.0
    c = max(contours, key=cv2.contourArea).reshape(-1, 2).astype(np.float64)
    per_raw = np.linalg.norm(np.diff(np.vstack([c, c[:1]]), axis=0), axis=1).sum()
    # smooth the contour at a scale relative to the face (kills curl jags,
    # keeps the overall silhouette)
    k = max(int(face_width * 0.15) | 1, 5)
    cs = np.stack([cv2.GaussianBlur(c[:, 0], (1, k), 0).ravel(),
                   cv2.GaussianBlur(c[:, 1], (1, k), 0).ravel()], axis=1)
    per_smooth = np.linalg.norm(np.diff(np.vstack([cs, cs[:1]]), axis=0), axis=1).sum()
    silhouette_jag = float(per_raw / max(per_smooth, 1e-6)) - 1.0

    # internal texture energy at a normalized scale: resize so the face is
    # 128px wide (low enough that phone photos of all resolutions still have
    # real detail there - comparing at 256 is biased against low-res selfies)
    scale = 128.0 / face_width
    small = cv2.resize(selfie, (0, 0), fx=scale, fy=scale)
    msmall = cv2.resize(mask, (small.shape[1], small.shape[0]),
                        interpolation=cv2.INTER_NEAREST)
    lum = cv2.cvtColor(small, cv2.COLOR_BGR2GRAY).astype(np.float32)
    hp = lum - cv2.GaussianBlur(lum, (0, 0), 4)
    sel = msmall > 0
    texture = float(np.abs(hp[sel]).mean()) if sel.any() else 0.0
    return silhouette_jag, texture


def color_bands(mask, selfie, lm):
    """Median hair color in three vertical bands (above eyes / eyes-chin /
    below chin) -> detects root/length gradients."""
    eye_y = (lm[33][1] + lm[263][1]) / 2
    chin_y = lm[152][1]
    ys, xs = np.where(mask > 0)
    out = {}
    for name, sel in [
            ('top', ys < eye_y),
            ('mid', (ys >= eye_y) & (ys < chin_y)),
            ('ends', ys >= chin_y)]:
        if sel.sum() > 200:
            med = np.median(selfie[ys[sel], xs[sel]], axis=0)
            out[name] = [int(med[2]), int(med[1]), int(med[0])]   # RGB
        else:
            out[name] = None
    return out


def beard_analysis(selfie, parsing, lm, skin_bgr, hair_bgr, face_width):
    """Classify lower-face pixels by Lab distance to skin vs hair color."""
    h, w = selfie.shape[:2]
    # candidate region: the lower FACE OVAL (beard grows ON the face) plus a
    # narrow box straight below the chin (big beards hang). Side-drape scalp
    # hair lives OUTSIDE the face silhouette and must not leak in.
    lip_y = lm[13][1]
    chin = lm[152]
    jaw_l, jaw_r = lm[172], lm[397]
    oval_poly = lm[FACE_OVAL_ORDER].astype(np.int32)
    oval_mask = np.zeros((h, w), dtype=np.uint8)
    cv2.fillPoly(oval_mask, [oval_poly], 255)
    yy = np.arange(h)[:, None]
    region = (oval_mask > 0) & (yy > lip_y - 0.05 * face_width)
    x0 = max(int(min(jaw_l[0], jaw_r[0]) + 0.10 * face_width), 0)
    x1 = min(int(max(jaw_l[0], jaw_r[0]) - 0.10 * face_width), w - 1)
    y1 = min(int(chin[1] + 0.6 * face_width), h - 1)
    below = np.zeros((h, w), dtype=bool)
    below[int(chin[1]):y1, x0:x1] = True
    region |= below

    # only pixels BiSeNet already considers head content (skin or hair)
    m512 = np.isin(parsing, [1, 17]).astype(np.uint8) * 255
    head = cv2.resize(m512, (w, h), interpolation=cv2.INTER_NEAREST)
    region &= head > 0
    # exclude the lips themselves
    lips512 = np.isin(parsing, [11, 12, 13]).astype(np.uint8) * 255
    lips = cv2.resize(lips512, (w, h), interpolation=cv2.INTER_NEAREST)
    region &= lips == 0

    if hair_bgr is None or not region.any():
        return {'coverage': 0.0, 'drape': 0.0, 'color_rgb': None, 'pixels': 0}, np.zeros((h, w), bool)

    # chroma-weighted Lab distance: shadowed skin keeps the SKIN chroma even
    # though it darkens, so down-weighting L stops under-chin shadows from
    # classifying as near-black beard
    L_WEIGHT = 0.3
    lab = cv2.cvtColor(selfie, cv2.COLOR_BGR2LAB).astype(np.float32)
    lab[..., 0] *= L_WEIGHT
    skin_lab = cv2.cvtColor(skin_bgr.reshape(1, 1, 3), cv2.COLOR_BGR2LAB)[0, 0].astype(np.float32)
    hair_lab = cv2.cvtColor(hair_bgr.reshape(1, 1, 3), cv2.COLOR_BGR2LAB)[0, 0].astype(np.float32)
    skin_lab[0] *= L_WEIGHT
    hair_lab[0] *= L_WEIGHT
    d_skin = np.linalg.norm(lab - skin_lab, axis=2)
    d_hair = np.linalg.norm(lab - hair_lab, axis=2)
    beard = region & (d_hair < d_skin)
    beard_u8 = (beard * 255).astype(np.uint8)
    beard_u8 = cv2.morphologyEx(beard_u8, cv2.MORPH_OPEN, np.ones((5, 5), np.uint8))
    beard = beard_u8 > 0

    coverage = float(beard.sum() / max(region.sum(), 1))
    ys = np.where(beard.any(axis=1))[0]
    drape = float((ys.max() - chin[1]) / face_width) if len(ys) else 0.0
    px = selfie[beard]
    color = np.median(px, axis=0).astype(int) if len(px) > 500 else None
    return {
        'coverage': round(coverage, 3),
        'drape': round(max(drape, 0.0), 3),
        'color_rgb': [int(color[2]), int(color[1]), int(color[0])] if color is not None else None,
        'pixels': int(beard.sum()),
    }, beard


def analyze(path, bisenet):
    selfie = cv2.imread(str(path))
    h, w = selfie.shape[:2]
    if max(h, w) > MAX_SELFIE_SIDE:
        s = MAX_SELFIE_SIDE / max(h, w)
        selfie = cv2.resize(selfie, (int(w * s), int(h * s)), interpolation=cv2.INTER_AREA)
        h, w = selfie.shape[:2]

    lm = detect_landmarks(selfie)
    roll = estimate_roll_deg(lm)
    if ROLL_CORRECT_MIN_DEG <= abs(roll) <= ROLL_CORRECT_MAX_DEG:
        center = np.mean(lm[FACE_OVAL_ORDER], axis=0)
        selfie = normalize_roll(selfie, roll, center)
        lm = detect_landmarks(selfie)

    parsing = parse_selfie(bisenet, selfie)
    oval = lm[FACE_OVAL_ORDER]
    face_width = float(oval[:, 0].max() - oval[:, 0].min())
    skin = extract_skin_color(parsing, selfie)
    hair_c = extract_hair_color(parsing, selfie)

    hmask = hair_mask_fullres(parsing, w, h)
    head_center = ((lm[33] + lm[263]) / 2).astype(np.float64)

    profile = radial_profile(hmask, head_center, face_width)
    volume = float(np.count_nonzero(hmask)) / face_width ** 2
    jag, texture = curliness(hmask, selfie, face_width)
    bands = color_bands(hmask, selfie, lm)
    # drape: how far the LOWEST hair pixel hangs below the chin (side drape
    # hangs beside the face, not straight below the head center, so radial
    # down-rays miss it)
    hair_ys = np.where(hmask.any(axis=1))[0]
    chin_y = lm[152][1]
    drape = float(max(hair_ys.max() - chin_y, 0) / face_width) if len(hair_ys) else 0.0
    beard, beard_mask = beard_analysis(selfie, parsing, lm, skin, hair_c, face_width)

    params = {
        'source': Path(path).name,
        'face_width_px': round(face_width, 1),
        'hair': {
            'has_hair': hair_c is not None,
            'volume': round(volume, 3),
            'silhouette_jag': round(jag, 3),
            'texture_energy': round(texture, 2),
            'radial_profile_36': [round(v, 3) for v in profile],
            'top_extent': round(max(profile[7:11]), 3),      # ~70-110 deg (up)
            'side_extent': round(max(profile[0] , profile[17], profile[18], profile[35]), 3),
            'drape_below': round(drape, 3),                  # lowest hair vs chin
            'color_bands_rgb': bands,
        },
        'beard': beard,
    }

    stem = Path(path).stem[:12]
    with open(OUT / f"hair_params_{stem}.json", 'w', encoding='utf-8') as f:
        json.dump(params, f, indent=2)

    # debug visualization
    dbg = selfie.copy()
    cs, _ = cv2.findContours(hmask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    cv2.drawContours(dbg, cs, -1, (0, 255, 0), 2)
    dbg[beard_mask] = (dbg[beard_mask] * 0.5 + np.array((255, 64, 64)) * 0.5).astype(np.uint8)
    for b, r in enumerate(profile):
        a = np.radians((b + 0.5) * 360 / N_RAYS)
        end = (int(head_center[0] + np.cos(a) * r * face_width),
               int(head_center[1] - np.sin(a) * r * face_width))
        cv2.line(dbg, tuple(head_center.astype(int)), end, (0, 128, 255), 1)
    cv2.imwrite(str(OUT / f"debug_hair_{stem}.jpg"), dbg)
    return params


def main():
    paths = sys.argv[1:]
    if not paths:
        print("usage: analyze_hair.py <selfie> [...]")
        sys.exit(1)
    bisenet = load_bisenet()
    results = [analyze(p, bisenet) for p in paths]

    print(f"\n{'param':<22}" + "".join(f"{r['source'][:18]:>20}" for r in results))
    def row(label, fn):
        print(f"{label:<22}" + "".join(f"{fn(r):>20}" for r in results))
    row("hair volume", lambda r: r['hair']['volume'])
    row("silhouette jag", lambda r: r['hair']['silhouette_jag'])
    row("texture energy", lambda r: r['hair']['texture_energy'])
    row("top extent (fw)", lambda r: r['hair']['top_extent'])
    row("side extent (fw)", lambda r: r['hair']['side_extent'])
    row("drape below (fw)", lambda r: r['hair']['drape_below'])
    row("hair color top", lambda r: str(r['hair']['color_bands_rgb']['top']))
    row("beard coverage", lambda r: r['beard']['coverage'])
    row("beard drape (fw)", lambda r: r['beard']['drape'])
    row("beard color", lambda r: str(r['beard']['color_rgb']))


if __name__ == "__main__":
    main()
