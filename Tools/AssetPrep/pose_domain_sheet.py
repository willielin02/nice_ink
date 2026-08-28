# 姿勢邊界接觸表（2026-08-27）：把 pose_domain_shots 的圖與 pose_domain_sweep 的數字
# 合成一張圖——**圖與數字必須同源同姿勢**，否則就是「量的線不是他看的線」。
# 每一格＝側面＋正面，標題列＝姿勢與該姿勢實測的破圖指標。
#
# Run: python pose_domain_sheet.py <shots_dir> <ladder.tsv> <out_dir>
import sys
import os
import csv
from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pose_domain_ladder import LADDER  # noqa: E402

SHOTS, TSV, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(OUT, exist_ok=True)

num = {}
for r in csv.DictReader(open(TSV, encoding="utf-8"), delimiter="\t"):
    num[r["param"]] = r

TH_W, TH_H = 200, 270
CAP = 52
PAD = 8


def font(sz):
    for p in (r"C:\Windows\Fonts\msjh.ttc", r"C:\Windows\Fonts\msyh.ttc",
              r"C:\Windows\Fonts\arial.ttf"):
        if os.path.exists(p):
            try:
                return ImageFont.truetype(p, sz)
            except Exception:
                pass
    return ImageFont.load_default()


F1, F2 = font(17), font(15)
GROUPS = {}
for g, tag, label, ops in LADDER:
    GROUPS.setdefault(g, []).append((tag, label))

TITLES = {
    "bend": "軀幹前彎（Spine+Spine1 各半）",
    "hip": "髖屈（雙腿）",
    "knee": "膝屈（雙腿）",
    "neck": "頭頸俯仰（Neck+Head 各半）",
    "cand": "候選姿勢與對照組",
}

for g, items in GROUPS.items():
    cw = TH_W * 2 + PAD
    W = len(items) * (cw + PAD) + PAD
    H = CAP + TH_H + PAD * 2 + 34
    sheet = Image.new("RGB", (W, H), (248, 246, 242))
    dr = ImageDraw.Draw(sheet)
    dr.text((PAD, 8), TITLES.get(g, g) + "   （左＝側面 右＝正面；同一固定機位）",
            fill=(20, 20, 20), font=F1)
    for i, (tag, label) in enumerate(items):
        x = PAD + i * (cw + PAD)
        y = 34
        for j, v in enumerate(("s", "f")):
            p = os.path.join(SHOTS, "%s_%s.png" % (tag, v))
            if os.path.exists(p):
                im = Image.open(p).convert("RGB").resize((TH_W, TH_H), Image.LANCZOS)
                sheet.paste(im, (x + j * (TH_W + PAD // 2), y))
        r = num.get(tag)
        cap = label
        sub = "-"
        if r:
            sub = "摺疊 %s  自穿透 %s  褌穿刺 %s" % (r["fold"], r["xsect"], r["cross"])
        dr.rectangle([x, y + TH_H, x + cw, y + TH_H + CAP], fill=(255, 255, 255),
                     outline=(210, 205, 198))
        dr.text((x + 6, y + TH_H + 5), cap, fill=(15, 15, 15), font=F1)
        dr.text((x + 6, y + TH_H + 28), sub, fill=(90, 90, 90), font=F2)
    out = os.path.join(OUT, "sheet_%s.png" % g)
    sheet.save(out)
    print("WROTE", out, sheet.size)
