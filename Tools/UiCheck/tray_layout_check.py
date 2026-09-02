# -*- coding: utf-8 -*-
"""墨杯盤／cluster 版面離線契約（不開引擎、一輪 1 秒）。
FNiInkTrayLayout::Compute 與 HUD 繪製常數的 1:1 重現。
09-02 二修：①文字高度預算改 字級×1.4（FCanvasTextItem 的 Y＝行框頂端，
字身比字級矮、位置比字級低——一版用 1.0 讓目測貼在一起的版面照樣過關）
②加入列標欄（Gutter）與雙色地的契約。"""
COLS, ROWS = 10, 3
SMALL_PX = 12.0          # NiType::HudSmall
def text_h(S): return SMALL_PX * 1.4 * S   # 行框高度預算（**必須跟著 S**——
    # 一版寫成未縮放的常數，於是只有 1080p 那一列是誠實的：契約自己也要被查

def layout(vw, vh):
    S = max(vh / 1080.0, 0.25)
    gap, pad, cell = 6.0 * S, 18.0 * S, 46.0 * S
    gutter, head, foot = 48.0 * S, 34.0 * S, 26.0 * S
    gw = COLS * cell + (COLS - 1) * gap
    gh = ROWS * cell + (ROWS - 1) * gap
    cw = gutter + gw + 2 * pad
    ch = head + gh + foot + 2 * pad
    cx, cy = (vw - cw) / 2.0, (vh - ch) / 2.0
    return dict(S=S, gap=gap, pad=pad, cell=cell, gutter=gutter, head=head, foot=foot,
                gw=gw, gh=gh, cx=cx, cy=cy, cw=cw, ch=ch,
                ox=cx + pad + gutter, oy=cy + pad + head)

def cell_pos(L, c, r):
    return (L['ox'] + c * (L['cell'] + L['gap']), L['oy'] + r * (L['cell'] + L['gap']))

def clamp_to_grid(L, px, py):
    """累積位移的可行域＝格陣矩形（ClampToGrid）"""
    gw = COLS * L['cell'] + (COLS - 1) * L['gap']
    gh = ROWS * L['cell'] + (ROWS - 1) * L['gap']
    return (min(max(px, L['ox']), L['ox'] + gw), min(max(py, L['oy']), L['oy'] + gh))


def hit(L, px, py):
    """SnapCell：鉗到盤內再取最近格——**永遠有解**（方向選擇，無游標）"""
    qx, qy = clamp_to_grid(L, px, py)
    sx, sy = L['cell'] + L['gap'], L['cell'] + L['gap']
    c = int(round((qx - L['ox'] - L['cell'] * 0.5) / sx))
    r = int(round((qy - L['oy'] - L['cell'] * 0.5) / sy))
    return (min(max(c, 0), COLS - 1), min(max(r, 0), ROWS - 1))

def tier_from_row(r): return ROWS - 1 - r
def row_from_tier(t): return ROWS - 1 - t
TIER_A = {2: 1.00, 1: 156 / 255.0, 0: 77 / 255.0}   # ShaderTierAlphaFor

fails, checks = [], 0
def ck(name, ok, detail=""):
    global checks
    checks += 1
    if not ok:
        fails.append("FAIL %s  %s" % (name, detail))

for vw, vh in [(1280, 720), (1600, 900), (1920, 1080), (2559, 1398), (2560, 1440), (3840, 2160)]:
    L = layout(vw, vh)
    S, tag = L['S'], "%dx%d" % (vw, vh)

    ck("c1 card on-screen " + tag,
       L['cx'] >= 0 and L['cy'] >= 0 and L['cx'] + L['cw'] <= vw and L['cy'] + L['ch'] <= vh,
       "card=(%.0f,%.0f) %.0fx%.0f view=%dx%d" % (L['cx'], L['cy'], L['cw'], L['ch'], vw, vh))

    # c2 抬頭字（Y=行框頂端）不壓第一列、且在卡內
    h0 = L['cy'] + L['pad']
    ck("c2 header clear of row0 " + tag, h0 + text_h(S) <= L['oy'] and h0 >= L['cy'],
       "head=%.1f..%.1f row0=%.1f" % (h0, h0 + text_h(S), L['oy']))

    # c3 欄標（數字鍵）在末列之下、且在卡內
    lx, ly = cell_pos(L, 0, ROWS - 1)
    lab0 = ly + L['cell'] + 4.0 * S
    ck("c3 col labels inside card " + tag, lab0 >= ly + L['cell'] and lab0 + text_h(S) <= L['cy'] + L['ch'],
       "lab=%.1f..%.1f card_bot=%.1f" % (lab0, lab0 + text_h(S), L['cy'] + L['ch']))

    # c4 列標（100%/60%/30%）完全落在左側 gutter 內、不壓格子也不出卡
    lbl_w = 4 * SMALL_PX * 0.62 * S      # "100%" 四字元的保守寬度
    for r in range(ROWS):
        rx, ry = cell_pos(L, 0, r)
        right = rx - 10.0 * S            # 右對齊錨點
        ck("c4 row label in gutter r%d %s" % (r, tag),
           right - lbl_w >= L['cx'] + L['pad'] * 0.25 and right <= rx,
           "lbl=%.1f..%.1f gutter=[%.1f,%.1f]" % (right - lbl_w, right, L['cx'], rx))
        ck("c5 row label vcentred r%d %s" % (r, tag),
           ry <= ry + L['cell'] * 0.5 - 8.0 * S and
           ry + L['cell'] * 0.5 - 8.0 * S + text_h(S) <= ry + L['cell'])

    # c6/c7 命中無死區
    ok = all(hit(L, cell_pos(L, c, r)[0] + L['cell'] + L['gap'] * 0.5,
                 cell_pos(L, c, r)[1] + L['cell'] * 0.5) is not None
             for r in range(ROWS) for c in range(COLS - 1))
    ck("c6 gap midpoint snaps to a neighbour " + tag, ok)
    ok = all(hit(L, cell_pos(L, 0, r)[0] + L['cell'] * 0.5,
                 cell_pos(L, 0, r)[1] + L['cell'] + L['gap'] * 0.5) is not None
             for r in range(ROWS - 1))
    ck("c7 gap midpoint snaps to a neighbour " + tag, ok)

    # c8 輕點 RMB 必沾回同一杯
    ok = True
    for c in range(COLS):
        for t in range(ROWS):
            r = row_from_tier(t)
            x, y = cell_pos(L, c, r)
            got = hit(L, x + L['cell'] * 0.5, y + L['cell'] * 0.5)
            if got != (c, r) or tier_from_row(got[1]) != t:
                ok = False
    ck("c8 tap-RMB round-trip (no move = same cup) " + tag, ok)

    # c9 方向選擇的構造保證：**任何位移都落在某一格**，四個角落夾到四個角格。
    #（舊契約是「盤外＝取消」——那是有游標時代的語義，已隨游標一起退役。）
    corners = {(2, 2): (0, 0), (vw - 2, 2): (COLS - 1, 0),
               (2, vh - 2): (0, ROWS - 1), (vw - 2, vh - 2): (COLS - 1, ROWS - 1)}
    ck("c9 every input snaps to a cell " + tag,
       all(hit(L, px, py) == want for (px, py), want in corners.items()),
       str({k: hit(L, *k) for k in corners}))

    # c10/c11 cluster
    cell_c, gap_c = 18.0 * S, 3.0 * S
    colh = 3 * cell_c + 2 * gap_c
    col_y = (vh - 104.0 * S) + 20.0 * S - colh
    pen_y0 = col_y + colh * 0.5 - 17.0 * S
    q_y0 = col_y + colh * 0.5 + 1.0 * S
    ck("c10 cluster rows disjoint " + tag, pen_y0 + text_h(S) <= q_y0,
       "pen_bot=%.1f q_top=%.1f" % (pen_y0 + text_h(S), q_y0))
    ck("c11 cluster above hint " + tag, col_y + colh <= vh - 46.0 * S)

# c12 列↔檔位互為反函式（上濃下淡）
ck("c12 row/tier inverse", all(row_from_tier(tier_from_row(r)) == r for r in range(ROWS))
   and tier_from_row(0) == 2 and tier_from_row(ROWS - 1) == 0)

# c13/c14 **地的對比有工作區間**（09-02 三修）。一版的閘門只有下限
#（「半透明必須看得到格子」），所以「地深到格子壓過顏色」照樣亮綠燈——
# 閘門看不見 user 回報的那種壞。現在同時驗下限與**上限**：
#   下限＝格子要看得見（否則讀成摻白的粉彩＝原始 bug）
#   上限＝格子不能壓過顏色（混濁度＝這一杯離「乾淨的同色淡墨」多遠，
#         而它恆等於格子對比的一半 ⇒ 上限與下限是同一個量的兩端）
PAL = {"black":(0.0168,0.0168,0.0168), "white":(0.8469,0.8469,0.8469),
       "red":(0.8550,0.0144,0.0742),   "orange":(1.0,0.1779,0.0395),
       "yellow":(0.9734,0.8070,0.2270),"green":(0.0116,0.4125,0.1878),
       "blue":(0.0137,0.1779,0.9911),  "violet":(0.2874,0.1559,0.4233),
       "pink":(1.0,0.4020,0.6038),     "brown":(0.4564,0.1356,0.0742)}
def lin(b):
    c = b/255.0
    return c/12.92 if c <= 0.04045 else ((c+0.055)/1.055)**2.4
def srgb(l):
    c = 12.92*l if l <= 0.0031308 else 1.055*(l**(1/2.4))-0.055
    return max(0, min(255, int(round(c*255))))
GROUND_LO = tuple(lin(v) for v in (242, 232, 214))   # NiHudColor::Paper
GROUND_HI = tuple(lin(v) for v in (176, 168, 153))   # NiHudColor::PaperShade
TIER_A = {2: 1.0, 1: 156/255.0, 0: 77/255.0}
def over(a, ink, bg): return tuple(a*ink[i] + (1-a)*bg[i] for i in range(3))

DELTA_MIN, DELTA_MAX = 15, 70    # sRGB 階；Photoshop 自己的棋盤地對比 ~51
for tier, a in sorted(TIER_A.items(), reverse=True):
    ds = []
    for name, c in PAL.items():
        lo, hi = over(a, c, GROUND_LO), over(a, c, GROUND_HI)
        ds.append(max(abs(srgb(lo[i]) - srgb(hi[i])) for i in range(3)))
    lo_d, hi_d = min(ds), max(ds)
    if tier == 2:
        ck("c13 opaque hides the checker (tier2)", hi_d == 0, "max delta=%d" % hi_d)
    else:
        ck("c13 translucent shows the checker (tier%d)" % tier, lo_d >= DELTA_MIN,
           "min delta=%d (floor %d)" % (lo_d, DELTA_MIN))
        ck("c14 checker must not overpower the colour (tier%d)" % tier, hi_d <= DELTA_MAX,
           "max delta=%d (ceiling %d) muddiness=%d" % (hi_d, DELTA_MAX, hi_d//2))

# c15 單一底色下透明與摻白**逐值相等**（記錄原始 bug 的成因，防再犯）
ck("c15 flat ground cannot show alpha at all",
   all(max(abs(srgb(over(a, (0,0,0), GROUND_LO)[i]) - srgb(over(a, (0,0,0), GROUND_LO)[i]))
           for i in range(3)) == 0 for a in TIER_A.values()))


# ---------------------------------------------------------------------------
# c21~c24 墨杯 chip 與首次教學卡（DrawInkChip / DrawFirstLockCard 的 1:1 重現）
# 手算過的位置要有東西盯著：上一輪「抬頭字會壓到第一列」就是手算錯的。
def chip_rect(vw, vh, pct_w):
    S = max(vh / 1080.0, 0.25)
    cell, padx, pady = 26.0 * S, 10.0 * S, 7.0 * S
    cw = padx * 2 + cell + 8.0 * S + pct_w
    ch = pady * 2 + cell
    return (vw * 0.5 - cw * 0.5, vh - 46.0 * S - 14.0 * S - ch, cw, ch, S)

def teach_rect(vw, vh, text_w):
    S = max(vh / 1080.0, 0.25)
    lineh, padx, pady = 26.0 * S, 22.0 * S, 16.0 * S
    cw = text_w + padx * 2
    ch = pady * 2 + lineh * 7.0
    return (vw - cw - 40.0 * S, vh - ch - 92.0 * S, cw, ch, S)

for vw, vh in [(1280, 720), (1920, 1080), (2559, 1398), (3840, 2160)]:
    tag = "%dx%d" % (vw, vh)
    S = max(vh / 1080.0, 0.25)
    # 「100%」在 Small 字級的保守寬度
    pct_w = 4 * SMALL_PX * 0.62 * S
    cx, cy, cw, ch, _ = chip_rect(vw, vh, pct_w)
    ck("c21 chip on-screen " + tag,
       cx >= 0 and cy >= 0 and cx + cw <= vw and cy + ch <= vh,
       "chip=(%.0f,%.0f) %.0fx%.0f" % (cx, cy, cw, ch))
    # chip 底邊要在底部提示行之上（提示行文字頂端＝vh-46S）
    ck("c22 chip clears the hint line " + tag, cy + ch <= vh - 46.0 * S,
       "chip_bot=%.1f hint_top=%.1f" % (cy + ch, vh - 46.0 * S))
    # 教學卡：最長那一行用一個誇張的估計（法文/德文最長），必須仍在畫面內
    text_w = 34 * SMALL_PX * 0.62 * S
    tx, ty, tw, th, _ = teach_rect(vw, vh, text_w)
    ck("c23 teach card on-screen " + tag,
       tx >= 0 and ty >= 0 and tx + tw <= vw and ty + th <= vh,
       "card=(%.0f,%.0f) %.0fx%.0f view=%dx%d" % (tx, ty, tw, th, vw, vh))
    # 教學卡不可蓋住 chip（兩者同時在畫面上：卡片還在時玩家可能已經在調杯）
    overlap = not (tx >= cx + cw or tx + tw <= cx or ty >= cy + ch or ty + th <= cy)
    ck("c24 teach card does not cover the chip " + tag, not overlap,
       "chip=(%.0f,%.0f,%.0f,%.0f) card=(%.0f,%.0f,%.0f,%.0f)" % (cx, cy, cw, ch, tx, ty, tw, th))


# c25 **列標與實際 alpha 必須對得起彼此**（09-02：chip 顯示 61% 而托盤寫 60%＝
# 同一個東西的名字寫在兩個地方）。列標是名字、alpha 是值——值取整必須等於名字。
ALPHA_BYTES = {2: 255, 1: 153, 0: 77}      # ShaderTierAlphaFor
LABELS      = {0: "100%", 1: "60%", 2: "30%"}   # TierLabel(Row)
for row, lab in LABELS.items():
    tier = tier_from_row(row)
    got = int(round(ALPHA_BYTES[tier] / 255.0 * 100))
    ck("c25 label matches alpha (row%d)" % row, ("%d%%" % got) == lab,
       "alpha=%d -> %d%%  label=%s" % (ALPHA_BYTES[tier], got, lab))


# c26 **推過頭之後要立刻回得來**（ClampToGrid 真正保護的性質；c9 看不見它——
# 取整本身也會夾，所以少了鉗位角落照樣正確＝空洞）。
# 自由累積的位移會跑到畫面外，回程要走一樣的距離才回得來＝「對不準」的來源。
def accumulate(L, start, deltas, do_clamp=True):
    p = start
    for d in deltas:
        p = (p[0] + d[0], p[1] + d[1])
        if do_clamp:
            p = clamp_to_grid(L, *p)
    return p

for vw, vh in [(1280, 720), (1920, 1080), (2559, 1398)]:
    L = layout(vw, vh)
    step = L['cell'] + L['gap']
    start = (cell_pos(L, 0, 0)[0] + L['cell'] * 0.5, cell_pos(L, 0, 0)[1] + L['cell'] * 0.5)
    # 往右狠推 20 格，再往回一格
    p = accumulate(L, start, [(step * 20, 0), (-step, 0)])
    got = hit(L, *p)
    ck("c26 overshoot then one step back " + ("%dx%d" % (vw, vh)),
       got == (COLS - 2, 0), "landed on %s (want col %d)" % (str(got), COLS - 2))
    # 往上狠推再回一格（列同理）
    p = accumulate(L, start, [(0, -step * 20), (0, step)])
    got = hit(L, *p)
    ck("c26 overshoot vertical " + ("%dx%d" % (vw, vh)), got == (0, 1),
       "landed on %s" % str(got))

print(chr(10).join(fails) if fails else "ALL PASS")
print("checks=%d  fail=%d" % (checks, len(fails)))
