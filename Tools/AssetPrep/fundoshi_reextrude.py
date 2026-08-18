"""褌重新擠出：用**平滑的表面法線場**取代舊 Solidify 的配對方向（2026-08-18）。

**病根（量測定罪）**：舊配對方向（內→外）相鄰夾角 p50 5.27° / p90 18.81° / p99 51.55°
——那是 v70 時代 Solidify even-offset + clamp + 去刺 pass 留下的爛方向場。
厚度 0.8mm 時橫向誤差 0.07~0.7mm＝次像素、看不見；
加厚到 15mm 後同一組角度變成 1.4~13mm 的橫向抖動＝user 看到的梳齒鋸齒。
**加厚不會製造鋸齒，它只是把既有的方向雜訊乘以厚度。**

修法：內層（貼身面，＝覆蓋輪廓的真相，一顆不動）算角度加權法線 → 沿表面
拉普拉斯平滑 → 用它當唯一擠出方向。外層＝內層 + 平滑法線 x 厚度。

Run: blender --background <master.blend> --python fundoshi_reextrude.py
"""
import bpy, numpy as np, os, shutil
from collections import defaultdict

ROOT = r"C:\games\Unreal Engine\nice_ink"
MASTER = os.path.join(ROOT, "SourceAssets", "sumo_character_master.blend")
SRC = os.path.join(ROOT, "SourceAssets", "masters", "sumo_character_master_v19_prebevel.blend")
THICK_MM = 15.0
SMOOTH_ITERS = 25
LAM = 0.6

print(f"來源＝{os.path.basename(SRC)}（15mm、輪廓已平滑、未 bevel）")
bpy.ops.wm.open_mainfile(filepath=SRC)
if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')
ob = bpy.data.objects["Fundoshi"]; me = ob.data
n = len(me.vertices); half = n // 2
assert n == 7134, f"來源不是未 bevel 版（verts={n}）"
co = np.empty(n*3); me.vertices.foreach_get("co", co); co = co.reshape(n,3)*1000.0
inner0 = co[half:].copy()
old_dir = co[:half] - co[half:]
old_dir /= np.maximum(np.linalg.norm(old_dir, axis=1), 1e-9)[:, None]

# 內層面的角度加權法線（Blender 5 headless 讀不到頂點法線＝已知坑，自己算）
me.calc_loop_triangles()
tris = np.empty(len(me.loop_triangles)*3, dtype=np.int64)
me.loop_triangles.foreach_get("vertices", tris); tris = tris.reshape(-1,3)
inner_tris = tris[(tris >= half).all(axis=1)] - half
P = inner0
A,B,C = P[inner_tris[:,0]], P[inner_tris[:,1]], P[inner_tris[:,2]]
fn = np.cross(B-A, C-A); fn /= np.maximum(np.linalg.norm(fn,axis=1,keepdims=True),1e-18)
N = np.zeros((half,3))
def corner(p,q,r):
    u=q-p; v=r-p
    u/=np.maximum(np.linalg.norm(u,axis=1,keepdims=True),1e-18)
    v/=np.maximum(np.linalg.norm(v,axis=1,keepdims=True),1e-18)
    return np.arccos(np.clip((u*v).sum(1),-1,1))
for k,(a,b,c) in enumerate(((0,1,2),(1,2,0),(2,0,1))):
    w = corner(P[inner_tris[:,a]], P[inner_tris[:,b]], P[inner_tris[:,c]])
    np.add.at(N, inner_tris[:,a], fn*w[:,None])
ln = np.linalg.norm(N,axis=1,keepdims=True)
loose = ln[:,0] < 1e-12
N[loose] = old_dir[loose]; ln[loose] = 1.0
N /= ln
# 朝外（用舊方向定號——舊方向噪但正負是對的）
flip = (N*old_dir).sum(1) < 0
N[flip] *= -1.0

# 沿表面平滑（只走內層鄰接）
vadj = defaultdict(set)
for t in inner_tris:
    for i in range(3):
        vadj[int(t[i])].add(int(t[(i+1)%3])); vadj[int(t[(i+1)%3])].add(int(t[i]))
idx = np.zeros(sum(len(v) for v in vadj.values()), dtype=np.int64)
ptr = np.zeros(half+1, dtype=np.int64); pos = 0
for i in range(half):
    ptr[i] = pos
    for j in vadj.get(i, ()): idx[pos] = j; pos += 1
ptr[half] = pos
cnt = np.maximum(ptr[1:]-ptr[:-1], 1)
for _ in range(SMOOTH_ITERS):
    acc = np.zeros_like(N)
    np.add.at(acc, np.repeat(np.arange(half), ptr[1:]-ptr[:-1]), N[idx])
    N = (1-LAM)*N + LAM*(acc/cnt[:,None])
    N /= np.maximum(np.linalg.norm(N,axis=1,keepdims=True),1e-12)

def dir_scatter(D, tag):
    euse = defaultdict(int)
    for p in me.polygons:
        vs=[int(v) for v in p.vertices]
        if max(vs)>=half: continue
        for i in range(len(vs)):
            euse[tuple(sorted((vs[i],vs[(i+1)%len(vs)])))]+=1
    adj=defaultdict(list)
    for e,c in euse.items():
        if c==1: adj[e[0]].append(e[1]); adj[e[1]].append(e[0])
    ang=[]
    for a,nb in adj.items():
        for b in nb:
            ang.append(np.degrees(np.arccos(np.clip(np.dot(D[a],D[b]),-1,1))))
    ang=np.array(ang)
    print(f"  {tag}: 相鄰方向夾角 p50={np.percentile(ang,50):.2f} p90={np.percentile(ang,90):.2f} "
          f"p99={np.percentile(ang,99):.2f}deg -> 外緣抖動 p90={2*THICK_MM*np.sin(np.radians(np.percentile(ang,90))/2):.2f}mm")
dir_scatter(old_dir, "舊方向場")
dir_scatter(N, "平滑法線場")

co[:half] = inner0 + N*THICK_MM
assert np.abs(co[half:] - inner0).max() < 1e-9, "內層被動到了"
print(f"**內層位移 max = 0.000000 mm（露膚度零損失）**")
th = np.linalg.norm(co[:half]-co[half:], axis=1)
print(f"厚度(mm) p50={np.percentile(th,50):.3f} min={th.min():.3f} max={th.max():.3f}")
me.vertices.foreach_set("co", (co/1000.0).ravel()); me.update()
bpy.ops.wm.save_mainfile(filepath=MASTER)
print("SAVED master")
