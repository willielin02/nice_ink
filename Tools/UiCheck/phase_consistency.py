# -*- coding: utf-8 -*-
"""跨相位的版面一致性（2026-09-05）。

既有 shot_measure.py 量的是**一張圖裡面**的版面不變量（單一右緣／列距／等高）。
它答不出 user 的題目——「整個遊戲的 UI 統一到什麼程度」是**跨相位**的量：
同一個區塊在九個相位裡有沒有站在同一個位置、用同一種語言。

受測物＝Tools/RoboTest/robo_ui_shots_all.py 產出的 uiall_{art,vic}_*.png。
量四件事：
  1. 上緣中央那一群（祈使句／倒數／臉 chip）的**垂直起點**
  2. 右緣操作列的**右緣座標**與**垂直起點**
  3. 右下規則塊的**存在與否**（標題＝酒金明朝體）
  4. **螢幕中心有沒有色票方塊**（規範 §3：0° 區只准放「墨會落在哪」——
     細十字是視線錨點，留著；09-05 拆掉的是那枚 24px 的「目前選色」方塊）

**閘門本身被改過一次**（2026-09-05）：規則塊的標題色從 Green 改成 Amber 之後，
這支腳本照樣去找綠色 ⇒ 回報「規則塊覆蓋 0/10」而畫面上明明有。
**閘門必須去讀真相的正本**——它抄了一份結論在自己身上，那份就會過期。

輸出是一張表，不是 PASS/FAIL——這一輪要回答的是「現況長怎樣」。
"""
import os
import sys

import numpy as np
from PIL import Image

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
SHOTDIR = os.path.join(ROOT, "Saved/Screenshots/WindowsEditor")

# NiHudColor（sRGB byte）
PAPER = np.array([242, 232, 214])
GREEN = np.array([134, 176, 108])
AMBER = np.array([232, 163, 61])
# 規則塊標題色＝酒金（2026-09-05 由 Green 改；值取自 NiceInkUiTokens.h）
RULE_TITLE = AMBER

SHOTS = [
    ("Lobby",            "uiall_art_01_lobby.png"),
    ("Seating",          "uiall_art_03_seating.png"),
    ("Drawing 站著",     "uiall_art_04_draw_standing.png"),
    ("Drawing 鎖定",     "uiall_art_05_draw_locked.png"),
    ("Tour",             "uiall_art_07_tour.png"),
    ("Accusation 旁觀",  "uiall_art_08_accusation.png"),
    ("Resolution",       "uiall_art_09_resolution.png"),
    # 醉夢＝整片黑底、**設計上沒有上緣帶**（沉睡端走 DrawVictimSleepUI 早退）
    # ⇒ 上緣那一欄對它不適用，統計時排除，不要讓它污染「上緣起點極差」。
    ("醉夢描圖",         "uiall_vic_04_dream_trace.png"),
    ("Tour（受害者）",   "uiall_vic_07_tour.png"),
    ("Accusation 本人",  "uiall_vic_08_accusation.png"),
]


def load(name):
    p = os.path.join(SHOTDIR, name)
    if not os.path.exists(p):
        return None
    return np.asarray(Image.open(p).convert("RGB")).astype(np.int16)


# 容差 14 不是隨手挑的：**負向測試校準出來的**。tol=26 會把道場的榻榻米收成
# 「綠色」⇒ Seating／Resolution（原始碼明文沒有規則塊）照樣報「有」＝假通過。
# tol=14 之下兩者實測 0 px、Drawing 1218 / Tour 1461 ⇒ 與原始碼逐項相符。
def near(a, rgb, tol=14):
    """像素接近某個 token 色（逐通道，chrome 有 1px 黑陰影）。"""
    return np.abs(a - rgb.reshape(1, 1, 3)).max(axis=2) < tol


def first_row(mask, y0, y1, x0, x1, minpix=3):
    sub = mask[y0:y1, x0:x1]
    rows = np.where(sub.sum(axis=1) >= minpix)[0]
    return (y0 + int(rows[0])) if len(rows) else None


def last_col(mask, y0, y1, x0, x1, minpix=2):
    sub = mask[y0:y1, x0:x1]
    cols = np.where(sub.sum(axis=0) >= minpix)[0]
    return (x0 + int(cols[-1])) if len(cols) else None


def main():
    print("=" * 104)
    print("%-18s %10s %10s %10s %10s %10s" % (
        "相位", "上緣起點y", "右列右緣x", "右列起點y", "規則塊", "中心色票"))
    print("-" * 104)
    rows = []
    for label, fn in SHOTS:
        a = load(fn)
        if a is None:
            print("%-18s  (缺圖 %s)" % (label, fn))
            continue
        H, W = a.shape[:2]
        paper = near(a, PAPER, 30)
        green = near(a, GREEN)
        amber = near(a, AMBER)

        # 1) 上緣中央群：中央 40% 寬、上緣 20% 高
        topy = first_row(paper | amber, 0, int(H * 0.20), int(W * 0.30), int(W * 0.70))

        # 2) 右緣操作列：右側 18% 寬、中段 60% 高（避開右上現金與右下規則塊）
        rx = last_col(paper, int(H * 0.20), int(H * 0.80), int(W * 0.90), W, minpix=4)
        ry = first_row(paper, int(H * 0.20), int(H * 0.80), int(W * 0.90), W, minpix=10)

        # 3) 規則塊：右下角 30%×22% 內有沒有 Green 標題
        rb = near(a, RULE_TITLE, 22)[int(H * 0.86):int(H * 0.95), int(W * 0.70):].sum()

        # 4) 中心色票方塊（09-05 已拆除）：它畫在準星**右下** +14..+38px、
        #    24px 見方、底是 alpha 0.6 的純黑 ⇒ 量那個小窗裡的近黑像素。
        #    細十字本身在中心 ±9px，不落在這個窗裡 ⇒ 這支量的是方塊不是十字。
        #    **要求「暗方塊落在較亮的地上」**：醉夢是整片黑底，只量「有沒有暗像素」
        #    會在那張圖上恆真（第一版就是這樣假報「殘留」的）。加上與周圍的比較，
        #    黑底畫面自然不會命中——閘門要問「什麼樣的壞會讓它照樣亮綠燈」，
        #    這裡的反面是「什麼樣的好會讓它照樣報紅燈」。
        cy, cx = H // 2, W // 2
        win = a[cy + 14:cy + 38, cx + 14:cx + 38]
        ring = a[cy - 60:cy + 90, cx - 60:cx + 90]
        dark = (win.sum(axis=2) < 190).sum()
        ground_lit = float(np.median(ring.sum(axis=2))) > 260   # 地本身夠亮才談得上「暗方塊」
        cross = ground_lit and dark >= 120

        rows.append((label, topy, rx, ry, rb > 200, cross))
        print("%-18s %10s %10s %10s %10s %10s" % (
            label,
            topy if topy is not None else "—",
            rx if rx is not None else "—",
            ry if ry is not None else "—",
            "有" if rb > 200 else "—",
            "有" if cross else "—"))

    print("-" * 104)
    NO_TOPBAR = {"醉夢描圖"}   # 設計上沒有上緣帶的畫面
    tops = [r[1] for r in rows if r[1] is not None and r[0] not in NO_TOPBAR]
    rxs = [r[2] for r in rows if r[2] is not None]
    rys = [r[3] for r in rows if r[3] is not None]
    if tops:
        print("上緣起點 y  極差 = %d px   (值：%s)" % (max(tops) - min(tops), sorted(set(tops))))
    if rxs:
        print("右列右緣 x  極差 = %d px   (值：%s)" % (max(rxs) - min(rxs), sorted(set(rxs))))
    if rys:
        print("右列起點 y  極差 = %d px   (值：%s)" % (max(rys) - min(rys), sorted(set(rys))))
    print("規則塊覆蓋 %d/%d   中心色票方塊殘留 %d/%d 個相位"
          % (sum(1 for r in rows if r[4]), len(rows),
             sum(1 for r in rows if r[5]), len(rows)))


# --- 第二段：上緣祈使句的對比（2026-09-05 加）---------------------------------
# 09-04 拆面板之後，上緣那一句話的可讀性由「面板」改成由「陰影」保證。而當時的
# 驗收截圖背景全是**皮膚**（作畫相位）——大廳／入座／巡禮站的是**白色障子牆**。
# 閘門必須能看見它要擋的那種失敗：這一段就是把背景換成牆再量一次。
def text_contrast(a, y0, y1, x0=600, x1=1320):
    """回傳 (字像素數, 背景中位色, WCAG 對比)。

    文字＝近 Paper **且 2px 內有陰影**（bTokShadows 黑 0.6）。少了「有陰影」這一條，
    白障子牆本身就落在 Paper 的容差內 ⇒ 量到的是牆不是字（本函式第一版的錯）。
    """
    from scipy.ndimage import maximum_filter
    band = a[y0:y1, x0:x1]
    txt = np.abs(band - PAPER.reshape(1, 1, 3)).max(axis=2) < 45
    dark = band.sum(axis=2) < 210
    glyph = txt & (maximum_filter(dark.astype(np.uint8), size=5) > 0)
    if glyph.sum() < 20:
        return 0, None, None
    g_out = maximum_filter(glyph.astype(np.uint8), size=17) > 0
    g_in = maximum_filter(glyph.astype(np.uint8), size=9) > 0
    bg = np.median(band[g_out & (~g_in)], axis=0)
    return int(glyph.sum()), [int(v) for v in bg], wcag(PAPER, bg)


def _lin(c):
    c = np.array(c, dtype=float) / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def wcag(fg, bg):
    def lum(rgb):
        r, g, b = _lin(rgb)
        return 0.2126 * r + 0.7152 * g + 0.0722 * b
    a, b = lum(fg), lum(bg)
    hi, lo = max(a, b), min(a, b)
    return (hi + 0.05) / (lo + 0.05)


BANNER = [
    ("Lobby",           "uiall_art_01_lobby.png",        30, 60),
    ("Seating",         "uiall_art_03_seating.png",     100, 125),
    ("Drawing 鎖定",   "uiall_art_05_draw_locked.png",  30, 60),
    ("Tour",            "uiall_art_07_tour.png",        100, 128),
    ("Accusation",      "uiall_art_08_accusation.png",   30, 60),
]


def banner_report():
    print()
    print("=" * 104)
    print("上緣祈使句的對比（拆面板後只剩陰影在擐）")
    print("-" * 104)
    print("%-18s %8s %22s %8s" % ("相位", "字px", "背景", "對比"))
    for label, fn, y0, y1 in BANNER:
        a = load(fn)
        if a is None:
            continue
        n, bg, r = text_contrast(a, y0, y1)
        if not n:
            print("%-18s  (找不到帶陰影的字)" % label)
            continue
        print("%-18s %8d %22s %8.2f" % (label, n, str(bg), r))
    print("WCAG: 4.5 = 小字合格，3.0 = 大字合格")


if __name__ == "__main__":
    main()
    banner_report()
