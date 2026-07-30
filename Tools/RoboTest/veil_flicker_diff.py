# -*- coding: utf-8 -*-
# Pixel-diff the two same-pose veil shots: flicker => large per-pixel temporal delta.
import numpy as np
from PIL import Image

A = r"c:/games/Unreal Engine/nice_ink/Saved/Screenshots/WindowsEditor/veilflk_a.png"
B = r"c:/games/Unreal Engine/nice_ink/Saved/Screenshots/WindowsEditor/veilflk_b.png"

a = np.asarray(Image.open(A).convert("RGB"), dtype=np.int16)
b = np.asarray(Image.open(B).convert("RGB"), dtype=np.int16)
if a.shape != b.shape:
    print("SHAPE_MISMATCH", a.shape, b.shape)
else:
    d = np.abs(a - b).max(axis=2)
    # HUD 動畫（頂部橫幅/底部提示）排除：取中央 60% 區域
    h, w = d.shape
    core = d[int(h * 0.2):int(h * 0.8), int(w * 0.1):int(w * 0.9)]
    print("full: mean=%.3f p99=%d max=%d  pct(diff>8)=%.4f%%"
          % (d.mean(), np.percentile(d, 99), d.max(), (d > 8).mean() * 100))
    print("core: mean=%.3f p99=%d max=%d  pct(diff>8)=%.4f%%"
          % (core.mean(), np.percentile(core, 99), core.max(), (core > 8).mean() * 100))
