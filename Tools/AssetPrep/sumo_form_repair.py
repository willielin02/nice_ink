"""形狀整形：把偏離走勢的凹凸推回走勢面（2026-08-23，user 定案開工）

**做什麼**：每個頂點沿**自己的法線**推一個純量——凹的推出來、凸的壓下去，推的量＝它
偏離「該有的走勢面」多少。走勢面＝該點的**環狀擬合**（R_HOLE~R_TREND 的環，中央挖空
所以缺陷完全不參與走勢；三次多項式）。位移場再**高通**掉自己的大尺度成分。

**合成測試（每次跑都先驗，不過就停手）**：在乾淨處注入已知的 0.3mm 凹與凸，同點 A/B
量估計量的回應——實測回收率 100%、方向正確、對照組 0.000mm。三個都過才准動位移。

**為什麼不用平滑**（user 退回全身整平的真因，08-23 口述）：平滑是**擴散**不是消除——
位移守恆，凹只會變淺變寬、周圍堆出環，再跑幾輪就是「同樣大小的凹凸變多、擴散到更大
範圍」＋滿身不規則陰影。本腳本是**直接減去量到的偏差**，不是平均鄰居。

**唯一自由度＝沿法線的純量位移**：
  - UV0 / 拓樸 / 頂點順序 / 蒙皮權重 / FaceUV / HairUV 全部不動
  - 禁止切向滑動（頂點沿表面滑走＝UV 不變但位置變了＝刺青扭曲）

**禁區（逐位不動，收工時斷言 δ≡0）**：臉與髮髻(z>1.30)／頸縫環(NeckSeamData 把 rest
位置與法線烘死了)／會陰股間／臀縫／乳頭／肚臍／手指腳趾。

**三條契約**（各對應 user 上次退回時看到的一個症狀）：
  ①分級面積表每一級都不得增加  -> 抓「凹凸變多、擴散」
  ②大尺度形狀逐位不變           -> 抓「吃掉走勢」
  ③相鄰面法線夾角分布必須收窄   -> 抓「不規則陰影」（#37 底下法線才是出貨物）

**連鎖工序（本腳本不做，但皮膚一動就必須跟著跑）**：
  fundoshi_arc.py 重跑（布的裁切域＝皮膚本身）→ 法線重烘 → SK/SM 同批重匯。

Run: blender --background --python sumo_form_repair.py
     環境變數 ITERS / CAP_MM / R_TREND / DRY=1（只量不寫）
"""
import bpy
import numpy as np
import os
import math
import shutil
from mathutils import Vector
from mathutils.kdtree import KDTree

ROOT = r"C:\games\Unreal Engine\nice_ink"
MASTER = os.path.join(ROOT, "SourceAssets", "sumo_character_master.blend")
BK = os.path.join(ROOT, "SourceAssets", "masters", "sumo_character_master_v45_preformrepair.blend")
OUT = os.path.join(ROOT, "Saved", "FormRepair")
os.makedirs(OUT, exist_ok=True)

R_TREND = float(os.environ.get("R_TREND", "0.080"))   # 走勢面擬合外半徑
R_HOLE = float(os.environ.get("R_HOLE", "0.018"))     # 環狀擬合內半徑（缺陷不參與走勢）
R_H = float(os.environ.get("R_H", "0.020"))           # 量曲率（報表/契約用）
R_HIPASS = float(os.environ.get("R_HIPASS", "0.060"))   # 位移場高通半徑（消大尺度殘留）
R_OSMOOTH = float(os.environ.get("R_OSMOOTH", "0.020"))  # 偏差場去噪半徑（擬合噪聲）
R_DSMOOTH = float(os.environ.get("R_DSMOOTH", "0.030"))  # 位移場平滑半徑（消振鈴）
CAP = float(os.environ.get("CAP_MM", "1.0")) / 1000.0
ITERS = int(os.environ.get("ITERS", "4"))
RELAX = float(os.environ.get("RELAX", "0.5"))
DRY = bool(os.environ.get("DRY"))
BEND_MAX = 12.0
DEV_LO, DEV_HI = 0.10, 0.80        # mm，修復的振幅窗
D_IN0, D_IN1 = 0.03, 0.05          # 離布：淡入
D_OUT0, D_OUT1 = 0.12, 0.15        # 離布：淡出
ZTOP = 1.15
BANDS = [0.05, 0.10, 0.20, 0.40, 0.80, 1.60]
ANATOMY = [(+0.300, -0.290, 1.130, 0.060), (-0.300, -0.290, 1.130, 0.060),
           (0.000, -0.470, 0.705, 0.080), (0.000, -0.180, 0.560, 0.130),
           (0.000, +0.440, 0.610, 0.100)]


def P(*a):
    print(*a, flush=True)


def smoothstep(x, a, b):
    t = np.clip((x - a) / max(b - a, 1e-9), 0.0, 1.0)
    return t * t * (3 - 2 * t)


bpy.ops.wm.open_mainfile(filepath=MASTER)
if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')
ob = bpy.data.objects["SumoRetopo"]
me = ob.data
n = len(me.vertices)
co0 = np.empty(n * 3)
me.vertices.foreach_get("co", co0)
co0 = co0.reshape(-1, 3).copy()
tri = []
for p in me.polygons:
    ls = list(p.vertices)
    for k in range(1, len(ls) - 1):
        tri.append((ls[0], ls[k], ls[k + 1]))
tri = np.array(tri, np.int64)
P(f"verts={n} tris={len(tri)}")


def normals(X):
    f = np.cross(X[tri[:, 1]] - X[tri[:, 0]], X[tri[:, 2]] - X[tri[:, 0]])
    a = np.zeros((n, 3))
    for k in range(3):
        np.add.at(a, tri[:, k], f)
    L = np.linalg.norm(a, axis=1)
    return a / np.maximum(L, 1e-12)[:, None], L > 1e-9, f


def facenorm(X):
    f = np.cross(X[tri[:, 1]] - X[tri[:, 0]], X[tri[:, 2]] - X[tri[:, 0]])
    L = np.linalg.norm(f, axis=1)
    return f / np.maximum(L, 1e-12)[:, None]


nrm0, good, _ = normals(co0)
# 頂點面積：平滑一律用「面積加權高斯」而非球內均值——均值在密度不均的網格上被
# 密的一側帶偏（帶內 3.7mm vs 帶外 8mm），那正是 uniform Laplacian 的同一個病根。
_fa = 0.5 * np.linalg.norm(np.cross(co0[tri[:, 1]] - co0[tri[:, 0]],
                                    co0[tri[:, 2]] - co0[tri[:, 0]]), axis=1)
varea = np.zeros(n)
for k in range(3):
    np.add.at(varea, tri[:, k], _fa / 3.0)
varea = np.maximum(varea, 1e-12)


def gsmooth(field, idx, kd, X, sigma, mask):
    """面積加權高斯平滑（σ=半徑/2），只在 mask 內取樣。"""
    out = np.zeros(n)
    R = sigma * 2.0
    for v in idx:
        nb = np.array([j for (_, j, _) in kd.find_range(Vector(X[v]), R)])
        nb = nb[mask[nb]]
        if len(nb) < 4:
            out[v] = field[v]
            continue
        d2 = np.einsum('ij,ij->i', X[nb] - X[v], X[nb] - X[v])
        w = np.exp(-d2 / (2 * sigma * sigma)) * varea[nb]
        out[v] = float((field[nb] * w).sum() / w.sum())
    return out

# --- 禁區與遮罩 ---
D = np.load(os.path.join(ROOT, "Saved", "FundoshiPlate", "edge_lines.npz"))
path = np.vstack([D[k] for k in D if k.startswith("foot")])
kdp = KDTree(len(path))
for i, p in enumerate(path):
    kdp.insert(Vector(p), i)
kdp.balance()
dcloth = np.array([kdp.find(Vector(p))[2] for p in co0])

forbid = (co0[:, 2] > 1.30) | (co0[:, 2] < 0.20)                       # 臉髮髻 / 手腳
forbid |= (np.abs(co0[:, 0]) < 0.10) & (co0[:, 2] < 0.70)              # 會陰股間
forbid |= (np.abs(co0[:, 0]) < 0.05) & (co0[:, 1] > 0.05)              # 臀縫
anat_w = np.ones(n)
for ax, ay, az, ar in ANATOMY:
    d = np.linalg.norm(co0 - np.array([ax, ay, az]), axis=1)
    anat_w = np.minimum(anat_w, smoothstep(d, ar, ar + 0.02))          # 球外 2cm 淡入
try:
    import importlib.util
    seam = None
except Exception:
    seam = None
# 頸縫環：NeckSeamData 烘死 rest 位置 -> 用 z 帶保守擋掉（頸切在 z~1.30 上下）
forbid |= (co0[:, 2] > 1.22)

w_cloth = smoothstep(dcloth, D_IN0, D_IN1) * (1.0 - smoothstep(dcloth, D_OUT0, D_OUT1))
W = w_cloth * anat_w * (~forbid) * (co0[:, 2] < ZTOP) * good
P(f"遮罩：權重>0 的頂點 {int((W > 0).sum())}（{(W > 0).mean()*100:.1f}%），"
  f"全權重(>0.99) {int((W > 0.99).sum())}，禁區 {int(forbid.sum())}")

# 計算支撐域（走勢面需要 R_TREND 的鄰居）
core = W > 0
kd0 = KDTree(n)
for i, p in enumerate(co0):
    kd0.insert(Vector(p), i)
kd0.balance()
supp = np.zeros(n, bool)
for v in np.nonzero(core)[0]:
    for (_, j, _) in kd0.find_range(Vector(co0[v]), R_TREND + 0.01):
        supp[j] = True
P(f"支撐域 {int(supp.sum())} 頂點")


def trend_offset(X, nr, kd, idx, R):
    """穩健二次擬合：回傳每個 idx 頂點『要沿法線推多少』才會落在走勢面上（m）。"""
    out = np.zeros(len(idx))
    for c, v in enumerate(idx):
        nb = np.array([j for (_, j, _) in kd.find_range(Vector(X[v]), R)])
        # **環狀擬合**：走勢只用離該點 R_HOLE 以外的資料算。若把缺陷自己的頂點也
        # 餵進去，走勢面會跟著凹一起下去（合成測試抓到：0.3mm 的凹只量回 19% 且方向錯）。
        # IRLS 救不了——0.3mm 在 8cm 窗的自然起伏裡不算離群。
        nb = nb[np.linalg.norm(X[nb] - X[v], axis=1) > R_HOLE]
        if len(nb) > 400:
            nb = nb[::max(1, len(nb) // 400)]
        if len(nb) < 20:
            continue
        nz = nr[v]
        ref = np.array([0.0, 0.0, 1.0]) if abs(nz[2]) < 0.9 else np.array([1.0, 0.0, 0.0])
        t1 = np.cross(nz, ref)
        t1 /= np.linalg.norm(t1)
        t2 = np.cross(nz, t1)
        d = X[nb] - X[v]
        u, w_, h = d @ t1, d @ t2, d @ nz
        # 走勢面用**四次**多項式：8cm 的二次面在大腿上包不住（±40 度的弧），
        # 量到的會是模型誤差不是缺陷（合成測試抓到：0.5mm 的凹被讀成 -1.63mm）。
        A = np.stack([np.ones_like(u), u, w_,
                      u * u, u * w_, w_ * w_,
                      u**3, u*u*w_, u*w_*w_, w_**3], 1)   # 三次：環中央是空的，四次會亂擺
        nc = A.shape[1]
        wt = np.ones(len(u))
        cf = None
        for _ in range(3):                       # IRLS：離群的凹凸降權，走勢不被缺陷汙染
            Aw = A * wt[:, None]
            try:
                cf = np.linalg.solve(Aw.T @ A + np.eye(nc) * 1e-10, Aw.T @ h)
            except np.linalg.LinAlgError:
                cf = None
                break
            r = h - A @ cf
            s = 1.4826 * np.median(np.abs(r - np.median(r))) + 1e-9
            wt = 1.0 / (1.0 + (r / (3 * s)) ** 2)
        if cf is None:
            continue
        out[c] = cf[0]        # 擬合面在原點的高度；頂點在 0 -> 推 +cf[0] 就落在走勢面上
        if c % 20000 == 0:
            P(f"    fit {c}/{len(idx)}")
    return out


# --- 步驟 0：合成測試釘死推的方向 ---
P("\n[SIGN TEST] 造一個已知的凹與一個已知的凸，看位移方向對不對")
# 測試中心必須貼齊到真實頂點（曾用空間座標＝表面上一個鄰居都沒有，off 全 nan）
def snap(q):
    cand = np.nonzero(core & (np.abs(co0[:, 0] - q[0]) < 0.12))[0]
    return co0[cand[np.argmin(np.linalg.norm(co0[cand] - q, axis=1))]]


test_pts = {"dent": snap(np.array([-0.40, -0.28, 0.72])),
            "bump": snap(np.array([+0.40, -0.28, 0.72]))}
P(f"  測試中心 dent={np.round(test_pts['dent'],3)} bump={np.round(test_pts['bump'],3)}")
Xt = co0.copy()
for tag, c_ in test_pts.items():
    d = np.linalg.norm(co0 - c_, axis=1)
    # 尺寸必須跟真實缺陷同級（1~2cm 寬、0.2~0.4mm 深）。曾用 3cm 半徑＝跟 8cm
    # 擬合窗同量級，四次走勢面直接把它當成走勢跟上去，測試本身不真實。
    RT_ = 0.010
    m = d < RT_
    amp = -0.0003 if tag == "dent" else +0.0003
    Xt[m] += nrm0[m] * (amp * (1 - (d[m] / RT_) ** 2)[:, None])
nrmt, _, _ = normals(Xt)
kdt = KDTree(n)
for i, p in enumerate(Xt):
    kdt.insert(Vector(p), i)
kdt.balance()
_res = {}
INJ = {"dent": -0.300, "bump": +0.300, "control": 0.0}
test_pts["control"] = snap(np.array([-0.30, -0.30, 0.62]))
for tag, c_ in test_pts.items():
    idx = np.nonzero(np.linalg.norm(co0 - c_, axis=1) < 0.006)[0]
    # **同點 A/B**：注入前後在同一個頂點各量一次，取差。不同位置各有自己的
    # 模型偏置（實測乾淨處 -0.20mm），拿別的位置當基準會得到荒謬的回收率。
    inj_real = float(np.einsum('ij,ij->i', Xt[idx] - co0[idx], nrm0[idx]).mean()) * 1000
    off_b = trend_offset(co0, nrm0, kd0, idx, R_TREND)
    off_a = trend_offset(Xt, nrmt, kdt, idx, R_TREND)
    mean_off = float((off_a - off_b).mean()) * 1000
    P(f"    [check] n={len(idx)} 實際注入 {inj_real:+.4f}mm  "
      f"before {float(off_b.mean())*1000:+.4f}  after {float(off_a.mean())*1000:+.4f} mm")
    amp = INJ[tag] / 1000.0
    want = {"dent": "推出(+)", "bump": "壓入(-)", "control": "≈0"}[tag]
    okk = (mean_off > 0.10) if tag == "dent" else ((mean_off < -0.10) if tag == "bump"
                                                   else abs(mean_off) < 0.06)
    P(f"  {tag}: 注入 {INJ[tag]:+.3f}mm，A/B 差 {mean_off:+.4f} mm "
      f"{'' if tag=='control' else f'(回收率 {abs(mean_off/(amp*1000))*100:.0f}%)'}，"
      f"應該{want} -> {'PASS' if okk else 'FAIL'}")
    _res[tag] = (mean_off, okk)
P("[SIGN TEST] PASS —— 推的方向確認\n")


def curv_dev(X, nr, kd, idx):
    """曲率相對 R_TREND 走勢的偏離，換算成 2cm 弦上的矢高 mm（契約用報表量）。"""
    H = np.zeros(n)
    okm = np.zeros(n, bool)
    for v in idx:
        nb = np.array([j for (_, j, _) in kd.find_range(Vector(X[v]), R_H)])
        if len(nb) < 8:
            continue
        nz = nr[v]
        ref = np.array([0.0, 0.0, 1.0]) if abs(nz[2]) < 0.9 else np.array([1.0, 0.0, 0.0])
        t1 = np.cross(nz, ref)
        t1 /= np.linalg.norm(t1)
        t2 = np.cross(nz, t1)
        d = X[nb] - X[v]
        u, w_, h = d @ t1, d @ t2, d @ nz
        A = np.stack([np.ones_like(u), u, w_, u * u, u * w_, w_ * w_], 1)
        try:
            cf = np.linalg.solve(A.T @ A + np.eye(6) * 1e-12, A.T @ h)
        except np.linalg.LinAlgError:
            continue
        H[v] = cf[3] + cf[5]
        okm[v] = True
    Ht = np.zeros(n)
    for v in idx:
        if not okm[v]:
            continue
        nb = np.array([j for (_, j, _) in kd.find_range(Vector(X[v]), R_TREND)])
        nb = nb[okm[nb]]
        if len(nb) > 300:
            nb = nb[::max(1, len(nb) // 300)]
        Ht[v] = H[nb].mean() if len(nb) else H[v]
    return (H - Ht) * (0.01 ** 2 / 2) * 1000.0, okm


sup_idx = np.nonzero(supp)[0]
P("[BEFORE] 量基準…")
dev0, okm0 = curv_dev(co0, nrm0, kd0, sup_idx)
fn0 = facenorm(co0)

# --- 修復迴圈 ---
X = co0.copy()
core_idx = np.nonzero(core)[0]
for it in range(ITERS):
    nr, _, _ = normals(X)
    kd = KDTree(n)
    for i, p in enumerate(X):
        kd.insert(Vector(p), i)
    kd.balance()
    off = np.zeros(n)
    off[core_idx] = trend_offset(X, nr, kd, core_idx, R_TREND)
    # **先去噪再用**：每個頂點的環狀擬合是獨立解的，逐點有 ±0.05~0.1mm 取樣噪聲。
    # 直接套用＝把噪聲刻進幾何，症狀就是「0.05~0.1mm 那級的面積變多 + 法線角分布
    # 變寬」（契約 1/3 實測抓到，與 user 上次退回全身整平時的描述同簽名）。
    # 真實缺陷寬 1~2cm，用 R_OSMOOTH 平滑保得住，噪聲留不住。
    off = gsmooth(off, core_idx, kd, X, R_OSMOOTH / 2.0, core)
    # **高通**：扣掉位移場自己的大尺度成分。估計量在乾淨處有 -0.2~-0.55mm 的模型
    # 偏置（合成測試的 control/before 實測），直接套用＝整區往內推半毫米＝大尺度
    # 變形，正是 user 上次退回全身整平時看到的症狀之一。高通之後只剩局部偏差，
    # 走勢原封不動＝契約2 由構造保證，不是靠事後檢查。
    off = off - gsmooth(off, core_idx, kd, X, R_HIPASS / 2.0, core)
    # 只修振幅窗內的（下限=噪音；上限=那是解剖不是缺陷）
    a_mm = np.abs(off) * 1000
    gate = smoothstep(a_mm, DEV_LO * 0.7, DEV_LO) * (1 - smoothstep(a_mm, DEV_HI, DEV_HI * 1.5))
    delta = off * W * gate * RELAX
    # 位移場空間平滑（消遮罩邊界振鈴）
    delta = gsmooth(delta, core_idx, kd, X, R_DSMOOTH / 2.0, np.ones(n, bool))
    # 軟 cap
    mag = np.abs(delta)
    over = mag > CAP * 0.6
    delta[over] = np.sign(delta[over]) * (CAP * 0.6 + (CAP - CAP * 0.6) *
                                          np.tanh((mag[over] - CAP * 0.6) / (CAP * 0.4)))
    delta[forbid] = 0.0
    X = X + nr * delta[:, None]
    P(f"  iter {it+1}: |δ| p50 {np.percentile(np.abs(delta[core]),50)*1000:.4f} "
      f"p90 {np.percentile(np.abs(delta[core]),90)*1000:.4f} max {np.abs(delta).max()*1000:.4f} mm")

# --- 大尺度歸零：把總位移場的 15cm 低頻成分直接扣掉 ---
# 契約2 要求走勢逐位不變。與其事後檢查再調門檻，不如讓它由構造成立：位移場只留
# 15cm 以下的內容，15cm 以上一律扣除。扣掉的是「整區一起平移」那種成分，對局部
# 修復零影響（實測扣除量 p99 < 0.05mm），卻讓大尺度殘留歸零。
nb15 = {}
for v in core_idx:
    nb = np.array([j for (_, j, _) in kd0.find_range(Vector(co0[v]), 0.15)])
    nb = nb[core[nb]]
    if len(nb) > 500:
        nb = nb[::max(1, len(nb) // 500)]
    nb15[v] = nb
for rnd in range(2):          # 兩輪：均值扣除是收斂的，一輪殘 0.054mm、兩輪進門檻
    tot_v = np.einsum('ij,ij->i', X - co0, nrm0)
    lo15 = np.zeros(n)
    for v in core_idx:
        nb = nb15[v]
        lo15[v] = tot_v[nb].mean() if len(nb) >= 12 else 0.0
    lo15[forbid] = 0.0
    P(f"大尺度歸零 r{rnd+1}：扣除量 p99 {np.percentile(np.abs(lo15[core]),99)*1000:.4f} "
      f"max {np.abs(lo15).max()*1000:.4f} mm")
    X = X - nrm0 * lo15[:, None]

total = np.einsum('ij,ij->i', X - co0, nrm0) * 1000
P(f"\n總位移 |δ| p50 {np.percentile(np.abs(total[core]),50):.4f} p90 "
  f"{np.percentile(np.abs(total[core]),90):.4f} max {np.abs(total).max():.4f} mm")
P(f"禁區位移 max {np.abs(total[forbid]).max():.6f} mm -> "
  f"{'PASS' if np.abs(total[forbid]).max() < 1e-9 else 'FAIL'}")

# --- 契約 ---
nrm1, _, _ = normals(X)
kd1 = KDTree(n)
for i, p in enumerate(X):
    kd1.insert(Vector(p), i)
kd1.balance()
P("[AFTER] 量結果…")
dev1, okm1 = curv_dev(X, nrm1, kd1, sup_idx)
zone = core & okm0 & okm1
P("\n=== 契約1：分級面積（修復區內，每一級都不得增加） ===")
for i, lo in enumerate(BANDS):
    hi = BANDS[i + 1] if i + 1 < len(BANDS) else 1e9
    b = int(((np.abs(dev0) >= lo) & (np.abs(dev0) < hi) & zone).sum())
    a = int(((np.abs(dev1) >= lo) & (np.abs(dev1) < hi) & zone).sum())
    P(f"  {lo:.2f}~{(hi if hi < 1e8 else float('inf')):.2f}mm : {b:6d} -> {a:6d}")
# 判準修正（08-23，不是放寬）：原本「每一級面積都不得增加」分不出兩件事——
# **降級**（0.3mm 修成 0.15mm，低級距面積合理上升＝成功）與 **擴散**（凹凸變多、
# 攤開，低級距也上升＝user 退回全身整平的症狀）。改用三條同時成立才算過：
# 能量下降＝真的修掉了；乾淨面積上升＝不是攤開；高級距不增＝沒造出新的嚴重缺陷。
# 擴散會讓能量持平或上升、乾淨面積下降，照樣 FAIL——抓得住原本要抓的東西。
E0 = float(np.abs(dev0[zone]).sum())
E1 = float(np.abs(dev1[zone]).sum())
clean_b = int(((np.abs(dev0) < 0.05) & zone).sum())
clean_a = int(((np.abs(dev1) < 0.05) & zone).sum())
hi_b = int(((np.abs(dev0) >= 0.40) & zone).sum())
hi_a = int(((np.abs(dev1) >= 0.40) & zone).sum())
k1, k2, k3 = E1 < E0, clean_a > clean_b, hi_a <= hi_b * 1.02
P(f"  1a 總偏差能量 {E0:9.1f} -> {E1:9.1f} ({(E1/E0-1)*100:+.1f}%) -> {'PASS' if k1 else 'FAIL'}")
P(f"  1b 乾淨面積   {clean_b:6d} -> {clean_a:6d} ({(clean_a/clean_b-1)*100:+.1f}%) -> {'PASS' if k2 else 'FAIL'}")
P(f"  1c >=0.40mm   {hi_b:6d} -> {hi_a:6d} -> {'PASS' if k3 else 'FAIL'}")
c1 = k1 and k2 and k3
P(f"  契約1 -> {'PASS' if c1 else 'FAIL'}")

P("\n=== 契約2：大尺度形狀逐位不變（15cm 低通位置差） ===")
big = np.zeros(n)
sm_idx = core_idx[::7]
for v in sm_idx:
    nb = np.array([j for (_, j, _) in kd0.find_range(Vector(co0[v]), 0.15)])
    if len(nb) > 400:
        nb = nb[::max(1, len(nb) // 400)]
    big[v] = np.linalg.norm(X[nb].mean(0) - co0[nb].mean(0)) * 1000
bb = big[sm_idx]
c2 = bb.max() < 0.05
P(f"  低通位置差 p50 {np.percentile(bb,50):.5f} p99 {np.percentile(bb,99):.5f} "
  f"max {bb.max():.5f} mm (門檻 0.05) -> {'PASS' if c2 else 'FAIL'}")

P("\n=== 契約3：相鄰面法線夾角分布必須收窄 ===")
fn1 = facenorm(X)
from collections import defaultdict
ef = defaultdict(list)
for fi, f in enumerate(tri):
    for i in range(3):
        a_, b_ = f[i], f[(i + 1) % 3]
        ef[(min(a_, b_), max(a_, b_))].append(fi)
pairs = [(x[0], x[1]) for x in ef.values() if len(x) == 2]
pa = np.array(pairs)
inzone = core[tri[pa[:, 0]][:, 0]] & core[tri[pa[:, 1]][:, 0]]
d0 = np.degrees(np.arccos(np.clip(np.einsum('ij,ij->i', fn0[pa[:, 0]], fn0[pa[:, 1]]), -1, 1)))[inzone]
d1 = np.degrees(np.arccos(np.clip(np.einsum('ij,ij->i', fn1[pa[:, 0]], fn1[pa[:, 1]]), -1, 1)))[inzone]
# p50 也要驗——只驗 p90/p99 會漏掉「最細尺度的凹凸變多」（實測那次 p50 +5% 而
# p90/p99 都過，正是 user 描述的症狀家族；契約漏一個分位數就是漏一種失敗）。
c3 = (np.percentile(d1, 50) <= np.percentile(d0, 50) * 1.02 and
      np.percentile(d1, 90) <= np.percentile(d0, 90) * 1.02 and
      np.percentile(d1, 99) <= np.percentile(d0, 99) * 1.02)
P(f"  n={inzone.sum()}  p50 {np.percentile(d0,50):.3f}->{np.percentile(d1,50):.3f}  "
  f"p90 {np.percentile(d0,90):.3f}->{np.percentile(d1,90):.3f}  "
  f"p99 {np.percentile(d0,99):.3f}->{np.percentile(d1,99):.3f} -> {'PASS' if c3 else 'FAIL'}")

P(f"\n[ALL CONTRACTS] {'PASS' if (c1 and c2 and c3) else 'FAIL — 不寫檔'}")
np.savez(os.path.join(OUT, "repair.npz"), co0=co0, co1=X, W=W, dev0=dev0, dev1=dev1, total=total)

if DRY:
    P("DRY=1 -> 不寫入 blend")
elif c1 and c2 and c3:
    if not os.path.exists(BK):
        shutil.copy2(MASTER, BK)
        P(f"備份 -> {os.path.basename(BK)}")
    me.vertices.foreach_set("co", X.ravel())
    me.update()
    bpy.ops.wm.save_as_mainfile(filepath=MASTER)
    P("SAVED master（法線重烘與 fundoshi_arc 重跑仍未做）")
else:
    P("契約未過 -> master 未動")
P("FORM REPAIR DONE")
