"""鍵帽四案並排（2026-09-09 二版；user viewport：「這個按鍵不好看」）。

判斷不在腦子裡做：把四種畫法**烘成真的貼圖、貼到真機截圖的三種底上、用實際尺寸**排一張。
底＝從 `Saved/Screenshots/WindowsEditor/` 取的皮膚／榻榻米／障子牆（不是我調的色塊）。
同一行右邊放一顆 Kenney 滑鼠線稿＝**家族對照**（提示列裡它們一定同時出現）。

四案：
  A framed  現行：暗側身 90% ＋外投影（user 打回的那顆）
  B lit     白鍵・光造型：拿掉暗框，厚度全部用光講（頂面漸變＋灰前唇＋淡投影）
  C outline 線稿鍵：不填色，白線圓角框＋加厚的下緣（＝前唇），與滑鼠同一種畫法
  D dark    暗鍵：黑 62% 底＋白字＋上緣一條亮邊

Usage: python -X utf8 Tools/AssetPrep/keycap_variants.py
產出：Saved/UiMock/keycap_variants.png
"""
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SHOTS = os.path.join(ROOT, "Saved", "Screenshots", "WindowsEditor")
SIZE, DISP, SUP = 128, 32.0, 4
K = SIZE / DISP
S = SIZE * SUP
CAP_TOP, CAP_BOT, RADIUS = 1.0, 30.0, 4.0


def rrect(x0, y0, x1, y1, r, w=DISP):
    im = Image.new("L", (S, S), 0)
    ImageDraw.Draw(im).rounded_rectangle(
        [x0 * K * SUP, y0 * K * SUP, x1 * K * SUP - 1, y1 * K * SUP - 1], radius=r * K * SUP, fill=255)
    return np.asarray(im.resize((SIZE, SIZE), Image.LANCZOS)).astype(np.float64) / 255.0


def over(drgb, da, srgb, sa):
    oa = sa + da * (1.0 - sa)
    safe = np.maximum(oa, 1e-6)
    return (srgb * sa[..., None] + drgb * da[..., None] * (1.0 - sa[..., None])) / safe[..., None], oa


def shadow_layer(body, dy, blur, alpha):
    sh = Image.fromarray((body * 255).astype(np.uint8))
    sh = sh.transform(sh.size, Image.AFFINE, (1, 0, 0, 0, 1, -dy * K), resample=Image.BILINEAR)
    sh = sh.filter(ImageFilter.GaussianBlur(blur * K))
    return np.asarray(sh).astype(np.float64) / 255.0 * alpha


def bake(kind):
    ys = np.arange(SIZE)[:, None] / K
    rgb, a = np.zeros((SIZE, SIZE, 3)), np.zeros((SIZE, SIZE))
    body = rrect(0.0, CAP_TOP, DISP, CAP_BOT, RADIUS)

    if kind == "framed":                              # A：現行
        rgb, a = over(rgb, a, np.zeros((SIZE, SIZE, 3)) + 8.0, shadow_layer(body, 1.5, 1.6, 0.38))
        rgb, a = over(rgb, a, np.zeros((SIZE, SIZE, 3)) + 34.0, body * 0.90)
        face = rrect(1.5, 2.5, DISP - 1.5, 26.0, RADIUS - 0.75)
        t = np.clip((ys - 2.5) / 23.5, 0, 1)
        f = np.repeat(252 + (216 - 252) * t, SIZE, axis=1)[..., None].repeat(3, axis=2)
        hi = np.clip(1.0 - (ys - 2.5) / 1.0, 0, 1) ** 1.5
        f = f + (255 - f) * hi[..., None]
        lip = np.clip(1.0 - (26.0 - ys) / 1.5, 0, 1) ** 1.2
        f = f + (190 - f) * lip[..., None]
        rgb, a = over(rgb, a, f, face)

    elif kind == "lit":                               # B：白鍵・光造型（無暗框）
        rgb, a = over(rgb, a, np.zeros((SIZE, SIZE, 3)) + 8.0, shadow_layer(body, 1.2, 1.1, 0.30))
        # 鍵體本身就是白的：上亮下暗，最下面 3px 是「前面那一片」＝灰
        t = np.clip((ys - CAP_TOP) / (CAP_BOT - 3.0 - CAP_TOP), 0, 1)
        f = np.repeat(253 + (223 - 253) * t, SIZE, axis=1)[..., None].repeat(3, axis=2)
        front = np.clip((ys - (CAP_BOT - 3.0)) / 1.0, 0, 1)          # 前唇：一路暗到 168
        f = f + (168 - f) * front[..., None]
        rgb, a = over(rgb, a, f, body)

    elif kind == "outline":                           # C：線稿鍵（與滑鼠同家）
        rgb, a = over(rgb, a, np.zeros((SIZE, SIZE, 3)) + 8.0, shadow_layer(body, 1.0, 1.0, 0.45))
        ring = body - rrect(2.0, CAP_TOP + 2.0, DISP - 2.0, CAP_BOT - 3.0, RADIUS - 1.0)
        ring = np.clip(ring, 0, 1)                    # 下緣自動比其他三邊厚 1px＝前唇
        rgb, a = over(rgb, a, np.zeros((SIZE, SIZE, 3)) + 242.0, ring)

    elif kind == "dark":                              # D：暗鍵＋白字
        rgb, a = over(rgb, a, np.zeros((SIZE, SIZE, 3)) + 8.0, shadow_layer(body, 1.0, 1.2, 0.30))
        rgb, a = over(rgb, a, np.zeros((SIZE, SIZE, 3)) + 10.0, body * 0.62)
        top = rrect(1.0, CAP_TOP, DISP - 1.0, CAP_TOP + 1.0, 0.5)
        rgb, a = over(rgb, a, np.zeros((SIZE, SIZE, 3)) + 242.0, top * 0.55)

    out = np.concatenate([np.clip(rgb, 0, 255), np.clip(a, 0, 1)[..., None] * 255.0], axis=2)
    return Image.fromarray(out.round().astype(np.uint8), "RGBA")


def nine_slice(tex, w, h, m=0.375):
    tm, dm = int(round(SIZE * m)), int(round(h * m))
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    src = [(0, 0, tm, tm), (tm, 0, SIZE - tm, tm), (SIZE - tm, 0, SIZE, tm),
           (0, tm, tm, SIZE - tm), (tm, tm, SIZE - tm, SIZE - tm), (SIZE - tm, tm, SIZE, SIZE - tm),
           (0, SIZE - tm, tm, SIZE), (tm, SIZE - tm, SIZE - tm, SIZE), (SIZE - tm, SIZE - tm, SIZE, SIZE)]
    dst = [(0, 0, dm, dm), (dm, 0, w - dm, dm), (w - dm, 0, w, dm),
           (0, dm, dm, h - dm), (dm, dm, w - dm, h - dm), (w - dm, dm, w, h - dm),
           (0, h - dm, dm, h), (dm, h - dm, w - dm, h), (w - dm, h - dm, w, h)]
    for b, d in zip(src, dst):
        tw, th = max(1, d[2] - d[0]), max(1, d[3] - d[1])
        out.paste(tex.crop(b).resize((tw, th), Image.LANCZOS), (d[0], d[1]))
    return out


def backgrounds(h, w=900):
    """三種真底：皮膚／榻榻米／障子牆（從真機截圖裁，不是我調的色塊）。
    裁切點避開既有 HUD（右緣提示、底部狀態句）——底上有別的字就不是乾淨的對照。"""
    draw = Image.open(os.path.join(SHOTS, "uishot_02_draw_standing00000.png")).convert("RGB")
    lobby = Image.open(os.path.join(SHOTS, "uishot_01_lobby00000.png")).convert("RGB")
    return [("skin 175", draw.crop((620, 620, 620 + w, 620 + h))),
            ("tatami 176", draw.crop((620, 1030, 620 + w, 1030 + h))),
            ("shoji 208", lobby.crop((620, 560, 620 + w, 560 + h)))]


def main():
    CAP_H = 38                      # user 視窗 2560×1380 下的實際鍵帽高（UiScale ≈ 1.28）
    font = ImageFont.truetype(os.path.join(ROOT, "SourceAssets", "Fonts", "NotoSans", "NotoSans-Bold.ttf"), 21)
    vfont = ImageFont.truetype(os.path.join(ROOT, "SourceAssets", "Fonts", "Oswald", "Oswald-Medium.ttf"), 22)
    mouse = Image.open(os.path.join(ROOT, "SourceAssets", "UI", "icons", "ico_mouse_left.png")).resize((34, 34), Image.LANCZOS)

    kinds = [("A framed", "framed"), ("B lit", "lit"), ("C outline", "outline"), ("D dark", "dark")]
    texs = {k: bake(k) for _, k in kinds}

    rowh = CAP_H + 30
    bgs = backgrounds(rowh * len(kinds) + 24)
    sheet = Image.new("RGB", (900 * len(bgs) + 16 * (len(bgs) + 1), rowh * len(kinds) + 68), (24, 22, 20))
    d = ImageDraw.Draw(sheet)

    for bi, (bname, bg) in enumerate(bgs):
        x0 = 16 + bi * (900 + 16)
        d.text((x0, 8), bname, font=vfont, fill=(235, 235, 235))
        sheet.paste(bg, (x0, 34))
        for ki, (label, kind) in enumerate(kinds):
            y = 42 + ki * rowh
            x = x0 + 12
            d.text((x, y + 8), label, font=vfont, fill=(255, 255, 255),
                   stroke_width=2, stroke_fill=(0, 0, 0))
            x += 130
            for lab in ("Q", "G", "WASD"):
                tw = d.textlength(lab, font=font)
                w = max(CAP_H, int(tw + 20))
                cap = nine_slice(texs[kind], w, CAP_H)
                sheet.paste(cap, (x, y), cap)
                ink = (10, 10, 12) if kind in ("framed", "lit") else (242, 242, 242)
                # 字落在頂面中心：A/B 有裙邊要往上讓，C/D 是滿框
                cy = y + (CAP_H * 0.44 if kind in ("framed", "lit") else CAP_H * 0.5)
                d.text((x + w / 2, cy), lab, font=font, fill=ink, anchor="mm")
                x += w + 14
            # 家族對照：同一行右邊就是 Kenney 滑鼠線稿
            sheet.paste(mouse, (x + 20, y + (CAP_H - 34) // 2), mouse)
            d.text((x + 62, y + CAP_H / 2), "= same line", font=vfont, fill=(255, 255, 255),
                   anchor="lm", stroke_width=2, stroke_fill=(0, 0, 0))

    out = os.path.join(ROOT, "Saved", "UiMock", "keycap_variants.png")
    sheet.save(out)
    print("wrote", out, sheet.size)


if __name__ == "__main__":
    main()
