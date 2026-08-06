"""One-selfie intake for the in-game profile page (2026-08-06, SPEC #52 v4.0e).

Runs the full face pipeline for a single selfie and drops the four artifacts
the game needs into <outdir>:
    face_open.png       (2048^2 RGBA, FaceUV)   <- out/face_texture.png
    face_closed.png     (sleep variant)          <- out/face_texture_eyes_closed.png
    eye_mask_ink.png    (UV0, white=ink allowed) <- sumo bake of out/eye_mask.png
                                                    premultiplied with the static
                                                    hair+fundoshi no-draw layer
    skin_color.json     (linear_rgb)             <- out/skin_color.json

Usage: <venv python> intake_selfie.py <selfie.jpg> <outdir>
Exit 0 + prints DONE_INTAKE on success; nonzero on any failure.
The game launches this with the FacePipeline venv python and polls the process.
"""
import json
import shutil
import subprocess
import sys
from pathlib import Path

import cv2
import numpy as np

BASE = Path(__file__).resolve().parent
SA = BASE.parent.parent / "SourceAssets"
UV_MAP_JSON = BASE / "data" / "sumo_ink_uv_map.json"
SIZE = 2048


def bake_sumo_ink_mask(eye_mask_path: Path, out_path: Path):
    # 與 bake_sumo_ink_masks.py 同一機制（單人版）：FaceUV 眼罩 -> UV0，
    # 再與靜態禁畫（髮+褌）取聯集。sumo FaceUV 已對齊 canonical frame，
    # 任何玩家的 eye_mask.png 都走同一張三角表。
    eye = cv2.imread(str(eye_mask_path), cv2.IMREAD_GRAYSCALE)
    if eye is None:
        raise SystemExit(f"cannot read {eye_mask_path}")
    src_size = eye.shape[0]
    with open(UV_MAP_JSON, encoding="utf-8") as f:
        table = json.load(f)

    hair = cv2.imread(str(SA / "hair_mask.png"), cv2.IMREAD_GRAYSCALE)
    fund = cv2.imread(str(SA / "fundoshi_mask_sharp.png"), cv2.IMREAD_GRAYSCALE)
    if hair is None or fund is None:
        raise SystemExit("hair_mask.png / fundoshi_mask_sharp.png missing in SourceAssets")
    nodraw = np.maximum(cv2.resize(hair, (SIZE, SIZE), interpolation=cv2.INTER_LINEAR),
                        cv2.resize(fund, (SIZE, SIZE), interpolation=cv2.INTER_AREA))
    static_allowed = 255 - nodraw

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


def main():
    if len(sys.argv) < 3:
        raise SystemExit("usage: intake_selfie.py <selfie> <outdir>")
    selfie = Path(sys.argv[1])
    outdir = Path(sys.argv[2])
    if not selfie.is_file():
        raise SystemExit(f"selfie not found: {selfie}")
    outdir.mkdir(parents=True, exist_ok=True)
    log = outdir / "intake_log.txt"
    log.write_text(f"argv={sys.argv}\ncwd={Path.cwd()}\n", encoding="utf-8")

    def append_log(text):
        with open(log, "a", encoding="utf-8") as f:
            f.write(text + "\n")

    try:
        # 1) 自拍 -> 臉貼圖（v7 管線原樣；輸出到 out/）——stdout/stderr 全記檔
        #（遊戲以隱藏行程啟動＝console 輸出無處可去，失敗要能事後驗屍）
        r = subprocess.run([sys.executable, str(BASE / "selfie_to_face_texture.py"), str(selfie)],
                           cwd=str(BASE), capture_output=True, text=True, encoding="utf-8",
                           errors="replace")
        append_log((r.stdout or "")[-4000:])
        append_log((r.stderr or "")[-4000:])
        if r.returncode != 0:
            raise SystemExit(f"selfie_to_face_texture failed ({r.returncode})")

        out = BASE / "out"
        for name in ("face_texture.png", "face_texture_eyes_closed.png", "eye_mask.png", "skin_color.json"):
            if not (out / name).is_file():
                raise SystemExit(f"pipeline output missing: {name}")

        # 2) 眼罩 -> sumo UV0（含靜態禁畫預乘）
        bake_sumo_ink_mask(out / "eye_mask.png", outdir / "eye_mask_ink.png")

        # 3) 成品搬運
        shutil.copyfile(out / "face_texture.png", outdir / "face_open.png")
        shutil.copyfile(out / "face_texture_eyes_closed.png", outdir / "face_closed.png")
        shutil.copyfile(out / "skin_color.json", outdir / "skin_color.json")

        # 4) 縮圖（臉庫列表用）：照 HUD FaceTok 的 FaceUV 裁切框 (0.30,0.22)+(0.40,0.40)
        #    取臉區，alpha 疊在量測膚色底上（外圈透明＝直貼會變黑塊）
        face = cv2.imread(str(out / "face_texture.png"), cv2.IMREAD_UNCHANGED)
        if face is not None and face.shape[2] == 4:
            h, w = face.shape[:2]
            crop = face[int(0.22 * h):int(0.62 * h), int(0.30 * w):int(0.70 * w)]
            with open(out / "skin_color.json", encoding="utf-8") as f:
                skin = json.load(f)["srgb_rgb_255"]
            bg = np.zeros((crop.shape[0], crop.shape[1], 3), np.float32)
            bg[:] = (skin[2], skin[1], skin[0])  # cv2=BGR
            a = crop[:, :, 3:4].astype(np.float32) / 255.0
            comp = crop[:, :, :3].astype(np.float32) * a + bg * (1.0 - a)
            thumb = cv2.resize(comp.astype(np.uint8), (256, 256), interpolation=cv2.INTER_AREA)
            cv2.imwrite(str(outdir / "thumb.png"), thumb)
    except BaseException:
        import traceback
        append_log(traceback.format_exc())
        raise

    append_log("DONE_INTAKE")
    print("DONE_INTAKE")


if __name__ == "__main__":
    main()
