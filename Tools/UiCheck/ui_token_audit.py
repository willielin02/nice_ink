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
    s = re.sub(r"/\*.*?\*/", "", s, flags=re.S)
    return "\n".join(re.sub(r"//.*$", "", ln) for ln in s.split("\n"))

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
styles = re.findall(r"(\w+Style)\s*=\s*MakeButtonStyle\(([^;]*?)\);", menu, re.S)
print(f"  定義了 {len(styles)} 種按鈕樣式：")
for n, body in styles:
    first = re.sub(r"\s+", " ", body.strip())[:58]
    print(f"    {n:<15} {first}")
ghost_uses = len(re.findall(r"GhostStyle", menu))
print(f"  GhostStyle 被用了 {ghost_uses} 次 —— 導覽(Profile/Settings/Back) 與 "
      f"離開遊戲(Quit) 共用同一個外觀 ⇒ 同一視覺＝不同後果")

print("=" * 68)
print("④ 字級角色：明朝體（品牌聲部）在兩個載體的分布")
roles = re.findall(r"constexpr FRole (\w+)\s*\{\s*(\d+),\s*(-?\d+),\s*(true|false)", read(TOK))
serif = [r for r in roles if r[3] == "true"]
print(f"  選單 FRole：{len(roles)} 個角色，其中明朝體 {len(serif)} 個 → "
      f"{[r[0] for r in serif]}")
hud_tiers = re.findall(r"constexpr float (Hud\w+)\s*=\s*([0-9.]+)f", read(TOK))
print(f"  局內 tier：{[t[0] for t in hud_tiers]} —— **沒有 serif 旗標**")
print("  → 判定：FAIL —— 明朝體是選單的品牌聲部，"
      "**局內一個字都沒有**；同一款遊戲兩種字體人格")

print("=" * 68)
print("⑤ 間距網格")
print("  NiSpace  = 4/8/16/24（註解自陳：『選單 Slate 版面用』）")
print("  局內 HUD = CAP_H 19、ROW_H 26、BandTop 56 …… 19%%4=%d 26%%4=%d" % (19 % 4, 26 % 4))
print("  → 判定：FAIL —— 局內從未併入網格，且沒有自己的網格")
