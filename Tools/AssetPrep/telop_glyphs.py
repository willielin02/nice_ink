# 電視新聞テロップ的點陣字（2026-08-29）。
#
# user viewport 判決：「下面的新聞標題太大了，而且裡面的文字也根本看不出來在寫什麼，
# 也完全不像日本字」。三點全中——追記87 的假字塊每個等寬等高、橫劃貫穿整格，
# 實際讀起來是**條碼**不是文字。這支把它換成**真的字**。
#
# 為什麼手工點字而不是拿字型 rasterize：
#   ① 9px 上把向量字型縮下來一定糊——真正的日文點陣字在這個尺寸全是手工調的
#      （MS Gothic 內嵌 bitmap strike 就是為此）。手工不是退路，是這個尺寸的正解。
#   ② 專案的字型全是 OFL 但只有 .uasset（沒有原始 ttf），而且把別人的字型 rasterize
#      進出貨的二進位是授權灰區。手工點的字形不是任何字型的衍生物。
#
# 為什麼是片假名：直線構成 ⇒ 8×9 上能站得住（平假名的曲線與漢字的筆畫密度都不行）；
# 而且不會被誤認成別的文字系統。漢字只收「生」（5 劃，生中継のバッジと見出しブロック）。
#
# 產出：Source/NiceInk/Private/NiceInkTvTelopData.h（C++ 點陣表）
#       Saved/TvFilm/telop_preview.png（自查用：字單體 6× ＋ 實際字串的原尺寸/3×）
# Run: <venv>/python.exe Tools/AssetPrep/telop_glyphs.py
import os

W, H = 8, 9  # 8 幅 × 9 高（advance 9px）。uint8 一列剛好。

# ── 字形（'.'＝空 '#'＝墨）──────────────────────────────────────────────
GLYPHS = {
    "na": (  # ナ
        "........",
        ".######.",
        "....#...",
        "....#...",
        "....#...",
        "...#....",
        "...#....",
        "..#.....",
        ".#......",
    ),
    "tsu": (  # ツ：短い二画＋**右上から左下へ落ちる**長画（シ／ン とはここが逆）
        "........",
        ".#..#..#",
        "..#..#.#",
        "......#.",
        ".....#..",
        "....#...",
        "...#....",
        "..#.....",
        ".#......",
    ),
    "ma": (  # マ：**第二画は落とす**（半角片仮名 ﾏ の形）。
        "........",     # 8px では枝を付けると必ず ス に読める（08-29 preview で三回実証）——
        ".######.",     # 枝の長短では分けられない。ス は第二画が無いと成立しないので、
        "......#.",     # 落とせば誤読の余地そのものが消える。**この尺の正解は簡略化。**
        ".....#..",
        "....#...",
        "...#....",
        "..#.....",
        ".#......",
        "........",
    ),
    "ri": (  # リ
        "........",
        "..#...#.",
        "..#...#.",
        "..#...#.",
        "..#...#.",
        "..#...#.",
        "...#..#.",
        "....##..",
        "........",
    ),
    "ta": (  # タ
        "........",
        "...####.",
        "..#...#.",
        ".#....#.",
        "#####.#.",
        "....#.#.",
        "...#..#.",
        "..#..#..",
        ".#..#...",
    ),
    "i": (  # イ
        "........",
        "....#...",
        "...#....",
        "..#.#...",
        ".#..#...",
        "#...#...",
        "....#...",
        "....#...",
        "....#...",
    ),
    "ko": (  # コ
        "........",
        ".######.",
        "......#.",
        "......#.",
        "......#.",
        "......#.",
        "......#.",
        ".######.",
        "........",
    ),
    "mi": (  # ミ：右下がりの短画を三段
        "........",
        ".#####..",
        "......#.",
        "........",
        ".#####..",
        "......#.",
        "........",
        ".#####..",
        "......#.",
    ),
    "shi": (  # シ：短い二画は**左側**に縦に並び、長画は**左下から右上へ昇る**
        "........",
        ".#......",
        "..#....#",
        "......#.",
        ".#...#..",
        "..#.#...",
        "...#....",
        "..#.....",
        ".#......",
    ),
    "ho": (  # ホ
        "........",
        "....#...",
        ".######.",
        "....#...",
        "...###..",
        "..#.#.#.",
        ".#..#..#",
        "....#...",
        "....#...",
    ),
    "mo": (  # モ
        "........",
        "..####..",
        "....#...",
        ".######.",
        "....#...",
        "....#...",
        "....#...",
        "....##..",
        "......#.",
    ),
    "no": (  # ノ
        "........",
        "......#.",
        ".....#..",
        ".....#..",
        "....#...",
        "...#....",
        "..#.....",
        ".#......",
        "#.......",
    ),
    "o": (  # オ
        "........",
        "....#...",
        ".######.",
        "....#...",
        "...##...",
        "..#.#...",
        ".#..#...",
        "....#...",
        "...#....",
    ),
    "to": (  # ト：縦画＋中ほどから右下へ短い斜め
        "........",
        "..#.....",
        "..#.....",
        "..##....",
        "..#.#...",
        "..#..#..",
        "..#...#.",
        "..#.....",
        "..#.....",
    ),
    "sei": (  # 生
        "........",
        "....#...",
        "..#####.",
        "..#.#...",
        ".######.",
        "....#...",
        "....#...",
        ".######.",
        "........",
    ),
}

ORDER = list(GLYPHS.keys())
IDX = {k: i for i, k in enumerate(ORDER)}

# ── テロップ本文（分鏡ごと）──────────────────────────────────────────
# 見出しブロック＝「生」。本文＝片假名（古い文字発生器の読み＋この尺で読める唯一の選択）。
CAPTIONS = [
    ("yomatsuri", ["na", "tsu", "ma", "tsu", "ri"]),        # ナツマツリ
    ("taiko",     ["ta", "i", "ko"]),                       # タイコ
    ("mikoshi",   ["mi", "ko", "shi"]),                     # ミコシ
    ("reveal",    ["ho", "ri", "mo", "no"]),                # ホリモノ
    ("turn",      ["ma", "tsu", "ri", "no", "o", "to", "ko"]),  # マツリノオトコ
]

ROOT = r"C:\games\Unreal Engine\nice_ink"
HDR = os.path.join(ROOT, "Source", "NiceInk", "Private", "NiceInkTvTelopData.h")
PREVIEW = os.path.join(ROOT, "Saved", "TvFilm", "telop_preview.png")


def rows_to_bytes(rows):
    out = []
    for r in rows:
        assert len(r) == W, "glyph row must be %d wide: %r" % (W, r)
        v = 0
        for c, ch in enumerate(r):
            if ch == "#":
                v |= 1 << (W - 1 - c)
        out.append(v)
    assert len(out) == H
    return out


def emit_header():
    lines = []
    lines.append("// AUTO-GENERATED by Tools/AssetPrep/telop_glyphs.py — DO NOT EDIT")
    lines.append("// 電視新聞テロップの点陣字（8×9、advance 9px）。片假名＋「生」。")
    lines.append("// 手工点字＝この尺では正解（向量字型を 9px に縮めると必ず潰れる）；")
    lines.append("// どの字型の衍生物でもないので授權問題なし。字形を直すのは .py の側。")
    lines.append("#pragma once")
    lines.append("")
    lines.append("#include \"CoreMinimal.h\"")
    lines.append("")
    lines.append("namespace NiceInkTvTelop")
    lines.append("{")
    lines.append("\tinline constexpr int32 GlyphW = %d;" % W)
    lines.append("\tinline constexpr int32 GlyphH = %d;" % H)
    lines.append("\tinline constexpr int32 Num = %d;" % len(ORDER))
    lines.append("")
    lines.append("\t// 行ごとに 1 バイト、bit7 が左端")
    lines.append("\tinline constexpr uint8 Bits[Num][GlyphH] = {")
    for k in ORDER:
        b = rows_to_bytes(GLYPHS[k])
        lines.append("\t\t{ %s }, // %s" % (", ".join("0x%02X" % v for v in b), k))
    lines.append("\t};")
    lines.append("")
    lines.append("\t// 分鏡ごとの本文（字の索引列）。見出しブロックは「生」固定。")
    lines.append("\tinline constexpr int32 SeiIndex = %d;" % IDX["sei"])
    lines.append("\tinline constexpr int32 MaxCaption = %d;"
                 % max(len(c[1]) for c in CAPTIONS))
    lines.append("\tinline constexpr int32 CaptionLen[%d] = { %s };"
                 % (len(CAPTIONS), ", ".join(str(len(c[1])) for c in CAPTIONS)))
    lines.append("\tinline constexpr int32 Caption[%d][MaxCaption] = {" % len(CAPTIONS))
    for name, seq in CAPTIONS:
        pad = list(seq) + ["sei"] * (max(len(c[1]) for c in CAPTIONS) - len(seq))
        lines.append("\t\t{ %s }, // %s：%s" % (", ".join(str(IDX[g]) for g in pad),
                                               name, " ".join(seq)))
    lines.append("\t};")
    lines.append("}")
    with open(HDR, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("HEADER", HDR)


def emit_preview():
    from PIL import Image
    BG = (18, 22, 44)
    INK = (224, 228, 242)
    RED = (176, 44, 36)
    PAD = 4

    # 上段＝字単体 6×（形を直すため）／下段＝実際の帯を原尺・3×・6×
    zoom = 6
    cols = len(ORDER)
    top_w = cols * (W + 2) * zoom + PAD * 2
    top_h = (H + 2) * zoom + PAD * 2
    top = Image.new("RGB", (top_w, top_h), (10, 10, 14))
    for i, k in enumerate(ORDER):
        rows = GLYPHS[k]
        ox = PAD + i * (W + 2) * zoom
        for y in range(H):
            for x in range(W):
                col = INK if rows[y][x] == "#" else (30, 32, 52)
                for dy in range(zoom):
                    for dx in range(zoom):
                        top.putpixel((ox + x * zoom + dx, PAD + y * zoom + dy), col)

    # 帯を実寸で組む（本番と同じ寸法：帯高 12、字は y+2 から）
    BANDH, HEADW, ADV = 12, 11, 9
    bands = []
    for name, seq in CAPTIONS:
        bw = HEADW + 2 + len(seq) * ADV + 3
        im = Image.new("RGB", (bw, BANDH), BG)
        for x in range(HEADW):
            for y in range(BANDH):
                im.putpixel((x, y), RED)
        def blit(gi, ox, oy, col):
            rows = GLYPHS[ORDER[gi]]
            for y in range(H):
                for x in range(W):
                    if rows[y][x] == "#" and 0 <= ox + x < im.width and 0 <= oy + y < BANDH:
                        im.putpixel((ox + x, oy + y), col)
        blit(IDX["sei"], 2, 2, INK)
        for j, g in enumerate(seq):
            blit(IDX[g], HEADW + 2 + j * ADV, 2, INK)
        bands.append((name, im))

    bw_max = max(b.width for _, b in bands)
    bot_h = sum(b.height * z + PAD for _, b in bands for z in (1,)) * 0  # placeholder
    rows_out = []
    for name, im in bands:
        for z in (1, 3, 6):
            rows_out.append(im.resize((im.width * z, im.height * z), Image.NEAREST))
    bot_w = max(r.width for r in rows_out) + PAD * 2
    bot_h = sum(r.height + PAD for r in rows_out) + PAD
    bot = Image.new("RGB", (bot_w, bot_h), (10, 10, 14))
    y = PAD
    for r in rows_out:
        bot.paste(r, (PAD, y))
        y += r.height + PAD

    out = Image.new("RGB", (max(top.width, bot.width), top.height + bot.height), (10, 10, 14))
    out.paste(top, (0, 0))
    out.paste(bot, (0, top.height))
    os.makedirs(os.path.dirname(PREVIEW), exist_ok=True)
    out.save(PREVIEW)
    print("PREVIEW", PREVIEW, out.size)


emit_header()
emit_preview()
print("TELOP_GLYPHS_DONE")
