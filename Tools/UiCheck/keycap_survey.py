# -*- coding: utf-8 -*-
"""全站鍵帽普查（2026-09-10；user：「現在所有有出現的按鍵的排版數據去給我調查實際的螢幕顯示的數據」）。

掃 `Saved/Screenshots/WindowsEditor/uiall_*.png`（robo_ui_shots_all 的全流程截圖；PIE 視口 1157×1019
⇒ UI 縮放 0.94），對每張圖的 HUD 區找**白實心鍵帽**與**空心灰鍵帽**，量：
  帽 w×h／帽內字高／字與帽邊左右留白（以墨跡為準；排除貼邊 3px 的投影）
再換算成 1080p 設計值與 user 視窗（2560×1380，×1.28）的實際像素。

只量、不判斷；判斷交給看表的人。
Usage: python -X utf8 Tools/UiCheck/keycap_survey.py [glob]
"""
import glob
import os
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SHOTS = os.path.join(ROOT, "Saved", "Screenshots", "WindowsEditor")
VIEWPORT = (603, 176, 1760, 1195)     # robo 編輯器視窗裡的 PIE 視口（量過：1157×1019）
# **這批截圖的 Slate DPI 縮放是 1.0，不是 1019/1080**（第一版寫 0.94，把所有換算都算錯 6%）：
# 證據＝24px 的鍵帽用 >200 門檻量到 22 ＝ 兩側各被抗鋸齒吃掉 1px，不是被縮成 22.6；
# ENTER 帽量到 65＋2 ＝ 67 ＝ 設計值 55（字）＋12（留白）逐位相符。
UI_SCALE = 1.0
USER_SCALE = 1380 / 1080.0            # user 視窗 2560×1380（他的 Slate DPI 曲線值要另量，這裡只給比例）
# 帽邊界用 >200：帽的最外 1px 是白面與**烘進貼圖的軟投影**（暗）的混色，永遠 <200 ⇒ 量到的帽比真帽
# 每側少 1px（24 量成 22）。試過降到 160：帽邊照樣量不到（那圈混的是暗色不是底色），只多出一堆
# 銭幣圖示的假陽性 ⇒ 維持 200，**留白數字報出來時每側 +1 補回帽邊**。
CAP_THR = 200
EDGE_FIX = 1


def find_caps(gray):
    """回傳 [(kind, x, y, w, h, ink_h, pad_l, pad_r)]；kind = 'white' | 'ghost'。"""
    out = []
    # 白實心帽：>200 的連通塊、帽內有深色字
    lab, n = ndimage.label(gray > CAP_THR)
    for i in range(1, n + 1):
        ys, xs = np.where(lab == i)
        h, w = ys.max() - ys.min() + 1, xs.max() - xs.min() + 1
        if not (18 <= h <= 34 and 18 <= w <= 110):
            continue
        sub = gray[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
        if (sub > CAP_THR).mean() < 0.45:       # 帽面要占大半（排除字本身、障子格）
            continue
        inner = sub[2:-2, 2:-2]
        iy, ix = np.where(inner < 110)
        if len(iy) < 6:
            continue
        ink_h = int(iy.max() - iy.min() + 1)
        # 鍵名字高 13pt ⇒ 12~15（Q 有尾 15）；16 是銭幣圖示的孔、更高的是臉框／面板——都不是鍵
        if not (11 <= ink_h <= 15) or h > 28 or w < 21 or w > h * 4:
            continue
        out.append(("white", int(xs.min()) - EDGE_FIX, int(ys.min()) - EDGE_FIX, int(w) + 2 * EDGE_FIX, int(h) + 2 * EDGE_FIX,
                    ink_h, int(ix.min() + 2) + EDGE_FIX, int(inner.shape[1] - 1 - ix.max() + 2) + EDGE_FIX))
    # 空心灰帽（不可按）：帽內是被投影壓暗的牆（median ~140）＋灰字；抓「比牆暗一截的圓角塊」
    dark = gray < 175
    lab, n = ndimage.label(dark)
    for i in range(1, n + 1):
        ys, xs = np.where(lab == i)
        h, w = ys.max() - ys.min() + 1, xs.max() - xs.min() + 1
        if not (18 <= h <= 30 and 28 <= w <= 110):
            continue
        sub = gray[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
        if (sub < 175).mean() < 0.6:
            continue
        inner = sub[3:-3, 3:-3]
        iy, ix = np.where(inner < 90)          # 灰字比灰帽更暗
        if len(iy) < 6 or not (9 <= iy.max() - iy.min() + 1 <= 17):
            continue
        out.append(("ghost", int(xs.min()), int(ys.min()), int(w), int(h),
                    int(iy.max() - iy.min() + 1), int(ix.min() + 3), int(inner.shape[1] - 1 - ix.max() + 3)))
    return sorted(out, key=lambda t: (t[2], t[1]))


def main():
    pattern = sys.argv[1] if len(sys.argv) > 1 else "uiall_*.png"
    files = sorted(glob.glob(os.path.join(SHOTS, pattern)))
    print("UI 縮放 %.2f（PIE 視口 1019/1080）；user 視窗 ×%.2f" % (UI_SCALE, USER_SCALE))
    print("%-34s %-6s %-9s %-6s %-9s | %-10s %-10s" % ("截圖", "種類", "帽 w×h", "字高", "留白 L/R", "1080p 帽", "user 帽"))
    for f in files:
        im = Image.open(f).convert("L").crop(VIEWPORT)
        g = np.asarray(im).astype(int)
        # 只看 HUD 會出現鍵帽的區：右緣 1/3、底部 1/4、左下角
        caps = [c for c in find_caps(g)
                if c[1] > g.shape[1] * 0.6 or c[2] > g.shape[0] * 0.75 or (c[1] < g.shape[1] * 0.2 and c[2] > g.shape[0] * 0.8)]
        name = os.path.basename(f).replace(".png", "")
        if not caps:
            print("%-34s (no caps)" % name)
            continue
        for kind, x, y, w, h, ih, pl, pr in caps:
            w1080, h1080 = w / UI_SCALE, h / UI_SCALE
            print("%-34s %-6s %3d×%-3d   %-6d %2d/%-6d | %4.0f×%-4.0f  %4.0f×%-4.0f" % (
                name, kind, w, h, ih, pl, pr, w1080, h1080, w1080 * USER_SCALE, h1080 * USER_SCALE))


if __name__ == "__main__":
    main()
