# 定案八式的源清理重現腳本（08-03）：Wikimedia 折鶴 SVG 剝全畫布背景矩形
# →final9/crane_origami.svg（窄刺充氣在管線端 FATTEN 做，不在這裡）。
# 歷史註記：家紋背景矩形/酒壺去杯的清理也走過這條路（酒壺後被 user 終刀移除，
# 全史=Docs/DREAM_TRACE_PLAN.md 08-03 節）。
import re, os, sys

sys.stdout.reconfigure(encoding="utf-8")
BASE = os.path.dirname(os.path.abspath(__file__))

src = os.path.join(BASE, "kamon2", "crane_origami.svg")
dst = os.path.join(BASE, "final9", "crane_origami.svg")
with open(src, encoding="utf-8") as f:
    t = f.read()
t = re.sub(r"<rect\b[^>]*?/>", "", t, flags=re.S)
with open(dst, "w", encoding="utf-8") as f:
    f.write(t)
print("rect-stripped:", os.path.basename(dst))
