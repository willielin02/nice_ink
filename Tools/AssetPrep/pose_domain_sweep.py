# 姿勢域掃描器（2026-08-27）：對現行角色 master 逐關節／耦合掃描姿勢，量「破圖」指標，
# 輸出 TSV 供擬合「不破圖域」的邊界。
#
# 設計依據：程序化動畫必須活在「不破圖 ∩ 合理」的交集裡。本工具負責**不破圖**那一半
# （合理那一半＝關節 ROM 表 + 使用者認邊界）。
#
# 鐵坑（實測）：
#   1. master 出廠 pose_position == 'REST' ⇒ 不強制設 'POSE' 的話擺姿勢網格一動也不動，
#      而且所有指標「完全相等」＝空跑（08-24 fundoshi_pose_check 同款血價）。
#   2. Blender 5.1 的 Action 沒有 .fcurves（全 slotted）：要走 layers→strips→channelbags。
#   3. 摺疊指標不可拿「變形後法線 vs rest 世界法線」——大角度下合法旋轉會被誤記成翻面。
#      正解＝局部二面角（跟鄰居比，不跟 rest 比）。
#
# Run: blender --background <master.blend> --python pose_domain_sweep.py -- <mode> <out.tsv> [args]
#   slice            : Spine/Spine1 前彎一維切片（尺的校準用）
#   rom  [step] [lim]: 逐關節單軸掃描（本體的 ROM 表）
#   couple <which>   : 耦合二維掃描 bend_hip|hip_knee|neck_bend|arm_fore
import bpy
import sys
import os
import math
import time
import numpy as np
from mathutils import Quaternion

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pose_domain_metrics import (Metrics, eval_arrays, verdict_of)  # noqa: E402

ARGS = sys.argv[sys.argv.index("--") + 1:]
MODE = ARGS[0]
OUT = ARGS[1]

arm = bpy.data.objects["Skeleton_Plus-size"]
arm.data.pose_position = "POSE"          # 鐵坑 1
if arm.animation_data:
    arm.animation_data.action = None
BODY = bpy.data.objects["SumoRetopo"]
CLOTH = bpy.data.objects.get("Fundoshi")


def reset_pose():
    for pb in arm.pose.bones:
        pb.rotation_mode = 'QUATERNION'
        pb.rotation_quaternion = (1.0, 0.0, 0.0, 0.0)
        pb.location = (0.0, 0.0, 0.0)
        pb.scale = (1.0, 1.0, 1.0)


def set_bone(name, axis_idx, deg):
    ax = [(1, 0, 0), (0, 1, 0), (0, 0, 1)][axis_idx]
    arm.pose.bones[name].rotation_quaternion = Quaternion(ax, math.radians(deg))


from pose_domain_rig import ground_and_balance, apply_ops  # noqa: E402

GROUND = MODE in ("ladder", "slice")

reset_pose()
bpy.context.view_layer.update()
M = Metrics(BODY, CLOTH)
ROWS = ["case\tparam\tverdict\tfold\tfold_groups\tfold_max_cm\tfold_at\t"
        "xsect\txs_groups\txs_max_cm\txs_at\tcross\tvol_loss_pct\tbalance_cm\tsec"]

BASE_LOC = tuple(arm.location)


def run(case, param, setup):
    t0 = time.time()
    reset_pose()
    arm.location = BASE_LOC
    setup()
    bpy.context.view_layer.update()
    if GROUND:
        ground_and_balance(arm, lambda: eval_arrays(BODY), M.foot_mask,
                           M.foot_zmin, M.foot_cy, BASE_LOC)
        bpy.context.view_layer.update()
    m = M.measure()
    line = "%s\t%s\t%s\t%d\t%d\t%.1f\t%s\t%d\t%d\t%.1f\t%s\t%d\t%.2f\t%.1f\t%.2f" % (
        case, param, verdict_of(m), m['fold'], m['fold_groups'], m['fold_max_cm'],
        m['fold_at'], m['xsect'], m['xs_groups'], m['xs_max_cm'], m['xs_at'],
        m['cross'], m['vol_loss'], m['balance'] * 100.0, time.time() - t0)
    ROWS.append(line)
    print("ROW " + line, flush=True)


def set_bend(d):
    set_bone("Spine", 0, d * 0.5)
    set_bone("Spine1", 0, d * 0.5)


if MODE == "ladder":
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from pose_domain_ladder import LADDER

    for group, tag, label, ops in LADDER:
        run(group, tag, lambda o=ops: apply_ops(arm, o))

elif MODE == "slice":
    for deg in range(0, 85, 5):
        run("spine_bend", str(deg), lambda d=deg: set_bend(d))

elif MODE == "rom":
    STEP = int(ARGS[2]) if len(ARGS) > 2 else 10
    LIM = int(ARGS[3]) if len(ARGS) > 3 else 120
    BONES = ["Hips", "Spine", "Spine1", "Neck", "Head",
             "LeftShoulder", "LeftArm", "LeftForeArm", "LeftHand",
             "RightShoulder", "RightArm", "RightForeArm", "RightHand",
             "LeftUpLeg", "LeftLeg", "LeftFoot", "LeftToeBase",
             "RightUpLeg", "RightLeg", "RightFoot", "RightToeBase"]
    for bn in BONES:
        if bn not in arm.pose.bones:
            print("MISSING BONE " + bn)
            continue
        for ax in range(3):
            for deg in range(-LIM, LIM + 1, STEP):
                if deg == 0:
                    continue
                run("%s.%s" % (bn, "XYZ"[ax]), str(deg),
                    lambda b=bn, a=ax, d=deg: set_bone(b, a, d))

elif MODE == "couple":
    which = ARGS[2]
    if which == "bend_hip":
        for bend in range(0, 65, 8):
            for hip in range(0, 97, 12):
                def s(b=bend, h=hip):
                    set_bend(b)
                    set_bone("LeftUpLeg", 0, h)
                    set_bone("RightUpLeg", 0, h)
                run("bend_hip", "%d/%d" % (bend, hip), s)
    elif which == "hip_knee":
        for hip in range(0, 121, 15):
            for knee in range(0, 145, 18):
                def s(h=hip, k=knee):
                    set_bone("LeftUpLeg", 0, h)
                    set_bone("RightUpLeg", 0, h)
                    set_bone("LeftLeg", 0, k)
                    set_bone("RightLeg", 0, k)
                run("hip_knee", "%d/%d" % (hip, knee), s)
    elif which == "neck_bend":
        for bend in range(0, 65, 8):
            for pitch in range(-60, 61, 15):
                def s(b=bend, p=pitch):
                    set_bend(b)
                    set_bone("Neck", 0, p * 0.5)
                    set_bone("Head", 0, p * 0.5)
                run("neck_bend", "%d/%d" % (bend, pitch), s)
    elif which == "arm_fore":
        for sh in range(-90, 121, 30):
            for el in range(0, 151, 25):
                def s(a=sh, e=el):
                    set_bone("RightArm", 0, a)
                    set_bone("RightForeArm", 0, e)
                run("arm_fore", "%d/%d" % (sh, el), s)

with open(OUT, "w", encoding="utf-8") as f:
    f.write("\n".join(ROWS))
print("WROTE %s %d rows" % (OUT, len(ROWS) - 1))
