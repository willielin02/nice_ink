"""NICE INK logotype (2026-09-06 第一批「怎麼畫」)：用 Oswald Bold 排字＋一滴墨。
不是手繪美術——是字型排出來的標誌字，加一個能被讀成「墨」的形狀。
輸出 SourceAssets/UI/logo_nice_ink.png（透明底、白字；引擎裡當 UI 貼圖 tint）。
Usage: python -X utf8 Tools/AssetPrep/make_logo.py
"""
import os
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FONT = os.path.join(ROOT, "SourceAssets", "Fonts", "Oswald", "Oswald-Bold.ttf")
OUT = os.path.join(ROOT, "SourceAssets", "UI", "logo_nice_ink.png")

S = 4  # supersample
W, H = 1024 * S, 480 * S          # 高度要放得下 Oswald 1.19em 的上伸＋墨滴（一版 320 把字腳切掉）
font = ImageFont.truetype(FONT, 220 * S)
img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
d = ImageDraw.Draw(img)

text = "NICE INK"
# tracking：逐字畫，字距 +6%
track = int(220 * S * 0.06)
x = 0
y = 20 * S
glyph_boxes = []
for ch in text:
    bbox = d.textbbox((x, y), ch, font=font)
    glyph_boxes.append((ch, x, bbox))
    d.text((x, y), ch, font=font, fill=(255, 255, 255, 255))
    adv = font.getlength(ch)
    x += int(adv) + (track if ch != " " else track // 2)
text_w = x - track

# 墨滴：掛在最後一個 K 的右下腳——一顆圓＋上方尖端（淚滴），再一小圈反光
k_ch, k_x, k_box = glyph_boxes[-1]
kx0, ky0, kx1, ky1 = k_box
r = 34 * S
cx = kx1 - r * 0.55
cy = ky1 + r * 1.9          # 滴掛在 K 的右腳正下方，尖端咬進字腳 0.2r
d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(255, 255, 255, 255))
d.polygon([(cx - r * 0.72, cy - r * 0.62), (cx + r * 0.72, cy - r * 0.62), (cx, cy - r * 2.3)], fill=(255, 255, 255, 255))
# 反光：挖一個小透明橢圓
d.ellipse((cx - r * 0.55, cy - r * 0.45, cx - r * 0.15, cy - r * 0.05), fill=(0, 0, 0, 0))

# 裁到內容並留邊
bbox = img.getbbox()
pad = 12 * S
img = img.crop((max(0, bbox[0] - pad), max(0, bbox[1] - pad), min(W, bbox[2] + pad), min(H, bbox[3] + pad)))
img = img.resize((img.width // S, img.height // S), Image.LANCZOS)
os.makedirs(os.path.dirname(OUT), exist_ok=True)
img.save(OUT)
print(OUT, img.size)
