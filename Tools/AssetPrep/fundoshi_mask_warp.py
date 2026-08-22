"""手繪褌遮罩 → 沿皮膚平滑位移到凹槽（2026-08-23，user 定案：
「以我手繪的樣子為基準，位移與稍微形變至凹槽處；保留後腰走勢」）。

做法（全部在皮膚網格上、UV0 精確）：
  1. 皮膚每頂點取手繪遮罩值（模糊場 σ15mm，與裁切同源）→ inside；
  2. 錨點位移：
     - 大腿帶（正面斜帶）：帶中心線 → 肚/腿摺谷線（每 2cm 一個錨，位移＝谷點−帶中心）；
     - 背面腰帶：頂緣 → 腰窩谷線（|x|≥10 的兩側；薦骨凹中央位移→0）；
     - 胯下帶（屁溝）：位移 0；其餘區域由場自然衰減＝走勢保留；
  3. 位移場 D(v)＝錨點高斯內插（σ=FIELD_SIG），切向投影；
  4. 拉回取樣：new_inside(v) = inside(nearest(v − D(v)))（形狀整體平移/微形變，不重畫）；
  5. 光柵化到 UV（皮膚三角形填色）→ SourceAssets/fundoshi_mask_sharp_v2.png（手繪原檔不動）。
輸入：Saved/FundoshiPlate/groove/groove_centerlines.npz、contact_lines.npz（groove_map/groove_plot2 產出）。
Run: blender --background --python fundoshi_mask_warp.py
"""
import bpy, os, numpy as np
from mathutils import Vector
from mathutils.kdtree import KDTree
ROOT = r"C:\games\Unreal Engine\nice_ink"
MASTER = os.path.join(ROOT, "SourceAssets", "sumo_character_master.blend")
MASK = os.path.join(ROOT, "SourceAssets", "fundoshi_mask_sharp.png")
OUT = os.path.join(ROOT, "SourceAssets", "fundoshi_mask_sharp_v2.png")
GD = os.path.join(ROOT, "Saved", "FundoshiPlate", "groove")
FIELD_SIG = float(os.environ.get("WARP_SIG", "0.08"))
REP = os.path.join(ROOT, "Saved", "fundoshi_mask_warp_report.txt")
rep = []
def P(*a):
    s = " ".join(str(x) for x in a); print(s, flush=True); rep.append(s)

bpy.ops.wm.open_mainfile(filepath=MASTER)
if bpy.context.object and bpy.context.object.mode != 'OBJECT': bpy.ops.object.mode_set(mode='OBJECT')
body = bpy.data.objects["SumoRetopo"]; me = body.data
co = np.empty(len(me.vertices) * 3); me.vertices.foreach_get("co", co); co = co.reshape(-1, 3)
nrm = np.empty(len(me.vertices) * 3); me.vertices.foreach_get("normal", nrm); nrm = nrm.reshape(-1, 3)
uvl = me.uv_layers["UVMap"].data
luv = np.empty(len(uvl) * 2); uvl.foreach_get("uv", luv); luv = luv.reshape(-1, 2)
lvi = np.empty(len(me.loops), np.int64); me.loops.foreach_get("vertex_index", lvi)
n = len(co)

# 1) 遮罩（模糊場）→ 每頂點
img = bpy.data.images.load(MASK, check_existing=True); W, H = img.size
px = np.empty(W * H * 4, np.float32); img.pixels.foreach_get(px); mask_raw = px.reshape(H, W, 4)[:, :, 0].astype(np.float32)
PXMM = 0.617 * W / 4096.0; sig_px = 15.0 * PXMM
valid = np.zeros((H, W), np.float32)
for p_ in me.polygons:
    uvs = luv[p_.loop_start:p_.loop_start + p_.loop_total] * [W, H]
    x0 = max(int(np.floor(uvs[:, 0].min())) - 1, 0); x1 = min(int(np.ceil(uvs[:, 0].max())) + 1, W - 1)
    y0 = max(int(np.floor(uvs[:, 1].min())) - 1, 0); y1 = min(int(np.ceil(uvs[:, 1].max())) + 1, H - 1)
    if x1 >= x0 and y1 >= y0: valid[y0:y1 + 1, x0:x1 + 1] = 1.0
def blur(im, s):
    ky = np.fft.fftfreq(im.shape[0]); kx = np.fft.rfftfreq(im.shape[1])
    G = np.exp(-2 * np.pi ** 2 * s ** 2 * (ky[:, None] ** 2 + kx[None, :] ** 2))
    return np.fft.irfft2(np.fft.rfft2(im) * G, s=im.shape).astype(np.float32)
num = blur(mask_raw * valid, sig_px); den = blur(valid, sig_px)
mask_b = np.where(den > 1e-3, num / np.maximum(den, 1e-3), mask_raw)
u = np.clip((np.mod(luv[:, 0], 1.0) * (W - 1)).astype(int), 0, W - 1); v_ = np.clip((np.mod(luv[:, 1], 1.0) * (H - 1)).astype(int), 0, H - 1)
ms = np.zeros(n); mc = np.zeros(n); np.add.at(ms, lvi, mask_b[v_, u]); np.add.at(mc, lvi, 1)
mval = ms / np.maximum(mc, 1)
inside0 = (mval > 0.5) & (co[:, 2] < 1.3)
P(f"skin verts {n}; inside {inside0.sum()}")

# 2) 錨點
G = np.load(os.path.join(GD, "groove_centerlines.npz"))
C = np.load(os.path.join(GD, "contact_lines_orig.npz"))   # 必須是原始手繪布的接觸線（groove_map 每跑一次會覆寫 contact_lines.npz）
lines = [C[k] for k in C]
allL = np.vstack(lines)
kd_body = KDTree(n)
for i, p in enumerate(co): kd_body.insert(Vector(p), i)
kd_body.balance()
def line_normal(P):
    return np.array([nrm[kd_body.find(Vector(p))[1]] for p in P])
anchors = []   # (pos, disp)
# 2a) 大腿帶：正面（法線 y<-0.2）的兩條邊在 x 切片的中點 → 谷點
front = G["front"] / 100.0          # (x,y,z,H) m
Ln = line_normal(allL); frontL = allL[Ln[:, 1] < -0.2]
for gx, gy, gz, gh in front:
    band = frontL[(np.abs(frontL[:, 0] - gx) < 0.012)]
    if len(band) < 4: continue
    zs = band[:, 2]
    # 兩條邊：z 最低群與最高群（斜帶寬 ~4.5cm）
    lo = band[zs < np.median(zs)]; hi = band[zs >= np.median(zs)]
    if len(lo) == 0 or len(hi) == 0: continue
    center = (lo.mean(0) + hi.mean(0)) / 2
    if abs(center[2] - gz) > 0.10: continue      # 不是同一條帶（安全）
    tap = np.clip((abs(gx) - 0.10) / 0.08, 0, 1); tap = tap * tap * (3 - 2 * tap)   # 胯下端漸縮到 0（|x|<10cm=0、>18cm=全量）
    anchors.append((center, (np.array([gx, gy, gz]) - center) * tap))
n_front = len(anchors)
# 2b) 背面腰帶頂緣 → 腰窩谷線（|x|>=0.10），中央 0
back_top = G["back_top"] / 100.0
backL = allL[Ln[:, 1] > 0.2]
for gx, gy, gz, gh in back_top:
    if os.environ.get("WARP_BACK", "1") == "0": break     # 08-23 user：背帶走勢保留＝背面不動
    if abs(gx) < 0.10: continue
    band = backL[(np.abs(backL[:, 0] - gx) < 0.012) & (backL[:, 2] > 0.72)]
    if len(band) == 0: continue
    top = band[np.argmax(band[:, 2])]
    anchors.append((top, np.array([gx, gy, gz]) - top))
n_back = len(anchors) - n_front
# 2c) 零位移錨：屁溝帶（|x|<0.06, y>0.05, z<0.80）＋ 背面中央頂緣（|x|<0.06, z>0.78）
for p in allL[(np.abs(allL[:, 0]) < 0.06) & (allL[:, 1] > 0.05)]:
    anchors.append((p, np.zeros(3)))
# 零位移錨：胯前中央（前片下緣弧，|x|<12cm、z<56cm、前側）
for p in allL[(np.abs(allL[:, 0]) < 0.12) & (allL[:, 2] < 0.56) & (allL[:, 1] < 0.05)]:
    anchors.append((p, np.zeros(3)))
P(f"anchors: front {n_front}, back {n_back}, zero {len(anchors) - n_front - n_back}")
A = np.array([a[0] for a in anchors]); Dm = np.array([a[1] for a in anchors])
P(f"front disp |d| p50 {np.percentile(np.linalg.norm(Dm[:n_front],axis=1),50)*100:.1f}cm; back disp |d| p50 {(np.percentile(np.linalg.norm(Dm[n_front:n_front+n_back],axis=1),50)*100 if n_back else 0):.1f}cm")

# 3) 位移場＝顯式剖面（08-23 v3）：前帶/背帶各一條隨 x 變化的位移曲線（沿 x σ8cm 平滑），
#    側面以 y 的 smoothstep 混合；帶寬方向位移恆定＝不撕不震盪（RBF/加權平均兩案均退役）。
sel = np.where((co[:, 2] > 0.3) & (co[:, 2] < 1.15))[0]
Af = A[:n_front]; Df_ = Dm[:n_front]; Ab = A[n_front:n_front + n_back]; Db_ = Dm[n_front:n_front + n_back]
def profile(Ax, Dx, x, sig=0.08):
    if len(Ax) == 0: return np.zeros(3)
    w = np.exp(-0.5 * ((Ax[:, 0] - x) / sig) ** 2)
    if w.sum() < 1e-6: return np.zeros(3)
    return (w[:, None] * Dx).sum(0) / w.sum()
def sstep(t): t = np.clip(t, 0, 1); return t * t * (3 - 2 * t)
D = np.zeros_like(co)
for v in sel:
    x, y, z = co[v]
    df = profile(Af, Df_, x) * sstep((abs(x) - 0.10) / 0.08)          # 胯下端漸縮（|x|<10cm → 0）
    db = profile(Ab, Db_, x) * sstep((abs(x) - 0.05) / 0.10)          # 薦骨中央漸縮（|x|<5cm → 0）
    wb = sstep((y + 0.05) / 0.25)                                      # y −5cm→+20cm 前→背
    dv = (1 - wb) * df + wb * db
    dv = dv - np.dot(dv, nrm[v]) * nrm[v]
    D[v] = dv
P(f"field |D| on pelvis p50 {np.percentile(np.linalg.norm(D[sel],axis=1),50)*100:.2f} p90 {np.percentile(np.linalg.norm(D[sel],axis=1),90)*100:.2f} max {np.linalg.norm(D[sel],axis=1).max()*100:.2f} cm")

# 4) 正向噴灑（拉回取樣在位移場發散處會把帶子拉成零寬＝撕裂；正向搬運只拉伸不撕）
SPLAT_R = float(os.environ.get("WARP_SPLAT", "0.009"))
inside1 = inside0.copy()
selset = np.zeros(n, bool); selset[sel] = True
inside1[selset] = False
STEPS = 5
for v in np.where(inside0 & selset)[0]:
    dst = co[v].copy(); step = D[v] / STEPS; cur = v
    for _k in range(STEPS):                      # 分步走＋每步投影回皮膚（測地近似；直線 10cm 會離面 3cm）
        dst = dst + step
        j = kd_body.find(Vector(dst))[1]; dst = co[j]; cur = j
        step = step - np.dot(step, nrm[j]) * nrm[j]
    for (loc, j, d) in kd_body.find_range(Vector(dst), SPLAT_R):
        inside1[j] = True
# 侵蝕回同半徑（測地距：從邊界頂點 Dijkstra，d<SPLAT_R 者剔除＝寬度還原、連通不受稀疏影響）
import heapq, collections
adj0 = collections.defaultdict(list)
for p_ in me.polygons:
    vs = list(p_.vertices)
    for i in range(len(vs)):
        adj0[vs[i]].append(vs[(i + 1) % len(vs)]); adj0[vs[(i + 1) % len(vs)]].append(vs[i])
# 先補小洞（噴灑稀疏處）
for _r in range(2):
    fill0 = inside1.copy()
    for v in np.where(selset & ~inside1)[0]:
        nb = adj0[v]
        if nb and np.mean(inside1[nb]) >= 0.5: fill0[v] = True
    inside1 = fill0
bd = [v for v in np.where(inside1 & selset)[0] if any(not inside1[u] for u in adj0[v])]
dist = {v: 0.0 for v in bd}; pq = [(0.0, v) for v in bd]; heapq.heapify(pq)
while pq:
    d0, u = heapq.heappop(pq)
    if d0 > dist.get(u, np.inf) or d0 > SPLAT_R: continue
    for w_ in adj0[u]:
        if not inside1[w_]: continue
        nd = d0 + float(np.linalg.norm(co[w_] - co[u]))
        if nd < dist.get(w_, np.inf): dist[w_] = nd; heapq.heappush(pq, (nd, w_))
er = inside1.copy()
for v, d_ in dist.items():
    if d_ < SPLAT_R: er[v] = False
inside1 = er
# 小洞補齊：1-ring 多數決一次
import collections
adj = collections.defaultdict(list)
for p_ in me.polygons:
    vs = list(p_.vertices)
    for i in range(len(vs)):
        adj[vs[i]].append(vs[(i + 1) % len(vs)]); adj[vs[(i + 1) % len(vs)]].append(vs[i])
fill = inside1.copy()
for v in sel:
    nb = adj[v]
    if nb and not inside1[v] and np.mean(inside1[nb]) > 0.6: fill[v] = True
inside1 = fill
P(f"inside after warp {inside1.sum()} (was {inside0.sum()})")
np.savez(os.path.join(GD, "warp_debug.npz"), co=co, nrm=nrm, inside0=inside0, inside1=inside1, D=D, A=A, Dm=Dm)

# 5) 光柵化到 UV：每個皮膚三角形以頂點 inside 的重心內插 >0.5 填白；其餘（非骨盆區）抄手繪原樣
out = (mask_raw * 255).astype(np.uint8).copy()
region = np.zeros((H, W), bool)
for p_ in me.polygons:
    vs = list(p_.vertices)
    if co[vs, 2].max() > 1.3 or co[vs, 2].min() < 0.25: continue
    uvs = luv[p_.loop_start:p_.loop_start + p_.loop_total] * [W, H]
    vals = inside1[vs].astype(np.float32)
    x0 = max(int(np.floor(uvs[:, 0].min())), 0); x1 = min(int(np.ceil(uvs[:, 0].max())), W - 1)
    y0 = max(int(np.floor(uvs[:, 1].min())), 0); y1 = min(int(np.ceil(uvs[:, 1].max())), H - 1)
    if x1 < x0 or y1 < y0: continue
    xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
    for i in range(1, len(uvs) - 1):
        a_, b_, c_ = uvs[0], uvs[i], uvs[i + 1]; va, vb, vc = vals[0], vals[i], vals[i + 1]
        den_ = (b_[1] - c_[1]) * (a_[0] - c_[0]) + (c_[0] - b_[0]) * (a_[1] - c_[1])
        if abs(den_) < 1e-12: continue
        w0 = ((b_[1] - c_[1]) * (xs - c_[0]) + (c_[0] - b_[0]) * (ys - c_[1])) / den_
        w1 = ((c_[1] - a_[1]) * (xs - c_[0]) + (a_[0] - c_[0]) * (ys - c_[1])) / den_
        w2 = 1 - w0 - w1
        ins = (w0 >= -0.01) & (w1 >= -0.01) & (w2 >= -0.01)
        val = w0 * va + w1 * vb + w2 * vc
        sub = out[y0:y1 + 1, x0:x1 + 1]; sub[ins] = np.where(val[ins] > 0.5, 255, 0); out[y0:y1 + 1, x0:x1 + 1] = sub
        region[y0:y1 + 1, x0:x1 + 1] |= ins
P(f"rasterized pelvis region px {int(region.sum())}; white frac in region {(out[region]>127).mean():.3f}")
# 邊界去毛：噴灑/侵蝕後的鋸齒邊 → σ6mm 模糊後重新二值化（只在骨盆區）
sm = blur(out.astype(np.float32) / 255.0, 10.0 * PXMM)
out = np.where(region, (sm > 0.5).astype(np.uint8) * 255, out).astype(np.uint8)
im = bpy.data.images.new("mask_v2", W, H, alpha=True)
rgba = np.empty((H, W, 4), np.float32); rgba[..., :3] = (out / 255.0)[..., None]; rgba[..., 3] = 1.0
im.pixels.foreach_set(rgba.ravel()); im.filepath_raw = OUT; im.file_format = 'PNG'; im.save()
P("WROTE", OUT)
open(REP, "w", encoding="utf-8").write("\n".join(rep) + "\nDONE\n")
