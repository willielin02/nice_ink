"""褌邊界「尖刺/缺口」修復（2026-08-18）——修 user 長期抓到的 3D 鋸齒。

**診斷路徑（含兩次走錯，留著當教訓）**：
 (1) 先誤判成 z-fighting（量到內層 70% 距皮膚 <0.2mm——數字為真但不是病因；
     user 直接指出「問題不是服貼，是建模的邊緣就是折線」）。
 (2) 再誤判成紋素階梯，上全域平滑（Taubin 60 對 / 弧長高斯 2.5mm）——
     殘差只從 0.42 掉到 0.22，而且**把整條手繪線都動了 0.43mm**。
 (3) 把邊界線畫出來看（Saved/Screenshots/fd_edge_curve.png）才看清真相：
     **曲線整體流暢，defect 是孤立的單點尖刺/缺口**（2~3mm 級，最大 12mm）。
     來源＝當年跨 UV 島縫的直線橋接與焊接點（見 sumo migration 記錄）。

**所以做法＝脈衝濾波（只修異常點），不是低通（動整條線）**：
逐點量它離「局部低通」多遠；只有超過門檻的點被拉回，其餘**一顆不動**。
這樣 user 手繪的形狀保住，只有壞掉的點被修。

Run: blender --background <master.blend> --python fundoshi_edge_despike.py
"""
import bpy, numpy as np, os, shutil
from collections import defaultdict

ROOT = r"C:\games\Unreal Engine\nice_ink"
MASTER = os.path.join(ROOT, "SourceAssets", "sumo_character_master.blend")
BACKUP = os.path.join(ROOT, "SourceAssets", "masters", "sumo_character_master_v18_preedgesmooth.blend")
SIGMA_MM = 3.0        # 局部低通尺度
THRESH_MM = 2.5       # 只抓真離群（實測門檻 0.8 會誤判 37.6%＝等於重塑手繪線）
DIFFUSE_ITERS = 10

if not os.path.exists(BACKUP):
    shutil.copy2(MASTER, BACKUP)
bpy.ops.wm.open_mainfile(filepath=MASTER)
if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')
ob = bpy.data.objects["Fundoshi"]; me = ob.data
n = len(me.vertices); half = n//2
co = np.empty(n*3); me.vertices.foreach_get("co", co); co = co.reshape(-1,3)*1000.0
co0 = co.copy()

euse = defaultdict(int)
for p in me.polygons:
    vs = [int(v) for v in p.vertices]
    if max(vs) >= half: continue
    for i in range(len(vs)):
        euse[tuple(sorted((vs[i], vs[(i+1) % len(vs)])))] += 1
adj = defaultdict(list)
for e, c in euse.items():
    if c == 1:
        adj[e[0]].append(e[1]); adj[e[1]].append(e[0])
loops, seen = [], set()
for s0 in adj:
    if s0 in seen or len(adj[s0]) != 2: continue
    loop, cur, prev = [s0], s0, None; seen.add(s0)
    while True:
        nx = [x for x in adj[cur] if x != prev]
        if not nx or nx[0] == s0 or nx[0] in seen: break
        loop.append(nx[0]); seen.add(nx[0]); prev, cur = cur, nx[0]
    if len(loop) > 20: loops.append(loop)
loops.sort(key=len, reverse=True)
print(f"封閉迴圈 {len(loops)} 條 {[len(l) for l in loops]}")

def lowpass(P, sigma):
    d = np.linalg.norm(np.diff(np.vstack([P, P[:1]]), axis=0), axis=1)
    s = np.concatenate([[0.0], np.cumsum(d)[:-1]]); total = d.sum()
    out = np.empty_like(P)
    for i in range(len(P)):
        ds = s - s[i]; ds = ds - total*np.round(ds/total)
        w = np.exp(-0.5*(ds/sigma)**2); w[np.abs(ds) > 3*sigma] = 0.0
        out[i] = (P*w[:, None]).sum(axis=0)/w.sum()
    return out

fixed_total, devs_all = 0, []
for loop in loops:
    L = np.array(loop); P = co[L].copy()
    for _ in range(3):                       # 反覆幾輪：大刺修掉後小刺才現形
        lp = lowpass(P, SIGMA_MM)
        dev = np.linalg.norm(P - lp, axis=1)
        bad = dev > THRESH_MM
        if not bad.any(): break
        P[bad] = lp[bad]
        fixed_total += int(bad.sum())
    devs_all.append(np.linalg.norm(co[L] - P, axis=1))
    co[L] = P
devs = np.concatenate(devs_all)
touched = devs > 1e-9
print(f"**修掉的尖刺 {int(touched.sum())} 顆 / 邊界 {len(devs)} 顆 ({touched.mean()*100:.1f}%)**")
print(f"   被修的點位移(mm): p50={np.percentile(devs[touched],50):.2f} "
      f"p90={np.percentile(devs[touched],90):.2f} max={devs.max():.2f}")
print(f"   **未被修的 {int((~touched).sum())} 顆＝user 手繪形狀一顆未動**")

delta = co - co0
bset = set(int(v) for v in adj.keys())
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
co = co0 + delta
co[half:] = co[half:] + delta[:half]
me.vertices.foreach_set("co", (co/1000.0).ravel())
me.update()
th = np.linalg.norm(co[:half]-co[half:], axis=1)
print(f"厚度守恆(mm) p50={np.percentile(th,50):.3f} min={th.min():.3f} max={th.max():.3f}")
bpy.ops.wm.save_mainfile()
print("SAVED master")
