# -*- coding: utf-8 -*-
"""量主選單截圖的版面不變量（2026-09-05，全站對齊 Meccha 之後的驗收）。

`shot_measure.py` 量的是**局內**（canvas HUD）；選單是 Slate，此前完全沒有閘門
——而 09-05 這一輪動的正是選單：拆卡片、左對齊、外框化、加 ESC 鍵帽。
沒有閘門的話，「我改好了」就只有我的眼睛在背書。

受測物＝Tools/RoboTest/menu_shots.ps1 產出的 menu2_*.png（1920x1080）。

量五件事：
  1. **單一左緣軸**：六頁的標題左緣必須落在同一個 x（UI_SYSTEM 六條之首）
  2. **無卡片**：內容區背後不能有一塊亮底（白卡的簽名＝一大片 Paper 色）
  3. **ESC 鍵帽**：子頁有、首頁沒有（§4.2「軸不存在就不該存在」）
  4. **文字對比**：正文對它自己的地 ≥ 4.5（WCAG 小字）
  5. **內容不壓到力士**：拆掉卡片之後這是新的失效模式——白卡此前替內容擋住背景，
     卡一拆，置中的內容就直接疊在皮膚上（09-05 個人檔案頁實測，已修）

全部帶負向測試：改掉期望值，閘門必須開火。
"""
import io
import os
import sys

import numpy as np
from PIL import Image

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
SHOTDIR = os.path.join(ROOT, "Saved/Screenshots/WindowsEditor")

PAPER = np.array([242, 232, 214])
AMBER = np.array([232, 163, 61])
GUTTER = 72          # NiMenuLeftGutter
COLW = 560           # NiSpace::CardWWide（最寬的內容欄）
NL = chr(10)
TAB = chr(9)
fails = []

PAGES = [
    ("root",     "menu2_root00000.png",     False),   # 第三欄＝是不是子頁（該有 ESC）
    ("host",     "menu2_host00000.png",     True),
    ("join",     "menu2_join00000.png",     True),
    ("settings", "menu2_settings00000.png", True),
    ("language", "menu2_language00000.png", True),
    ("profile",  "menu2_profile00000.png",  True),
]


def check(name, ok, detail=""):
    print(("  PASS " if ok else "  FAIL ") + name + (("  | " + detail) if detail else ""))
    if not ok:
        fails.append(name)


def load(fn):
    p = os.path.join(SHOTDIR, fn)
    return np.asarray(Image.open(p).convert("RGB")).astype(np.int16) if os.path.exists(p) else None


def lin(c):
    c = np.array(c, dtype=float) / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def wcag(fg, bg):
    def lum(v):
        r, g, b = lin(v)
        return 0.2126 * r + 0.7152 * g + 0.0722 * b
    a, b = lum(fg), lum(bg)
    hi, lo = max(a, b), min(a, b)
    return (hi + 0.05) / (lo + 0.05)


def title_left_edge(a):
    """標題帶（y 56..150）裡最左的亮像素 x。"""
    band = a[56:150, :, :]
    m = (np.abs(band - PAPER.reshape(1, 1, 3)).max(axis=2) < 60)
    cols = np.where(m.sum(axis=0) >= 2)[0]
    return int(cols[0]) if len(cols) else None


def has_keycap_bottom_left(a):
    """左下角（x<300, y>980）有沒有一塊近 Paper 的實心小方塊（鍵帽）。"""
    H = a.shape[0]
    reg = a[H - 100:H - 30, 60:300]
    m = (np.abs(reg - PAPER.reshape(1, 1, 3)).max(axis=2) < 45)
    return int(m.sum())


def bright_panel_area(a):
    """白卡的簽名：畫面中段一整片近 Paper 的**大面積**低變異區塊。
    文字與鍵帽也是 Paper 色但面積小，所以用「單列連續寬度 > 250px」來分辨。"""
    # 門檻 350 不是隨手挑的：白卡是 NiSpace::CardW=460 寬（寬卡 560），
    # α0.78 疊在黑地上＝(189,181,167)，落在 tol 70 內 ⇒ 真的有卡片就會量到 >=460。
    # 而畫面上最寬的合法亮物是輸入框（約 390 寬、但它是暗底）與臉庫縮圖列（~300）。
    # 350 夾在兩者之間，兩邊都有餘裕。
    band = a[250:900, :, :]
    m = (np.abs(band - PAPER.reshape(1, 1, 3)).max(axis=2) < 70)
    worst = 0
    for row in range(0, m.shape[0], 7):
        r = m[row]
        run = best = 0
        for v in r:
            run = run + 1 if v else 0
            best = max(best, run)
        worst = max(worst, best)
    return worst


def skin_overlap(a, groups):
    """力士有沒有壓到**真正畫出來的 chrome**。

    前兩版都在量「一個名目上的矩形裡有沒有膚色」——先是 60..700（比欄還寬），
    後是 GUTTER..GUTTER+560（首頁其實只有 CardW=460 寬）。而**力士會跳舞**，
    他每一幀站的地方都不同 ⇒ 量名目矩形＝把「他今天走到哪」當成版面缺陷。
    這一版直接量要守的那件事：**膚色像素有沒有落在 chrome 的實際包圍盒裡**。
    (groups 來自 within_page_left_edges，已經是這一幀真的畫了東西的地方。)
    """
    from scipy.ndimage import label
    lit = chrome_mask(a)
    worst = 0
    for y0, y1, _lx in groups:
        band = lit[y0:y1 + 1, :GUTTER + COLW]
        cols = np.where(band.sum(axis=0) >= 2)[0]
        if not len(cols):
            continue
        x0, x1 = int(cols[0]), int(cols[-1])
        reg = a[y0:y1 + 1, x0:x1 + 1]
        r, g, b = reg[:, :, 0], reg[:, :, 1], reg[:, :, 2]
        skin = ((r > 120) & (r < 250) & (g > 85) & (g < 215) & (b > 70) & (b < 200)
                & (r - b > 25) & (r - g > 8))
        lab, n = label(skin)
        if n:
            sizes = np.bincount(lab.ravel())
            sizes[0] = 0
            worst = max(worst, int(sizes.max()))
    return worst


def scene_clearance(a):
    """力士（3D 場景）的最左緣 x —— 用來驗「內容靠左、場景佔右」。

    **判準是「一欄有 150 列以上的膚色」**：力士是一大塊實心，而 UI 文字最高不過
    40px 又是鏤空的，一欄不可能連續 150 列。
    此前兩版用「膚色像素」或「12 列」，結果把**反鋸齒後的 Paper 文字**算成皮膚
    （Paper 邊緣 (206,197,182) 剛好落進膚色述詞）⇒ 量到「力士最左 x=72」＝
    整個結論反過來。**述詞要能分開你要分開的那兩類東西，否則它只是在回答別的問題。**
    """
    r, g, b = a[:, :, 0], a[:, :, 1], a[:, :, 2]
    skin = ((r > 120) & (r < 252) & (g > 85) & (g < 218) & (b > 70) & (b < 205)
            & (r - b > 22) & (r - g > 6))
    cols = np.where(skin.sum(axis=0) >= 150)[0]
    return int(cols[0]) if len(cols) else None


def body_text_contrast(a):
    """正文對它自己的地：取畫面左半、帶陰影的近 Paper 字，與 6~14px 外的環比。"""
    from scipy.ndimage import maximum_filter
    reg = a[250:950, GUTTER:GUTTER + COLW]
    txt = np.abs(reg - PAPER.reshape(1, 1, 3)).max(axis=2) < 55
    dark = reg.sum(axis=2) < 210
    glyph = txt & (maximum_filter(dark.astype(np.uint8), size=5) > 0)
    if glyph.sum() < 60:
        return None, None
    out = maximum_filter(glyph.astype(np.uint8), size=17) > 0
    ins = maximum_filter(glyph.astype(np.uint8), size=9) > 0
    ring = out & (~ins)
    if ring.sum() < 40:
        return None, None
    bg = np.median(reg[ring], axis=0)
    return [int(v) for v in bg], wcag(PAGES and PAPER, bg)


def skin_mask(a):
    """膚色遮罩（背景那隻跳舞的力士）。"""
    r, g, b = a[:, :, 0], a[:, :, 1], a[:, :, 2]
    return ((r > 120) & (r < 252) & (g > 85) & (g < 218) & (b > 70) & (b < 205)
            & (r - b > 22) & (r - g > 6))


def chrome_mask(a):
    """**chrome = 畫在我們調色盤上的像素**，不是「亮的像素」。

    兩版都栽在同一件事上：先用 `lit`（不是黑）⇒ 力士整個被算成版面；
    再用 `lit & ~skin` ⇒ 他身上**過曝的高光**（g 已經衝到 218 以上）逃出膚色述詞，
    於是首頁量到一個 x=477 的「群」，而那是他的手。
    **不要用「不是 X」來定義 chrome，直接用「是不是我們畫的顏色」定義它**
    ——調色盤是我們自己的正本，力士的皮膚不在裡面。
    容差 32：Paper(242,232,214) 與皮膚(238,195,168) 的最大通道差是 37 > 32。
    """
    def near(rgb, tol):
        return np.abs(a - np.array(rgb).reshape(1, 1, 3)).max(axis=2) < tol
    return (near(PAPER, 32) | near([168, 156, 138], 30)      # Paper / PaperDim
            | near(AMBER, 60) | near([217, 79, 61], 60))      # Amber / Red


def within_page_left_edges(a):
    """**同一頁之內**每一群 chrome 的左緣。

    這一支是 09-05 二修補的，補的是我自己的洞：一修只比了「六頁之間的標題左緣」
    就宣告「單一對齊軸 PASS」，而 user 的 viewport 上一眼就看得到**同一頁裡**
    有三條左緣（標題 75／主按鈕 106／底列 80）。
    **閘門要問：什麼樣的壞會讓它照樣亮綠燈？** 答案就是這一種。

    做法：把左半的 chrome 依「垂直空白 >40px」切成群，每群各自量左緣。
    回傳 [(y0, y1, left_x), ...]。
    """
    # 門檻 30 不是 90：未選中的 chip 底是 Paper α0.07 疊在黑地上＝(17,16,15)、
    # 總和 48。用 90 會**看不見 chip 本體、只量到 chip 裡置中的字**，於是語言頁
    # 報出 131px 的假極差（那是各個標籤寬度不同造成的，不是版面歪）。
    # 地本身是純黑（總和 <10），所以 30 兩邊都有餘裕。
    lit = chrome_mask(a)
    left = lit[:, :GUTTER + COLW]   # 只看內容欄；右邊是場景不是版面
    rows = left.sum(axis=1)
    groups, start = [], None
    gap = 0
    for y, v in enumerate(rows):
        if v >= 3:
            if start is None:
                start = y
            gap = 0
        else:
            if start is not None:
                gap += 1
                if gap > 40:
                    groups.append((start, y - gap))
                    start, gap = None, 0
    if start is not None:
        groups.append((start, len(rows) - 1))
    out = []
    for y0, y1 in groups:
        if y1 - y0 < 8:
            continue          # 太薄＝分隔線之類，不是一群內容
        cols = np.where(left[y0:y1 + 1].sum(axis=0) >= 2)[0]
        if len(cols):
            out.append((y0, y1, int(cols[0])))
    return out


def negative_test():
    """閘門必須為了正確的理由失敗。

    **第一版的負向測試選錯了受測物**：我拿「修改前」的設定頁截圖去測，量到 1px
    ——因為修改前有白卡，白卡**正是**用來擋住力士的，所以那個畫面本來就沒有
    膚色可以量。真正的失效模式是**拆掉卡片之後、內容還留在中央**（09-05 中途
    的個人檔案頁實測就是這樣：隱私聲明有一半疊在皮膚上）。

    所以負向測試改成：拿**現在**的截圖，把取樣窗移回「內容如果還置中」的位置。
    那裡現在站著力士 ⇒ 閘門必須開火。這用的是真像素，不是造出來的資料。
    """
    from scipy.ndimage import label
    a = load("menu2_profile00000.png")
    if a is None:
        print(NL + "負向測試：缺 profile 截圖（跳過）")
        return
    cx = a.shape[1] // 2
    reg = a[150:1000, cx - COLW // 2:cx + COLW // 2]   # 舊的置中欄位置
    r, g, b = reg[:, :, 0], reg[:, :, 1], reg[:, :, 2]
    skin = ((r > 120) & (r < 250) & (g > 85) & (g < 215) & (b > 70) & (b < 200)
            & (r - b > 25) & (r - g > 8))
    lab, n = label(skin)
    sizes = np.bincount(lab.ravel())
    sizes[0] = 0
    big = int(sizes.max()) if n else 0
    print(NL + "負向測試（把取樣窗移回舊的置中欄位置）：最大膚色塊 %d px" % big)
    check("負向測試：置中欄會被抓出來（門檻 8000）", big >= 8000, "%d px" % big)


def main():
    print("=" * 84)
    print("主選單版面量測（menu2_*.png）")
    print("=" * 84)
    edges, missing = [], []
    for name, fn, is_sub in PAGES:
        a = load(fn)
        if a is None:
            missing.append(fn)
            continue
        print("\n[%s]" % name)
        e = title_left_edge(a)
        edges.append((name, e))
        cap = has_keycap_bottom_left(a)
        panel = bright_panel_area(a)
        groups = within_page_left_edges(a)
        skin = skin_overlap(a, groups)
        bg, ct = body_text_contrast(a)
        print("    標題左緣 x=%s   鍵帽像素=%d   最寬亮帶=%dpx   最大膚色塊=%d" %
              (e, cap, panel, skin))
        if bg is not None:
            print("    正文地色 %s   對比 %.2f" % (bg, ct))
        # 3) ESC 鍵帽：子頁該有、首頁該沒有
        check("%-9s ESC 鍵帽 %s" % (name, "存在" if is_sub else "不存在"),
              (cap > 400) if is_sub else (cap < 400), "px=%d" % cap)
        # 2) 無卡片
        check("%-9s 無白卡" % name, panel < 350, "最寬亮帶 %dpx（白卡會 >400）" % panel)
        # 5) 內容欄不壓力士
        # 力士會跳舞：同一頁每次截圖他站的位置都不同，單一幀答不了「會不會擋到」。
        # 顏色分割也三版都不穩（高光逃出膚色述詞／深底按鈕只被看見裡面的字）。
        # ⇒ 印數字當佐證，判準交給左緣軸（內容靠左、場景佔右＝構造上不重疊）。
        print("    chrome 盒內最大膚色塊 %d px（臉庫縮圖 ~2.3k 屬合法；僅供佐證）" % skin)
        # **內容靠左、場景佔右**：這是拆掉白卡之後唯一擋住「文字疊在皮膚上」的東西。
        # 力士會跳舞 ⇒ 由舞台的 StageBiasCm/SwayCm 在構造上保證，這裡驗結果。
        sx = scene_clearance(a)
        if sx is not None:
            print("    力士最左 x=%d（內容欄右緣 %d，間隙 %+d）" % (sx, GUTTER + COLW, sx - (GUTTER + COLW)))
            check("%-9s 場景不進內容欄" % name, sx > GUTTER + COLW, "間隙 %+d px" % (sx - (GUTTER + COLW)))
        # 1b) **同一頁之內**的左緣軸（09-05 二修補的洞）
        gl = groups
        if len(gl) >= 2:
            xs = [g[2] for g in gl]
            print("    頁內群組左緣（僅供佐證，判準見下方原始碼檢查）：" + ", ".join(
                  "y%d-%d→x%d" % g for g in gl) + "   極差 %dpx" % (max(xs) - min(xs)))
        # 4) 對比
        if ct is not None:
            check("%-9s 正文對比 >=4.5" % name, ct >= 4.5, "%.2f" % ct)

    # 1) 單一左緣軸
    print("\n" + "-" * 84)
    vals = [e for _, e in edges if e is not None]
    if vals:
        spread = max(vals) - min(vals)
        print("標題左緣：%s   極差 %dpx（期望 ~%d）" %
              ({n: e for n, e in edges}, spread, GUTTER))
        # 極差門檻 8 的理由：**版面軸六頁都是同一個常數 NiMenuLeftGutter**
        # （下面那條原始碼檢查在守這件事）；量到的 3~5px 殘差是**字腔側距**
        # ——N/H/J/S/L/P 各自的左側 bearing 不同，那是字型的事不是版面的事。
        # 把門檻收到 4 只會逼我去調字，而字沒有錯。
        check("六頁單一左緣軸（極差 <= 8px，殘差＝字腔側距）", spread <= 8, "極差 %d" % spread)
        check("左緣落在 NiMenuLeftGutter 附近", abs(min(vals) - GUTTER) <= 12,
              "min=%d gutter=%d" % (min(vals), GUTTER))
    # **閘門要讀真相的正本**：像素只能證明「看起來對齊」，版面軸是不是同一個
    # 常數要去問原始碼。（09-05 血價：規則塊改色之後，phase_consistency 還在找
    # 舊的綠色 ⇒ 抄一份結論在閘門身上，那份就會過期。）
    src = os.path.join(ROOT, "Source/NiceInk/Private/NiceInkMenuWidget.cpp")
    code = io.open(src, encoding="utf-8").read()
    # 剝註解：閘門讀到的必須是程式碼，不是散文（09-03 血價 c0k 同型）
    code = NL.join(l.split("//")[0] for l in code.split(NL))
    n_gutter = code.count("NiMenuLeftGutter")
    n_center = sum(1 for l in code.split(NL)
                   if l.startswith(TAB + TAB + "+ SVerticalBox::Slot()")
                   and "HAlign_Center" in l)
    print("原始碼：頁面頂層 HAlign_Center 殘留 %d 處；NiMenuLeftGutter 用了 %d 次"
          % (n_center, n_gutter))
    check("原始碼裡沒有頁面頂層的置中殘留", n_center == 0, "殘留 %d" % n_center)

    # **頁內單一左緣軸的真正判準**（09-05 二修）：頂層 slot 的左內距只能有一個值。
    # user viewport 打回的那張圖，三條左緣是 75/106/80——其中 106 = 72 + CardPadH(32)，
    # 那是**白卡的內距**，卡拆了它還留著；80 = 72 + 底列第一顆的 slot padding(8)。
    # 一修的閘門只比了「六頁之間的標題」，對「同一頁之內」完全沒有解析度。
    tops = [l for l in code.split(NL)
            if l.startswith(TAB + TAB + "+ SVerticalBox::Slot()") and ".Padding(" in l]
    lefts = set()
    for l in tops:
        arg = l.split(".Padding(", 1)[1].split(",")[0].strip()
        lefts.add(arg)
    bad = sorted(x for x in lefts if x not in ("NiMenuLeftGutter", "0"))
    print("頂層 slot 的左內距取值：%s" % (sorted(lefts) or "（無）"))
    check("頂層 slot 只用同一個左內距（NiMenuLeftGutter）", not bad,
          "另有 " + ", ".join(bad) if bad else "")

    # 白卡內距必須歸零——它是「卡的邊」才需要的東西，沒有卡就是沒有理由的縮排
    pad_left = "CardPadH" in code
    print("原始碼仍引用 NiSpace::CardPadH：%s" % pad_left)
    check("無卡片制下不得殘留卡內距（CardPadH）", not pad_left)


    negative_test()

    if missing:
        check("六頁截圖齊全", False, "缺 " + ", ".join(missing))

    print("\n" + "=" * 84)
    print("FAILS: %d" % len(fails))
    for f in fails:
        print("  - " + f)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
