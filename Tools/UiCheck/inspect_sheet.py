"""交付前自查用：把一張 1080p 截圖的七個 chrome 區域以原生解析度裁出來排成一張檢視圖
（上左／上中／上右／右中／下左／下中／下右），讓人（我）逐個容器看「有沒有包住裡面的東西」。
Usage: python -X utf8 inspect_sheet.py <out.png> <shot.png> [...]
"""
import os, sys
from PIL import Image, ImageDraw, ImageFont

REGIONS = [
    ("TL", (0, 0, 640, 220)), ("TC", (640, 0, 1280, 220)), ("TR", (1280, 0, 1920, 220)),
    ("RM", (1440, 380, 1920, 720)), ("BL", (0, 860, 640, 1080)), ("BC", (640, 860, 1280, 1080)), ("BR", (1280, 860, 1920, 1080)),
]


def sheet_for(path):
    im = Image.open(path).convert("RGB")
    sx, sy = im.width / 1920.0, im.height / 1080.0
    crops = []
    for name, (x0, y0, x1, y1) in REGIONS:
        c = im.crop((int(x0 * sx), int(y0 * sy), int(x1 * sx), int(y1 * sy)))
        if sx != 1.0:
            c = c.resize((x1 - x0, y1 - y0))
        crops.append((name, c))
    W = 1920
    row1 = [c for n, c in crops[:3]]
    row2 = [crops[3][1]]
    row3 = [c for n, c in crops[4:]]
    H = 24 + 220 + 8 + 340 + 8 + 220
    out = Image.new("RGB", (W, H), (30, 30, 30))
    d = ImageDraw.Draw(out)
    try:
        f = ImageFont.truetype("arial.ttf", 16)
    except Exception:
        f = ImageFont.load_default()
    d.text((6, 4), os.path.basename(path), fill=(240, 240, 240), font=f)
    x = 0
    for c in row1:
        out.paste(c, (x, 24)); x += 640
    out.paste(row2[0], (1440, 24 + 220 + 8))
    x = 0
    for c in row3:
        out.paste(c, (x, 24 + 220 + 8 + 340 + 8)); x += 640
    return out


if __name__ == "__main__":
    out, shots = sys.argv[1], sys.argv[2:]
    sheets = [sheet_for(p) for p in shots]
    H = sum(s.height for s in sheets) + 10 * (len(sheets) - 1)
    big = Image.new("RGB", (1920, H), (10, 10, 10))
    y = 0
    for s in sheets:
        big.paste(s, (0, y)); y += s.height + 10
    big.save(out)
    print(out, big.size)
