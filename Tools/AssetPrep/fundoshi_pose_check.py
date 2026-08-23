"""褌埋深的姿勢穩健性（2026-08-24）：布緣只埋在皮膚下 0.8mm——**那是靜止姿勢量的**。

為什麼要驗：走路（摺り足）、五根 Jiggle 骨的世界空間彈簧、躺姿都會讓皮膚形變。
布繼承皮膚的權重，理論上同步，但切點的權重是沿邊線性內插，而皮膚表面在 LBS 下是
「先變換再內插」——兩者不等價。差多少沒人量過。若埋深在某個姿勢跌破 0，
布緣（黑色的牆）會刺出皮膚＝一圈黑邊，而且只在動的時候出現。

做法：直接擺姿勢，用 evaluated mesh 量布相對皮膚的帶號距離。這不是取樣測試，
是對 LBS 的直接量測——同一副骨架、同一組權重，姿勢是唯一變因。
唯讀：不寫任何資產。
Run: blender --background --python fundoshi_pose_check.py
"""
import bpy, os, math
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

ROOT = r"C:\games\Unreal Engine\nice_ink"
BLEND = os.path.join(ROOT, "SourceAssets", "sumo_character_master.blend")
OUT = os.path.join(ROOT, "Saved", "fundoshi_pose_report.txt")
rep = []


def P(*a):
    s = " ".join(str(x) for x in a)
    print(s, flush=True)
    rep.append(s)


bpy.ops.wm.open_mainfile(filepath=BLEND)
body = bpy.data.objects["SumoRetopo"]
fund = bpy.data.objects["Fundoshi"]
arm = bpy.data.objects["Skeleton_Plus-size"]

me = fund.data
nV = len(me.vertices) // 2
E = np.empty(len(me.edges) * 2, np.int64); me.edges.foreach_get("vertices", E); E = E.reshape(-1, 2)
pair = np.zeros(nV, bool)
for a, b in E:
    a, b = int(a), int(b)
    if abs(a - b) == nV:
        pair[min(a, b)] = True
rim = np.where(pair)[0]
P(f"布緣頂點 {len(rim)}；布頂面頂點 {nV}")

names = [b.name for b in arm.pose.bones]
P(f"骨架 {len(names)} 骨: {', '.join(names)}")


def match(*keys):
    out = []
    for n in names:
        ln = n.lower()
        if any(k in ln for k in keys):
            out.append(n)
    return out


def measure(tag):
    dg = bpy.context.evaluated_depsgraph_get()
    bvh = BVHTree.FromObject(body, dg)
    ev = fund.evaluated_get(dg)
    m2 = ev.to_mesh()
    co = np.empty(len(m2.vertices) * 3); m2.vertices.foreach_get("co", co); co = co.reshape(-1, 3)
    mw = np.array(fund.matrix_world)
    co = co @ mw[:3, :3].T + mw[:3, 3]
    sd = np.empty(nV)
    for i in range(nV):
        loc, nor, _, _ = bvh.find_nearest(Vector(co[i]))
        sd[i] = np.dot(co[i] - np.array(loc), np.array(nor)) * 1000.0
    ev.to_mesh_clear()
    r = sd[rim]
    out = int((r > 0).sum())
    P(f"  {tag:<26} 布緣埋深 mm: p50 {np.percentile(r,50):7.3f}  p99 {np.percentile(r,99):7.3f}  "
      f"max {r.max():7.3f}   刺出皮膚 {out} 個")
    return r.max(), out


def set_pose(pairs):
    for pb in arm.pose.bones:
        pb.rotation_mode = 'XYZ'
        pb.rotation_euler = (0, 0, 0)
        pb.location = (0, 0, 0)
    for nm, eul in pairs:
        arm.pose.bones[nm].rotation_euler = eul
    bpy.context.view_layer.update()


P("")
P("========  埋深 vs 姿勢（正值＝刺出皮膚＝出事）  ========")
set_pose([])
base_max, base_out = measure("rest（基準）")

D = math.radians
thigh = match("thigh", "upleg", "leg_", "femur")
spine = match("spine", "hips", "pelvis", "torso", "waist")
jig = match("jiggle")
P(f"  比對用骨：thigh={thigh[:4]} spine={spine[:4]} jiggle={jig[:6]}")

tests = []
if len(thigh) >= 2:
    tests.append(("走路（大腿 ±25°）", [(thigh[0], (D(25), 0, 0)), (thigh[1], (D(-25), 0, 0))]))
    tests.append(("大步（大腿 ±45°）", [(thigh[0], (D(45), 0, 0)), (thigh[1], (D(-45), 0, 0))]))
    tests.append(("蹲踞（雙腿前 60° 外開 20°）",
                  [(thigh[0], (D(60), 0, D(20))), (thigh[1], (D(60), 0, D(-20)))]))
if spine:
    tests.append(("軀幹前彎 35°＋扭 25°", [(spine[0], (D(35), D(25), 0))]))
if jig:
    tests.append(("Jiggle 全骨 15°（極端）", [(n, (D(15), D(15), D(15))) for n in jig]))
if thigh and spine:
    tests.append(("複合：大腿 40°＋軀幹 30°",
                  [(thigh[0], (D(40), 0, 0)), (thigh[1], (D(-40), 0, 0)), (spine[0], (D(30), 0, 0))]))

worst = base_max
worst_tag = "rest"
for tag, pairs in tests:
    set_pose(pairs)
    mx, out = measure(tag)
    if mx > worst:
        worst, worst_tag = mx, tag
set_pose([])

P("")
P(f"最壞姿勢＝{worst_tag}，埋深最大值 {worst:.3f} mm（<0 表示全程埋在皮下）")
if worst < 0:
    P("契約 PASS：所有測試姿勢下布緣都沒有刺出皮膚")
else:
    P(f"契約 FAIL：有姿勢讓布緣刺出皮膚 {worst:.3f} mm")
os.makedirs(os.path.dirname(OUT), exist_ok=True)
open(OUT, "w", encoding="utf-8").write("\n".join(rep) + "\nDONE\n")
P("WROTE " + OUT)
