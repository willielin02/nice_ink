"""鍵帽貼圖（2026-09-10 三版；user：「有辦法把整個遊戲的按鍵指引都改成 Meccha／PEAK 同款嗎？」）。

**三版史，每一版都是被畫面推翻的，記著免得再走一次**：
  一版 白面＋黑框＋投影   → user：「黑底框很突兀，在清一色白色 icon 中」（**兩個顏色**）
  二版 白線稿＋白字       → 與滑鼠同家、但九款出貨遊戲裡沒有一款拿空心框當鍵
                            （空心框在那些遊戲裡是**道具格**的語彙；§15.12 實測）
  三版 **實心白＋深色字**  → Meccha 23px／PEAK 21px 的做法，九款裡最多人用的那一種

**09-06 曾經否決過「白色實心塊」**（原話：「看不太出來是在講按鍵」），所以這一版刻意避開
那次的三個成因：
  1. 當時是**硬陰影**（讀成貼紙）⇒ 現在是 dy／blur 的軟投影，只為了在障子牆（208）上有輪廓
  2. 當時鍵高是動詞字高的 **2.0 倍**（§15.12 量到；參照是 1.3~1.4）⇒ 鍵高 32→24
  3. 當時旁邊沒有線稿滑鼠 ⇒ 現在「實心鍵＋線稿滑鼠」有出貨先例（RV There Yet 同一行就是這樣）

**狀態＝同一張圖乘透明度**（2026-09-11 十一修，user 定案：「我想要按鍵是在可按的樣子的基礎上調整透明度即可」）：
可按＝實心白＋深色字；不可按＝**同一張圖 × NiUi::KeycapDimAlpha（0.45，與同欄動詞、滑鼠圖示同值）**。
09-10 的「空心灰框」版退役（當時的理由＝整顆乘 alpha 在障子牆上只剩 19 階對比；user 知情選擇）。

=====================================================================================
**十修（2026-09-11）：字也烘進圖裡，一顆鍵一張。**
user 在自己的視窗（2560×1380）量到 ESC 上／下留白 6／5、C 7／6，而且 C 帽 25px、ESC 帽 26px
——同一欄兩顆鍵連高度都不一樣。原因不在任何一個常數：Slate 把**盒子**貼齊整數像素，
又把**每個字形**各自貼齊整數像素，兩次取整互相獨立 ⇒ 在非整數縮放（1.234）下，
帽高 26 配字高 15 剩 11 ＝ 奇數，怎麼擺都是 6／5。九修之前的八修全在調抬升量，
調的是連續值，而病是離散的。

正解＝**把字畫進帽裡再一起縮放**（PEAK／Kenney Input Prompts 就是這樣：一顆鍵一張圖）：
字在 4× 貼圖裡以 1/16 px 精度置中，畫到螢幕上時整顆帽被當成一張圖重取樣 ⇒ 上下邊緣的
半像素灰一樣深、左右也一樣 ⇒ 對稱是構造保證，不再取決於縮放倍率湊不湊巧。
9-slice（keycap.png／keycap_dim.png）保留給表裡沒有的鍵名當保底。

貼圖幾何（設計單位＝1080p px；貼圖 TEX_PER_UNIT=128/24 texel/單位）：
  盒 24 高（= NiUi::KeycapH，就是 Slate 端 SBox 的高）＝ 帽體 + 上下各 INSET 給投影
  帽體 = 24 − 2×INSET；單字母＝正方形；多字母＝墨跡寬 + 2×PAD
  寬度補到 2 的冪（引擎 NPOT 不生 mip），帽靠左、右邊留透明；C++ 用 UV 只取帽那一段。
  尺寸表 ⇒ Source/NiceInk/Public/NiceInkKeycapData.h（本檔生成，與 telop_glyphs.py 同一套做法）。

輸出：SourceAssets/UI/chrome/keycap.png（9-slice 保底，128×128）
      SourceAssets/UI/chrome/keys/<KEY>.png（一顆鍵一張；不可按態＝同圖乘透明度，不另烘）
Usage: python -X utf8 Tools/AssetPrep/keycap_texture.py [--preview]
"""
import json
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT_DIR = os.path.join(ROOT, "SourceAssets", "UI", "chrome")
KEYS_DIR = os.path.join(OUT_DIR, "keys")
HEADER = os.path.join(ROOT, "Source", "NiceInk", "Public", "NiceInkKeycapData.h")
FONT_BOLD = os.path.join(ROOT, "SourceAssets", "Fonts", "NotoSans", "NotoSans-Bold.ttf")

# --- 設計單位（1080p px）---
BOX_H = 24.0                     # NiUi::KeycapH：Slate 端 SBox 的高＝整張圖的高
INSET = 1.0                      # 帽體到盒邊：投影的空間（四邊等距 ⇒ 單鍵帽體仍是方的）
CAP_H = BOX_H - 2.0 * INSET      # 22：帽體高
RADIUS = 3.0                     # 帽體圓角（三版實測值：4 顯示 px 在 32→24 的貼圖裡就是 3）
PAD = 6.0                        # NiUi::KeycapPad：墨跡到帽體邊（多字母左右）
KEY_PT = 13                      # NiType::KeyLabel＝Noto Sans Bold 13pt
KEY_EM = KEY_PT * 96.0 / 72.0    # 17.333 設計 px（Slate 的字級是 pt）
SHADOW_DY, SHADOW_BLUR, SHADOW_A = 0.75, 0.9, 0.35
DIM_ALPHA = 0.45                 # 預覽用；正本在 NiUi::KeycapDimAlpha（不可按＝整顆乘這個）
# 字的加粗（設計 px／每側）。同一個字型檔，Slate 排出來的字比烘進圖裡的**重 20%**
#（實測 user 視窗：ESC 墨量 245 vs 204、C 76 vs 62）——Slate 的字形 alpha 直接在 sRGB 值域
# 混色（偏重），而貼圖被 GPU 解成線性再濾波（深字在白底上偏細）。把筆畫每側加 0.2px 補回去，
# 讓烘出來的鍵名跟旁邊 Slate 排的動詞看起來是同一支筆寫的。
EMBOLDEN = 0.2

# --- 貼圖解析度 ---
TEX_H = 128
TEX_PER_UNIT = TEX_H / BOX_H     # 5.333 texel／設計 px
SUP = 4                          # 超取樣：畫在 4× 再下採樣＝抗鋸齒

# --- 顏色（sRGB 0-255；與 NiHudColor 對齊）---
FACE = 242                       # Paper
INK = (10, 10, 12)               # Ink
SHADOW_RGB = 8

# 全站會出現的鍵名（BuildControlHints ＋ 大廳 C ＋ 選單 ESC ＋ 墨杯盤 Q／SCROLL）。
# 表裡沒有的鍵名 C++ 會退回 9-slice＋Slate 排字（不會消失，只是回到±1px 的世界）。
KEYS = ["C", "E", "F", "G", "L", "Q", "ESC", "ENTER", "SHIFT", "TAB", "WASD", "SCROLL"]


def u2s(v):
    """設計單位 → 超取樣貼圖 px。"""
    return v * TEX_PER_UNIT * SUP


def rrect_mask(w_s, h_s, x0, y0, x1, y1, r):
    """浮點遮罩（超取樣尺度）。座標與半徑都是超取樣 px。"""
    im = Image.new("L", (int(w_s), int(h_s)), 0)
    ImageDraw.Draw(im).rounded_rectangle([x0, y0, x1 - 1, y1 - 1], radius=r, fill=255)
    return np.asarray(im).astype(np.float64) / 255.0


def shadow(body, dy_s, blur_s, alpha):
    sh = Image.fromarray((body * 255).astype(np.uint8))
    sh = sh.transform(sh.size, Image.AFFINE, (1, 0, 0, 0, 1, -dy_s), resample=Image.BILINEAR)
    sh = sh.filter(ImageFilter.GaussianBlur(blur_s))
    return np.asarray(sh).astype(np.float64) / 255.0 * alpha


def over(dst_rgb, dst_a, src_rgb, src_a):
    """一般 alpha 合成（RGB 以 0~255 直線混合＝在 sRGB 值域上混，與 Slate 的取樣一致）。"""
    out_a = src_a + dst_a * (1.0 - src_a)
    safe = np.maximum(out_a, 1e-6)
    out_rgb = (src_rgb * src_a[..., None] + dst_rgb * dst_a[..., None] * (1.0 - src_a[..., None])) / safe[..., None]
    return out_rgb, out_a


def downsample(rgb, a, factor):
    """預乘後縮小再除回來：直接縮 straight-alpha 會在邊緣拖出暗邊。"""
    prem = np.concatenate([rgb * a[..., None], a[..., None] * 255.0], axis=2)
    im = Image.fromarray(np.clip(prem, 0, 255).astype(np.uint8), "RGBA")
    im = im.resize((im.width // factor, im.height // factor), Image.LANCZOS)
    out = np.asarray(im).astype(np.float64)
    oa = out[..., 3] / 255.0
    orgb = out[..., :3] / np.maximum(oa, 1e-6)[..., None]
    return np.clip(orgb, 0, 255), np.clip(oa, 0, 1)


def label_mask(text, w_s, h_s):
    """把鍵名畫在超取樣畫布上，**墨跡包圍盒**的中心對到畫布中心（1/SUP texel 精度）。

    置中的是墨跡不是行框——行框置中正是 Slate 端把大寫字放偏下的原因（§15.13 七修）。
    Q 的尾巴也算墨跡：user 量的就是墨到帽邊的距離。
    """
    font = ImageFont.truetype(FONT_BOLD, u2s(KEY_EM))
    big = Image.new("L", (int(w_s) * 2, int(h_s) * 2), 0)
    d = ImageDraw.Draw(big)
    d.text((big.width / 2, big.height / 2), text, font=font, fill=255, anchor="mm",
           stroke_width=int(round(u2s(EMBOLDEN))), stroke_fill=255)
    arr = np.asarray(big)
    ys, xs = np.where(arr > 0)
    cx = (xs.min() + xs.max() + 1) / 2.0
    cy = (ys.min() + ys.max() + 1) / 2.0
    ink_w_s = xs.max() - xs.min() + 1
    ink_h_s = ys.max() - ys.min() + 1
    # 平移使墨跡中心＝目標畫布中心（整數位移；剩下 <1 超取樣 px ＝ <1/16 texel）
    dx = int(round(w_s / 2.0 - cx))
    dy = int(round(h_s / 2.0 - cy))
    shifted = Image.new("L", (int(w_s), int(h_s)), 0)
    shifted.paste(big, (dx, dy))
    return np.asarray(shifted).astype(np.float64) / 255.0, ink_w_s / (TEX_PER_UNIT * SUP), ink_h_s / (TEX_PER_UNIT * SUP)


def ink_width_units(text):
    font = ImageFont.truetype(FONT_BOLD, u2s(KEY_EM))
    big = Image.new("L", (4096, 1024), 0)
    ImageDraw.Draw(big).text((2048, 512), text, font=font, fill=255, anchor="mm",
                             stroke_width=int(round(u2s(EMBOLDEN))), stroke_fill=255)
    arr = np.asarray(big)
    xs = np.where(arr.max(axis=0) > 0)[0]
    return (xs.max() - xs.min() + 1) / (TEX_PER_UNIT * SUP)


def bake_cap(box_w, box_h, label=None):
    """回傳 (rgb, a) 已下採樣到貼圖解析度；盒 box_w×box_h 設計單位。只有可按態；不可按＝畫的時候乘透明度。"""
    w_s, h_s = int(round(u2s(box_w))), int(round(u2s(box_h)))
    body = rrect_mask(w_s, h_s, u2s(INSET), u2s(INSET), w_s - u2s(INSET), h_s - u2s(INSET), u2s(RADIUS))
    rgb = np.zeros((h_s, w_s, 3))
    a = np.zeros((h_s, w_s))
    # 投影＝在亮底上撐出輪廓（我們的世界是亮的：皮膚 175／榻榻米 176／障子 208，
    # 而 Meccha／PEAK 的鍵下面是中暗的牆 ⇒ 他們不需要這一層，我們需要）
    rgb, a = over(rgb, a, np.zeros((h_s, w_s, 3)) + SHADOW_RGB, shadow(body, u2s(SHADOW_DY), u2s(SHADOW_BLUR), SHADOW_A))
    rgb, a = over(rgb, a, np.zeros((h_s, w_s, 3)) + float(FACE), body)
    ink_wh = None
    if label:
        m, iw, ih = label_mask(label, w_s, h_s)
        ink_wh = (iw, ih)
        rgb, a = over(rgb, a, np.zeros((h_s, w_s, 3)) + np.array(INK, dtype=np.float64), m)
    rgb, a = downsample(rgb, a, SUP)
    return rgb, a, ink_wh


def to_image(rgb, a):
    out = np.concatenate([np.clip(rgb, 0, 255), np.clip(a, 0, 1)[..., None] * 255.0], axis=2)
    return Image.fromarray(out.round().astype(np.uint8), "RGBA")


def pot(n):
    p = 1
    while p < n:
        p *= 2
    return p


def bake_nine_slice():
    """保底用的 9-slice（盒＝正方形 24；字由 Slate 排）。"""
    rgb, a, _ = bake_cap(BOX_H, BOX_H)
    return to_image(rgb, a)


def bake_key(key):
    """一顆鍵一張：回傳 (Image, entry)。entry 的尺寸是設計單位（盒＝Slate SBox 的大小）。"""
    if len(key) <= 1:
        box_w = BOX_H                                   # 單字母恆為正方形（PEAK 1/2/3/4 逐位相同）
    else:
        box_w = ink_width_units(key) + 2.0 * PAD + 2.0 * INSET
    rgb, a, ink_wh = bake_cap(box_w, BOX_H, key)
    cap_px_w, cap_px_h = rgb.shape[1], rgb.shape[0]
    tex_w = pot(cap_px_w)
    canvas = Image.new("RGBA", (tex_w, TEX_H), (0, 0, 0, 0))
    canvas.paste(to_image(rgb, a), (0, 0))
    entry = dict(key=key, tex_w=tex_w, tex_h=TEX_H, cap_px_w=cap_px_w, cap_px_h=cap_px_h,
                 box_w=cap_px_w / TEX_PER_UNIT, box_h=cap_px_h / TEX_PER_UNIT,
                 ink_w=ink_wh[0], ink_h=ink_wh[1])
    return canvas, entry


def write_header(entries):
    lines = [
        "#pragma once",
        "// GENERATED by Tools/AssetPrep/keycap_texture.py — 不要手改；改了烘焙腳本再跑一次。",
        "// 一顆鍵一張貼圖（/Game/UI/Keys/T_Key_<KEY>；不可按＝同圖 × NiUi::KeycapDimAlpha），字烘在圖裡＝上下左右留白",
        "// 由構造保證對稱（§15.13 十修）。BoxW／BoxH＝設計單位（1080p px）＝Slate 端 SBox 的大小、",
        "// 也是 canvas 端 ×UiScale 的畫布大小；U1＝貼圖右邊補到 2 的冪，畫的時候 UV 只取 [0,U1]。",
        "#include \"CoreMinimal.h\"",
        "",
        "namespace NiKeycapData",
        "{",
        "\tstruct FEntry",
        "\t{",
        "\t\tconst TCHAR* Key;",
        "\t\tfloat BoxW;",
        "\t\tfloat BoxH;",
        "\t\tfloat U1;      // 帽在貼圖上佔的寬度比例",
        "\t};",
        "\tconstexpr float TexPerUnit = %.6ff;" % TEX_PER_UNIT,
        "\tconstexpr float CapInset = %.3ff;    // 盒邊到帽體（投影的空間）" % INSET,
        "\tconstexpr FEntry Entries[] =",
        "\t{",
    ]
    for e in entries:
        lines.append("\t\t{ TEXT(\"%s\"), %.4ff, %.4ff, %.6ff },   // 貼圖 %dx%d、墨跡 %.1fx%.1f" % (
            e["key"], e["box_w"], e["box_h"], e["cap_px_w"] / e["tex_w"], e["tex_w"], e["tex_h"], e["ink_w"], e["ink_h"]))
    lines += [
        "\t};",
        "\tconstexpr int32 Num = %d;" % len(entries),
        "",
        "\t/** 表查找；沒有＝nullptr（呼叫端退回 9-slice＋Slate 排字）。 */",
        "\tinline const FEntry* Find(const FString& Key)",
        "\t{",
        "\t\tfor (const FEntry& E : Entries)",
        "\t\t{",
        "\t\t\tif (Key.Equals(E.Key, ESearchCase::CaseSensitive)) { return &E; }",
        "\t\t}",
        "\t\treturn nullptr;",
        "\t}",
        "}",
        "",
    ]
    with open(HEADER, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))


# ----------------------------------------------------------------------------
# 自查：所有鍵在 1.0 與 1.234（user 視窗）兩個縮放下、亮底暗底各一。**不開引擎**。
# ----------------------------------------------------------------------------
def preview(entries):
    scales = [1.0, 1.234, 1.55]
    sheet_w = 1400
    row_h = 70
    sheet = Image.new("RGB", (sheet_w, row_h * len(scales) * 2 + 20), (60, 48, 36))
    d = ImageDraw.Draw(sheet)
    y = 10
    for bg_i, bg in enumerate([(60, 48, 36), (208, 204, 196)]):
        for s in scales:
            d.rectangle([0, y, sheet_w, y + row_h], fill=bg)
            x = 16
            for e in entries:
                for dim in (False, True):
                    tex = Image.open(os.path.join(KEYS_DIR, "%s.png" % e["key"]))
                    cap = tex.crop((0, 0, e["cap_px_w"], e["cap_px_h"]))
                    if dim:   # 不可按＝同圖乘透明度（引擎端 NiUi::KeycapDimAlpha）
                        r_, g_, b_, a_ = cap.split()
                        cap = Image.merge("RGBA", (r_, g_, b_, a_.point(lambda v: int(v * DIM_ALPHA))))
                    w, h = int(round(e["box_w"] * s)), int(round(e["box_h"] * s))
                    cap = cap.resize((w, h), Image.LANCZOS)
                    sheet.paste(cap, (x, y + (row_h - h) // 2), cap)
                    x += w + 6
                x += 6
            d.text((sheet_w - 60, y + 4), "x%.3f" % s, fill=(255, 255, 255) if bg_i == 0 else (20, 20, 20))
            y += row_h
    out = os.path.join(ROOT, "Saved", "UiMock", "keycap_preview.png")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    sheet.save(out)
    print("preview:", out)


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    os.makedirs(KEYS_DIR, exist_ok=True)
    tex = bake_nine_slice()
    tex.save(os.path.join(OUT_DIR, "keycap.png"))
    for stale in [os.path.join(OUT_DIR, "keycap_dim.png")] + [os.path.join(KEYS_DIR, "%s_dim.png" % k) for k in KEYS]:
        if os.path.exists(stale):
            os.remove(stale)          # 十一修：不可按態不再另烘（匯入端是鏡像，會跟著刪資產）
    print("baked 9-slice: keycap.png", tex.size)

    entries = []
    for key in KEYS:
        img, entry = bake_key(key)
        img.save(os.path.join(KEYS_DIR, "%s.png" % key))
        entries.append(entry)
        # 自查：墨跡包圍盒在貼圖裡是否置中（左右／上下差 < 0.05 texel 的量化極限＝1/SUP）
        a = np.asarray(img).astype(int)
        cap = a[:entry["cap_px_h"], :entry["cap_px_w"]]
        ink = (cap[..., :3].max(axis=2) < 110) & (cap[..., 3] > 200)
        iy, ix = np.where(ink)
        L, R = ix.min(), entry["cap_px_w"] - 1 - ix.max()
        T, B = iy.min(), entry["cap_px_h"] - 1 - iy.max()
        print("  %-6s 貼圖 %4dx%3d 盒 %.2fx%.2f 墨 %.1fx%.1f | 貼圖內深墨 L/R %d/%d T/B %d/%d" % (
            key, entry["tex_w"], entry["tex_h"], entry["box_w"], entry["box_h"], entry["ink_w"], entry["ink_h"], L, R, T, B))
    write_header(entries)
    with open(os.path.join(KEYS_DIR, "keys.json"), "w", encoding="utf-8") as f:
        json.dump(entries, f, indent=1, ensure_ascii=False)
    print("header:", HEADER)
    if "--preview" in sys.argv:
        preview(entries)


if __name__ == "__main__":
    main()
