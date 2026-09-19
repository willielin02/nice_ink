# -*- coding: utf-8 -*-
"""把「別款派對遊戲的踢人畫面」排成一張對照表，最後一格放我們自己的（2026-09-19）。

user：「請把這些遊戲的踢人畫面整理成照片給我看」。
規矩（這一節是這支存在的理由）：
  1. **每一張都要是真的抓下來、而且我自己看過確認它拍到踢人控制**。抓不到就留白寫「沒有實機圖」，
     不准拿同一款遊戲的宣傳圖或別的畫面充數（09-19 一度抓到 PEAK 的宣傳圖，已刪）。
  2. 沒有影像佐證的款，只放它的逐字描述與來源，並標成「文字來源，非實機圖」。
  3. 最後一格是我們自己的畫面——**與參考圖並排由 user 裁決**是這個專案唯一有效的 UI 閘門
     （project_ui_meccha_postmortem）。

Usage: python -X utf8 kick_ref_sheet.py <out.png>
圖的來源資料夾＝Saved/UiMock/ref/kick/（curl 抓的，僅供內部比對參考）。
"""
import os, sys
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
SRC = os.path.join(ROOT, "Saved", "UiMock", "ref", "kick")
SHOT = os.path.join(ROOT, "Saved", "Screenshots", "WindowsEditor")

CELL_W = 880
PAD = 18
CAP_H = 64
BG = (24, 24, 26)
CAP_BG = (38, 38, 42)
FG = (238, 238, 238)
DIM = (168, 168, 172)


def font(size, bold=False):
    for p in (r"C:\Windows\Fonts\msjhbd.ttc" if bold else r"C:\Windows\Fonts\msjh.ttc",
              r"C:\Windows\Fonts\msjh.ttc", r"C:\Windows\Fonts\segoeui.ttf"):
        try:
            return ImageFont.truetype(p, size)
        except Exception:
            continue
    return ImageFont.load_default()


# (檔名, 標題, 說明)。檔名 None ＝ 找不到實機圖，只放文字。
CELLS = [
    (os.path.join(SRC, "cw_kick.jpg"), "Content Warning — 暫停選單的玩家卡",
     "卡內由上而下＝頭像｜VOL 音量滑桿｜整條紅色 KICK。常駐、無確認。\n"
     "那張卡本來就是「我對這個人的控制台」——所以踢人放在裡面不突兀。"),
    (os.path.join(SRC, "cw_mutemenu.jpg"), "Content Warning — 暫停選單全頁",
     "動詞清單在左下（RESUME／INVITE FRIENDS／SETTINGS／QUIT）、玩家清單在右上。\n"
     "非房主時那張卡只有頭像＋名字＋音量，沒有 KICK。"),
    (os.path.join(SRC, "jackbox_mod.jpg"), "Jackbox Party Pack 9+ — moderation 頁（另一台裝置）",
     "每人一張卡：名字在上、紅色 KICK? 藥丸鈕在下；已踢的人顯示 KICKED。\n"
     "分頁標籤的圖示是一隻靴子。常駐、無確認。"),
    (os.path.join(SRC, "amongus_hostmenu_v.png"), "Among Us — 點玩家跳出的房主面板",
     "點角色旁的靴子徽章 → 彈出小面板：名字＋KICK／BAN／REPORT 三顆鈕。\n"
     "不是每列一顆鈕，是點了才出現的選單。"),
    (os.path.join(SRC, "gartic_lobby_v.png"), "Gartic Phone — 大廳玩家清單（注意：沒拍到踢人）",
     "這張是單人房，所以那個 X 不會出現。放它是因為清單結構值得看：\n"
     "標題 PLAYERS 1/14、人數下拉、一列一席＋EMPTY 空席列、房主右側王冠。"),
    (None, "PEAK ／ Lethal Company — 找不到實機圖",
     "PEAK（wiki 逐字）：「Hosts will now see a little boot next to each audio slider in the\n"
     "pause menu.」＝暫停選單、每人音量滑桿旁一隻靴子、常駐、無確認。\n"
     "Lethal Company（兩個來源）：ESC 選單 hover 玩家的 Steam 名字才出現一個 X 方塊，並且有二次確認。"),
    (os.path.join(SHOT, "khov_host00000.png"), "◆ 我們現在的版本（2026-09-19）",
     "卡內只有身分（臉｜名字｜錢），踢出在卡外、hover 那一列才出現、整列同時亮起。\n"
     "無確認。圖中第 2 列是強制 hover 拍的。"),
]


def main(out):
    f_title = font(26, True)
    f_body = font(19)
    cells = []
    for path, title, body in CELLS:
        im = None
        if path and os.path.exists(path):
            im = Image.open(path).convert("RGB")
            h = max(1, int(im.height * CELL_W / im.width))
            im = im.resize((CELL_W, h), Image.LANCZOS)
        lines = body.count("\n") + 1
        cap = CAP_H + lines * 26
        cells.append((im, title, body, cap))

    # 兩欄
    col_h = [0, 0]
    place = []
    for idx, (im, title, body, cap) in enumerate(cells):
        c = 0 if col_h[0] <= col_h[1] else 1
        y = col_h[c]
        place.append((c, y))
        col_h[c] += cap + (im.height if im else 40) + PAD * 2

    W = PAD + (CELL_W + PAD) * 2
    H = max(col_h) + PAD
    sheet = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(sheet)

    for (im, title, body, cap), (c, y) in zip(cells, place):
        x = PAD + c * (CELL_W + PAD)
        ih = im.height if im else 40
        d.rectangle([x, y, x + CELL_W, y + cap + ih + PAD], fill=CAP_BG)
        d.text((x + 14, y + 12), title, font=f_title, fill=FG)
        ty = y + 12 + 34
        for ln in body.split("\n"):
            d.text((x + 14, ty), ln, font=f_body, fill=DIM)
            ty += 26
        if im:
            sheet.paste(im, (x, y + cap))
        else:
            d.text((x + 14, y + cap + 6), "（無實機圖＝不放圖，不拿別的畫面充數）", font=f_body, fill=(200, 120, 110))

    sheet.save(out)
    print(f"{out}  {sheet.size[0]}x{sheet.size[1]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(SRC, "kick_ref_sheet.png")))
