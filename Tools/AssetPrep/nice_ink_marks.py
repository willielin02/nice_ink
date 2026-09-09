"""Nice Ink 自家記號（2026-09-09；user：「日圓和罰酒標你可以試著自己設計看看」）。

為什麼這兩個是自己畫的，而滑鼠是拿現成的：
- 滑鼠是**輸入提示**，有整個品類專門為 16~40px 的 UI 畫（Kenney）。
- 這兩個是**遊戲世界的名詞**（猪口・日圓）。通用 UI 圖示集裡沒有猪口（只有西式高腳杯
  ＝被換掉的那一顆）；而有猪口的圖源（emoji／家紋）是插畫尺度的實心向量，實測 40px
  下讀成一團塊。⇒ 只能為這個尺寸畫。

畫法（三條自律，全部是量出來的不是猜的）：
1. **線寬 ÷ 外框高 = 0.067**，與 Kenney 線稿滑鼠等重（§15.8 的相容性量法）。
2. 形狀畫滿畫布再置中，不留隨手的白邊——留白由消費端的 `WidthOverride` 決定。
3. 8× 超取樣後 LANCZOS 下採樣＝斜線與圓弧的抗鋸齒；成品一律白，上色走 Slate tint。

輸出：SourceAssets/UI/icons/ico_{choko_full,choko_empty,coin}.png（128×128）
Usage: python -X utf8 Tools/AssetPrep/nice_ink_marks.py [--preview]
"""
import os
import sys
import math

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "SourceAssets", "UI", "icons")
SIZE = 128
SUP = 8
S = SIZE * SUP                    # 1024
STROKE_RATIO = 0.067              # 與 Kenney 線稿滑鼠同重


def canvas():
    im = Image.new("L", (S, S), 0)
    return im, ImageDraw.Draw(im)


def finish(mask):
    """L 遮罩 → 白色透明底 RGBA，並置中到 94% 內接框（與滑鼠同一條裁切規矩）。"""
    bb = mask.getbbox()
    mask = mask.crop(bb)
    sc = (S * 0.94) / max(mask.size)
    mask = mask.resize((max(1, round(mask.size[0] * sc)), max(1, round(mask.size[1] * sc))), Image.LANCZOS)
    full = Image.new("L", (S, S), 0)
    full.paste(mask, ((S - mask.size[0]) // 2, (S - mask.size[1]) // 2))
    small = full.resize((SIZE, SIZE), Image.LANCZOS)
    out = Image.new("RGBA", (SIZE, SIZE), (255, 255, 255, 0))
    out.putalpha(small)
    return out


def choko(bFull):
    """猪口（お猪口）：口が広く、底が締まった小さな盃。

    形の要点＝**上から見下ろす角度**（口の楕円が見えていること）。真横から描くと
    ただの台形＝コップになり、「猪口」に見えない。
    """
    im, d = canvas()
    top, bot = 180, 800                       # 口の高さ／高台の底（浅い＝猪口、深いと湯呑み）
    rim_w, foot_w = 900, 480
    ry = 130                                  # 口の楕円の短半径（見下ろし角）
    cx = S // 2
    w = int(STROKE_RATIO * (bot - top + ry))  # 線寬＝外框高 × 0.067

    # 胴：口の両端から高台へ、わずかに内側へ反る（直線だと湯呑みに見える）
    left = []
    right = []
    N = 40
    for i in range(N + 1):
        t = i / N
        # 二次ベジエ（制御点を内側に置く＝ひかえめな反り）
        x0, y0 = cx - rim_w / 2, top + ry
        x1, y1 = cx - rim_w / 2 * 0.80, top + ry + (bot - top - ry) * 0.62
        x2, y2 = cx - foot_w / 2, bot
        x = (1 - t) ** 2 * x0 + 2 * (1 - t) * t * x1 + t ** 2 * x2
        y = (1 - t) ** 2 * y0 + 2 * (1 - t) * t * y1 + t ** 2 * y2
        left.append((x, y))
        right.append((S - x, y))
    d.line(left, fill=255, width=w, joint="curve")
    d.line(right, fill=255, width=w, joint="curve")

    # 高台（底）：短い楕円の下半分＝置いてある感じ
    d.arc([cx - foot_w / 2, bot - 70, cx + foot_w / 2, bot + 70], 0, 180, fill=255, width=w)

    # 口：楕円。**満＝口いっぱいまで塗る**／空＝線だけ。
    # 一版は縁から線幅 1.9 倍だけ内側を塗った＝実寸 40px では 1px の細い輪＝満と空が見分けられない。
    # 見分けは「面積の差」で付ける（明暗ではなく量）。
    rim = [cx - rim_w / 2, top, cx + rim_w / 2, top + 2 * ry]
    if bFull:
        d.ellipse(rim, fill=255)
    else:
        d.ellipse(rim, outline=255, width=w)
    return finish(im)


def coin():
    """日圓＝**穴あき銭**（円形＋中央の角穴）。

    なぜ「¥」でも「円」でもないか：20px では漢字の画は潰れ、`¥` は文字であって記号ではない
    （数字の隣に置くと二つの書体が並ぶ）。**丸と四角の穴**は 20px でも構造が壊れない、
    かつ日本の銭にしか無い形。
    """
    im, d = canvas()
    r = S * 0.46
    cx = cy = S / 2
    w = int(STROKE_RATIO * (2 * r))
    # 角穴＝外径の 52%。実物の寛永通宝は約 30% だが、**20px では 30% の穴は「点」に潰れる**
    #  （0.36／0.46／0.58 を実寸で並べて選定＝Saved/UiMock/coin_variants.png）。
    #  実物の比率ではなく、その尺寸で穴に見える比率を採る。
    # 線稿版（円の輪＋穴の輪）も焼いて比べたが、20px では輪が二重になって煩い＝実心を採用。
    h = r * 0.52
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=255)
    d.rectangle([cx - h, cy - h, cx + h, cy + h], fill=0)
    _ = w
    return finish(im)


def main():
    os.makedirs(OUT, exist_ok=True)
    marks = {
        "choko_full": choko(True),
        "choko_empty": choko(False),
        "coin": coin(),
    }
    for name, im in marks.items():
        im.save(os.path.join(OUT, "ico_%s.png" % name))
        print("ico_%s.png" % name)

    if "--preview" in sys.argv:
        # 実寸で確かめる：猪口 40px（右下の比分）／銭 20px（右上の現金）
        rows = [("choko_full", 40), ("choko_empty", 40), ("coin", 20)]
        zoom = 6
        cellw = 40 * zoom + 60
        sheet = Image.new("RGB", (cellw * len(rows), 40 * zoom + 70), (22, 21, 20))
        x = 20
        for name, px in rows:
            im = marks[name]
            small = im.resize((px, px), Image.LANCZOS)
            big = small.resize((px * zoom, px * zoom), Image.NEAREST)
            sheet.paste(big, (x, 10), big)
            sheet.paste(small, (x + (px * zoom - px) // 2, 20 + 40 * zoom), small)
            x += cellw
        p = os.path.join(ROOT, "Saved", "UiMock", "marks_preview.png")
        sheet.save(p)
        print("preview:", p, "（上＝%d× 放大、下＝実寸）" % zoom)


if __name__ == "__main__":
    main()
