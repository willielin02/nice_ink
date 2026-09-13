"""按鍵畫法一字排開（2026-09-10；user：「請把這些設計一字排開成一張圖讓我檢視」
→ 追問：「遊戲畫面上的按鍵圖示和圖裡呈現的設計是一樣的嗎？」）。

**答案是不一樣，而這支的第一版正是害它不一樣的原因**：第 1 格標著「現行」，畫的卻是
09-09 二版的白線稿；隔天鍵帽改成三版實心白之後那個標籤就變成假的。**任何寫著「現行」的
對照圖，都必須從實際出貨的資產與實際尺寸生成**，否則它只會在你最信任它的時候騙你。

現制：
  第 1 格＝**真的那顆**——讀 `SourceAssets/UI/chrome/keycap.png`（＝ `/Game/UI/T_UI_Keycap`
  的來源檔），用 `NiUi::KeycapH` 的實際值 24 畫、字級用實際的 `NiType::Small`。
  其餘各格＝mock（PIL 畫的近似），只為了比**畫法**，標題已註明。
  最後一格＝現行但畫成 30px，用來看清楚 09-10 那一刀「32→24」到底改了多少。

底＝我們自己的真機截圖（皮膚／榻榻米）裁下來的。每格右邊放一顆 Kenney 線稿滑鼠＝家族對照。

Usage: python -X utf8 Tools/UiCheck/keycap_lineup.py
產出：Saved/UiMock/keycap_lineup.png（原生）＋ _2x.png
"""
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "Tools", "AssetPrep"))
import keycap_variants as KV  # noqa: E402  （framed／lit／dark 的烘焙器在那裡）

SHOT = os.path.join(ROOT, "Saved", "Screenshots", "WindowsEditor", "uishot_02_draw_standing00000.png")
SHIPPED = os.path.join(ROOT, "SourceAssets", "UI", "chrome", "keycap.png")
OUT = os.path.join(ROOT, "Saved", "UiMock", "keycap_lineup.png")

CAP_H = 24           # NiUi::KeycapH 的實際值（2026-09-10 由 32 降下來）
BIG_H = 30           # 09-09 二版的鍵高，留一格做尺寸對照
VERB = "PROPOSE A FLIP"
KEY = "F"
CELL_W, CELL_H = 340, 150


def fonts():
    verb = ImageFont.truetype(os.path.join(ROOT, "SourceAssets", "Fonts", "Oswald", "Oswald-Medium.ttf"), 21)
    # 鍵名字級＝實際的 NiType::Small(13pt) × 96/72 ＝ 17px（Slate 的 pt→px）
    key = ImageFont.truetype(os.path.join(ROOT, "SourceAssets", "Fonts", "NotoSans", "NotoSans-Bold.ttf"), 17)
    key_s = ImageFont.truetype(os.path.join(ROOT, "SourceAssets", "Fonts", "NotoSans", "NotoSans-Bold.ttf"), 17)
    # 標題有中文 ⇒ 一定要用有 CJK 的那一支（第一版用 NotoSans-Regular，整排標題渲成豆腐）
    lab = ImageFont.truetype(os.path.join(ROOT, "SourceAssets", "Fonts", "NotoSans", "NotoSansTC-Regular.otf"), 14)
    return verb, key, key_s, lab


def shadow_text(d, xy, text, font, fill, anchor="lm"):
    d.text((xy[0] + 1, xy[1] + 1), text, font=font, fill=(0, 0, 0, 150), anchor=anchor)
    d.text(xy, text, font=font, fill=fill, anchor=anchor)


def solid_white(h):
    """Meccha／PEAK：實心白、無框，淡投影撐亮底輪廓（他們的底不亮，我們的亮）。"""
    im = Image.new("RGBA", (h * 3, h * 3), (0, 0, 0, 0))
    sh = Image.new("L", im.size, 0)
    ImageDraw.Draw(sh).rounded_rectangle([3, 6, h * 3 - 4, h * 3 - 3], radius=h * 3 // 8, fill=110)
    im.putalpha(sh.filter(ImageFilter.GaussianBlur(3)))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([3, 3, h * 3 - 4, h * 3 - 6], radius=h * 3 // 8, fill=(242, 242, 242, 255))
    return im.resize((h, h), Image.LANCZOS)


def solid_dark(h):
    """RV There Yet：實心炭黑、無第二個顏色。"""
    im = Image.new("RGBA", (h * 3, h * 3), (0, 0, 0, 0))
    ImageDraw.Draw(im).rounded_rectangle([3, 3, h * 3 - 4, h * 3 - 4], radius=h * 3 // 8, fill=(46, 46, 50, 235))
    return im.resize((h, h), Image.LANCZOS)


def skeuo(h):
    """Liar's Bar：面＋框＋裙邊（第二個顏色來自框）。"""
    S = h * 3
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, S - 1, S - 1], radius=S // 7, fill=(226, 214, 168, 255))       # 外框（奶油）
    d.rounded_rectangle([5, 4, S - 6, S - 5], radius=S // 8, fill=(58, 76, 92, 255))          # 側身
    d.rounded_rectangle([9, 8, S - 10, S - 16], radius=S // 9, fill=(74, 96, 112, 255))       # 頂面
    return im.resize((h, h), Image.LANCZOS)


def nine(tex, w, h):
    return KV.nine_slice(tex, w, h)


def cell(bg, label, mode, verb_f, key_f, key_s_f, lab_f, mouse):
    im = bg.copy()
    d = ImageDraw.Draw(im, "RGBA")
    # 標題（左上，黑描邊保證任何底可讀）
    d.text((10, 8), label, font=lab_f, fill=(255, 255, 255), stroke_width=2, stroke_fill=(0, 0, 0))

    cy = CELL_H // 2 + 12
    right = CELL_W - 14
    # 滑鼠（家族對照）永遠在最右
    im.paste(mouse, (right - mouse.width, cy - mouse.height // 2), mouse)
    x = right - mouse.width - 16

    h = BIG_H if mode.endswith("_big") else CAP_H
    kf = key_f
    tw = d.textlength(KEY, font=kf)
    w = max(h, int(tw + h * 0.55))

    if mode.startswith("shipped") or mode in ("outline", "framed"):
        # shipped＝真的那張貼圖；outline／framed＝重烘一版舊的當歷史對照
        tex = Image.open(SHIPPED) if mode.startswith("shipped") else KV.bake(mode)
        cap = nine(tex, w, h)
        im.paste(cap, (x - w, cy - h // 2), cap)
        ink = (10, 10, 12) if mode.startswith("shipped") or mode == "framed" else (242, 242, 242)
        if mode == "outline":                 # 白線稿＝白字，要陰影才在亮底上分得開
            d.text((x - w / 2 + 1, cy + 1), KEY, font=kf, fill=(0, 0, 0, 140), anchor="mm")
        d.text((x - w / 2, cy), KEY, font=kf, fill=ink, anchor="mm")
        x -= w + 14
    elif mode in ("solid_white", "solid_dark", "skeuo"):
        maker = {"solid_white": solid_white, "solid_dark": solid_dark, "skeuo": skeuo}[mode]
        sq = maker(h)
        cap = sq.resize((w, h), Image.LANCZOS)
        im.paste(cap, (x - w, cy - h // 2), cap)
        ink = (10, 10, 12) if mode in ("solid_white",) else (242, 242, 242)
        if mode == "skeuo":
            ink = (232, 196, 96)
        d.text((x - w / 2, cy - (1 if mode == "skeuo" else 0)), KEY, font=kf, fill=ink, anchor="mm")
        x -= w + 14
    elif mode == "bracket":
        t = "[%s]" % KEY
        tw = d.textlength(t, font=key_f)
        shadow_text(d, (x - tw, cy), t, key_f, (242, 242, 242), anchor="lm")
        x -= tw + 14
    elif mode == "bare":
        tw = d.textlength(KEY, font=key_f)
        shadow_text(d, (x - tw, cy), KEY, key_f, (242, 242, 242), anchor="lm")
        x -= tw + 14

    vw = d.textlength(VERB, font=verb_f)
    shadow_text(d, (x - vw, cy), VERB, verb_f, (242, 242, 242), anchor="lm")
    return im


def main():
    verb_f, key_f, key_s_f, lab_f = fonts()
    shot = Image.open(SHOT).convert("RGB")
    # 我們自己的兩種底：皮膚（上排取樣）與榻榻米
    skin = shot.crop((700, 600, 700 + CELL_W, 600 + CELL_H))
    tatami = shot.crop((700, 1020, 700 + CELL_W, 1020 + CELL_H))

    mouse = Image.open(os.path.join(ROOT, "SourceAssets", "UI", "icons", "ico_mouse_left.png")).resize((26, 26), Image.LANCZOS)

    cases = [
        ("1  現行＝真資產 T_UI_Keycap 24px", "shipped"),
        ("2  現行畫成 30px（09-09 的尺寸）", "shipped_big"),
        ("3  mock：實心炭黑  RV There Yet", "solid_dark"),
        ("4  mock：白線稿（09-09 二版）", "outline"),
        ("5  mock：白面＋黑框（一版）", "framed"),
        ("6  mock：擬物立體  Liar's Bar", "skeuo"),
        ("7  mock：方括號  Lethal Company", "bracket"),
        ("8  mock：裸字母  REPO", "bare"),
    ]

    sheet = Image.new("RGB", (CELL_W * len(cases), CELL_H * 2 + 4), (24, 22, 20))
    for i, (label, mode) in enumerate(cases):
        sheet.paste(cell(skin, label, mode, verb_f, key_f, key_s_f, lab_f, mouse), (i * CELL_W, 0))
        sheet.paste(cell(tatami, "", mode, verb_f, key_f, key_s_f, lab_f, mouse), (i * CELL_W, CELL_H + 4))
    def save(img, path):
        # 檔案可能正被看圖程式開著（Windows 鎖檔）⇒ 換一個名字存，不要靜靜失敗
        try:
            img.save(path)
            return path
        except OSError:
            alt = path.replace(".png", "_new.png")
            img.save(alt)
            print("！原檔被鎖（看圖程式開著？）改存：", alt)
            return alt

    save(sheet, OUT)
    save(sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST), OUT.replace(".png", "_2x.png"))
    print("wrote", OUT, sheet.size)


if __name__ == "__main__":
    main()
