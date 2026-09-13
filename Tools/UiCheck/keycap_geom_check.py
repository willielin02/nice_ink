# -*- coding: utf-8 -*-
"""驗收：單鍵是否全等方形、長鍵是否落在 1.75 上限內。量真機截圖，不看程式碼。"""
import numpy as np
from PIL import Image
from scipy import ndimage

SHOTS = r"C:/games/Unreal Engine/nice_ink/Saved/Screenshots/WindowsEditor/"


def caps(path, box, light=True, thr=195, fill_min=0.55):
    a = np.asarray(Image.open(path).convert("L")).astype(int)
    x0, y0, x1, y1 = box
    s = a[y0:y1, x0:x1]
    lab, n = ndimage.label(s > thr if light else s < thr)
    out = []
    for i in range(1, n + 1):
        ys, xs = np.where(lab == i)
        h, w = ys.max() - ys.min() + 1, xs.max() - xs.min() + 1
        if not (14 <= h <= 40 and w >= 14):
            continue
        m = (lab[ys.min():ys.max() + 1, xs.min():xs.max() + 1] == i)
        if m.mean() < fill_min:
            continue
        out.append((int(w), int(h), round(w / h, 2), int(x0 + xs.min()), int(y0 + ys.min())))
    return sorted(out, key=lambda t: t[4])


print("=== 作畫相位（F／G 兩顆單鍵；期待兩顆逐位相同、w/h=1.00）===")
for c in caps(SHOTS + "uishot_02_draw_standing00000.png", (1600, 640, 1790, 900)):
    print("   w=%d h=%d w/h=%.2f at (%d,%d)" % c)

print()
print("=== 大廳（ENTER 不可按＝深帽／ESC 可按＝白帽；期待 w/h ≤ 1.75）===")
for c in caps(SHOTS + "uishot_01_lobby00000.png", (1600, 700, 1790, 800), light=False, thr=90):
    print("   ENTER(暗) w=%d h=%d w/h=%.2f at (%d,%d)" % c)
for c in caps(SHOTS + "uishot_01_lobby00000.png", (1600, 760, 1790, 830), light=True, thr=195):
    print("   ESC(白)   w=%d h=%d w/h=%.2f at (%d,%d)" % c)

print()
print("=== 主選單 ESC ===")
for c in caps(SHOTS + "menu2_settings00000.png", (40, 990, 400, 1050)):
    print("   w=%d h=%d w/h=%.2f at (%d,%d)" % c)
