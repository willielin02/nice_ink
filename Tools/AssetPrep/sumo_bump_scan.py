"""全身小尺度不平整偵測器（2026-08-23，user 定案「一次把同量級的不平整都糾出來」）

**為什麼既有的尺看不到這一族**（08-23 血價，NiMark 紫筆圈選點實測）：
  lump_R3（R=3cm 帶通、滿檔 1.5mm）對「1~2cm 尺度、0.15~0.5mm 深」的淺凹構造上不敏感——
  實測標記點 lump 只有 -0.31mm ＝同高腿環的 |lump| 中位數，完全平庸；而它在 viewport
  上看得見。真兇機制 08-22 已定罪：**皮膚殘餘 0.2~0.5mm 微起伏 × 掠射遮擋放大 30~60x**。

**量什麼（08-23 二度修正）**：主指標＝**相對局部二次擬合面的殘餘高度 amp**（真實弧度
扣掉後剩下的就是缺陷；正凸負凹，mm）。原本以法線偏差 tilt 為主指標——理論上它才是頭燈
假光下真正上畫面的量，但實測**已知缺陷 tilt 1.65 度 < 治療區自己的 p90 2.20 度**＝根本
分不開（自檢 1 抓到）。tilt 降為報表欄位。舊敘述：不量高度，量法線場的局部偏差——皮膚是 fullbright＋頭燈假光（亮度＝
f(N·V)，SPEC #37），螢幕上的明暗**就是**法線的函數。tilt = angle(頂點法線, 該點
R_SMOOTH 鄰域的平均法線)，單位度，直接對應著色誤差。amp（相對二次擬合面的高度，
正凸負凹）只是附帶輸出，用來說「這個凹多深」。

**門檻不寫死，由資料校準**：對照組＝**治療過的前大腿平坦面**（離布<8cm、z0.50~0.85、
腹側、離中線>20cm，扣掉解剖排除球）——必須與待判區同解剖脈絡，否則校歪（曾用整條
褌帶當對照＝繞過股間/髖緣、分布肥、誤報 12%）；門檻 = 該區 tilt 的 p99 × TILT_MARGIN。這樣「缺陷」的
定義永遠是「超出已知乾淨區能做到的水準」，不是我拍腦袋的數字。

**局部異常比（ratio）只是報表欄位、不是閘門**：曾當閘門用，但 user 這一族缺陷是
「整片區域都粗、上面再疊凹」——不是「周圍平只有它翹」，ratio 反而把它濾掉（自檢 1
FAIL 抓到）。真摺痕（會陰／臀縫／腿摺）靠 ANATOMY 排除球處理，不靠 ratio。

**兩條自檢，上限與下限同時驗**（08-16 血價：只驗上限的契約，什麼都沒發生也會 PASS）：
  ①下限：已知缺陷（KNOWN_MARK＝user 紫筆圈選點）必須被抓到
  ②上限：玻璃級對照區的命中率必須夠低（否則門檻是噪音）
兩條都必須 PASS，掃描結果才可信。

**解剖摺痕怎麼擋（08-23 三度修正，決定性）**：第一版全身掃出來「整片紅」，實測定罪＝
紅區的大尺度彎曲 bend（6cm 鄰域法線散布）p50 **46.1 度**，非紅區才 10.1 度——**紅的
絕大多數是肚下摺／髖摺／腰窩這類真解剖**，只是門檻拿平坦大腿校的，凡不平坦者一律踩線。
user 圈選的真缺陷 bend ≈ 10 度＝站在平地上。所以加 BEND_MAX 閘門：只有「大尺度平坦
但局部有凹凸」才算缺陷（紅區裡只有 4.7% 屬於此類）。

**解剖排除（點狀）**：乳頭／肚臍／胯下鼓起／頭臉這些真解剖在任何尺度都是「凸起」，會塞滿排名
前段。ANATOMY 是排除球清單（圓心 m + 半徑 m），新發現的解剖特徵往這裡加，缺陷永遠
不會因此被誤刪——被排除的群另外列在報表尾巴，可覆查。

用法：
  blender --background --python sumo_bump_scan.py
  環境變數：BLEND（預設現行 master）／R_SMOOTH（預設 0.025）／R_FIT（預設 0.025）
            ／TILT_MARGIN（預設 1.0）／ZLO ZHI／RENDER=1（另出四視角熱點圖）
產出：Saved/BumpScan/bumps.txt（排名表）＋ bumps.npz ＋ RENDER=1 時 bump_*.png
"""
import bpy
import numpy as np
import os
import math
from collections import defaultdict
from mathutils import Vector
from mathutils.kdtree import KDTree

ROOT = r"C:\games\Unreal Engine\nice_ink"
OUT = os.path.join(ROOT, "Saved", "BumpScan")
os.makedirs(OUT, exist_ok=True)
BLEND = os.environ.get("BLEND", os.path.join(ROOT, "SourceAssets", "sumo_character_master.blend"))
R_IN = float(os.environ.get("R_IN", "0.012"))    # 小尺度擬合半徑（平面）
R_OUT = float(os.environ.get("R_OUT", "0.035"))  # 大尺度擬合半徑（二次）
TILT_MARGIN = float(os.environ.get("TILT_MARGIN", "1.0"))
R_LOCAL = float(os.environ.get("R_LOCAL", "0.06"))      # 局部異常比的鄰域半徑
RATIO_MIN = float(os.environ.get("RATIO_MIN", "3.0"))
MIN_AMP = float(os.environ.get("MIN_AMP", "0.08"))   # mm，排除純法線噪音
BEND_MAX = float(os.environ.get("BEND_MAX", "12.0"))  # 度；bend 超過＝真解剖摺痕，不算缺陷
MIN_VERTS = int(os.environ.get("MIN_VERTS", "4"))
ZLO = float(os.environ.get("ZLO", "0.05"))
ZHI = float(os.environ.get("ZHI", "1.30"))          # 預設不掃頭臉（另有臉部管線）
KNOWN_MARK = np.array([-0.5273, -0.1801, 0.6614])   # 08-23 user 紫筆圈選點（下限自檢）

# 真解剖排除球：(x, y, z, r) 單位 m。新發現的解剖特徵加這裡。
ANATOMY = [
    (+0.300, -0.290, 1.130, 0.055),   # 左乳頭
    (-0.300, -0.290, 1.130, 0.055),   # 右乳頭
    (+0.000, -0.470, 0.705, 0.070),   # 肚臍
    (+0.000, -0.180, 0.560, 0.120),   # 胯下鼓起（褌下，物理遮擋）
    (+0.000, +0.440, 0.610, 0.090),   # 臀縫（band_fair 的 protect 區）
]


def P(*a):
    print(*a, flush=True)


bpy.ops.wm.open_mainfile(filepath=BLEND)
if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')
ob = bpy.data.objects["SumoRetopo"]
me = ob.data
n = len(me.vertices)
co = np.empty(n * 3)
me.vertices.foreach_get("co", co)
co = co.reshape(-1, 3)
ed = np.empty(len(me.edges) * 2, np.int64)
me.edges.foreach_get("vertices", ed)
ed = ed.reshape(-1, 2)
loops_v = np.empty(len(me.loops), np.int64)
me.loops.foreach_get("vertex_index", loops_v)
luv = np.empty(len(me.loops) * 2)
me.uv_layers[0].data.foreach_get("uv", luv)
luv = luv.reshape(-1, 2)
vuv = np.zeros((n, 2))
cnt = np.zeros(n)
np.add.at(vuv, loops_v, luv)
np.add.at(cnt, loops_v, 1)
vuv /= np.maximum(cnt, 1)[:, None]

# 三角扇（多邊形通吃——上一版把四邊形當三角形，第 4 角法線=0 ⇒ 假的 90/180 度）
tri = []
for p in me.polygons:
    ls = list(p.vertices)
    for k in range(1, len(ls) - 1):
        tri.append((ls[0], ls[k], ls[k + 1]))
tri = np.array(tri, np.int64)
P(f"verts={n} polys={len(me.polygons)} tris={len(tri)}")

# 面積加權頂點法線（面積權重＝面法線未歸一化的長度）
fn = np.cross(co[tri[:, 1]] - co[tri[:, 0]], co[tri[:, 2]] - co[tri[:, 0]])
acc = np.zeros((n, 3))
for k in range(3):
    np.add.at(acc, tri[:, k], fn)
ln = np.linalg.norm(acc, axis=1)
good = ln > 1e-9                       # 退化／孤立頂點：不參與判定
nrm = acc / np.maximum(ln, 1e-12)[:, None]

kd = KDTree(n)
for i, p in enumerate(co):
    kd.insert(Vector(p), i)
kd.balance()

scan = np.where(good & (co[:, 2] > ZLO) & (co[:, 2] < ZHI))[0]
P(f"scan verts={len(scan)}  R_IN={R_IN*100:.1f}cm  R_OUT={R_OUT*100:.1f}cm")

def fit(v, R, quad):
    """在半徑 R 內擬合（quad=True 二次、False 平面），回傳 (法線, 中心高度 mm)。
    兩個尺度都用**固定物理半徑的擬合**法線，不用頂點法線——頂點法線是相鄰三角形的
    平均，會隨網格密度變（帶內 3.7mm vs 粗區 8.2mm），拿去互比是蘋果比橘子（08-23 記帳）。"""
    nb = np.array([j for (_, j, _) in kd.find_range(Vector(co[v]), R)])
    nb = nb[good[nb]]
    if len(nb) < (8 if quad else 5):
        return None, 0.0, 0.0
    nz = nrm[v]
    ref = np.array([0.0, 0.0, 1.0]) if abs(nz[2]) < 0.9 else np.array([1.0, 0.0, 0.0])
    t1 = np.cross(nz, ref)
    t1 /= np.linalg.norm(t1)
    t2 = np.cross(nz, t1)
    d = co[nb] - co[v]
    u, w, h = d @ t1, d @ t2, d @ nz
    cols = [np.ones_like(u), u, w] + ([u * u, u * w, w * w] if quad else [])
    A = np.stack(cols, 1)
    try:
        cf = np.linalg.solve(A.T @ A + np.eye(A.shape[1]) * 1e-12, A.T @ h)
    except np.linalg.LinAlgError:
        return None, 0.0, 0.0
    nf = -cf[1] * t1 - cf[2] * t2 + nz           # 擬合面在原點的法線（局部座標 (-fu,-fw,1)）
    # bend＝鄰域法線的角度散布（度）：真摺痕高、平面上的凹低。用來把解剖摺痕擋掉。
    bd = math.degrees(math.acos(min(1.0, max(-1.0, float(np.linalg.norm(nrm[nb].mean(0)))))))
    return nf / np.linalg.norm(nf), -cf[0] * 1000.0, bd


tilt = np.zeros(n)
amp = np.zeros(n)
bend = np.zeros(n)
for c, v in enumerate(scan):
    # amp 只依賴 R_OUT 的二次擬合；tilt 才需要 R_IN。兩者分開判——曾經寫成
    # 「任一失敗就 continue」，結果粗網格區 R_IN(1.2cm) 湊不到 5 個鄰居 ⇒ amp 也被跳過
    # ⇒ 離布 >12cm 整片 p50/p90 = 0.0000 假裝很平（其實是沒量到，分環表抓出來的）。
    n_out, h_out, bd = fit(v, R_OUT, True)
    if n_out is None:
        continue
    amp[v] = h_out
    bend[v] = bd
    n_in, _, _ = fit(v, R_IN, False)
    if n_in is not None:
        tilt[v] = math.degrees(math.acos(min(1.0, max(-1.0, float(n_in @ n_out)))))
    if c % 20000 == 0:
        P(f"  scan {c}/{len(scan)}")

# --- 門檻校準：褌帶內＝08-22 緊膜整平過的玻璃級對照組 ---
D = np.load(os.path.join(ROOT, "Saved", "FundoshiPlate", "edge_lines.npz"))
path = np.vstack([D[k] for k in D if k.startswith("foot")])
kdp = KDTree(len(path))
for i, p in enumerate(path):
    kdp.insert(Vector(p), i)
kdp.balance()
dbv = np.array([kdp.find(Vector(p))[2] for p in co])
anat = np.zeros(n, bool)
for (ax, ay, az, ar) in ANATOMY:
    anat |= np.linalg.norm(co - np.array([ax, ay, az]), axis=1) < ar

# 對照組必須與待判區**同解剖脈絡**：治療過(<8cm)的「大腿平坦面」。曾用「整條褌帶
# <4cm」＝它繞過股間／髖緣等高曲率地帶、分布本身就肥，校出的門檻誤報 12%（自檢2 抓到）。
# 而且**校準組與驗證組必須分割**（同一塊校準又拿來驗上限＝套套邏輯，p99 門檻必然
# 剛好 1% 命中）：網格左右對稱，左腿校準、右腿驗證。
# 真正的玻璃標準只有**最內圈 <4cm**（08-22 緊膜整平的全量作用域）。曾用 <8cm＝
# 那圈本身 |amp| p99 0.446mm 已經比 user 圈選的缺陷（0.261mm）還糟，門檻自然抓不到。
FLATG = bend < BEND_MAX          # 大尺度平坦：缺陷才在這裡，摺痕不在
flat = (good & ~anat & FLATG & (dbv < 0.04) & (co[:, 2] > 0.45) & (co[:, 2] < 0.95)
        & (co[:, 1] < 0.10) & (np.abs(co[:, 0]) > 0.15))
calib = flat & (co[:, 0] > 0)      # 左腿＝校準組
glass = flat & (co[:, 0] < 0)      # 右腿＝驗證組（held-out）
AB = np.abs(amp)
g99 = float(np.percentile(AB[calib], 99))
THR = max(g99 * TILT_MARGIN, 0.02)
P(f"\n門檻校準: 校準組（治療過的左前大腿平坦面）n={int(calib.sum())} "
  f"|amp| p50 {np.percentile(AB[calib],50):.4f} p90 {np.percentile(AB[calib],90):.4f} "
  f"p99 {g99:.4f} mm -> THR = {THR:.4f} mm (p99 x{TILT_MARGIN})；"
  f"驗證組（右腿 held-out）n={int(glass.sum())}；"
  f"[參考] 同組 tilt p90 {np.percentile(tilt[calib],90):.2f} deg")

# 局部異常比：報表欄位，不當閘門（見檔頭）
cand = np.nonzero((AB > THR) & FLATG & good & (co[:, 2] > ZLO) & (co[:, 2] < ZHI))[0]
ratio = np.zeros(n)
for c, v in enumerate(cand):
    nb = np.array([j for (_, j, _) in kd.find_range(Vector(co[v]), R_LOCAL)])
    nb = nb[good[nb]]
    if len(nb) < 12:
        continue
    ratio[v] = AB[v] / max(float(np.median(AB[nb])), 0.005)
    if c % 20000 == 0:
        P(f"  ratio {c}/{len(cand)}")

hot = (AB > THR) & FLATG & good & (co[:, 2] > ZLO) & (co[:, 2] < ZHI)
parent = np.arange(n)


def find(a):
    while parent[a] != a:
        parent[a] = parent[parent[a]]
        a = parent[a]
    return a


for a, b in ed:
    if hot[a] and hot[b]:
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[ra] = rb
groups = defaultdict(list)
for i in np.nonzero(hot)[0]:
    groups[find(i)].append(i)


def label(p):
    x, y, z = p
    side = "L" if x > 0.04 else ("R" if x < -0.04 else "M")
    fb = "back" if y > 0.02 else ("front" if y < -0.02 else "side")
    part = ("neck" if z > 1.30 else "chest" if z > 1.10 else "waist" if z > 0.95 else
            "hip" if z > 0.80 else "thigh" if z > 0.50 else "calf" if z > 0.25 else "foot")
    return f"{part}.{fb}.{side}"


rows, skipped = [], []
for g, idx in groups.items():
    if len(idx) < MIN_VERTS:
        continue
    idx = np.array(idx)
    k = int(idx[np.argmax(np.abs(amp[idx]))])
    r = dict(k=k, n=len(idx), tilt=float(tilt[k]), ratio=float(ratio[k]),
             tilt90=float(np.percentile(tilt[idx], 90)),
             amp=float(amp[k]), bend=float(bend[k]),
             span=float((co[idx].max(0) - co[idx].min(0)).max() * 100),
             db=float(dbv[k] * 100), pos=co[k], uv=vuv[k])
    (skipped if anat[idx].mean() > 0.5 else rows).append(r)
rows.sort(key=lambda r: -(abs(r["amp"]) * math.sqrt(r["n"])))


def fmt(i, r):
    p = r["pos"]
    return (f"{i:3d}. {label(p):16s} tilt {r['tilt']:5.2f} x{r['ratio']:4.1f} "
            f"amp {r['amp']:+6.3f}mm bend{r['bend']:5.1f} {r['n']:5d}pt {r['span']:4.1f}cm cloth{r['db']:5.1f}cm "
            f"{'[TREATED]' if r['db'] < 10 else '         '} "
            f"uv({r['uv'][0]:.5f},{r['uv'][1]:.5f}) @({p[0]:+.3f},{p[1]:+.3f},{p[2]:.3f})")


lines = [f"# 小尺度不平整掃描 R_IN={R_IN*100:.1f}cm R_OUT={R_OUT*100:.1f}cm THR={THR:.3f}deg(玻璃區 p99 x{TILT_MARGIN})",
         "# tilt=法線場局部偏差(度)＝頭燈假光下的著色誤差   amp=相對二次擬合面高度(mm,正凸負凹)",
         "# 座標 m，+X=左 +Y=背 +Z=上；[TREATED]＝離褌接觸線<10cm（sumo_skin_hires_band 治療區內）",
         f"# 命中 {int(hot.sum())} 頂點 / {len(groups)} 群；列出 {len(rows)} 群（解剖排除 {len(skipped)} 群）", ""]
for i, r in enumerate(rows[:80]):
    lines.append(fmt(i + 1, r))
lines += ["", "# --- 解剖排除（覆查用） ---"]
for i, r in enumerate(sorted(skipped, key=lambda r: -r["tilt"])[:15]):
    lines.append(fmt(i + 1, r))
open(os.path.join(OUT, "bumps.txt"), "w", encoding="utf-8").write("\n".join(lines))
P("\n".join(lines[:50]))
np.savez(os.path.join(OUT, "bumps.npz"), co=co, tilt=tilt, amp=amp, uv=vuv, thr=THR)

# --- 自檢：上限與下限都要驗 ---
# 分環統計：這一族缺陷不是離散的點，是「離治療區越遠越粗」的連續場——排名表只講
# 得出最糟的幾處，這張表才講得出「有多大面積不合格」。
P("\n離褌接觸線分環 |amp| 統計（mm）與不合格面積占比:")
ringrep = ["", "# --- 離褌接觸線分環 |amp|(mm) ---"]
for lo, hi in ((0.00, 0.04), (0.04, 0.08), (0.08, 0.12), (0.12, 0.20), (0.20, 0.35), (0.35, 1.00)):
    m = good & ~anat & FLATG & (dbv >= lo) & (dbv < hi) & (co[:, 2] > ZLO) & (co[:, 2] < ZHI)
    if m.sum() < 50:
        continue
    ln_ = (f"  {lo*100:5.1f}~{hi*100:5.1f}cm  n={int(m.sum()):6d}  p50 {np.percentile(AB[m],50):.4f}  "
           f"p90 {np.percentile(AB[m],90):.4f}  p99 {np.percentile(AB[m],99):.4f}  "
           f"超標 {float((AB[m] > THR).mean()*100):5.1f}%")
    P(ln_)
    ringrep.append(ln_)
open(os.path.join(OUT, "bumps.txt"), "a", encoding="utf-8").write("\n".join(ringrep) + "\n")

near = np.linalg.norm(co - KNOWN_MARK, axis=1) < 0.015
mk_tilt = float(tilt[near].max())
ok1 = bool(hot[near].any())
rank = next((i + 1 for i, r in enumerate(rows) if np.linalg.norm(r["pos"] - KNOWN_MARK) < 0.03), None)
P(f"\n[SELFTEST-1 lower] known defect: |amp| {float(AB[near].max()):.4f}mm vs THR {THR:.4f}mm "
  f"(tilt {mk_tilt:.2f}deg), rank {rank} -> {'PASS' if ok1 else 'FAIL (missed)'}")
rate = float(hot[glass].mean() * 100)
ok2 = rate < 2.0
P(f"[SELFTEST-2 upper] glass zone hitrate {rate:.2f}% -> {'PASS' if ok2 else 'FAIL (false alarms)'}")
P(f"[SELFTEST] {'BOTH PASS' if (ok1 and ok2) else 'NOT TRUSTWORTHY'}")

if os.environ.get("RENDER"):
    ca = me.color_attributes.get("Bump") or me.color_attributes.new("Bump", 'FLOAT_COLOR', 'POINT')
    # 上色＝判準本身：紅=hot（大尺度平坦但局部超標）、藍灰=解剖摺痕(bend>BEND_MAX)被排除、
    # 灰=合格。曾用 tilt/(THR*3) 上色，而 THR 改成 mm 之後那式子瞬間飽和＝圖與判準脫節。
    t = np.clip((np.abs(amp) - THR) / max(THR * 2.0, 1e-6), 0, 1) * hot
    cols = np.zeros((n, 4))
    cols[:, 3] = 1
    fold = (~FLATG | anat) & good
    cols[:, 0] = np.where(fold, 0.42, 0.62 + 0.38 * t)
    cols[:, 1] = np.where(fold, 0.46, 0.62 * (1 - t))
    cols[:, 2] = np.where(fold, 0.55, 0.58 * (1 - t))
    ca.data.foreach_set("color", cols.ravel())
    me.attributes.active_color_name = "Bump"
    for o in bpy.data.objects:
        if o.name != "SumoRetopo":
            o.hide_render = True
    sc = bpy.context.scene
    sc.render.engine = 'BLENDER_WORKBENCH'
    sc.display.shading.light = 'FLAT'
    sc.display.shading.color_type = 'VERTEX'
    sc.render.resolution_x = 1200
    sc.render.resolution_y = 1050
    cd = bpy.data.cameras.new("c")
    cd.type = 'ORTHO'
    cd.ortho_scale = 2.10
    cam = bpy.data.objects.new("c", cd)
    sc.collection.objects.link(cam)
    sc.camera = cam
    ctr = Vector((0.0, 0.036, 0.873))
    views = {"front": (Vector((0, -1, 0)), (math.radians(90), 0, 0)),
             "back": (Vector((0, 1, 0)), (math.radians(90), 0, math.radians(180))),
             "left": (Vector((1, 0, 0)), (math.radians(90), 0, math.radians(90))),
             "right": (Vector((-1, 0, 0)), (math.radians(90), 0, math.radians(-90)))}
    for nm, (dv, rot) in views.items():
        cam.location = ctr + dv * 4.0
        cam.rotation_euler = rot
        sc.render.filepath = os.path.join(OUT, f"bump_{nm}.png")
        bpy.ops.render.render(write_still=True)
        P("RENDERED " + nm)
P("BUMP SCAN DONE")
