# 縫合版（記憶體重建）vs master：逐頂點位置/法線/權重差異審計
import bpy, bmesh, collections, math, sys
import numpy as np
from mathutils import Vector
MASTER = r"C:/games/Unreal Engine/nice_ink/SourceAssets/sumo_character_master.blend"
bpy.ops.wm.open_mainfile(filepath=MASTER)
body = bpy.data.objects["SumoRetopo"]; me = body.data; mw = body.matrix_world
# 快照 master：位置、loop 法線（per-vertex 平均）、權重
me.calc_normals_split() if hasattr(me,"calc_normals_split") else None
pos0 = {v.index: v.co.copy() for v in me.vertices}
nrm0 = collections.defaultdict(lambda: Vector((0,0,0)))
for l in me.loops: nrm0[l.vertex_index] += Vector(l.normal)
gi = {g.index: g.name for g in body.vertex_groups}
w0 = {v.index: {gi[g.group]: g.weight for g in v.groups} for v in me.vertices}
# ---- 執行縫合版建置腳本的前三刀（不匯出）：直接 exec 到匯出前 ----
src = open(r"C:/games/Unreal Engine/nice_ink/Tools/AssetPrep/build_sumo_skeletal_whole_fbx.py", encoding="utf-8").read()
cut = src.index("# ---------- ④")
code = src[:cut].replace('bpy.ops.wm.open_mainfile(filepath=MASTER)', '')  # 已開
# 焊縫會刪 84 個頂點→索引重排；先記 seam 頂點座標以便對映
exec(compile(code, "whole_build", "exec"), globals())
# 現在 me = 縫合版。用 KDTree 把縫合版頂點對回 master（rest 位置在帶外完全相同）
from mathutils.kdtree import KDTree
kd = KDTree(len(pos0))
for i,p in pos0.items(): kd.insert(p, i)
kd.balance()
me.calc_normals_split() if hasattr(me,"calc_normals_split") else None
nrm1 = collections.defaultdict(lambda: Vector((0,0,0)))
for l in me.loops: nrm1[l.vertex_index] += Vector(l.normal)
gi1 = {g.index: g.name for g in body.vertex_groups}
c_seam = c; n_seam = nrm  # from build script
rows=[]
for v in me.vertices:
    co, i0, d = kd.find(v.co)
    # 帶內頂點位置變了→用「平滑前」對映不可得；用最近 master 頂點（≤8mm）
    s = (mw @ v.co - c_seam).dot(n_seam)
    a = 0.0
    n_a = nrm0[i0].normalized() if nrm0[i0].length>1e-9 else None
    n_b = nrm1[v.index].normalized() if nrm1[v.index].length>1e-9 else None
    if n_a is not None and n_b is not None:
        a = math.degrees(math.acos(max(-1,min(1,n_a.dot(n_b)))))
    wa = w0[i0]; wb = {gi1[g.group]: g.weight for g in v.groups}
    keys = set(wa)|set(wb); wd = sum(abs(wa.get(k,0)-wb.get(k,0)) for k in keys if not k.startswith("MARK_") and k not in ("FaceUV","Scalp","BeardRoot"))
    rows.append((v.index, s, d*1000.0, a, wd, v.co.z, v.co.y, v.co.x))
# 分區：帶內 |s|<=5cm、帶外
import statistics as st
def rep(name, sel):
    if not sel: print(name, "n=0"); return
    print(f"{name}: n={len(sel)} posDiff mm mean {st.mean(r[2] for r in sel):.2f} max {max(r[2] for r in sel):.2f} | normalAng deg mean {st.mean(r[3] for r in sel):.2f} p95 {sorted(r[3] for r in sel)[int(len(sel)*0.95)]:.2f} max {max(r[3] for r in sel):.2f} | weightL1 mean {st.mean(r[4] for r in sel):.3f} max {max(r[4] for r in sel):.3f}")
inb=[r for r in rows if abs(r[1])<=0.05]; outb=[r for r in rows if abs(r[1])>0.05]
rep("BAND(|s|<=5cm)", inb); rep("OUTSIDE band", outb)
# 帶外法線差 >2° 的頂點：分佈在哪（z 分層 + 是否在中線 |x|<2cm）
bad=[r for r in outb if r[3]>2.0]
print("outside band verts with normalAng>2deg:", len(bad))
zb=collections.Counter(round(r[5],1) for r in bad); print(" z-histogram(0.1m):", sorted(zb.items())[:30])
mid=[r for r in bad if abs(r[7])<0.02]; print(" of which near midline |x|<2cm:", len(mid), " y range", (min((r[6] for r in mid),default=0), max((r[6] for r in mid),default=0)))
# 帶外位置差 >0.1mm（不該有）
pb=[r for r in outb if r[2]>0.1]; print("outside band verts moved >0.1mm:", len(pb))
# 帶外權重差 >0.01
wb_=[r for r in outb if r[4]>0.01]; print("outside band verts weight changed >0.01:", len(wb_))
