"""BiSeNet (79999_iter.pth) -> ONNX，遊戲內建臉管線（SPEC #52 裁決①）第一塊磚。

輸出 models/bisenet_512.onnx（輸入 1x3x512x512 float32、ImageNet 正規化後；
輸出 1x19x512x512 logits——與 selfie_to_face_texture.parse_selfie 同一契約），
並用測試自拍做 pth vs onnx 數值對賬（label map 逐像素一致率）。

Usage: <venv python> export_bisenet_onnx.py [test_selfie]
"""
import sys
from pathlib import Path

import cv2
import numpy as np
import torch

from face_parsing.model import BiSeNet

BASE = Path(__file__).resolve().parent
PTH = BASE / "models" / "79999_iter.pth"
OUT = BASE / "models" / "bisenet_512.onnx"

MEAN = np.array([0.485, 0.456, 0.406], np.float32)
STD = np.array([0.229, 0.224, 0.225], np.float32)


def preprocess(bgr):
    img = cv2.resize(bgr, (512, 512), interpolation=cv2.INTER_LINEAR)
    rgb = cv2.cvtColor(img, cv2.COLOR_BGR2RGB).astype(np.float32) / 255.0
    rgb = (rgb - MEAN) / STD
    return rgb.transpose(2, 0, 1)[None]  # 1x3x512x512


def main():
    net = BiSeNet(n_classes=19)
    net.load_state_dict(torch.load(str(PTH), map_location="cpu"))
    net.eval()

    dummy = torch.zeros(1, 3, 512, 512, dtype=torch.float32)
    torch.onnx.export(
        net, dummy, str(OUT),
        input_names=["image"], output_names=["logits"],
        # 固定 512 輸入（管線本來就縮到 512）；opset 17＝ORT/NNE 穩定域
        opset_version=17, do_constant_folding=True)

    # torch 新匯出器把權重外掛成 .onnx.data——合併回單檔（NNE 資產要自包含）
    import onnx
    m = onnx.load(str(OUT))
    data = OUT.with_suffix(".onnx.data")
    if data.exists():
        data.unlink()
    onnx.save(m, str(OUT), save_as_external_data=False)
    print(f"exported {OUT} ({OUT.stat().st_size / 1e6:.1f} MB, self-contained)")

    # 對賬：pth vs onnx 的 label map 一致率
    selfie_path = sys.argv[1] if len(sys.argv) > 1 else str(
        BASE / "test_selfies" / "7AF4F8FF-4E03-4BE1-9F24-C89180F884FE.jpg")
    bgr = cv2.imread(selfie_path)
    if bgr is None:
        raise SystemExit(f"cannot read {selfie_path}")
    x = preprocess(bgr)

    with torch.no_grad():
        ref = net(torch.from_numpy(x))[0].numpy()  # BiSeNet 回傳 tuple、[0]=主輸出
    ref_labels = ref.argmax(1)[0]

    import onnxruntime as ort
    sess = ort.InferenceSession(str(OUT), providers=["CPUExecutionProvider"])
    out = sess.run(None, {"image": x.astype(np.float32)})[0]
    onnx_labels = out.argmax(1)[0]

    agree = float((ref_labels == onnx_labels).mean())
    print(f"label agreement pth vs onnx: {agree * 100:.3f}%")
    if agree < 0.999:
        raise SystemExit("PARITY FAIL")
    print("DONE_BISENET_ONNX")


if __name__ == "__main__":
    main()
