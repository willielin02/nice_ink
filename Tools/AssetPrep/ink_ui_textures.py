"""墨＝全站唯一的簽名材質（2026-09-07 UI 大改）。白底 alpha 貼圖，畫時吃頂點色 tint。
  ink_splat.png   512×256  印章墨漬（CORRECT／WRONG／OUT COLD 的地）
  ink_brush.png   512×32   一筆刷痕（底線、分隔線、計時線、巡禮框的四邊）
  ink_edge.png    512×24   滲墨邊（模態面板／揭曉帶的上下緣；alpha 沿 Y 由 1 收到 0、沿 X 有毛邊）
  ink_dot.png     64×64    軟墨點（面板角落／小記號）
輸出 SourceAssets/UI/ink/；匯入 → /Game/UI/Ink/T_UI_<Name>（ue_import_ink.py）。
Usage: python -X utf8 Tools/AssetPrep/ink_ui_textures.py
"""
import math, os, random
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "SourceAssets", "UI", "ink")
os.makedirs(OUT, exist_ok=True)
rng = random.Random(20260907)


def save_alpha(mask, name):
    img = Image.new("RGBA", mask.size, (255, 255, 255, 0))
    img.putalpha(mask)
    img.save(os.path.join(OUT, name))
    print(name, mask.size)


def splat(w=512, h=256):
    # 墨漬＝**密的核＋硬一點的邊＋外圍飛沫**。一版用十幾顆橢圓疊＋大高斯＝一顆灰蛋，
    # 讀不成墨。正解：核心是一個半徑受低頻雜訊擾動的多邊形（邊緣不規則但銳），
    # 核心 alpha 高（0.92），只在最外緣 1.5px 柔化；飛沫另外撒。
    W, H = w * 2, h * 2
    m = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(m)
    cx, cy = W * 0.5, H * 0.5
    # 低頻雜訊：疊幾個正弦
    harm = [(rng.uniform(1, 4), rng.uniform(0, math.tau), rng.uniform(0.05, 0.16)) for _ in range(5)]
    pts = []
    N = 360
    for i in range(N):
        a = i / N * math.tau
        r = 1.0
        for k, ph, amp in harm:
            r += amp * math.sin(k * a + ph)
        rx = W * 0.40 * r
        ry = H * 0.36 * r
        pts.append((cx + math.cos(a) * rx, cy + math.sin(a) * ry))
    d.polygon(pts, fill=235)
    # 第二層：偏一點的次核（濃淡不均＝墨在紙上的堆積）
    pts2 = []
    for i in range(N):
        a = i / N * math.tau
        r = 1.0
        for k, ph, amp in harm:
            r += amp * 0.8 * math.sin(k * a + ph + 0.7)
        pts2.append((cx - W * 0.06 + math.cos(a) * W * 0.30 * r, cy + H * 0.04 + math.sin(a) * H * 0.27 * r))
    d.polygon(pts2, fill=255)
    # 飛沫：沿長軸方向較多（甩出去的方向）
    for i in range(90):
        ang = rng.uniform(0, math.tau)
        dist = rng.uniform(0.95, 1.45)
        x = cx + math.cos(ang) * W * 0.40 * dist
        y = cy + math.sin(ang) * H * 0.36 * dist
        r = rng.uniform(1.5, 7.0) * (1.6 - dist * 0.6)
        d.ellipse((x - r, y - r, x + r, y + r), fill=int(rng.uniform(150, 240)))
    # 幾條甩出去的尾巴
    for i in range(6):
        ang = rng.choice([0.0, math.pi]) + rng.uniform(-0.5, 0.5)
        x0 = cx + math.cos(ang) * W * 0.36
        y0 = cy + math.sin(ang) * H * 0.30
        x1 = x0 + math.cos(ang) * rng.uniform(W * 0.08, W * 0.18)
        y1 = y0 + math.sin(ang) * rng.uniform(H * 0.05, H * 0.12)
        d.line((x0, y0, x1, y1), fill=200, width=int(rng.uniform(3, 7)))
    m = m.filter(ImageFilter.GaussianBlur(1.6)).resize((w, h), Image.LANCZOS)
    px = m.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = int(min(255, px[x, y] * 0.92))
    save_alpha(m, "ink_splat.png")


def brush(w=512, h=32):
    # 一筆：由左到右，中段厚、兩端收；上下緣加乾筆毛刺（隨機缺口）
    m = Image.new("L", (w * 2, h * 2), 0)
    d = ImageDraw.Draw(m)
    W, H = w * 2, h * 2
    cy = H * 0.5
    pts_top, pts_bot = [], []
    for i in range(0, W + 1, 4):
        t = i / W
        env = math.sin(math.pi * min(1.0, max(0.0, (t - 0.02) / 0.96))) ** 0.45  # 兩端收尖
        thick = H * 0.36 * env + H * 0.05
        jt = rng.uniform(-1.0, 1.0) * H * 0.045
        jb = rng.uniform(-1.0, 1.0) * H * 0.045
        pts_top.append((i, cy - thick + jt))
        pts_bot.append((i, cy + thick + jb))
    d.polygon(pts_top + pts_bot[::-1], fill=235)
    # 乾筆：沿線挖幾道細縫
    for k in range(9):
        x0 = rng.uniform(W * 0.15, W * 0.85)
        ln = rng.uniform(W * 0.05, W * 0.18)
        yy = cy + rng.uniform(-H * 0.28, H * 0.28)
        d.line((x0, yy, x0 + ln, yy + rng.uniform(-3, 3)), fill=int(rng.uniform(60, 140)), width=int(rng.uniform(1, 3)))
    m = m.filter(ImageFilter.GaussianBlur(1.2)).resize((w, h), Image.LANCZOS)
    save_alpha(m, "ink_brush.png")


def edge(w=512, h=24):
    # 滲墨邊：y=0 全不透明（接面板），往下毛邊收到 0；毛邊＝每欄不同的收尾高度
    m = Image.new("L", (w, h), 0)
    px = m.load()
    heights = []
    v = h * 0.55
    for x in range(w):
        v += rng.uniform(-2.2, 2.2)
        v = max(h * 0.25, min(h * 0.95, v))
        heights.append(v)
    for x in range(w):
        hh = heights[x]
        for y in range(h):
            if y < hh:
                a = 1.0 - (y / hh) ** 1.6
                px[x, y] = int(255 * max(0.0, min(1.0, a)))
    m = m.filter(ImageFilter.GaussianBlur(0.8))
    save_alpha(m, "ink_edge.png")


def dot(s=64):
    m = Image.new("L", (s * 2, s * 2), 0)
    d = ImageDraw.Draw(m)
    for i in range(6):
        r = rng.uniform(s * 0.55, s * 0.85)
        ox, oy = rng.uniform(-s * 0.12, s * 0.12), rng.uniform(-s * 0.12, s * 0.12)
        d.ellipse((s + ox - r, s + oy - r, s + ox + r, s + oy + r), fill=int(rng.uniform(160, 240)))
    m = m.filter(ImageFilter.GaussianBlur(3)).resize((s, s), Image.LANCZOS)
    save_alpha(m, "ink_dot.png")


splat()
brush()
edge()
dot()
