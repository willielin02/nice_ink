# 整場局數分布模擬（一局＝一次畫＋猜）
#
# 回合引擎：受害者猜對(p)→作者換上、杯歸零；猜錯→杯+1；一任內第3杯→終局。
# p 的三數模型（SPEC 判讀校準三數）：
#   p = e_t*ACC_HARD + f*ACC_BAIT + (1-e_t-f)*min(CAP, b*(1+style*reveals))
#   e_t 硬證據落地率（抓現行/標記）、f 被餌騙走率、b=1/(N-1) 裸猜、
#   reveals=本場已揭曉的真作者數（每次猜錯+1＝帶答案的畫風樣本）
# 朋友房＝裸猜底率乘 FRIEND_MULT（畫風指紋+交情 meta 的軟知識層）。
# wall-clock（粗估）：回合＝迷宮時間(隨杯數檔) + 巡禮(N-1)*20s + 指認/結算 25s
import random
import sys

ACC_HARD = 0.9
ACC_BAIT = 0.02
CAP = 0.9
TOUR_PER_WORK = 20.0
RESOLVE_T = 25.0
# 迷宮中位時間（sim_playtime median 原型，杯0/1/2 檔）——粗估用
MAZE_T = {0: 103.0, 1: 137.0, 2: 188.0}


def sim_game(N, e_t, f, style, friend_mult, rng):
    rounds = 0
    reveals = 0
    t = 0.0
    cups = 0
    while True:
        b = min(CAP, (1.0 / (N - 1)) * friend_mult * (1.0 + style * reveals))
        p = e_t * ACC_HARD + f * ACC_BAIT + max(0.0, 1 - e_t - f) * b
        rounds += 1
        t += MAZE_T[min(cups, 2)] + (N - 1) * TOUR_PER_WORK + RESOLVE_T
        if rng.random() < p:
            cups = 0          # 猜對：作者上座，新任開始
        else:
            reveals += 1      # 猜錯：真作者揭曉（畫風樣本+1）
            cups += 1
            if cups >= 3:
                return rounds, t


def pct(a, q):
    a = sorted(a)
    return a[min(len(a) - 1, int(q * (len(a) - 1)))]


if __name__ == "__main__":
    n_games = int(sys.argv[1]) if len(sys.argv) > 1 else 20000
    scenarios = [
        ("陌生人房 e_t=.20 f=.10", 0.20, 0.10, 0.12, 1.0),
        ("陌生人房 e_t=.35 f=.10", 0.35, 0.10, 0.12, 1.0),
        ("陌生人房 e_t=.50 f=.10", 0.50, 0.10, 0.12, 1.0),
        ("朋友房   e_t=.35 f=.15", 0.35, 0.15, 0.20, 2.2),
    ]
    for name, e_t, f, style, fm in scenarios:
        print("== %s ==" % name)
        for N in (4, 5, 6):
            rng = random.Random(1234 + N)
            rr, tt = [], []
            first_out = 0
            for _ in range(n_games):
                r, t = sim_game(N, e_t, f, style, fm, rng)
                rr.append(r)
                tt.append(t / 60.0)
                first_out += 1 if r == 3 else 0
            works = [(r * (N - 1)) for r in rr]
            print("  N=%d 局數 p10=%2d p50=%2d p90=%2d p99=%3d | 首任即終局=%4.1f%% | 傑作總數p50=%3d | 粗估時長 p50=%4.1f min p90=%4.1f min"
                  % (N, pct(rr, .1), pct(rr, .5), pct(rr, .9), pct(rr, .99),
                     100.0 * first_out / n_games, pct(works, .5), pct(tt, .5), pct(tt, .9)))
