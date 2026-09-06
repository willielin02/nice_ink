"""Lucide（ISC 授權）線圖示 → 白色透明 PNG（2026-09-06 UI 第二批：一組同家的圖示）。
來源＝lucide-static npm tgz（Saved/UiMock/ref/fonts/lucide.tgz）裡的 font/lucide.ttf＋info.json；
用字型渲染而不是解析 SVG（PIL 不吃 SVG；字型版線寬與 SVG 同源）。
輸出 SourceAssets/UI/icons/ico_<name>.png（128×128）＋ LICENSE。
Usage: python -X utf8 Tools/AssetPrep/lucide_icons.py
"""
import io, json, os, tarfile
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TGZ = os.path.join(ROOT, "Saved", "UiMock", "ref", "fonts", "lucide.tgz")
OUT = os.path.join(ROOT, "SourceAssets", "UI", "icons")
NAMES = [
    "coins", "wine", "droplet", "palette", "pen-tool", "hand", "footprints", "play", "user-x",
    "log-out", "check", "copy", "languages", "eye", "gavel", "settings", "user", "log-in", "plus",
    "scan-eye", "rotate-ccw", "eraser", "alarm-clock", "route", "zap", "arrow-right", "layers",
    "sun", "moon", "bed", "refresh-cw", "hourglass", "x",
]
SIZE = 128

os.makedirs(OUT, exist_ok=True)
t = tarfile.open(TGZ)
names = t.getnames()
ttf = [n for n in names if n.endswith("lucide.ttf")]
assert ttf, "lucide.ttf not in package"
font_bytes = t.extractfile(ttf[0]).read()
info = json.load(t.extractfile("package/font/info.json"))
lic = [n for n in names if n.lower().endswith("license")]
if lic:
    open(os.path.join(OUT, "LICENSE_lucide.txt"), "wb").write(t.extractfile(lic[0]).read())

font = ImageFont.truetype(io.BytesIO(font_bytes), int(SIZE * 0.78))
done, missing = [], []
for name in NAMES:
    meta = info.get(name)
    if not meta:
        missing.append(name)
        continue
    cp = meta["encodedCode"] if "encodedCode" in meta else meta.get("unicode")
    if isinstance(cp, str):
        cp = cp.replace("\\", "")
        ch = chr(int(cp, 16))
    else:
        ch = chr(int(cp))
    img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    bbox = d.textbbox((0, 0), ch, font=font)
    w, h = bbox[2] - bbox[0], bbox[3] - bbox[1]
    d.text(((SIZE - w) / 2 - bbox[0], (SIZE - h) / 2 - bbox[1]), ch, font=font, fill=(255, 255, 255, 255))
    img.save(os.path.join(OUT, f"ico_{name.replace('-', '_')}.png"))
    done.append(name)
print("rendered", len(done), "missing", missing)
