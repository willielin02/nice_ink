# robo_stencilwall_probe.py 的離線判讀器（2026-09-01）
#
# 為什麼分兩支：探針跑在 UE 的 python（無 numpy/PIL），只負責驅動畫布並把五個
# 情境的圖層 dump 出來；判讀在外面用 numpy 做。**不用開引擎、一輪 3 秒。**
#
# 用法（先跑 probe 產生 Saved/robo_wall_*.png，再跑這支）：
#   "C:/games/Unreal Engine/nice_ink_face_pipeline/venv/Scripts/python.exe" \
#       Tools/RoboTest/robo_stencilwall_check.py
#
# 契約（見 probe 標頭）：c1 基準有越線／c2 自己的稿線擋住／c2b 牆內不連坐／
#                        c3 別人的稿線不擋／c4 洗稿後裁切維持／c5 割線不受影響
import os
import sys

import numpy as np
from PIL import Image

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")

SAVED = r"c:/games/Unreal Engine/nice_ink/Saved"
# 必須與 probe 的常數一致
CY = 0.55
WALL_Y = 0.5515
RES = 4096
YW = int(round(WALL_Y * RES))
YC = int(round(CY * RES))
XS = slice(1900, 2200)

PASS_N = [0]
FAIL_N = [0]


def band(fname):
    p = os.path.join(SAVED, fname)
    if not os.path.exists(p):
        return None
    a = np.array(Image.open(p)).astype(np.float32)[:, :, 3] / 255.0
    outside = a[YW + 3:YW + 60, XS].sum()   # 牆的另一側
    inside = a[YC - 30:YW - 3, XS].sum()    # 牆與帶心之間
    return outside, inside


def ck(name, cond, detail):
    (PASS_N if cond else FAIL_N)[0] += 1
    print(("PASS " if cond else "FAIL ") + name + " | " + detail)


def main():
    cases = [
        ("a 無稿線（基準）", "robo_wall_a_mist.png"),
        ("b 自己的稿線", "robo_wall_b_mist.png"),
        ("c 別人的稿線", "robo_wall_c_mist.png"),
        ("d 洗掉稿線後", "robo_wall_d_mist.png"),
        ("e 割線＋自己稿線", "robo_wall_e_marker.png"),
        ("g0 正面衝牆基準", "robo_wall_g0_mist.png"),
        ("g 正面衝牆+稿線", "robo_wall_g_mist.png"),
    ]
    res = {}
    print("%-18s%14s%14s" % ("情境", "牆外側墨量", "牆內側墨量"))
    for tag, f in cases:
        r = band(f)
        if r is None:
            print("%-18s(缺檔 %s — 先跑 robo_stencilwall_probe.py)" % (tag, f))
            continue
        res[tag.split()[0]] = r
        print("%-18s%14.0f%14.0f" % (tag, r[0], r[1]))
    print()

    a, b, c, d, e = (res.get(k) for k in "abcde")
    g0, g = res.get("g0"), res.get("g")
    if a:
        # 門檻用相對量：絕對數字會隨筆刷剖面改動而過期（09-01 圓章改制實錘：
        # 同一條掃描從 378 掉到 172，契約假 FAIL 而行為完全正確）
        ck("c1 無牆時墨確實越過那條線", a[0] > a[1] * 0.01,
           "牆外 %.0f（牆內 %.0f 的 %.1f%%）" % (a[0], a[1], a[0] / max(a[1], 1e-6) * 100))
    if a and b:
        ck("c2 自己的稿線擋住墨", b[0] < a[0] * 0.05,
           "牆外 %.0f → %.0f（%.1f%%）" % (a[0], b[0], b[0] / max(a[0], 1e-6) * 100))
        ck("c2b 牆內側的墨沒有被連坐", b[1] > a[1] * 0.95,
           "牆內 %.0f → %.0f" % (a[1], b[1]))
    if a and c:
        ck("c3 別人的稿線不擋", c[0] > a[0] * 0.95,
           "牆外 %.0f vs 基準 %.0f" % (c[0], a[0]))
    if b and d:
        ck("c4 洗掉稿線後裁切維持住", abs(d[0] - b[0]) <= max(b[0], 1.0) * 0.15 + 5,
           "洗前 %.0f → 洗後 %.0f" % (b[0], d[0]))
    if e:
        ck("c5 割線不受牆影響", e[0] + e[1] > 100,
           "牆外 %.0f 牆內 %.0f" % (e[0], e[1]))
    if g0:
        # 基準必須先證明「正面衝牆真的會越線」——否則 c6 是空洞契約
        #（v1 的病＝圓章前半壓過線；沒有這個基準，c6 對「根本沒衝到牆」也亮綠燈）
        ck("c6a 無牆時正面衝真的越線", g0[0] > 50,
           "牆外 %.0f" % g0[0])
    if g0 and g:
        ck("c6 正面衝牆被擋住（v1 白刺案例）", g[0] < g0[0] * 0.05,
           "牆外 %.0f → %.0f（%.1f%%）" % (g0[0], g[0], g[0] / max(g0[0], 1e-6) * 100))
        ck("c6b 牆內側的墨沒有被連坐", g[1] > g0[1] * 0.90,
           "牆內 %.0f → %.0f" % (g0[1], g[1]))

    print("\nPASS=%d FAIL=%d" % (PASS_N[0], FAIL_N[0]))
    return 1 if FAIL_N[0] else 0


if __name__ == "__main__":
    sys.exit(main())
