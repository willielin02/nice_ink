# 解剖島 UV0 重展＋搬家（2026-08-30；user 回報乳頭爆墨/斷墨三修不成之後的根治）
#
# 病根（量測，不是推論）：`MarkerUvRadius` 活在 UV 空間，等於 3.0mm 的前提是
# 「UV0 均勻紋素密度」。身體成立；**乳頭/乳暈/肚臍這五顆 retopo 保護島不成立**。
#   一條 3mm 的線在那裡有幾個紋素寬（面積加權，RT4096）：
#     身體   p50 3.39 / p10 2.82 / <2紋素 0.5%
#     五島   p50 3.38 / p10 **0.90** / <2紋素 **23.5%**
#   ⇒ 常數 UV 半徑 ⇒ 實體 12~14mm 的章（一坨）；改成實體 3mm ⇒ 0.8 個紋素（細又斷）。
#   **兩個症狀同一個事實，而且互斥——筆寬那個旋鈕裡沒有解。**
#
# 為什麼是這五顆：它們是 retopo 為了保住解剖形狀而留的**獨立連通元件**（各自封閉、
# 零邊界邊），於是各自成為 UV chart，**逃出了均勻化的作用域**。而 uv0_uniform.py 的
# 密度閘門量的是 sqrt(Auv/A3)＝**面積**比，對「單方向被壓扁」天生看不見
# ——實測它們的面積密度是身體的 1.01~1.27 倍（看起來完全正常）。閘門一直在報成功。
#
# 為什麼不整張重展：build_sumo_skeletal_fbx.py 開宗明義「**絕不重展 UV0**」——
# fundoshi 手繪正源/hair/face mask/edge shadow/nodraw/眼罩對應表全錨在現版面上。
# 所以只動這五顆島，其餘 chart 逐位不動。
# 貼圖代價已量（ink_uv_mask_survey）：10 張 UV0 錨定貼圖裡 **8 張在島上是常數**
# ＝搬家免費；只有 body_ao / body_ao_shadow 有真內容 ⇒ 由 sumo_anat_uv_retarget.py
# 依「舊 UV↔新 UV 同一個三角形」重取樣搬過去；body_chroma_anat 本來就是腳本重烘。
#
# 參數選擇（anat_uv_sweep.py 掃過 66/55/45/30°，驗收量＝上面那個線寬紋素）：
#   目前 43 charts, p10 0.90, <2紋素 23.5%
#   66° → 39 charts, p10 2.77, 0.0%   ← **兩個軸都比現況好**，且與 uv0_uniform 同參數
#   55° → 68 charts, 45° → 86, 30° → 167（p10 更高但縫更多＝筆劃斷點更多）
# 取 66°：它嚴格支配現況，而且「這五顆島只是從來沒被做過身體做過的事」。
#
# 用法：blender --background --python Tools/AssetPrep/sumo_anat_uv_fix.py
import bpy, os, math, shutil
import numpy as np
from collections import deque

SA = r"C:\games\Unreal Engine\nice_ink\SourceAssets"
MASTER = os.path.join(SA, "sumo_character_master.blend")
BACKUP = os.path.join(SA, "sumo_character_master_pre_anatuv.blend")
CORR   = os.path.join(SA, "anat_uv_correspondence.npz")
ANGLE, ISLAND_MAX, RT, LINE_MM = 66.0, 5000, 4096, 3.0
GUTTER_UV = 4.0 / RT          # chart 之間留 4 紋素
DRYRUN = os.environ.get("ANAT_UV_DRYRUN", "") == "1"
def P(*a): print(*a, flush=True)

def tri_arrays(ob):
    me = ob.data
    mw = np.array(ob.matrix_world); nv = len(me.vertices)
    co = np.empty(nv*3); me.vertices.foreach_get("co", co)
    co = co.reshape(nv,3) @ mw[:3,:3].T + mw[:3,3]
    me.calc_loop_triangles(); nt = len(me.loop_triangles)
    tv = np.empty(nt*3,np.int64); me.loop_triangles.foreach_get("vertices",tv)
    tl = np.empty(nt*3,np.int64); me.loop_triangles.foreach_get("loops",tl)
    return co, tv.reshape(nt,3), tl.reshape(nt,3), nt

def line_texels(co, tv, tuv, mask):
    p0,p1,p2 = co[tv][:,0],co[tv][:,1],co[tv][:,2]
    q0,q1,q2 = tuv[:,0],tuv[:,1],tuv[:,2]
    A3 = 0.5*np.linalg.norm(np.cross(p1-p0,p2-p0),axis=1)
    e0,e1 = q1-q0, q2-q0
    f0,f1 = p1-p0, p2-p0
    det = e0[:,0]*e1[:,1]-e0[:,1]*e1[:,0]
    g = mask & (A3>1e-12) & (np.abs(det)>1e-14)
    n=len(A3); J=np.zeros((n,3,2))
    for k in range(3):
        J[g,k,0]=(f0[g,k]*e1[g,1]-f1[g,k]*e0[g,1])/det[g]
        J[g,k,1]=(f1[g,k]*e0[g,0]-f0[g,k]*e1[g,0])/det[g]
    sv=np.zeros((n,2)); sv[g]=np.linalg.svd(J[g],compute_uv=False)
    lt=np.zeros(n); lt[g]=LINE_MM*RT/(sv[g,0]*1000.0)
    w=A3[g]; w=w/w.sum(); v=lt[g]; o=np.argsort(v); cw=np.cumsum(w[o])
    return v[o][np.searchsorted(cw,.5)], v[o][np.searchsorted(cw,.10)], w[v<2.0].sum()*100

def area_density(co, tv, tuv, mask):
    p0,p1,p2 = co[tv][:,0],co[tv][:,1],co[tv][:,2]
    q0,q1,q2 = tuv[:,0],tuv[:,1],tuv[:,2]
    A3=0.5*np.linalg.norm(np.cross(p1-p0,p2-p0),axis=1)
    Auv=0.5*np.abs(np.cross(q1-q0,q2-q0))
    g=mask&(A3>1e-12)&(Auv>1e-16)
    d=np.sqrt(Auv[g]/A3[g]); w=A3[g]/A3[g].sum(); o=np.argsort(d)
    return d[o][np.searchsorted(np.cumsum(w[o]),0.5)]

# ---------------- 讀取＋定位五島 ----------------
bpy.ops.wm.open_mainfile(filepath=MASTER)
ob = bpy.data.objects["SumoRetopo"]; me = ob.data
nv, nl = len(me.vertices), len(me.loops)
co, tv, tl, nt = tri_arrays(ob)
uv_old = np.empty(nl*2); me.uv_layers["UVMap"].data.foreach_get("uv", uv_old)
uv_old = uv_old.reshape(nl,2)
tuv_old = uv_old[tl]

ne=len(me.edges); ev=np.empty(ne*2,np.int64); me.edges.foreach_get("vertices",ev); ev=ev.reshape(ne,2)
adj=[[] for _ in range(nv)]
for a,b in ev: adj[a].append(b); adj[b].append(a)
comp=np.full(nv,-1,np.int64); nc=0
for s in range(nv):
    if comp[s]>=0: continue
    q=deque([s]); comp[s]=nc
    while q:
        u=q.popleft()
        for v in adj[u]:
            if comp[v]<0: comp[v]=nc; q.append(v)
    nc+=1
sizes=np.bincount(comp)
small=[i for i in range(nc) if sizes[i]<=ISLAND_MAX]
assert len(small)==5, f"預期 5 顆解剖島，實得 {len(small)}——網格改過了，先重新確認"
isl_v=np.zeros(nv,bool)
for i in small: isl_v |= comp==i
isl_t = isl_v[tv].all(axis=1)
body_t = ~isl_v[tv].any(axis=1)
assert isl_t.sum()+body_t.sum()==nt, "有三角形同時跨島與身體＝島不是獨立元件"

REF = area_density(co, tv, tuv_old, body_t)
b50,b10,bsub = line_texels(co, tv, tuv_old, body_t)
i50,i10,isub = line_texels(co, tv, tuv_old, isl_t)
P("身體基準  p50=%.2f p10=%.2f <2紋素=%.1f%%   REF密度=%.4f uv/m" % (b50,b10,bsub,REF))
P("五島 改前 p50=%.2f p10=%.2f <2紋素=%.1f%%" % (i50,i10,isub))

# ---------------- 重展（只動島） ----------------
poly_isl = np.array([bool(isl_v[list(p.vertices)].all()) for p in me.polygons])
P("島面數=%d / 全體=%d" % (poly_isl.sum(), len(me.polygons)))
me.uv_layers.active = me.uv_layers["UVMap"]
for o_ in bpy.context.view_layer.objects: o_.select_set(False)
ob.select_set(True); bpy.context.view_layer.objects.active = ob
bpy.context.scene.tool_settings.use_uv_select_sync = True
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.select_mode(type='FACE')
bpy.ops.object.mode_set(mode='OBJECT')
me.polygons.foreach_set("select", poly_isl)
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.uv.smart_project(angle_limit=math.radians(ANGLE), island_margin=0.002,
                         correct_aspect=True, scale_to_bounds=False)
bpy.ops.object.mode_set(mode='OBJECT')

uv_new = np.empty(nl*2); me.uv_layers["UVMap"].data.foreach_get("uv", uv_new)
uv_new = uv_new.reshape(nl,2)
# 身體 loop 必須逐位不動
isl_loop = np.zeros(nl, bool)
for pi, p in enumerate(me.polygons):
    if poly_isl[pi]:
        for l in p.loop_indices: isl_loop[l] = True
assert np.array_equal(uv_new[~isl_loop], uv_old[~isl_loop]), "**身體 UV 被動到了**——中止"
P("身體 UV 逐位不動：OK（%d 個非島 loop）" % (~isl_loop).sum())

# 縮放到 REF 密度
cur = area_density(co, tv, uv_new[tl], isl_t)
uv_new[isl_loop] *= (REF/cur)
P("島密度 %.4f → 目標 %.4f（縮放 %.4f）" % (cur, REF, REF/cur))

# ---------------- 切 chart、找空地、貨架打包 ----------------
it = np.nonzero(isl_t)[0]
par = list(range(len(it)*3)); key = {}
def find(x):
    while par[x]!=x: par[x]=par[par[x]]; x=par[x]
    return x
def uni(a,b):
    ra,rb=find(a),find(b)
    if ra!=rb: par[rb]=ra
tuv_new = uv_new[tl]
for n_,t in enumerate(it):
    for k in range(3):
        kk=(round(tuv_new[t,k,0],6), round(tuv_new[t,k,1],6))
        if kk in key: uni(key[kk], n_*3+k)
        else: key[kk]=n_*3+k
    uni(n_*3,n_*3+1); uni(n_*3,n_*3+2)
chart_of = {}
for n_,t in enumerate(it):
    chart_of.setdefault(find(n_*3), []).append(t)
charts = list(chart_of.values())
P("島 charts=%d（改前 43）" % len(charts))

# chart → 它涵蓋的 loop（用來平移）
loops_of = []
for tris in charts:
    ls = set()
    for t in tris: ls.update(tl[t].tolist())
    loops_of.append(np.fromiter(ls, np.int64))

# 障礙＝非島三角形（bbox 保守）
R = 512
occ = np.zeros((R,R), bool)
tuv_b = uv_old[tl]
for t in np.nonzero(~isl_t)[0]:
    u = tuv_b[t]*R
    x0=max(0,int(np.floor(u[:,0].min()))); x1=min(R-1,int(np.ceil(u[:,0].max())))
    y0=max(0,int(np.floor(u[:,1].min()))); y1=min(R-1,int(np.ceil(u[:,1].max())))
    if x1>=x0 and y1>=y0: occ[y0:y1+1,x0:x1+1]=True
o2=occ.copy()
for dy in (-2,-1,0,1,2):
    for dx in (-2,-1,0,1,2): o2 |= np.roll(np.roll(occ,dy,0),dx,1)
free=~o2
best=(0,0,0,0,0); h=np.zeros(R,int)
for y in range(R):
    h=np.where(free[y],h+1,0); st=[]
    for x in range(R+1):
        curh=h[x] if x<R else 0; start=x
        while st and st[-1][1]>=curh:
            sx,sh=st.pop()
            if sh*(x-sx)>best[0]: best=(sh*(x-sx),sx,y-sh+1,x-sx,sh)
            start=sx
        st.append((start,curh))
_,bx,by,bw,bh = best
RX0,RY0,RW,RH = bx/R, by/R, bw/R, bh/R
P("空矩形：UV (%.4f,%.4f) 大小 %.4f x %.4f" % (RX0,RY0,RW,RH))

# 貨架打包（高的先放；決定性＝先依高度再依現有 bbox 排序）
boxes=[]
for ci,tris in enumerate(charts):
    u = uv_new[loops_of[ci]]
    boxes.append((ci, u[:,0].min(), u[:,1].min(), u[:,0].max()-u[:,0].min(), u[:,1].max()-u[:,1].min()))
boxes.sort(key=lambda b: (-b[4], -b[3], b[1], b[2]))
cx, cy, shelf_h = RX0+GUTTER_UV, RY0+GUTTER_UV, 0.0
place={}
for ci,ux,uy,w_,h_ in boxes:
    if cx + w_ > RX0+RW-GUTTER_UV:
        cx = RX0+GUTTER_UV; cy += shelf_h + GUTTER_UV; shelf_h = 0.0
    assert cy + h_ <= RY0+RH-GUTTER_UV, "空矩形放不下（需要更好的打包或第二塊空地）"
    place[ci]=(cx-ux, cy-uy)
    cx += w_ + GUTTER_UV
    shelf_h = max(shelf_h, h_)
for ci,(dx,dy) in place.items():
    uv_new[loops_of[ci]] += np.array([dx,dy])
P("打包完成：%d charts 放進空矩形，最終高度用到 %.4f / %.4f" % (len(charts), cy+shelf_h-RY0, RH))

# ---------------- 契約 ----------------
assert uv_new[isl_loop].min() >= 0.0 and uv_new[isl_loop].max() <= 1.0, "島 UV 出界"
A = 2048
m_isl = np.zeros((A,A), bool); m_bod = np.zeros((A,A), bool)
tuv_fin = uv_new[tl]
for t in np.nonzero(isl_t)[0]:
    u=tuv_fin[t]*A
    x0=max(0,int(np.floor(u[:,0].min()))); x1=min(A-1,int(np.ceil(u[:,0].max())))
    y0=max(0,int(np.floor(u[:,1].min()))); y1=min(A-1,int(np.ceil(u[:,1].max())))
    m_isl[y0:y1+1,x0:x1+1]=True
for t in np.nonzero(~isl_t)[0]:
    u=tuv_fin[t]*A
    x0=max(0,int(np.floor(u[:,0].min()))); x1=min(A-1,int(np.ceil(u[:,0].max())))
    y0=max(0,int(np.floor(u[:,1].min()))); y1=min(A-1,int(np.ceil(u[:,1].max())))
    m_bod[y0:y1+1,x0:x1+1]=True
ov = (m_isl & m_bod).sum()
P("重疊檢查（2048 bbox 保守）：島∩身體 = %d 紋素" % ov)
assert ov == 0, "島壓到身體的 chart 上了——中止"

f50,f10,fsub = line_texels(co, tv, tuv_fin, isl_t)
fd = area_density(co, tv, tuv_fin, isl_t)
P("")
P("========== 驗收 ==========")
P("             p50    p10   <2紋素   面積密度")
P("身體基準    %5.2f  %5.2f   %4.1f%%   %.4f" % (b50,b10,bsub,REF))
P("五島 改前   %5.2f  %5.2f   %4.1f%%   -" % (i50,i10,isub))
P("五島 改後   %5.2f  %5.2f   %4.1f%%   %.4f" % (f50,f10,fsub,fd))
assert fsub <= bsub, "改後 <2紋素 比身體還糟"
assert f10 >= b10*0.95, "改後 p10 沒有達到身體水準"
assert abs(fd/REF-1.0) < 0.02, "密度沒對齊 REF"
# 身體 loop 最終仍須逐位不動
assert np.array_equal(uv_new[~isl_loop], uv_old[~isl_loop]), "身體 UV 最終不一致"
P("契約全過。")

if DRYRUN:
    P("DRYRUN=1 ⇒ 不寫檔。")
else:
    if not os.path.exists(BACKUP):
        shutil.copyfile(MASTER, BACKUP); P("已備份 → %s" % os.path.basename(BACKUP))
    me.uv_layers["UVMap"].data.foreach_set("uv", uv_new.reshape(-1))
    me.update()
    np.savez(CORR, uv_old=uv_old, uv_new=uv_new, isl_loop=isl_loop)
    bpy.ops.wm.save_as_mainfile(filepath=MASTER)
    P("已存 master；對應表 → %s" % os.path.basename(CORR))
