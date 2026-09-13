# -*- coding: utf-8 -*-
"""頭像亭 v5 校準對照圖（2026-09-11）。

把 Saved/Portraits/<set>/*.png（NiPortraitSweep 的輸出）逐組拼成一列，每張以 **實際顯示尺寸**
（64 設計 px × user 縮放 1.28 ≈ 82px）與 2× 放大各排一行，貼在大廳席位格同款的底（黑 25% 圓角）
與榻榻米色上。**選外觀不要用形容詞，要把候選並排在它真正會出現的尺寸上。**

Usage: python -X utf8 Tools/UiCheck/portrait_sheet.py [--scale 1.28] [--out Saved/UiMock/portrait_sweep.png]
"""
import glob
import os
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "Saved", "Portraits")
OUT = os.path.join(ROOT, "Saved", "UiMock", "portrait_sweep.png")
TATAMI = (168, 166, 138)
GROUND_A = 0.25
FACE = 64.0     # NiUi::FaceM
TILE = 80.0     # NiUi::SeatFrame
RADIUS = 4.0    # NiUi::Radius


def tile(portrait, scale):
    t = int(round(TILE * scale)); f = int(round(FACE * scale)); pad = (t - f) // 2
    im = Image.new("RGBA", (t, t), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, t - 1, t - 1], radius=RADIUS * scale, fill=(0, 0, 0, int(255 * GROUND_A)))
    p = portrait.convert("RGBA").resize((f, f), Image.LANCZOS)
    im.alpha_composite(p, (pad, pad))
    return im


def main():
    scale = 1.28
    out = OUT
    args = sys.argv[1:]
    if "--scale" in args:
        scale = float(args[args.index("--scale") + 1])
    if "--out" in args:
        out = args[args.index("--out") + 1]
    sets = sorted(d for d in os.listdir(SRC) if os.path.isdir(os.path.join(SRC, d)))
    if "--sets" in args:   # 只拼這幾組（逗號分隔）
        want = args[args.index("--sets") + 1].split(",")
        sets = [s for s in want if s in sets]
    if not sets:
        print("no sets in", SRC); return 2
    font_path = os.path.join(ROOT, "SourceAssets", "Fonts", "NotoSans", "NotoSans-Regular.ttf")
    font = ImageFont.truetype(font_path, 18) if os.path.exists(font_path) else ImageFont.load_default()

    rows = []
    for s in sets:
        files = sorted(glob.glob(os.path.join(SRC, s, "*.png")))
        if not files:
            continue
        imgs = [Image.open(f) for f in files]
        row1 = [tile(im, scale) for im in imgs]              # 實際顯示尺寸
        row2 = [tile(im, scale * 2) for im in imgs]          # 2× 檢視
        rows.append((s, row1, row2))

    gap = 14
    w = max(sum(t.width for t in r2) + gap * (len(r2) + 1) for _, _, r2 in rows) + 260
    h = 20
    for _, r1, r2 in rows:
        h += r1[0].height + 8 + r2[0].height + 34
    sheet = Image.new("RGB", (w, h), TATAMI)
    d = ImageDraw.Draw(sheet)
    y = 10
    for name, r1, r2 in rows:
        d.text((12, y + 4), name, font=font, fill=(20, 20, 20))
        x = 250
        for t in r1:
            sheet.paste(t, (x, y), t); x += t.width + gap
        y += r1[0].height + 8
        x = 250
        for t in r2:
            sheet.paste(t, (x, y), t); x += t.width + gap
        y += r2[0].height + 26
    os.makedirs(os.path.dirname(out), exist_ok=True)
    sheet.save(out)
    print("sheet:", out, sheet.size, "sets:", len(rows))
    return 0


if __name__ == "__main__":
    sys.exit(main())
