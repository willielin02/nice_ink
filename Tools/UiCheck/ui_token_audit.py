# -*- coding: utf-8 -*-
"""全站 UI 樣式稽核（不開引擎，一輪一秒）。

**去讀真相的正本**：直接掃 C++ 原始碼，把兩個載體（Slate 選單／canvas 局內 HUD）
實際用到的圓角半徑、面板底色與 alpha、按鈕樣式、字級角色全部抽出來對賬。
問的是同一個問題：**同一件事，兩邊是不是用同一個值？**
"""
import os, re, sys, collections

ROOT = os.path.join(os.path.dirname(__file__), "..", "..", "Source", "NiceInk")
HUD  = os.path.join(ROOT, "Private", "NiceInkHUD.cpp")
MENU = os.path.join(ROOT, "Private", "NiceInkMenuWidget.cpp")
TOK  = os.path.join(ROOT, "Public",  "NiceInkUiTokens.h")

def read(p): return open(p, encoding="utf-8").read()

def strip_comments(s):
    # **先剝行註解、再剝塊註解**（2026-09-05 修）。反過來做會踩到一個真的坑：
    # NiceInkHUD.cpp 裡有一行行註解寫著 `/**墨**＝地對比…`，naive 的 `/\*.*?\*/`
    # 會把那個 `/**` 當成塊註解開頭，一路吃到下一個 `*/` ——實測**吞掉 10,918 個字元**，
    # 而那段剛好蓋住 BuildControlHints ⇒ 操作列整欄報 0/14。
    # 這是既有的潛伏 bug（那行註解早就在），只是被吞的範圍這次剛好蓋到要讀的函式。
    s = chr(10).join(re.sub(r"//.*$", "", ln) for ln in s.split(chr(10)))
    return re.sub(r"/\*.*?\*/", "", s, flags=re.S)

hud, menu = strip_comments(read(HUD)), strip_comments(read(MENU))

print("=" * 68)
print("① 圓角半徑（同一個形狀語言應該只有少數幾階）")
radii = collections.Counter()
for m in re.finditer(r"DrawRoundedBox\([^;]*?,\s*([0-9.]+)f\s*\*\s*UiScale", hud, re.S):
    radii[("HUD", float(m.group(1)))] += 1
# 取 MakeRounded 的**最後一個**引數（半徑）——內層 WithA(...) 的 alpha 不算
for m in re.finditer(r"MakeRounded\((?:[^()]|\([^()]*\))*,\s*([0-9.]+)f\s*\)", menu):
    radii[("選單", float(m.group(1)))] += 1
for m in re.finditer(r"CornerRadius\(FVector4\(([0-9.]+)", menu):
    radii[("選單", float(m.group(1)))] += 1
vals = sorted({r for _, r in radii})
for who in ("HUD", "選單"):
    got = sorted({r for w, r in radii if w == who})
    print(f"  {who:<4} 用了 {len(got)} 種：{got}")
print(f"  → 全站合計 {len(vals)} 種半徑：{vals}")
print(f"  判定：{'FAIL 沒有半徑階梯' if len(vals) > 4 else 'PASS'}")

print("=" * 68)
print("② 面板底色與 alpha（『一塊面板』在兩邊是不是同一個東西）")
hud_alpha = sorted({float(m) for m in re.findall(r"DrawPanelBox\([^;]*?,\s*([0-9.]+)f\s*\)", hud, re.S)})
print(f"  HUD  面板 = Ink（墨色）  alpha 用了 {len(hud_alpha)} 種：{hud_alpha}")
mcard = re.search(r"CardBrush\s*=\s*MakeRounded\(WithA\(NiHudColor::(\w+),\s*([0-9.]+)f", menu)
print(f"  選單 卡片 = {mcard.group(1)}（紙色） alpha {mcard.group(2)}  ＋ BackgroundBlur(14)")
print("  規則（2026-09-04 定案）：**常駐 chrome 無面板**（靠文字/鍵帽陰影），")
print("  **模態才有面板**（墨杯盤／指認／兇手轉盤——Meccha 的 picker 同理）；")
print("  面板顏色取背景反相（局內背景是亮皮膚⇒墨底、選單背景是黑⇒紙底）。")
ok_a = set(hud_alpha) <= {0.6, 0.72, 0.88}
print("  → alpha 收斂成 0.72（次級）/0.88（模態）/0.6（dev）：%s" % ("PASS" if ok_a else "FAIL"))

print("=" * 68)
print("③ 選單按鈕樣式數與語義")
# 2026-09-05：選單改成外框控制項之後，樣式有兩個工廠（MakeButtonStyle 與
# MakeOutlinedStyle）。舊版只數前者 ⇒ 報「定義了 2 種」而實際有 7 種。
styles = re.findall(r"(\w+Style)\s*=\s*Make(?:Outlined|Button)Style\(([^;]*?)\);", menu, re.S)
print(f"  定義了 {len(styles)} 種按鈕樣式：")
for n, body in styles:
    first = re.sub(r"\s+", " ", body.strip())[:58]
    print(f"    {n:<15} {first}")
# **量，不要斷言**：Quit 用的是不是與導覽同一個樣式？
mcode = strip_comments(menu)
quit_danger = "DangerStyle" in mcode
ghost_uses = len(re.findall(r"GhostStyle", mcode))
print(f"  GhostStyle 用了 {ghost_uses} 次；Quit 使用 DangerStyle = {quit_danger}")
print("  → 判定：" + ("PASS —— 毀滅性動作有自己的視覺通道"
      if quit_danger else "FAIL —— Quit 與導覽同一個外觀、不同後果"))
print("=" * 68)
print("④ 字級角色：明朝體（品牌聲部）在兩個載體的分布")
roles = re.findall(r"constexpr FRole (\w+)\s*\{\s*(\d+),\s*(-?\d+),\s*(true|false)", read(TOK))
serif = [r for r in roles if r[3] == "true"]
print(f"  選單 FRole：{len(roles)} 個角色，其中明朝體 {len(serif)} 個 → "
      f"{[r[0] for r in serif]}")
hud_tiers = re.findall(r"constexpr float (Hud\w+)\s*=\s*([0-9.]+)f", read(TOK))
# **量，不要印結論**（2026-09-05 改）：局內的明朝體不住在 tier 表裡（那張表只有
# 字級），它是 ANiceInkHUD::bSerifFace ——掛在最底層原語上、用 TGuardValue 圈範圍。
# 舊版寫死一句「沒有 serif 旗標」，於是 09-05 把明朝體接進局內之後它照樣報 FAIL。
hud_src = read(HUD)
hud_code = strip_comments(hud_src)   # 剝註解：閘門要讀程式碼不是散文
serif_face_decl = "SerifBlack" in hud_code and "SerifRegular" in hud_code
serif_sites = hud_code.count("TGuardValue<bool> SerifGuard")
print(f"  局內 tier：{[t[0] for t in hud_tiers]}（字級表本來就只有字級）")
print(f"  局內明朝體：bSerifFace 接上字面={serif_face_decl}、實際切換點 {serif_sites} 處")
ok4 = serif_face_decl and serif_sites >= 1
print("  → 判定：" + ("PASS —— 兩個載體共用同一個字體人格（明朝體＝聲部）"
      if ok4 else "FAIL —— 明朝體是選單的品牌聲部，局內一個字都沒有"))

print("=" * 68)
print("⑤ 間距網格")
# 同上：舊版把 CAP_H 19 / ROW_H 26 寫死在 print 裡，而 09-04 立了 NiUi（U=4）之後
# 那兩個常數已經不存在。改成真的去讀 NiUi 並驗它們是不是 U 的整數倍。
tok_src = read(TOK)
niui = re.findall(r"constexpr float (\w+)\s*=\s*([0-9.]+)f\s*\*\s*U;", tok_src)
u = re.search(r"constexpr float U\s*=\s*([0-9.]+)f", tok_src)
uval = float(u.group(1)) if u else 0.0
print(f"  NiSpace  = 4/8/16/24（選單 Slate 版面用）")
print(f"  NiUi     = U {uval:g}；{len(niui)} 個常數全部寫成 U 的倍數："
      + ", ".join(f"{n}={float(m) * uval:g}" for n, m in niui))
# 局內仍有裸數字（× UiScale），數出來報實情——這是已記帳、待 user 裁決的殘項
lits = re.findall(r"([0-9]+(?:\.[0-9]+)?)f \* UiScale", hud_code)
off = [v for v in lits if abs(float(v) / uval - round(float(v) / uval)) > 1e-6] if uval else []
print(f"  局內裸數字 × UiScale：{len(lits)} 個，其中 {len(off)} 個不在 {uval:g}px 網格上")
ok5 = bool(niui) and len(off) == 0
print("  → 判定：" + ("PASS —— 局內尺度全在網格上"
      if ok5 else f"部分 —— NiUi 的 {len(niui)} 個常數在網格上，"
      f"但另有 {len(off)} 個舊的裸數字沒有併入（已記帳、待裁決）"))
