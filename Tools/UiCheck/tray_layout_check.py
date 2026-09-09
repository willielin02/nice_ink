# -*- coding: utf-8 -*-
"""墨杯盤／cluster 版面離線契約（不開引擎、一輪 1 秒）。
FNiInkTrayLayout::Compute 與 HUD 繪製常數的 1:1 重現。
09-02 二修：①文字高度預算改 字級×1.4（FCanvasTextItem 的 Y＝行框頂端，
字身比字級矮、位置比字級低——一版用 1.0 讓目測貼在一起的版面照樣過關）
②加入列標欄（Gutter）與雙色地的契約。"""
import io as _io, os as _os, re as _re

# 專案根（允許 NI_SRC_ROOT 覆蓋＝負向測試可以指著一份被弄壞的複本跑）
ROOT = _os.environ.get("NI_SRC_ROOT",
                       _os.path.abspath(_os.path.join(_os.path.dirname(__file__), "..", "..")))
def _src(rel):
    return _io.open(_os.path.join(ROOT, rel), encoding="utf-8").read()

MAX_COLS, MAX_ROWS = 10, 3
# **盤的形狀＝這支筆真正擁有的維度**（2026-09-03）。真相在 BeginStroke：稿筆恆
# 結晶紫、濃度只有打霧筆吃得到（`(Needle==Shader) ? ShaderTierAlphaFor(...) : 255`）
# ⇒ 盤只畫這支筆真的有的軸。**閘門必須跟著改**：不改的話它會繼續對一個已經不
# 存在的 10×3 版面亮綠燈——「閘門必須能看見它要擋的那種失敗」（專案鐵則）。
# (欄數, 有沒有濃度軸)。**列數恆 1**——09-03 二修：打霧筆此前是 10×3，改成一排。
# 理由＝①二維方向選擇會讓「往右換個顏色」順手改掉濃度，而濃度誤用單向不可逆
# ②盤有三種形狀＝三套肌肉記憶 ③濃度已經有滾輪（自然映射），盤再做一次＝兩條
# 通道做同一件事（與同日砍掉數字鍵快捷是同一個理由）。
NEEDLES = {"stencil": (1, False), "liner": (MAX_COLS, False), "shader": (MAX_COLS, True)}
SMALL_PX = 12.0          # NiType::HudSmall
def text_h(S): return SMALL_PX * 1.4 * S   # 行框高度預算（**必須跟著 S**——
    # 一版寫成未縮放的常數，於是只有 1080p 那一列是誠實的：契約自己也要被查

HEAD_MIN_W = 240.0    # FNiInkTrayLayout::HeadMinWidth()（未乘縮放）

def layout(vw, vh, needle="shader"):
    cols, tier_axis = NEEDLES[needle]
    rows = 1                     # **盤永遠只有一排**
    S = max(vh / 1080.0, 0.25)
    gap, pad, cell = 6.0 * S, 18.0 * S, 46.0 * S
    # 左側列標欄隨「三列」一起退役（沒有列要標，當前濃度移到抬頭行）；
    # foot（欄標＝數字鍵帽）已隨數字鍵快捷退役
    gutter, head, foot = 0.0, 34.0 * S, 0.0
    gw = cols * cell + (cols - 1) * gap
    gh = rows * cell + (rows - 1) * gap
    # 卡片不得比抬頭行窄——稿筆的格陣只有一格（46px），沒有下限抬頭字會戳出卡片
    content_w = max(gutter + gw, HEAD_MIN_W * S)
    cw = content_w + 2 * pad
    ch = head + gh + foot + 2 * pad
    cx, cy = (vw - cw) / 2.0, (vh - ch) / 2.0
    return dict(S=S, gap=gap, pad=pad, cell=cell, gutter=gutter, head=head, foot=foot,
                gw=gw, gh=gh, cx=cx, cy=cy, cw=cw, ch=ch,
                cols=cols, rows=rows, tier_axis=tier_axis, color_axis=(cols > 1),
                ox=cx + (cw - gw) / 2.0, oy=cy + pad + head)   # 格陣在卡片內置中

def cell_pos(L, c, r):
    return (L['ox'] + c * (L['cell'] + L['gap']), L['oy'] + r * (L['cell'] + L['gap']))

def clamp_to_grid(L, px, py):
    """累積位移的可行域＝格陣矩形（ClampToGrid）"""
    gw = L['cols'] * L['cell'] + (L['cols'] - 1) * L['gap']
    gh = L['rows'] * L['cell'] + (L['rows'] - 1) * L['gap']
    return (min(max(px, L['ox']), L['ox'] + gw), min(max(py, L['oy']), L['oy'] + gh))


def hit(L, px, py):
    """SnapCell：鉗到盤內再取最近格——**永遠有解**（方向選擇，無游標）"""
    qx, qy = clamp_to_grid(L, px, py)
    sx, sy = L['cell'] + L['gap'], L['cell'] + L['gap']
    c = int(round((qx - L['ox'] - L['cell'] * 0.5) / sx))
    r = int(round((qy - L['oy'] - L['cell'] * 0.5) / sy))
    return (min(max(c, 0), L['cols'] - 1), min(max(r, 0), L['rows'] - 1))

# 檔位對應是**濃度軸本身**的定義（永遠 3 檔），與盤畫幾列無關——割線筆的盤只有
# 一列，但它的 ShaderTierIdx 仍是 3 檔裡的某一檔（換回打霧筆時原樣還在）
def tier_from_row(r): return MAX_ROWS - 1 - r
def row_from_tier(t): return MAX_ROWS - 1 - t
TIER_A = {2: 1.00, 1: 156 / 255.0, 0: 77 / 255.0}   # ShaderTierAlphaFor

fails, checks = [], 0
def ck(name, ok, detail=""):
    global checks
    checks += 1
    if not ok:
        fails.append("FAIL %s  %s" % (name, detail))

RES = [(1280, 720), (1600, 900), (1920, 1080), (2559, 1398), (2560, 1440), (3840, 2160)]
for needle in ("stencil", "liner", "shader"):
  for vw, vh in RES:
    L = layout(vw, vh, needle)
    S, tag = L['S'], "%s %dx%d" % (needle, vw, vh)
    COLS, ROWS = L['cols'], L['rows']

    # 盤形狀的正確性**不在這個迴圈裡驗**——拿 NEEDLES 跟自己比是同義反覆
    # （第一版就是這麼寫的，負向測試把 stencil 改回 (10,3) 照樣 ALL PASS）。
    # 真相住在 C++，對賬在檔尾的 c0a~c0j。
    # 盤恆一排（二維方向選擇會讓換顏色順手改掉不可逆的濃度）
    ck("c0b tray is always a single row " + tag, ROWS == 1, "rows=%d" % ROWS)
    # 抬頭行必須放得進卡片。**內容隨筆而異**（估算跟著實作走，不是抄一個大數字）：
    #   所有筆：筆名（中文 4 字）＋ Q 鍵帽 ＋ 動詞（2 字）
    #   打霧另加：SCROLL 鍵帽 ＋ "100%"
    head_need = 4 * SMALL_PX + (19 + 12) + 2 * SMALL_PX + 20      # 名＋Q＋動詞＋間距
    if L['tier_axis']:
        head_need += (6 * SMALL_PX * 0.62 + 12) + 4 * SMALL_PX * 0.62 + 16
    head_need *= S
    ck("c0b2 header row fits inside the card " + tag,
       head_need <= L['cw'] - 2 * L['pad'] + 1.0,
       "need=%.1f avail=%.1f" % (head_need, L['cw'] - 2 * L['pad']))
    # 格陣在卡片內水平置中（卡片有最小寬度之後，靠左貼齊會讓一格的盤歪在左邊）
    ck("c0b3 grid centred in the card " + tag,
       abs((L['ox'] - L['cx']) - (L['cx'] + L['cw'] - (L['ox'] + L['gw']))) < 0.01,
       "left=%.2f right=%.2f" % (L['ox'] - L['cx'], L['cx'] + L['cw'] - (L['ox'] + L['gw'])))

    ck("c1 card on-screen " + tag,
       L['cx'] >= 0 and L['cy'] >= 0 and L['cx'] + L['cw'] <= vw and L['cy'] + L['ch'] <= vh,
       "card=(%.0f,%.0f) %.0fx%.0f view=%dx%d" % (L['cx'], L['cy'], L['cw'], L['ch'], vw, vh))

    # c2 抬頭字（Y=行框頂端）不壓第一列、且在卡內
    h0 = L['cy'] + L['pad']
    ck("c2 header clear of row0 " + tag, h0 + text_h(S) <= L['oy'] and h0 >= L['cy'],
       "head=%.1f..%.1f row0=%.1f" % (h0, h0 + text_h(S), L['oy']))

    # c3（欄標＝數字鍵帽在末列之下）**已刪除**：數字鍵快捷與那排鍵帽都在 09-03
    # 退役了。留著一條測不存在元素的契約＝它永遠亮綠燈，而綠燈什麼都不保證。

    # c4/c5（左側列標在 gutter 內）**已刪除**：列標隨「三列」一起退役，當前濃度
    # 移到抬頭行右端。抬頭行放不放得下由 c0b2 管。

    # c6/c7 命中無死區
    ok = all(hit(L, cell_pos(L, c, r)[0] + L['cell'] + L['gap'] * 0.5,
                 cell_pos(L, c, r)[1] + L['cell'] * 0.5) is not None
             for r in range(ROWS) for c in range(COLS - 1))
    ck("c6 gap midpoint snaps to a neighbour " + tag, ok)
    ok = all(hit(L, cell_pos(L, 0, r)[0] + L['cell'] * 0.5,
                 cell_pos(L, 0, r)[1] + L['cell'] + L['gap'] * 0.5) is not None
             for r in range(ROWS - 1))
    ck("c7 gap midpoint snaps to a neighbour " + tag, ok)

    # c8 輕點 RMB 必沾回同一杯（退化盤上＝唯一那格，同樣必須自己回自己）
    #    ——注意「杯」現在只含顏色：濃度整條歸滾輪，盤放開時不寫 ShaderTierIdx
    ok = True
    for c in range(COLS):
        for r in range(ROWS):
            x, y = cell_pos(L, c, r)
            got = hit(L, x + L['cell'] * 0.5, y + L['cell'] * 0.5)
            if got != (c, r):
                ok = False
    ck("c8 tap-RMB round-trip (no move = same cup) " + tag, ok)

    # c9 方向選擇的構造保證：**任何位移都落在某一格**，四個角落夾到四個角格。
    #（舊契約是「盤外＝取消」——那是有游標時代的語義，已隨游標一起退役。）
    #  退化盤上四個角全部夾到同一格＝仍然「永遠有解」，這正是要保住的性質。
    corners = {(2, 2): (0, 0), (vw - 2, 2): (COLS - 1, 0),
               (2, vh - 2): (0, ROWS - 1), (vw - 2, vh - 2): (COLS - 1, ROWS - 1)}
    ck("c9 every input snaps to a cell " + tag,
       all(hit(L, px, py) == want for (px, py), want in corners.items()),
       str({k: hit(L, *k) for k in corners}))

# c10/c11 cluster（與筆無關 ⇒ 只跑一輪，不隨三支筆重複計數）
for vw, vh in RES:
    S, tag = max(vh / 1080.0, 0.25), "%dx%d" % (vw, vh)
    cell_c, gap_c = 18.0 * S, 3.0 * S
    colh = 3 * cell_c + 2 * gap_c
    col_y = (vh - 104.0 * S) + 20.0 * S - colh
    pen_y0 = col_y + colh * 0.5 - 17.0 * S
    q_y0 = col_y + colh * 0.5 + 1.0 * S
    ck("c10 cluster rows disjoint " + tag, pen_y0 + text_h(S) <= q_y0,
       "pen_bot=%.1f q_top=%.1f" % (pen_y0 + text_h(S), q_y0))
    ck("c11 cluster above hint " + tag, col_y + colh <= vh - 46.0 * S)

# c12 列↔檔位互為反函式（上濃下淡）
# 用 MAX_ROWS：檔位對應是濃度軸本身的定義，不是「某支筆的盤畫幾列」
# （此前這裡讀的是上面迴圈漏出來的 COLS/ROWS＝依賴「最後一輪剛好是 shader」＝
#  偶然正確；把偶然寫成契約，下次改迴圈順序就會靜靜地測錯東西）
ck("c12 row/tier inverse", all(row_from_tier(tier_from_row(r)) == r for r in range(MAX_ROWS))
   and tier_from_row(0) == 2 and tier_from_row(MAX_ROWS - 1) == 0)

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
    L = layout(vw, vh, "shader")   # 推過頭再回一格＝只有完整盤上量得到的性質
    step = L['cell'] + L['gap']
    start = (cell_pos(L, 0, 0)[0] + L['cell'] * 0.5, cell_pos(L, 0, 0)[1] + L['cell'] * 0.5)
    # 往右狠推 20 格，再往回一格
    p = accumulate(L, start, [(step * 20, 0), (-step, 0)])
    got = hit(L, *p)
    ck("c26 overshoot then one step back " + ("%dx%d" % (vw, vh)),
       got == (L['cols'] - 2, 0), "landed on %s (want col %d)" % (str(got), L['cols'] - 2))
    # 往上狠推再回一格：**單排盤上垂直無處可去**，鉗位後仍是同一格＝正確行為。
    # （這條保留是為了驗 ClampToGrid 在 Y 上也真的有鉗——沒鉗的話累積位移會飛走，
    #   回程要走一樣的距離才回得來＝「對不準」的來源。）
    p = accumulate(L, start, [(0, -step * 20), (0, step)])
    got = hit(L, *p)
    ck("c26 vertical overshoot stays clamped " + ("%dx%d" % (vw, vh)), got == (0, 0),
       "landed on %s" % str(got))
    ck("c26b vertical clamp actually bites " + ("%dx%d" % (vw, vh)),
       abs(accumulate(L, start, [(0, -step * 20)])[1] - L['oy']) < 0.01,
       "y=%.2f oy=%.2f" % (accumulate(L, start, [(0, -step * 20)])[1], L['oy']))

# ---------------------------------------------------------------------------
# c0a~c0f **與 C++ 對賬**：盤的形狀是從 `EInkNeedle` 推出來的，而那個推導的真相
# 住在 NiceInkCharacter.cpp。閘門若只是把結論抄一份到 python 再跟自己核對，就是
# 同義反覆——本檔第一版正是如此：負向測試（把 stencil 改回 10×3）照樣 ALL PASS。
# 這裡改成**從原始碼把那幾行抽出來**，所以「改了 C++ 沒改閘門」或反過來都會失敗。
CH = _src("Source/NiceInk/Private/NiceInkCharacter.cpp")
HD = _src("Source/NiceInk/Private/NiceInkHUD.cpp")
# 2026-09-08 起 chip／操作列／比分都住 Slate ⇒ 要驗它們就要讀這一份。
# （c0g/c0h 曾經在搬家那天起就靜靜地失敗——**閘門讀不到受測物時，它不是變寬鬆，是變成沒有意義**。）
SH = _src("Source/NiceInk/Private/SNiHud.cpp")

m = _re.search(r"L\.bColorAxis\s*=\s*\(Needle\s*!=\s*EInkNeedle::(\w+)\)", CH)
ck("c0a colour axis: only Stencil lacks it (from C++)", bool(m) and m.group(1) == "Stencil",
   "matched=%s" % (m.group(0) if m else "<none>"))

m = _re.search(r"L\.bTierAxis\s*=\s*\(Needle\s*==\s*EInkNeedle::(\w+)\)", CH)
ck("c0b2 tier axis: only Shader has it (from C++)", bool(m) and m.group(1) == "Shader",
   "matched=%s" % (m.group(0) if m else "<none>"))

# 欄數／列數的式子：軸不存在 ⇒ 退化成 1
ck("c0c NumCols degenerates to 1 without the colour axis",
   bool(_re.search(r"L\.NumCols\s*=\s*L\.bColorAxis[\s\S]{0,120}?:\s*1;", CH)))
ck("c0d NumRows is always 1 (single-row tray)",
   bool(_re.search(r"L\.NumRows\s*=\s*1;", CH)))

# 盤放開時**不寫** ShaderTierIdx——濃度整條歸滾輪。這條擋的是「盤又長回第二個維度」。
_pick = CH[CH.index("bInkTrayOpen = false;\n\tint32 Col"):]
_pick = _pick[:_pick.index("NiAudio::Play")]
# **剝掉註解再檢查**：第一版直接搜字串，結果抓到的是這段自己的說明文字
# （「盤放開時永不寫 ShaderTierIdx」）——閘門讀到的是散文不是程式碼。
_pick_code = "\n".join(l.split("//")[0] for l in _pick.split("\n"))
ck("c0k the tray writes colour only, never the tier",
   "ShaderTierIdx" not in _pick_code, "沾杯段出現了 ShaderTierIdx ⇒ 濃度又回到盤上了")

# 盤開著時滾輪要能調濃度（整排一起變淡＝這一版的教學機制本身）
ck("c0l scroll works while the tray is open (same scope as outside)",
   bool(_re.search(r"if\s*\(L\.bTierAxis\s*&&\s*!bTrapDialActive\)", CH)))

# c0n~c0t **針尖指示器**（09-03 六版終案：實體環帶 ＋ 內暗外亮雙向暈）。
# 這一批 user 連打回六次、全是外觀，而外觀我自驗不了 ⇒ **能寫成契約的部分一定要
# 寫滿**，剩下的才交給 viewport。
# 切片錨點**不可以是某條契約要驗的字串**——否則破壞那條契約會讓這裡拋 ValueError、
# 整支腳本崩潰，而**崩潰不產生 FAIL 訊號**（負向測試因此看起來「沒開火」＝空洞契約
# 的另一種形式；09-03 六版負向測試當場抓到）。錨點取 RadiusCm 那行、外加 try 保護：
# 找不到就讓 _ring 為空 ⇒ 依賴它的契約**失敗**而不是消失。
try:
    _ring = HD[HD.index("const float RadiusCm = MyChar->GetNeedleRadiusCmForHud();"):]
    _ring = _ring[:_ring.index("// 巡航導引")]
except ValueError:
    _ring = ""
_ring_code = chr(10).join(l.split("//")[0] for l in _ring.split(chr(10)))

# c0n **不准再用線段拼圓**。DrawLine 有兩個各自獨立的斷法：平頭端點在轉角留楔形
# 缺口；sub-pixel 寬度在無抗鋸齒的光柵化下整段被跳過（user：「為什麼會斷斷續續」）。
# 環帶是實體填充、相鄰段共用頂點 ⇒ 接縫在數學上不存在。
ck("c0n the ring is a solid triangle band, not stitched line segments",
   ("FCanvasTriangleItem RingItem(Tris," in _ring_code
    and "SE_BLEND_Translucent" in _ring_code
    and "DrawLine(" not in _ring_code),
   "圈又用 DrawLine 拼了 ⇒ 會回到斷斷續續")

# c0o **主線帶顏色資訊**（當前這杯墨；稿筆恆結晶紫）
ck("c0o the main line carries the current ink colour",
   ("CrosshairColor" in _ring_code and "NiceInkStencil::Color()" in _ring_code),
   "主線不再是當前墨色 ⇒ 顏色資訊沒了")

# c0p **雙向暈：內暗外亮**。這是「自己製造背景」那一類手段——不需要知道底色。
# 四種情況全涵蓋（底亮／底暗／墨與底同暗／墨與底同亮），最差是中灰底、兩側各有
# 約 0.5 的亮度差＝不讀畫面所能達到的下界。
ck("c0p the ring has a dark inner glow and a light outer glow",
   ("GlowDark" in _ring_code and "GlowLight" in _ring_code
    and "NiHudColor::Ink.CopyWithNewOpacity" in _ring_code
    and "NiHudColor::Paper.CopyWithNewOpacity" in _ring_code),
   "雙向暈少了一側 ⇒ 某一類底色上會整個消失")

# c0q 暈必須是**漸層**（外緣 alpha=0）——不然是三條並排的線，不是一條帶柔邊的線
# c0q **衰減曲線由貼圖驅動，不是頂點的線性插值**（09-03 六版三修，user：「暈感
# 不夠重，太像一圈亮圈、一圈暗圈」）。Canvas 的頂點顏色只能線性插值，而線性衰減
# 有一個看得出來的終點 ⇒ 讀成「一條有邊界的帶」而不是「從線滲出去的光」。
# 把曲線放進 64×1 的貼圖（alpha 走 e^-3t、扣尾正規化到 0）⇒ **三角形數量不變**
# 就能得到任意非線性，而每幀成本已經被 cruise tipSpd 抓過兩次，這一點是硬條件。
ck("c0q the falloff curve lives in a texture, not in vertex lerp",
   ("GlowTex" in _ring_code and "FMath::Exp(-K * T)" in HD
    and "GlowDark, GlowDark, 1.0f, 0.0f" in _ring_code
    and "GlowLight, GlowLight, 0.0f, 1.0f" in _ring_code),
   "暈又回到頂點線性插值 ⇒ 邊緣會出現看得見的終點")

# c0x 暈的峰值要對得起「總視覺重量」：user 要的是把線性版 0.55 降到 0.275
#（平均覆蓋 0.1375），而指數曲線平均覆蓋只有 0.281 ⇒ 峰值 0.489 才是同樣的重量。
# **換了衰減曲線就必須重算峰值**，照抄舊數字會讓暈整個消失（0.275×0.281=0.077）。
# 兩側都要驗：只驗「有 0.49」的話，破壞其中一個顏色照樣亮綠燈（負向測試抓到）。
ck("c0x the glow peak is recomputed for the exponential curve",
   ("NiHudColor::Ink.CopyWithNewOpacity(0.49f)" in _ring_code
    and "NiHudColor::Paper.CopyWithNewOpacity(0.49f)" in _ring_code),
   "峰值沒有跟著曲線重算 ⇒ 暈的總重量會偏掉一倍")

# c0r 主線維持細（1.5px）。三版曾把圈加粗一倍（user：「好醜」）。
ck("c0r the main line stays thin", "const float LW = 1.5f * UiScale;" in HD)

# c0s 圈內沒有填色（濃度回歸 chip）
ck("c0s the ring has no tier fill",
   ("FCanvasNGonItem" not in HD and "K2_DrawPolygon(nullptr, Aim," not in HD))

# c0t 三支筆共用一個指示器，半徑＝真實落墨半徑
_hh = _src("Source/NiceInk/Public/NiceInkCharacter.h")
ck("c0t all three pens share one indicator, sized by the real nib radius",
   ("GetNeedleRadiusCmForHud()" in HD and "DrawRect(DotCol," not in HD
    and "if (bShaderNeedle && bReach)" not in HD
    and "ShaderBrushRadiusCm" in _hh and "TattooNibDiameterCm * 0.5f" in _hh))

# c0u **段數隨半徑**（09-03 五版血價）：`cruise tipSpd` 契約 1.50~2.00、基線 1.99
# ＝只有 0.5% 餘裕；我對 4~5px 的小圈也畫 28 段把它推到 2.02（兩輪穩定重現、
# stash 驗基線復現 1.99）。**每幀成本會沿著離散步進洩漏進手感**——針以 v_max
# 逐幀追趕，幀率一降每幀就走更遠。
# c0w **暈寬隨半徑、小圈不畫內暈**（09-03 六版二修）。設計理由＝4px 半徑的圈內側
# 只有 2px，2.5px 的內暈會把中心填成一坨；成本理由＝三環帶每段 6 個三角形（線段版
# 4 個）把 cruise tipSpd 從 1.98 推到 2.01，省掉小圈的內暈才回到 1.98。
ck("c0w the glow scales with the radius and small rings skip the inner glow",
   ("FMath::Min(2.5f * UiScale, Rpx * 0.3f)" in _ring_code
    and "const bool bInnerGlow = Rpx > 10.0f * UiScale;" in _ring_code
    and "if (bInnerGlow)" in _ring_code),
   "小圈又在畫內暈 ⇒ 中心糊成一坨，而且成本會推移 cruise tipSpd")

ck("c0u the segment count scales with the radius",
   "FMath::Clamp(FMath::RoundToInt(Rpx * 0.9f), 8, 28)" in HD,
   "小圈又在畫 28 段 ⇒ 每幀成本會推移 cruise tipSpd")

# c0v **底色亮度圖那一整套必須保持拆除**（09-03 六版）：雙向暈不需要知道底色 ⇒
# 逐段取樣／Jacobian／64KB 的圖／每針記帳／洗墨重算全部沒有消費者。留著＝死碼，
# 而且它的每幀成本正是上一條血價的來源。
_cc = _src("Source/NiceInk/Private/InkCanvasComponent.cpp")
_ch2 = _src("Source/NiceInk/Private/NiceInkCharacter.cpp")
ck("c0v the luminance-grid machinery stays removed",
   not any(k in (HD + _cc + _ch2) for k in
           ("SurfaceLum", "SolveEdgeColor", "GetTipRingLumForHud", "NoteSurfaceLum")),
   "亮度圖又回來了 ⇒ 沒有消費者的基礎設施＋每幀成本")

# 抬頭寬度下限是**單一來源**：閘門的 HEAD_MIN_W 必須等於 C++ 的 HeadMinWidth()
_hd = _src("Source/NiceInk/Public/NiceInkCharacter.h")
m = _re.search(r"HeadMinWidth\(\)\s*\{\s*return\s*([0-9.]+)f;\s*\}", _hd)
ck("c0m header width floor matches C++", bool(m) and abs(float(m.group(1)) - HEAD_MIN_W) < 1e-6,
   "C++=%s  gate=%s" % (m.group(1) if m else "<none>", HEAD_MIN_W))

# **軸存在與否的最終真相＝落墨**：BeginStroke 只在打霧筆送分檔 alpha，其餘恆 255。
# 盤的形狀若與這一行不一致，玩家就會看到「選了但落墨沒變」（09-03 修掉的原始 bug）。
ck("c0e tier alpha reaches the canvas only for Shader (from C++)",
   bool(_re.search(r"SelectedNeedle\s*==\s*EInkNeedle::Shader\)[\s\S]{0,80}?ShaderTierAlphaFor\(ShaderTierIdx\)\s*:\s*255", CH)),
   "BeginStroke 的分檔 alpha 條件變了 ⇒ 盤的形狀要跟著重新推導")

# 稿筆恆結晶紫＝顏色軸不存在的真相來源
ck("c0f stencil forces crystal violet at BeginStroke (from C++)",
   bool(_re.search(r"SelectedNeedle\s*==\s*EInkNeedle::Stencil\)[\s\S]{0,60}?NiceInkStencil::Color\(\)", CH)))

# c0g chip 與盤看的是同一個條件（HUD 端）：此前 chip 無條件畫「顏色＋百分比」
# ＝拿稿筆時顯示藍色 60% 而落墨是紫色 100%（HUD 說謊）。
ck("c0g chip hides the percentage off-Shader (from Slate)",
   bool(_re.search(r"EVisibility\s+PctVis\(\)\s*const\s*\{\s*return\s+IsShader\(\)\s*\?", SH))
   and bool(_re.search(r"bool\s+IsShader\(\)\s*const\s*\{[\s\S]{0,120}?SelectedNeedle\s*==\s*EInkNeedle::Shader", SH)))
ck("c0h chip shows crystal violet for the stencil (from Slate)",
   bool(_re.search(r"bStencil\s*=\s*\(C->SelectedNeedle\s*==\s*EInkNeedle::Stencil\)[\s\S]{0,160}?bStencil\s*\?\s*NiceInkStencil::Color\(\)", SH)))

# c0i 滾輪的作用域＝濃度軸的作用域（PollLockedDraw）；順帶驗轉盤讓路（A1）
ck("c0i scroll wheel is scoped to Shader and yields to the trap dial (from C++)",
   bool(_re.search(r"if\s*\(!bTrapDialActive\s*&&\s*SelectedNeedle\s*==\s*EInkNeedle::Shader\)", CH)))

# c0j 數字鍵快捷確實不見了（B4）——留著一顆會改狀態卻沒有回饋的鍵，比沒刪更糟
ck("c0j the 1-0 colour shortcut is gone (from C++)",
   "PollPalette(PC);" not in CH and "void ANiceInkCharacter::PollPalette" not in CH)


# ---- 2026-09-09 圖示大減（user：「這些 icon 都非常詞不達意」）----
# 這批的風險不是「畫錯」，是**悄悄長回來**：下一個人覺得某一行「有點空」就補一顆圖示，
# 於是二階隱喻一顆一顆回到畫面上。所以把「不存在」寫成閘門。
_SRC_ALL = "".join(_src(p) for p in (
    "Source/NiceInk/Private/SNiHud.cpp",
    "Source/NiceInk/Private/NiceInkHUD.cpp",
    "Source/NiceInk/Public/NiceInkHUD.h",
))
ck("c1a the verb-icon table stays gone (no ActionIcon anywhere)",
   ("ActionIcon" not in _SRC_ALL),
   "動詞圖示回來了 ⇒ 21 條提示每一條的動詞本來就把答案寫在那裡（13 語齊全）")

# 畫面上允許存在的圖示＝這五個名字，全部有出處：滑鼠三顆 Kenney CC0、銭與猪口自家畫。
_names = set(_re.findall(r'Ico\(TEXT\("(\w+)"\)\)', _SRC_ALL)) | set(
    _re.findall(r'GetIcon\(TEXT\("(\w+)"\)\)', _SRC_ALL))
ck("c1b only the five allowed icons are referenced",
   _names <= {"mouse_left", "mouse_right", "mouse_scroll", "coin", "choko_full", "choko_empty"},
   "多出來的：%s" % sorted(_names - {"mouse_left", "mouse_right", "mouse_scroll",
                                    "coin", "choko_full", "choko_empty"}))

# 資產＝來源資料夾的鏡像（ue_import_icons.py 會刪多的）。/Game/UI 整個目錄在 cook 白名單裡，
# 留在硬碟上的孤兒圖示會直接進包。
import os as _os
_icodir = _os.path.join(ROOT, "Content", "UI", "Icons")
_assets = sorted(f[:-7] for f in _os.listdir(_icodir) if f.endswith(".uasset"))
ck("c1c icon assets mirror the source folder",
   _assets == ["T_Ico_choko_empty", "T_Ico_choko_full", "T_Ico_coin", "T_Ico_mouse_left",
               "T_Ico_mouse_right", "T_Ico_mouse_scroll", "T_UI_MarkerPen", "T_UI_TattooPen"],
   "實際=%s" % _assets)

# 猪口的滿／空是**兩張圖**（面積差），不是同一張換 tint（亮度差）——
# 亮度差在 40px 的三顆並排上讀不出「喝了幾杯」。
ck("c1d the cup row picks full/empty art per cup",
   bool(_re.search(r"GetChokoIcon\(i\s*<\s*this->Cups\(\)\)", SH)))

# 房主＝字（LobbyHostTag，13 語）；冠不得回來
# **兩個**房主記號（大廳玩家卡＋ESC 玩家列）。只驗「字串出現過」擋不住「其中一處被拿掉」
# ——負向測試證明過：改掉兩處之一，那版閘門照樣 PASS。
ck("c1e both host markers are text, not a crown",
   SH.count("ENiLocKey::LobbyHostTag") >= 2 and ('"crown"' not in _SRC_ALL),
   "LobbyHostTag 出現 %d 次（要 2：大廳玩家卡＋ESC 玩家列）" % SH.count("ENiLocKey::LobbyHostTag"))

# 身體小地圖（09-07 加、09-09 拆）：user 看到它的第一句話是「這是什麼？」
# ——畫面上唯一沒有標籤也沒有邊界的東西。連同它的繪製點一起寫成閘門，免得又長回來。
ck("c1f the body minimap stays gone",
   ("DrawBodyMap" not in _SRC_ALL),
   "身體小地圖回來了 ⇒ 它要嘛有標籤與邊界，要嘛不存在")

print(chr(10).join(fails) if fails else "ALL PASS")
print("checks=%d  fail=%d" % (checks, len(fails)))
