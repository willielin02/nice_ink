# -*- coding: utf-8 -*-
"""驗收 user 問的兩件事：①每顆鍵裡的字高一不一致 ②字在帽裡的左右／上下留白一不一致。
量真機截圖：對每顆白帽找帽的包圍盒，再找帽內深色像素（字）的包圍盒。
"""
import numpy as np
from PIL import Image
from scipy import ndimage

SH = r"C:/games/Unreal Engine/nice_ink/Saved/Screenshots/WindowsEditor/"


def caps_with_ink(path, box, cap_thr=200, ink_thr=110):
    a = np.asarray(Image.open(path).convert("L")).astype(int)
    x0, y0, x1, y1 = box
    s = a[y0:y1, x0:x1]
    lab, n = ndimage.label(s > cap_thr)
    rows = []
    for i in range(1, n + 1):
        ys, xs = np.where(lab == i)
        h, w = ys.max() - ys.min() + 1, xs.max() - xs.min() + 1
        if not (16 <= h <= 34 and 16 <= w <= 60):
            continue
        # 用填滿的包圍盒切出帽（帽內的字會把連通塊打洞，所以看 bbox 不看 mask）
        sub = s[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
        iy, ix = np.where(sub < ink_thr)
        if len(iy) < 6:
            continue
        rows.append(dict(cap=(int(w), int(h)), ink_h=int(iy.max() - iy.min() + 1), ink_w=int(ix.max() - ix.min() + 1),
                         pad_l=int(ix.min()), pad_r=int(w - 1 - ix.max()), pad_t=int(iy.min()), pad_b=int(h - 1 - iy.max()),
                         at=(int(x0 + xs.min()), int(y0 + ys.min()))))
    return sorted(rows, key=lambda r: r["at"][1])


for title, path, box in [
    ("作畫站著 F／G", SH + "uishot_02_draw_standing00000.png", (1600, 640, 1790, 900)),
    ("作畫鎖定 Q／G／WASD", SH + "uishot_03_draw_locked00000.png", (1000, 600, 1790, 1195)),
    ("大廳 ESC", SH + "uishot_01_lobby00000.png", (1600, 700, 1790, 830)),
    ("選單 ESC", SH + "menu2_settings00000.png", (40, 990, 400, 1050)),
]:
    print("===", title, "===")
    for r in caps_with_ink(path, box):
        print("   cap %dx%d  字 %dx%d  左右留白 %d/%d  上下留白 %d/%d  at %s" % (
            r["cap"][0], r["cap"][1], r["ink_w"], r["ink_h"], r["pad_l"], r["pad_r"], r["pad_t"], r["pad_b"], r["at"]))
