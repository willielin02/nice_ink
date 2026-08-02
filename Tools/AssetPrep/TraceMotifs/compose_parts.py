# 同源零件組合器（08-03 定案八式的「雙浪」重現腳本）——user「海浪應該可以
# 有兩個吧」：Twemoji 原浪＋同源 0.6× 副本右移 13＝大浪小浪。零件形狀一筆不改，
# 只做複製/平移/等比縮放（機械變換；美學來源仍是原設計師）。
# 鐵坑：svg2paths2 不解析 <g transform>（注入包裝=無聲無效、三份副本 bbox 全同
# 的簽名）——變換必須用 svgpathtools 烘進 d 字串再落檔。
# 歷史註記：龍加爪/燈籠吊桿的組合實驗同出此腳本、後被 user 淘汰
#（全史=Docs/DREAM_TRACE_PLAN.md 08-03 節）。
import os, sys

sys.stdout.reconfigure(encoding="utf-8")
from svgpathtools import svg2paths2

BASE = os.path.dirname(os.path.abspath(__file__))
SCALE = float(os.environ.get("WD_S", 0.6))
DX = float(os.environ.get("WD_DX", 13))

paths, attrs, sa = svg2paths2(os.path.join(BASE, "twemoji", "wave_1f30a.svg"))
allb = [p.bbox() for p in paths]
x1 = max(b[1] for b in allb)
y1 = max(b[3] for b in allb)

pairs = list(zip(paths, attrs))
add = []
for p, a in pairs:
    q = (p.translated(complex(-x1, -y1)).scaled(SCALE, SCALE)
          .translated(complex(x1, y1)).translated(complex(DX, 0)))
    add.append((q, a))

out = os.path.join(BASE, "final9", "wave_double_1.svg")
body = "".join(f'<path fill="{a.get("fill", "#000")}" d="{p.d()}"/>' for p, a in pairs + add)
with open(out, "w", encoding="utf-8") as f:
    f.write(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {int(x1 + DX + 2)} 36">{body}</svg>')
print("wrote", os.path.basename(out), f"(scale {SCALE}, dx {DX})")
