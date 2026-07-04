"""
Selfie -> FaceUV texture pipeline (v4).

Architecture:
  - Geometry: MediaPipe landmarks + ellipse-arc contour define the TPS mapping.
    Smooth oval <-> smooth FaceUV oval keeps semantic correspondence stable.
  - Content validity: BiSeNet face mask (face WITHOUT hair/ears/neck/background)
    decides which pixels may enter the texture; everything outside becomes the
    player's skin color, so the TPS can never pull in ears, neck or background.
  - v4 contract (2026-07-03): scalp hair and the jaw/cheek beard are MESH-borne
    (hair/beard library) and are skin-filled out of the texture. Everything
    inside the face stays untouched pixel-for-pixel: mustache, wrinkles, and
    brows (even when BiSeNet fails to label faint-but-visible brows). When the
    fringe truly covers the brows the fill leaves an empty brow region - the
    hairstyle veto MUST then only allow styles that certainly cover the brow
    line (see face_flags.json brow status; correctness, not preference).
  - Feature concentration: the TPS source contour is inflated outward instead of
    shrinking the FaceUV target, so the texture edge receives real content
    rather than dilation smear.
"""
import sys
import json
import argparse
import cv2
import numpy as np
import torch
import torchvision.transforms as transforms
from PIL import Image
import mediapipe as mp
from mediapipe.tasks import python as mp_python
from mediapipe.tasks.python import vision as mp_vision
from face_parsing.model import BiSeNet
from pathlib import Path

BASE = Path(r"C:\games\Unreal Engine\nice_ink_face_pipeline")
DEFAULT_SELFIE = BASE / "3DDFA_V2" / "examples" / "inputs" / "d74dbdfef4c65c8271c42b882054311d.jpg"
FACEUV_MASK_PATH = BASE / "faceuv_mask.png"
# expanded-island mode (see make_faceuv_expansion.py): coverage mask defines
# alpha/fades, the JSON holds W-mapped TPS constants that freeze the feature
# layout of the old island while extending sampling into the new ring
COVERAGE_MASK_PATH = BASE / "faceuv_mask_coverage.png"
EXPANSION_DATA_PATH = BASE / "faceuv_expansion_data.json"
FACE_LANDMARKER_PATH = BASE / "face_landmarker.task"
BISENET_PATH = BASE / "79999_iter.pth"
OUTPUT_PATH = BASE / "face_texture.png"
SKIN_COLOR_PATH = BASE / "skin_color.json"
HAIR_COLOR_PATH = BASE / "hair_color.json"
BEARD_COLOR_PATH = BASE / "beard_color.json"
DEBUG_PATH = BASE / "face_texture_debug.png"
DEBUG_SELFIE_CONTOUR = BASE / "debug_selfie_contour.jpg"
DEBUG_FACEUV_CONTOUR = BASE / "debug_faceuv_contour.jpg"
DEBUG_BISENET_OVERLAY = BASE / "debug_bisenet_overlay.jpg"
DEBUG_SKINFILL = BASE / "debug_skinfill.jpg"

TEX_SIZE = 2048
MAX_SELFIE_SIDE = 1600          # keeps canvas margin > max inflate + safety, so TPS never samples off-canvas
CANVAS_SAFETY = 48              # TPS source points clamped this far inside the canvas
N_CONTOUR_PTS = 64
N_TOP_ARC_PTS = 36
MASK_DEFLATE_RATIO = 0.03       # erode head mask inward (3% of face width)
MASK_DEFLATE_MIN_512 = 2        # erode floor: at least this many px at BiSeNet's 512 resolution
SELFIE_INFLATE_RATIO = 0.09     # inflate TPS source contour outward (9% of face width)
FILL_SOURCE_INSET_RATIO = 0.015 # extra erode for the fill sampling ring (deeper into clean content)
FILL_BLUR_RATIO = 0.04          # gaussian sigma smoothing the extended fill into soft shading
FILL_BLEND_DIST_RATIO = 0.35    # distance (in face widths) of the median pull ramp
FILL_MEDIAN_PULL = 0.3          # v4.5: cap the pull toward the global median - the median is
                                # brighter than jaw/cheek content and used to paint a flat halo;
                                # body continuity now comes from the alpha fade + shared material
                                # tint chain, so the fill can stay local
FILL_GRAIN_STD = 7.0            # luminance grain matching the selfie's photo noise (content
                                # std ~7.6 in texture space; TPS resampling smooths ~30%)
FACEUV_Y_OFFSET = 30            # FaceUV space (fixed px; legacy mode only --
                                # in expanded mode it is baked into old_targets)
FADE_DIST = 25                  # FaceUV space (fixed px)
FADE_TRANSITION = 30            # FaceUV space (fixed px)
EDGE_BLEND_DIST = 30            # RGB converges to skin color this far inside the boundary
ROT_COMPENSATE_MAX = 15.0       # sanity cap for the mapping-rotation compensation (deg)
ROLL_CORRECT_MIN_DEG = 2.0      # normalize head roll beyond this
ROLL_CORRECT_MAX_DEG = 30.0     # beyond this, assume intentional/extreme photo and leave it

# BiSeNet labels kept in the texture: skin, brows, eyes, glasses, nose, mouth,
# lips. Hair(17) is OUT since v4 - hair is mesh-borne, its pixels skin-fill.
# Excludes ears(7,8), earring(9), neck(14), necklace(15), cloth(16), hat(18).
HEAD_LABELS = [1, 2, 3, 4, 5, 6, 10, 11, 12, 13]
SKIN_LABEL = 1
HAIR_LABEL = 17
LIP_LABELS = [11, 12, 13]
EAR_LABELS = [7, 8]
NECK_LABEL = 14

BEARD_REMOVE_MIN_COVERAGE = 0.10  # below this it is stubble/shadow: keep it in the texture
BEARD_DILATE_RATIO = 0.012        # eat mixed boundary pixels around the removed beard
PHILTRUM_X_MARGIN_RATIO = 0.08    # mustache protection extends this far past the mouth corners
LIP_GUARD_RATIO = 0.05            # fill sampling and beard removal keep this margin off the lips
BROW_COVER_FRACTION = 0.5         # hair over this share of a brow band = that brow is covered
FACE_FLAGS_PATH = BASE / "face_flags.json"
DEBUG_BEARD_REMOVAL = BASE / "debug_beard_removal.jpg"

# MediaPipe brow landmark chains (upper edge), used for the brow-cover bands
BROW_L_LM = [70, 63, 105, 66, 107]
BROW_R_LM = [336, 296, 334, 293, 300]

FACE_OVAL_ORDER = [
    10, 338, 297, 332, 284, 251, 389, 356, 454, 323, 361, 288,
    397, 365, 379, 378, 400, 377, 152, 148, 176, 149, 150, 136,
    172, 58, 132, 93, 234, 127, 162, 21, 54, 103, 67, 109,
]


# ---------------------------------------------------------------- BiSeNet

def load_bisenet():
    net = BiSeNet(n_classes=19)
    state = torch.load(str(BISENET_PATH), map_location='cpu', weights_only=True)
    net.load_state_dict(state)
    net.eval()
    return net


def parse_selfie(net, selfie_bgr):
    """Run BiSeNet, return 512x512 label map."""
    rgb = cv2.cvtColor(selfie_bgr, cv2.COLOR_BGR2RGB)
    to_tensor = transforms.Compose([
        transforms.Resize((512, 512)),
        transforms.ToTensor(),
        transforms.Normalize((0.485, 0.456, 0.406), (0.229, 0.224, 0.225)),
    ])
    tensor = to_tensor(Image.fromarray(rgb)).unsqueeze(0)
    with torch.no_grad():
        out = net(tensor)[0]
    return out.squeeze(0).argmax(0).cpu().numpy().astype(np.uint8)


def build_head_mask(parsing, w, h, deflate_px, face_contour):
    """Head mask at selfie resolution, anchored to the landmarked face,
    eroded inward by deflate_px (with a floor covering 512-res upscale error)."""
    m512 = np.isin(parsing, HEAD_LABELS).astype(np.uint8) * 255
    # Seal only pinhole segmentation noise at 512; a bigger close at full
    # resolution would seal genuine background channels between hair strands
    # back into the mask.
    m512 = cv2.morphologyEx(m512, cv2.MORPH_CLOSE, np.ones((3, 3), np.uint8))
    mask = cv2.resize(m512, (w, h), interpolation=cv2.INTER_NEAREST)

    # Keep the component that overlaps the landmarked face, not the largest:
    # in multi-person photos the largest blob can be someone else's head.
    n_labels, labels, stats, _ = cv2.connectedComponentsWithStats(mask)
    if n_labels > 2:
        poly = np.zeros((h, w), dtype=np.uint8)
        cv2.fillPoly(poly, [face_contour.astype(np.int32)], 1)
        best, best_overlap = 0, 0
        for i in range(1, n_labels):
            overlap = np.count_nonzero((labels == i) & (poly > 0))
            if overlap > best_overlap:
                best, best_overlap = i, overlap
        if best_overlap == 0:
            best = 1 + np.argmax(stats[1:, cv2.CC_STAT_AREA])
        mask = np.where(labels == best, np.uint8(255), np.uint8(0))

    upscale = max(h, w) / 512
    iters = max(int(round(deflate_px)),
                int(np.ceil(MASK_DEFLATE_MIN_512 * upscale)), 1)
    mask = cv2.erode(mask, np.ones((3, 3), np.uint8), iterations=iters)
    return mask


def extract_skin_color(parsing, selfie_bgr):
    """Per-channel median of BiSeNet face-skin pixels."""
    h, w = selfie_bgr.shape[:2]
    skin512 = (parsing == SKIN_LABEL).astype(np.uint8) * 255
    skin512 = cv2.erode(skin512, np.ones((3, 3), np.uint8), iterations=2)
    skin = cv2.resize(skin512, (w, h), interpolation=cv2.INTER_NEAREST)
    px = selfie_bgr[skin > 0]
    if len(px) == 0:
        return np.array([120, 140, 180], dtype=np.uint8)
    return np.median(px, axis=0).astype(np.uint8)


MIN_HAIR_FRACTION = 0.005   # hair pixels below this share of the image = bald


def extract_hair_color(parsing, selfie_bgr):
    """Per-channel median of BiSeNet hair pixels; None when effectively bald.
    Drives the tint of the in-game hairstyle mesh."""
    h, w = selfie_bgr.shape[:2]
    hair512 = (parsing == 17).astype(np.uint8) * 255
    hair512 = cv2.erode(hair512, np.ones((3, 3), np.uint8), iterations=2)
    hair = cv2.resize(hair512, (w, h), interpolation=cv2.INTER_NEAREST)
    px = selfie_bgr[hair > 0]
    if len(px) < MIN_HAIR_FRACTION * h * w:
        return None
    return np.median(px, axis=0).astype(np.uint8)


def write_hair_color(hair_bgr):
    if hair_bgr is None:
        data = {"has_hair": False}
    else:
        r, g, b = int(hair_bgr[2]), int(hair_bgr[1]), int(hair_bgr[0])
        rgb01 = [r / 255.0, g / 255.0, b / 255.0]
        data = {
            "has_hair": True,
            "srgb_rgb_255": [r, g, b],
            "srgb_hex": f"#{r:02x}{g:02x}{b:02x}",
            "linear_rgb": [round(srgb_to_linear(c), 6) for c in rgb01],
        }
    with open(HAIR_COLOR_PATH, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)


def natural_skin_fill(selfie_bgr, head_mask, skin_color, face_width,
                      sample_exclude=None):
    """Fill outside the head mask with a natural continuation instead of a flat color.

    Three safety layers keep contaminated silhouette pixels out of the fill:
    the head mask is already eroded 3% inward, the sampling ring is eroded a
    further FILL_SOURCE_INSET_RATIO, and each sampled color is a masked local
    average (normalized convolution) rather than a single pixel.
    sample_exclude: bool HxW of pixels that must not act as fill sources (e.g.
    dilated lips - a removed-beard hole next to the mouth must fill from cheek
    skin, not smear lip pink onto the chin).
    """
    h, w = selfie_bgr.shape[:2]
    inset = max(1, int(round(face_width * FILL_SOURCE_INSET_RATIO)))
    src_mask = cv2.erode(head_mask, np.ones((3, 3), np.uint8), iterations=inset)
    if sample_exclude is not None:
        src_mask = src_mask.copy()
        src_mask[sample_exclude] = 0
    if cv2.countNonZero(src_mask) == 0:
        src_mask = head_mask

    # Local interior average: blur image*mask and mask separately, divide.
    # Sampled colors become neighborhood means of pure head content, so a
    # stray shadow/highlight pixel on the ring cannot tint a whole region.
    sigma_nc = max(2.0, face_width * 0.02)
    m = (src_mask > 0).astype(np.float32)
    num = cv2.GaussianBlur(selfie_bgr.astype(np.float32) * m[..., None], (0, 0), sigma_nc)
    den = cv2.GaussianBlur(m, (0, 0), sigma_nc)
    interior_avg = num / np.maximum(den, 1e-6)[..., None]

    # Extend: every outside pixel takes the color of its nearest source pixel.
    inv = (src_mask == 0).astype(np.uint8)
    dist, lbl = cv2.distanceTransformWithLabels(
        inv, cv2.DIST_L2, 5, labelType=cv2.DIST_LABEL_PIXEL)
    src_coords = np.argwhere(inv == 0)
    lut = np.zeros(int(lbl.max()) + 1, dtype=np.int64)
    lut[lbl[inv == 0]] = src_coords[:, 0] * w + src_coords[:, 1]
    extended = interior_avg.reshape(-1, 3)[lut[lbl].ravel()].reshape(h, w, 3)

    # Radial streaks -> soft shading, with only a CAPPED pull toward the
    # median so the fill keeps the local tone (full convergence painted a
    # bright flat halo around darker jaw/cheek content).
    extended = cv2.GaussianBlur(extended, (0, 0), max(2.0, face_width * FILL_BLUR_RATIO))
    t = np.clip(dist / max(1.0, face_width * FILL_BLEND_DIST_RATIO), 0, 1)[..., None]
    t *= FILL_MEDIAN_PULL
    fill = extended * (1.0 - t) + skin_color.astype(np.float32) * t

    # CROSS-BAND blend instead of a hard cut: replacing outside pixels
    # wholesale leaves the mask's own edge as a visible dirty rim (the
    # selfie's silhouette shading sits right on it, unlike either side).
    # Feather the mask a few px so content dissolves into fill.
    m_soft = cv2.GaussianBlur((head_mask > 0).astype(np.float32), (0, 0),
                              max(2.0, face_width * 0.008))[..., None]
    filled_f = (selfie_bgr.astype(np.float32) * m_soft
                + np.clip(fill, 0, 255) * (1.0 - m_soft))
    # photo-noise grain on the fill side: the smooth fill next to real sensor
    # noise reads as a boundary on its own; deterministic seed = reproducible
    rng = np.random.default_rng(7)
    grain = rng.normal(0.0, FILL_GRAIN_STD, size=(h, w, 1)).astype(np.float32)
    filled_f += np.repeat(grain, 3, axis=2) * (1.0 - m_soft)
    return np.clip(filled_f, 0, 255).astype(np.uint8)


def lips_guard_mask(parsing, w, h, face_width):
    """Dilated-lips bool mask: protected from beard removal and fill sampling."""
    lips512 = np.isin(parsing, LIP_LABELS).astype(np.uint8) * 255
    lips = cv2.resize(lips512, (w, h), interpolation=cv2.INTER_NEAREST)
    guard = max(1, int(round(face_width * LIP_GUARD_RATIO)))
    return cv2.dilate(lips, np.ones((3, 3), np.uint8), iterations=guard) > 0


def brow_cover_status(parsing, all_lm, w, h, face_width):
    """Are the brows under the fringe? Judged by HAIR pixels covering the
    landmark brow bands - NOT by brow-label counts. BiSeNet misses faint but
    fully visible brows (CaseOh: visible blond brows, zero brow labels), so
    label counts would wrongly demand a brow-covering hairstyle."""
    hair512 = (parsing == HAIR_LABEL).astype(np.uint8) * 255
    hair = cv2.resize(hair512, (w, h), interpolation=cv2.INTER_NEAREST)
    pad = max(2, int(round(face_width * 0.02)))
    out = {}
    for side, idx in (("L", BROW_L_LM), ("R", BROW_R_LM)):
        pts = all_lm[idx]
        x0 = max(int(pts[:, 0].min()) - pad, 0)
        x1 = min(int(pts[:, 0].max()) + pad, w - 1)
        y0 = max(int(pts[:, 1].min()) - pad, 0)
        y1 = min(int(pts[:, 1].max()) + pad, h - 1)
        band = hair[y0:y1 + 1, x0:x1 + 1]
        frac = float(np.count_nonzero(band)) / max(band.size, 1)
        out[side] = {"hair_fraction": round(frac, 3),
                     "covered": bool(frac > BROW_COVER_FRACTION),
                     "box": [x0, y0, x1, y1]}
    # ANY covered brow triggers the safety lock: partial brows must not bake
    out["covered"] = bool(out["L"]["covered"] or out["R"]["covered"])
    return out


def philtrum_mask(all_lm, w, h, face_width):
    """Protected mustache band: subnasale down to the upper inner lip,
    mouth width plus margin. Stays texture-borne (user spec)."""
    mouth_l, mouth_r = all_lm[61], all_lm[291]
    y0 = max(int(all_lm[2][1]), 0)
    y1 = min(int(all_lm[13][1]), h - 1)
    xm = face_width * PHILTRUM_X_MARGIN_RATIO
    x0 = max(int(min(mouth_l[0], mouth_r[0]) - xm), 0)
    x1 = min(int(max(mouth_l[0], mouth_r[0]) + xm), w - 1)
    m = np.zeros((h, w), bool)
    m[y0:y1 + 1, x0:x1 + 1] = True
    return m


def lower_face_zone(all_lm, w, h, face_width, deflate_iters):
    """GENEROUS beard zone for LaMa: everything below the lip line inside the
    (deflated) face oval. Pixel-exact beard masks are the failed paradigm -
    LaMa regenerates the whole zone from surrounding skin, and any beard left
    at the zone border would be propagated back in, so the zone must cover the
    beard completely (cheeks included, which the oval provides)."""
    oval = np.zeros((h, w), np.uint8)
    cv2.fillPoly(oval, [all_lm[FACE_OVAL_ORDER].astype(np.int32)], 255)
    oval = cv2.erode(oval, np.ones((3, 3), np.uint8), iterations=max(deflate_iters, 1))
    lip_y = all_lm[13][1]
    yy = np.arange(h)[:, None]
    return (oval > 0) & (yy > lip_y - 0.05 * face_width)


def save_bisenet_overlay(selfie_bgr, parsing, path):
    vis = np.zeros((512, 512, 3), dtype=np.uint8)
    vis[np.isin(parsing, HEAD_LABELS)] = (0, 255, 0)
    vis[np.isin(parsing, EAR_LABELS)] = (0, 0, 255)
    vis[parsing == NECK_LABEL] = (255, 0, 0)
    small = cv2.resize(selfie_bgr, (512, 512))
    overlay = cv2.addWeighted(small, 0.4, vis, 0.6, 0)
    cv2.imwrite(str(path), overlay)


# ---------------------------------------------------------------- landmarks

def detect_landmarks(selfie_bgr):
    h, w = selfie_bgr.shape[:2]
    rgb = cv2.cvtColor(selfie_bgr, cv2.COLOR_BGR2RGB)
    mp_img = mp.Image(image_format=mp.ImageFormat.SRGB, data=np.ascontiguousarray(rgb))
    opts = mp_vision.FaceLandmarkerOptions(
        base_options=mp_python.BaseOptions(model_asset_path=str(FACE_LANDMARKER_PATH)),
        num_faces=1,
    )
    det = mp_vision.FaceLandmarker.create_from_options(opts)
    result = det.detect(mp_img)
    det.close()
    if not result.face_landmarks:
        print("ERROR: No face detected")
        sys.exit(1)
    face = result.face_landmarks[0]
    return np.array([(lm.x * w, lm.y * h) for lm in face], dtype=np.float64)


def estimate_roll_deg(all_landmarks):
    """Head roll from the outer eye corners (33 = image-left, 263 = image-right)."""
    p1, p2 = all_landmarks[33], all_landmarks[263]
    return float(np.degrees(np.arctan2(p2[1] - p1[1], p2[0] - p1[0])))


def normalize_roll(selfie_bgr, roll_deg, face_center):
    """Rotate the selfie so the eye line is horizontal.
    BORDER_REPLICATE avoids black corners; those areas end up skin-filled anyway."""
    h, w = selfie_bgr.shape[:2]
    M = cv2.getRotationMatrix2D(tuple(face_center), roll_deg, 1.0)
    return cv2.warpAffine(selfie_bgr, M, (w, h), flags=cv2.INTER_LINEAR,
                          borderMode=cv2.BORDER_REPLICATE)


# ---------------------------------------------------------------- contours

def shoelace_area(contour):
    xs, ys = contour[:, 0], contour[:, 1]
    js = np.arange(1, len(contour) + 1) % len(contour)
    return np.sum(xs * ys[js] - xs[js] * ys)


def ensure_clockwise(contour):
    if shoelace_area(contour) < 0:
        return contour[::-1].copy()
    return contour


def build_selfie_contour(all_landmarks):
    """Bottom half from FACE_OVAL landmarks, top half from a fitted ellipse arc."""
    oval_pts = np.array([all_landmarks[i] for i in FACE_OVAL_ORDER], dtype=np.float64)
    n = len(oval_pts)

    ellipse = cv2.fitEllipse(oval_pts.astype(np.float32))
    (cx, cy), (w_diam, h_diam), angle_deg = ellipse
    a, b = w_diam / 2, h_diam / 2
    ar = np.radians(angle_deg)
    cos_r, sin_r = np.cos(ar), np.sin(ar)

    x_med = np.median(oval_pts[:, 0])
    left_mask = oval_pts[:, 0] < x_med
    right_mask = ~left_mask
    left_top_idx = np.where(left_mask)[0][np.argmin(oval_pts[left_mask, 1])]
    right_top_idx = np.where(right_mask)[0][np.argmin(oval_pts[right_mask, 1])]

    bottom = []
    i = right_top_idx
    while True:
        bottom.append(oval_pts[i])
        if i == left_top_idx and len(bottom) > 1:
            break
        i = (i + 1) % n

    def to_local(pt):
        dx, dy = pt[0] - cx, pt[1] - cy
        return dx * cos_r + dy * sin_r, -dx * sin_r + dy * cos_r

    def param_angle(pt):
        lx, ly = to_local(pt)
        return np.arctan2(ly / b, lx / a)

    def ellipse_pt(t):
        lx, ly = a * np.cos(t), b * np.sin(t)
        return np.array([cx + lx * cos_r - ly * sin_r,
                         cy + lx * sin_r + ly * cos_r])

    t_left = param_angle(oval_pts[left_top_idx])
    t_right = param_angle(oval_pts[right_top_idx])

    if t_left <= t_right:
        ts_a = np.linspace(t_left, t_right, N_TOP_ARC_PTS)
        ts_b = np.linspace(t_left, t_right - 2 * np.pi, N_TOP_ARC_PTS)
    else:
        ts_a = np.linspace(t_left, t_right + 2 * np.pi, N_TOP_ARC_PTS)
        ts_b = np.linspace(t_left, t_right, N_TOP_ARC_PTS)

    pts_a = np.array([ellipse_pt(t) for t in ts_a])
    pts_b = np.array([ellipse_pt(t) for t in ts_b])
    top_arc = pts_a if np.mean(pts_a[:, 1]) < np.mean(pts_b[:, 1]) else pts_b

    contour = np.vstack([np.array(bottom), top_arc])
    contour = ensure_clockwise(contour)
    return contour, ellipse


def get_faceuv_contour(mask_path=FACEUV_MASK_PATH):
    mask = cv2.imread(str(mask_path), cv2.IMREAD_GRAYSCALE)
    if mask is None:
        print(f"ERROR: Cannot read {mask_path}")
        sys.exit(1)
    _, binary = cv2.threshold(mask, 127, 255, cv2.THRESH_BINARY)
    n_labels, labels, stats, _ = cv2.connectedComponentsWithStats(binary)
    if n_labels > 2:
        largest = 1 + np.argmax(stats[1:, cv2.CC_STAT_AREA])
        binary = np.where(labels == largest, np.uint8(255), np.uint8(0))
    contours, _ = cv2.findContours(binary, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_NONE)
    contour = max(contours, key=cv2.contourArea).reshape(-1, 2).astype(np.float64)
    contour = ensure_clockwise(contour)
    return contour, binary


def load_expansion():
    """Expanded-island constants, or None for the legacy single-mask mode."""
    if not (EXPANSION_DATA_PATH.exists() and COVERAGE_MASK_PATH.exists()):
        return None
    with open(EXPANSION_DATA_PATH, encoding='utf-8') as f:
        d = json.load(f)
    return {k: np.array(v, dtype=np.float64) for k, v in d.items()}


def resample_contour(contour, n_pts):
    top_idx = np.argmin(contour[:, 1])
    contour = np.roll(contour, -top_idx, axis=0)
    closed = np.vstack([contour, contour[0:1]])
    seg_lens = np.linalg.norm(np.diff(closed, axis=0), axis=1)
    cum = np.concatenate([[0], np.cumsum(seg_lens)])
    total = cum[-1]
    dists = np.linspace(0, total, n_pts, endpoint=False)
    sampled = np.zeros((n_pts, 2), dtype=np.float64)
    for i, d in enumerate(dists):
        idx = min(np.searchsorted(cum, d, side="right") - 1, len(contour) - 1)
        seg = cum[idx + 1] - cum[idx]
        t = 0 if seg < 1e-8 else (d - cum[idx]) / seg
        sampled[i] = closed[idx] * (1 - t) + closed[idx + 1] * t
    return sampled


def inflate_contour(pts, margin):
    """Move points along outward normals; positive margin = outward (CW contour)."""
    n = len(pts)
    inflated = pts.copy()
    for i in range(n):
        tangent = pts[(i + 1) % n] - pts[(i - 1) % n]
        normal = np.array([tangent[1], -tangent[0]])
        norm = np.linalg.norm(normal)
        if norm > 1e-8:
            normal /= norm
        inflated[i] = pts[i] + normal * margin
    return inflated


# ---------------------------------------------------------------- warp

def make_canvas(image, fill_color, src_pts):
    """Skin-prefilled canvas + source points in canvas space (safety-clamped:
    warpImage returns black for off-canvas samples with no border control, so
    off-frame points are pulled into the prefilled area instead)."""
    h, w = image.shape[:2]
    canvas = np.full((TEX_SIZE, TEX_SIZE, 3), fill_color, dtype=np.uint8)
    oy, ox = (TEX_SIZE - h) // 2, (TEX_SIZE - w) // 2
    canvas[oy:oy + h, ox:ox + w] = image
    adj_src = src_pts.copy()
    adj_src[:, 0] += ox
    adj_src[:, 1] += oy
    np.clip(adj_src, CANVAS_SAFETY, TEX_SIZE - CANVAS_SAFETY, out=adj_src)
    return canvas, adj_src, (ox, oy)


def estimate_tps(dst_pts, src_pts):
    tps = cv2.createThinPlateSplineShapeTransformer()
    matches = [cv2.DMatch(i, i, 0) for i in range(len(dst_pts))]
    tps.estimateTransformation(dst_pts.reshape(1, -1, 2).astype(np.float32),
                               src_pts.reshape(1, -1, 2).astype(np.float32), matches)
    return tps


def rotate_pts(pts, center, deg):
    r = np.radians(deg)
    R = np.array([[np.cos(r), -np.sin(r)], [np.sin(r), np.cos(r)]])
    return (pts - center) @ R.T + center


# symmetric landmark pairs (right, left) and midline landmarks used to fit
# the face symmetry axis: eye outer/inner corners, mouth corners, brow outer,
# cheeks; nose bridge / nose tip / upper lip / chin
SYM_PAIRS = [(33, 263), (133, 362), (61, 291), (70, 300), (50, 280)]
MIDLINE = [168, 1, 13, 152]
MAX_CENTER_SHIFT = 80.0   # px cap for the lateral centering translation


def align_symmetry(dst_all, src_all, lm_canvas, island_center_x):
    """Rigidly rotate + laterally translate the target constellation so the
    face's own symmetry axis is vertical and centered on the island.

    'Eye line level' is not what makes a face read as straight - viewers key
    on the nose/philtrum/chin column. Fit the symmetry axis from paired
    landmarks' midpoints plus midline landmarks (PCA line), cancel its tilt
    and lateral offset. Rigid transform: feature geometry untouched; the
    content asymmetry of a yawed photo is identity information and stays."""
    fwd = estimate_tps(src_all, dst_all)          # canvas -> texture
    _, out = fwd.applyTransformation(lm_canvas.reshape(1, -1, 2).astype(np.float32))
    pts = out.reshape(-1, 2).astype(np.float64)
    n_pairs = len(SYM_PAIRS)
    mids = (pts[:n_pairs] + pts[n_pairs:2 * n_pairs]) / 2
    axis_pts = np.vstack([mids, pts[2 * n_pairs:]])

    centered = axis_pts - axis_pts.mean(axis=0)
    _, _, vt = np.linalg.svd(centered)
    d = vt[0]
    if d[1] < 0:
        d = -d                                     # point down (image coords)
    theta = float(np.degrees(np.arctan2(d[0], d[1])))  # deviation from vertical
    if abs(theta) > ROT_COMPENSATE_MAX:
        print(f"   WARNING: symmetry-axis tilt {theta:+.1f} deg exceeds cap, skipping")
        return dst_all, 0.0, 0.0
    center = dst_all.mean(axis=0)
    dst2 = rotate_pts(dst_all, center, theta)
    axis_mean = rotate_pts(axis_pts.mean(axis=0)[None, :], center, theta)[0]
    dx = float(np.clip(island_center_x - axis_mean[0],
                       -MAX_CENTER_SHIFT, MAX_CENTER_SHIFT))
    dst2[:, 0] += dx
    return dst2, theta, dx


def _warp_extra(tps, extra):
    """Send a single-channel mask through the SAME tps as the image."""
    if extra is None:
        return None
    h, w = extra.shape[:2]
    ec = np.zeros((TEX_SIZE, TEX_SIZE, 3), dtype=np.uint8)
    oy, ox = (TEX_SIZE - h) // 2, (TEX_SIZE - w) // 2
    ec[oy:oy + h, ox:ox + w] = extra[..., None].repeat(3, axis=2)
    return tps.warpImage(ec)[..., 0]


def tps_warp(image, src_pts, dst_pts, fill_color, lm_pts=None, island_center_x=None,
             extra=None):
    """Legacy single-mask warp (+ optional symmetry alignment)."""
    canvas, adj_src, (ox, oy) = make_canvas(image, fill_color, src_pts)
    dst = dst_pts
    if lm_pts is not None:
        lm_canvas = lm_pts + np.array([ox, oy])
        dst, theta, dx = align_symmetry(dst_pts.copy(), adj_src, lm_canvas, island_center_x)
        print(f"   symmetry aligned: rot {theta:+.2f} deg, shift {dx:+.1f}px")
    tps = estimate_tps(dst, adj_src)
    return tps.warpImage(canvas), _warp_extra(tps, extra)


def tps_warp_expanded(image, src_pts, exp, fill_color, lm_pts, island_center_x,
                      extra=None):
    """Expanded-island warp with a frozen feature layout.

    The per-player boundary sources pair with the W-mapped new targets, and
    the fixed interior anchors are pinned by evaluating the OLD texture->canvas
    mapping at the anchor positions (per player, cheap: one extra TPS estimate
    + one applyTransformation). Features land on identical mesh surface points
    as the old island (up to the rigid derotation); the ring receives the
    mapping's natural extension."""
    canvas, adj_src, (ox, oy) = make_canvas(image, fill_color, src_pts)

    t_old = estimate_tps(exp['old_targets'], adj_src)
    _, chk = t_old.applyTransformation(
        exp['old_targets'].reshape(1, -1, 2).astype(np.float32))
    err = np.abs(chk.reshape(-1, 2) - adj_src).max()
    if err > 1.0:
        print(f"   WARNING: TPS direction self-check failed (err {err:.2f}px)")
    _, mapped = t_old.applyTransformation(
        exp['anchor_old_px'].reshape(1, -1, 2).astype(np.float32))
    anchor_src = mapped.reshape(-1, 2).astype(np.float64)
    np.clip(anchor_src, CANVAS_SAFETY, TEX_SIZE - CANVAS_SAFETY, out=anchor_src)

    dst_all = np.vstack([exp['new_targets'], exp['anchor_new_px']])
    src_all = np.vstack([adj_src, anchor_src])
    lm_canvas = lm_pts + np.array([ox, oy])
    dst_all, theta, dx = align_symmetry(dst_all, src_all, lm_canvas, island_center_x)
    print(f"   symmetry aligned: rot {theta:+.2f} deg, shift {dx:+.1f}px")
    tps = estimate_tps(dst_all, src_all)
    return tps.warpImage(canvas), _warp_extra(tps, extra)


SEAM_QA_PNG = BASE / "seam_qa.png"
SEAM_QA_JSON = BASE / "seam_qa.json"
CONTENT_MASK_PNG = BASE / "content_mask.png"
DIRECT_MODE = False   # set by --direct: A/B variant replacing LaMa with direct fills


def direct_extend_fill(img, hole, sigma_avg=6.0, blur=4.0, feather=3.0):
    """A/B alternative to LaMa for thin holes (seam ring, brow strands):
    nearest-clean-neighbor extension from BOTH sides + blur + feathered paste."""
    if hole is None or not hole.any():
        return img
    h, w = img.shape[:2]
    m = (~hole).astype(np.float32)
    num = cv2.GaussianBlur(img.astype(np.float32) * m[..., None], (0, 0), sigma_avg)
    den = cv2.GaussianBlur(m, (0, 0), sigma_avg)
    avg = num / np.maximum(den, 1e-6)[..., None]
    inv = hole.astype(np.uint8)
    _, lbl = cv2.distanceTransformWithLabels(inv, cv2.DIST_L2, 5,
                                             labelType=cv2.DIST_LABEL_PIXEL)
    src_coords = np.argwhere(inv == 0)
    lut = np.zeros(int(lbl.max()) + 1, dtype=np.int64)
    lut[lbl[inv == 0]] = src_coords[:, 0] * w + src_coords[:, 1]
    ext = avg.reshape(-1, 3)[lut[lbl].ravel()].reshape(h, w, 3)
    ext = cv2.GaussianBlur(ext, (0, 0), blur)
    f = cv2.GaussianBlur(hole.astype(np.float32), (0, 0), feather)[..., None]
    return np.clip(img.astype(np.float32) * (1 - f) + ext * f, 0, 255).astype(np.uint8)


def seam_qa(warped, content_mask, uv_mask):
    """Detect boundary color mismatch: per boundary pixel of the CONTENT
    region (selfie content vs fill), dE between the local mean color sampled
    a few px INSIDE vs a few px OUTSIDE. Overlay: green <5, yellow 5-10,
    red >10. Returns (overlay BGR, stats dict)."""
    inside = (content_mask > 127) & (uv_mask > 0)
    if not inside.any():
        return None, {}
    GAP, RING, SIGMA = 4, 10, 8.0
    lab_img = cv2.cvtColor(warped, cv2.COLOR_BGR2LAB).astype(np.float32)
    din = cv2.distanceTransform(inside.astype(np.uint8), cv2.DIST_L2, 5)
    dout = cv2.distanceTransform((~inside).astype(np.uint8), cv2.DIST_L2, 5)
    in_ring = (din > GAP) & (din <= GAP + RING)
    out_ring = (dout > GAP) & (dout <= GAP + RING) & (uv_mask > 0)

    def field(sel):
        m = sel.astype(np.float32)
        num = cv2.GaussianBlur(lab_img * m[..., None], (0, 0), SIGMA)
        den = cv2.GaussianBlur(m, (0, 0), SIGMA)
        return num / np.maximum(den, 1e-6)[..., None], den

    fin, win = field(in_ring)
    fout, wout = field(out_ring)
    boundary = (din > 0) & (din <= 1.5) & (win > 1e-3) & (wout > 1e-3)
    dE = np.linalg.norm(fin - fout, axis=2)

    # LINE-ARTIFACT metric: the seam pixels THEMSELVES. A clean seam pixel
    # lies between its two sides; color that escapes the inside<->outside
    # segment (e.g. a desaturated gray-green rim from mask-edge mixed pixels)
    # is an artifact even when both sides agree. The side-vs-side metric has
    # a deliberate GAP and is blind to this - it once scored a visibly dirty
    # chin seam 'green'.
    seam_band = (din <= 2.5) | ((dout <= 2.5) & (uv_mask > 0))
    seam_band &= (win > 1e-3) & (wout > 1e-3)
    mid = (fin + fout) / 2
    half = np.linalg.norm(fin - fout, axis=2) / 2
    dev = np.linalg.norm(lab_img - mid, axis=2) - half   # >0: outside the blend range
    dev = np.maximum(dev, 0)

    vals = dE[boundary]
    line_vals = dev[seam_band]
    stats = {
        "boundary_px": int(boundary.sum()),
        "dE_mean": round(float(vals.mean()), 2),
        "dE_p95": round(float(np.percentile(vals, 95)), 2),
        "dE_max": round(float(vals.max()), 2),
        "frac_over_5": round(float((vals > 5).mean()), 3),
        "frac_over_10": round(float((vals > 10).mean()), 3),
        "line_dev_mean": round(float(line_vals.mean()), 2),
        "line_dev_p95": round(float(np.percentile(line_vals, 95)), 2),
        "line_frac_over_5": round(float((line_vals > 5).mean()), 3),
    }
    clusters, seen = [], set()
    for y, x in zip(*np.where(boundary & (dE > 10))):
        key = (y // 128, x // 128)
        if key not in seen:
            seen.add(key)
            clusters.append([int(x), int(y), round(float(dE[y, x]), 1)])
    stats["hotspots_xy_dE"] = clusters[:20]
    clusters, seen = [], set()
    for y, x in zip(*np.where(seam_band & (dev > 5))):
        key = (y // 128, x // 128)
        if key not in seen:
            seen.add(key)
            clusters.append([int(x), int(y), round(float(dev[y, x]), 1)])
    stats["line_hotspots_xy_dev"] = clusters[:20]

    overlay = warped.copy()
    for lo, hi, col in ((0, 5, (0, 200, 0)), (5, 10, (0, 220, 255)), (10, 1e9, (0, 0, 255))):
        sel = boundary & (dE >= lo) & (dE < hi)
        sel = cv2.dilate(sel.astype(np.uint8), np.ones((5, 5), np.uint8)) > 0
        overlay[sel] = col
    line_bad = cv2.dilate((seam_band & (dev > 5)).astype(np.uint8),
                          np.ones((5, 5), np.uint8)) > 0
    overlay[line_bad] = (255, 0, 255)   # magenta: dirty seam-line pixels
    return overlay, stats


# ---------------------------------------------------------------- debug

def draw_contour_debug(img, contour, sampled, path, head_mask=None):
    dbg = img.copy() if len(img.shape) == 3 else cv2.cvtColor(img, cv2.COLOR_GRAY2BGR)
    if head_mask is not None:
        mask_contours, _ = cv2.findContours(head_mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_NONE)
        cv2.drawContours(dbg, mask_contours, -1, (255, 255, 0), 2)
    pts_i = contour.astype(np.int32)
    for i in range(len(pts_i)):
        cv2.line(dbg, tuple(pts_i[i]), tuple(pts_i[(i + 1) % len(pts_i)]), (0, 255, 0), 2)
    for x, y in sampled:
        cv2.circle(dbg, (int(x), int(y)), 5, (0, 0, 255), -1)
    cv2.circle(dbg, (int(sampled[0][0]), int(sampled[0][1])), 12, (255, 0, 0), 3)
    cv2.imwrite(str(path), dbg)


# ---------------------------------------------------------------- main

def srgb_to_linear(c01):
    return c01 / 12.92 if c01 <= 0.04045 else ((c01 + 0.055) / 1.055) ** 2.4


def write_beard_color(beard_meta, bearded):
    """Median beard color from beard_analysis -> tint for the beard MESH
    (mirrors hair_color.json so avatar assembly shares one tint code path)."""
    color = beard_meta.get("color_rgb")
    if not bearded or color is None:
        data = {"has_beard": False}
    else:
        r, g, b = int(color[0]), int(color[1]), int(color[2])
        rgb01 = [r / 255.0, g / 255.0, b / 255.0]
        data = {
            "has_beard": True,
            "srgb_rgb_255": [r, g, b],
            "srgb_hex": f"#{r:02x}{g:02x}{b:02x}",
            "linear_rgb": [round(srgb_to_linear(c), 6) for c in rgb01],
        }
    with open(BEARD_COLOR_PATH, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)


def write_skin_color(skin_bgr):
    r, g, b = int(skin_bgr[2]), int(skin_bgr[1]), int(skin_bgr[0])
    rgb01 = [r / 255.0, g / 255.0, b / 255.0]
    data = {
        "srgb_rgb_255": [r, g, b],
        "srgb_hex": f"#{r:02x}{g:02x}{b:02x}",
        "linear_rgb": [round(srgb_to_linear(c), 6) for c in rgb01],
    }
    with open(SKIN_COLOR_PATH, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)


def main():
    parser = argparse.ArgumentParser(description="Selfie -> FaceUV texture")
    parser.add_argument("selfie", nargs="?", default=str(DEFAULT_SELFIE),
                        help="path to the selfie photo")
    parser.add_argument("--direct", action="store_true",
                        help="A/B variant: direct clean-skin fill + gradients instead of LaMa")
    args = parser.parse_args()
    global DIRECT_MODE
    DIRECT_MODE = args.direct
    if DIRECT_MODE:
        print("   [A/B] DIRECT fill mode - no LaMa passes")
    selfie_path = Path(args.selfie)

    print("=== Selfie -> FaceUV Texture (BiSeNet validity + landmark TPS) ===\n")

    print("1. Loading BiSeNet model...")
    bisenet = load_bisenet()

    print("2. Reading selfie...")
    selfie = cv2.imread(str(selfie_path))
    if selfie is None:
        print(f"ERROR: Cannot read {selfie_path}")
        sys.exit(1)
    h, w = selfie.shape[:2]
    if max(h, w) > MAX_SELFIE_SIDE:
        scale = MAX_SELFIE_SIDE / max(h, w)
        selfie = cv2.resize(selfie, (int(w * scale), int(h * scale)), interpolation=cv2.INTER_AREA)
        h, w = selfie.shape[:2]
        print(f"   downscaled to {w}x{h}")
    else:
        print(f"   {w}x{h}")

    print("3. Detecting face landmarks...")
    all_lm = detect_landmarks(selfie)
    roll_deg = estimate_roll_deg(all_lm)
    if ROLL_CORRECT_MIN_DEG <= abs(roll_deg) <= ROLL_CORRECT_MAX_DEG:
        face_center = np.mean(all_lm[FACE_OVAL_ORDER], axis=0)
        selfie = normalize_roll(selfie, roll_deg, face_center)
        all_lm = detect_landmarks(selfie)
        print(f"   roll corrected: {roll_deg:+.1f} deg")
    selfie_contour, ellipse_params = build_selfie_contour(all_lm)
    selfie_sampled = resample_contour(selfie_contour, N_CONTOUR_PTS)
    face_width = ellipse_params[1][0]
    deflate_px = face_width * MASK_DEFLATE_RATIO
    inflate_px = face_width * SELFIE_INFLATE_RATIO
    print(f"   face width: {face_width:.0f}px, mask deflate: {deflate_px:.1f}px, "
          f"contour inflate: {inflate_px:.1f}px")

    print("4. BiSeNet head segmentation...")
    parsing = parse_selfie(bisenet, selfie)
    head_mask = build_head_mask(parsing, w, h, deflate_px, selfie_contour)
    head_area = np.count_nonzero(head_mask)
    if head_area < 0.01 * h * w:
        print("ERROR: Head mask too small, segmentation failed")
        sys.exit(1)
    skin_color = extract_skin_color(parsing, selfie)
    hair_color = extract_hair_color(parsing, selfie)
    write_hair_color(hair_color)
    save_bisenet_overlay(selfie, parsing, DEBUG_BISENET_OVERLAY)
    print(f"   head mask: {head_area} px ({100 * head_area / (h * w):.1f}%)")
    if hair_color is None:
        print("   hair: none detected (bald)")
    else:
        print(f"   hair BGR: ({hair_color[0]}, {hair_color[1]}, {hair_color[2]})")
    print(f"   -> {HAIR_COLOR_PATH}")
    print(f"   -> {DEBUG_BISENET_OVERLAY}")

    print("4b. Selfie analysis: beard detection + brow status...")
    from analyze_hair import beard_analysis  # deferred: analyze_hair imports this module
    lips_guard = lips_guard_mask(parsing, w, h, face_width)
    brows = brow_cover_status(parsing, all_lm, w, h, face_width)
    beard_meta, beard_px = beard_analysis(selfie, parsing, all_lm, skin_color,
                                          hair_color, face_width)
    bearded = beard_meta["coverage"] >= BEARD_REMOVE_MIN_COVERAGE
    beard_det = None
    if bearded and beard_px.any():
        grow = max(1, int(round(face_width * BEARD_DILATE_RATIO)))
        beard_det = cv2.dilate(beard_px.astype(np.uint8) * 255,
                               np.ones((3, 3), np.uint8), iterations=grow) > 0
        # clean tone for the fill: the crude detector finds the DARKEST beard
        # pixels, which is exactly what poisons the median
        skin512 = (parsing == SKIN_LABEL).astype(np.uint8) * 255
        skin_full = cv2.resize(skin512, (w, h), interpolation=cv2.INTER_NEAREST) > 0
        px = selfie[skin_full & ~beard_det]
        if len(px):
            skin_color = np.median(px, axis=0).astype(np.uint8)
    print(f"   beard coverage {beard_meta['coverage']:.2f} "
          f"({'mesh-borne, zone regenerated below' if bearded else 'kept in texture'})")
    print(f"   brows covered: L={brows['L']['covered']} R={brows['R']['covered']}"
          f" (hair frac L={brows['L']['hair_fraction']} R={brows['R']['hair_fraction']})")
    with open(FACE_FLAGS_PATH, "w", encoding="utf-8") as f:
        json.dump({"brows": brows, "beard_mesh_borne": bool(bearded),
                   "beard_coverage": round(float(beard_meta["coverage"]), 3)}, f, indent=2)
    print(f"   -> {FACE_FLAGS_PATH}")
    write_beard_color(beard_meta, bearded)
    print(f"   -> {BEARD_COLOR_PATH}")

    print("5. Natural skin fill outside head mask...")
    sample_ex = lips_guard.copy()
    if beard_det is not None:
        sample_ex |= beard_det   # jaw fill must sample cheek skin, not beard
    selfie_filled = natural_skin_fill(selfie, head_mask, skin_color, face_width,
                                      sample_exclude=sample_ex)

    print("5b. LaMa regeneration on the hair-free image...")
    # ORDER MATTERS: LaMa runs AFTER the hair fill. On the raw selfie the
    # context around every zone is hair, so LaMa regenerates hair (v4.2 baked
    # black chunks above the brows). On the filled image the context is skin.
    from lama_fill import lama_inpaint      # deferred: loads 208MB ONNX
    dbg = selfie_filled.copy()

    beard_zone = None
    if bearded:
        deflate_iters = max(int(round(deflate_px)), 1)
        in_oval = lower_face_zone(all_lm, w, h, face_width, deflate_iters)
        philtrum = philtrum_mask(all_lm, w, h, face_width)
        lips512 = np.isin(parsing, LIP_LABELS).astype(np.uint8) * 255
        lips_keep = cv2.dilate(cv2.resize(lips512, (w, h),
                               interpolation=cv2.INTER_NEAREST),
                               np.ones((3, 3), np.uint8), iterations=2) > 0
        # The LaMa mask must cover the ANATOMICAL beard territory completely:
        # ear-bottom -> mouth-corner lines down to the frame bottom. v4.1-4.3
        # lessons: any beard left at a mask border (cheek flanks above the lip
        # line, hanging beard below a too-short chin strip) is context LaMa
        # continues right back into the zone. The mustache is inside the mask
        # too and pasted back from the pre-LaMa image afterwards.
        ear_l, ear_r = all_lm[132], all_lm[361]
        mouth_l, mouth_r = all_lm[61], all_lm[291]
        poly = np.array([
            [ear_l[0], ear_l[1]], [mouth_l[0], mouth_l[1]],
            [mouth_r[0], mouth_r[1]], [ear_r[0], ear_r[1]],
            [min(ear_r[0] + 0.15 * face_width, w - 1), h - 1],
            [max(ear_l[0] - 0.15 * face_width, 0), h - 1],
        ], dtype=np.int32)
        territory = np.zeros((h, w), np.uint8)
        cv2.fillPoly(territory, [poly], 255)
        zone_lama = (territory > 0) & ~lips_keep

        keep = philtrum & ~lips_keep
        beard_zone = in_oval & ~keep

        # upper-face clean-skin reference tone (both modes need it)
        terr_dil = cv2.dilate(zone_lama.astype(np.uint8),
                              np.ones((3, 3), np.uint8), iterations=6) > 0
        skin512b = (parsing == SKIN_LABEL).astype(np.uint8) * 255
        skin_fullb = cv2.resize(skin512b, (w, h), interpolation=cv2.INTER_NEAREST) > 0
        ref = cv2.erode((skin_fullb & ~terr_dil).astype(np.uint8),
                        np.ones((3, 3), np.uint8), iterations=2) > 0
        ref_tone = (np.median(selfie_filled[ref].reshape(-1, 3), axis=0)
                    if ref.sum() > 500 else skin_color.astype(np.float64))
        gray_f = cv2.cvtColor(selfie_filled, cv2.COLOR_BGR2GRAY).astype(np.float32)
        hf = gray_f - cv2.GaussianBlur(gray_f, (0, 0), 2.0)
        std_ref = float(hf[ref].std()) if ref.any() else 0.0
        dz = cv2.distanceTransform(beard_zone.astype(np.uint8), cv2.DIST_L2, 5)
        wgt = np.clip(dz / max(2.0, face_width * 0.06), 0, 1)[..., None]

        if DIRECT_MODE:
            # pure replacement: reference tone + vertical shading ramp,
            # feathered border, grain. No context borrowing at all.
            ys = np.where(beard_zone.any(axis=1))[0]
            y0z, y1z = int(ys.min()), int(ys.max())
            ramp = 1.0 - 0.10 * np.clip((np.arange(h) - y0z) / max(y1z - y0z, 1), 0, 1)
            tone_field = ref_tone[None, None, :] * ramp[:, None, None]
            img_f = selfie_filled.astype(np.float32)
            img_f = img_f * (1 - wgt) + tone_field * wgt
            if std_ref > 0.5:
                g = np.random.default_rng(11).normal(0, std_ref, size=(h, w, 1)).astype(np.float32)
                img_f += np.repeat(g, 3, axis=2) * wgt
            selfie_filled = np.clip(img_f, 0, 255).astype(np.uint8)
            print(f"   [A/B] beard zone DIRECT-filled ({int(beard_zone.sum())} px, "
                  f"ref BGR {ref_tone.round(0)}, ramp 10%, grain {std_ref:.1f})")
        else:
            original = selfie_filled.copy()
            selfie_filled = lama_inpaint(selfie_filled, zone_lama)
            feather = cv2.GaussianBlur(keep.astype(np.float32), (0, 0), 1.5)[..., None]
            selfie_filled = (selfie_filled.astype(np.float32) * (1 - feather)
                             + original.astype(np.float32) * feather).astype(np.uint8)
            print(f"   beard zone regenerated ({int(zone_lama.sum())} px incl. "
                  f"philtrum+chin strip), mustache pasted back")

            # v4.9 tone retarget + grain match: LaMa borrows shading from the
            # zone borders but its tone inherits the stubble-shadowed context;
            # pull the low frequency toward the upper-face reference.
            if ref.sum() > 500:
                img_f = selfie_filled.astype(np.float32)
                low = cv2.GaussianBlur(img_f, (0, 0), max(4.0, face_width * 0.12))
                w85 = wgt * 0.85
                img_f = img_f - low + (low * (1 - w85) + ref_tone[None, None, :] * w85)
                gray2 = cv2.cvtColor(selfie_filled, cv2.COLOR_BGR2GRAY).astype(np.float32)
                hf2 = gray2 - cv2.GaussianBlur(gray2, (0, 0), 2.0)
                add = float(np.sqrt(max(std_ref ** 2 - float(hf2[beard_zone].std()) ** 2, 0.0)))
                if add > 0.5:
                    g = np.random.default_rng(11).normal(0, add, size=(h, w, 1)).astype(np.float32)
                    img_f += np.repeat(g, 3, axis=2) * beard_zone[..., None]
                selfie_filled = np.clip(img_f, 0, 255).astype(np.uint8)
                print(f"   zone tone retargeted to upper-face skin "
                      f"(ref px {int(ref.sum())}, ref BGR {ref_tone.round(0)}, grain +{add:.1f})")
        skin_color = extract_skin_color(parsing, selfie_filled)
    write_skin_color(skin_color)
    print(f"   skin BGR: ({skin_color[0]}, {skin_color[1]}, {skin_color[2]})")
    print(f"   -> {SKIN_COLOR_PATH}")

    # visible brows: the radial fill already replaced fringe strands with
    # streaky skin; regenerate those bands plus the under-fringe SHADOW (skin-
    # labeled, darker than the skin tone) so nothing hair-ish bakes near the
    # brows. Covered brows get NO bake - the veto picks a covering style.
    hair512 = (parsing == HAIR_LABEL).astype(np.uint8) * 255
    hair_full = cv2.resize(hair512, (w, h), interpolation=cv2.INTER_NEAREST) > 0
    gray = cv2.cvtColor(selfie_filled, cv2.COLOR_BGR2GRAY)
    skin512 = (parsing == SKIN_LABEL).astype(np.uint8) * 255
    skin_full = cv2.resize(skin512, (w, h), interpolation=cv2.INTER_NEAREST) > 0
    skin_lum = float(np.median(gray[skin_full])) if skin_full.any() else 128.0
    brow_holes = np.zeros((h, w), bool)
    for side in ("L", "R"):
        if brows[side]["covered"]:
            continue
        bx0, by0, bx1, by1 = brows[side]["box"]
        xm = int(0.04 * face_width)
        band = np.zeros((h, w), bool)
        band[by0:by1 + 1, max(bx0 - xm, 0):min(bx1 + xm, w - 1) + 1] = True
        brow_holes |= band & hair_full          # ex-fringe streaks in the band
        sy0 = max(by0 - int(0.20 * face_width), 0)
        strip = np.zeros((h, w), bool)
        strip[sy0:by0, max(bx0 - xm, 0):min(bx1 + xm, w - 1) + 1] = True
        brow_holes |= strip & hair_full
        brow_holes |= strip & skin_full & (gray < 0.80 * skin_lum)
    if brow_holes.any():
        brow_holes = cv2.dilate(brow_holes.astype(np.uint8),
                                np.ones((3, 3), np.uint8), iterations=2) > 0
        selfie_filled = (direct_extend_fill(selfie_filled, brow_holes) if DIRECT_MODE
                         else lama_inpaint(selfie_filled, brow_holes))
        print(f"   brow repair: {int(brow_holes.sum())} px regenerated "
              f"(ex-fringe streaks + shadow, skin_lum {skin_lum:.0f})")

    # seam-ring regeneration: the selfie's own silhouette shading (a darker
    # band hugging the face edge) is real content, so masks/feathering leave
    # it as a dirty rim between face and fill (seam QA flagged the whole
    # contour). Regenerate a thin ring straddling the boundary - LaMa
    # rebuilds the transition from clean skin on both sides.
    rr = max(3, int(round(face_width * 0.02)))
    hb = (head_mask > 0).astype(np.uint8)
    k3 = np.ones((3, 3), np.uint8)
    ring = (cv2.dilate(hb, k3, iterations=rr) - cv2.erode(hb, k3, iterations=rr)) > 0
    selfie_filled = (direct_extend_fill(selfie_filled, ring) if DIRECT_MODE
                     else lama_inpaint(selfie_filled, ring))
    print(f"   seam ring regenerated: {int(ring.sum())} px (±{rr}px of the content edge)")

    for zone, col in ((beard_zone, (0, 0, 255)), (brow_holes, (255, 0, 255))):
        if zone is not None and zone.any():
            m3 = zone[..., None].astype(np.float32) * 0.45
            tint = np.zeros_like(dbg, dtype=np.float32)
            tint[..., :] = col
            dbg = (dbg.astype(np.float32) * (1 - m3) + tint * m3).astype(np.uint8)
    for side in ("L", "R"):
        bx0, by0, bx1, by1 = brows[side]["box"]
        col = (0, 255, 255) if brows[side]["covered"] else (0, 200, 0)
        cv2.rectangle(dbg, (bx0, by0), (bx1, by1), col, 2)
    cv2.imwrite(str(DEBUG_BEARD_REMOVAL), dbg)
    cv2.imwrite(str(DEBUG_SKINFILL), selfie_filled)
    print(f"   -> {DEBUG_BEARD_REMOVAL}")
    print(f"   -> {DEBUG_SKINFILL}")

    print("6. Extracting FaceUV contour...")
    expansion = load_expansion()
    if expansion is not None:
        uv_contour, uv_mask = get_faceuv_contour(COVERAGE_MASK_PATH)
        uv_sampled = resample_contour(uv_contour, N_CONTOUR_PTS)
        print(f"   expanded mode: coverage mask + {len(expansion['anchor_old_px'])} anchors")
    else:
        uv_contour, uv_mask = get_faceuv_contour()
        uv_sampled = resample_contour(uv_contour, N_CONTOUR_PTS)
        print(f"   legacy mode: contour {len(uv_contour)} pts, sampled: {N_CONTOUR_PTS}")

    print("7. Saving contour debug images...")
    src_inflated = inflate_contour(selfie_sampled, inflate_px)
    draw_contour_debug(selfie, selfie_contour, src_inflated, DEBUG_SELFIE_CONTOUR, head_mask)
    draw_contour_debug(uv_mask, uv_contour, uv_sampled, DEBUG_FACEUV_CONTOUR)
    print(f"   -> {DEBUG_SELFIE_CONTOUR}")
    print(f"   -> {DEBUG_FACEUV_CONTOUR}")

    print("8. TPS warp (inflated source -> full FaceUV)...")
    lm_idx = [p[0] for p in SYM_PAIRS] + [p[1] for p in SYM_PAIRS] + MIDLINE
    lm_pts = all_lm[lm_idx]
    xs_mask = np.where(uv_mask > 0)[1]
    island_center_x = float((xs_mask.min() + xs_mask.max()) / 2)
    if expansion is not None:
        warped, content_mask = tps_warp_expanded(selfie_filled, src_inflated, expansion,
                                                 skin_color, lm_pts, island_center_x,
                                                 extra=head_mask)
    else:
        uv_target = uv_sampled.copy()
        uv_target[:, 1] += FACEUV_Y_OFFSET
        warped, content_mask = tps_warp(selfie_filled, src_inflated, uv_target, skin_color,
                                        lm_pts, island_center_x, extra=head_mask)

    print("9. Alpha from feathered FaceUV mask...")
    mask_f = cv2.GaussianBlur(uv_mask.astype(np.float32), (0, 0), sigmaX=5)
    alpha = np.clip(mask_f, 0, 255).astype(np.uint8)

    print("10. Edge RGB convergence (local target) + bottom-half alpha fade...")
    dist = cv2.distanceTransform(uv_mask, cv2.DIST_L2, 5)
    # v4.5: converge the outermost band toward the LOCAL content color just
    # inside the band, not the global skin median - the median is dominated by
    # bright forehead/nose pixels, so darker jaw/cheek boundaries jumped ~18dE
    # into a flat halo ring. Continuity with the BODY is provided by the alpha
    # fade plus the shared material tint chain (setup_skin_material 'Tint
    # Chain' multiplies detail/zone over both sides since v4.5).
    src = (uv_mask > 0) & (dist > EDGE_BLEND_DIST)
    mf = src.astype(np.float32)
    num = cv2.GaussianBlur(warped.astype(np.float32) * mf[..., None], (0, 0), 12.0)
    den = cv2.GaussianBlur(mf, (0, 0), 12.0)
    interior_avg = num / np.maximum(den, 1e-6)[..., None]
    inv = (~src).astype(np.uint8)
    _, lbl = cv2.distanceTransformWithLabels(inv, cv2.DIST_L2, 5,
                                             labelType=cv2.DIST_LABEL_PIXEL)
    src_coords = np.argwhere(inv == 0)
    lut = np.zeros(int(lbl.max()) + 1, dtype=np.int64)
    lut[lbl[inv == 0]] = src_coords[:, 0] * TEX_SIZE + src_coords[:, 1]
    local_target = interior_avg.reshape(-1, 3)[lut[lbl].ravel()].reshape(TEX_SIZE, TEX_SIZE, 3)
    local_target = cv2.GaussianBlur(local_target, (0, 0), 6.0)
    edge_w = np.clip(1.0 - dist / EDGE_BLEND_DIST, 0, 1)[..., None].astype(np.float32)
    warped = (warped.astype(np.float32) * (1 - edge_w)
              + local_target * edge_w).astype(np.uint8)
    ys_mask = np.where(uv_mask > 0)[0]
    y_center = (ys_mask.min() + ys_mask.max()) // 2
    y_fade_start = y_center - FADE_TRANSITION // 2

    y_coords = np.arange(TEX_SIZE).reshape(-1, 1).astype(np.float32)
    blend_weight = np.clip((y_coords - y_fade_start) / max(FADE_TRANSITION, 1), 0, 1)
    edge_factor = np.clip(dist / FADE_DIST, 0, 1)
    fade_alpha = 1.0 - blend_weight * (1.0 - edge_factor)
    fade_alpha[uv_mask == 0] = 0

    alpha = (alpha.astype(np.float32) * fade_alpha).astype(np.uint8)
    filled = np.count_nonzero(alpha)
    print(f"   coverage: {filled} px ({100 * filled / TEX_SIZE ** 2:.1f}%)")

    print("11. Seam QA (content-boundary dE detector)...")
    if content_mask is not None:
        cv2.imwrite(str(CONTENT_MASK_PNG), content_mask)
        qa_overlay, qa = seam_qa(warped, content_mask, uv_mask)
        if qa_overlay is not None:
            cv2.imwrite(str(SEAM_QA_PNG), qa_overlay)
            with open(SEAM_QA_JSON, "w", encoding="utf-8") as f:
                json.dump(qa, f, indent=2)
            print(f"   side-vs-side dE mean {qa['dE_mean']} p95 {qa['dE_p95']} max {qa['dE_max']}"
                  f" | >5: {qa['frac_over_5']:.0%} >10: {qa['frac_over_10']:.0%}")
            print(f"   seam-line dev mean {qa['line_dev_mean']} p95 {qa['line_dev_p95']}"
                  f" | >5: {qa['line_frac_over_5']:.0%}"
                  f" | line hotspots: {len(qa['line_hotspots_xy_dev'])}")
            print(f"   -> {SEAM_QA_PNG}")

    print("12. Saving outputs...")
    rgba = np.zeros((TEX_SIZE, TEX_SIZE, 4), dtype=np.uint8)
    rgba[:, :, :3] = warped
    rgba[:, :, 3] = alpha
    cv2.imwrite(str(OUTPUT_PATH), rgba)
    print(f"   -> {OUTPUT_PATH}")

    debug = warped.copy()
    debug[alpha == 0] = [40, 40, 40]
    cv2.imwrite(str(DEBUG_PATH), debug)
    print(f"   -> {DEBUG_PATH}")

    print("\nDone!")


if __name__ == "__main__":
    main()
