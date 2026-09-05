# -*- coding: utf-8 -*-
"""量真機截圖的版面不變量（不開引擎）。

user 2026-09-04：「全部完成並都有截圖量測清楚確認無誤後再讓我驗收」。
截圖＝Tools/RoboTest/robo_ui_shots.py 產出的 uishot_*.png（1920x1080）。

量的是**畫面上真的畫出來的像素**，不是程式裡的常數——常數對不對是編譯期的事，
這裡要回答的是「玩家看到的那張圖對不對」。
"""
import io, os, sys
import numpy as np
from PIL import Image

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
SHOTDIR = os.path.join(ROOT, "Saved/Screenshots/WindowsEditor")
U, MARGIN, CAP_H, PITCH = 4, 24, 32, 64
fails = []


def check(name, ok, detail=""):
    print(("  PASS " if ok else "  FAIL ") + name + (("  | " + detail) if detail else ""))
    if not ok:
        fails.append(name)


# 門檻 190 不是隨手挑的：鍵帽是 Paper(242,232,214) α0.94 疊在膚色上 ⇒ 藍通道只剩 208，
# 用 >210 會把**鍵帽整個濾掉**、只量到純白的滑鼠圖（第一版就是這樣假通過的）。
# 膚色 (163,121,110) 離 190 還很遠，不會誤收。
def bright_rows(a, x0, x1, y0, y1, thr=190):
    """在指定區塊找『近白 chrome』的連通橫帶（鍵帽／滑鼠圖）。"""
    sub = a[y0:y1, x0:x1]
    m = (sub[:, :, 0] > thr) & (sub[:, :, 1] > thr) & (sub[:, :, 2] > thr)
    rows = np.where(m.sum(axis=1) >= 3)[0]
    if len(rows) == 0:
        return []
    # 這裡曾經寫成 groups.append(cur) + cur.clear()——append 的是**參照**，
    # clear 之後全部變成同一個空 list ⇒ 列距全報 0。閘門自己也要被測。
    groups, cur = [], [rows[0]]
    for r in rows[1:]:
        if r - cur[-1] <= 3:
            cur.append(r)
        else:
            groups.append(cur)
            cur = [r]
    groups.append(cur)
    out = []
    for g in groups:
        if len(g) < 6:
            continue
        cols = np.where(m[g[0]:g[-1] + 1].any(axis=0))[0]
        box = (x0 + int(cols[0]), y0 + int(g[0]), x0 + int(cols[-1]), y0 + int(g[-1]))
        out.append(box)
    return out


def lum(c):
    def f(v):
        v /= 255.0
        return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
    r, g, b = [f(float(x)) for x in c[:3]]
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def contrast(a, b):
    la, lb = lum(a), lum(b)
    return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)


def measure(path):
    print("\n=== %s ===" % os.path.basename(path))
    im = Image.open(path).convert("RGB")
    a = np.array(im)
    H, W, _ = a.shape
    check("解析度 1920x1080", (W, H) == (1920, 1080), "%dx%d" % (W, H))

    # --- 右緣操作列：glyph 的右緣必須是同一條軸，列距必須恆定 ---
    allb = bright_rows(a, W - 200, W - 4, 300, 800)
    # **glyph 與動詞是兩種東西**：glyph 高 32、動詞字高 ~12 ⇒ 高度 >=15 才是 glyph。
    # 混在一起量會得到「列距 [64,42,64]」這種既非列距也非行距的數字。
    # glyph 的高度是**已知的**（KeycapH 32）⇒ 用 15~45 的視窗篩。
    # 沒有上限的話，明亮的牆面會被量成「高 239 的 glyph」（站著那張實錘）；
    # 而「色彩平坦度」不能當篩選——鍵帽裡有深色鍵名、滑鼠圖有紅色亮鍵，
    # 它們的色彩起伏本來就高，用平坦度會把真的 glyph 殺掉。
    boxes = [b for b in allb if 15 <= (b[3] - b[1] + 1) <= 45]
    texts = [b for b in allb if (b[3] - b[1] + 1) < 15]
    if len(boxes) >= 3:
        rights = [b[2] for b in boxes]
        tops = [b[1] for b in boxes]
        pitches = [tops[i + 1] - tops[i] for i in range(len(tops) - 1)]
        check("操作列 glyph 共用單一右緣", max(rights) - min(rights) <= 2,
              "極差 %dpx（右緣 x=%d）" % (max(rights) - min(rights), max(rights)))
        check("操作列右邊距 = %d" % MARGIN, abs((W - 1 - max(rights)) - MARGIN) <= 3,
              "實測 %dpx" % (W - 1 - max(rights)))
        check("操作列列距恆定 = %d" % PITCH,
              max(pitches) - min(pitches) <= 3 and abs(np.mean(pitches) - PITCH) <= 4,
              "實測 %s" % pitches)
        heights = [b[3] - b[1] + 1 for b in boxes]
        check("glyph 高度一致", max(heights) - min(heights) <= 4, "實測 %s" % heights)
    elif "draw" in os.path.basename(path):
        # 亮度分割在**白色障子牆**前分不出鍵帽（牆本身就是一大片亮）。
        # 那不代表版面錯——改成**去規格說的位置取樣**：右緣 W-Margin、
        # 列距 64、置中排列，逐格驗「那裡真的有一顆 chrome，而且跟周圍有邊界」。
        # 這比「找得到就好」強：它會在版面移位時失敗，而不是在背景變亮時失敗。
        # **負向測試打臉過一次**：第一版只驗「有邊界對比」，把期望邊距改成 120
        # （指到白牆上）它照樣通過——牆的框線也有對比。空洞契約。
        # 現制驗**兩個族群同時存在**：紙色 chrome（鍵帽／glyph 本體）＋深色墨
        #（鍵名或陰影）。白牆只有前者，木框只有後者，都過不了。
        PAPER = np.array([242.0, 232.0, 214.0])
        ok_rows, details = 0, []
        for n_rows in (3, 5, 6):
            hit = 0
            for i in range(n_rows):
                top = int(H * 0.5 - n_rows * PITCH * 0.5 + i * PITCH)
                x1 = W - MARGIN
                box = a[top:top + CAP_H, max(0, x1 - CAP_H):x1].astype(float)
                if box.size == 0:
                    continue
                px = box.reshape(-1, 3)
                paperish = (np.abs(px - PAPER).max(axis=1) <= 22).mean()
                darkish = (px.mean(axis=1) < 90).mean()
                if paperish >= 0.20 and 0.02 <= darkish <= 0.55:
                    hit += 1
            if hit == n_rows:
                ok_rows = n_rows
                details = ["%d 列全部命中（紙色%%＋墨%%皆達標）" % n_rows]
                break
        check("操作列在規格位置上（右緣 W-%d、列距 %d、置中）" % (MARGIN, PITCH),
              ok_rows > 0, "; ".join(details) or "3/5/6 列都對不上")

    # --- 四邊共用同一個邊距：右上資源列 ---
    # 資源列最右邊的元件是**琥珀色的錢幣圖示**，不是白色數字——
    # 用近白遮罩會量到數字的右緣（少了 icon 寬＋間距），那是量錯對象不是版面歪。
    sub = a[8:80, W - 260:W - 2].astype(int)
    # 膚色 (163,121,110) 會通過寬鬆的琥珀條件 ⇒ 收緊：B 要夠低、紅藍差要夠大
    amber = ((sub[:, :, 0] > 190) & (sub[:, :, 2] < 90)
             & ((sub[:, :, 0] - sub[:, :, 2]) > 120))
    ys, xs = np.where(amber)
    if len(xs) > 30:
        icon_right = W - 260 + int(xs.max())
        icon_top = 8 + int(ys.min())
        check("資源列右邊距 = %d（量最右的錢幣圖示）" % MARGIN,
              abs((W - 1 - icon_right) - MARGIN) <= 4, "實測 %dpx" % (W - 1 - icon_right))
        # 錢幣圖示（T_UI_Cash）自帶透明邊 ⇒ 墨跡頂端比版面 Y 低幾像素；
        # 量的是墨跡不是版面框，容差放寬並記帳（輸入 glyph 已在 bake 端裁掉留白）
        check("資源列上邊距 = %d（±8，圖示自帶透明邊）" % MARGIN, abs(icon_top - MARGIN) <= 8,
              "實測 %dpx" % icon_top)

    # --- 無面板：右緣操作列的背景不得是均勻半透明色塊 ---
    strip = a[300:800, W - 200:W - 40].reshape(-1, 3).astype(float)
    check("操作列底下無面板（背景仍是場景，非均勻色塊）", strip.std(axis=0).mean() > 6.0,
          "背景標準差 %.1f（面板會把它壓到 ~0）" % strip.std(axis=0).mean())

    # --- 動詞可讀性：無面板時對比是背景的函數，所以要量**兩個**量 ---
    #   ① 文字 vs 背景：隨背景跑，單看它會誤判（Meccha 也一樣）
    #   ② 文字 vs 自身陰影：構造保證、與背景無關——這才是「無面板」成立的理由
    if texts:
        t = texts[len(texts) // 2]
        pad = 3
        core = a[t[1]:t[3] + 1, t[0]:t[2] + 1].reshape(-1, 3).astype(float)
        ring = a[max(0, t[1] - pad):t[3] + 1 + pad, max(0, t[0] - pad):t[2] + 1 + pad]
        ringpx = ring.reshape(-1, 3).astype(float)
        bright = core[(core[:, 0] > 200)]
        dark = ringpx[np.argsort(ringpx.sum(axis=1))[:max(8, len(ringpx) // 40)]]
        bg = a[t[1]:t[3] + 1, W - 460:W - 300].reshape(-1, 3).astype(float).mean(axis=0)
        if len(bright) > 10:
            c_bg = contrast(bright.mean(axis=0), bg)
            c_sh = contrast(bright.mean(axis=0), dark.mean(axis=0))
            print("  INFO 動詞 vs 背景對比 %.2f（無面板 ⇒ 隨背景跑，不當閘門）" % c_bg)
            check("動詞 vs 自身陰影對比 >= 4.5（與背景無關的構造保證）", c_sh >= 4.5,
                  "實測 %.2f  字 %s  陰影 %s"
                  % (c_sh, bright.mean(axis=0).round(0), dark.mean(axis=0).round(0)))


def main():
    shots = sorted(f for f in os.listdir(SHOTDIR) if f.startswith("uishot_") and f.endswith(".png"))
    if not shots:
        print("找不到 uishot_*.png——先跑 Tools/RoboTest/robo_ui_shots.py")
        return 1
    for f in shots:
        measure(os.path.join(SHOTDIR, f))
    print("")
    print("總計：%d 項失敗" % len(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
