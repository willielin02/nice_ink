# -*- coding: utf-8 -*-
"""按鍵提示的跨遊戲對照表：每一款裁出真正的那一塊，原生像素並排（2× 放大版另存）。
底下附我們現行的那一塊。所有裁切點寫死在這裡＝下次要重跑不必再找一次。
"""
import os

from PIL import Image, ImageDraw, ImageFont

REF = r"C:/games/Unreal Engine/nice_ink/Saved/UiMock/ref"
SHOTS = r"C:/games/Unreal Engine/nice_ink/Saved/Screenshots/WindowsEditor"
OUT = r"C:/games/Unreal Engine/nice_ink/Saved/UiMock/keyref_0910.png"
FONT = r"C:/games/Unreal Engine/nice_ink/SourceAssets/Fonts/NotoSans/NotoSans-Regular.ttf"

ROWS = [
    ("Liar's Bar  filled dark + gold rim + skirt (48x27)", REF + "/liarsbar/shot_01.jpg", (360, 1008, 700, 1060)),
    ("Meccha  filled white + dark letter (23x23)", REF + "/meccha/shot_05.jpg", (1790, 300, 1920, 360)),
    ("PEAK  filled cream + dark letter, fill = text colour (21x21)", REF + "/peak/shot_02.jpg", (880, 660, 1120, 715)),
    ("PEAK hotbar  outline slot + filled cream number", REF + "/peak/shot_02.jpg", (1480, 940, 1800, 1060)),
    ("RV There Yet  filled charcoal + white letter, mouse = white outline", REF + "/rvthereyet/shot_03.jpg", (1400, 730, 1760, 990)),
    ("Lethal Company  no cap: [G] in brackets, mono type", REF + "/lethalcompany/shot_03.jpg", (1150, 120, 1611, 205)),
    ("REPO  no cap: bare numerals (near-black world)", REF + "/repo/shot_02.jpg", (760, 980, 1160, 1050)),
    ("Content Warning  outline box = slot, bare numeral = key", REF + "/contentwarning/shot_02.jpg", (1380, 860, 1920, 1070)),
    ("OURS 2026-09-09  outline cap + white letter", SHOTS + "/uishot_02_draw_standing00000.png", (1470, 680, 1770, 840)),
]


def main():
    font = ImageFont.truetype(FONT, 15)
    crops = []
    for label, path, box in ROWS:
        im = Image.open(path).convert("RGB")
        crops.append((label, im.crop(box)))
    pad, lab = 14, 26
    W = max(c.width for _, c in crops) + pad * 2
    H = sum(c.height + lab + pad for _, c in crops) + pad
    sheet = Image.new("RGB", (W, H), (26, 24, 22))
    d = ImageDraw.Draw(sheet)
    y = pad
    for label, c in crops:
        d.text((pad, y), label, font=font, fill=(240, 240, 240))
        sheet.paste(c, (pad, y + lab))
        y += c.height + lab + pad
    sheet.save(OUT)
    print("wrote", OUT, sheet.size)
    sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST).save(OUT.replace(".png", "_2x.png"))
    print("wrote 2x")


if __name__ == "__main__":
    main()
