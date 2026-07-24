# -*- coding: utf-8 -*-
"""
Nice Ink 客觀刺青色盤選擇器
============================
方法論（每步依據）：
1. 膚色域 = Monk Skin Tone 量表 10 階（Google Skin Tone 研究發布的包容性
   膚色標準，取代 Fitzpatrick 六階；本遊戲自拍管線實測 skin_color 上身，
   玩家膚色即落在此域）。
2. 對比指標 = CIEDE2000（工業標準感知色差）。不用純亮度對比的理由：
   深膚上深色的「亮度」對比塌掉，但皮膚無論深淺都是低彩度——高彩度墨
   靠彩度差照樣可讀，ΔE2000 同時計入明度/彩度/色相差。
3. 合格門檻 = 資料驅動：用公認可讀組合（黑墨對淺膚=現實刺青的黃金標準）
   與公認弱組合（黃對淺膚、棕對深膚、白對淺膚）實算 ΔE，門檻取兩群之間
   的分隔值——不拍腦袋。
4. 黑/白特赦 = 互補價值錨：無單色能在最深與最淺膚上都靠亮度贏（黑對淺膚
   滿分/對極深趨零，白相反）。合格判準改為配對式：對每一階膚色，
   max(ΔE黑, ΔE白) ≥ 門檻（任何膚色至少其一可用）。
5. 表達色 8 席 = 通過「min-over-全膚域 ΔE ≥ 門檻」的候選中，做 max-min
   分散選擇（最大化彼此最小 ΔE=互相可辨→巡禮判讀/畫風指紋），並施加
   色相家族配額（紅/橙黃/綠/青藍/紫洋紅各 ≥1=表達覆蓋：畫得出血、水、草）。
6. 候選池 = 真實刺青墨基本組近似色（題材誠實）＋HSV 系統掃描（不遺漏）。
7. 本工具只做紙上初篩；最終閘門=引擎內雙膚色截圖（材質鏈才是真相）＋
   user viewport。
"""
import json
import colorsys
import numpy as np
from PIL import Image, ImageDraw

OUT_DIR = "C:/games/Unreal Engine/nice_ink/Saved"

# ---------- 色彩數學 ----------
def srgb_to_linear(c):
    c = np.asarray(c, dtype=np.float64)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)

def linear_to_srgb(c):
    c = np.clip(np.asarray(c, dtype=np.float64), 0.0, 1.0)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)

M_RGB2XYZ = np.array([[0.4124564, 0.3575761, 0.1804375],
                      [0.2126729, 0.7151522, 0.0721750],
                      [0.0193339, 0.1191920, 0.9503041]])
WHITE_D65 = np.array([0.95047, 1.0, 1.08883])

def srgb_to_lab(rgb):
    xyz = M_RGB2XYZ @ srgb_to_linear(rgb)
    t = xyz / WHITE_D65
    f = np.where(t > (6 / 29) ** 3, np.cbrt(t), t / (3 * (6 / 29) ** 2) + 4 / 29)
    return np.array([116 * f[1] - 16, 500 * (f[0] - f[1]), 200 * (f[1] - f[2])])

def de2000(lab1, lab2):
    L1, a1, b1 = lab1
    L2, a2, b2 = lab2
    C1 = np.hypot(a1, b1)
    C2 = np.hypot(a2, b2)
    Cb = (C1 + C2) / 2
    G = 0.5 * (1 - np.sqrt(Cb ** 7 / (Cb ** 7 + 25.0 ** 7)))
    a1p, a2p = (1 + G) * a1, (1 + G) * a2
    C1p, C2p = np.hypot(a1p, b1), np.hypot(a2p, b2)
    h1p = np.degrees(np.arctan2(b1, a1p)) % 360
    h2p = np.degrees(np.arctan2(b2, a2p)) % 360
    dLp = L2 - L1
    dCp = C2p - C1p
    if C1p * C2p == 0:
        dhp = 0.0
    else:
        dh = h2p - h1p
        if dh > 180: dh -= 360
        elif dh < -180: dh += 360
        dhp = dh
    dHp = 2 * np.sqrt(C1p * C2p) * np.sin(np.radians(dhp) / 2)
    Lbp = (L1 + L2) / 2
    Cbp = (C1p + C2p) / 2
    if C1p * C2p == 0:
        hbp = h1p + h2p
    else:
        s = h1p + h2p
        if abs(h1p - h2p) > 180:
            s += 360 if s < 360 else -360
        hbp = s / 2
    T = (1 - 0.17 * np.cos(np.radians(hbp - 30)) + 0.24 * np.cos(np.radians(2 * hbp))
         + 0.32 * np.cos(np.radians(3 * hbp + 6)) - 0.20 * np.cos(np.radians(4 * hbp - 63)))
    Sl = 1 + 0.015 * (Lbp - 50) ** 2 / np.sqrt(20 + (Lbp - 50) ** 2)
    Sc = 1 + 0.045 * Cbp
    Sh = 1 + 0.015 * Cbp * T
    dTheta = 30 * np.exp(-(((hbp - 275) / 25) ** 2))
    Rc = 2 * np.sqrt(Cbp ** 7 / (Cbp ** 7 + 25.0 ** 7))
    Rt = -Rc * np.sin(np.radians(2 * dTheta))
    return np.sqrt((dLp / Sl) ** 2 + (dCp / Sc) ** 2 + (dHp / Sh) ** 2
                   + Rt * (dCp / Sc) * (dHp / Sh))

def hex_rgb(h):
    return np.array([int(h[i:i + 2], 16) / 255.0 for i in (1, 3, 5)])

# ---------- 1. 膚色域：Monk Skin Tone 10 階（skintone.google 發布值） ----------
MST = [hex_rgb(h) for h in ["#f6ede4", "#f3e7db", "#f7ead0", "#eadaba", "#d7bd96",
                            "#a07e56", "#825c43", "#604134", "#3a312a", "#292420"]]
MST_LAB = [srgb_to_lab(c) for c in MST]

def min_de_vs_skin(rgb):
    lab = srgb_to_lab(rgb)
    return min(de2000(lab, s) for s in MST_LAB)

def de_profile(rgb):
    lab = srgb_to_lab(rgb)
    return [de2000(lab, s) for s in MST_LAB]

# ---------- 3. 門檻：資料驅動 ----------
BLACK = hex_rgb("#1a1a1a")   # 墨黑（真墨非純0：碳黑）
WHITE = hex_rgb("#f2f2f2")
print("=" * 72)
print("門檻校準（依據：『邊際但公認可用』下限 vs 『公認弱』上限的 gap 中點）")
print("（初版錯誤已修：黑對淺膚 dE=84 是可達對比的天花板、不是可讀下限——")
print("  拿天花板當門檻會把整個色域殺光。可讀下限要用邊際案例錨定。）")
CUR = {"黃(現行)": np.array(linear_to_srgb([0.92, 0.85, 0.05])),
       "紅(現行)": np.array(linear_to_srgb([0.78, 0.05, 0.05])),
       "棕(現行)": np.array(linear_to_srgb([0.4, 0.22, 0.08])),
       "白(現行)": np.array(linear_to_srgb([0.95, 0.95, 0.95]))}
# 邊際可用（依據：黑墨在棕膚=現實刺青的標準做法；現行紅在中膚=本作截圖可讀）
marginal = [("黑墨 on MST-7(現實標準做法)", de2000(srgb_to_lab(BLACK), MST_LAB[6])),
            ("紅(現行) on MST-6", de2000(srgb_to_lab(CUR["紅(現行)"]), MST_LAB[5]))]
# 公認弱（user viewport 可證：黃在淺膚弱、棕與深膚同族、白在淺膚近隱形）
bad = [("黃 on MST-3", de2000(srgb_to_lab(CUR["黃(現行)"]), MST_LAB[2])),
       ("棕 on MST-7", de2000(srgb_to_lab(CUR["棕(現行)"]), MST_LAB[6])),
       ("白 on MST-1", de2000(srgb_to_lab(CUR["白(現行)"]), MST_LAB[0]))]
for n, v in marginal:
    print(f"  邊際可用  {n:28s} dE00={v:5.1f}")
for n, v in bad:
    print(f"  公認弱    {n:28s} dE00={v:5.1f}")
worst_marginal = min(v for _, v in marginal)
best_bad = max(v for _, v in bad)
T = round((worst_marginal + best_bad) / 2.0, 1)
print(f"  → 門檻 T = (邊際下限 {worst_marginal:.1f} + 弱例上限 {best_bad:.1f}) / 2 = {T}")

# ---------- 6. 候選池 ----------
pool = {}
# 真實刺青墨基本組近似色（題材誠實；近似值——各廠牌未公開精確色度）
REAL_INKS = {
    "triple-black": "#141414", "mixing-white": "#f2f0eb",
    "crimson": "#9e1b32", "lipstick-red": "#c8102e", "bright-orange": "#f4691e",
    "golden-yellow": "#e8b419", "mustard": "#c99700",
    "lime-green": "#5cb130", "grass-green": "#2e8b2e", "deep-teal": "#0e7c6b",
    "turquoise": "#26bfb2", "true-blue": "#1660d1", "sky-blue": "#3aa0f0",
    "deep-purple": "#6a2fa0", "bright-violet": "#8d4fd3",
    "magenta": "#d3208b", "hot-pink": "#f0559e",
    "mahogany": "#7a3b21", "greywash": "#8a8f94",
}
for k, v in REAL_INKS.items():
    pool[f"ink:{k}"] = hex_rgb(v)
# HSV 系統掃描（不遺漏色域；中明度×中高彩度帶是全膚域存活區的先驗）
for hdeg in range(0, 360, 15):
    for s in (0.65, 0.85, 1.0):
        for v in (0.35, 0.5, 0.65, 0.8, 0.92):
            rgb = np.array(colorsys.hsv_to_rgb(hdeg / 360.0, s, v))
            pool[f"hsv:{hdeg}/{int(s*100)}/{int(v*100)}"] = rgb

# ---------- 過濾：全膚域 min dE ≥ T ----------
passed = {k: v for k, v in pool.items() if min_de_vs_skin(v) >= T}
print(f"\n候選池 {len(pool)} 色 → 全膚域過線（min dE ≥ {T}）{len(passed)} 色")

# ---------- 4. 黑白互補對驗證 ----------
pair_min = min(max(de2000(srgb_to_lab(BLACK), s), de2000(srgb_to_lab(WHITE), s))
               for s in MST_LAB)
print(f"黑白互補對：每階膚色 max(dE黑, dE白) 最小值 = {pair_min:.1f} "
      f"({'≥' if pair_min >= T else '<'} T={T} → {'特赦成立' if pair_min >= T else '不成立'})")

# ---------- 5. 表達色 8 席：max-min 分散 + 色相家族配額 ----------
FAMILIES = {"red": (345, 25), "orange-yellow": (25, 90), "green": (90, 165),
            "cyan-blue": (165, 255), "purple-magenta": (255, 345)}

def hue_of(rgb):
    h, s, v = colorsys.rgb_to_hsv(*rgb)
    return h * 360.0, s

def family_of(rgb):
    h, s = hue_of(rgb)
    if s < 0.25:
        return None
    for f, (lo, hi) in FAMILIES.items():
        if lo < hi:
            if lo <= h < hi:
                return f
        elif h >= lo or h < hi:
            return f
    return None

names = list(passed.keys())
labs = {k: srgb_to_lab(passed[k]) for k in names}
anchor_labs = [srgb_to_lab(BLACK), srgb_to_lab(WHITE)] + MST_LAB  # 與錨/膚都要離得開

def score_set(sel):
    vals = []
    for i, a in enumerate(sel):
        for b in sel[i + 1:]:
            vals.append(de2000(labs[a], labs[b]))
        for al in anchor_labs:
            vals.append(de2000(labs[a], al))
    return min(vals)

# 貪婪 max-min：從離錨最遠者起，逐一加入「與已選+錨的最小距離最大」者
sel = []
cand = names[:]
start = max(cand, key=lambda k: min(de2000(labs[k], al) for al in anchor_labs))
sel.append(start)
cand.remove(start)
while len(sel) < 8 and cand:
    def gain(k):
        return min([de2000(labs[k], labs[s]) for s in sel] +
                   [de2000(labs[k], al) for al in anchor_labs])
    nxt = max(cand, key=gain)
    sel.append(nxt)
    cand.remove(nxt)

# 家族配額修補：缺席家族→踢掉「對 min-score 貢獻最小」者換入該家族最佳
def families_covered(sl):
    return {family_of(passed[k]) for k in sl}

for fam in FAMILIES:
    if fam not in families_covered(sel):
        fam_cands = [k for k in names if family_of(passed[k]) == fam and k not in sel]
        if not fam_cands:
            print(f"  ⚠ 家族 {fam} 無過線候選（全膚域可讀性殺光此家族）——誠實缺席")
            continue
        best_in = max(fam_cands, key=lambda k: min_de_vs_skin(passed[k]))
        # 踢掉同家族重複最多/移除後 min-score 最高者
        best_set, best_val = None, -1
        for out in sel:
            if family_of(passed[out]) in (None, fam):
                continue
            trial = [s for s in sel if s != out] + [best_in]
            fams = [family_of(passed[k]) for k in trial]
            if any(fams.count(f) == 0 for f in families_covered(sel) - {None}):
                continue
            v = score_set(trial)
            if v > best_val:
                best_val, best_set = v, trial
        if best_set:
            sel = best_set

# ---------- 報表 ----------
final = [("black-anchor", BLACK), ("white-anchor", WHITE)] + [(k, passed[k]) for k in sel]
print("\n" + "=" * 72)
print(f"建議色盤（2 錨 + 8 表達色）；表達色彼此+對錨+對膚 最小 dE = {score_set(sel):.1f}")
print(f"{'名稱':24s} {'sRGB hex':9s} {'linear RGB':26s} minΔE膚域  10階剖面(MST1→10)")
result = []
for k, rgb in final:
    lin = srgb_to_linear(rgb)
    prof = de_profile(rgb)
    print(f"{k:24s} #{''.join(f'{int(round(c*255)):02x}' for c in rgb)}  "
          f"({lin[0]:.3f},{lin[1]:.3f},{lin[2]:.3f})  {min(prof):5.1f}     "
          + " ".join(f"{p:3.0f}" for p in prof))
    result.append({"name": k,
                   "hex": "#" + "".join(f"{int(round(c*255)):02x}" for c in rgb),
                   "linear": [round(float(x), 4) for x in lin],
                   "min_de_skin": round(float(min(prof)), 1),
                   "de_profile": [round(float(p), 1) for p in prof]})
with open(f"{OUT_DIR}/palette_proposal.json", "w", encoding="utf-8") as f:
    json.dump({"threshold": T, "colors": result}, f, ensure_ascii=False, indent=1)

# ---------- 紙上驗證圖：10 色 × 10 階膚色 swatch 矩陣 ----------
SW, PAD = 64, 4
img = Image.new("RGB", (10 * SW, len(final) * SW), (30, 30, 30))
dr = ImageDraw.Draw(img)
for row, (k, rgb) in enumerate(final):
    for col, skin in enumerate(MST):
        x0, y0 = col * SW, row * SW
        dr.rectangle([x0, y0, x0 + SW, y0 + SW],
                     fill=tuple(int(round(c * 255)) for c in skin))
        dr.ellipse([x0 + PAD * 3, y0 + PAD * 3, x0 + SW - PAD * 3, y0 + SW - PAD * 3],
                   fill=tuple(int(round(c * 255)) for c in rgb))
img = img.resize((10 * SW, len(final) * SW), Image.NEAREST)
img.save(f"{OUT_DIR}/palette_proposal_matrix.png")
print(f"\n輸出：{OUT_DIR}/palette_proposal.json + palette_proposal_matrix.png")
print("（紙上初篩完畢；最終閘門=引擎內雙膚色截圖＋user viewport）")
