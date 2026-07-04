"""LaMa inpainting wrapper: generous-mask removal, regenerated skin.

Model: Carve/LaMa-ONNX lama_fp32.onnx (LaMa WACV'22, fixed 512px I/O),
runs locally via onnxruntime CPU; the in-game port runs the same ONNX on NNE.

Paradigm note (why this replaced per-pixel classification): LaMa regenerates
plausible textured content from context, so the mask should be GENEROUS over
the object to remove. Chasing pixel-exact masks and smearing neighbor colors
was the failed v4.0 approach (chin residue + textureless fill seams).

lama_inpaint(bgr, hole): crops a context window around the hole, runs LaMa at
512, feather-pastes the regenerated pixels back at native resolution.
"""
import cv2
import numpy as np
from pathlib import Path

MODEL_PATH = Path(__file__).resolve().parent / "models" / "lama_fp32.onnx"
SIDE = 512
CONTEXT = 0.6      # extra context around the hole bbox, in bbox-size units
MIN_HALF = 96      # never crop tighter than this (px) - LaMa needs real context
FEATHER_SIGMA = 3.0

_session = None
_out_is_unit = None   # lazily detected: some exports return 0..1, some 0..255


def _sess():
    global _session
    if _session is None:
        import onnxruntime as ort
        _session = ort.InferenceSession(str(MODEL_PATH),
                                        providers=["CPUExecutionProvider"])
    return _session


def _run(img512_bgr, mask512):
    """One 512x512 LaMa pass. mask512: uint8 {0,255}, 255 = regenerate."""
    global _out_is_unit
    sess = _sess()
    rgb = img512_bgr[..., ::-1].transpose(2, 0, 1)[None].astype(np.float32) / 255.0
    m = (mask512[None, None] > 127).astype(np.float32)
    names = [i.name for i in sess.get_inputs()]
    out = sess.run(None, {names[0]: rgb, names[1]: m})[0][0]
    if _out_is_unit is None:
        _out_is_unit = out.max() <= 1.5
    if _out_is_unit:
        out = out * 255.0
    return np.clip(out.transpose(1, 2, 0)[..., ::-1], 0, 255).astype(np.uint8)


def lama_inpaint(bgr, hole):
    """bgr HxWx3 uint8, hole HxW bool. Returns bgr with the hole regenerated."""
    if hole is None or not hole.any():
        return bgr
    h, w = bgr.shape[:2]
    ys, xs = np.where(hole)
    y0, y1, x0, x1 = int(ys.min()), int(ys.max()), int(xs.min()), int(xs.max())
    cy, cx = (y0 + y1) // 2, (x0 + x1) // 2
    half = int(max(y1 - y0, x1 - x0) * (1 + CONTEXT) / 2) + 16
    half = max(half, MIN_HALF)

    # square window shifted to stay inside the frame where possible
    def clamp_axis(c, size):
        a, b = c - half, c + half
        if a < 0:
            b += -a
            a = 0
        if b > size:
            a -= b - size
            b = size
        return max(a, 0), b

    ya, yb = clamp_axis(cy, h)
    xa, xb = clamp_axis(cx, w)
    crop = bgr[ya:yb, xa:xb]
    mcrop = (hole[ya:yb, xa:xb].astype(np.uint8)) * 255
    ch, cw = crop.shape[:2]

    # reflect-pad to square if the frame was smaller than the window
    pad_y, pad_x = max(0, (cw - ch)), max(0, (ch - cw))
    if pad_y or pad_x:
        crop = cv2.copyMakeBorder(crop, 0, pad_y, 0, pad_x, cv2.BORDER_REFLECT)
        mcrop = cv2.copyMakeBorder(mcrop, 0, pad_y, 0, pad_x, cv2.BORDER_CONSTANT, value=0)

    img512 = cv2.resize(crop, (SIDE, SIDE), interpolation=cv2.INTER_AREA)
    m512 = cv2.resize(mcrop, (SIDE, SIDE), interpolation=cv2.INTER_NEAREST)
    m512 = cv2.dilate(m512, np.ones((5, 5), np.uint8))   # cover resize halo

    out512 = _run(img512, m512)
    out = cv2.resize(out512, crop.shape[1::-1], interpolation=cv2.INTER_CUBIC)
    out = out[:ch, :cw]

    # feathered paste: only hole pixels change, soft edge kills the seam
    feather = cv2.GaussianBlur((mcrop[:ch, :cw] > 127).astype(np.float32),
                               (0, 0), FEATHER_SIGMA)[..., None]
    res = bgr.copy()
    sub = res[ya:yb, xa:xb].astype(np.float32)
    res[ya:yb, xa:xb] = (sub * (1 - feather)
                         + out.astype(np.float32) * feather).astype(np.uint8)
    return res


if __name__ == "__main__":
    import sys
    src, out_path = sys.argv[1], sys.argv[2]
    img = cv2.imread(src)
    h, w = img.shape[:2]
    hole = np.zeros((h, w), bool)
    if len(sys.argv) > 6:
        x0, y0, x1, y1 = map(int, sys.argv[3:7])
        hole[y0:y1, x0:x1] = True
    else:
        hole[h // 3:h // 2, w // 3:w // 2] = True
    cv2.imwrite(out_path, lama_inpaint(img, hole))
    print("LAMA_OK", out_path)
