# 解剖島重展 UV 的參數掃描（唯讀；不存檔）
# 驗收量＝ink_texel_floor 的那一個：一條 3mm 的線在那裡有幾個紋素寬。
# 身體基準：p50 3.39 / p10 2.82 / <2紋素 0.5%
import bpy, os, math
import numpy as np
from collections import deque
SA = r"C:\games\Unreal Engine\nice_ink\SourceAssets"
MASTER = os.path.join(SA, "sumo_character_master.blend")
RT, LINE_MM, ISLAND_MAX = 4096, 3.0, 5000
def P(*a): print(*a, flush=True)

def mesh_arrays(ob):
    me = ob.data
    mw = np.array(ob.matrix_world); nv = len(me.vertices)
    co = np.empty(nv*3); me.vertices.foreach_get("co", co)
    co = co.reshape(nv,3) @ mw[:3,:3].T + mw[:3,3]
    me.calc_loop_triangles(); nt = len(me.loop_triangles)
    tv = np.empty(nt*3,np.int64); me.loop_triangles.foreach_get("vertices",tv)
    tl = np.empty(nt*3,np.int64); me.loop_triangles.foreach_get("loops",tl)
    uva = np.empty(len(me.loops)*2); me.uv_layers["UVMap"].data.foreach_get("uv",uva)
    return co, tv.reshape(nt,3), uva.reshape(-1,2)[tl.reshape(nt,3)], nt

def metrics(co, tv, tuv, mask):
    p0,p1,p2 = co[tv][:,0],co[tv][:,1],co[tv][:,2]
    q0,q1,q2 = tuv[:,0],tuv[:,1],tuv[:,2]
    A3 = 0.5*np.linalg.norm(np.cross(p1-p0,p2-p0),axis=1)
    e0,e1 = q1-q0, q2-q0
    f0,f1 = p1-p0, p2-p0
    det = e0[:,0]*e1[:,1]-e0[:,1]*e1[:,0]
    good = mask & (A3>1e-12) & (np.abs(det)>1e-14)
    n = len(A3); J = np.zeros((n,3,2))
    for k in range(3):
        J[good,k,0] = (f0[good,k]*e1[good,1]-f1[good,k]*e0[good,1])/det[good]
        J[good,k,1] = (f1[good,k]*e0[good,0]-f0[good,k]*e1[good,0])/det[good]
    sv = np.zeros((n,2)); sv[good] = np.linalg.svd(J[good],compute_uv=False)
    line_tex = np.zeros(n); line_tex[good] = LINE_MM*RT/(sv[good,0]*1000.0)
    w = A3[good]; w = w/w.sum(); lt = line_tex[good]
    o = np.argsort(lt); cw = np.cumsum(w[o])
    return (lt[o][np.searchsorted(cw,.5)], lt[o][np.searchsorted(cw,.10)],
            w[lt<2.0].sum()*100, np.sqrt((0.5*np.abs(np.cross(e0,e1))[good]/A3[good])).mean())

bpy.ops.wm.open_mainfile(filepath=MASTER)
ob = bpy.data.objects["SumoRetopo"]; me = ob.data
co, tv, tuv0, nt = mesh_arrays(ob)
nv = len(me.vertices)
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
isl_v = np.zeros(nv, bool)
for i in small: isl_v |= comp==i
isl_t = isl_v[tv].all(axis=1)
body_t = ~isl_v[tv].any(axis=1)

# 身體參考（面積密度 uv/m）與基準線
p0,p1,p2 = co[tv][:,0],co[tv][:,1],co[tv][:,2]
A3 = 0.5*np.linalg.norm(np.cross(p1-p0,p2-p0),axis=1)
q0,q1,q2 = tuv0[:,0],tuv0[:,1],tuv0[:,2]
Auv = 0.5*np.abs(np.cross(q1-q0,q2-q0))
okb = body_t & (A3>1e-12) & (Auv>1e-16)
dens_b = np.sqrt(Auv[okb]/A3[okb]); wb = A3[okb]/A3[okb].sum()
o=np.argsort(dens_b); REF = dens_b[o][np.searchsorted(np.cumsum(wb[o]),0.5)]
P("身體參考面積密度 REF = %.4f uv/m" % REF)
b = metrics(co, tv, tuv0, body_t)
P("身體基準       線寬紋素 p50=%.2f p10=%.2f  <2紋素=%.1f%%" % (b[0],b[1],b[2]))
i0 = metrics(co, tv, tuv0, isl_t)
P("解剖島 現況    線寬紋素 p50=%.2f p10=%.2f  <2紋素=%.1f%%" % (i0[0],i0[1],i0[2]))
P("")
P("---- 掃描 smart_project 的 angle_limit（重展後統一縮放到 REF 密度）----")
P("%-10s %10s %10s %12s %8s" % ("angle","p50","p10","<2紋素","charts"))

poly_isl = np.array([all(isl_v[v] for v in p.vertices) for p in me.polygons])
for ang in [66, 55, 45, 30]:
    bpy.ops.wm.open_mainfile(filepath=MASTER)
    ob2 = bpy.data.objects["SumoRetopo"]; me2 = ob2.data
    me2.uv_layers.active = me2.uv_layers["UVMap"]
    for o_ in bpy.context.view_layer.objects: o_.select_set(False)
    ob2.select_set(True); bpy.context.view_layer.objects.active = ob2
    bpy.context.scene.tool_settings.use_uv_select_sync = True
    sel = np.zeros(len(me2.polygons), bool); sel[:len(poly_isl)] = poly_isl
    me2.polygons.foreach_set("select", sel)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_mode(type='FACE')
    bpy.ops.object.mode_set(mode='OBJECT')
    me2.polygons.foreach_set("select", sel)
    bpy.ops.object.mode_set(mode='EDIT')
    try:
        bpy.ops.uv.smart_project(angle_limit=math.radians(ang), island_margin=0.002,
                                 correct_aspect=True, scale_to_bounds=False)
    except Exception as e:
        P("%-10s EXC %s" % (ang, e)); bpy.ops.object.mode_set(mode='OBJECT'); continue
    bpy.ops.object.mode_set(mode='OBJECT')
    co2, tv2, tuv2, nt2 = mesh_arrays(ob2)
    # 統一縮放到 REF
    q0,q1,q2 = tuv2[:,0],tuv2[:,1],tuv2[:,2]
    Auv2 = 0.5*np.abs(np.cross(q1-q0,q2-q0))
    oki = isl_t & (A3>1e-12) & (Auv2>1e-16)
    di = np.sqrt(Auv2[oki]/A3[oki]); wi = A3[oki]/A3[oki].sum()
    o2=np.argsort(di); cur = di[o2][np.searchsorted(np.cumsum(wi[o2]),0.5)]
    tuv2 = tuv2*(REF/cur)
    m = metrics(co2, tv2, tuv2, isl_t)
    # UV chart 數＝以 UV 位置焊接後的連通元件（chart 邊界＝畫線會斷的地方）
    it = np.nonzero(isl_t)[0]
    key = {}; par = list(range(len(it)*3))
    def find(x):
        while par[x]!=x: par[x]=par[par[x]]; x=par[x]
        return x
    def uni(a,b):
        ra,rb=find(a),find(b)
        if ra!=rb: par[rb]=ra
    for n_,t in enumerate(it):
        for k in range(3):
            kk=(round(tuv2[t,k,0],6), round(tuv2[t,k,1],6))
            if kk in key: uni(key[kk], n_*3+k)
            else: key[kk]=n_*3+k
    for n_ in range(len(it)):
        uni(n_*3, n_*3+1); uni(n_*3, n_*3+2)
    charts = len({find(i) for i in range(len(it)*3)})
    q0,q1,q2 = tuv2[:,0],tuv2[:,1],tuv2[:,2]
    area = 0.5*np.abs(np.cross(q1-q0,q2-q0))[isl_t].sum()
    P("%-10s %10.2f %10.2f %11.1f%% %8d   UV面積=%.6f" % ("%d deg"%ang, m[0], m[1], m[2], charts, area))
