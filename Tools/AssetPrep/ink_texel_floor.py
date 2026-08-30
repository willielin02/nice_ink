# 紋素密度樓地板：一條 3mm 的線，在各區域「有幾個紋素寬」（唯讀離線）
#
# 2026-08-30 三修：把「一針太胖」修成「一針正確的實體寬度」之後，user 的畫面顯示
# 乳暈內的線變成**又細又斷**。那代表真正的約束不在章的大小，在**那裡有幾個紋素**。
# 這支就量那件事：texels/mm、以及 3.0mm 的線佔幾個紋素。
import bpy, os
import numpy as np
from collections import deque
SA = r"C:\games\Unreal Engine\nice_ink\SourceAssets"
MASTER = os.path.join(SA, "sumo_character_master.blend")
RT = 4096
LINE_MM = 3.0
ISLAND_MAX = 5000
def P(*a): print(*a, flush=True)

bpy.ops.wm.open_mainfile(filepath=MASTER)
ob = bpy.data.objects["SumoRetopo"]; me = ob.data
mw = np.array(ob.matrix_world); nv = len(me.vertices)
co = np.empty(nv*3); me.vertices.foreach_get("co", co)
co = co.reshape(nv,3) @ mw[:3,:3].T + mw[:3,3]
me.calc_loop_triangles(); nt = len(me.loop_triangles)
tv = np.empty(nt*3,np.int64); me.loop_triangles.foreach_get("vertices",tv); tv=tv.reshape(nt,3)
tl = np.empty(nt*3,np.int64); me.loop_triangles.foreach_get("loops",tl); tl=tl.reshape(nt,3)
uva = np.empty(len(me.loops)*2); me.uv_layers[0].data.foreach_get("uv",uva); uva=uva.reshape(-1,2)
tuv = uva[tl]
p0,p1,p2 = co[tv][:,0],co[tv][:,1],co[tv][:,2]
q0,q1,q2 = tuv[:,0],tuv[:,1],tuv[:,2]
A3 = 0.5*np.linalg.norm(np.cross(p1-p0,p2-p0),axis=1)
e0uv,e1uv = q1-q0, q2-q0
e03d,e13d = p1-p0, p2-p0
det = e0uv[:,0]*e1uv[:,1]-e0uv[:,1]*e1uv[:,0]
good = (A3>1e-12)&(np.abs(det)>1e-14)
J = np.zeros((nt,3,2))
for k in range(3):
    J[good,k,0] = (e03d[good,k]*e1uv[good,1] - e13d[good,k]*e0uv[good,1])/det[good]
    J[good,k,1] = (e13d[good,k]*e0uv[good,0] - e03d[good,k]*e1uv[good,0])/det[good]
sv = np.zeros((nt,2)); sv[good] = np.linalg.svd(J[good],compute_uv=False)
# sigma1 = m per uv (最大拉伸方向)。紋素/mm = (RT uv->px) / (m/uv * 1000 mm/m)
tex_per_mm = RT/(sv[:,0]*1000.0)
line_tex = LINE_MM*tex_per_mm

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
small=sorted([i for i in range(nc) if sizes[i]<=ISLAND_MAX], key=lambda i: sizes[i])
body=np.zeros(nt,bool)
for i in [i for i in range(nc) if sizes[i]>ISLAND_MAX]: body |= comp[tv[:,0]]==i

P("RT=%d  一條 %.1fmm 的線佔幾個紋素（面積加權；<3 紋素＝抗鋸齒失效、<1＝根本畫不出線）" % (RT, LINE_MM))
P("")
P("%-16s %14s %14s %14s %14s" % ("區域","紋素/mm 中位","線寬(紋素) p50","p10","<2紋素 佔面積"))
def rep(lbl,m):
    m=m&good
    if m.sum()<3: return
    w=A3[m]; w=w/w.sum()
    def wq(a,q):
        o=np.argsort(a); return a[o][np.searchsorted(np.cumsum(w[o]),q)]
    lt=line_tex[m]
    P("%-16s %14.3f %14.2f %14.2f %13.1f%%" %
      (lbl, wq(tex_per_mm[m],.5), wq(lt,.5), wq(lt,.10), w[lt<2.0].sum()*100))
rep("身體＋頭（對照）", body)
names=["乳頭核心A","乳頭核心B","乳暈A","乳暈B","肚臍"]
for k,i in enumerate(small):
    m=(comp[tv[:,0]]==i)&(comp[tv[:,1]]==i)&(comp[tv[:,2]]==i)
    rep(names[k] if k<len(names) else "島%d"%i, m)
P("")
P("設計文件宣稱：0.617 px/mm @2048 ⇒ 1.234 px/mm @4096、線寬 3.70 紋素")
