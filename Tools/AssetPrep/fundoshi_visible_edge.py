"""可見邊儀器（2026-08-24）：量「你看得到的那條線」，而不是接觸線。

**發現**（本檔存在的理由）：布緣（接觸線 ●）被 SINK_EDGE 壓在皮膚下 0.8mm，
實測 3706/3706 個布緣頂點全部 sd = -0.800mm，露在皮膚外的 **0 個**。
玩家看到的黑／膚交界（✕）是「布頂面穿出皮膚」的那條等值線，位在 ● 內側
d* = 1.18~2.44mm（p50 1.39），而 d* 由局部帶寬決定（corr 0.916）。
✕ 在管線裡不是任何實體——沒被切出來、沒被平滑、沒有閘門。本檔補上量測。

量什麼（與專案對 ● 的處理同源，只是換一條線）：
  1. 弧長等距重取樣 1mm（索引平滑會製造假訊號＝08-24 血價）
  2. 分尺度帶通位移，拆成 N（法向＝皮膚進出）與 B（側向＝貼著皮膚左右挪）
     <1.5cm ｜ 1.5~5cm ← user 肉眼抱怨的尺度 ｜ 5~15cm（合法造型）
  3. 弦 3/6/12mm 轉折角（沿用舊尺，供跨版本對照）
  4. 帶寬沿長度的變化（配對用線上真實弧長，>10cm 才算對面——
     「最近非鄰近點」在迴轉處會抓到自己＝08-24 血價）
唯讀：不寫任何資產。
Run: blender --background --python fundoshi_visible_edge.py -- [blend路徑]
"""
import bpy, os, sys
import numpy as np
from collections import defaultdict
from mathutils import Vector
from mathutils.kdtree import KDTree
from mathutils.bvhtree import BVHTree

ROOT = r"C:\games\Unreal Engine\nice_ink"
argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
BLEND = argv[0] if argv else os.path.join(ROOT, "SourceAssets", "sumo_character_master.blend")
OUT = os.path.join(ROOT, "Saved", "fundoshi_edge_report.txt")
rep = []


def P(*a):
    s = " ".join(str(x) for x in a)
    print(s, flush=True)
    rep.append(s)


bpy.ops.wm.open_mainfile(filepath=BLEND)
body = bpy.data.objects["SumoRetopo"]
fund = bpy.data.objects["Fundoshi"]
me = fund.data
V = np.empty(len(me.vertices) * 3); me.vertices.foreach_get("co", V); V = V.reshape(-1, 3)
E = np.empty(len(me.edges) * 2, np.int64); me.edges.foreach_get("vertices", E); E = E.reshape(-1, 2)
me.calc_loop_triangles()
tri = np.array([list(t.vertices) for t in me.loop_triangles])
nV = len(V) // 2
P("blend = " + os.path.basename(BLEND))
P(f"Fundoshi verts={len(V)} tris={len(tri)}  (頂面頂點 0..{nV-1}, 底面 {nV}..)")

bvh = BVHTree.FromObject(body, bpy.context.evaluated_depsgraph_get())


def skin_sd(p):
    loc, nor, _, _ = bvh.find_nearest(Vector(p))
    nor = np.array(nor)
    return float(np.dot(np.array(p) - np.array(loc), nor)) * 1000.0, nor


sd = np.empty(nV)
for i in range(nV):
    sd[i] = skin_sd(V[i])[0]

# ---------- 布緣 ●：頂↔底成對相連的頂點（牆腳） ----------
pair = np.zeros(nV, bool)
for a, b in E:
    a, b = int(a), int(b)
    if abs(a - b) == nV:
        pair[min(a, b)] = True
rim = [int(x) for x in np.where(pair)[0]]
rimset = set(rim)
P(f"布緣 ● 頂點 {len(rim)}; 帶號距離 min {sd[rim].min():.3f} max {sd[rim].max():.3f} mm "
  f"(露出皮膚的 {int((sd[rim] > 0).sum())} 個)")

# ---------- 可見交線 ✕：頂面三角形上 sd=0 的等值線 ----------
# 頂面的自訂 split normal（＝實際餵給著色的法線）。可見線的像素由它內插而來，
# 所以「線的形狀」與「線的著色」是兩個獨立的量，必須分開驗。
cn = np.empty(len(me.loops) * 3); me.corner_normals.foreach_get("vector", cn); cn = cn.reshape(-1, 3)
lvi = np.empty(len(me.loops), np.int64); me.loops.foreach_get("vertex_index", lvi)
vtop_n = np.zeros((nV, 3))
have_n = np.zeros(nV, bool)
for p_ in me.polygons:
    vs = [int(v) for v in p_.vertices]
    if max(vs) >= nV:
        continue                    # 只取頂面
    for lo in range(p_.loop_start, p_.loop_start + p_.loop_total):
        vi = int(lvi[lo])
        if not have_n[vi]:
            vtop_n[vi] = cn[lo]
            have_n[vi] = True

outside_skin = sd >= 0.0            # True = 露在皮膚外 = 看得見
node_of = {}
nodes = []
node_nrm = []
segs = []


def cut_node(a, b):
    key = (a, b) if a < b else (b, a)
    if key in node_of:
        return node_of[key]
    t = sd[a] / (sd[a] - sd[b])
    node_of[key] = len(nodes)
    nodes.append(V[a] * (1 - t) + V[b] * t)
    nn = vtop_n[a] * (1 - t) + vtop_n[b] * t
    node_nrm.append(nn / max(np.linalg.norm(nn), 1e-12))
    return node_of[key]


for t3 in tri:
    a, b, c = int(t3[0]), int(t3[1]), int(t3[2])
    if a >= nV or b >= nV or c >= nV:
        continue                    # 只取頂面
    f = [outside_skin[a], outside_skin[b], outside_skin[c]]
    k = sum(f)
    if k == 0 or k == 3:
        continue
    vs = [a, b, c]
    i = f.index(True) if k == 1 else f.index(False)
    p_, q_, r_ = vs[i], vs[(i + 1) % 3], vs[(i + 2) % 3]
    segs.append((cut_node(p_, q_), cut_node(p_, r_)))
nodes = np.array(nodes)
node_nrm = np.array(node_nrm)
P(f"可見交線 ✕: 交點 {len(nodes)}, 線段 {len(segs)}")


def chain_idx(links, min_len=50):
    """回傳索引串（不是座標）——呼叫端才知道要取位置還是法線。"""
    adj = defaultdict(list)
    for a, b in links:
        adj[a].append(b)
        adj[b].append(a)
    seen = set()
    out = []
    for s0 in list(adj):
        if s0 in seen or len(adj[s0]) != 2:
            continue
        loop = [s0]
        seen.add(s0)
        prev, cur = None, s0
        while True:
            nb = [x for x in adj[cur] if x != prev and x not in seen]
            if not nb:
                break
            nxt = nb[0]
            loop.append(nxt)
            seen.add(nxt)
            prev, cur = cur, nxt
        if len(loop) >= min_len:
            out.append(loop)
    return out


def chain_loops(pos, links, min_len=50):
    return [np.array([pos[i] for i in idx]) for idx in chain_idx(links, min_len)]


xchains = chain_idx(segs)
xloops = [nodes[idx] for idx in xchains]
P(f"可見交線 ✕ 環: {[len(l) for l in xloops]}  合計 {sum(len(l) for l in xloops)} / 全部 {len(nodes)} 交點")

# 布緣連通性用「牆面」推導＝精確解。牆是成對三角形 (a,b,底b) 與 (a,底b,底a)，
# 前者恰有兩個頂面頂點 ⇒ (a,b) 就是輪廓邊。用頂面邊去猜會撿到跨窄帶的邊與 snapped
# 切點，串出分支度 3/4 的節點與 22 段碎片（08-24 血價：那版 ● 數字全是垃圾，
# 轉折角印到 179° 還照印，下一個人會直接引用）。
rlinks = []
for p_ in me.polygons:
    tops = [int(v) for v in p_.vertices if int(v) < nV]
    if len(tops) == 2:
        rlinks.append((tops[0], tops[1]))
rloops = chain_loops({i: V[i] for i in rim}, rlinks)
_deg = defaultdict(int)
for a, b in rlinks:
    _deg[a] += 1
    _deg[b] += 1
_bad = sum(1 for v in rim if _deg[v] != 2)
P(f"布緣 ● 環: {[len(l) for l in rloops]}  (分支度≠2 的節點 {_bad} 個; 覆蓋 "
  f"{sum(len(l) for l in rloops)}/{len(rim)})")
if _bad or sum(len(l) for l in rloops) < len(rim) * 0.95:
    P("  ！！布緣串環不完整 ⇒ 下面 ● 的數字不可信，不得引用")

# ---------- 分析 ----------
DX = 0.001


def resample(c):
    c = np.vstack([c, c[:1]])
    seg = np.linalg.norm(np.diff(c, axis=0), axis=1)
    s = np.concatenate([[0], np.cumsum(seg)])
    L = float(s[-1])
    n = max(int(round(L / DX)), 32)
    q = np.linspace(0, L, n, endpoint=False)
    out = np.empty((n, 3))
    for k in range(3):
        out[:, k] = np.interp(q, s, c[:, k])
    return out, L


def lp(y, sig):
    n = len(y)
    k = np.fft.rfftfreq(n, d=DX)
    G = np.exp(-2 * (np.pi ** 2) * (sig ** 2) * (k ** 2))
    return np.fft.irfft(np.fft.rfft(y, axis=0) * G[:, None], n=n, axis=0)


def frames(c):
    T = np.roll(c, -1, 0) - np.roll(c, 1, 0)
    T /= np.maximum(np.linalg.norm(T, axis=1), 1e-12)[:, None]
    N = np.array([skin_sd(p)[1] for p in c])
    N -= (np.einsum('ij,ij->i', N, T))[:, None] * T
    N /= np.maximum(np.linalg.norm(N, axis=1), 1e-12)[:, None]
    return T, N, np.cross(T, N)


def turn_deg(c, chord_mm):
    m = int(round(chord_mm))
    a = c - np.roll(c, m, 0)
    b = np.roll(c, -m, 0) - c
    a /= np.maximum(np.linalg.norm(a, axis=1), 1e-12)[:, None]
    b /= np.maximum(np.linalg.norm(b, axis=1), 1e-12)[:, None]
    return np.degrees(np.arccos(np.clip(np.einsum('ij,ij->i', a, b), -1, 1)))


BANDS = [("<1.5cm", 0.0, 0.004), ("1.5~5cm", 0.004, 0.015), ("5~15cm", 0.015, 0.050)]


def analyse(tag, loops):
    P("")
    P("########  " + tag + "  ########")
    accB = {k: [] for k, _, _ in BANDS}
    accN = {k: [] for k, _, _ in BANDS}
    accT = {c: [] for c in (3, 6, 12)}
    tot = 0.0
    for li, c0 in enumerate(loops):
        c, L = resample(c0)
        tot += L
        _, N, B = frames(lp(c, 0.015))
        for name, s1, s2 in BANDS:
            hi = c if s1 == 0 else lp(c, s1)
            D = hi - lp(c, s2)
            accN[name].append(np.abs(np.einsum('ij,ij->i', D, N)) * 1000)
            accB[name].append(np.abs(np.einsum('ij,ij->i', D, B)) * 1000)
        cd = lp(c, 0.0015)
        for ch in (3, 6, 12):
            accT[ch].append(turn_deg(cd, ch))
        P(f"  環{li}: 長 {L*100:7.1f} cm, {len(c)} 取樣點")
    P(f"  總長 {tot*100:.1f} cm")
    P(f"  {'尺度':<10}{'側向B p50':>11}{'p90':>9}{'p99':>9}   {'法向N p50':>11}{'p90':>9}{'p99':>9}   (mm)")
    for name, _, _ in BANDS:
        b = np.concatenate(accB[name])
        n_ = np.concatenate(accN[name])
        P(f"  {name:<10}{np.percentile(b,50):11.3f}{np.percentile(b,90):9.3f}{np.percentile(b,99):9.3f}   "
          f"{np.percentile(n_,50):11.3f}{np.percentile(n_,90):9.3f}{np.percentile(n_,99):9.3f}")
    P(f"  {'弦mm':<10}{'轉折 p50':>11}{'p90':>9}{'p99':>9}{'p99.9':>10}{'max':>9}   (deg)")
    for ch in (3, 6, 12):
        t_ = np.concatenate(accT[ch])
        P(f"  {ch:<10}{np.percentile(t_,50):11.3f}{np.percentile(t_,90):9.3f}{np.percentile(t_,99):9.3f}"
          f"{np.percentile(t_,99.9):10.3f}{t_.max():9.3f}")


analyse("✕ 可見交線（玩家看到的）", xloops)
analyse("● 布緣接觸線（埋在皮下 0.8mm）", rloops)

# ---------- 可見線上的著色（形狀之外的另一半：眼睛讀的是明暗，明暗由法線決定）----------
P("")
P("########  可見線上的著色法線（自訂 split normal 內插）  ########")
accS = []
accR = []
for idx in xchains:
    c0 = nodes[idx]
    n0 = node_nrm[idx]
    cc = np.vstack([c0, c0[:1]])
    nn = np.vstack([n0, n0[:1]])
    seg = np.linalg.norm(np.diff(cc, axis=0), axis=1)
    s_ = np.concatenate([[0], np.cumsum(seg)])
    L = float(s_[-1])
    n_ = max(int(round(L / DX)), 32)
    q = np.linspace(0, L, n_, endpoint=False)
    nr = np.stack([np.interp(q, s_, nn[:, k]) for k in range(3)], 1)
    nr /= np.maximum(np.linalg.norm(nr, axis=1), 1e-12)[:, None]
    step = np.degrees(np.arccos(np.clip(np.einsum('ij,ij->i', nr, np.roll(nr, -1, 0)), -1, 1)))
    sm = lp(nr, 0.004)
    sm /= np.maximum(np.linalg.norm(sm, axis=1), 1e-12)[:, None]
    rough = np.degrees(np.arccos(np.clip(np.einsum('ij,ij->i', nr, sm), -1, 1)))
    accS.append(step)
    accR.append(rough)
st = np.concatenate(accS)
rg = np.concatenate(accR)
P(f"  相鄰 1mm 法線夾角: p50 {np.percentile(st,50):.3f} p90 {np.percentile(st,90):.3f} "
  f"p99 {np.percentile(st,99):.3f} max {st.max():.3f} deg/mm")
P(f"  相對 σ4mm 平滑場的偏離（細尺度著色噪聲）: p50 {np.percentile(rg,50):.3f} "
  f"p90 {np.percentile(rg,90):.3f} p99 {np.percentile(rg,99):.3f} max {rg.max():.3f} deg")

# ---------- 可見帶寬 ----------
P("")
P("########  可見帶寬（沿 ✕ 量, 配對用線上弧長 >10cm）  ########")
pts = []
owner = []
spans = []
off = 0
for li, c0 in enumerate(xloops):
    c, L = resample(c0)
    spans.append((off, len(c)))
    for i, p in enumerate(c):
        pts.append(p)
        owner.append((li, i, len(c)))
    off += len(c)
pts = np.array(pts)
kd = KDTree(len(pts))
for i, p in enumerate(pts):
    kd.insert(Vector(p), i)
kd.balance()
W = np.full(len(pts), np.nan)
for i, p in enumerate(pts):
    li, ii, n_ = owner[i]
    for (loc, j, dd) in kd.find_n(Vector(p), 800):
        lj, jj, nj = owner[j]
        if lj != li:
            W[i] = dd * 1000.0
            break
        da = abs(jj - ii)
        da = min(da, n_ - da)          # 取樣間距 1mm => 索引差 = mm
        if da > 100:
            W[i] = dd * 1000.0
            break
ok = ~np.isnan(W)
P(f"有效 {int(ok.sum())}/{len(W)}; 帶寬 mm: p5 {np.percentile(W[ok],5):.1f} "
  f"p50 {np.percentile(W[ok],50):.1f} p95 {np.percentile(W[ok],95):.1f}")
for li, (a, n_) in enumerate(spans):
    w = W[a:a + n_]
    if (~np.isnan(w)).sum() < 100:
        continue
    med = float(np.nanmedian(w))
    ww = np.where(np.isnan(w), med, w)[:, None]
    d1 = (ww - lp(ww, 0.004))[:, 0]
    d2 = (lp(ww, 0.004) - lp(ww, 0.015))[:, 0]
    P(f"  環{li}: 帶寬 p50 {med:6.1f} mm; 起伏 std  <1.5cm {d1.std():.3f}  1.5~5cm {d2.std():.3f} mm")

os.makedirs(os.path.dirname(OUT), exist_ok=True)
open(OUT, "w", encoding="utf-8").write("\n".join(rep) + "\nDONE\n")
P("")
P("WROTE " + OUT)
