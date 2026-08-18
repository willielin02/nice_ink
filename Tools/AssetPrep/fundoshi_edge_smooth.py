"""褌邊界曲線平滑（2026-08-18）——修 user 長期抓到的「3D 鋸齒」。

**病根（量測定罪）**：邊界生成鏈＝手繪遮罩 → 4096 圖上 marching squares 抽等值線
→ RDP 0.1mm，且當年明文「零平滑＝筆跡本身」。紋素約 0.8mm ⇒ 階梯約 0.4mm，
而 RDP 門檻 0.1mm 比階梯小 ⇒ 階梯被原封保留。
實測邊界殘差 p50=0.399mm（平滑曲線在同尺度應為 0.035mm）＝粗糙 11 倍。
**這是 3D 實體鋸齒，與 AA/材質/深度緩衝無關。**
更早的 v63 其實有做（「邊界圓滑量＝Taubin 60 對」），是 v68 重建時拿掉的＝被移除的解。

**做法**：
1) 邊界＝**只被一個外層面使用的邊**（不是「與內層相鄰的頂點」——後者在窄處
   會把對面邊界也收進來，實測會併成單一連通分量）。
2) 沿每條封閉迴圈做 Taubin（λ/μ 交替：只去高頻、不縮水；純 Laplacian 會讓迴圈塌陷）。
3) 邊界位移**擴散進內部**（Laplacian、邊界為 Dirichlet）——帶子橫向只有 2~3 頂點，
   只動邊界會把面撕歪。
4) 外層與其配對內層**位移相同** ⇒ 厚度守恆。

Run: blender --background <master.blend> --python fundoshi_edge_smooth.py
"""
import bpy, numpy as np, os, shutil
from collections import defaultdict

ROOT = r"C:\games\Unreal Engine\nice_ink"
MASTER = os.path.join(ROOT, "SourceAssets", "sumo_character_master.blend")
BACKUP = os.path.join(ROOT, "SourceAssets", "masters", "sumo_character_master_v18_preedgesmooth.blend")

# 平滑＝**弧長域高斯低通**（不是等權 Laplacian）。理由：邊界頂點間距極不均
# （RDP 簡化過，實測 p50 1.1mm / p90 5.0mm），等權 Laplacian 在不均勻取樣上有偏差、
# 不收斂到平滑曲線——實測 Taubin 60 對只把殘差 0.42→0.22 就停住。
SIGMA_MM = 2.5        # 低通尺度：殺掉 <5mm 的階梯，保留輪廓本身的彎
HF_SIGMA_MM = 2.0     # 量測用：只看這個尺度以下的高頻＝紋素階梯住的地方
LAM, MU = 0.5, -0.53
DIFFUSE_ITERS = 12    # 邊界位移往內部擴散

if not os.path.exists(BACKUP):
    shutil.copy2(MASTER, BACKUP); print(f"BACKUP -> {BACKUP}")
bpy.ops.wm.open_mainfile(filepath=MASTER)
if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')
ob = bpy.data.objects["Fundoshi"]; me = ob.data
n = len(me.vertices); half = n//2
co = np.empty(n*3); me.vertices.foreach_get("co", co); co = co.reshape(-1,3)*1000.0  # mm
co0 = co.copy()

# --- 1) 邊界＝只被一個外層面用到的邊 ---
euse = defaultdict(int)
for p in me.polygons:
    vs = [int(v) for v in p.vertices]
    if max(vs) >= half:      # 只看外層面
        continue
    for i in range(len(vs)):
        euse[tuple(sorted((vs[i], vs[(i+1) % len(vs)])))] += 1
bedges = [e for e, c in euse.items() if c == 1]
adj = defaultdict(list)
for a, b in bedges:
    adj[a].append(b); adj[b].append(a)
deg = np.array([len(adj[v]) for v in adj])
print(f"邊界邊 {len(bedges)}  邊界頂點 {len(adj)}  度數分布 {np.bincount(deg)}")

# 走迴圈
loops, seen = [], set()
for s in adj:
    if s in seen or len(adj[s]) != 2:
        continue
    loop, cur, prev = [s], s, None
    seen.add(s)
    while True:
        nxts = [x for x in adj[cur] if x != prev]
        if not nxts: break
        nxt = nxts[0]
        if nxt == s: break
        if nxt in seen: break
        loop.append(nxt); seen.add(nxt); prev, cur = cur, nxt
    if len(loop) > 20:
        loops.append(loop)
loops.sort(key=len, reverse=True)
print(f"封閉迴圈 {len(loops)} 條，長度 {[len(l) for l in loops][:6]}")

def arc(P):
    d = np.linalg.norm(np.diff(np.vstack([P, P[:1]]), axis=0), axis=1)
    return np.concatenate([[0.0], np.cumsum(d)[:-1]]), d.sum()

def gauss_loop(P, sigma):
    """弧長域高斯低通（環繞）。不均勻取樣安全：權重用真實弧長距離。"""
    s, total = arc(P); m = len(P); out = np.empty_like(P)
    for i in range(m):
        ds = s - s[i]
        ds = ds - total*np.round(ds/total)          # 環繞最短距離
        w = np.exp(-0.5*(ds/sigma)**2)
        w[np.abs(ds) > 3*sigma] = 0.0
        out[i] = (P*w[:, None]).sum(axis=0)/w.sum()
    return out

def hf_energy(loops, X, sigma):
    """高頻能量＝點離「自己的低通版」多遠。真實曲率活在長波長，不進這個量。"""
    out = []
    for loop in loops:
        P = X[np.array(loop)]
        out.append(np.linalg.norm(P - gauss_loop(P, sigma), axis=1))
    return np.concatenate(out)

allb = np.array(sorted(adj.keys()))
r0 = hf_energy(loops, co, HF_SIGMA_MM)
print(f"平滑前 高頻(<{HF_SIGMA_MM}mm) p50={np.percentile(r0,50):.3f} p90={np.percentile(r0,90):.3f} max={r0.max():.3f}")

# --- 2) 逐迴圈 Taubin ---
for loop in loops:
    L = np.array(loop)
    co[L] = gauss_loop(co[L].copy(), SIGMA_MM)

# --- 3) 位移擴散進內部（邊界 Dirichlet）---
delta = co - co0
bset = set(int(v) for v in allb)
vadj = defaultdict(set)
for p in me.polygons:
    vs = [int(v) for v in p.vertices]
    if max(vs) >= half: continue
    for i in range(len(vs)):
        a, b = vs[i], vs[(i+1) % len(vs)]
        vadj[a].add(b); vadj[b].add(a)
interior = [v for v in range(half) if v in vadj and v not in bset]
for _ in range(DIFFUSE_ITERS):
    for v in interior:
        nb = list(vadj[v])
        if nb: delta[v] = delta[nb].mean(axis=0)

# --- 4) 套用；內層跟著同一位移＝厚度守恆 ---
co = co0 + delta
co[half:] = co[half:] + delta[:half]        # 內層配對位移
me.vertices.foreach_set("co", (co/1000.0).ravel())
me.update()

r1 = hf_energy(loops, co, HF_SIGMA_MM)
d = np.linalg.norm(co - co0, axis=1)
print(f"平滑後 高頻(<{HF_SIGMA_MM}mm) p50={np.percentile(r1,50):.3f} p90={np.percentile(r1,90):.3f} max={r1.max():.3f}")
print(f"**輪廓位移(mm) 邊界 p50={np.percentile(d[allb],50):.3f} p90={np.percentile(d[allb],90):.3f} "
      f"max={d[allb].max():.3f}**")
th = np.linalg.norm(co[:half]-co[half:], axis=1)
print(f"厚度守恆檢查(mm) p50={np.percentile(th,50):.3f} min={th.min():.3f} max={th.max():.3f}")
bpy.ops.wm.save_mainfile()
print("SAVED master")
