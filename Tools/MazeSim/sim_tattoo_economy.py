# 跨場刺青累積與雷射經濟蒙地卡羅（4/5/6 人局）
#
# 接線：
#   昏睡時間 = sim_playtime.simulate_run（同構生成器、陷阱數 = N-1、杯數檔 ring7/8/9）
#   局數引擎 = sim_gamelength 的判讀三數模型（p = e_t*hard + f*bait + 裸猜*畫風增值）
#   實作值   = START_CASH 10000（NiceInkPlayerState/SaveGame）、雷射 2000/次×3 = 6000/幅
#              （NiceInkGameMode LaserCostPerPass）、輸家整包歸零平分（GameMode:760）
#
# 面積模型（最軟的常數，全部攤在這裡，AREA_SCALE 掃靈敏度）：
#   足跡速率 RATE_FP ≈ 筆尖 8cm/s × 筆寬 0.38cm × 佔空比 0.5 × 足跡/墨跡 2.5 ≈ 3.8 cm²/s
#   野心分布：小品/中品/大作 = 35%/45%/20%，中位足跡 120/300/650 cm²（A5=315 cm²）
#   實際足跡 = min(野心, RATE_FP × 昏睡秒 × U(0.7,1))
# 挑選模型（賭注加權）：杯0 猜溫和小品（最小2幅擇一）、杯1 中位、杯2 信心最大化=最大幅
# 畫布：PRIME=11000 cm²（腹胸臉大腿前側等高價值面）、全身可畫 TOTAL≈24000 cm²
#   飽和判定用「永久足跡累積 / PRIME」線性近似（作畫者會避開既有刺青 → 前期近線性）
#   30%=讀感退化、50%=判讀危機、70%=無法再玩
import math
import random
import sys

import render_maze_map as M
import sim_playtime as SP

A5 = 315.0
RATE_FP = 3.8
AMBITION = [(0.35, 120.0, 0.35), (0.45, 300.0, 0.30), (0.20, 650.0, 0.30)]
PRIME = 11000.0
TOTAL_SKIN = 24000.0
LASER_FULL = 6000
START_CASH = 10000
ACC_HARD = 0.9
ACC_BAIT = 0.02
CAP = 0.9
THRESHOLDS = (0.30, 0.50, 0.70)

ARCHETYPES = [(0.2, 0.3), (0.5, 0.5), (0.8, 0.2)]  # (p_keep, 權重)


def build_time_pools(traps, n_seeds=30, runs_per=2):
    pools = {}
    for cup in range(3):
        p = M.params_cup(cup)
        samples = []
        rng = random.Random(777 + cup * 13 + traps)
        for i in range(n_seeds):
            L = M.generate(p, 913 * (i + 1) + 7, traps)
            if not L:
                continue
            for p_keep, w in ARCHETYPES:
                for _ in range(max(1, round(runs_per * 3 * w))):
                    t, _d = SP.simulate_run(L, p_keep, rng)
                    samples.append(t)
        pools[cup] = samples
    return pools


def draw_work(rng, t_draw, area_scale):
    r = rng.random()
    acc = 0.0
    amb = AMBITION[-1][1]
    for w, med, sg in AMBITION:
        acc += w
        if r <= acc:
            amb = med * math.exp(rng.gauss(0.0, sg))
            break
    return area_scale * min(amb, RATE_FP * t_draw * rng.uniform(0.7, 1.0))


def pick_work(works, cup, rng):
    ws = sorted(works)
    if cup == 0:
        return ws[rng.randint(0, min(1, len(ws) - 1))]
    if cup == 1:
        return ws[len(ws) // 2]
    return ws[-1]


def sim_game(N, players, e_t, f, style, fmult, pools, rng, area_scale, stats):
    victim = rng.randrange(N)
    cups = 0
    reveals = 0
    rounds = 0
    while True:
        rounds += 1
        t_draw = rng.choice(pools[min(cups, 2)])
        stats["sleep"][min(cups, 2)].append(t_draw)
        works = [draw_work(rng, t_draw, area_scale) for _ in range(N - 1)]
        stats["work_area"].extend(works)
        b = min(CAP, (1.0 / (N - 1)) * fmult * (1.0 + style * reveals))
        p = e_t * ACC_HARD + f * ACC_BAIT + max(0.0, 1.0 - e_t - f) * b
        if rng.random() < p:
            others = [i for i in range(N) if i != victim]
            victim = others[rng.randrange(N - 1)]
            cups = 0
        else:
            area = pick_work(works, cups, rng)
            players[victim]["carbons"].append(area)
            stats["carbon_created"] += 1
            stats["carbon_area"] += area
            reveals += 1
            cups += 1
            if cups >= 3:
                loser = victim
                locked = players[loser]["carbons"]
                stats["perm_per_loss_cnt"].append(len(locked))
                stats["perm_per_loss_area"].append(sum(locked))
                players[loser]["perm_area"] += sum(locked)
                players[loser]["carbons"] = []
                share = players[loser]["cash"] // (N - 1)
                for i in range(N):
                    if i != loser:
                        players[i]["cash"] += share
                players[loser]["cash"] = 0
                players[loser]["losses"] += 1
                return rounds, loser


def lobby_laser(players, stats):
    for pl in players:
        while pl["carbons"] and pl["cash"] >= LASER_FULL:
            pl["carbons"].pop(0)
            pl["cash"] -= LASER_FULL
            stats["lasered"] += 1


def pct(a, q):
    a = sorted(a)
    return a[min(len(a) - 1, int(q * (len(a) - 1)))] if a else float("nan")


def run_scenario(N, e_t, f, style, fmult, pools, area_scale, n_tables=300, games_per=40, seed=9, buyin=0):
    rng = random.Random(seed + N)
    stats = {
        "sleep": {0: [], 1: [], 2: []},
        "work_area": [],
        "carbon_created": 0,
        "carbon_area": 0.0,
        "perm_per_loss_cnt": [],
        "perm_per_loss_area": [],
        "lasered": 0,
        "rounds": [],
        # 債務帶（終局後、雷射前，只統計身上有碳黑的玩家-場觀測）：
        "debt_full": 0,   # 現金夠洗光全部碳黑
        "debt_part": 0,   # 夠洗一些、不夠洗光＝取捨帶
        "debt_none": 0,   # 連一幅都洗不起
    }
    # 門檻交叉紀錄：per player => (games_played, losses) 於首次跨越各門檻
    cross = {th: {"games": [], "losses": []} for th in THRESHOLDS}
    carried_after_game = []
    broke_game_idx = []  # 全桌買不起任何一次完整雷射的首場
    for _tb in range(n_tables):
        players = [
            {"cash": START_CASH, "carbons": [], "perm_area": 0.0, "losses": 0, "crossed": set()}
            for _ in range(N)
        ]
        broke_found = False
        for g in range(1, games_per + 1):
            for pl in players:
                pl["cash"] += buyin
            r, _loser = sim_game(N, players, e_t, f, style, fmult, pools, rng, area_scale, stats)
            stats["rounds"].append(r)
            for pl in players:
                if pl["carbons"]:
                    need = len(pl["carbons"]) * LASER_FULL
                    if pl["cash"] >= need:
                        stats["debt_full"] += 1
                    elif pl["cash"] >= LASER_FULL:
                        stats["debt_part"] += 1
                    else:
                        stats["debt_none"] += 1
            lobby_laser(players, stats)
            carried_after_game.append(sum(len(pl["carbons"]) for pl in players))
            if not broke_found and all(pl["cash"] < LASER_FULL for pl in players):
                broke_game_idx.append(g)
                broke_found = True
            for pl in players:
                cov = pl["perm_area"] / PRIME
                for th in THRESHOLDS:
                    if cov >= th and th not in pl["crossed"]:
                        pl["crossed"].add(th)
                        cross[th]["games"].append(g)
                        cross[th]["losses"].append(pl["losses"])
        if not broke_found:
            broke_game_idx.append(games_per + 1)
    n_games = n_tables * games_per
    d_tot = max(1, stats["debt_full"] + stats["debt_part"] + stats["debt_none"])
    print("  局數/場 p50=%d p90=%d | 碳黑 %.2f 幅/場（%.1f A5/場）| 雷射消化率 %.0f%%"
          % (pct(stats["rounds"], 0.5), pct(stats["rounds"], 0.9),
             stats["carbon_created"] / n_games, stats["carbon_area"] / n_games / A5,
             100.0 * stats["lasered"] / max(1, stats["carbon_created"])))
    print("  債務帶（有碳黑的玩家-場）：洗得光 %.0f%% | 取捨帶 %.0f%% | 洗不起 %.0f%%"
          % (100.0 * stats["debt_full"] / d_tot, 100.0 * stats["debt_part"] / d_tot,
             100.0 * stats["debt_none"] / d_tot))
    print("  永久/敗場 p50=%d 幅 %.1f A5（p90=%d 幅 %.1f A5）| 桌面破產於第 %s 場(p50)"
          % (pct(stats["perm_per_loss_cnt"], 0.5), pct(stats["perm_per_loss_area"], 0.5) / A5,
             pct(stats["perm_per_loss_cnt"], 0.9), pct(stats["perm_per_loss_area"], 0.9) / A5,
             pct(broke_game_idx, 0.5)))
    for th in THRESHOLDS:
        g50 = pct(cross[th]["games"], 0.5)
        l50 = pct(cross[th]["losses"], 0.5)
        g10 = pct(cross[th]["games"], 0.1)
        n_crossed = len(cross[th]["games"])
        frac = 100.0 * n_crossed / (n_tables * N)
        print("  PRIME %d%%：輸 %s 場 / 玩 %s 場到達（最倒楣10%%：第 %s 場）— %d 場內到達者佔 %.0f%%"
              % (int(th * 100), l50, g50, g10, games_per, frac))
    return stats


if __name__ == "__main__":
    n_tables = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    print("儀器：sim_tattoo_economy（實作值 cash=%d laser=%d/幅；PRIME=%d cm²；A5=%.0f cm²）"
          % (START_CASH, LASER_FULL, int(PRIME), A5))
    scenarios = [("陌生人房 e_t=.35 f=.10", 0.35, 0.10, 0.12, 1.0),
                 ("朋友房   e_t=.35 f=.15", 0.35, 0.15, 0.20, 2.2)]
    for N in (4, 5, 6):
        pools = build_time_pools(N - 1)
        print("\n===== N=%d（陷阱 %d）=====" % (N, N - 1))
        for cup in range(3):
            s = pools[cup]
            print("  昏睡時間 杯%d：p10=%3.0fs p50=%3.0fs p90=%3.0fs（300s 上限截斷 %.0f%%）"
                  % (cup, pct(s, 0.1), pct(s, 0.5), pct(s, 0.9),
                     100.0 * sum(1 for x in s if x >= SP.TIME_CAP) / len(s)))
        for name, e_t, f, style, fm in scenarios:
            print(" -- %s AREA_SCALE=1.0 --" % name)
            st = run_scenario(N, e_t, f, style, fm, pools, 1.0, n_tables)
            wa = st["work_area"]
            print("  單幅足跡 p50=%.0f cm²（%.2f A5）p90=%.0f cm²（%.2f A5）"
                  % (pct(wa, 0.5), pct(wa, 0.5) / A5, pct(wa, 0.9), pct(wa, 0.9) / A5))
        for scale in (0.6, 1.5):
            print(" -- 陌生人房 靈敏度 AREA_SCALE=%.1f --" % scale)
            run_scenario(N, 0.35, 0.10, 0.12, 1.0, pools, scale, n_tables)
