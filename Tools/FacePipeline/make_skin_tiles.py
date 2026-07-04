"""
Build seamless body-skin tiles from the CC textures packed in Brat6.blend.

Source (extract with Blender first):
  Std_Skin_Body_Diffuse.jpg -> cc_body_diffuse.png   (albedo mottling source)
  Std_Skin_Head_Normal.png  -> cc_head_normal.png    (pore source: cheek region
                               has by far the strongest pore energy in the set)

Outputs (tile set for the tattoo-canvas skin material):
  skin_tile_albedo_detail.png  16-bit COLOR, multiplicative detail around 1.0
                               (encoded: value/32768 = per-channel linear ratio)
  skin_zone_tint.png           16-bit COLOR, LOW-frequency body color zoning
                               (blurred CC diffuse / mean, 1x over UVMap, NOT tiled)
                               -- the "alive flesh" layer: vascular-scale hue shifts
  skin_tile_normal.png         tangent-space pore detail normal (low-freq removed)
  skin_tile_rough.png          8-bit subtle roughness breakup around 128
  preview_*.png                2x2 tiled previews + detail applied on flat skin color
"""
import cv2
import numpy as np
from pathlib import Path

SRC_DIR = Path(r"C:\Users\willi\AppData\Local\Temp\claude\c--games-Unreal-Engine-nice-ink\0766f453-cbdd-4192-b58b-f7850c45308a\scratchpad")
OUT_DIR = Path(r"C:\games\Unreal Engine\nice_ink_face_pipeline")

# albedo source: clean flat patch on the body diffuse (left waist) --
# no nipples / veins / anatomical curvature
ALBEDO_PATCH = (300, 850, 512)          # x, y, size in the 2048 body diffuse
# pore source: left cheek of the 4096 head normal — strongest clean pore
# stipple that avoids eyes / nose wing / forehead creases (zone is narrow,
# so take 384 and upscale to tile size)
PORE_PATCH = (1440, 1750, 384)          # x, y, size in the 4096 head normal
TILE = 512

ALBEDO_BAND_LO = 8      # keep mottling below this blur ...
ALBEDO_BAND_HI = 100    # ... and above this blur (bandpass via blur ratio)
ALBEDO_CLAMP = 0.08     # max multiplicative deviation stored
ZONE_SIGMA = 64         # low-pass for the body zone tint (vascular-zone scale)
ZONE_CLAMP = 0.35       # max multiplicative deviation of the zone tint
NORMAL_HP_SIGMA = 10    # remove curvature lower-frequency than this from normals
NORMAL_GAIN = 2.0       # amplify extracted pore slopes (strength is retuned in-engine)
SEAM_BAND = 72          # cross-fade band width for seamless wrap
PREVIEW_SKIN_BGR = (120, 140, 180)
PREVIEW_DETAIL_AMOUNT = 0.05  # +-5% as recommended in the material design


def make_seamless(img):
    """Roll by half, then hide the resulting cross seams under a narrow blend
    with the original (continuous there). Borders come purely from the rolled
    copy (wrap-continuous); energy stays uniform outside the narrow band."""
    h, w = img.shape[:2]
    rolled = np.roll(np.roll(img, h // 2, axis=0), w // 2, axis=1)
    ys = np.abs(np.arange(h) - h / 2)[:, None]
    xs = np.abs(np.arange(w) - w / 2)[None, :]
    d = np.minimum(ys, xs)                      # distance to the seam cross
    t = np.clip(d / SEAM_BAND, 0, 1).astype(np.float32)
    wgt = 1.0 - t * t * (3 - 2 * t)             # smoothstep, 1 on seams -> 0
    if img.ndim == 3:
        wgt = wgt[..., None]
    return img * wgt + rolled * (1.0 - wgt)


def srgb_to_linear(img01):
    return np.where(img01 <= 0.04045, img01 / 12.92,
                    ((img01 + 0.055) / 1.055) ** 2.4)


def main():
    diffuse = cv2.imread(str(SRC_DIR / "cc_body_diffuse.png")).astype(np.float32)
    head_n = cv2.imread(str(SRC_DIR / "cc_head_normal.png")).astype(np.float32)
    diffuse_lin = srgb_to_linear(diffuse / 255.0)

    ax, ay, ap = ALBEDO_PATCH
    px, py, pp = PORE_PATCH
    d = diffuse_lin[ay:ay + ap, ax:ax + ap]
    n = head_n[py:py + pp, px:px + pp]
    if pp != TILE:
        n = cv2.resize(n, (TILE, TILE), interpolation=cv2.INTER_CUBIC)

    # ---- albedo detail: bandpassed multiplicative ratio, CHROMATIC ----
    # per-channel ratio keeps the hue variation of living skin
    band = cv2.GaussianBlur(d, (0, 0), ALBEDO_BAND_LO) / \
        np.maximum(cv2.GaussianBlur(d, (0, 0), ALBEDO_BAND_HI), 1e-5)
    ratio = np.clip(band, 1.0 - ALBEDO_CLAMP, 1.0 + ALBEDO_CLAMP)
    ratio = make_seamless(ratio)
    cv2.imwrite(str(OUT_DIR / "skin_tile_albedo_detail.png"),
                np.clip(ratio * 32768, 0, 65535).astype(np.uint16))

    # ---- body zone tint: low-frequency color zoning over the FULL UV ----
    # (sampled 1x via UVMap; anatomically approximate on a different UV
    # layout, but soft vascular-scale blotches read as living flesh either way)
    zone = cv2.GaussianBlur(diffuse_lin, (0, 0), ZONE_SIGMA)
    zone_ratio = zone / np.maximum(zone.reshape(-1, 3).mean(axis=0), 1e-5)
    zone_ratio = np.clip(zone_ratio, 1.0 - ZONE_CLAMP, 1.0 + ZONE_CLAMP)
    zone_small = cv2.resize(zone_ratio, (1024, 1024), interpolation=cv2.INTER_AREA)
    cv2.imwrite(str(OUT_DIR / "skin_zone_tint.png"),
                np.clip(zone_small * 32768, 0, 65535).astype(np.uint16))
    vis_zone = np.clip((zone_small - 1.0) * 2.0 + 0.5, 0, 1)
    cv2.imwrite(str(OUT_DIR / "preview_zone_tint.png"),
                (vis_zone * 255).astype(np.uint8))

    # ---- pore normal: high-pass in vector space, renormalize ----
    vec = n[..., ::-1] / 127.5 - 1.0          # BGR->RGB, to [-1,1] (x,y,z)
    low = cv2.GaussianBlur(vec, (0, 0), NORMAL_HP_SIGMA)
    hp = (vec - low) * NORMAL_GAIN
    hp[..., 2] = 0.0
    detail = hp.copy()
    detail[..., 2] = 1.0                       # rebuild z as flat + xy detail
    detail[..., 0] = make_seamless(detail[..., 0])
    detail[..., 1] = make_seamless(detail[..., 1])
    norm = np.linalg.norm(detail, axis=2, keepdims=True)
    detail /= np.maximum(norm, 1e-6)
    out_n = ((detail + 1.0) * 127.5)[..., ::-1]  # RGB->BGR for imwrite
    cv2.imwrite(str(OUT_DIR / "skin_tile_normal.png"),
                np.clip(out_n, 0, 255).astype(np.uint8))

    # ---- roughness breakup: blurred pore-slope magnitude around 0.5 ----
    slope = np.linalg.norm(hp[..., :2], axis=2)
    slope = cv2.GaussianBlur(slope, (0, 0), 8)
    slope = (slope - slope.mean()) / max(slope.std(), 1e-6)
    rough = np.clip(0.5 + slope * 0.04, 0, 1)  # +-~8% roughness variation
    rough = make_seamless(rough)
    cv2.imwrite(str(OUT_DIR / "skin_tile_rough.png"),
                np.clip(rough * 255, 0, 255).astype(np.uint8))

    # ---- previews ----
    def tile2x2(img):
        return np.vstack([np.hstack([img, img]), np.hstack([img, img])])

    # albedo detail visualization (contrast-boosted so it is inspectable)
    vis = np.clip((ratio - 1.0) * 5.0 + 0.5, 0, 1)
    cv2.imwrite(str(OUT_DIR / "preview_albedo_detail_2x2.png"),
                (tile2x2(vis) * 255).astype(np.uint8))  # BGR: hue shifts visible
    cv2.imwrite(str(OUT_DIR / "preview_normal_2x2.png"),
                tile2x2(np.clip(out_n, 0, 255).astype(np.uint8)))
    cv2.imwrite(str(OUT_DIR / "preview_rough_2x2.png"),
                (tile2x2(rough) * 255).astype(np.uint8))

    # detail applied on flat skin color at the recommended +-5% amount
    amount = PREVIEW_DETAIL_AMOUNT / ALBEDO_CLAMP
    applied_ratio = 1.0 + (ratio - 1.0) * amount
    skin = np.full((ap, ap, 3), PREVIEW_SKIN_BGR, dtype=np.float32)
    applied = np.clip(skin * applied_ratio, 0, 255).astype(np.uint8)
    cv2.imwrite(str(OUT_DIR / "preview_skin_applied_2x2.png"), tile2x2(applied))

    print("tiles + previews written to", OUT_DIR)


if __name__ == "__main__":
    main()
