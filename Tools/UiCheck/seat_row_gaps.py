# -*- coding: utf-8 -*-
"""量 ESC 首頁席位列的**墨跡間距**（吃一張真機截圖，不開引擎）。

§14.3：「一律量墨跡到墨跡，不准用行框高推位置」。程式裡寫的 12 是**盒子**的間距；
玩家看到的是**墨跡**的間距，兩者不相等——臉是去背的頭形（80 的盒子四周本來就是透明的）、
文字有 side bearing、靴子是實心圖貼滿它的盒子。這支就是把那個差量出來。

作法：UI 的墨（文字、臉、圖示）畫在 62% 暗底**之上**＝亮；實景過暗底再過列底 25% ⇒ 最亮約 73。
所以 L>120 可以乾淨地只取到 UI 的墨。取一列的水平投影，切成連續的墨塊，報塊與塊之間的空白。

Usage: python -X utf8 seat_row_gaps.py <shot.png>
"""
import sys
import numpy as np
from PIL import Image

INK = 120.0   # UI 墨的亮度門檻（實景過 62% 暗底＋列底 25% 後最亮 ~73）


def runs(mask, min_len=2):
    out, s = [], None
    for i, v in enumerate(mask):
        if v and s is None:
            s = i
        elif not v and s is not None:
            if i - s >= min_len:
                out.append((s, i))
            s = None
    if s is not None and len(mask) - s >= min_len:
        out.append((s, len(mask)))
    return out


def main(path):
    im = Image.open(path).convert("RGB")
    W, H = im.size
    a = np.asarray(im).astype(np.float32)
    L = 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]
    ink = L > INK

    # 中央帶：避開左欄的動詞與左下 ESC 鍵帽
    cx0, cx1 = int(W * 0.30), int(W * 0.72)
    band = ink[:, cx0:cx1]

    # 有臉的列＝縱向墨跡厚度 >= 60px（臉 80×縮放）；房碼／PLAYERS 那幾行厚度只有 20~45
    ys = runs(band.sum(axis=1) > 0, min_len=8)
    rows = [(y0, y1) for (y0, y1) in ys if y1 - y0 >= 60]
    print(f"畫面 {W}x{H}；中央帶 x{cx0}..{cx1}；找到 {len(rows)} 列有臉的席位")
    if not rows:
        return 1

    # 列的框邊：列寬 560、整塊置中於螢幕 ⇒ 由**列距**反推縮放，再從畫面中軸推兩側邊界。
    # （列底只有黑 25%＝沒有固定顏色可認，但列距是幾何、量得準：列高 104 ＋ 列距 8 ＝ 112 單位。）
    frame = None
    if len(rows) >= 2:
        pitch = rows[1][0] - rows[0][0]
        scale = pitch / 112.0
        half = 560.0 * scale * 0.5
        frame = (W * 0.5 - half, W * 0.5 + half)
        print(f"  列距 {pitch}px ⇒ scale {scale:.3f}；列框 x {frame[0]:.0f}..{frame[1]:.0f}（寬 {2 * half:.0f}）")

    for k, (y0, y1) in enumerate(rows):
        seg = band[y0:y1]
        xr = runs(seg.sum(axis=0) > 0, min_len=3)
        xs = [(cx0 + s, cx0 + e) for (s, e) in xr]
        gaps = [xs[i + 1][0] - xs[i][1] for i in range(len(xs) - 1)]
        widths = [e - s for (s, e) in xs]
        print(f"  第 {k + 1} 列  y{y0}..{y1}（高 {y1 - y0}）")
        if frame and xs:
            print(f"     框左→首墨 {xs[0][0] - frame[0]:.0f}   末墨→框右 {frame[1] - xs[-1][1]:.0f}")
        print(f"     墨塊寬 {widths}")
        print(f"     墨塊之間 {gaps}")
    print("\n（上面全是**畫面上量到的墨跡**。程式裡的盒子間距＝框邊 12／元件間 16／數字↔銭 8；")
    print("  差額＝去背頭形的透明邊、字的 side bearing、按鈕自己的 ContentPadding。）")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
