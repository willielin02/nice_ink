# UV0 圖集現況調查（唯讀）：解剖島的拓樸、UV 佔位、以及圖集空地在哪
import bpy, os
import numpy as np
from collections import deque, defaultdict
SA = r"C:\games\Unreal Engine\nice_ink\SourceAssets"
MASTER = os.path.join(SA, "sumo_character_master.blend")
ISLAND_MAX = 5000
GRID = 256
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
names=["乳頭核心A","乳頭核心B","乳暈A","乳暈B","肚臍"]

P("---- 解剖島的 3D 拓樸（邊界迴圈數：1=圓盤、2=環狀需要剪一刀）----")
# 邊界邊＝只被一個面用到的邊
face_edges = defaultdict(int)
npoly = len(me.polygons)
pv = [list(p.vertices) for p in me.polygons]
for f in pv:
    for k in range(len(f)):
        e = (min(f[k],f[(k+1)%len(f)]), max(f[k],f[(k+1)%len(f)]))
        face_edges[e]+=1
bedges=[e for e,c in face_edges.items() if c==1]
for k,i in enumerate(small):
    vm = comp==i
    be=[e for e in bedges if vm[e[0]]]
    ga=defaultdict(list)
    for a,b in be: ga[a].append(b); ga[b].append(a)
    seen=set(); loops=0
    for s in ga:
        if s in seen: continue
        loops+=1; q=deque([s]); seen.add(s)
        while q:
            u=q.popleft()
            for v in ga[u]:
                if v not in seen: seen.add(v); q.append(v)
    P("%-10s verts=%5d 邊界邊=%4d 邊界迴圈=%d  %s"
      % (names[k], sizes[i], len(be), loops, "環狀（要剪一刀）" if loops>=2 else "圓盤"))

P("")
P("---- UV 佔位 ----")
occ = np.zeros((GRID,GRID), bool)
def rast(mask, target):
    for t in np.nonzero(mask)[0]:
        u=tuv[t]
        x0=max(0,int(np.floor(u[:,0].min()*GRID))); x1=min(GRID-1,int(np.ceil(u[:,0].max()*GRID)))
        y0=max(0,int(np.floor(u[:,1].min()*GRID))); y1=min(GRID-1,int(np.ceil(u[:,1].max()*GRID)))
        target[y0:y1+1, x0:x1+1] = True
alltri = np.ones(nt, bool)
rast(alltri, occ)
P("圖集被佔用（%dx%d 粗格）：%.1f%%   空地：%.1f%%" % (GRID,GRID, occ.mean()*100, (1-occ.mean())*100))
tot_uv = 0.0
for k,i in enumerate(small):
    m=(comp[tv[:,0]]==i)&(comp[tv[:,1]]==i)&(comp[tv[:,2]]==i)
    u=tuv[m].reshape(-1,2)
    q0,q1,q2 = tuv[m][:,0],tuv[m][:,1],tuv[m][:,2]
    auv = 0.5*np.abs(np.cross(q1-q0,q2-q0)).sum()
    tot_uv += auv
    P("%-10s UV bbox=[%.4f,%.4f]x[%.4f,%.4f]  寬高=%.4f x %.4f  UV面積=%.6f"
      % (names[k], u[:,0].min(),u[:,0].max(),u[:,1].min(),u[:,1].max(),
         u[:,0].max()-u[:,0].min(), u[:,1].max()-u[:,1].min(), auv))
P("五島 UV 總面積 = %.6f（佔圖集 %.3f%%）" % (tot_uv, tot_uv*100))

# 身體參考密度（uv 單位 / cm，Jacobian 最大奇異值的倒數不好平均，改用面積式中位）
p0,p1,p2 = co[tv][:,0],co[tv][:,1],co[tv][:,2]
q0,q1,q2 = tuv[:,0],tuv[:,1],tuv[:,2]
A3 = 0.5*np.linalg.norm(np.cross(p1-p0,p2-p0),axis=1)
Auv= 0.5*np.abs(np.cross(q1-q0,q2-q0))
ok=(A3>1e-12)&(Auv>1e-16)
body=np.zeros(nt,bool)
for i in [i for i in range(nc) if sizes[i]>ISLAND_MAX]: body |= comp[tv[:,0]]==i
dens = np.zeros(nt); dens[ok]=np.sqrt(Auv[ok]/A3[ok])   # uv / m
bm = body&ok
w=A3[bm]; o=np.argsort(dens[bm])
ref = dens[bm][o][np.searchsorted(np.cumsum(w[o]/w.sum()),0.5)]
P("")
P("身體參考面積密度 = %.4f uv/m = %.6f uv/cm" % (ref, ref/100.0))
for k,i in enumerate(small):
    m=(comp[tv[:,0]]==i)&(comp[tv[:,1]]==i)&(comp[tv[:,2]]==i)&ok
    w=A3[m]; o=np.argsort(dens[m])
    d=dens[m][o][np.searchsorted(np.cumsum(w[o]/w.sum()),0.5)]
    P("%-10s 面積密度 = %.4f uv/m  → 相對身體 %.3f 倍（1.0=一樣）" % (names[k], d, d/ref))
