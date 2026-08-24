"""褌埋深的姿勢穩健性（2026-08-24）：布緣只埋在皮膚下 0.8mm——**那是靜止姿勢量的**。

為什麼要驗：走路（摺り足）、五根 Jiggle 骨的世界空間彈簧、躺姿都會讓皮膚形變。
布繼承皮膚的權重，理論上同步，但切點的權重是沿邊線性內插，而皮膚表面在 LBS 下是
「先變換再內插」——兩者不等價。差多少沒人量過。若埋深在某個姿勢跌破 0，
布緣（黑色的牆）會刺出皮膚＝一圈黑邊，而且只在動的時候出現。

做法：直接擺姿勢，用 evaluated mesh 量布相對皮膚的帶號距離。這不是取樣測試，
是對 LBS 的直接量測——同一副骨架、同一組權重，姿勢是唯一變因。
唯讀：不寫任何資產。
Run: blender --background --python fundoshi_pose_check.py

**2026-08-24 修正（第一版是空洞契約，血價記此）**：master 的 armature 出廠
`pose_position='REST'`，而且 pose 裡還留著一組 DrawPose（9 根骨非單位）。
第一版只寫 `rotation_euler`、沒切 POSE ⇒ **七個姿勢量到的全是 rest 網格**，
所以「埋深全等 −0.798mm」不是「LBS 構造保證」，是根本沒擺過姿勢。
修：①切 `pose_position='POSE'` ②用 `matrix_basis.identity()` 清掉殘留 DrawPose
（實測清完與 REST 逐位相同＝基準乾淨）③**每個姿勢都驗「網格真的動了」的下限**
（`MIN_MOVE_MM`）——本專案鐵則：不該動的契約只驗上限，什麼都沒發生也會 PASS。
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


MIN_MOVE_MM = 5.0   # 下限自檢：每個測試姿勢至少要讓皮膚動這麼多，否則契約是空的

bpy.ops.wm.open_mainfile(filepath=BLEND)
body = bpy.data.objects["SumoRetopo"]
fund = bpy.data.objects["Fundoshi"]
arm = bpy.data.objects["Skeleton_Plus-size"]
# 出廠是 REST（＋殘留 DrawPose）：不切 POSE 的話下面每一個姿勢都是白擺的
arm.data.pose_position = 'POSE'
for _pb in arm.pose.bones:
    _pb.matrix_basis.identity()
bpy.context.view_layer.update()

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


def skin_co():
    dg = bpy.context.evaluated_depsgraph_get()
    ev = body.evaluated_get(dg)
    m = ev.to_mesh()
    co = np.empty(len(m.vertices) * 3)
    m.vertices.foreach_get("co", co)
    ev.to_mesh_clear()
    return co.reshape(-1, 3)


REST_SKIN = None


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
    mv = 0.0 if REST_SKIN is None else float(np.linalg.norm(skin_co() - REST_SKIN, axis=1).max() * 1000.0)
    flag = "" if (REST_SKIN is None or mv >= MIN_MOVE_MM) else "  <<< 下限自檢 FAIL：網格沒動，這一列不算數"
    P(f"  {tag:<26} 網格動了 {mv:7.1f}mm  布緣埋深 mm: p50 {np.percentile(r,50):7.3f}  "
      f"p99 {np.percentile(r,99):7.3f}  max {r.max():7.3f}   刺出皮膚 {out} 個{flag}")
    return r.max(), out, mv


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
base_max, base_out, _ = measure("rest（基準）")
REST_SKIN = skin_co()

D = math.radians
thigh = match("thigh", "upleg", "leg_", "femur")
# 「軀幹前彎」必須用 Spine：轉 Hips 是整具剛體旋轉、相對形變為零＝又一個空洞測試
spine = match("spine") or match("hips", "pelvis", "torso", "waist")
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
# 肚骨單獨列出＝08-24 實測的真兇（權重域一路吃到大腿前側＝布邊所在）。
# 25° 是引擎鉗位本人（NiceInkCharacter.cpp MaxRollRad 0.44rad）；10° 是走路常態量級。
for _d in (10, 25):
    if "Jiggle_Belly" in names:
        tests.append((f"Jiggle_Belly 繞X {_d}°", [("Jiggle_Belly", (D(_d), 0, 0))]))

worst = base_max
worst_tag = "rest"
hollow = []
for tag, pairs in tests:
    set_pose(pairs)
    mx, out, mv = measure(tag)
    if mv < MIN_MOVE_MM:
        hollow.append(tag)
        continue
    if mx > worst:
        worst, worst_tag = mx, tag
set_pose([])

P("")
if hollow:
    P(f"下限自檢 FAIL：{len(hollow)} 個姿勢根本沒讓網格動 -> {hollow}；契約無效，先修姿勢設定")
P(f"最壞姿勢＝{worst_tag}，埋深最大值 {worst:.3f} mm（<0 表示全程埋在皮下）")
if worst < 0:
    P("契約 PASS：所有測試姿勢下布緣都沒有刺出皮膚")
else:
    P(f"契約 FAIL：有姿勢讓布緣刺出皮膚 {worst:.3f} mm")
os.makedirs(os.path.dirname(OUT), exist_ok=True)
open(OUT, "w", encoding="utf-8").write("\n".join(rep) + "\nDONE\n")
P("WROTE " + OUT)
