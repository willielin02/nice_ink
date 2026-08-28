# 姿勢域共用 rig 操作（2026-08-27）。掃描器與截圖器共用，確保「量的姿勢＝渲的姿勢」。
import math
import numpy as np
from mathutils import Quaternion

# 世界軸：X＝左右（矢狀面屈伸繞它）、Y＝前後、Z＝上下
WORLD_AXIS = [(1, 0, 0), (0, 1, 0), (0, 0, 1)]


def apply_ops(arm, ops):
    """ops = [(bone, axis_idx, deg[, space])]；space 'L'＝骨頭自己的局部軸（預設），
    'W'＝世界軸。**組合姿勢一律用 W**——各骨的局部軸朝向不同，用 L 疊出來的
    「髖鉸鏈」不會落在同一個平面上（2026-08-27 實測：腿沒被扳回垂直，整個人躺平）。"""
    for op in ops:
        name, ax_i, deg = op[0], op[1], op[2]
        space = op[3] if len(op) > 3 else "L"
        pb = arm.pose.bones[name]
        pb.rotation_mode = 'QUATERNION'
        if space == "W":
            rest = pb.bone.matrix_local.to_quaternion()
            qw = Quaternion(WORLD_AXIS[ax_i], math.radians(deg))
            pb.rotation_quaternion = rest.inverted() @ qw @ rest
        else:
            pb.rotation_quaternion = Quaternion(WORLD_AXIS[ax_i], math.radians(deg))


def ground_and_balance(arm, eval_fn, foot_mask, rest_foot_zmin, rest_foot_cy, base_loc):
    """擺完姿勢之後把人放回地上並站穩——沒有這一步，任何屈膝／屈髖的姿勢都是飄的。

    ① 垂直：把腳底（靜止時貼地的那批頂點）拉回原本的地面高度
    ② 前後：把整體質心移到雙腳正上方（真人彎腰時屁股會往後坐，就是這個配重）
    兩者都是移動整具 armature，不動任何骨頭 ⇒ 不影響已經量到的關節角度。
    """
    co, tris = eval_fn()
    fz = float(co[foot_mask][:, 2].min())
    cen = (co[tris[:, 0]] + co[tris[:, 1]] + co[tris[:, 2]]) / 3.0
    a = np.cross(co[tris[:, 1]] - co[tris[:, 0]], co[tris[:, 2]] - co[tris[:, 0]])
    w = np.linalg.norm(a, axis=1)
    com = np.average(cen, axis=0, weights=w)
    dz = rest_foot_zmin - fz
    dy = rest_foot_cy - float(com[1])
    arm.location = (base_loc[0], base_loc[1] + dy, base_loc[2] + dz)
    return dy, dz
