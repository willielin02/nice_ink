# -*- coding: utf-8 -*-
"""局內 HUD 本地化閘門（不開引擎、一輪一秒）。

為什麼要機械化：2026-09-02 這一週，我們正在討論「HUD 沒有本地化」的同時，
我自己又往裡面加了六條寫死的英文字串。**意志力不會 scale，閘門才會。**

規則：`GUARDED` 名單裡的函式（＝作畫相位，已全數進字串表）**不准出現會上畫面的
裸英文字面量**——出現就 exit 1。其餘函式屬已記帳的待辦，只計數不擋（見
Docs/DRAW_HUD_INVENTORY.md 的「記帳（刻意未做）」）。

跑法：  python -X utf8 Tools/UiCheck/hud_loc_lint.py
"""
import io
import os
import re
import sys

HUD = os.path.join(os.path.dirname(__file__), "..", "..",
                   "Source", "NiceInk", "Private", "NiceInkHUD.cpp")

# 作畫相位的畫面：本批已全數進 NiceInkLocText，之後不准退步
GUARDED = {
    "DrawInkChip", "DrawInkTray", "DrawInkCup",
    "DrawInkCrosshair", "DrawTopBar", "GetPhaseLabel", "DrawHUD",
    "DrawControlStrip", "DrawKeycap",
}

# 刻意不進字串表的字面量，每一條都要有理由
ALLOW = {
    # 純格式/符號，沒有語言內容
    "%d%%": "百分比格式（數字由 FText 側處理）",
    "%s   ·   %s": "分隔符版型",
    "  ·  %.0fs": "倒數版型",
    "%d / %d": "計數版型",
    # 鍵位字形＝物理鍵盤上的刻字，翻譯反而讀不到那顆鍵
    "Q": "鍵位字形",
    "F": "鍵位字形",
    "G": "鍵位字形",
    "WASD": "鍵位字形",
    "ESC": "鍵位字形",
    "LMB": "鍵位字形",
    "RMB": "鍵位字形",
    "SCROLL": "鍵位字形（滑鼠滾輪）",
    "?": "無臉時的佔位字形",
    # 引擎/資產識別字，不是 UI 文字
    "Regular": "字體 typeface 名",
    "Bold": "字體 typeface 名",
    "SerifRegular": "字體 typeface 名",
    "SerifBlack": "字體 typeface 名",
    "NiUiFont": "字體物件名",
    "ni.DebugHud": "console 變數名",
}

CALL_RE = re.compile(
    r'(?:DrawTok|DrawBottomHint|DrawBigTitle|Button|AdjustRow)\s*\(\s*TEXT\("([^"]*)"\)')
FUNC_RE = re.compile(r'^[A-Za-z_][\w:<>,\s\*&]*\sANiceInkHUD::(\w+)\s*\(', re.M)
HAS_WORD = re.compile(r'[A-Za-z]{2,}')


def main():
    src = io.open(HUD, encoding="utf-8").read()

    # 依函式切段
    marks = [(m.start(), m.group(1)) for m in FUNC_RE.finditer(src)]
    marks.append((len(src), "<eof>"))

    violations, backlog = [], []
    for i in range(len(marks) - 1):
        start, name = marks[i]
        body = src[start:marks[i + 1][0]]
        line0 = src[:start].count("\n") + 1
        for m in CALL_RE.finditer(body):
            lit = m.group(1)
            if lit in ALLOW or not HAS_WORD.search(lit):
                continue
            line = line0 + body[:m.start()].count("\n")
            rec = (name, line, lit[:64])
            (violations if name in GUARDED else backlog).append(rec)

    if violations:
        print("FAIL  作畫相位出現裸英文字面量（必須進 NiceInkLocText）：")
        for name, line, lit in violations:
            print("  NiceInkHUD.cpp:%d  %s()  \"%s\"" % (line, name, lit))
        print()
        print("修法：在 ENiLocKey 加一個鍵、在 GTable 加一列 13 語，改用 NiLoc::T()。")
        print("（表的列數有 static_assert 對賬，少一列會編譯失敗。）")

    print("guarded=%d functions   violations=%d   backlog(其餘相位, 已記帳)=%d"
          % (len(GUARDED), len(violations), len(backlog)))
    if backlog:
        seen = {}
        for name, _, _ in backlog:
            seen[name] = seen.get(name, 0) + 1
        print("  backlog by function: " +
              ", ".join("%s=%d" % kv for kv in sorted(seen.items())))
    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main())
