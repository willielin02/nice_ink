# 姿勢域報表（2026-08-27）：把 pose_domain_sweep 的 rom_table.tsv 收斂成「每個關節每個軸
# 的可用角度上下限」，門檻可調。輸出人讀表 + JSON（供之後生成 C++ PoseDomain 表）。
#
# Run: python pose_domain_report.py <rom_table.tsv> <out_prefix>
import sys
import json
import csv
from collections import defaultdict

SRC, OUT = sys.argv[1], sys.argv[2]

# 門檻（可調）。fold＝局部二面角 >100° 的邊數；xsect＝皮膚自穿透對數；cross＝褌被刺穿對數
# （xsect/cross 皆已扣靜止基線）。**分兩組**：skin＝只看皮膚，cloth＝再加上褌
# ——實測褌才是腿部動作的瓶頸（髖屈 30° 皮膚只摺 13 條邊，褌卻被刺穿 391 對），
# 而那是資產問題不是身體問題，兩者必須分開報。
TIER_SKIN = [("skin_clean", 0, 0, 10 ** 9), ("skin_ok", 50, 200, 10 ** 9),
             ("skin_loose", 200, 800, 10 ** 9)]
TIER_CLOTH = [("all_clean", 0, 0, 0), ("all_ok", 50, 200, 100),
              ("all_loose", 200, 800, 600)]
TIERS = TIER_SKIN + TIER_CLOTH

rows = list(csv.DictReader(open(SRC, encoding="utf-8"), delimiter="\t"))
by_case = defaultdict(list)
for r in rows:
    by_case[r["case"]].append((int(r["param"]), int(r["fold"]), int(r["xsect"]), int(r["cross"])))

report = {}
lines = ["case\t" + "\t".join("%s[-,+]" % t[0] for t in TIERS)]
for case in sorted(by_case):
    samples = sorted(by_case[case])
    entry = {}
    cells = []
    for tname, fmax, xmax, cmax in TIERS:
        # 從 0 往兩側走，遇到第一個超標就停 ⇒ 連通區間（域必須是連通的才有意義）
        lo = 0
        for deg, fold, xs, cross in [s for s in samples if s[0] < 0][::-1]:
            if fold > fmax or xs > xmax or cross > cmax:
                break
            lo = deg
        hi = 0
        for deg, fold, xs, cross in [s for s in samples if s[0] > 0]:
            if fold > fmax or xs > xmax or cross > cmax:
                break
            hi = deg
        entry[tname] = [lo, hi]
        cells.append("%d..%d" % (lo, hi))
    report[case] = entry
    lines.append(case + "\t" + "\t".join(cells))

open(OUT + ".tsv", "w", encoding="utf-8").write("\n".join(lines))
json.dump(report, open(OUT + ".json", "w", encoding="utf-8"), indent=1)
print("\n".join(lines))
print("\nWROTE %s.tsv / %s.json  (%d cases)" % (OUT, OUT, len(report)))
