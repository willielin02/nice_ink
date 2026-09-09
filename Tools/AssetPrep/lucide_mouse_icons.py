"""滑鼠輸入圖示（left／right／scroll）＝**Lucide 的 `mouse` 本體＋自繪的分界與高亮**。

為什麼不直接用現成的 prompt 素材包（Kenney／Xelu 那類）：2026-09-06 已經把 Kenney 的剪影
退役過一次，理由是「線條不同家」＋「亮起的鍵是紅色＝第二個強調色」。那個理由今天仍然成立
——右緣同一句話裡就有 Lucide 的動詞圖示，換一家的線寬與圓角進來，兩個圖示會互相打架。

為什麼也不繼續自己畫整顆：2026-09-08 user 指「不好看」。自己畫的膠囊沒有 Lucide 的比例與
圓角語言。

所以現制＝**骨架用 Lucide 官方 `mouse`（專業繪製、ISC、與其餘 47 個圖示同一家）**，
我們只補它缺的那一件事：哪一顆鍵亮起。分界線用同一個 stroke 寬度；高亮填在外框的
**內部遮罩**裡（flood fill 求內部），所以填色在構造上不可能溢出線外。

輸出 SourceAssets/UI/icons/ico_mouse_{left,right,scroll}.png（128×128）
Usage: python -X utf8 Tools/AssetPrep/lucide_mouse_icons.py [--preview]
"""
import io, json, os, re, sys, tarfile
from collections import deque
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TGZ = os.path.join(ROOT, "Saved", "UiMock", "ref", "fonts", "lucide.tgz")
OUT = os.path.join(ROOT, "SourceAssets", "UI", "icons")
SIZE = 128          # 與 lucide_icons.py 同尺寸
SUPER = 6           # 超取樣：分界線與高亮要跟外框一樣平滑


def codepoint(meta):
    cp = meta["encodedCode"] if "encodedCode" in meta else meta.get("unicode")
    if not isinstance(cp, str):
        return int(cp)
    cp = cp.replace(chr(92), "")
    m = re.search(r"&#(\d+);", cp)
    if m:
        return int(m.group(1))
    return int(re.search(r"([0-9a-fA-F]{4,6})", cp).group(1), 16)


def render_mouse(px):
    """Lucide `mouse` 的線（白、透明底）。"""
    t = tarfile.open(TGZ)
    fb = t.extractfile([n for n in t.getnames() if n.endswith("lucide.ttf")][0]).read()
    info = json.load(t.extractfile("package/font/info.json"))
    ch = chr(codepoint(info["mouse"]))
    f = ImageFont.truetype(io.BytesIO(fb), int(px * 0.78))
    im = Image.new("L", (px, px), 0)
    d = ImageDraw.Draw(im)
    b = d.textbbox((0, 0), ch, font=f)
    w, h = b[2] - b[0], b[3] - b[1]
    d.text(((px - w) / 2 - b[0], (px - h) / 2 - b[1]), ch, font=f, fill=255)
    return im


def interior_mask(stroke):
    """外框圍出來的內部（不含線本身）：從本體中心 flood fill。"""
    w, h = stroke.size
    px = stroke.load()
    bb = stroke.getbbox()
    inside = Image.new("L", (w, h), 0)
    ip = inside.load()
    sx, sy = (bb[0] + bb[2]) // 2, int(bb[1] + (bb[3] - bb[1]) * 0.78)   # 下半部一定是空的
    if px[sx, sy] > 96:
        raise RuntimeError("flood 種子落在線上")
    seen = bytearray(w * h)
    q = deque([(sx, sy)])
    seen[sy * w + sx] = 1
    while q:
        x, y = q.popleft()
        if px[x, y] > 96:
            continue
        ip[x, y] = 255
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = x + dx, y + dy
            if 0 <= nx < w and 0 <= ny < h and not seen[ny * w + nx]:
                seen[ny * w + nx] = 1
                q.append((nx, ny))
    return inside


def stroke_width(stroke):
    """**量**外框的線寬，不要用常數。

    一版寫 `sw = int(px * 0.0165)`（想成 24 格上的 2）＝768 上的 12px，
    而實際渲出來是 60px（128 上 10px）——**差 5 倍**。於是「加粗的滾輪」比原本的線還細、
    整條藏在既有的線裡面，看起來跟原圖一模一樣。
    量法＝在本體垂直中點掃一列，取左邊那一段（外框左壁）的寬度。
    """
    bb = stroke.getbbox()
    y = (bb[1] + bb[3]) // 2
    px = stroke.load()
    run = 0
    for x in range(bb[0], bb[2]):
        if px[x, y] > 128:
            run += 1
        elif run:
            return run
    return max(2, (bb[2] - bb[0]) // 12)


def wheel_span(stroke, sw):
    """滾輪短線的上下端。

    **中央那一列同時會打到外框的上弧與下弧**——第一版只濾掉上面那段，於是 wy1 變成外框
    底部 ⇒ 分界線畫到最底、高亮填滿整側（實拍證據 Saved/mouse_icons_preview.png 一版）。
    正解＝把該列切成連續段，取「兩端都不碰外框」的那一段。
    """
    bb = stroke.getbbox()
    cx = (bb[0] + bb[2]) // 2
    px = stroke.load()
    runs, st = [], None
    for y in range(bb[1], bb[3] + 1):
        on = (y < bb[3]) and px[cx, y] > 128
        if on and st is None:
            st = y
        if not on and st is not None:
            runs.append((st, y - 1))
            st = None
    inner = [r for r in runs if r[0] > bb[1] + sw * 1.5 and r[1] < bb[3] - sw * 1.5]
    if not inner:
        raise RuntimeError("找不到滾輪那一段")
    return inner[0]


def build(kind, px):
    stroke = render_mouse(px)
    inside = interior_mask(stroke)
    bb = stroke.getbbox()
    cx = (bb[0] + bb[2]) // 2
    sw = stroke_width(stroke)                # **量出來的**，不是常數（見 stroke_width）
    wy0, wy1 = wheel_span(stroke, sw)
    divider = wy1 + sw * 1.4

    # 高亮不貼著外框：內部遮罩內縮約半個線寬，讀起來才是「一顆鍵」而不是「半邊被塗掉」
    k = max(3, (int(sw * 0.5) | 1))
    inside_hi = inside.filter(ImageFilter.MinFilter(k))

    hi = Image.new("L", (px, px), 0)
    d = ImageDraw.Draw(hi)
    if kind == "scroll":
        # 輪子要**明顯比線粗**才讀得出「這一顆是滾輪」（一版只粗一點點＝跟原圖沒有差別）
        r = sw * 1.55
        d.rounded_rectangle([cx - r, wy0 - sw * 0.9, cx + r, wy1 + sw * 0.9], radius=r, fill=255)
        # **不要用內部遮罩裁它**：輪子那條線本身不屬於「內部」，一裁就把加粗的部分整個剪掉
        # ⇒ 看起來跟原圖一模一樣（一版的症狀）。膠囊本來就完全落在框內，不需要裁。
    else:
        gap = sw * 0.7                        # 中線兩側留縫＝兩顆鍵分得開
        top, bot = bb[1], divider - sw * 0.9
        if kind == "left":
            d.rectangle([bb[0], top, cx - gap, bot], fill=255)
        else:
            d.rectangle([cx + gap, top, bb[2], bot], fill=255)
        hi = Image.composite(hi, Image.new("L", (px, px), 0), inside_hi)
        # 分界線（與外框同寬同色）：沒有高亮時也讀得出「這裡有兩顆鍵」
        line = Image.new("L", (px, px), 0)
        ImageDraw.Draw(line).rectangle(
            [bb[0], divider - sw / 2.0, bb[2], divider + sw / 2.0], fill=255)
        line = Image.composite(line, Image.new("L", (px, px), 0), inside)
        stroke = Image.composite(Image.new("L", (px, px), 255), stroke, line)

    out = Image.composite(Image.new("L", (px, px), 255), stroke, hi)
    img = Image.new("RGBA", (px, px), (255, 255, 255, 0))
    img.putalpha(out)
    return img


def main():
    os.makedirs(OUT, exist_ok=True)
    px = SIZE * SUPER
    made = []
    for kind in ("left", "right", "scroll"):
        small = build(kind, px).resize((SIZE, SIZE), Image.LANCZOS)
        # 與 lucide_icons.py 同一個線寬補償（20px 下約 +0.3px）
        small.putalpha(small.split()[3].filter(ImageFilter.MaxFilter(3)))
        small.save(os.path.join(OUT, "ico_mouse_%s.png" % kind))
        made.append(kind)
    print("rendered:", ", ".join("ico_mouse_%s.png" % k for k in made))

    if "--preview" in sys.argv:
        cell = SIZE
        sheet = Image.new("RGB", (cell * 4 + 60, cell), (30, 30, 30))
        for i, k in enumerate(made):
            g = Image.open(os.path.join(OUT, "ico_mouse_%s.png" % k))
            sheet.paste(g, (i * (cell + 20), 0), g)
        ref = Image.open(os.path.join(OUT, "ico_hand.png"))     # 同家對照：線寬要一致
        sheet.paste(ref, (3 * (cell + 20), 0), ref)
        sheet = sheet.resize((sheet.size[0] * 2, sheet.size[1] * 2), Image.NEAREST)
        p = os.path.join(ROOT, "Saved", "mouse_icons_preview.png")
        sheet.save(p)
        print("preview:", p, "（左→右：left / right / scroll / 對照 hand）")


if __name__ == "__main__":
    main()
