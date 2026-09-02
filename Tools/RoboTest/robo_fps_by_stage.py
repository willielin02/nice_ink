# -*- coding: utf-8 -*-
"""每個 STAGE 的實際幀率（離線讀 log，不開引擎、一秒）。

**用途：分辨「我的改動壞了」和「這台機器當時被餓死了」。**
directdraw 裡有一整族牆鐘×頻率敏感的契約（superfast 的針數、cruise 的 tipSpd、
sweeping 的排數、movement gate 的殘留針）——它們在幀率塌掉時會集體假 FAIL，
而且**看起來就像真的迴歸**。年鑑的原話：「superfast 混疊成慢爬實錘」。

判讀：正常各段 80~115 fps。**某一段掉到 20~30 而鄰段正常＝那一段被餓死**，
該段的失敗不可歸因於程式改動；重跑並確認該段回到常態才算數。

跑法：  python -X utf8 Tools/RoboTest/robo_fps_by_stage.py [log路徑]
"""
import io
import os
import re
import sys

DEFAULT_LOG = os.path.join(os.path.dirname(__file__), "..", "..",
                           "Saved", "Logs", "NiceInk.log")
PAT = re.compile(
    r"\[(\d{4})\.(\d\d)\.(\d\d)-(\d\d)\.(\d\d)\.(\d\d):(\d\d\d)\]\[\s*(\d+)\].*STAGE -> (\w+)")
SLOW_FPS = 40.0     # 低於此＝可疑
MIN_WALL = 1.2      # 只判「量測段」。契約住在 1.5~3 秒的量測段裡；0.3~0.9 秒的
                    # 過場（切工具、進出鎖、RPC 來回）幀數本來就少，把它們算進來
                    # 會讓工具一直喊狼來了——**會發假警報的閘門等於沒有閘門**。
# 等待狀態不是量測段：PIE 還沒起來／在等相位，本來就沒有幀在跑。
# 把它們算進去會讓工具自己發假警報（閘門不該對不該開火的東西開火）。
WAITING = {"wait_pie", "wait_drawing"}


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_LOG
    rows = []
    for line in io.open(path, encoding="utf-8", errors="ignore"):
        m = PAT.search(line)
        if m:
            t = (int(m.group(4)) * 3600 + int(m.group(5)) * 60 +
                 int(m.group(6)) + int(m.group(7)) / 1000.0)
            rows.append((t, int(m.group(8)), m.group(9)))
    if len(rows) < 2:
        print("no STAGE markers in", path)
        return 2

    print("stage                      wall(s)  frames     fps")
    samples, slow = [], []
    for i in range(1, len(rows)):
        dt = rows[i][0] - rows[i - 1][0]
        df = (rows[i][1] - rows[i - 1][1]) % 1000   # 引擎的幀計數欄是三位循環
        if dt < MIN_WALL:
            continue
        fps = df / dt
        name = rows[i - 1][2]
        if name in WAITING:
            print("%-26s %6.2f  %6d  %6.1f  (等待段，不判)" % (name, dt, df, fps))
            continue
        flag = "  <-- 被餓死？" if fps < SLOW_FPS else ""
        print("%-26s %6.2f  %6d  %6.1f%s" % (name, dt, df, fps, flag))
        samples.append(fps)
        if fps < SLOW_FPS:
            slow.append((name, fps))

    if samples:
        med = sorted(samples)[len(samples) // 2]
        print()
        print("中位幀率 %.1f fps" % med)
        if slow:
            print("**有段落被餓死**：" + "、".join("%s(%.0f fps)" % s for s in slow))
            print("⇒ 這些段落的失敗**不可歸因於程式改動**；重跑並確認它回到中位值才算數。")
        else:
            print("全段落幀率正常 ⇒ 失敗（若有）要當成真的迴歸查。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
