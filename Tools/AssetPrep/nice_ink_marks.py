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


def _bez(p0, p1, p2, n=40):
    out = []
    for i in range(1, n + 1):
        t = i / n
        out.append(((1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * p1[0] + t ** 2 * p2[0],
                    (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * p1[1] + t ** 2 * p2[1]))
    return out


def boot():
    """靴子＝「踢出房間」（2026-09-17 user 定案：踢人留在 ESC 選單、鈕改成靴子圖示）。

    2026-09-18 四輪重畫（user：「這些圖示讀不出來是靴子，去調查其他派對遊戲的靴子長什麼樣子」）。
    調查（Saved/UiMock/boot_reference.png）：真的用靴子當踢人鈕的派對遊戲只有 Among Us；其餘（Goose Goose
    Duck／Phasmophobia／Gartic Phone）都是 ×。把 Among Us、game-icons.net 六顆、Twemoji／Noto 的靴子並排，
    **所有靴子共同的特徵**（本函式逐條照畫）：
    1. **實心剪影**——線稿版在 24px 讀成字母 L（一～三輪的血價）。
    2. **筒高 ≈ 腳長、筒寬 ≈ 腳長的一半**——細筒＝襪子、長腳＝滑板。
    3. **筒口有翻邊**（比筒略寬的一段）＝「這是靴不是鞋」。
    4. **腳背內凹**（筒前緣到腳背是一道凹弧）＝腳踝。
    5. **圓頭略上翹**。
    6. **鞋跟是獨立一塊**：足弓處有缺口、鞋跟比鞋底略低。
    7. **鞋尖朝左＋略斜起**（Among Us／Lorc／emoji 全部朝左；斜起＝「踢」的動勢）。
    斜度掃 0／10／20／30 在 24px 對照（Saved/UiMock/boot_variants4.png）：20° 以上邊緣被抗鋸齒糊掉、
    剪影散成斜塊；**10°** 保住每一條邊又有動勢。動線／爆裂星試過：單色下在 24px 只剩幾顆孤點、
    還把主體擠小（finish 依外框縮放）＝不畫。
    """
    im, d = canvas()
    TILT = 10
    top, sole = 130, 880
    bl, br = 250, 640                     # 筒（背側／前側）：寬 390 ≈ 腳長 740 的 53%
    cuff_l, cuff_r, cuff_b = 200, 690, 250
    instep_end = 800
    notch, heel_w, heel_drop = 90, 520, 40
    # 腳尖朝右畫（座標好讀），最後鏡射成朝左、再繞中心轉 TILT。
    pts = [(cuff_l, top), (cuff_r, top), (cuff_r, cuff_b), (br, cuff_b), (br, 500)]
    pts += _bez((br, 500), (br + 5, 670), (instep_end, 660))     # 腳背內凹
    pts += _bez((instep_end, 660), (1010, 640), (985, 810))      # 圓頭（略上翹）
    pts += _bez((985, 810), (975, 885), (900, sole))             # 頭底收圓
    pts += [(heel_w + 170, sole), (heel_w + 170, sole - notch), (heel_w, sole - notch),   # 足弓缺口
            (heel_w, sole + heel_drop), (bl, sole + heel_drop),                             # 鞋跟獨立、略低於鞋底
            (bl, cuff_b), (cuff_l, cuff_b)]
    cx = cy = S / 2
    r = math.radians(TILT)
    out = []
    for x, y in pts:
        x = S - x
        out.append((cx + (x - cx) * math.cos(r) - (y - cy) * math.sin(r),
                    cy + (x - cx) * math.sin(r) + (y - cy) * math.cos(r)))
    d.polygon(out, fill=255)
    return finish(im)


def main():
    os.makedirs(OUT, exist_ok=True)
    marks = {
        "choko_full": choko(True),
        "choko_empty": choko(False),
        "coin": coin(),
        "boot": boot(),
    }
    for name, im in marks.items():
        im.save(os.path.join(OUT, "ico_%s.png" % name))
        print("ico_%s.png" % name)

    if "--preview" in sys.argv:
        # 実寸で確かめる：猪口 40px（右下の比分）／銭 20px（右上の現金）
        rows = [("choko_full", 40), ("choko_empty", 40), ("coin", 20), ("boot", 20)]
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
