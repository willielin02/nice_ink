#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""從 Claude Code 對話匯出檔抽出「自然語言」，移除其餘內容。

匯出檔裡的非自然語言內容全部是工具區塊，形狀有四種：

    Bash/PowerShell <說明>   IN <指令…>   OUT <輸出…>
    Edit  <路徑>             Added N lines            （不含內容）
    Write <路徑>             N lines＋接下來 N 行檔案內容
    Read  <路徑>                                      （單行）

前三種的**起點與長度都是結構性的**，可以確定地切掉：IN 區一路跳到 OUT、Write 自
報行數。唯一沒有結束標記的是 **OUT 區**——程式輸出後面直接接敘述文字，中間沒有
空行也沒有分隔線。所以 OUT 區只能掃到「下一個工具標頭」或「第一行看起來像敘述文
字的行」為止。

好消息是：一旦判定進入敘述狀態就整段照抄到下一個工具標頭，因此**只有每一段的第
一行需要判斷正確**（此檔 118 個 OUT 區＝118 次判斷），誤判的代價也是局部的。判準
見 looks_like_prose()；跑 --report 可以把每一個切點印出來人工複核。

判準分兩級：
  HARD  結構性簽名（箭頭、路徑、grep 行號前綴、賦值、方括號、「標籤: 數字」結尾）
        ——一律不是敘述文字。
  SOFT  詞彙性簽名（PASS/FAIL/max/p50/副檔名）——只在中文佔比低時採信。敘述文字
        本來就會講「三個測試全 PASS，回收率 100%」，中文句子不能因為出現 PASS 就
        被丟掉。

用法：
    python Tools/transcript_prose.py <輸入檔> [-o 輸出檔] [--report]

不給 -o 時輸出到 <輸入檔同名>.prose.txt。
"""

import argparse
import io
import os
import re
import sys

BACKSLASH = chr(92)

# ------------------------------------------------------------ 結構性標記

TOOL_HDR = re.compile(
    r"^(Bash|PowerShell|Edit|MultiEdit|Update|Write|Read|Glob|Grep|Task|"
    r"TodoWrite|WebFetch|WebSearch|Agent|Skill|NotebookEdit|Artifact)"
    r"(?:\s+\S.*)?$"
)
EDIT_SUMMARY = re.compile(
    r"^(Added|Removed|Updated|Changed|Applied|Wrote|Modified|Created|Deleted|Renamed)\b")
LINE_COUNT = re.compile(r"^(\d+)\s+lines?$")
# Grep/Glob 的結果摘要行：「59 lines of output」「3 files」——內容不進匯出檔
RESULT_COUNT = re.compile(r"^\d+\s+\w+(\s+of\s+output)?$")

# 網頁工具的標頭沒有空白：「Web SearchTikTok Display API…」「Web Fetchhttps://…」
WEB_HDR = re.compile(r"^Web (Search|Fetch)\S")
WEB_FETCHED = re.compile(r"^Fetched from \S")
# Web Search 區塊的結束標記＝搜尋工具塞給模型的那句指示
WEB_SEARCH_END = re.compile(r"^REMINDER: You MUST include the sources")


# 匯出檔裡的 harness 區塊：背景任務通知、過大輸出的落檔通知。整塊丟掉。
XML_BLOCKS = {
    "<task-notification>": "</task-notification>",
    "<persisted-output>": "</persisted-output>",
}


def any_header(line):
    return bool(TOOL_HDR.match(line) or WEB_HDR.match(line))


def starts_tool_block(lines, i):
    """lines[i] 是不是一個工具區塊的開頭。

    只有標頭還不夠——敘述文字也可能以 Read／Update 開頭。要求下一行是該工具
    自己的第二行（IN／N lines／Added…）或另一個標頭，才算數。用在 Write 內容
    的邊界上：自報行數實測會差一行，光靠計數會把下一個區塊的標頭吃掉。
    """
    if i >= len(lines):
        return False
    if WEB_HDR.match(lines[i]):
        return True
    if not TOOL_HDR.match(lines[i]):
        return False
    nxt = lines[i + 1].strip() if i + 1 < len(lines) else ""
    return bool(nxt == "IN" or LINE_COUNT.match(nxt) or EDIT_SUMMARY.match(nxt)
                or any_header(nxt))

# ------------------------------------------------------------ 敘述文字判準

CJK_RANGES = (
    (0x3000, 0x303F),   # CJK 標點
    (0x3400, 0x4DBF),   # 漢字擴展 A
    (0x4E00, 0x9FFF),   # 漢字
    (0xFF00, 0xFFEF),   # 全形
)
SENTENCE_END = "。！？：；、」』）—…"

CJK_RATIO_MIN = 0.35            # 中文字佔比
SOFT_EXEMPT_RATIO = 0.55        # 中文佔比高於此值就不採信 SOFT 簽名
DIGIT_RATIO_MAX = 0.22          # 數字佔比：機器輸出是數字堆
SHORT_LINE_MAX = 10             # 這麼短的行要收在句末標點才算敘述
CJK_MIN_WHEN_SENTENCE_END = 4

# 行首記號：清單、標籤、分隔線、路徑、命令、程式碼
LEADING_JUNK = set(r"[]|>#*=-+`$%/" + BACKSLASH + r"(){}<~@!;:,.'" + '"')
# 行首全大寫代號＝程式印的標籤（SAVED / DONE / GATE / POOLED…）
LEADING_TAG = re.compile(r"^[A-Z][A-Z0-9_]{2,}\b")
# 行首是函式呼叫＝程式碼（P("…") / print(… / foo.bar(…）
LEADING_CALL = re.compile(r"^[A-Za-z_][A-Za-z0-9_.]*\s*\(")

# 結構性簽名：出現就一定不是敘述文字
HARD_SIGNATURE = re.compile(
    r"->|=>"                                # 箭頭：判定結果／對應
    r"|/[A-Za-z_][\w.\-]*/"                 # 路徑（反斜線路徑另在程式碼裡查）
    r"|^\S+\s*=\s*\S"                       # 賦值／鍵值
    r"|^\d+[-:]"                            # grep -n 的行號前綴（-C 的脈絡行用 `-`）
    r"|^\d{4}-\d{2}-\d{2}\b"                # 日期開頭＝git log／檔案列表
    r"|^[0-9a-f]{7,40}\s"                   # git log 的 commit hash 開頭
    r"|^\d+\.\s+\*\*"                       # 「26. **題材改制**…」＝被 cat 出來的文件條目
    r"|^[a-z][a-z0-9_]*:\s"                 # 「description: …」＝被 cat 出來的 YAML frontmatter
    r"|^[A-Za-z_]\w*\s+\d+\s*[:：]"         # 「loop 0:」這種進度標籤
    r"|\[[^\]]*\]"                          # ASCII 方括號＝陣列／標籤，敘述用「」
    r"|[:：]\s*[-+]?\d[\d.,%]*\s*$"         # 「標籤: 數字」結尾
    r"|\S {3,}\S"                           # 行內多重空白＝欄位對齊的表頭／資料列
    r"|\d+\s*/\s*\d+\s*$"                   # 「202 / 241」比值結尾
)
# 註：曾試過把「2192..2314」這種範圍表示法也列為機器簽名，撤掉了——敘述句
# 本來就會引數字範圍（「主要筆劃在 y 2192..2314…。看那一段。」），代價大於收益。
# 詞彙性簽名：只在中文佔比低時採信
SOFT_SIGNATURE = re.compile(
    r"\bp\d{2}\b"                           # p50 / p90 / p99 分位數
    r"|\b(max|min|mean|PASS|FAIL|DONE|OK|WARNING|Error|Traceback)\b"
    r"|\.(py|png|txt|log|blend|fbx|uasset|json|md|ini|bat|ps1|npz)\b"
)


def _is_cjk(ch):
    o = ord(ch)
    return any(lo <= o <= hi for lo, hi in CJK_RANGES)


def looks_like_prose(line):
    """這一行可不可以當成一段敘述文字的開頭。"""
    if not line or line[:1] in " \t":            # 縮排＝輸出或程式碼
        return False
    s = line.strip()
    if len(s) < 4:
        return False
    # 例外：行首 ** 是粗體，不是項目符號——本語料的敘述常以「**驗證**：…」開頭。
    if s[0] in LEADING_JUNK and not s.startswith("**"):
        return False
    if LEADING_TAG.match(s) or LEADING_CALL.match(s):
        return False
    if HARD_SIGNATURE.search(s) or BACKSLASH in s:   # 反斜線＝Windows 路徑
        return False

    body = [c for c in s if not c.isspace()]
    n_cjk = sum(1 for c in body if _is_cjk(c))
    if n_cjk == 0:
        return False
    cjk_ratio = n_cjk / len(body)

    if cjk_ratio < SOFT_EXEMPT_RATIO:
        # 中文佔比不高時才採信詞彙簽名與數字密度：整句中文的敘述本來就會夾
        # 「p90 3.00→3.22」「全 PASS」這種數字與判定詞，不能因此被丟掉。
        if SOFT_SIGNATURE.search(s):
            return False
        if sum(1 for c in body if c.isdigit()) / len(body) >= DIGIT_RATIO_MAX:
            return False

    if len(body) <= SHORT_LINE_MAX:              # 「FBX 已出」這種單行回報
        return s[-1] in SENTENCE_END
    if cjk_ratio >= CJK_RATIO_MIN:
        return True
    # 中文佔比低但收在句末標點（例：「…備份在 masters/xxx。」）
    return s[-1] in SENTENCE_END and n_cjk >= CJK_MIN_WHEN_SENTENCE_END


# ------------------------------------------------------------ 主要解析

def extract(lines, report=None):
    kept, i, n = [], 0, len(lines)
    while i < n:
        close = XML_BLOCKS.get(lines[i].strip())
        if close:
            start = i
            i += 1
            while i < n and lines[i].strip() != close:
                i += 1
            i = min(n, i + 1)                     # 連結束標籤一起吃掉
            if report is not None:
                report.append((start + 1, i, "harness",
                               lines[i] if i < n else "<EOF>"))
            continue

        web = WEB_HDR.match(lines[i])
        if web:
            start = i
            i += 1
            if web.group(1) == "Fetch":
                if i < n and WEB_FETCHED.match(lines[i]):
                    i += 1                        # 抓取內容沒有進匯出檔，只有這兩行
            else:
                while i < n and not any_header(lines[i]):
                    end = WEB_SEARCH_END.match(lines[i])
                    i += 1
                    if end:                       # REMINDER 那行就是結束標記
                        break
            if report is not None:
                report.append((start + 1, i, "Web" + web.group(1),
                               lines[i] if i < n else "<EOF>"))
            continue

        m = TOOL_HDR.match(lines[i])
        if not m:
            kept.append(lines[i])
            i += 1
            continue

        tool, start = m.group(1), i
        i += 1

        if tool in ("Edit", "MultiEdit", "Update"):
            if i < n and EDIT_SUMMARY.match(lines[i]):
                i += 1                            # 只有一行「Added N lines」摘要

        elif tool in ("Grep", "Glob"):
            if i < n and RESULT_COUNT.match(lines[i]):
                i += 1                            # 只有一行「59 lines of output」摘要

        elif tool in ("Write", "Read", "NotebookEdit"):
            lc = LINE_COUNT.match(lines[i]) if i < n else None
            if lc:
                i += 1
                stop = min(n, i + int(lc.group(1)))
                while i < stop:
                    # 自報行數是邊界提示、實測會差一行；看到下一個工具區塊就收手
                    if starts_tool_block(lines, i):
                        break
                    i += 1

        else:                                     # Bash / PowerShell
            while i < n and lines[i].strip() != "OUT" and not any_header(lines[i]):
                i += 1                            # IN 區：結構性，無條件跳到 OUT
            if i < n and lines[i].strip() == "OUT":
                i += 1
                while (i < n and not any_header(lines[i])
                       and not looks_like_prose(lines[i])):
                    i += 1                        # OUT 區：唯一靠判斷的邊界

        if report is not None:
            report.append((start + 1, i, tool, lines[i] if i < n else "<EOF>"))
    return kept


# 夾在敘述段落裡、但不是文字的雜訊：貼圖附件標記與匯出介面殘跡
UI_NOISE = re.compile(
    r"^([\w.\-]+\.(png|jpg|jpeg|gif|webp)"      # 附件檔名
    r"|\d+\s*[×x]\s*\d+"                        # 附件尺寸
    r"|Show (less|more)"                        # 折疊按鈕
    r"|Shell cwd was reset to .*"
    r"|Sources: .*[·、].*"              # 搜尋工具產生的出處腳註
    r"|.{0,40}safeguards flagged this message.*"  # 平台安全提示
    r"|Details: `\[\w+\]`)$", re.IGNORECASE)


def squeeze_blanks(lines):
    out, blank = [], False
    for ln in lines:
        if UI_NOISE.match(ln.strip()):
            continue
        if ln.strip():
            out.append(ln.rstrip())
            blank = False
        elif not blank and out:
            out.append("")
            blank = True
    while out and not out[-1]:
        out.pop()
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description="抽出對話匯出檔中的自然語言")
    ap.add_argument("src", help="輸入的匯出檔")
    ap.add_argument("-o", "--out", help="輸出檔（預設 <輸入檔>.prose.txt）")
    ap.add_argument("--report", action="store_true",
                    help="把每一個切點印到 stdout 供人工複核")
    a = ap.parse_args(argv)

    raw = io.open(a.src, encoding="utf-8-sig", errors="replace").read()
    lines = raw.replace("\r\n", "\n").replace("\r", "\n").split("\n")

    report = [] if a.report else None
    kept = squeeze_blanks(extract(lines, report))

    dst = a.out or (os.path.splitext(a.src)[0] + ".prose.txt")
    io.open(dst, "w", encoding="utf-8", newline="\r\n").write("\n".join(kept) + "\n")

    if report:
        w = io.open(sys.stdout.fileno(), "w", encoding="utf-8",
                    errors="replace", closefd=False)
        for s, e, tool, nxt in report:
            w.write("L%5d-%-5d %-11s 續接 L%d: %s\n" % (s, e, tool, e + 1, nxt[:70]))
        w.flush()

    sys.stderr.write(
        "%s: %d 行 -> %d 行（去除 %d 行，%.1f%%）\n輸出：%s\n"
        % (a.src, len(lines), len(kept), len(lines) - len(kept),
           100 * (1 - len(kept) / max(1, len(lines))), dst))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
