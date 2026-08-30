# 哪些 UV0 錨定貼圖在解剖島上真的有內容？（唯讀）——決定「搬島」要付多少錢
import bpy, os
import numpy as np
from collections import deque
SA = r"C:\games\Unreal Engine\nice_ink\SourceAssets"
MASTER = os.path.join(SA, "sumo_character_master.blend")
ISLAND_MAX = 5000
TEX = ["body_ao.png","body_ao_shadow.png","body_height.png","body_cloth_normal.png",
       "fundoshi_mask_sharp.png","fundoshi_mask_final.png","fundoshi_edge_shadow.png",
       "hair_mask.png","face_mask.png","body_chroma_anat.png"]
def P(*a): print(*a, flush=True)

bpy.ops.wm.open_mainfile(filepath=MASTER)
ob = bpy.data.objects["SumoRetopo"]; me = ob.data
nv = len(me.vertices)
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
isl = np.zeros(nt, bool)
for i in small: isl |= (comp[tv[:,0]]==i)&(comp[tv[:,1]]==i)&(comp[tv[:,2]]==i)

def raster(mask, R):
    """把選定三角形掃進 RxR 的布林圖（保守：用 bbox 內的重心測試）"""
    out = np.zeros((R,R), bool)
    for t in np.nonzero(mask)[0]:
        u = tuv[t]*R
        x0=max(0,int(np.floor(u[:,0].min()))); x1=min(R-1,int(np.ceil(u[:,0].max())))
        y0=max(0,int(np.floor(u[:,1].min()))); y1=min(R-1,int(np.ceil(u[:,1].max())))
        if x1<x0 or y1<y0: continue
        xs,ys = np.meshgrid(np.arange(x0,x1+1)+0.5, np.arange(y0,y1+1)+0.5)
        d0=u[1]-u[0]; d1=u[2]-u[0]
        den=d0[0]*d1[1]-d1[0]*d0[1]
        if abs(den)<1e-9: continue
        vx=xs-u[0,0]; vy=ys-u[0,1]
        b1=(vx*d1[1]-d1[0]*vy)/den; b2=(d0[0]*vy-vx*d0[1])/den
        ins=(b1>=-0.01)&(b2>=-0.01)&(b1+b2<=1.01)
        out[y0:y1+1, x0:x1+1] |= ins
    return out

P("貼圖                        尺寸    島上內容(mean/std)      非島皮膚(mean/std)   判定")
for name in TEX:
    fp = os.path.join(SA, name)
    if not os.path.exists(fp):
        P("%-26s (不存在)" % name); continue
    img = bpy.data.images.load(fp, check_existing=False)
    w,h = img.size
    px = np.empty(w*h*4, np.float32); img.pixels.foreach_get(px)
    px = px.reshape(h,w,4)
    lum = px[:,:,:3].mean(axis=2)
    R = min(w, 1024)
    m_isl = raster(isl, R)
    m_all = raster(np.ones(nt,bool), R)
    m_skin = m_all & ~m_isl
    if R != w:
        f = w//R
        lum_s = lum[:R*f,:R*f].reshape(R,f,R,f).mean(axis=(1,3))
    else:
        lum_s = lum
    a = lum_s[m_isl]; b = lum_s[m_skin]
    if a.size == 0:
        P("%-26s %4dx%-4d (島上無取樣)" % (name,w,h)); continue
    flat_isl = a.std() < 0.01
    P("%-26s %4dx%-4d  %.4f / %.4f       %.4f / %.4f   %s"
      % (name, w, h, a.mean(), a.std(), b.mean(), b.std(),
         "島上是常數＝搬家免費" if flat_isl else "**島上有內容＝必須跟著搬**"))
    bpy.data.images.remove(img)
