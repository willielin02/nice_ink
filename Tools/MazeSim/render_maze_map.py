# 醉夢圓形迷宮全圖渲染 v2（與 UE FDreamMazeGen 同構：FRandomStream 逐位元移植）
# 2026-07-14 拓樸改版同步：K=5 等分中央門＋環1五重對稱殼＋鎖邊＋破圈＋禁入殼
# 用法：python render_maze_map.py <seed> <cup 0-2> <輸出png>
import math
import struct
import sys
from PIL import Image, ImageDraw, ImageFont

TAU = math.tau
CENTER_RADIUS = 1.5
CENTER_DOORS = 5
EXIT_GAP_FRAC = 0.6
WALL_T = 0.38


class Rand:
    def __init__(self, seed):
        self.seed = seed & 0xFFFFFFFF

    def mutate(self):
        self.seed = (self.seed * 196314165 + 907633515) & 0xFFFFFFFF

    def frand(self):
        self.mutate()
        u = (self.seed & 0x007FFFFF) | 0x3F800000
        return struct.unpack("<f", struct.pack("<I", u))[0] - 1.0

    def rand_range(self, lo, hi):
        n = hi - lo + 1
        return lo + (min(int(self.frand() * n), n - 1) if n > 0 else 0)


def params_cup(cup):
    base = dict(rings=8, base_sectors=10, arc_w=1.25, branch=0.55, braid=0.08,
                radial=0.30, path_bias=0.65, min_depth=2, min_sep=3,
                det_min=0.15, det_max=0.55, speed=2.4,
                min_solve=8.0, max_solve=70.0)
    if cup == 0:
        base.update(rings=7, branch=0.5, braid=0.12, radial=0.35,
                    path_bias=0.5, min_solve=6.0, max_solve=60.0)
    elif cup == 2:
        base.update(rings=9, branch=0.6, braid=0.05, radial=0.25,
                    path_bias=0.8, min_solve=10.0, max_solve=80.0)
    return base


class Layout:
    def __init__(self):
        self.sectors = []
        self.ring_start = []
        self.r_in = []
        self.r_out = []
        self.edges = []       # [a, b, open, is_ring]
        self.cell_edges = []
        self.exit_cell = -1
        self.exit_phi0 = 0.0
        self.exit_phi1 = 0.0
        self.traps = []
        self.cp_s = -1
        self.cp_k = -1

    def num_cells(self):
        return self.ring_start[-1] + self.sectors[-1]

    def cell_index(self, r, s):
        return self.ring_start[r] + s

    def coords(self, c):
        for r in range(len(self.ring_start) - 1, -1, -1):
            if c >= self.ring_start[r]:
                return r, c - self.ring_start[r]
        return 0, 0

    def center(self, c):
        r, s = self.coords(c)
        if r == 0:
            return (0.0, 0.0)
        rad = (self.r_in[r] + self.r_out[r]) * 0.5
        phi = (s + 0.5) * TAU / self.sectors[r]
        return (rad * math.cos(phi), rad * math.sin(phi))

    def open_neighbors(self, c):
        out = []
        for ei in self.cell_edges[c]:
            a, b, op, _ = self.edges[ei]
            if op:
                out.append(b if a == c else a)
        return out

    def bfs_map(self, frm, blocked):
        dist = [-1] * self.num_cells()
        if frm in blocked:
            return dist
        q = [frm]
        dist[frm] = 0
        h = 0
        while h < len(q):
            c = q[h]
            h += 1
            for n in self.open_neighbors(c):
                if dist[n] < 0 and n not in blocked:
                    dist[n] = dist[c] + 1
                    q.append(n)
        return dist

    def bfs_path(self, frm, to, blocked):
        par = [-2] * self.num_cells()
        if frm in blocked or to in blocked:
            return None
        q = [frm]
        par[frm] = -1
        h = 0
        while h < len(q):
            c = q[h]
            h += 1
            if c == to:
                break
            for n in self.open_neighbors(c):
                if par[n] == -2 and n not in blocked:
                    par[n] = c
                    q.append(n)
        if par[to] == -2:
            return None
        path = []
        c = to
        while c != -1:
            path.append(c)
            c = par[c]
        path.reverse()
        return path


def find_edge(L, a, b):
    for ei in L.cell_edges[a]:
        ea, eb, _, _ = L.edges[ei]
        if (ea == a and eb == b) or (ea == b and eb == a):
            return ei
    return -1


def build_grid(p, L):
    L.sectors = [1]
    L.ring_start = [0]
    L.r_in = [0.0]
    L.r_out = [CENTER_RADIUS]
    # 扇區數向上取 5 的倍數（下限 10）——K=5 整除前提（與 C++ 同步）
    S = 5 * max(2, -(-max(4, p["base_sectors"]) // 5))
    for ring in range(1, p["rings"] + 1):
        inner = L.r_out[-1]
        outer = inner + 1.0
        if ring > 1:
            mid = (inner + outer) * 0.5
            if TAU * mid / S > p["arc_w"] * 1.5:
                S *= 2
        L.ring_start.append(L.ring_start[-1] + L.sectors[-1])
        L.sectors.append(S)
        L.r_in.append(inner)
        L.r_out.append(outer)
    n = L.num_cells()
    L.cell_edges = [[] for _ in range(n)]

    def add_edge(a, b, is_ring):
        idx = len(L.edges)
        L.edges.append([a, b, False, is_ring])
        L.cell_edges[a].append(idx)
        L.cell_edges[b].append(idx)

    for ring in range(p["rings"]):
        s_in, s_out = L.sectors[ring], L.sectors[ring + 1]
        for so in range(s_out):
            si = 0 if ring == 0 else so * s_in // s_out
            add_edge(L.cell_index(ring, si), L.cell_index(ring + 1, so), True)
    for ring in range(1, p["rings"] + 1):
        S2 = L.sectors[ring]
        if S2 < 3:
            continue
        for s in range(S2):
            add_edge(L.cell_index(ring, s), L.cell_index(ring, (s + 1) % S2), False)


def build_shell(R, L, locked):
    # 五重對稱內殼（與 C++ BuildShell 逐呼叫同序）
    S1 = L.sectors[1]
    W = S1 // CENTER_DOORS
    door_off = R.rand_range(0, W - 1)
    for s in range(S1):
        ei = find_edge(L, 0, L.cell_index(1, s))
        L.edges[ei][2] = (s % W) == door_off
        locked.add(ei)
    for s in range(S1):
        ei = find_edge(L, L.cell_index(1, s), L.cell_index(1, (s + 1) % S1))
        if ei < 0:
            continue
        L.edges[ei][2] = (s % W) != (W - 1)
        locked.add(ei)
    mult = L.sectors[2] // S1
    slot_count = W * mult
    slot_open = [False] * slot_count
    num_out = R.rand_range(1, min(2, slot_count))
    for _ in range(num_out):
        slot_open[R.rand_range(0, slot_count - 1)] = True
    for wedge in range(CENTER_DOORS):
        for slot in range(slot_count):
            si = wedge * W + slot // mult
            so = si * mult + slot % mult
            ei = find_edge(L, L.cell_index(1, si), L.cell_index(2, so))
            L.edges[ei][2] = slot_open[slot]
            locked.add(ei)


def carve(p, R, L, locked):
    n = L.num_cells()
    visited = [False] * n
    active = [0]
    visited[0] = True
    h = 0
    while h < len(active):
        for nb in L.open_neighbors(active[h]):
            if not visited[nb]:
                visited[nb] = True
                active.append(nb)
        h += 1
    while active:
        pick = len(active) - 1 if R.frand() < 1 - p["branch"] else R.rand_range(0, len(active) - 1)
        cell = active[pick]
        chosen = -1
        total = 0.0
        for ei in L.cell_edges[cell]:
            a, b, op, is_ring = L.edges[ei]
            other = b if a == cell else a
            if visited[other] or ei in locked:
                continue
            w = max(0.02, p["radial"] if is_ring else 1 - p["radial"])
            total += w
            if R.frand() * total <= w:
                chosen = ei
        if chosen == -1:
            active.pop(pick)
            continue
        L.edges[chosen][2] = True
        a, b, _, _ = L.edges[chosen]
        other = b if a == cell else a
        visited[other] = True
        active.append(other)


def braid(p, R, L, locked):
    dead = []
    for c in range(L.num_cells()):
        deg = sum(1 for ei in L.cell_edges[c] if L.edges[ei][2])
        if deg == 1:
            dead.append(c)
    for i in range(len(dead) - 1, 0, -1):
        j = R.rand_range(0, i)
        dead[i], dead[j] = dead[j], dead[i]
    for i in range(round(len(dead) * p["braid"])):
        closed = [ei for ei in L.cell_edges[dead[i]]
                  if not L.edges[ei][2] and ei not in locked]
        if closed:
            L.edges[closed[R.rand_range(0, len(closed) - 1)]][2] = True


def break_rings(R, L, locked):
    for ring in range(2, len(L.sectors)):
        S = L.sectors[ring]
        if S < 3:
            continue
        breakable = []
        all_open = True
        for s in range(S):
            ei = find_edge(L, L.cell_index(ring, s), L.cell_index(ring, (s + 1) % S))
            if ei < 0 or not L.edges[ei][2]:
                all_open = False
                break
            if ei not in locked:
                breakable.append(ei)
        if all_open and breakable:
            L.edges[breakable[R.rand_range(0, len(breakable) - 1)]][2] = False


def place(p, R, trap_count, relax, L):
    outer = len(L.sectors) - 1
    es = R.rand_range(0, L.sectors[outer] - 1)
    L.exit_cell = L.cell_index(outer, es)
    span = TAU / L.sectors[outer]
    L.exit_phi0 = es * span + span * (0.5 - EXIT_GAP_FRAC * 0.5)
    L.exit_phi1 = es * span + span * (0.5 + EXIT_GAP_FRAC * 0.5)
    dc = L.bfs_map(0, set())
    de = L.bfs_map(L.exit_cell, set())
    base = dc[L.exit_cell]
    if base <= 0:
        return False
    dmin, dmax = p["det_min"], max(p["det_max"], p["det_min"] + 0.05)
    if relax >= 3:
        dmin *= 0.3
        dmax *= 2
    # 拾取點禁入環 1 殼（角向地標）——與 C++ 同步
    cands = [c for c in range(1, L.num_cells())
             if L.coords(c)[0] >= 2 and c != L.exit_cell and dc[c] >= 0
             and dmin <= (dc[c] + de[c] - base) / base <= dmax]
    if len(cands) < 2:
        return False
    for i in range(len(cands) - 1, 0, -1):
        j = R.rand_range(0, i)
        cands[i], cands[j] = cands[j], cands[i]
    L.cp_s = cands[0]
    L.cp_k = -1
    for i in range(1, len(cands)):
        dmap = L.bfs_map(L.cp_s, set())
        if dmap[cands[i]] >= 3:
            L.cp_k = cands[i]
            break
    if L.cp_k == -1:
        L.cp_k = cands[1]
    L.traps = []
    if trap_count > 0:
        main = L.bfs_path(0, L.exit_cell, set()) or []
        dpath = [-1] * L.num_cells()
        q = list(main)
        for c in main:
            dpath[c] = 0
        h = 0
        while h < len(q):
            for n in L.open_neighbors(q[h]):
                if dpath[n] < 0:
                    dpath[n] = dpath[q[h]] + 1
                    q.append(n)
            h += 1
        # 深度下限 2＝結構不變量（殼內死亡不可歸咎）——與 C++ 同步
        min_depth = max(2, 1 if relax >= 4 else p["min_depth"])
        min_sep = 1 if relax >= 2 else p["min_sep"]

        def connected(a, b):
            return b in L.open_neighbors(a)

        cand, w = [], []
        for c in range(1, L.num_cells()):
            r, _ = L.coords(c)
            if (r < min_depth or c in (L.exit_cell, L.cp_s, L.cp_k)
                    or connected(c, L.cp_s) or connected(c, L.cp_k) or dc[c] < 0):
                continue
            cand.append(c)
            pd = max(0, dpath[c])
            w.append(1 + (1 / (1 + pd) - 1) * p["path_bias"])
        if len(cand) < trap_count:
            return False
        for _ in range(30):
            if len(L.traps) >= trap_count:
                break
            chosen = []
            guard = 0
            while len(chosen) < trap_count and guard < 400:
                guard += 1
                total = sum(w)
                roll = R.frand() * total
                pick = 0
                for i, wi in enumerate(w):
                    roll -= wi
                    if roll <= 0:
                        pick = i
                        break
                c = cand[pick]
                far = c not in chosen
                for pl in chosen:
                    if not far:
                        break
                    dmap = L.bfs_map(c, set())
                    if dmap[pl] < min_sep or dmap[pl] < 0:
                        far = False
                if far:
                    chosen.append(c)
            if len(chosen) < trap_count:
                continue
            ts = set(chosen)
            if (L.bfs_map(0, ts)[L.exit_cell] >= 0
                    and L.bfs_map(0, ts)[L.cp_s] >= 0
                    and L.bfs_map(0, ts)[L.cp_k] >= 0):
                L.traps = chosen
        if len(L.traps) < trap_count:
            return False
    ts = set(L.traps)
    solve = L.bfs_map(0, ts)[L.exit_cell]
    L.solve_len = solve
    L.ideal = solve / p["speed"]
    if relax < 5 and not (p["min_solve"] <= L.ideal <= p["max_solve"]):
        return False
    return True


def generate(p, seed, trap_count):
    for att in range(10):
        L = Layout()
        R = Rand(seed + att * 7919)
        locked = set()
        build_grid(p, L)
        build_shell(R, L, locked)
        carve(p, R, L, locked)
        braid(p, R, L, locked)
        break_rings(R, L, locked)
        if place(p, R, trap_count, min(att, 5), L):
            return L
    return None


# ---------- 渲染 ----------
def render(L, path_png, seed, cup):
    rim = L.r_out[-1]
    SZ = 1700
    PAD = 90
    scale = (SZ - 2 * PAD) / (2 * rim)
    cx = cy = SZ // 2

    def to_px(x, y):
        return (cx + x * scale, cy - y * scale)

    img = Image.new("RGB", (SZ, SZ + 130), (11, 10, 20))
    d = ImageDraw.Draw(img)
    d.ellipse([cx - rim * scale - 10, cy - rim * scale - 10,
               cx + rim * scale + 10, cy + rim * scale + 10], fill=(25, 23, 49))

    wall = (179, 174, 214)
    wall_px = WALL_T * scale

    def thick_line(a, b, color, width):
        d.line([a, b], fill=color, width=int(width))
        r = width / 2
        for p2 in (a, b):
            d.ellipse([p2[0] - r, p2[1] - r, p2[0] + r, p2[1] + r], fill=color)

    def arc_wall(radius, p0, p1):
        n = max(2, int(abs(p1 - p0) * radius / 0.15))
        pts = [to_px(radius * math.cos(p0 + (p1 - p0) * i / n),
                     radius * math.sin(p0 + (p1 - p0) * i / n)) for i in range(n + 1)]
        for i in range(n):
            thick_line(pts[i], pts[i + 1], wall, wall_px)

    for a, b, op, is_ring in L.edges:
        if op:
            continue
        if is_ring:
            r_out_ring, s_out = L.coords(b)
            span = TAU / L.sectors[r_out_ring]
            arc_wall(L.r_in[r_out_ring], s_out * span, (s_out + 1) * span)
        else:
            ring, sa = L.coords(a)
            span = TAU / L.sectors[ring]
            phi = (sa + 1) * span
            thick_line(to_px(L.r_in[ring] * math.cos(phi), L.r_in[ring] * math.sin(phi)),
                       to_px(L.r_out[ring] * math.cos(phi), L.r_out[ring] * math.sin(phi)),
                       wall, wall_px)
    # 外緣（出口缺口除外）
    outer = len(L.sectors) - 1
    span_o = TAU / L.sectors[outer]
    for s in range(L.sectors[outer]):
        p0, p1 = s * span_o, (s + 1) * span_o
        if L.cell_index(outer, s) == L.exit_cell:
            arc_wall(rim, p0, L.exit_phi0)
            arc_wall(rim, L.exit_phi1, p1)
        else:
            arc_wall(rim, p0, p1)

    # 最短安全路（避開陷阱）
    sp = L.bfs_path(0, L.exit_cell, set(L.traps))
    decisions = 0
    if sp:
        decisions = sum(1 for c in sp if len(L.open_neighbors(c)) >= 3)
        pts = [to_px(*L.center(c)) for c in sp]
        for i in range(len(pts) - 1):
            d.line([pts[i], pts[i + 1]], fill=(90, 84, 140), width=5)

    try:
        font = ImageFont.truetype(r"C:\Windows\Fonts\msjh.ttc", 34)
        font_s = ImageFont.truetype(r"C:\Windows\Fonts\msjh.ttc", 26)
    except Exception:
        font = font_s = ImageFont.load_default()

    def dot(c, color, r, label=None, label_color=(255, 255, 255)):
        x, y = to_px(*L.center(c))
        d.ellipse([x - r, y - r, x + r, y + r], fill=color)
        if label:
            d.text((x, y + r + 4), label, fill=label_color, font=font_s, anchor="ma")

    for i, t in enumerate(L.traps):
        dot(t, (217, 122, 90), 14, f"陷阱{i + 1}", (232, 138, 122))
        x, y = to_px(*L.center(t))
        d.ellipse([x - 6, y - 20, x + 6, y - 8], fill=(20, 18, 16))  # 髮髻
    dot(L.cp_s, (79, 209, 181), 13, "噴射", (79, 209, 181))
    dot(L.cp_k, (240, 160, 74), 13, "拳腳", (240, 160, 74))
    # 出口
    gm = (L.exit_phi0 + L.exit_phi1) / 2
    x, y = to_px(rim * math.cos(gm), rim * math.sin(gm))
    d.ellipse([x - 16, y - 16, x + 16, y + 16], outline=(217, 194, 122), width=4)
    d.text((x, y + 22), "出口", fill=(217, 194, 122), font=font_s, anchor="ma")
    # 起點
    x, y = to_px(0, 0)
    d.ellipse([x - 10, y - 10, x + 10, y + 10], fill=(242, 238, 218))
    d.text((x, y + 14), "起點（死亡回這裡）", fill=(242, 238, 218), font=font_s, anchor="ma")

    rings = len(L.sectors) - 1
    d.text((SZ // 2, SZ + 8),
           f"杯{cup + 1}難度檔  種子={seed}  安全最短路={L.solve_len}步  理想通關={L.ideal:.1f}s"
           f"  繞行比={L.solve_len / rings:.2f}  正解決策點={decisions}",
           fill=(157, 151, 187), font=font, anchor="ma")
    d.text((SZ // 2, SZ + 58),
           "五門等分＋環1五重對稱殼（2026-07-14 拓樸改版）；灰紫細線＝避開陷阱的最短路（遊戲中不顯示）；陷阱在遊戲中完全隱形",
           fill=(107, 101, 144), font=font_s, anchor="ma")
    img.save(path_png)
    print("saved", path_png)


if __name__ == "__main__":
    seed = int(sys.argv[1]) if len(sys.argv) > 1 else 20260714
    cup = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    out = sys.argv[3] if len(sys.argv) > 3 else r"C:\games\Unreal Engine\nice_ink\Saved\maze_map_v2.png"
    L = generate(params_cup(cup), seed, 5)
    if not L:
        print("generation failed")
        sys.exit(1)
    render(L, out, seed, cup)
