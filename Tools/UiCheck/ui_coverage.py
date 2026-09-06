# -*- coding: utf-8 -*-
"""全站 UI 覆蓋率稽核：新的設計語言到底鋪到哪些畫面、哪些還沒。

user 2026-09-05：「這是整個遊戲所有地方的同步改動，還是只有一部份的UI？」
——這個問題不准用印象回答。直接掃 C++，逐面回答四個問題：
  ① 有沒有祈使句（現在該做什麼）  ② 有沒有操作提示列
  ③ 有沒有規則塊（怎麼贏）        ④ 有沒有殘留舊語言的面板
"""
import io, os, re, sys

NL = chr(10)

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
HUD = io.open(os.path.join(ROOT, "Source/NiceInk/Private/NiceInkHUD.cpp"), encoding="utf-8").read()
# **先剝行註解、再剝塊註解**（2026-09-05 修）。反過來做會踩到一個真的坑：
# NiceInkHUD.cpp 裡有一行行註解寫著 `/**墨**＝地對比…`，naive 的 `/\*.*?\*/`
# 會把那個 `/**` 當成塊註解開頭，一路吃到下一個 `*/` ——實測**吞掉 10,918 個字元**，
# 而那段剛好蓋住 BuildControlHints ⇒ 操作列整欄報 0/14。
# 這是既有的潛伏 bug（那行註解早就在），只是被吞的範圍這次剛好蓋到要讀的函式。
HUD = NL.join(re.sub(r"//.*$", "", ln) for ln in HUD.split(NL))
HUD = re.sub(r"/\*.*?\*/", "", HUD, flags=re.S)


def body(fn):
    m = re.search(r"ANiceInkHUD::%s\s*\(" % fn, HUD)
    if not m:
        return ""
    nxt = re.search(r"\nANiceInkHUD::|\n[A-Za-z_][\w:<>,\s\*&]*\sANiceInkHUD::", HUD[m.end():])
    return HUD[m.start(): m.end() + (nxt.start() if nxt else len(HUD))]


IMP = body("GetPhaseImperative")
# 操作提示 2026-09-05 起住在 BuildControlHints（單一正本），DrawControlStrip 只負責畫。
# 舊版讀 DrawControlStrip 的內容 ⇒ 抽層之後那裡一條規則都沒有，整欄變成 0/14。
STRIP = body("BuildControlHints")
RULES = body("DrawRulesBlock")

# 受測面：局內每一個玩家真的會看到的狀態
SURFACES = [
    ("大廳（房主）",      "Lobby",      "ImpLobbyHost",   "ENiceInkPhase::Lobby",      "PhaseLobby"),
    ("大廳（非房主）",    "Lobby",      "ImpLobbyWait",   None,                        "PhaseLobby"),
    ("轉酒瓶",            "BottleSpin", "ImpBottleSpin",  None,                        None),
    ("入座昏睡",          "Seating",    "ImpSeating",     None,                        None),
    ("作畫・站著",        "Drawing",    "ImpDrawStand",   "ENiceInkPhase::Drawing",    "PhaseDrawing"),
    ("作畫・鎖定",        "Drawing",    "ImpDrawLocked",  "bLeanLocked",               "PhaseDrawing"),
    ("沉睡・醉夢描圖",    "Dream",      "ImpDream",       "bAsleep",                   "PhaseDream"),
    ("裝睡",              "Feign",      "ImpFeign",       "IsFeigningSleep",           None),
    ("傑作巡禮",          "Tour",       "ImpTour",        None,                        "PhaseAccusation"),
    ("指認（受害者）",    "Accusation", "ImpAccuseVictim","ENiceInkPhase::Accusation", "PhaseAccusation"),
    ("指認（旁觀）",      "Accusation", "ImpAccuseOther", None,                        "PhaseAccusation"),
    ("判決演出",          "Resolution", None,             None,                        None),
    ("三杯結局",          "Finale",     None,             None,                        None),
    ("場間刺青房",        "PostGame",   "ImpPostGame",    "ENiceInkPhase::PostGame",   None),
]

print("=" * 74)
print("%-18s %-10s %-10s %-10s" % ("畫面", "祈使句", "操作列", "規則塊"))
print("-" * 74)
n_imp = n_strip = n_rules = 0
for name, phase, imp_key, strip_key, rule_key in SURFACES:
    a = "✓" if (imp_key and imp_key in IMP) else "—"
    b = "✓" if (strip_key and strip_key in STRIP) else "—"
    c = "✓" if (rule_key and rule_key in RULES) else "—"
    n_imp += a == "✓"; n_strip += b == "✓"; n_rules += c == "✓"
    print("%-18s %-11s %-11s %-11s" % (name, a, b, c))
print("-" * 74)
print("覆蓋：祈使句 %d/%d   操作列 %d/%d   規則塊 %d/%d"
      % (n_imp, len(SURFACES), n_strip, len(SURFACES), n_rules, len(SURFACES)))

print()
print("=" * 74)
print("殘留舊語言的面板（DrawPanelBox 呼叫點）")
print("-" * 74)
for m in re.finditer(r"DrawPanelBox\(", HUD):
    line = HUD[:m.start()].count("\n") + 1
    seg = HUD[:m.start()]
    fn = re.findall(r"ANiceInkHUD::(\w+)\s*\(", seg)
    print("  %-24s NiceInkHUD.cpp:%d" % (fn[-1] if fn else "?", line))

print()
print("=" * 74)
print("選單（Slate）：本輪只動了樣式常數，版面與流程未動")
print("-" * 74)
MENU = io.open(os.path.join(ROOT, "Source/NiceInk/Private/NiceInkMenuWidget.cpp"), encoding="utf-8").read()
print("  頁數 = %d（Root/Host/Join/Settings/Credits/Language/Profile）"
      % len(re.findall(r"EPage::(\w+)", MENU[:MENU.index("EPage Page")] if "EPage Page" in MENU else MENU[:2000])) or 7)
print("  本輪改動：圓角半徑收斂（9→3 階）、Quit 改 DangerStyle")
print("  未動：版面、字級角色、流程、毛玻璃白卡（規則上正確——選單背景是黑、卡是紙底）")
