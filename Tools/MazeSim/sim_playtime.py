# 通關時間分布蒙地卡羅：在同構生成器上跑「有記憶的探索玩家」模型
#
# 玩家模型（有界理性）：
# - 不知道地圖；視野內探索＝優先未訪格、偏好往外（知道出口在外緣）
# - 完美記得走過的格與已知陷阱（直到被旋轉污染）
# - 死亡＝回原點＋懲罰鏈（死亡畫面1.2s＋轉盤等待~4s＋旋轉動畫3.4s≈9s）
# - 旋轉後以機率 p_keep 保住心智地圖（盯住動畫成功）；失敗＝visited/陷阱知識全清
#   ＋3s 重定向呆滯。K=5 錨賭注的「押錯門重播」效果由「知識清空後重探」承載（保守近似）
# - 有記憶重生：沿已知區最短路走到最近的探索前沿，不重新亂走
# 產出：各杯 × 各玩家原型的通關秒數分布
import sys
import random
import render_maze_map as M

STEP_T = 1.0 / 2.4      # 每格秒數（AvatarSpeed）
DEATH_PENALTY = 9.0     # 死亡畫面 + 轉盤等待 + 旋轉動畫
REORIENT_T = 3.0        # 記憶被清後的重定向呆滯
TIME_CAP = 300.0


def outward_bias_pick(L, pos, cands, rng, bias=0.7):
    # 偏好往外的加權抽選（玩家知道出口在外緣）
    r0 = L.coords(pos)[0]
    ws = []
    for c in cands:
        r = L.coords(c)[0]
        w = 3.0 if r > r0 else (1.0 if r == r0 else 0.4)
        ws.append(w ** (bias * 3))
    total = sum(ws)
    roll = rng.random() * total
    for c, w in zip(cands, ws):
        roll -= w
        if roll <= 0:
            return c
    return cands[-1]


def path_within_known(L, frm, targets, visited, known_traps):
    # 已知區內 BFS 到任一 target（frontier 格允許為終點）
    if frm in targets:
        return [frm]
    par = {frm: None}
    q = [frm]
    h = 0
    while h < len(q):
        c = q[h]
        h += 1
        for n in L.open_neighbors(c):
            if n in par or n in known_traps:
                continue
            if n in visited or n in targets:
                par[n] = c
                if n in targets:
                    path = [n]
                    while par[path[-1]] is not None:
                        path.append(par[path[-1]])
                    path.reverse()
                    return path
                q.append(n)
    return None


def simulate_run(L, p_keep, rng):
    traps = set(L.traps)
    pos = 0
    visited = {0}
    known_traps = set()
    stack = [0]
    t = 0.0
    deaths = 0

    while t < TIME_CAP:
        if pos == L.exit_cell:
            return t, deaths
        nbrs = L.open_neighbors(pos)
        unvis = [n for n in nbrs if n not in visited and n not in known_traps]
        if unvis:
            nxt = outward_bias_pick(L, pos, unvis, rng)
            stack.append(pos)
        else:
            # 前沿導航：已知區最短路走向最近的未探索邊界
            frontier = set()
            for v in visited:
                for n in L.open_neighbors(v):
                    if n not in visited and n not in known_traps:
                        frontier.add(n)
            if not frontier:
                return TIME_CAP, deaths  # 全圖探完仍無出口（不應發生）
            path = path_within_known(L, pos, frontier, visited, known_traps)
            if not path or len(path) < 2:
                return TIME_CAP, deaths
            nxt = path[1]
        t += STEP_T
        if nxt in traps:
            deaths += 1
            known_traps.add(nxt)
            t += DEATH_PENALTY
            pos = 0
            stack = [0]
            if rng.random() > p_keep:
                visited = {0}
                known_traps = set()  # 陷阱知識同座標系，一起清（保守）
                t += REORIENT_T
        else:
            pos = nxt
            visited.add(pos)

    return TIME_CAP, deaths


def pct(arr, q):
    arr = sorted(arr)
    return arr[min(len(arr) - 1, int(q * (len(arr) - 1)))]


if __name__ == "__main__":
    n_seeds = int(sys.argv[1]) if len(sys.argv) > 1 else 120
    runs_per = int(sys.argv[2]) if len(sys.argv) > 2 else 3
    archetypes = [("drunk_p0.2", 0.2), ("median_p0.5", 0.5), ("veteran_p0.8", 0.8)]
    print("sim: seeds=%d runs/seed=%d traps=4  (death penalty %.0fs, reorient %.0fs, cap %ds)"
          % (n_seeds, runs_per, DEATH_PENALTY, REORIENT_T, int(TIME_CAP)))
    for cup in range(3):
        p = M.params_cup(cup)
        layouts = []
        for i in range(n_seeds):
            L = M.generate(p, 913 * (i + 1) + 7, 4)
            if L:
                layouts.append(L)
        for name, p_keep in archetypes:
            times, deaths_all, capped = [], [], 0
            rng = random.Random(42)
            for L in layouts:
                for _ in range(runs_per):
                    t, d = simulate_run(L, p_keep, rng)
                    times.append(t)
                    deaths_all.append(d)
                    capped += 1 if t >= TIME_CAP else 0
            print("cup%d %-14s p10=%5.1fs p50=%5.1fs p90=%5.1fs p99=%5.1fs  deaths p50=%d p90=%d  capped=%d/%d"
                  % (cup, name, pct(times, 0.1), pct(times, 0.5), pct(times, 0.9), pct(times, 0.99),
                     pct(deaths_all, 0.5), pct(deaths_all, 0.9), capped, len(times)))
