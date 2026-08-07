"""MediaPipe FaceLandmarker 的純 ONNX 重現（M2 對賬儀器＋C++ 移植藍本）。

模型＝task 解包後 tf2onnx 轉出的同權重 ONNX（face_detector.onnx 128²/896 anchor、
face_landmarks.onnx 256²/478 點）——索引語義與現行管線 100% 相同。

這份 python 是 C++ 版（NiceInkFaceLandmarks.cpp）的行為規格：每一步都要能
逐數字對上。對賬：python mp_onnx_landmarks.py 跑全部 test_selfies，
比對 mediapipe 官方 FaceLandmarker 的 478 點像素座標。

MediaPipe 圖參數出處（face_detector_graph / face_landmarks_detector_graph）：
  - detector：letterbox 到 128²、[-1,1] 正規化、SSD anchors（strides 8/16/16/16、
    16×16×2+8×8×6=896、fixed_anchor_size）、score=sigmoid(clip ±100)、
    加權 NMS（min_suppression 0.3、min_score 0.5）
  - ROI：旋轉=兩眼 keypoint(0,1) 向量、scale 1.5、square long side
  - landmarks：旋轉裁切 256²、/255 正規化、輸出 478×3（crop 像素域）→映回原圖
"""
import sys
from pathlib import Path

import cv2
import numpy as np

BASE = Path(__file__).resolve().parent
MODELS = BASE / "models"
DET_ONNX = MODELS / "face_detector.onnx"
LM_ONNX = MODELS / "face_landmarks.onnx"

DET_SIDE = 128
LM_SIDE = 256
ROI_SCALE = 1.5
MIN_SCORE = 0.5
NMS_THRESH = 0.3

_det_sess = None
_lm_sess = None


def _sessions():
    global _det_sess, _lm_sess
    if _det_sess is None:
        import onnxruntime as ort
        _det_sess = ort.InferenceSession(str(DET_ONNX), providers=["CPUExecutionProvider"])
        _lm_sess = ort.InferenceSession(str(LM_ONNX), providers=["CPUExecutionProvider"])
    return _det_sess, _lm_sess


def gen_anchors():
    """SSD anchors for BlazeFace short-range 128 (fixed_anchor_size=true).
    strides [8,16,16,16] -> 16x16 grid x2 anchors + 8x8 grid x(2+2+2)=6 anchors."""
    anchors = []
    # layer spec: (feature map size, anchors per cell)
    for fm, per_cell in ((16, 2), (8, 6)):
        for y in range(fm):
            for x in range(fm):
                cx = (x + 0.5) / fm
                cy = (y + 0.5) / fm
                for _ in range(per_cell):
                    anchors.append((cx, cy))
    return np.array(anchors, np.float32)  # (896, 2)


_ANCHORS = gen_anchors()


def detect_face(bgr):
    """BlazeFace on the full image. Returns (box xywh normalized, keypoints (6,2)
    normalized, score) of the best face, or None."""
    det, _ = _sessions()
    h, w = bgr.shape[:2]
    # letterbox to square
    side = max(h, w)
    pad_x = (side - w) // 2
    pad_y = (side - h) // 2
    sq = cv2.copyMakeBorder(bgr, pad_y, side - h - pad_y, pad_x, side - w - pad_x,
                            cv2.BORDER_CONSTANT, value=(0, 0, 0))
    img = cv2.resize(sq, (DET_SIDE, DET_SIDE), interpolation=cv2.INTER_LINEAR)
    rgb = cv2.cvtColor(img, cv2.COLOR_BGR2RGB).astype(np.float32) / 127.5 - 1.0
    reg, cls = det.run(None, {det.get_inputs()[0].name: rgb[None]})
    reg, cls = reg[0], cls[0, :, 0]

    score = 1.0 / (1.0 + np.exp(-np.clip(cls, -100, 100)))
    keep = score >= MIN_SCORE
    if not keep.any():
        return None
    reg, sc, anc = reg[keep], score[keep], _ANCHORS[keep]

    # decode (fixed anchor size 1, scale 128)
    cx = reg[:, 0] / DET_SIDE + anc[:, 0]
    cy = reg[:, 1] / DET_SIDE + anc[:, 1]
    bw = reg[:, 2] / DET_SIDE
    bh = reg[:, 3] / DET_SIDE
    kps = reg[:, 4:16].reshape(-1, 6, 2) / DET_SIDE + anc[:, None, :]
    boxes = np.stack([cx - bw / 2, cy - bh / 2, bw, bh], axis=1)  # x,y,w,h

    # weighted NMS (mediapipe OVERLAP_SIMILARITY + weighted blending)
    order = np.argsort(-sc)
    picked = []
    used = np.zeros(len(sc), bool)
    for i in order:
        if used[i]:
            continue
        x0a, y0a, wa, ha = boxes[i]
        x1a, y1a = x0a + wa, y0a + ha
        cluster = [i]
        for j in order:
            if j == i or used[j]:
                continue
            x0b, y0b, wb, hb = boxes[j]
            x1b, y1b = x0b + wb, y0b + hb
            ix = max(0.0, min(x1a, x1b) - max(x0a, x0b))
            iy = max(0.0, min(y1a, y1b) - max(y0a, y0b))
            inter = ix * iy
            union = wa * ha + wb * hb - inter
            if union > 0 and inter / union > NMS_THRESH:
                cluster.append(j)
        for j in cluster:
            used[j] = True
        wsum = sc[cluster].sum()
        blended_box = (boxes[cluster] * sc[cluster, None]).sum(axis=0) / wsum
        blended_kp = (kps[cluster] * sc[cluster, None, None]).sum(axis=0) / wsum
        picked.append((blended_box, blended_kp, float(sc[cluster].max())))
    box, kp, s = max(picked, key=lambda p: p[2])

    # un-letterbox: normalized square coords -> normalized image coords
    def unpad(pts):
        out = pts.copy()
        out[..., 0] = (out[..., 0] * side - pad_x) / w
        out[..., 1] = (out[..., 1] * side - pad_y) / h
        return out

    box_xy = unpad(box[None, :2])[0]
    box_wh = np.array([box[2] * side / w, box[3] * side / h], np.float32)
    return np.concatenate([box_xy, box_wh]), unpad(kp), s


def roi_from_detection(box, kp, w, h):
    """DetectionsToRects + RectTransformation: center/size from the box,
    rotation from the eye keypoints (0=left eye, 1=right eye), scale 1.5,
    square around the long side. Returns (cx, cy, side, rot) in pixels."""
    cx = (box[0] + box[2] / 2) * w
    cy = (box[1] + box[3] / 2) * h
    bw = box[2] * w
    bh = box[3] * h
    x0, y0 = kp[0, 0] * w, kp[0, 1] * h
    x1, y1 = kp[1, 0] * w, kp[1, 1] * h
    rot = -np.arctan2(-(y1 - y0), x1 - x0)   # target angle 0
    # normalize to [-pi, pi]
    rot = rot - 2 * np.pi * np.floor((rot + np.pi) / (2 * np.pi))
    side = max(bw, bh) * ROI_SCALE
    return cx, cy, side, rot


def crop_roi(bgr, cx, cy, side, rot):
    """Rotated square crop -> LM_SIDE x LM_SIDE (bilinear)."""
    cos, sin = np.cos(rot), np.sin(rot)
    s = side / LM_SIDE
    # maps crop pixel (u,v) -> image (x,y)
    # x = cx + cos*s*(u - C) - sin*s*(v - C);  C = LM_SIDE/2
    C = LM_SIDE / 2.0
    M = np.array([
        [cos * s, -sin * s, cx - cos * s * C + sin * s * C],
        [sin * s, cos * s, cy - sin * s * C - cos * s * C],
    ], np.float32)
    crop = cv2.warpAffine(bgr, M, (LM_SIDE, LM_SIDE),
                          flags=cv2.INTER_LINEAR | cv2.WARP_INVERSE_MAP,
                          borderMode=cv2.BORDER_CONSTANT, borderValue=(0, 0, 0))
    return crop, M


def detect_landmarks_onnx(bgr):
    """Full chain. Returns (478, 2) pixel coords, or None if no face."""
    _, lm_sess = _sessions()
    h, w = bgr.shape[:2]
    det = detect_face(bgr)
    if det is None:
        return None
    box, kp, _ = det
    cx, cy, side, rot = roi_from_detection(box, kp, w, h)
    crop, M = crop_roi(bgr, cx, cy, side, rot)
    rgb = cv2.cvtColor(crop, cv2.COLOR_BGR2RGB).astype(np.float32) / 255.0
    outs = lm_sess.run(None, {lm_sess.get_inputs()[0].name: rgb[None]})
    # outputs by size: 1434 = landmarks, 1 = presence score
    lm_flat = next(o for o in outs if o.size == 1434).reshape(478, 3)
    pts = lm_flat[:, :2]
    ones = np.ones((478, 1), np.float32)
    return (np.hstack([pts, ones]) @ M.T).astype(np.float64)


def detect_landmarks_mediapipe(bgr):
    import mediapipe as mp
    from mediapipe.tasks import python as mp_python
    from mediapipe.tasks.python import vision as mp_vision
    h, w = bgr.shape[:2]
    rgb = cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB)
    mp_img = mp.Image(image_format=mp.ImageFormat.SRGB, data=np.ascontiguousarray(rgb))
    opts = mp_vision.FaceLandmarkerOptions(
        base_options=mp_python.BaseOptions(
            model_asset_path=str(MODELS / "face_landmarker.task")),
        num_faces=1)
    det = mp_vision.FaceLandmarker.create_from_options(opts)
    result = det.detect(mp_img)
    det.close()
    if not result.face_landmarks:
        return None
    return np.array([(p.x * w, p.y * h) for p in result.face_landmarks[0]],
                    dtype=np.float64)


def main():
    paths = sys.argv[1:] or sorted((BASE / "test_selfies").glob("*.jpg"))
    print(f"{'selfie':<42}{'mean px':>9}{'p95 px':>9}{'max px':>9}  (478 pts, vs mediapipe)")
    worst = 0.0
    for p in paths:
        bgr = cv2.imread(str(p))
        if bgr is None:
            continue
        # match pipeline preprocessing: downscale to MAX_SELFIE_SIDE
        h, w = bgr.shape[:2]
        if max(h, w) > 1600:
            s = 1600 / max(h, w)
            bgr = cv2.resize(bgr, (int(w * s), int(h * s)), interpolation=cv2.INTER_AREA)
        ref = detect_landmarks_mediapipe(bgr)
        ours = detect_landmarks_onnx(bgr)
        name = Path(p).name[:40]
        if ref is None and ours is None:
            print(f"{name:<42}{'--- no face (both) ---':>30}")
            continue
        if ref is None or ours is None:
            print(f"{name:<42}  DISAGREE: mp={'Y' if ref is not None else 'N'} onnx={'Y' if ours is not None else 'N'}")
            worst = 999
            continue
        d = np.linalg.norm(ref - ours, axis=1)
        print(f"{name:<42}{d.mean():>9.2f}{np.percentile(d, 95):>9.2f}{d.max():>9.2f}")
        worst = max(worst, float(d.mean()))
    print("PARITY", "OK" if worst < 2.0 else "FAIL", f"(worst mean {worst:.2f}px)")


if __name__ == "__main__":
    main()
