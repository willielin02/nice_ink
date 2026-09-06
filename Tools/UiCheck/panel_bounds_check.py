"""房碼面板包字閘門（2026-09-06；user 四張截圖字腳掉出面板、我看了說「乾淨」）。
量 user 看得到的那個量：字不能碰到面板的邊。做法不用「找字的外框」（面板外是白障子，亮像素分不出字與牆），
改成檢查**面板內緣的一圈**：面板矩形最外 4px 的環帶裡不得有亮像素（>200）。字掉出去＝字必然穿過這圈。
面板左上與寬度是版面常數（右邊距 24、寬 320、頂 60，×UiScale＝H/1080）；高度沿左內距欄往下掃暗像素得到。
Usage: python -X utf8 panel_bounds_check.py <screenshot.png> [...]
"""
import sys
from PIL import Image
import numpy as np


def check(path):
    im = np.asarray(Image.open(path).convert("L")).astype(np.int32)
    H, W = im.shape
    s = H / 1080.0
    px0 = int(round(W - 24 * s - 320 * s))
    px1 = int(round(W - 24 * s))
    py0 = int(round(60 * s))
    col = im[:, px0 + 6]
    y = py0 + 4
    if col[y] >= 90:
        return f"{path}: NO PANEL at expected ({px0},{py0}) (PIE 無房碼？) lum={col[y]}"
    while y < H - 1 and col[y] < 90:
        y += 1
    py1 = y - 1
    panel = im[py0:py1 + 1, px0:px1 + 1].copy()
    ring = int(round(4 * s))
    # 圓角（半徑 8×s）的四個角落是透明的，會露出背後的白障子＝不是字；從環帶裡挖掉
    r = int(round(8 * s)) + 2
    panel[:r, :r] = 0; panel[:r, -r:] = 0; panel[-r:, :r] = 0; panel[-r:, -r:] = 0
    top = panel[:ring, :]
    bottom = panel[-ring:, :]
    left = panel[:, :ring]
    right = panel[:, -ring:]
    hits = {"top": int((top > 200).sum()), "bottom": int((bottom > 200).sum()),
            "left": int((left > 200).sum()), "right": int((right > 200).sum())}
    # 面板內有沒有字（避免「空面板」假通過）
    inner = panel[ring:-ring, ring:-ring]
    text_px = int((inner > 200).sum())
    ok = all(v == 0 for v in hits.values()) and text_px > 50
    verdict = "PASS" if ok else "FAIL"
    return f"{path}: {verdict}  panel y[{py0},{py1}] x[{px0},{px1}] h={py1 - py0}  edge-hits={hits}  text_px={text_px}"


if __name__ == "__main__":
    bad = 0
    for p in sys.argv[1:]:
        r = check(p)
        print(r)
        bad += ("FAIL" in r)
    sys.exit(1 if bad else 0)
