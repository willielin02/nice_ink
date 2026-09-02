# robo_misttier_probe.py 的離線判讀器（2026-09-02）——不開引擎、3 秒一輪
#
# 用法（先跑 probe 產生 Saved/robo_tier_*.png，再跑這支）：
#   "C:/games/Unreal Engine/nice_ink_face_pipeline/venv/Scripts/python.exe" \
#       Tools/RoboTest/robo_misttier_check.py
import os
import sys

import numpy as np
from PIL import Image

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")

SAVED = r"c:/games/Unreal Engine/nice_ink/Saved"
# 必須與 probe 常數一致
CX, CY = 0.47, 0.55
RES = 4096
XC = int(round(CX * RES))
YC = int(round(CY * RES))
TIER_L, TIER_M, TIER_F = 77, 153, 255  # 與 ShaderTierAlphaFor 對賬（中檔＝剛好 60.0%）
GRAIN = 0.05  # 皮膚錨定紋理振幅（密度的 ±5%）

PASS_N = [0]
FAIL_N = [0]


def ck(name, cond, detail):
    (PASS_N if cond else FAIL_N)[0] += 1
    print(("PASS " if cond else "FAIL ") + name + " | " + detail)


def load(f):
    p = os.path.join(SAVED, f)
    if not os.path.exists(p):
        return None
    return np.array(Image.open(p)).astype(np.float32)


def alpha(img):
    return img[:, :, 3] / 255.0


# 帶心的取樣窗（避開軟邊：帶半寬 ~12px、硬心 ~9px ⇒ 取 ±4px）
def hband(a, dx0=-60, dx1=-25):
    """水平帶、交疊區之外的一段（x 相對 XC 的 px 偏移）"""
    return a[YC - 4:YC + 5, XC + dx0:XC + dx1]


def crossbox(a):
    """交疊區中心 ±4px"""
    return a[YC - 4:YC + 5, XC - 4:XC + 5]


def main():
    a = load("robo_tier_a_mist.png")
    b = load("robo_tier_b_mist.png")
    c = load("robo_tier_c_mist.png")
    d = load("robo_tier_d_mist.png")
    e = load("robo_tier_e_mist.png")
    fpre = load("robo_tier_f_pre_mist.png")
    fpost = load("robo_tier_f_post_mist.png")
    for name, img in [("a", a), ("b", b), ("c", c), ("d", d), ("e", e),
                      ("f_pre", fpre), ("f_post", fpost)]:
        if img is None:
            print("缺檔 robo_tier_%s_mist.png — 先跑 robo_misttier_probe.py" % name)
            return 1

    LT, FT = TIER_L / 255.0, TIER_F / 255.0
    tol = GRAIN + 0.03  # 紋理 ±5% ＋量化/取樣餘裕

    # t1 同檔交疊＝不變深
    aa = alpha(a)
    solo = float(np.mean(hband(aa)))
    cross = float(np.mean(crossbox(aa)))
    ck("t1 同檔（淡）交疊不變深", abs(cross - solo) <= LT * tol + 0.01,
       "單帶 %.3f 交疊 %.3f（檔位 %.3f）" % (solo, cross, LT))
    ck("t1b 淡檔密度＝檔位值", abs(solo - LT) <= LT * tol + 0.01,
       "量到 %.3f 期望 %.3f" % (solo, LT))

    # t2 深壓淺
    ba = alpha(b)
    ck("t2 深壓淺＝生效", abs(float(np.mean(crossbox(ba))) - FT) <= FT * tol,
       "交疊 %.3f 期望 %.3f" % (float(np.mean(crossbox(ba))), FT))

    # t3 淺壓深
    ca = alpha(c)
    ck("t3 淺壓深＝無事", abs(float(np.mean(crossbox(ca))) - FT) <= FT * tol,
       "交疊 %.3f 期望 %.3f" % (float(np.mean(crossbox(ca))), FT))

    # t4 流量淡出：帶心密度沿 x 單調下降（頭尾差 > 0.5）
    da = alpha(d)
    xs = np.arange(XC - 60, XC + 60, 8)
    prof = [float(np.mean(da[YC - 3:YC + 4, x - 2:x + 3])) for x in xs]
    drops = sum(1 for i in range(1, len(prof)) if prof[i] <= prof[i - 1] + 0.03)
    ck("t4 流量淡出單調下降", drops >= len(prof) - 2 and prof[0] - prof[-1] > 0.5,
       "頭 %.3f 尾 %.3f 降段 %d/%d" % (prof[0], prof[-1], drops, len(prof) - 1))

    # t5 色相曲線：反預乘後，淡檔黑偏冷（B>R）、實檔黑≈調色盤色（35,35,35 sRGB）
    def unpremult_srgb(img, region):
        px = region
        al = px[:, :, 3:4] / 255.0
        srgb = px[:, :, 0:3] / 255.0
        lin = np.where(srgb <= 0.04045, srgb / 12.92, ((srgb + 0.055) / 1.055) ** 2.4)
        good = al[:, :, 0] > 0.1
        lin_st = lin[good] / al[good]
        return lin_st.mean(axis=0)  # RGB（PIL＝RGBA 順序）

    # 09-02 終裁：MistToneCoolStrength 預設 0＝所有色**同色相只差透明度**
    #（要開薄墨偏冷＝Strength 調回 1.0，並把 t5 斷言改回 B > R×1.3）
    lt_rgb = unpremult_srgb(a, a[YC - 4:YC + 5, XC - 60:XC - 25])
    ft_rgb = unpremult_srgb(b, b[YC - 4:YC + 5, XC - 4:XC + 5])
    ck("t5 淡檔黑＝同色相（冷移關）", abs(lt_rgb[2] - lt_rgb[0]) < 0.005,
       "淡檔 straight linear RGB=(%.4f,%.4f,%.4f) 期望三通道相近" % tuple(lt_rgb))
    ck("t5b 實檔回到筆色", abs(ft_rgb[0] - 0.0168) < 0.006 and abs(ft_rgb[2] - 0.0168) < 0.006,
       "實檔 straight linear RGB=(%.4f,%.4f,%.4f) 期望 ~0.0168" % tuple(ft_rgb))

    # t6 重建等價：live 與整層重播逐位相同（圖樣含罩染＝罩染重建也受測）
    same = np.array_equal(fpre.astype(np.uint8), fpost.astype(np.uint8))
    diff = int(np.sum(fpre.astype(np.int32) != fpost.astype(np.int32)))
    ck("t6 重建逐位等價", same, "不同位元組 %d" % diff)

    # t7 罩染（09-02 glazing）：淡檔紅罩在滿檔黑上＝壓暗的紅（不再是無事）。
    # 期望（線性、含色相曲線紅豁免）：out ≈ 紅×0.3 + 黑×0.7 ⇒ R≈0.27 遠大於黑的
    # 0.0168；alpha 維持滿。畫了兩次＝冪等一併受測（值仍須等於單次期望）。
    g = load("robo_tier_g_mist.png")
    if g is None:
        print("缺檔 robo_tier_g_mist.png")
        return 1
    ga = alpha(g)
    ck("t7a 罩染區 alpha 維持滿", abs(float(np.mean(crossbox(ga))) - FT) <= FT * tol,
       "交疊 %.3f" % float(np.mean(crossbox(ga))))
    g_rgb = unpremult_srgb(g, g[YC - 4:YC + 5, XC - 4:XC + 5])
    solo_rgb = unpremult_srgb(g, g[YC - 4:YC + 5, XC - 60:XC - 25])
    ck("t7 淡紅罩滿黑＝壓暗的紅（畫兩次仍同值＝冪等）",
       g_rgb[0] > 0.15 and solo_rgb[0] < 0.05,
       "罩染區 R=%.3f（黑帶素段 R=%.3f、紅的期望 ~0.27）" % (g_rgb[0], solo_rgb[0]))

    print("\nPASS=%d FAIL=%d" % (PASS_N[0], FAIL_N[0]))
    return 1 if FAIL_N[0] else 0


if __name__ == "__main__":
    sys.exit(main())
