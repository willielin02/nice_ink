# 背部到底在動什麼（2026-08-27）：把背部頂點從靜止到姿勢的位移，用 Kabsch 拆成
# 「剛體跟著骨盆轉」與「真的形變」兩部分。前者是姿勢本來就該有的，後者才是問題。
import bpy, sys, os
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pose_domain_ladder import LADDER
from pose_domain_rig import apply_ops

arm = bpy.data.objects["Skeleton_Plus-size"]
arm.data.pose_position = "POSE"
if arm.animation_data:
    arm.animation_data.action = None
BODY = bpy.data.objects["SumoRetopo"]

def reset_pose():
    for pb in arm.pose.bones:
        pb.rotation_mode = 'QUATERNION'
        pb.rotation_quaternion = (1.0, 0.0, 0.0, 0.0)
        pb.location = (0.0, 0.0, 0.0); pb.scale = (1.0, 1.0, 1.0)

def verts():
    dg = bpy.context.evaluated_depsgraph_get()
    ev = BODY.evaluated_get(dg); me = ev.to_mesh()
    n = len(me.vertices); co = np.empty(n*3); me.vertices.foreach_get("co", co)
    ev.to_mesh_clear()
    mw = np.array(BODY.matrix_world)
    co = co.reshape(n,3) @ mw[:3,:3].T + mw[:3,3]
    return co

def kabsch(P, Q):
    pc, qc = P.mean(0), Q.mean(0)
    H = (P-pc).T @ (Q-qc)
    U,S,Vt = np.linalg.svd(H)
    d = np.sign(np.linalg.det(Vt.T @ U.T))
    R = Vt.T @ np.diag([1,1,d]) @ U.T
    fit = (P-pc) @ R.T + qc
    res = np.linalg.norm(fit-Q, axis=1)
    ang = np.degrees(np.arccos(np.clip((np.trace(R)-1)/2, -1, 1)))
    return ang, res

reset_pose(); bpy.context.view_layer.update()
R0 = verts()
OPS = dict((t,o) for _g,t,_l,o in LADDER)["pick_a"]
reset_pose(); apply_ops(arm, OPS); bpy.context.view_layer.update()
P1 = verts()

REGIONS = {
    "上背 (z1.15~1.45, y>0.05)": (R0[:,2]>1.15)&(R0[:,2]<1.45)&(R0[:,1]>0.05),
    "下背/腰 (z0.95~1.15, y>0.05)": (R0[:,2]>0.95)&(R0[:,2]<1.15)&(R0[:,1]>0.05),
    "臀 (z0.75~0.95, y>0.10)": (R0[:,2]>0.75)&(R0[:,2]<0.95)&(R0[:,1]>0.10),
    "肚 (z0.95~1.25, y<-0.05)": (R0[:,2]>0.95)&(R0[:,2]<1.25)&(R0[:,1]<-0.05),
}
for name, m in REGIONS.items():
    n = int(m.sum())
    if n < 50: 
        print(f"{name}: too few ({n})"); continue
    move = np.linalg.norm(P1[m]-R0[m], axis=1)
    ang, res = kabsch(R0[m], P1[m])
    print(f"{name}: verts={n} 總位移 mean={move.mean()*100:.1f}cm max={move.max()*100:.1f}cm "
          f"| 剛體旋轉={ang:.1f}° | **形變殘差** mean={res.mean()*1000:.1f}mm max={res.max()*1000:.1f}mm")
