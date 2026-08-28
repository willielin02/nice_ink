# 驗證：掃描用的姿勢裡，五根 Jiggle 骨到底有沒有在動（2026-08-27）
#
# 動機：使用者質疑「背部皮肉也會動，你有確保肥肉彈跳關閉嗎」。
# 我原本只是「假設」reset_pose 把所有骨頭歸零＝彈跳關閉——那是推論不是量測。
#
# 三個對照（空對照＋陽性對照，缺一不可——只驗上限的契約「什麼都沒發生」也會過）：
#   A 擺出 pick_a，記錄頂點雜湊
#   B 原封不動重跑一次 → 必須逐位相同（證明流程本身決定性）
#   C 把 Jiggle_Belly 轉 1° → 必須不同（證明「如果彈跳真的動了，這支尺看得見」）
# 另外列出 Jiggle 骨的 constraint / driver：那是唯一能繞過 reset_pose 的路徑。
#
# Run: blender --background <master.blend> --python pose_domain_jigglecheck.py
import bpy
import sys
import os
import math
import hashlib
import numpy as np
from mathutils import Quaternion

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pose_domain_ladder import LADDER  # noqa: E402
from pose_domain_rig import apply_ops  # noqa: E402

arm = bpy.data.objects["Skeleton_Plus-size"]
arm.data.pose_position = "POSE"
if arm.animation_data:
    arm.animation_data.action = None
BODY = bpy.data.objects["SumoRetopo"]
JIG = [b.name for b in arm.data.bones if b.name.startswith("Jiggle")]
print("JIGGLE BONES:", JIG)


def reset_pose():
    for pb in arm.pose.bones:
        pb.rotation_mode = 'QUATERNION'
        pb.rotation_quaternion = (1.0, 0.0, 0.0, 0.0)
        pb.location = (0.0, 0.0, 0.0)
        pb.scale = (1.0, 1.0, 1.0)


def verts():
    dg = bpy.context.evaluated_depsgraph_get()
    ev = BODY.evaluated_get(dg)
    me = ev.to_mesh()
    n = len(me.vertices)
    co = np.empty(n * 3, dtype=np.float64)
    me.vertices.foreach_get("co", co)
    ev.to_mesh_clear()
    return co


def h(co):
    return hashlib.sha1(co.tobytes()).hexdigest()[:16]


OPS = dict((t, o) for _g, t, _l, o in LADDER)["pick_a"]

# --- 0. constraint / driver 稽核（唯一能繞過 reset_pose 的路徑） ---
nc = 0
for pb in arm.pose.bones:
    if len(pb.constraints):
        nc += 1
        print("CONSTRAINT on %s: %s" % (pb.name, [c.type for c in pb.constraints]))
drv = []
if arm.animation_data:
    drv = [d.data_path for d in arm.animation_data.drivers]
print("CONSTRAINTS total=%d  DRIVERS total=%d" % (nc, len(drv)))
for d in drv[:20]:
    print("  driver:", d)

# --- A ---
reset_pose()
apply_ops(arm, OPS)
bpy.context.view_layer.update()
A = verts()
print("A pick_a           hash=%s" % h(A))
for bn in JIG:
    pb = arm.pose.bones[bn]
    q = pb.rotation_quaternion
    ang = math.degrees(2.0 * math.acos(min(1.0, abs(q.w))))
    print("  %s local_rot=%.4f° loc=(%.4f,%.4f,%.4f)"
          % (bn, ang, pb.location.x, pb.location.y, pb.location.z))

# --- B 空對照 ---
reset_pose()
apply_ops(arm, OPS)
bpy.context.view_layer.update()
B = verts()
print("B rerun            hash=%s  identical=%s" % (h(B), np.array_equal(A, B)))

# --- C 陽性對照：Jiggle_Belly 轉 1° ---
reset_pose()
apply_ops(arm, OPS)
pb = arm.pose.bones["Jiggle_Belly"]
pb.rotation_quaternion = Quaternion((1, 0, 0), math.radians(1.0))
bpy.context.view_layer.update()
C = verts()
d = np.abs(C - A).reshape(-1, 3)
print("C jiggle+1deg      hash=%s  changed=%s  max_move=%.3fmm  verts_moved=%d"
      % (h(C), not np.array_equal(A, C), float(np.linalg.norm(d, axis=1).max()) * 1000.0,
         int((np.linalg.norm(d, axis=1) > 1e-9).sum())))
