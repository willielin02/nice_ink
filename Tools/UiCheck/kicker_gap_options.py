"""大廳左下房碼區塊的間距候選並排（2026-09-08；user：「8、24 這兩個數字是哪裡來的？」）。

用途：業界只規定「從級距表取值」＋「組內明顯小於組外」，**沒有規定用哪一級**。
所以候選只有幾個（Carbon token 8／12／16），最後一步只能用眼睛選。這支把候選並排成一張圖，
不用開引擎、一輪 3 秒——把「我編一套說法」換成「你看一眼決定」。

輸出 Saved/UiMock/kicker_gap_options.png（2560×1380 視窗的實際像素尺寸）。
Usage: python -X utf8 Tools/UiCheck/kicker_gap_options.py
"""
import os
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OSW = os.path.join(ROOT, "SourceAssets", "Fonts", "Oswald", "Oswald-Medium.ttf")
OSWB = os.path.join(ROOT, "SourceAssets", "Fonts", "Oswald", "Oswald-Bold.ttf")

UI = 1380 / 1080.0            # 與真機同一個 UiScale
PT = 96.0 / 72.0              # Slate 的字級是 pt
LABEL_PX = int(round(13 * PT * UI))   # 小標
CODE_PX = int(round(64 * PT * UI))    # 房碼
TRACK = 0.050                 # 展示體字距 50‰

LABEL = "FRIENDS JOIN WITH THIS CODE"
CODE = "W K X M"
COPY = "COPY"
# 候選＝Carbon spacing token（$spacing-03/04/05）；外距固定 24（$spacing-06）
INNER = [8, 12, 16]
OUTER = 24


def tracked(draw, font, text, x, y, fill, track_px):
    for ch in text:
        draw.text((x, y), ch, font=font, fill=fill)
        x += draw.textlength(ch, font=font) + track_px
    return x


def ink_box(font, text, track_px):
    """墨跡框：逐字量 bbox 聯集（含字距）。"""
    img = Image.new("L", (4000, 400), 0)
    d = ImageDraw.Draw(img)
    x = 50.0
    for ch in text:
        d.text((x, 100), ch, font=font, fill=255)
        x += d.textlength(ch, font=font) + track_px
    return img.getbbox()   # (l, t, r, b)


def panel(inner):
    W, H = 620, 460
    im = Image.new("RGB", (W, H), (58, 44, 34))
    d = ImageDraw.Draw(im)
    # 底部漸層（模擬 DrawBottomScrim）
    for y in range(H):
        t = max(0.0, (y - H * 0.25) / (H * 0.75))
        a = int(90 * (t * t * (3 - 2 * t)))
        d.line([(0, y), (W, y)], fill=(58 - a // 3, 44 - a // 4, 34 - a // 5))

    f_lab = ImageFont.truetype(OSW, LABEL_PX)
    f_code = ImageFont.truetype(OSWB, CODE_PX)
    tr_lab = LABEL_PX * TRACK
    tr_code = CODE_PX * TRACK

    lab_bb = ink_box(f_lab, LABEL, tr_lab)
    code_bb = ink_box(f_code, CODE, tr_code)
    lab_h = lab_bb[3] - lab_bb[1]
    code_h = code_bb[3] - code_bb[1]

    X = 24
    key_h = int(round(32 * UI))
    key_top = H - 24 - key_h
    code_ink_bottom = key_top - round(OUTER * UI)
    code_ink_top = code_ink_bottom - code_h
    lab_ink_bottom = code_ink_top - round(inner * UI)
    lab_ink_top = lab_ink_bottom - lab_h

    # 畫：draw 的 y 是「畫布上文字繪製原點」，扣掉 ink bbox 的 top 才是墨跡頂對齊
    tracked(d, f_lab, LABEL, X, lab_ink_top - (lab_bb[1] - 100), (196, 196, 196), tr_lab)
    tracked(d, f_code, CODE, X, code_ink_top - (code_bb[1] - 100), (242, 242, 242), tr_code)

    # 鍵帽
    kw = int(round(32 * UI))
    d.rounded_rectangle([X, key_top, X + kw, key_top + key_h], radius=int(4 * UI),
                        fill=(242, 242, 242))
    f_key = ImageFont.truetype(OSWB, int(round(18 * PT * UI)))
    kb = d.textbbox((0, 0), "C", font=f_key)
    d.text((X + (kw - (kb[2] - kb[0])) / 2 - kb[0], key_top + (key_h - (kb[3] - kb[1])) / 2 - kb[1]),
           "C", font=f_key, fill=(10, 10, 12))
    f_v = ImageFont.truetype(OSW, LABEL_PX)
    vb = d.textbbox((0, 0), COPY, font=f_v)
    tracked(d, f_v, COPY, X + kw + int(8 * UI),
            key_top + (key_h - (vb[3] - vb[1])) / 2 - vb[1], (242, 242, 242), LABEL_PX * TRACK)

    # 標題
    f_t = ImageFont.truetype(OSWB, 26)
    d.text((X, 14), "inner %d  outer %d   (%d : %d px)" % (
        inner, OUTER, round(inner * UI), round(OUTER * UI)), font=f_t, fill=(255, 214, 120))
    return im


ims = [panel(v) for v in INNER]
out = Image.new("RGB", (620 * len(ims), 460), (20, 20, 20))
for i, im in enumerate(ims):
    out.paste(im, (620 * i, 0))
dst = os.path.join(ROOT, "Saved", "UiMock", "kicker_gap_options.png")
out.save(dst)
print(dst, out.size, "inner candidates", INNER, "outer", OUTER)
