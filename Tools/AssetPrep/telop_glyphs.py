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
# 2026-08-29 全面重畫（user：「這些字可以盡量做到這個畫質、字體大小下最能辨識的程度嗎」）。
# 這個尺寸的可辨識度有四條硬規則，第一版四條全沒做滿：
#   ① **把格子用滿**：第一版只用 8 欄裡的中間 6 欄 ⇒ 白白丟掉三分之一的解析度。
#      現在筆劃盡量頂到 col0/col7、row0/row8（字距靠 advance 的 1px 保證，不靠留白）。
#   ② **筆劃之間至少留一個空像素**：這個尺寸的頭號敵人是筆劃黏在一起糊成一塊。
#   ③ **能直就不斜**：8px 上的斜線會變成階梯、讀成雜訊。古典日文點陣字把很多斜筆
#      拉直就是這個道理（保留的斜筆只有那些「不斜就不成字」的：ノ／イ／タ／シ／ツ）。
#   ④ **讓「區別特徵」最粗壯**：每個字先問「它跟最像的那個字差在哪」，然後把那一點畫滿。
#      ナ／オ＝オ 多一條直落到底的豎＋鉤；ツ／シ＝長筆方向相反（ツ 右上→左下、
#      シ 左下→右上）且短筆一個直立一個橫躺；マ 不畫第二劃（畫了必讀成ス）。
GLYPHS = {
    "na": (  # ナ：全幅の横棒＋右寄りを貫いて左下へ流れる縦画
        ".....#..",
        "########",
        ".....#..",
        ".....#..",
        "....#...",
        "....#...",
        "...#....",
        "..#.....",
        ".#......",
    ),
    "tsu": (  # ツ：立った短画二本＋**右上から左下へ落ちる**長画（シ とは逆）
        ".#..#..#",
        "..#..#.#",
        ".......#",
        "......#.",
        "......#.",
        ".....#..",
        "....#...",
        "..##....",
        ".#......",
    ),
    "ma": (  # マ ※**使用しない**（08-29、五回まわして断念）。マ と ス は 8×9 では構造が
        # 完全に同じ（横棒＋左下斜画＋右下への分岐）で、差は比例だけ——第二画が短いか長いか。
        # 2px 対 4px の差はこの解像度の識別閾を下回る。描けば ス、描かなければ フ。
        # **工学的な正解は字を直すことではなく、その字を要らない語に替えること**（→ エンニチ）。
        "........",
        "#######.",
        "......#.",
        ".....#..",
        "...###..",
        "..#...#.",
        ".#.....#",
        "........",
        "........",
    ),
    "ri": (  # リ：短い左画＋長い右画、右画は下で左へ跳ねる
        ".#....#.",
        ".#....#.",
        ".#....#.",
        ".#....#.",
        ".#....#.",
        "......#.",
        "......#.",
        ".....#..",
        "..###...",
    ),
    "ta": (  # タ：勹の枠＋中を貫く丿
        "...####.",
        "..#...#.",
        ".#....#.",
        "#####.#.",
        "....#.#.",
        "...#..#.",
        "..#..#..",
        ".#..#...",
        "#..#....",
    ),
    "i": (  # イ：右寄りの縦画＋左下へ抜ける丿
        ".....#..",
        "....##..",
        "...#.#..",
        "..#..#..",
        ".#...#..",
        "#....#..",
        ".....#..",
        ".....#..",
        ".....#..",
    ),
    "ko": (  # コ：上下の横棒＋右の縦画（左は開く）
        "........",
        "#######.",
        "......#.",
        "......#.",
        "......#.",
        "......#.",
        "......#.",
        "#######.",
        "........",
    ),
    "mi": (  # ミ：右下がりの短画を三段、等間隔
        "######..",
        "......#.",
        "........",
        "######..",
        "......#.",
        "........",
        "######..",
        "......#.",
        "........",
    ),
    "shi": (  # シ：横に寝た短画二本が**左側**に縦並び＋**左下から右上へ昇る**長画
        ".##.....",
        "...#....",
        ".......#",
        ".##...#.",
        "...#.#..",
        "....#...",
        "...#....",
        "..#.....",
        ".#......",
    ),
    "ho": (  # ホ：全幅の横棒＋貫く縦画＋下で開く二本の足
        "....#...",
        "########",
        "....#...",
        "....#...",
        "...#.#..",
        "..#.#.#.",
        ".#..#..#",
        "....#...",
        "....#...",
    ),
    "mo": (  # モ：短い上棒＋全幅の横棒＋下で右へ跳ねる縦画
        "..#####.",
        "....#...",
        "########",
        "....#...",
        "....#...",
        "....#...",
        "....#...",
        "....#...",
        "....####",
    ),
    "no": (  # ノ：右上から左下へ一本、格子いっぱいに
        ".......#",
        "......#.",
        "......#.",
        ".....#..",
        "....#...",
        "...#....",
        "..#.....",
        ".#......",
        "#.......",
    ),
    "o": (  # オ：ナ＋**下まで真っ直ぐ落ちて左へ跳ねる縦画**（ナ との差はこの一点）
        "....#...",
        "########",
        "....#...",
        "...##...",
        "..#.#...",
        ".#..#...",
        "#...#...",
        "....#...",
        "...##...",
    ),
    "to": (  # ト：縦画＋中ほどから右下へ短い斜め
        "..#.....",
        "..#.....",
        "..#.....",
        "..##....",
        "..#.#...",
        "..#..#..",
        "..#...#.",
        "..#.....",
        "..#.....",
    ),
    "e": (  # エ：上下の横棒＋中央の縦画（ニ との差はこの縦画一本）
        "........",
        "########",
        "....#...",
        "....#...",
        "....#...",
        "....#...",
        "....#...",
        "########",
        "........",
    ),
    "n": (  # ン：短画は一本だけ＋**左下から右上へ昇る**長画（ソ は逆向き、シ は短画二本）
        ".##.....",
        "...#....",
        ".......#",
        "......#.",
        ".....#..",
        "....#...",
        "...#....",
        "..#.....",
        ".#......",
    ),
    "ni": (  # ニ：横棒二本だけ（上が短い）
        "........",
        "........",
        ".######.",
        "........",
        "........",
        "........",
        "########",
        "........",
        "........",
    ),
    "chi": (  # チ：短い斜め＋全幅の横棒＋下で左へ跳ねる縦画
        "....###.",
        "...#....",
        "########",
        "....#...",
        "....#...",
        "....#...",
        "....#...",
        "...#....",
        "..##....",
    ),
    "sei": (  # 生：丿 を 2px 伸ばして「主」に見えないようにする
        "......#.",
        ".....#..",
        "..#####.",
        "....#...",
        "....#...",
        ".######.",
        "....#...",
        "....#...",
        "########",
    ),
}

# **この尺で信頼できない字**（註解ではなくデータで持つ）。08-29：マ は ス と 8×9 で
# 構造が同じで比例しか違わないため、五回描き直しても分離できなかった。
# 註解に「使わない」と書くだけでは、いつか誰かが有効にして**静かに**壊す
# ——この専案が何度も踏んだ「註解は腐るのに誰も見ていない」の同族。だから下の
# assert が焼く前に落とす。字形自体は将来のために残す。
UNRELIABLE = {"ma"}

ORDER = list(GLYPHS.keys())
IDX = {k: i for i, k in enumerate(ORDER)}

# ── テロップ本文 ────────────────────────────────────────────────────
# **分鏡ごとではない**（2026-08-29 三修）：テロップは「この報道」に属するもので、
# 一つ一つの鏡頭に属するものではない。ここは**使える文言の一覧**であって、
# どれをいつ出すかは C++ 側（DrawBroadcastChrome）が段全体の時間軸で決める。
#
# 見出しブロック＝「生」。本文＝片假名（この尺で読める唯一の日本語文字）。
#
# **「ホリモノ」は現行の編成では出さない**（user 判決「為什麼新聞標題會出現刺青？
# 感覺有點生硬」——正しい）：この報道が存在する理由は祭を報じることで、彫物は
# たまたま画面にいるだけ。**たまたま画面にいることが、羨ましさの根拠そのもの**
# ——誰も売り込んでいないのに、あの男たちには在る。字幕が指させば広告になるし、
# 力士の代わりに結論を言ってしまう。文言自体は残す（いつか要るかもしれない）。
CAPTIONS = [
    ("ennichi",      ["e", "n", "ni", "chi"]),                        # エンニチ（縁日）
    ("taiko",        ["ta", "i", "ko"]),                             # タイコ（太鼓）
    ("mikoshi",      ["mi", "ko", "shi"]),                           # ミコシ（神輿）
    ("horimono",     ["ho", "ri", "mo", "no"]),                      # ホリモノ ※未使用
]

ROOT = r"C:\games\Unreal Engine\nice_ink"
HDR = os.path.join(ROOT, "Source", "NiceInk", "Private", "NiceInkTvTelopData.h")
PREVIEW = os.path.join(ROOT, "Saved", "TvFilm", "telop_preview.png")


# ── 閘門：使えない字を含む文言があれば、焼かずに落とす ────────────────────
for _name, _seq in CAPTIONS:
    _bad = sorted(set(_seq) & UNRELIABLE)
    if _bad:
        raise SystemExit(
            "TELOP GATE: 文言 %r が信頼できない字 %s を使っている。"
            "この尺では読み違えられるので、字を直すのではなく**文言を替えること**。"
            % (_name, _bad))


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
    lines.append("\t// **使える文言の一覧**（分鏡別ではない）。どれをいつ出すかは C++ 側が決める。")
    lines.append("\tinline constexpr int32 SeiIndex = %d;" % IDX["sei"])
    lines.append("\tinline constexpr int32 NumTexts = %d;" % len(CAPTIONS))
    lines.append("\tenum { %s };" % ", ".join(
        "Txt_%s = %d" % (n[0].upper() + n[1:], i)
        for i, (n, _) in enumerate(CAPTIONS)))
    lines.append("\tinline constexpr int32 MaxText = %d;"
                 % max(len(c[1]) for c in CAPTIONS))
    lines.append("\tinline constexpr int32 TextLen[%d] = { %s };"
                 % (len(CAPTIONS), ", ".join(str(len(c[1])) for c in CAPTIONS)))
    lines.append("\tinline constexpr int32 Text[%d][MaxText] = {" % len(CAPTIONS))
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
