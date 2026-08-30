# 筆頭實體大小 vs UV 紋素密度（唯讀離線；不開引擎）
#
# 2026-08-30：user 實測「斷墨稍有改善、乳頭爆墨完全沒改善」。追記88 的幾何探針
# 量的是「針落在哪」（零壓縮、零丟棄）——它從頭到尾沒量「每一針多大」。
# MarkerUvRadius 是 **UV 空間常數**（0.000452），它等於 3.0mm 的前提是
# 「UV0 均勻紋素密度」。而乳頭/乳暈/肚臍是**各自獨立的連通元件＝各自的 UV chart**，
# 打包器沒有義務給它們跟身體一樣的密度。密度低 ⇒ 同一個 UV 半徑 ⇒ 實體筆頭變大。
#
# 用法：blender --background --python Tools/AssetPrep/ink_texel_density.py
import bpy, os
import numpy as np
from collections import deque

SA = r"C:\games\Unreal Engine\nice_ink\SourceAssets"
MASTER = os.path.join(SA, "sumo_character_master.blend")
MARKER_UV_R = 0.000452     # InkCanvasComponent.h:60
RT = 4096
ISLAND_MAX = 5000

def P(*a): print(*a, flush=True)

bpy.ops.wm.open_mainfile(filepath=MASTER)
ob = bpy.data.objects["SumoRetopo"]
me = ob.data
mw = np.array(ob.matrix_world)
nv = len(me.vertices)
co = np.empty(nv*3); me.vertices.foreach_get("co", co)
co = co.reshape(nv,3) @ mw[:3,:3].T + mw[:3,3]
me.calc_loop_triangles()
nt = len(me.loop_triangles)
tv = np.empty(nt*3, np.int64); me.loop_triangles.foreach_get("vertices", tv); tv = tv.reshape(nt,3)
tl = np.empty(nt*3, np.int64); me.loop_triangles.foreach_get("loops", tl); tl = tl.reshape(nt,3)
uvl = me.uv_layers[0]
assert uvl.name == "UVMap", uvl.name
uva = np.empty(len(me.loops)*2); uvl.data.foreach_get("uv", uva); uva = uva.reshape(-1,2)
tuv = uva[tl]

p0,p1,p2 = co[tv][:,0], co[tv][:,1], co[tv][:,2]
A3 = 0.5*np.linalg.norm(np.cross(p1-p0, p2-p0), axis=1)          # m^2
q0,q1,q2 = tuv[:,0], tuv[:,1], tuv[:,2]
Auv = 0.5*np.abs(np.cross(q1-q0, q2-q0))                          # uv^2
ok = (A3 > 1e-12) & (Auv > 1e-16)
# uv 單位 / 公尺（線性）
dens = np.zeros(nt); dens[ok] = np.sqrt(Auv[ok]/A3[ok])

# 連通元件
ne = len(me.edges); ev = np.empty(ne*2, np.int64); me.edges.foreach_get("vertices", ev); ev = ev.reshape(ne,2)
adj = [[] for _ in range(nv)]
for a,b in ev: adj[a].append(b); adj[b].append(a)
comp = np.full(nv,-1,np.int64); nc = 0
for s in range(nv):
    if comp[s] >= 0: continue
    q = deque([s]); comp[s] = nc
    while q:
        u = q.popleft()
        for v in adj[u]:
            if comp[v] < 0: comp[v] = nc; q.append(v)
    nc += 1
sizes = np.bincount(comp)
small = sorted([i for i in range(nc) if sizes[i] <= ISLAND_MAX], key=lambda i: sizes[i])
big = [i for i in range(nc) if sizes[i] > ISLAND_MAX]

def report(label, tri_mask):
    m = tri_mask & ok
    if m.sum() < 3:
        P("%-22s (三角形太少)" % label); return None
    d = dens[m]
    # 實體筆頭直徑 = 2 * UV 半徑 / (uv per m)，轉成 mm
    dia = 2.0*MARKER_UV_R/d * 1000.0
    a3 = A3[m].sum()*1e4
    P("%-22s tris=%6d  面積=%7.2fcm²  筆頭直徑 mm: 中位=%6.2f  p10=%6.2f  p90=%6.2f  最大=%7.2f"
      % (label, int(m.sum()), a3, np.median(dia), np.percentile(dia,10), np.percentile(dia,90), dia.max()))
    return np.median(dia)

P("MESH tris=%d  MarkerUvRadius=%.6f  RT=%d" % (nt, MARKER_UV_R, RT))
P("設計值：筆頭直徑 3.00mm（TattooNibDiameterCm=0.30）；實測墨寬 2.81mm")
P("")
body = np.zeros(nt, bool)
for i in big: body |= comp[tv[:,0]] == i
base = report("身體+頭（對照）", body)
P("")
names = ["乳頭核心A","乳頭核心B","乳暈A","乳暈B","肚臍"]
for k,i in enumerate(small):
    m = (comp[tv[:,0]] == i) & (comp[tv[:,1]] == i) & (comp[tv[:,2]] == i)
    lbl = names[k] if k < len(names) else "島%d" % i
    d = report("%s (v=%d)" % (lbl, sizes[i]), m)
    if d and base:
        P("%-22s → **實體筆頭是身體的 %.2f 倍**" % ("", d/base))


# ================= 二輪：真正的筆頭橢圓（UV→3D Jacobian 奇異值）=================
# 等向平均 sqrt(Auv/A3) 會把「一個方向被拉長」平均掉。章是 UV 空間的圓，
# 映到皮膚上是橢圓，半軸 = MarkerUvRadius × sigma1 / sigma2（每 uv 單位幾公尺）。
P("")
P("================ 筆頭橢圓（Jacobian 奇異值）================")
P("章在 UV 是圓；皮膚上是橢圓。長軸 = 2*R*sigma1、短軸 = 2*R*sigma2（mm）")
P("")

e0uv = q1 - q0; e1uv = q2 - q0
e03d = p1 - p0; e13d = p2 - p0
det = e0uv[:,0]*e1uv[:,1] - e0uv[:,1]*e1uv[:,0]
good = ok & (np.abs(det) > 1e-14)
J = np.zeros((nt,3,2))
inv00 =  e1uv[:,1]; inv01 = -e1uv[:,0]
inv10 = -e0uv[:,1]; inv11 =  e0uv[:,0]
for k in range(3):
    J[good,k,0] = (e03d[good,k]*inv00[good] + e13d[good,k]*inv10[good]) / det[good]
    J[good,k,1] = (e03d[good,k]*inv01[good] + e13d[good,k]*inv11[good]) / det[good]
sv = np.zeros((nt,2))
sv[good] = np.linalg.svd(J[good], compute_uv=False)
major = 2.0*MARKER_UV_R*sv[:,0]*1000.0   # mm
minor = 2.0*MARKER_UV_R*sv[:,1]*1000.0

def report2(label, m):
    m = m & good
    if m.sum() < 3:
        P("%-20s (太少)" % label); return
    w = A3[m]; w = w/w.sum()          # 面積加權＝玩家真的會畫到的比例
    mj, mn = major[m], minor[m]
    o = np.argsort(mj)
    cw = np.cumsum(w[o])
    p50 = mj[o][np.searchsorted(cw,0.50)]
    p90 = mj[o][np.searchsorted(cw,0.90)]
    p99 = mj[o][np.searchsorted(cw,0.99)]
    frac = w[mj > 4.0].sum()*100.0     # >4mm = 設計值 1.33 倍
    aniso = np.median(mj/np.maximum(mn,1e-9))
    P("%-20s 長軸mm 面積加權 p50=%5.2f p90=%6.2f p99=%7.2f max=%8.2f | >4mm 佔面積 %5.1f%% | 長短比中位 %.2f"
      % (label, p50, p90, p99, mj.max(), frac, aniso))

report2("身體+頭（對照）", body)
for k,i in enumerate(small):
    m = (comp[tv[:,0]]==i)&(comp[tv[:,1]]==i)&(comp[tv[:,2]]==i)
    report2(names[k] if k < len(names) else "島%d"%i, m)

# 超大章長在哪：離島邊界多遠
P("")
P("---- 超大章（長軸>4mm）落在島的哪裡：離島邊界的距離 ----")
for k,i in enumerate(small):
    if k >= len(names): break
    vm = comp == i
    m = (comp[tv[:,0]]==i)&(comp[tv[:,1]]==i)&(comp[tv[:,2]]==i)&good
    if m.sum() < 3: continue
    # 島邊界頂點＝屬於島、但有鄰居不屬於島
    bnd = np.array([v for v in np.nonzero(vm)[0] if any(not vm[u] for u in adj[v])])
    if len(bnd) == 0:
        P("%-12s（島無外鄰＝完全獨立）" % names[k]); continue
    cen = co[tv[m]].mean(axis=1)
    d = np.linalg.norm(cen[:,None,:] - co[bnd][None,:,:], axis=2).min(axis=1)*1000.0
    bigm = major[m] > 4.0
    if bigm.sum() == 0:
        P("%-12s 無 >4mm 的章" % names[k]); continue
    P("%-12s >4mm 章 n=%4d：離島邊界 中位=%5.1fmm p90=%5.1fmm ｜ 全島中位=%5.1fmm"
      % (names[k], int(bigm.sum()), np.median(d[bigm]), np.percentile(d[bigm],90), np.median(d)))


# ================= 三輪：套用校正後的預測 =================
# 引擎端做的是 r_local = MarkerUvRadius * clamp(REF/sigma1, MIN, 1)
REF_CM_PER_UV = 361.7
MIN_SCALE = 0.12
P("")
P("================ 套用校正後（引擎端 StampUvRadiusAt 的離線重放）================")
P("r_local = R * clamp(%.1f / sigma1, %.2f, 1.0)   ⇒ 目標：長軸恆為 3.27mm 且只縮不放" % (REF_CM_PER_UV, MIN_SCALE))
P("")
sigma1_cm = sv[:,0]*100.0                      # m/uv -> cm/uv
scale = np.clip(REF_CM_PER_UV/np.maximum(sigma1_cm,1e-9), MIN_SCALE, 1.0)
major_fix = major*scale
minor_fix = minor*scale

def cmp(label, m):
    m = m & good
    if m.sum() < 3: return
    w = A3[m]; w = w/w.sum()
    def wq(a, q):
        o = np.argsort(a); return a[o][np.searchsorted(np.cumsum(w[o]), q)]
    b, f = major[m], major_fix[m]
    P("%-16s 改前 p50=%5.2f p90=%6.2f p99=%7.2f >4mm=%5.1f%%  |  改後 p50=%5.2f p90=%5.2f p99=%5.2f >4mm=%5.1f%%"
      % (label, wq(b,.5), wq(b,.9), wq(b,.99), w[b>4].sum()*100,
                wq(f,.5), wq(f,.9), wq(f,.99), w[f>4].sum()*100))

cmp("身體+頭", body)
for k,i in enumerate(small):
    m = (comp[tv[:,0]]==i)&(comp[tv[:,1]]==i)&(comp[tv[:,2]]==i)
    cmp(names[k] if k < len(names) else "島%d"%i, m)
P("")
P("身體受影響比例（scale<0.98 的面積佔比）：%.1f%%" % (A3[good & body & (scale<0.98)].sum()/A3[good & body].sum()*100))
P("身體 scale 中位 = %.3f（1.000 = 完全不動）" % np.median(scale[good & body]))
