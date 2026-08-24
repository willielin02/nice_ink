"""全身「斑」普查儀（2026-08-24，唯讀）：把玩家會看到的每一塊斑列成清單。

**量哪一份法線（2026-08-24 更正，血價）**：引擎匯入設定是 `FBXNIM_COMPUTE_NORMALS`
＝**主檔的柔化 split normal 被丟棄、法線由引擎從幾何重算**。本檔一度誤用主檔法線＝
量了一份沒上畫面、且本來就設計來藏缺陷的資料。現在讀重算法線（面積加權面法線平均）。

**量什麼（為什麼是這個量）**：08-24 引擎內 A/B 已定罪——皮膚是 fullbright，亮度
＝f(N·V)，N＝**自訂 split normal**；把假光關掉整具身體就變成純色剪影（亮度起伏
降 62~80%），而材質的斑駁/細節/平鋪法線三層本來就是關的。所以**斑 = 法線場在
小尺度上的抖動**，跟「幾何有多凹」不是同一個量（柔化法線就是刻意讓兩者分家）。
    rough(v) = angle( n_v , 拓樸 2 環鄰域的平均 n )    單位：度
鄰域用**拓樸環不用半徑球**：肚下緣/股間/乳下這些「兩片皮膚靠很近」的地方，
半徑球會把對面那片的法線平均進來，量出 p50 32 度的假訊號（08-24 實踩）。

**換算成畫面**（材質實值 HeadlightFloor=0.55 / Power=1.5 / Brightness=2.0）：
    B(θ) = 2.0 * (0.55 + 0.45 * cos(θ)^1.5)
    最敏感視角 cos²θ=1/3 → θ=54.7°，此處 |dB/B| = 0.560 * δ(rad) = **0.98% / 度**
所以一塊 3° 的斑 ≈ 3% 的明暗差（引擎實測：站立胸前淨值 1.17% ↔ 離線 p50 1.86°，同量級）。
報表同時給「度」與「最壞視角下的亮度差 %」，不要只給其中一個。

**門檻不拍腦袋**：對照組＝**被治療過的髖/大腿**（離褌布 2~8cm，08-22~24 三批整平的
全量作用域，也是 user 從沒抱怨過的區域）。門檻 = 對照組 p99 × MARGIN。
校準用左半邊、驗證用右半邊（同一塊拿來校又拿來驗＝套套邏輯，p99 必然剛好 1% 命中）。

**兩條自檢，上下限都驗**（本專案吃過最多虧的形態＝只驗一邊）：
  ①下限：注入已知假斑——在乾淨底上驗「線性 R²>0.99」並讀出回收率（報表的度 ÷ 回收率
    ＝真實傾角），在真實場上驗「1° 的假斑就越得過門檻」
  ②上限：對照組（held-out 右半邊）的命中率必須夠低
兩條都 PASS，普查表才可信。

**已知的限制（不要拿它當嚴重度排名）**：2 環在細網格上＝10mm、粗網格上＝26mm，所以
**跨區比較被網格密度混淆**——治療過的髖/大腿邊長 5.2mm，胸 13.7mm／上背 13.3mm／小腿 16.6mm。
門檻是拿細網格區校的 ⇒ 對粗網格區系統性高估。本表請當**位置圖**用；嚴重度要用引擎像素
（robo_skin_spots.py）或先把密度補齊再重量。試過的死路：固定 15mm 物理半徑鄰域——粗區
一顆球裡只有 4~6 個鄰居，量出肚子 p50 10.4° 這種明顯錯的數（該區目視是乾淨的）。

**真解剖不算斑**：乳頭/肚臍/股溝/臀縫用排除球；大尺度摺痕（bend>BEND_MAX）另計，
列在報表尾巴可覆查——不是丟掉。

用法：blender --background --python sumo_spot_census.py
環境變數：BLEND / MARGIN(1.0) / MIN_AREA_CM2(0.30) / RENDER=1
產出：Saved/SpotCensus/spots.txt（排名表＋分區小結＋總帳）＋ spots.npz ＋ RENDER=1 時 census_*.png
"""
import bpy
import os
import math
import numpy as np
from mathutils import Vector
from mathutils.kdtree import KDTree

ROOT = r"C:\games\Unreal Engine\nice_ink"
OUT = os.path.join(ROOT, "Saved", "SpotCensus")
os.makedirs(OUT, exist_ok=True)
BLEND = os.environ.get("BLEND", os.path.join(ROOT, "SourceAssets", "sumo_character_master.blend"))
MARGIN = float(os.environ.get("MARGIN", "1.0"))
MIN_AREA_CM2 = float(os.environ.get("MIN_AREA_CM2", "0.30"))
BEND_MAX = float(os.environ.get("BEND_MAX", "12.0"))     # 度；超過＝真摺痕
ZLO, ZHI = 0.30, 1.28                                     # 不掃頭臉（另有臉管線）與腳掌
DEG_TO_PCT = 0.98                                         # 最壞視角下 1° 法線偏差 ≈ 0.98% 亮度差
RING_BEND = 9                                             # 大尺度法線散布的環數（~3.5cm）

# 真解剖排除球 (x, y, z, r) 公尺
ANATOMY = [
    (+0.300, -0.290, 1.130, 0.060),   # 左乳頭
    (-0.300, -0.290, 1.130, 0.060),   # 右乳頭
    (+0.000, -0.470, 0.705, 0.075),   # 肚臍
    (+0.000, -0.180, 0.560, 0.130),   # 胯下
    (+0.000, +0.440, 0.610, 0.100),   # 臀縫
]

rep = []


def P(*a):
    s = " ".join(str(x) for x in a)
    print(s, flush=True)
    rep.append(s)


bpy.ops.wm.open_mainfile(filepath=BLEND)
ob = bpy.data.objects["SumoRetopo"]
me = ob.data
n = len(me.vertices)
co = np.empty(n * 3)
me.vertices.foreach_get("co", co)
co = co.reshape(-1, 3)

lv = np.empty(len(me.loops), np.int64)
me.loops.foreach_get("vertex_index", lv)
luv = np.empty(len(me.loops) * 2)
me.uv_layers[0].data.foreach_get("uv", luv)
luv = luv.reshape(-1, 2)
vuv = np.zeros((n, 2))
cnt = np.zeros(n)
np.add.at(vuv, lv, luv)
np.add.at(cnt, lv, 1)
vuv /= np.maximum(cnt, 1)[:, None]

ln = np.empty(len(me.loops) * 3)
me.corner_normals.foreach_get("vector", ln)
ln = ln.reshape(-1, 3)
sacc = np.zeros((n, 3))
np.add.at(sacc, lv, ln)
sn = sacc / np.maximum(np.linalg.norm(sacc, axis=1), 1e-12)[:, None]

tri = []
for p in me.polygons:
    ls = list(p.vertices)
    for k in range(1, len(ls) - 1):
        tri.append((ls[0], ls[k], ls[k + 1]))
tri = np.array(tri, np.int64)
fn = np.cross(co[tri[:, 1]] - co[tri[:, 0]], co[tri[:, 2]] - co[tri[:, 0]])
fa = np.linalg.norm(fn, axis=1) * 0.5
acc = np.zeros((n, 3))
for k in range(3):
    np.add.at(acc, tri[:, k], fn)
gl = np.linalg.norm(acc, axis=1)
good = gl > 1e-9
gn = acc / np.maximum(gl, 1e-12)[:, None]
varea = np.zeros(n)                      # 頂點面積（三角形面積 1/3）
for k in range(3):
    np.add.at(varea, tri[:, k], fa / 3.0)

# ---- 拓樸鄰接（CSR 形式，之後所有環平均都走它；半徑球會跨空隙）----
E = np.empty(len(me.edges) * 2, np.int64)
me.edges.foreach_get("vertices", E)
E = E.reshape(-1, 2)
both = np.concatenate([E, E[:, ::-1]], 0)
order = np.argsort(both[:, 0], kind="stable")
both = both[order]
nb_flat = both[:, 1]
deg = np.bincount(both[:, 0], minlength=n)
indptr = np.zeros(n + 1, np.int64)
np.cumsum(deg, out=indptr[1:])


def ring_mean(vec, rings):
    """把 (n,3) 的向量場沿拓樸環平均 rings 次（≈ 測地高斯，跨不過空隙）"""
    x = vec.copy()
    for _ in range(rings):
        s = np.add.reduceat(x[nb_flat], indptr[:-1], axis=0)
        s[deg == 0] = x[deg == 0]
        x = s / np.maximum(deg, 1)[:, None]
        x /= np.maximum(np.linalg.norm(x, axis=1, keepdims=True), 1e-12)
    return x


def ang(a, b):
    d = np.clip(np.einsum("ij,ij->i", a, b), -1.0, 1.0)
    return np.degrees(np.arccos(d))


P("網格 %d 頂點 / %d 三角形；平均邊長 %.2f mm" %
  (n, len(tri), np.linalg.norm(co[E[:, 0]] - co[E[:, 1]], axis=1).mean() * 1000))

# ---- 主指標：小尺度法線抖動（2 環 ≈ 8mm）----
# **2026-08-24 重大更正**：這裡本來讀 `sn`（主檔的自訂 split normal / 柔化法線轉印）。
# 錯了——`Tools/AssetPrep/ue_import_sumo.py` 匯入 SK/SM 時明寫
# `normal_import_method = FBXNIM_COMPUTE_NORMALS`：**引擎把主檔法線整份丟掉、自己從幾何重算**。
# 所以柔化法線根本沒上畫面，而且它本來的職責就是「把幾何缺陷藏起來」⇒ 拿它當尺
# 等於在量一份被丟棄、又刻意抹平過的資料。user 圈選的兩點在舊尺下是 0/142 超標，
# 換成引擎真正在用的法線後其中一點變成 5/142、峰值 0.46°→2.21°（4.8 倍）。
# 鐵則：**尺要量 GPU 拿到的那份資料，不是作者檔裡的那份。**
rough = ang(gn, ring_mean(gn, 2))
rough_soft = ang(sn, ring_mean(sn, 2))   # 保留舊值供對賬，不參與判定
# ---- 摺痕閘：大尺度法線散布（9 環 ≈ 3.5cm）----
gn9 = np.zeros((n, 3))
x = gn.copy()
acc9 = gn.copy()
for _ in range(RING_BEND):
    s = np.add.reduceat(x[nb_flat], indptr[:-1], axis=0)
    s[deg == 0] = x[deg == 0]
    x = s / np.maximum(deg, 1)[:, None]
    acc9 = x.copy()
    x = x / np.maximum(np.linalg.norm(x, axis=1, keepdims=True), 1e-12)
bend = np.degrees(np.arccos(np.clip(np.linalg.norm(acc9, axis=1), 0, 1)))

# ---- 離褌布的距離（治療區＝對照組的定義）----
fund = bpy.data.objects.get("Fundoshi")
if fund:
    fc = np.empty(len(fund.data.vertices) * 3)
    fund.data.vertices.foreach_get("co", fc)
    fc = fc.reshape(-1, 3)
    kdf = KDTree(len(fc))
    for i, p in enumerate(fc):
        kdf.insert(Vector(p), i)
    kdf.balance()
    dcloth = np.array([kdf.find(Vector(p))[2] for p in co])
else:
    dcloth = np.full(n, 9.9)
P("離褌布距離：中位 %.1f cm" % (np.median(dcloth) * 100))

anat = np.zeros(n, bool)
for (ax, ay, az, ar) in ANATOMY:
    anat |= np.linalg.norm(co - np.array([ax, ay, az]), axis=1) < ar

body = good & (co[:, 2] > ZLO) & (co[:, 2] < ZHI) & (np.abs(co[:, 0]) < 0.62)
flat_large = bend < BEND_MAX
# 對照組＝治療過、外露、非解剖、大尺度平坦的髖/大腿皮膚
ctrl = body & flat_large & ~anat & (dcloth > 0.02) & (dcloth < 0.08)
calib = ctrl & (co[:, 0] > 0)
valid = ctrl & (co[:, 0] < 0)
THR = float(np.percentile(rough[calib], 99)) * MARGIN
P("對照組（離布 2~8cm 的治療區）：校準 %d 頂點 p50 %.3f p90 %.3f p99 %.3f 度"
  % (calib.sum(), *np.percentile(rough[calib], [50, 90, 99])))
P("門檻 THR = 對照組 p99 x %.2f = **%.3f 度**（≈ %.2f%% 亮度差）" % (MARGIN, THR, THR * DEG_TO_PCT))

# ---- 分群：超標且非解剖且大尺度平坦，沿網格邊連通 ----
hot = body & (rough > THR) & ~anat & flat_large
fold = body & (rough > THR) & (anat | ~flat_large)     # 解剖/摺痕，另計


def components(mask):
    """最陡上升分水嶺：每個超標頂點沿 rough 往上爬，落到哪個局部極大就屬於哪一塊。
    純連通分量不行——門檻掃出 7.5% 的皮膚時，整片胸腹會連成「一塊 129cm 的斑」
    ＝把地圖當成清單（08-24 第一版實踩）。斑是「一個亮暗中心」，不是「一片連通域」。"""
    up = np.arange(n)
    for v in np.where(mask)[0]:
        nb = nb_flat[indptr[v]:indptr[v + 1]]
        nb = nb[mask[nb]]
        if len(nb) == 0:
            continue
        j = int(nb[np.argmax(rough[nb])])
        if rough[j] > rough[v]:
            up[v] = j
    root = np.arange(n)
    for v in np.where(mask)[0]:
        x = v
        for _ in range(4096):
            if up[x] == x:
                break
            x = up[x]
        root[v] = x
    # 峰太近的合併（同一塊斑被兩個極大切開）
    peaks = np.unique(root[mask])
    if len(peaks):
        kp = KDTree(len(peaks))
        for i, pk in enumerate(peaks):
            kp.insert(Vector(co[pk]), i)
        kp.balance()
        remap = {}
        for i, pk in enumerate(peaks):
            best = pk
            for (_, j, dd) in kp.find_range(Vector(co[pk]), MERGE_R):
                if rough[peaks[j]] > rough[best]:
                    best = peaks[j]
            remap[pk] = best
        for _ in range(6):
            remap = {k: remap.get(v, v) for k, v in remap.items()}
        root = np.array([remap.get(r, r) for r in root])
    uniq = {p: i for i, p in enumerate(np.unique(root[mask]))}
    lab = -np.ones(n, np.int64)
    idx = np.where(mask)[0]
    lab[idx] = [uniq[root[v]] for v in idx]
    return lab, len(uniq)


MERGE_R = 0.02   # 2cm 內的兩個峰視為同一塊斑
lab, ncomp = components(hot)
P("超標頂點 %d（%.1f%% 的可見皮膚）→ 連通群 %d 個" %
  (hot.sum(), 100.0 * hot.sum() / max(body.sum(), 1), ncomp))

# ---- 每群的高度振幅（相對 3.5cm 二次擬合面）：只對群內點算，便宜 ----
kd = KDTree(n)
for i, p in enumerate(co):
    kd.insert(Vector(p), i)
kd.balance()


def amp_at(v, R=0.035):
    nb = np.array([j for (_, j, _) in kd.find_range(Vector(co[v]), R)])
    nb = nb[good[nb]]
    # 只留法線同向的鄰居：肚下緣/股間/乳下兩片皮膚靠很近，半徑球會抓到對面那片
    nb = nb[gn[nb] @ gn[v] > 0.5]
    if len(nb) < 8:
        return 0.0
    nz = gn[v]
    ref = np.array([0.0, 0.0, 1.0]) if abs(nz[2]) < 0.9 else np.array([1.0, 0.0, 0.0])
    t1 = np.cross(nz, ref)
    t1 /= np.linalg.norm(t1)
    t2 = np.cross(nz, t1)
    d = co[nb] - co[v]
    u, w, h = d @ t1, d @ t2, d @ nz
    A = np.stack([np.ones_like(u), u, w, u * u, u * w, w * w], 1)
    try:
        cf = np.linalg.solve(A.T @ A + np.eye(6) * 1e-12, A.T @ h)
    except np.linalg.LinAlgError:
        return 0.0
    return -cf[0] * 1000.0


def part(p):
    x, y, z = p
    side = "左" if x > 0.05 else ("右" if x < -0.05 else "中")
    face = "背" if y > 0.06 else ("前" if y < -0.06 else "側")
    reg = ("胸" if z > 1.05 else "腰" if z > 0.95 else "髖" if z > 0.82 else
           "大腿上" if z > 0.60 else "大腿下" if z > 0.42 else "小腿")
    return reg + face + side


def blobs_from(lab, ncomp, tag):
    rows = []
    for c in range(ncomp):
        vs = np.where(lab == c)[0]
        area = varea[vs].sum() * 1e4          # cm²
        if area < MIN_AREA_CM2:
            continue
        pk = int(vs[np.argmax(rough[vs])])
        ext = np.linalg.norm(co[vs] - co[vs].mean(0), axis=1).max() * 200  # 直徑 cm
        rows.append(dict(tag=tag, n=len(vs), area=area, ext=ext,
                         peak=float(rough[pk]), mean=float(rough[vs].mean()),
                         amp=float(amp_at(pk)), pos=co[vs].mean(0), uv=vuv[pk],
                         part=part(co[vs].mean(0)), dcloth=float(dcloth[pk])))
    rows.sort(key=lambda r: -(r["peak"] * DEG_TO_PCT) * math.sqrt(r["area"]))
    return rows


spots = blobs_from(lab, ncomp, "spot")
lab_f, nf = components(fold)
folds = blobs_from(lab_f, nf, "fold")

# ---------------------------------------------------------------- 自檢
P("")
P("======== 自檢（兩條都要 PASS，下面的普查表才可信）========")
# ①下限：注入已知 3.0 度的假斑（在對照組最乾淨處），必須被抓到
clean = np.where(valid & (rough < THR * 0.5))[0]
seed = int(clean[np.argmax(varea[clean])])
INJ_R = 0.015
d = np.linalg.norm(co - co[seed], axis=1)
w = np.clip(1.0 - (d / INJ_R) ** 2, 0, 1) ** 2
axis = np.cross(sn[seed], np.array([0.0, 0.0, 1.0]))
axis /= max(np.linalg.norm(axis), 1e-9)
near = d < INJ_R * 1.2
base = float(rough[near].max())
AX = np.tile(axis, (n, 1))


def inject(field, deg):
    th = np.radians(deg) * w
    out = (field * np.cos(th)[:, None] + np.cross(AX, field) * np.sin(th)[:, None]
           + AX * (np.einsum("j,ij->i", axis, field) * (1 - np.cos(th)))[:, None])
    return out / np.maximum(np.linalg.norm(out, axis=1, keepdims=True), 1e-12)


# 線性度要在**乾淨底**上量：角度不是純量、注入量與既有噪聲是向量疊加，
# 直接在真實法線場上「量回峰值減基準」會被噪聲壓成非線性（第一版實踩 R²=0.93）。
sn_clean = ring_mean(gn, 6)
inj = np.array([1.0, 2.0, 4.0, 8.0])
r = np.array([float(ang(inject(sn_clean, d), ring_mean(inject(sn_clean, d), 2))[near].max())
              for d in inj])
slope = float((inj * r).sum() / (inj * inj).sum())
r2 = 1.0 - float((((r - slope * inj) ** 2).sum())) / max(float(((r - r.mean()) ** 2).sum()), 1e-12)
# 偵測力則在**真實場**上驗：最小的那個假斑要真的越過門檻
# 偵測底線＝門檻 ÷ 回收率＝「真實傾角要多大才抓得到」。用比它大一點的注入量驗，
# 並把底線明寫出來——儀器的盲區必須是公開數字，不是預設它什麼都抓得到。
floor_deg = THR / max(slope, 1e-6)
det = float(ang(inject(gn, floor_deg * 1.4), ring_mean(inject(gn, floor_deg * 1.4), 2))[near].max())
ok1 = (det > THR) and (0.35 < slope < 1.25) and (r2 > 0.99)   # 幾何法線的乾淨底不如柔化法線平滑，0.99 是它的實際線性度
P("[自檢1 下限] 乾淨底注入 1/2/4/8 度 → 量回 %.2f/%.2f/%.2f/%.2f 度" % tuple(r))
P("            回收率 %.3f  線性 R²=%.5f" % (slope, r2))
P("            **偵測底線 %.2f 度（＝%.2f%% 亮度差）以下的斑，本儀器抓不到**" % (floor_deg, floor_deg * DEG_TO_PCT))
P("            真實場注入 %.2f 度：峰值 %.2f 度 vs 門檻 %.2f 度 → %s"
  % (floor_deg * 1.4, det, THR, "抓得到" if det > THR else "抓不到"))
P("            ⇒ 報表的「峰值度」除以 %.3f ≈ 真實表面傾角  →  %s"
  % (slope, "PASS" if ok1 else "FAIL"))
# ②上限：held-out 對照組的命中率
rate = 100.0 * (rough[valid] > THR).sum() / max(valid.sum(), 1)
ok2 = rate < 3.0
P("[自檢2 上限] held-out 對照組（右半治療區）命中率 %.2f%% → %s" % (rate, "PASS" if ok2 else "FAIL"))
P("[自檢] %s" % ("BOTH PASS" if (ok1 and ok2) else "NOT TRUSTWORTHY"))

# ---------------------------------------------------------------- 報表
P("")
P("======== 分區總帳（可見皮膚；超標＝rough > %.3f 度）========" % THR)
P("⚠ 密度混淆：本表的『度』是「頂點 vs 2 環鄰域」——2 環在細網格上是 10mm、粗網格上是 26mm，")
P("  所以粗區的數字**同時**含真缺陷與粗取樣，兩者用這把尺分不開。治療過的髖/大腿邊長 5.2mm、")
P("  胸 13.7mm、上背 13.3mm、小腿 16.6mm ⇒ 跨區排名要當『哪裡該看』，不要當『嚴重度倍數』。")
P("%-12s %8s %10s %10s %9s %9s %9s" % ("部位", "頂點", "超標比例", "斑面積cm²", "p90(度)", "p99(度)", "邊長mm"))
regs = {}
for v in np.where(body)[0]:
    regs.setdefault(part(co[v]), []).append(v)
tot_area = 0.0
rows_reg = []
for k, vs in regs.items():
    vs = np.array(vs)
    if len(vs) < 200:
        continue
    hi = hot[vs]
    a = varea[vs][hi].sum() * 1e4
    tot_area += a
    edge = math.sqrt(varea[vs].mean() * 1e4 / 0.866) * 10.0
    rows_reg.append((k, len(vs), 100.0 * hi.mean(), a,
                     float(np.percentile(rough[vs], 90)), float(np.percentile(rough[vs], 99)), edge))
for r in sorted(rows_reg, key=lambda r: -r[2]):
    P("%-12s %8d %9.1f%% %10.1f %9.3f %9.3f %9.1f" % r)
P("")
P("**總帳**：可見皮膚 %.0f cm²，斑總面積 **%.0f cm²（%.1f%%）**，斑 %d 塊（≥%.2f cm²）"
  % (varea[body].sum() * 1e4, tot_area, 100.0 * tot_area / max(varea[body].sum() * 1e4, 1e-9),
     len(spots), MIN_AREA_CM2))

P("")
P("======== 斑清單（依「亮度差 × √面積」排序）========")
P("%3s %-12s %8s %7s %8s %8s %8s  %-22s %s" %
  ("#", "部位", "面積cm²", "直徑cm", "峰值度", "亮度差%", "高度mm", "UV", "座標(m)"))
for i, r in enumerate(spots[:60], 1):
    P("%3d %-12s %8.1f %7.1f %8.2f %8.2f %8.3f  uv(%.4f,%.4f)  (%+.3f,%+.3f,%+.3f)"
      % (i, r["part"], r["area"], r["ext"], r["peak"], r["peak"] * DEG_TO_PCT, r["amp"],
         r["uv"][0], r["uv"][1], r["pos"][0], r["pos"][1], r["pos"][2]))
if len(spots) > 60:
    P("... 另有 %d 塊未列出（全部在 spots.npz）" % (len(spots) - 60))

P("")
P("======== 解剖摺痕（被閘門擋下、列出可覆查；不計入總帳）========")
for i, r in enumerate(folds[:15], 1):
    P("%3d %-12s %8.1f cm² 峰值 %.2f 度  (%+.3f,%+.3f,%+.3f)"
      % (i, r["part"], r["area"], r["peak"], r["pos"][0], r["pos"][1], r["pos"][2]))

np.savez(os.path.join(OUT, "spots.npz"), co=co, uv=vuv, rough=rough, bend=bend,
         hot=hot, lab=lab, varea=varea, dcloth=dcloth, thr=THR,
         spot_pos=np.array([r["pos"] for r in spots]) if spots else np.zeros((0, 3)),
         spot_peak=np.array([r["peak"] for r in spots]) if spots else np.zeros(0),
         spot_area=np.array([r["area"] for r in spots]) if spots else np.zeros(0))

# ---------------------------------------------------------------- 熱點圖
if os.environ.get("RENDER"):
    ca = me.color_attributes.get("Census") or me.color_attributes.new("Census", 'FLOAT_COLOR', 'POINT')
    t = np.clip((rough - THR) / max(THR * 3.0, 1e-6), 0, 1)
    cols = np.zeros((n, 4))
    cols[:, 3] = 1
    is_fold = (anat | ~flat_large) & body
    cols[:, 0] = np.where(hot, 0.65 + 0.35 * t, np.where(is_fold, 0.42, 0.68))
    cols[:, 1] = np.where(hot, 0.62 * (1 - t), np.where(is_fold, 0.46, 0.68))
    cols[:, 2] = np.where(hot, 0.58 * (1 - t), np.where(is_fold, 0.56, 0.66))
    ca.data.foreach_set("color", cols.ravel())
    me.attributes.active_color_name = "Census"
    for o in bpy.data.objects:
        if o.name != "SumoRetopo":
            o.hide_render = True
    ob.hide_render = False
    sc = bpy.context.scene
    sc.render.engine = 'BLENDER_WORKBENCH'
    sh = sc.display.shading
    sh.light = 'FLAT'
    sh.color_type = 'VERTEX'
    sh.show_specular_highlight = False
    sc.render.resolution_x = 1500
    sc.render.resolution_y = 1500
    sc.render.film_transparent = True
    cd = bpy.data.cameras.new("CenCam")
    cam = bpy.data.objects.new("CenCam", cd)
    bpy.context.collection.objects.link(cam)
    sc.camera = cam
    cd.lens = 40
    for tag, dv in (("front", (0, -1, 0.18)), ("back", (0, 1, 0.18)),
                    ("left", (1, -0.15, 0.15)), ("right", (-1, -0.15, 0.15))):
        f = np.array([0.0, 0.0, 0.86])
        v = np.array(dv, float)
        v /= np.linalg.norm(v)
        eye = f + v * 3.2
        cam.location = Vector(eye.tolist())
        cam.rotation_euler = Vector((f - eye).tolist()).to_track_quat('-Z', 'Y').to_euler()
        sc.render.filepath = os.path.join(OUT, "census_%s.png" % tag)
        bpy.ops.render.render(write_still=True)
        P("WROTE census_%s.png" % tag)

open(os.path.join(OUT, "spots.txt"), "w", encoding="utf-8").write("\n".join(rep) + "\nDONE\n")
P("WROTE " + os.path.join(OUT, "spots.txt"))
