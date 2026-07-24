# -*- coding: utf-8 -*-
"""
正宗蠟筆盒色盤（user 指定十色：黑白紅橙黃綠藍紫粉棕）
=====================================================
方法（白話）：每個色詞收集多家「公認代表值」，在感知色彩空間（Lab）取
medoid（跟其他各家總距離最小的那一票）＝「大家最同意的正宗」。
來源（各自獨立的權威）：
  CSS     = 網頁標準命名色（數位慣例，值精確）
  Crayola = 經典蠟筆（蠟筆盒直覺的實物源頭）
  ISCC    = ISCC-NBS 色名系統 vivid 質心（色彩命名學的官方中點，近似值）
  NCS     = 自然色系統純色（心理物理學的「不偏任何鄰色的純X」，僅紅黃綠藍）
黑白＝墨水誠實值（碳黑 #1a1a1a／混白 #f2f2f2，不投票）。
輸出：palette_proposal.json（餵引擎矩陣腳本）＋swatch 矩陣＋每色 10 階膚色 ΔE 剖面。
"""
import json
import numpy as np
from PIL import Image, ImageDraw

OUT_DIR = "C:/games/Unreal Engine/nice_ink/Saved"

# --- 色彩數學（與 palette_pick.py 同款） ---
def srgb_to_linear(c):
    c = np.asarray(c, dtype=np.float64)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)

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
    C1, C2 = np.hypot(a1, b1), np.hypot(a2, b2)
    Cb = (C1 + C2) / 2
    G = 0.5 * (1 - np.sqrt(Cb ** 7 / (Cb ** 7 + 25.0 ** 7)))
    a1p, a2p = (1 + G) * a1, (1 + G) * a2
    C1p, C2p = np.hypot(a1p, b1), np.hypot(a2p, b2)
    h1p = np.degrees(np.arctan2(b1, a1p)) % 360
    h2p = np.degrees(np.arctan2(b2, a2p)) % 360
    dLp, dCp = L2 - L1, C2p - C1p
    if C1p * C2p == 0:
        dhp = 0.0
    else:
        dh = h2p - h1p
        if dh > 180: dh -= 360
        elif dh < -180: dh += 360
        dhp = dh
    dHp = 2 * np.sqrt(C1p * C2p) * np.sin(np.radians(dhp) / 2)
    Lbp, Cbp = (L1 + L2) / 2, (C1p + C2p) / 2
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

MST = [hex_rgb(h) for h in ["#f6ede4", "#f3e7db", "#f7ead0", "#eadaba", "#d7bd96",
                            "#a07e56", "#825c43", "#604134", "#3a312a", "#292420"]]
MST_LAB = [srgb_to_lab(c) for c in MST]

# --- 各家代表值（CSS/Crayola=精確；ISCC/NCS=文獻近似，注記於此） ---
# 粉→桃紅（user 定義換詞）：候選=CSS deeppink/hotpink＋中國傳統色桃紅近似
SOURCES = {
    "red":    {"CSS": "#ff0000", "Crayola": "#ee204d", "ISCC": "#be0032", "NCS": "#c40233"},
    "orange": {"CSS": "#ffa500", "Crayola": "#ff7538", "ISCC": "#f38400"},
    "yellow": {"CSS": "#ffff00", "Crayola": "#fce883", "ISCC": "#f3c300", "NCS": "#ffd300"},
    "green":  {"CSS": "#008000", "Crayola": "#1cac78", "ISCC": "#008856", "NCS": "#009f6b"},
    "blue":   {"CSS": "#0000ff", "Crayola": "#1f75fe", "ISCC": "#0067a5", "NCS": "#0087bd"},
    "purple": {"CSS": "#800080", "Crayola": "#926eae", "ISCC": "#9a4eae"},
    "taohong": {"CSS-deeppink": "#ff1493", "CSS-hotpink": "#ff69b4", "傳統色桃紅": "#f47983"},
    "brown":  {"CSS-brown": "#a52a2a", "CSS-saddle": "#8b4513", "Crayola": "#b4674d", "ISCC": "#80461b"},
}

def lab_chroma(lab):
    return float(np.hypot(lab[1], lab[2]))

print("=" * 76)
print("正宗色＝原型準則（Rosch 原型理論/Berlin-Kay 焦點色：『最正宗』不在色名")
print("區域中心、在最高彩度的最佳範例——上一輪 medoid≈區域中心=全體偏悶的病根）")
print("規則：各家候選取 Lab 彩度 C* 最高者；例外=棕（文獻：焦點棕=低明度的橙")
print("→限 L*<45 的暗候選內取 C* 最高）")
final = [("black", hex_rgb("#1a1a1a"), "墨水誠實值（碳黑）"),
         ("white", hex_rgb("#f2f2f2"), "墨水誠實值（混白）")]
for term, cands in SOURCES.items():
    names = list(cands.keys())
    labs = {n: srgb_to_lab(hex_rgb(cands[n])) for n in names}
    if term == "brown":
        elig = [n for n in names if labs[n][0] < 45.0]  # 焦點棕=暗（L*<45）
    else:
        elig = names
    pick = max(elig, key=lambda n: lab_chroma(labs[n]))
    detail = " ".join(f"{n}:{cands[n]}(C*{lab_chroma(labs[n]):.0f},L{labs[n][0]:.0f})"
                      for n in names)
    print(f"{term:8s} → {pick:12s} {cands[pick]}   候選: {detail}")
    final.append((term, hex_rgb(cands[pick]), f"prototype={pick}"))

print("\n" + "=" * 76)
print("正宗盤＋全膚域可讀性帳（ΔE2000 vs MST 10 階；<24=該膚階弱、帳上明記）")
print(f"{'色':7s} {'hex':9s} {'linear RGB':25s} min   MST1→10 剖面")
result = []
for term, rgb, note in final:
    lin = srgb_to_linear(rgb)
    prof = [de2000(srgb_to_lab(rgb), s) for s in MST_LAB]
    weak = [i + 1 for i, p in enumerate(prof) if p < 24.0]
    print(f"{term:7s} #{''.join(f'{int(round(c*255)):02x}' for c in rgb)}  "
          f"({lin[0]:.3f},{lin[1]:.3f},{lin[2]:.3f}) {min(prof):5.1f}  "
          + " ".join(f"{p:3.0f}" for p in prof)
          + (f"   弱膚階:{weak}" if weak else "   全域過線"))
    result.append({"name": term, "note": note,
                   "hex": "#" + "".join(f"{int(round(c*255)):02x}" for c in rgb),
                   "linear": [round(float(x), 4) for x in lin],
                   "min_de_skin": round(float(min(prof)), 1),
                   "weak_mst": weak,
                   "de_profile": [round(float(p), 1) for p in prof]})

with open(f"{OUT_DIR}/palette_proposal.json", "w", encoding="utf-8") as f:
    json.dump({"mode": "canonical-crayon", "colors": result}, f, ensure_ascii=False, indent=1)

SW = 64
img = Image.new("RGB", (10 * SW, len(final) * SW), (30, 30, 30))
dr = ImageDraw.Draw(img)
for row, (term, rgb, _) in enumerate(final):
    for col, skin in enumerate(MST):
        x0, y0 = col * SW, row * SW
        dr.rectangle([x0, y0, x0 + SW, y0 + SW],
                     fill=tuple(int(round(c * 255)) for c in skin))
        dr.ellipse([x0 + 12, y0 + 12, x0 + SW - 12, y0 + SW - 12],
                   fill=tuple(int(round(c * 255)) for c in rgb))
img.save(f"{OUT_DIR}/palette_canonical_matrix.png")
print(f"\n輸出：{OUT_DIR}/palette_proposal.json（canonical 模式，餵引擎矩陣）")
print(f"      {OUT_DIR}/palette_canonical_matrix.png")
