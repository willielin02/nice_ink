"""褌＝身體表面位移殼（2026-08-19）——「折線→牆」框架整組退役。

**四版失敗的共同病根**：Solidify/bevel/重擠出/構造式側壁全是「從一條有噪聲的
折線往上蓋牆」。鋸齒可見度 ≈ 邊界噪聲 × 牆高——0.4mm 手繪噪聲在 15mm 牆上
＝毛毛蟲鋸齒；細分讓相鄰剖面朝向變化更頻繁＝更糟（08-19 viewport 實錘）。

**新構造＝沒有牆**：貼身面（＝手繪遮罩的 3D 化身）當底，每頂點算「離邊界
距離 d」，沿平滑法線位移 h(d)：
    h = Hmax * smoothstep(d/W) - sink * (1 - smoothstep(d/3mm))
  邊界 h=-0.5mm（沉入皮膚＝交線穩定）→ 圓肩爬升 → 15mm 平台。
  **邊界噪聲只存在於高度≈0 處＝幾何上不可見**；光滑度繼承身體表面。

**實作坑（08-19 實踩）**：bmesh.subdivide_edges 對三角面拆分不一致＝
T-junction 假邊界（boundary verts 10520 vs 預期 ~4100、d p50 掉到 1mm）
→ 細分改**手寫中點 4:1**（無 T-junction 構造保證），mesh 用 from_pydata 重建。

**代價（誠實記帳）**：可見布邊＝殼穿出皮膚的交線，在手繪線內側 ~1mm；
剪影永不超出手繪線（位移沿法線＝零面內分量）；窄帶處平台達不到＝自然變薄。

Run: blender --background --python fundoshi_shell.py
"""
import bpy
import numpy as np
import os
import shutil
from collections import defaultdict
from mathutils import Vector
from mathutils.bvhtree import BVHTree

ROOT = r"C:\games\Unreal Engine\nice_ink"
MASTER = os.path.join(ROOT, "SourceAssets", "sumo_character_master.blend")
SRC = os.path.join(ROOT, "SourceAssets", "masters", "sumo_character_master_v19_prebevel.blend")
BK = os.path.join(ROOT, "SourceAssets", "masters", "sumo_character_master_v21_preshell.blend")

HMAX = 0.015
W = 0.015
SINK = 0.0005
SINK_RAMP = 0.003
SUBDIV_ROUNDS = 2
SMOOTH_ITERS = 25
LAM = 0.6

bpy.ops.wm.open_mainfile(filepath=SRC)
if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')
if not os.path.exists(BK):
    shutil.copy2(SRC, BK)
ob = bpy.data.objects["Fundoshi"]
me = ob.data
n0 = len(me.vertices)
half = n0 // 2
assert n0 == 7134, f"unexpected verts={n0}"

body = bpy.data.objects["SumoRetopo"]
dg = bpy.context.evaluated_depsgraph_get()
body_bvh = BVHTree.FromObject(body, dg)

# ---- 1) 抽貼身面為 numpy 陣列（含 UV/權重/頂點色）----
co_all = np.empty(n0 * 3)
me.vertices.foreach_get("co", co_all)
co_all = co_all.reshape(-1, 3)
uvl = me.uv_layers[0].data
vc = me.color_attributes["FaceMask"]
vcol_all = np.empty(len(vc.data) * 4)
vc.data.foreach_get("color", vcol_all)
vcol_all = vcol_all.reshape(-1, 4)
weights_all = [ [(g.group, g.weight) for g in v.groups] for v in me.vertices ]
vg_names = [vg.name for vg in ob.vertex_groups]   # 換 mesh data 會清掉 object 的群組

remap = {}
verts = []
faces = []           # 每面 = 頂點索引 tuple（局部）
face_uvs = []        # 每面 = 對應每角的 (u,v)
for p in me.polygons:
    vs = list(p.vertices)
    if min(vs) < half:
        continue                     # 只要貼身面
    li = list(p.loop_indices)
    ids = []
    for v in vs:
        if v not in remap:
            remap[v] = len(verts)
            verts.append(co_all[v])
        ids.append(remap[v])
    # 三角化（扇形；原面幾乎全 tri）
    for k in range(1, len(ids) - 1):
        faces.append((ids[0], ids[k], ids[k + 1]))
        face_uvs.append((tuple(uvl[li[0]].uv), tuple(uvl[li[k]].uv), tuple(uvl[li[k + 1]].uv)))
inv = {v: k for k, v in remap.items()}
co = np.array(verts)
wts = [dict(weights_all[inv[i]]) for i in range(len(verts))]
tone = np.array([vcol_all[inv[i]] for i in range(len(verts))])
print(f"BASE patch verts={len(co)} tris={len(faces)}")

# ---- 1b) 邊界重建為平滑曲線（2026-08-19 user 核准 ±5mm 偏離預算）----
# 手繪線實測殘差 p50 0.4mm / p99 3.5mm / 缺口最大 12mm（平滑曲線應為 0.035mm）
# ＝整條都是噪聲。做法＝**從手繪線重建**：只保留公分級以上走向（設計意圖），
# 公分以下全視為噪聲。中值濾波（殺孤立缺口）→ 弧長高斯 σ=8mm（λ=2.5cm 衰減 87%、
# λ=20cm 幾乎不動）→ 貼回皮膚表面 → 位移擴散進內部（帶窄、邊界佔 74% 頂點）。
def _loops_of(faces_l, nverts):
    ec = defaultdict(int)
    for a, b, c in faces_l:
        for e in ((a, b), (b, c), (c, a)):
            ec[(min(e), max(e))] += 1
    adj = defaultdict(list)
    for (a, b), k in ec.items():
        if k == 1:
            adj[a].append(b)
            adj[b].append(a)
    loops, seen = [], set()
    for s0 in adj:
        if s0 in seen or len(adj[s0]) != 2:
            continue
        loop, cur, prev = [s0], s0, None
        seen.add(s0)
        while True:
            nx = [x for x in adj[cur] if x != prev]
            if not nx or nx[0] == s0 or nx[0] in seen:
                break
            loop.append(nx[0])
            seen.add(nx[0])
            prev, cur = cur, nx[0]
        if len(loop) > 20:
            loops.append(loop)
    return loops, adj


def _arc(P):
    dseg = np.linalg.norm(np.diff(np.vstack([P, P[:1]]), axis=0), axis=1)
    return np.concatenate([[0.0], np.cumsum(dseg)[:-1]]), dseg.sum()


def _median_loop(P, win):
    sarc, total = _arc(P)
    out = np.empty_like(P)
    for i in range(len(P)):
        ds = sarc - sarc[i]
        ds -= total * np.round(ds / total)
        sel = np.abs(ds) <= win
        out[i] = np.median(P[sel], axis=0)
    return out


def _gauss_loop(P, sigma):
    sarc, total = _arc(P)
    out = np.empty_like(P)
    for i in range(len(P)):
        ds = sarc - sarc[i]
        ds -= total * np.round(ds / total)
        w = np.exp(-0.5 * (ds / sigma) ** 2)
        w[np.abs(ds) > 3 * sigma] = 0.0
        out[i] = (P * w[:, None]).sum(axis=0) / w.sum()
    return out


base_loops, _badj = _loops_of(faces, len(co))
print(f"BOUNDARY loops {len(base_loops)} sizes={[len(l) for l in base_loops]}")
delta = np.zeros_like(co)
devs = []
for lp in base_loops:
    L = np.array(lp)
    P0 = co[L].copy()
    P1 = _median_loop(P0, 0.015)       # ±15mm 中值窗（08-20 user 定調：手繪線=位置標記，公分級擺動也是噪聲）
    P2 = _gauss_loop(P1, 0.040)        # 08-20 二調：只留 ~13cm 以上走向
    # 切向鬆弛勻點距：中值/高斯會把點擠堆（相鄰點共位＝748 零面積面實錘）；
    # 全域弧長重取樣會讓點沿線滑 10mm+（偏離指標爆掉）——只沿切線局部勻距，
    # 側向形狀不動、無全域滑移。
    for _it in range(40):
        prv = np.roll(P2, 1, axis=0)
        nxt = np.roll(P2, -1, axis=0)
        tan = nxt - prv
        tan /= np.maximum(np.linalg.norm(tan, axis=1, keepdims=True), 1e-12)
        slide = ((0.5 * (prv + nxt) - P2) * tan).sum(1)
        P2 += 0.5 * slide[:, None] * tan
    for k in range(len(L)):            # 貼回皮膚表面（平滑會離面）
        loc, _, _, _ = body_bvh.find_nearest(Vector(P2[k]))
        if loc is not None:
            P2[k] = np.array(loc)
    delta[L] = P2 - P0
    devs.append(np.linalg.norm(P2 - P0, axis=1))
# 位移按局部帶寬鉗制：兩側邊界各移 ±5mm 在寬 <15mm 的帶上會對摺（實測 752 零面積面）
allb = np.concatenate([np.array(lp) for lp in base_loops])
lp_id = np.concatenate([[k] * len(lp) for k, lp in enumerate(base_loops)])
pos_in = np.concatenate([np.arange(len(lp)) for lp in base_loops])
sz = np.array([len(lp) for lp in base_loops])
bpos = co[allb]
for ii in range(len(allb)):
    dd = np.linalg.norm(bpos - bpos[ii], axis=1)
    ring = np.minimum(np.abs(pos_in - pos_in[ii]), sz[lp_id] - np.abs(pos_in - pos_in[ii]))
    dd[(lp_id == lp_id[ii]) & (ring < 15)] = np.inf
    width = float(dd.min())
    cap = 0.3 * width
    vi = allb[ii]
    mag = float(np.linalg.norm(delta[vi]))
    if mag > cap > 0:
        delta[vi] *= cap / mag
devs = [np.linalg.norm(delta[np.array(lp)], axis=1) for lp in base_loops]
dv = np.concatenate(devs) * 1000.0
print(f"**邊界偏離手繪線(mm) p50={np.percentile(dv,50):.2f} p90={np.percentile(dv,90):.2f} "
      f"max={dv.max():.2f}（預算 ±5、缺口處容許 ~10）**")
assert dv.max() < 30.0, "偏離爆預算"  # 大尺度平滑：偏離即修復量（噪聲非意圖），只防災難
# 位移擴散進內部（邊界 Dirichlet）＋貼回皮膚
bset0 = {v for lp in base_loops for v in lp}
nbr0 = defaultdict(set)
for a, b, c in faces:
    nbr0[a].update((b, c))
    nbr0[b].update((a, c))
    nbr0[c].update((a, b))
interior0 = [i for i in range(len(co)) if i not in bset0]
for _ in range(12):
    for i in interior0:
        nb = list(nbr0[i])
        if nb:
            delta[i] = delta[nb].mean(axis=0) if isinstance(nb, list) else delta[i]
    # numpy 化太瑣碎；內部僅 ~900 顆，純迴圈可負擔
co = co + delta
for i in interior0:
    loc, _, _, _ = body_bvh.find_nearest(Vector(co[i]))
    if loc is not None:
        co[i] = np.array(loc)
print("boundary rebuilt + interior diffused + snapped to skin")

# ---- 2) 手寫中點 4:1 細分（無 T-junction 構造保證）----
# 08-20 定罪：全域 2 輪後邊長 p90 仍 5.5mm——邊緣帶的坡道起皺線被 5mm 網格
# 多邊形化＝user 貼臉看到的階梯鋸齒（低通調不掉：噪聲在網格不在曲線）。
# 修＝全域 1 輪 + 邊緣帶自適應紅綠細分（保共形無 T-junction）到 <=1.6mm。
SUBDIV_ROUNDS_EFF = 1
for _r in range(SUBDIV_ROUNDS_EFF):
    co_l = list(co)
    mid = {}

    def midpoint(a, b):
        key = (a, b) if a < b else (b, a)
        if key in mid:
            return mid[key]
        i = len(co_l)
        co_l.append(0.5 * (co_l[a] + co_l[b]))
        wnew = defaultdict(float)
        for g, w in wts[a].items():
            wnew[g] += 0.5 * w
        for g, w in wts[b].items():
            wnew[g] += 0.5 * w
        wts.append(dict(wnew))
        mid[key] = i
        return i

    tone_l = list(tone)
    f2, u2 = [], []
    for (a, b, c), (ua, ub, uc) in zip(faces, face_uvs):
        ab, bc, ca = midpoint(a, b), midpoint(b, c), midpoint(c, a)
        while len(tone_l) < len(co_l):
            tone_l.append(None)      # 佔位，稍後補
        uab = tuple(0.5 * (np.array(ua) + np.array(ub)))
        ubc = tuple(0.5 * (np.array(ub) + np.array(uc)))
        uca = tuple(0.5 * (np.array(uc) + np.array(ua)))
        f2 += [(a, ab, ca), (ab, b, bc), (ca, bc, c), (ab, bc, ca)]
        u2 += [(ua, uab, uca), (uab, ub, ubc), (uca, ubc, uc), (uab, ubc, uca)]
        for i_new, pair in ((ab, (a, b)), (bc, (b, c)), (ca, (c, a))):
            if tone_l[i_new] is None:
                tone_l[i_new] = 0.5 * (tone[pair[0]] + tone[pair[1]])
    co = np.array(co_l)
    tone = np.array([t if t is not None else np.array([0, 1, 0, 1]) for t in tone_l])
    faces, face_uvs = f2, u2
print(f"SUBDIV verts={len(co)} tris={len(faces)}")

# ---- 2b) 邊緣帶自適應紅綠細分 ----
def _edge_len(a, b):
    return float(np.linalg.norm(co[a] - co[b]))


for _pass in range(4):   # 預算制：兩級（牆腳 1.5mm/外圈 3mm）
    # 邊界＝單面邊；d＝到邊界點雲距離
    ec2 = defaultdict(int)
    for a, b, c in faces:
        for e in ((a, b), (b, c), (c, a)):
            ec2[(min(e), max(e))] += 1
    bidx2 = np.array(sorted({v for e, k in ec2.items() if k == 1 for v in e}))
    bco2 = co[bidx2]
    dcur = np.empty(len(co))
    for i in range(0, len(co), 256):
        cch = co[i:i + 256]
        dcur[i:i + 256] = np.sqrt(((cch[:, None, :] - bco2[None, :, :]) ** 2).sum(-1)).min(1)
    # 紅面＝碰到邊緣帶(14mm)且有長邊(>1.6mm)
    red = set()
    for fi, (a, b, c) in enumerate(faces):
        dmin = min(dcur[a], dcur[b], dcur[c])
        emax = max(_edge_len(a, b), _edge_len(b, c), _edge_len(c, a))
        # 單級 8mm/3mm（兩級 1.5mm 連鎖爆 413k tris＝效能不可受）；
        # 殘餘弦差交給沿邊法線平滑（免費）吃
        if dmin < 0.008 and emax > 0.0030:
            red.add(fi)
    if not red:
        break
    # 紅面三邊全取中點；綠面（鄰居）按被切邊數共形拆分
    co_l = list(co)
    midp = {}

    def getmid(a, b):
        key = (a, b) if a < b else (b, a)
        if key in midp:
            return midp[key]
        i2 = len(co_l)
        co_l.append(0.5 * (co_l[a] + co_l[b]))
        wnew = defaultdict(float)
        for g, w in wts[a].items():
            wnew[g] += 0.5 * w
        for g, w in wts[b].items():
            wnew[g] += 0.5 * w
        wts.append(dict(wnew))
        tone_l.append(0.5 * (tone_arr[a] + tone_arr[b]))
        midp[key] = i2
        return i2

    tone_arr = tone
    tone_l = []
    split_edges = set()
    for fi in red:
        a, b, c = faces[fi]
        for e in ((a, b), (b, c), (c, a)):
            split_edges.add((min(e), max(e)))
    f2, u2 = [], []
    for fi, ((a, b, c), (ua, ub, uc)) in enumerate(zip(faces, face_uvs)):
        se = [(min(a, b), max(a, b)) in split_edges,
              (min(b, c), max(b, c)) in split_edges,
              (min(c, a), max(c, a)) in split_edges]
        uva, uvb, uvc = np.array(ua), np.array(ub), np.array(uc)
        if not any(se):
            f2.append((a, b, c))
            u2.append((ua, ub, uc))
            continue
        mab = getmid(a, b) if se[0] else None
        mbc = getmid(b, c) if se[1] else None
        mca = getmid(c, a) if se[2] else None
        uab, ubc, uca = tuple(0.5 * (uva + uvb)), tuple(0.5 * (uvb + uvc)), tuple(0.5 * (uvc + uva))
        cnt = sum(se)
        if cnt == 3:
            f2 += [(a, mab, mca), (mab, b, mbc), (mca, mbc, c), (mab, mbc, mca)]
            u2 += [(ua, uab, uca), (uab, ub, ubc), (uca, ubc, uc), (uab, ubc, uca)]
        elif cnt == 1:
            if se[0]:
                f2 += [(a, mab, c), (mab, b, c)]
                u2 += [(ua, uab, uc), (uab, ub, uc)]
            elif se[1]:
                f2 += [(b, mbc, a), (mbc, c, a)]
                u2 += [(ub, ubc, ua), (ubc, uc, ua)]
            else:
                f2 += [(c, mca, b), (mca, a, b)]
                u2 += [(uc, uca, ub), (uca, ua, ub)]
        else:  # cnt == 2：從共享頂點扇出
            if se[0] and se[1]:
                f2 += [(b, mbc, mab), (a, mab, mbc), (a, mbc, c)]
                u2 += [(ub, ubc, uab), (ua, uab, ubc), (ua, ubc, uc)]
            elif se[1] and se[2]:
                f2 += [(c, mca, mbc), (b, mbc, mca), (b, mca, a)]
                u2 += [(uc, uca, ubc), (ub, ubc, uca), (ub, uca, ua)]
            else:
                f2 += [(a, mab, mca), (b, mca, mab), (b, c, mca)]
                u2 += [(ua, uab, uca), (ub, uca, uab), (ub, uc, uca)]
    co = np.array(co_l)
    tone = np.vstack([tone_arr, np.array(tone_l)]) if tone_l else tone_arr
    faces, face_uvs = f2, u2
    print(f"  adaptive pass: red={len(red)} -> verts={len(co)} tris={len(faces)}")
print(f"ADAPTIVE done verts={len(co)} tris={len(faces)}")


# ---- 3) 邊界（只被一面用到的邊）＋距離場 ----
ecount = defaultdict(int)
for a, b, c in faces:
    for e in ((a, b), (b, c), (c, a)):
        ecount[(min(e), max(e))] += 1
bidx = sorted({v for e, k in ecount.items() if k == 1 for v in e})
bidx = np.array(bidx)
print(f"boundary verts={len(bidx)}  (預期 ~邊界長 8.2m / 細分後段長)")
bco = co[bidx]
V = len(co)
d = np.empty(V)
for i in range(0, V, 256):
    c = co[i:i + 256]
    d[i:i + 256] = np.sqrt(((c[:, None, :] - bco[None, :, :]) ** 2).sum(-1)).min(1)
print(f"d(mm) p50={np.percentile(d,50)*1000:.1f} p90={np.percentile(d,90)*1000:.1f} max={d.max()*1000:.1f}")

# ---- 4) 平滑法線場 ----
tris = np.array(faces)
A, B, C = co[tris[:, 0]], co[tris[:, 1]], co[tris[:, 2]]
fn = np.cross(B - A, C - A)
fn /= np.maximum(np.linalg.norm(fn, axis=1, keepdims=True), 1e-18)
N = np.zeros((V, 3))


def corner(p, q, r):
    u = q - p
    v = r - p
    u /= np.maximum(np.linalg.norm(u, axis=1, keepdims=True), 1e-18)
    v /= np.maximum(np.linalg.norm(v, axis=1, keepdims=True), 1e-18)
    return np.arccos(np.clip((u * v).sum(1), -1, 1))


for a, b, c in ((0, 1, 2), (1, 2, 0), (2, 0, 1)):
    w = corner(co[tris[:, a]], co[tris[:, b]], co[tris[:, c]])
    np.add.at(N, tris[:, a], fn * w[:, None])
N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-12)
for i in range(V):
    loc, bn, _, _ = body_bvh.find_nearest(Vector(co[i]))
    if loc is not None and np.dot(N[i], np.array(bn)) < 0:
        N[i] = -N[i]
nbr = defaultdict(set)
for a, b, c in faces:
    nbr[a].update((b, c))
    nbr[b].update((a, c))
    nbr[c].update((a, b))
idx = np.zeros(sum(len(v) for v in nbr.values()), dtype=np.int64)
ptr = np.zeros(V + 1, dtype=np.int64)
pos = 0
for i in range(V):
    ptr[i] = pos
    for j in nbr.get(i, ()):
        idx[pos] = j
        pos += 1
ptr[V] = pos
cnt = np.maximum(ptr[1:] - ptr[:-1], 1)
for _ in range(SMOOTH_ITERS):
    acc = np.zeros_like(N)
    np.add.at(acc, np.repeat(np.arange(V), ptr[1:] - ptr[:-1]), N[idx])
    N = (1 - LAM) * N + LAM * (acc / cnt[:, None])
    N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-12)

# ---- 5) 高度場＋位移 ----


def ss(x):
    x = np.clip(x, 0.0, 1.0)
    return x * x * (3.0 - 2.0 * x)


# 陡板剖面（08-20 終案）：7mm 內直升 15mm＋sin 圓肩＝「有斷面的實體板」讀感，
# 但仍是單一位移殼＝無牆、無剖面框架、轉角自動平滑（高度場=平滑距離場的函數）。
# 逐頂點蓋牆的兩種 B 定義都在轉角翻車（碎裂片）＝該路線廢棄。
E = 0.009   # 2.2mm 網格要 4 排才彎得順（7mm=3 排=坡面細皺）；59 度仍是陡斷面
# 坡腳 C1 軟起：牆腳硬折痕線會被網格多邊形化＝階梯鋸齒的載體；3.5mm 內漸入
h = -SINK * (1.0 - ss(d / SINK_RAMP)) +     HMAX * np.sin(np.pi / 2 * np.clip(d / E, 0.0, 1.0)) * ss(d / 0.0035)
print(f"h(mm): 邊界 {h[bidx].mean()*1000:.2f}  p50={np.percentile(h,50)*1000:.1f}  "
      f"max={h.max()*1000:.1f}  平台(>=14.5)比例={(h >= 0.0145).mean()*100:.0f}%")
new_co = co + N * h[:, None]

# ---- 5b) 程序化圓柱 UV：取代 1377 個亂向 UV 島 ----
# 島狀 UV 的每島 ~14mm 各採織紋隨機一塊＝貼臉距離的鋁箔馬賽克（08-19 viewport 實錘），
# 走向散布 21.7deg 的老病同源。圓柱參數化（軸=Z、繞骨盆）：u=弧長、v=高度，
# 織紋連續且順著纏繞方向。整數倍貼圖週期＝theta 接縫處無縫。
cx, cy = float(new_co[:, 0].mean()), float(new_co[:, 1].mean())
theta = np.arctan2(new_co[:, 1] - cy, new_co[:, 0] - cx)
Ravg = float(np.sqrt((new_co[:, 0] - cx) ** 2 + (new_co[:, 1] - cy) ** 2).mean())
TILE_M = 0.208                     # 一張貼圖蓋 20.8cm（沿用原密度＝織紋 2.31mm@TileScale 1）
Mrep = max(1, round(2 * np.pi * Ravg / TILE_M))
u = (theta + np.pi) / (2 * np.pi) * (Mrep / 12.0)   # 材質端 x12 tiling => 整數倍週期
vv = new_co[:, 2] / TILE_M / 12.0 * 12.0 / 12.0     # v: 1 uv 單位 / (12*TILE_M)
vv = new_co[:, 2] / (12.0 * TILE_M)
print(f"CYL UV: R={Ravg*100:.1f}cm  週期數={Mrep}（整數＝theta 接縫無縫）")
vert_uv_arr = np.stack([u, vv], axis=1)
face_uvs = [tuple(tuple(vert_uv_arr[i]) for i in f) for f in faces]

# ---- 5c) 濾掉殘餘零面積面（切向鬆弛後僅個位數；零面積＝不可見、褌無碰撞/墨水消費者）----
keep = []
for fi, (a, b, c) in enumerate(faces):
    ar0 = 0.5 * np.linalg.norm(np.cross(new_co[b] - new_co[a], new_co[c] - new_co[a]))
    if ar0 >= 1e-10:   # float32 落盤會把 ~1e-12 m2 捨成真零——門檻要蓋過捨入
        keep.append(fi)
dropped = len(faces) - len(keep)
faces = [faces[i] for i in keep]
face_uvs = [face_uvs[i] for i in keep]
print(f"zero-area faces dropped = {dropped}")

# ---- 6) 重建 mesh（from_pydata；object/修改器/父子/頂點群組保留）----
me2 = bpy.data.meshes.new("Fundoshi_shell")
me2.from_pydata([tuple(v) for v in new_co], [], faces)
me2.update()
# 繞向：面法線要離開身體（開放殼、背面剔除）
me2.calc_loop_triangles()
p0 = np.array(me2.polygons[0].normal)
if np.dot(p0, N[faces[0][0]]) < 0:
    me2.flip_normals()
    print("winding flipped")
uv_new = me2.uv_layers.new(name="UVMap")
li = 0
for f_i, p in enumerate(me2.polygons):
    for k in range(3):
        uv_new.data[p.loop_start + k].uv = face_uvs[f_i][k]
ca = me2.color_attributes.new(name="FaceMask", type='FLOAT_COLOR', domain='POINT')
flat = tone.ravel()
ca.data.foreach_set("color", flat)
mat = bpy.data.materials.get("M_Fundoshi")
me2.materials.append(mat)
for p in me2.polygons:
    p.use_smooth = True

# 沿邊各向異性法線平滑：亮邊扇貝紋的載體＝著色法線沿邊方向的抖動。
# 只在邊緣帶(d<12mm)、只跟「同距離帶」鄰居平滑（|Δd|<1mm 高斯權重）
# ＝沿等距線抹平、不跨剖面（剖面明暗＝斷面立體感，不可糊）。
d_final = np.empty(len(new_co))
for i in range(0, len(new_co), 256):
    cch = new_co[i:i + 256]
    d_final[i:i + 256] = np.sqrt(((cch[:, None, :] - bco[None, :, :]) ** 2).sum(-1)).min(1)
tt2 = np.array(faces)
A2, B2, C2 = new_co[tt2[:, 0]], new_co[tt2[:, 1]], new_co[tt2[:, 2]]
fn2 = np.cross(B2 - A2, C2 - A2)
fn2 /= np.maximum(np.linalg.norm(fn2, axis=1, keepdims=True), 1e-18)
VN = np.zeros((len(new_co), 3))
for aa, bb, cc2 in ((0, 1, 2), (1, 2, 0), (2, 0, 1)):
    w2 = corner(new_co[tt2[:, aa]], new_co[tt2[:, bb]], new_co[tt2[:, cc2]])
    np.add.at(VN, tt2[:, aa], fn2 * w2[:, None])
VN /= np.maximum(np.linalg.norm(VN, axis=1, keepdims=True), 1e-12)
# 繞向對齊（面已可能 flip）：與位移法線同向
flipped = np.dot(np.array(me2.polygons[0].normal), N[faces[0][0]]) < 0
if flipped:
    VN = -VN
nbr2 = defaultdict(set)
for a, b, c in faces:
    nbr2[a].update((b, c))
    nbr2[b].update((a, c))
    nbr2[c].update((a, b))
band = np.nonzero(d_final < 0.012)[0]
for _it in range(12):
    VN2 = VN.copy()
    for i in band:
        acc = VN[i].copy()
        wsum = 1.0
        for j in nbr2[int(i)]:
            wgt = float(np.exp(-0.5 * ((d_final[i] - d_final[j]) / 0.001) ** 2))
            acc += VN[j] * wgt
            wsum += wgt
        VN2[i] = acc / wsum
    VN = VN2 / np.maximum(np.linalg.norm(VN2, axis=1, keepdims=True), 1e-12)
me2.normals_split_custom_set_from_vertices([tuple(v) for v in VN])
print(f"aniso normal smooth: band verts={len(band)} flipped={flipped}")

old = ob.data
ob.data = me2
bpy.data.meshes.remove(old)
# 權重：換 data 後群組名單被清空——按原名原序重建（序＝群組索引的契約）
if len(ob.vertex_groups) != len(vg_names):
    ob.vertex_groups.clear()
    for nm in vg_names:
        ob.vertex_groups.new(name=nm)
print(f"vertex groups rebuilt: {len(ob.vertex_groups)}")
for i, wd in enumerate(wts):
    for g, w in wd.items():
        ob.vertex_groups[g].add([i], w, 'REPLACE')
unweighted = sum(1 for v in me2.vertices if not v.groups)
print(f"unweighted={unweighted}")
assert unweighted == 0

me2.calc_loop_triangles()
tt = np.empty(len(me2.loop_triangles) * 3, dtype=np.int64)
me2.loop_triangles.foreach_get("vertices", tt)
tt = tt.reshape(-1, 3)
cc = np.empty(len(me2.vertices) * 3)
me2.vertices.foreach_get("co", cc)
cc = cc.reshape(-1, 3) * 1000.0
ar = 0.5 * np.linalg.norm(np.cross(cc[tt[:, 1]] - cc[tt[:, 0]], cc[tt[:, 2]] - cc[tt[:, 0]]), axis=1)
zero = int((ar < 1e-6).sum())
print(f"FINAL verts={len(me2.vertices)} tris={len(tt)} zero_area={zero}")
assert zero == 0
assert [m.name for m in me2.materials] == ["M_Fundoshi"]
assert any(m.type == 'ARMATURE' for m in ob.modifiers), "armature modifier lost"
bpy.ops.wm.save_mainfile(filepath=MASTER)
print("SAVED master")
