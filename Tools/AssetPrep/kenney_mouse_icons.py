"""滑鼠輸入圖示（left／right／scroll）＝ **Kenney Input Prompts 1.5（CC0）的線稿版**。

為什麼這次可以換家（2026-09-09；user：「滑鼠用專業圖示」）：
09-06 退掉 Kenney、09-08 改用 Lucide 本體＋自繪高亮，兩次的理由都是同一句——
**右緣同一句話裡就有 Lucide 的動詞圖示，換家會打架**。而 09-09 那批動詞圖示已經
全數移除（`ActionIcon()` 拆除），滑鼠成為全 UI 唯一的圖示 ⇒ **那條理由不存在了**，
留下的唯一要求是「它要跟鍵帽相處」。

為什麼選 outline 而不是實心版：實心版的「哪一顆鍵亮著」是**紅色**（231,50,70）＝
中性制不准的第二個強調色；線稿版用「線 vs 實填」表達同一件事，**單色就講得完**。

裁切：Kenney 的 PNG 自帶大量透明留白 ⇒ 先裁到 alpha 邊界再置中縮放，否則同樣
`WidthOverride(28)` 之下它會比鍵帽小一圈（§12.11：字與圖在盒子裡的位置要量）。

輸出 SourceAssets/UI/icons/ico_mouse_{left,right,scroll}.png（128×128、白、透明底）
Usage: python -X utf8 Tools/AssetPrep/kenney_mouse_icons.py [--preview]
"""
import os
import io
import sys
import zipfile

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ZIP = os.path.join(ROOT, "SourceAssets", "InputPrompts", "kenney_input.zip")
OUT = os.path.join(ROOT, "SourceAssets", "UI", "icons")
SIZE = 128
FILL = 0.94          # 裁切後佔滿多少（留一點呼吸，與鍵帽等重）
SRC = {
    "left": "Keyboard & Mouse/Double/mouse_left_outline.png",
    "right": "Keyboard & Mouse/Double/mouse_right_outline.png",
    "scroll": "Keyboard & Mouse/Double/mouse_scroll_outline.png",
}


def bake(z, member):
    im = Image.open(io.BytesIO(z.read(member))).convert("RGBA")
    bb = im.split()[3].getbbox()
    if bb is None:
        raise RuntimeError("空圖：%s" % member)
    im = im.crop(bb)
    # 等比縮放到 SIZE*FILL 的內接方框（不變形）
    s = (SIZE * FILL) / max(im.size)
    im = im.resize((max(1, round(im.size[0] * s)), max(1, round(im.size[1] * s))), Image.LANCZOS)
    out = Image.new("RGBA", (SIZE, SIZE), (255, 255, 255, 0))
    out.paste(im, ((SIZE - im.size[0]) // 2, (SIZE - im.size[1]) // 2), im)
    # 一律白（上色由 Slate 的 ColorAndOpacity 給；Kenney 線稿本來就是白的，這裡只是保證）
    white = Image.new("RGBA", out.size, (255, 255, 255, 255))
    white.putalpha(out.split()[3])
    return white


def stroke_ratio(im):
    """線寬 ÷ 外框高（§15.8 的相容性量法）：在本體垂直中點掃一列取左壁那一段。"""
    a = im.split()[3]
    bb = a.getbbox()
    px = a.load()
    y = (bb[1] + bb[3]) // 2
    run = 0
    for x in range(bb[0], bb[2]):
        if px[x, y] > 128:
            run += 1
        elif run:
            return run / float(bb[3] - bb[1])
    return 0.0


def main():
    os.makedirs(OUT, exist_ok=True)
    z = zipfile.ZipFile(ZIP)
    made = []
    for kind, member in SRC.items():
        im = bake(z, member)
        p = os.path.join(OUT, "ico_mouse_%s.png" % kind)
        im.save(p)
        made.append((kind, im))
        print("ico_mouse_%s.png  線寬/外框高 = %.3f" % (kind, stroke_ratio(im)))

    if "--preview" in sys.argv:
        cell, zoom = 28, 4
        sheet = Image.new("RGB", ((cell + 8 + cell * zoom + 24) * len(made), cell * zoom), (22, 21, 20))
        x = 0
        for _, im in made:
            small = im.resize((cell, cell), Image.LANCZOS)
            sheet.paste(small, (x, (cell * zoom - cell) // 2), small)
            big = small.resize((cell * zoom, cell * zoom), Image.NEAREST)
            sheet.paste(big, (x + cell + 8, 0), big)
            x += cell + 8 + cell * zoom + 24
        p = os.path.join(ROOT, "Saved", "UiMock", "mouse_kenney_preview.png")
        sheet.save(p)
        print("preview:", p)


if __name__ == "__main__":
    main()
