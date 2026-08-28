# 坐→站插值帶探針（2026-08-27）：驗 user 的假說「端點姿勢都好，逐骨插值的過渡也不會壞」。
#
# 起點＝SitBones（DrawPose_Backup2，user 七月手擺的盤腿坐）、終點＝rest 站姿。
# 每一格：逐骨 quaternion slerp＋location lerp → 垂直落地（全網格最低點貼回地面）
# → 同一把尺（pose_domain_metrics）量 fold/xsect/cross → 渲正面＋側面。
#
# 兩個此前沒人看過的問題會在這條帶上現形：
#   1. 盤腿＝雙腿交鎖，解鎖時小腿要繞過對側大腿——逐骨插值不知道「繞過」；
#   2. SitBones 是七月擺的，弓形褌是 08-23 生的——**盤腿＋現行褌從未被渲染過**。
#
# Run: blender --background sumo_character_master.blend --python pose_domain_sitstand.py
import bpy
import sys
import os
import numpy as np
from mathutils import Quaternion, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pose_domain_metrics import Metrics, eval_arrays, verdict_of  # noqa: E402

PROBE = "c:/games/Unreal Engine/nice_ink/Saved/PoseProbe"
SHOTS = os.path.join(PROBE, "sitstand")
os.makedirs(SHOTS, exist_ok=True)
LIB = "c:/games/Unreal Engine/nice_ink/SourceAssets/sumo_retopo_base20.blend"

arm = bpy.data.objects["Skeleton_Plus-size"]
arm.data.pose_position = 'POSE'
if arm.animation_data:
    arm.animation_data.action = None
BODY = bpy.data.objects["SumoRetopo"]
CLOTH = bpy.data.objects.get("Fundoshi")
BASE_LOC = tuple(arm.location)


def log(*a):
    print(*a, flush=True)


def reset_pose():
    for pb in arm.pose.bones:
        pb.rotation_mode = 'QUATERNION'
        pb.rotation_quaternion = (1.0, 0.0, 0.0, 0.0)
        pb.location = (0.0, 0.0, 0.0)
        pb.scale = (1.0, 1.0, 1.0)


# ---------------------------------------------------------------- 取 SitBones
with bpy.data.libraries.load(LIB, link=False) as (src, dst):
    log("actions in base20:", list(src.actions))
    dst.actions = [n for n in src.actions if n == "DrawPose_Backup2"]
act = bpy.data.actions.get("DrawPose_Backup2")
if act is None:
    raise RuntimeError("DrawPose_Backup2 not found")

reset_pose()
if not arm.animation_data:
    arm.animation_data_create()
arm.animation_data.action = act
try:
    if act.slots:
        arm.animation_data.action_slot = act.slots[0]
except Exception as e:
    log("slot assign skipped:", e)
bpy.context.scene.frame_set(1)
bpy.context.view_layer.update()
SIT = {}
for pb in arm.pose.bones:
    pb.rotation_mode = 'QUATERNION'
    SIT[pb.name] = (pb.rotation_quaternion.copy(), Vector(pb.location))
nz = sum(1 for q, l in SIT.values() if abs(q.w - 1.0) > 1e-4 or l.length > 1e-5)
log("bones with non-identity sit pose: %d / %d" % (nz, len(SIT)))
if nz == 0:
    # fallback：直接讀 slotted action 的 channelbag fcurves（5.1 沒有 act.fcurves）
    log("action assignment produced no pose; evaluating fcurves manually")
    bag = act.layers[0].strips[0].channelbags[0]
    vals = {}
    for fc in bag.fcurves:
        vals.setdefault((fc.data_path, fc.array_index), fc.evaluate(1.0))
    import re
    for (path, idx), v in vals.items():
        m = re.match(r'pose\.bones\["(.+)"\]\.(rotation_quaternion|location)', path)
        if not m:
            continue
        q, l = SIT[m.group(1)]
        if m.group(2) == "rotation_quaternion":
            q[idx] = v
        else:
            l[idx] = v
    nz = sum(1 for q, l in SIT.values() if abs(q.w - 1.0) > 1e-4 or l.length > 1e-5)
    log("after fallback: non-identity bones = %d" % nz)
    if nz == 0:
        raise RuntimeError("SitBones evaluated to identity — 儀器沒在動，先修儀器")
arm.animation_data.action = None

# ---------------------------------------------------------------- 尺（rest 基線）
reset_pose()
arm.location = BASE_LOC
bpy.context.view_layer.update()
M = Metrics(BODY, CLOTH)
FLOOR_Z = float(M.co0[:, 2].min())

IDQ = Quaternion((1.0, 0.0, 0.0, 0.0))


def apply_t(t):
    """t=0 盤腿坐、t=1 站。逐骨 slerp＋location lerp，然後全網格最低點貼地。"""
    reset_pose()
    arm.location = BASE_LOC
    for pb in arm.pose.bones:
        q, l = SIT[pb.name]
        pb.rotation_quaternion = q.slerp(IDQ, t)
        pb.location = l * (1.0 - t)
    bpy.context.view_layer.update()
    co, _ = eval_arrays(BODY)
    dz = FLOOR_Z - float(co[:, 2].min())
    arm.location = (BASE_LOC[0], BASE_LOC[1], BASE_LOC[2] + dz)
    bpy.context.view_layer.update()


# ---------------------------------------------------------------- 渲染設定
scene = bpy.context.scene
scene.render.engine = 'BLENDER_WORKBENCH'
scene.render.resolution_x = 640
scene.render.resolution_y = 780
scene.display.shading.light = 'STUDIO'
scene.display.shading.color_type = 'OBJECT'   # 身體與褌不同色，穿刺一眼可見
scene.display.shading.show_cavity = True
BODY.color = (0.72, 0.58, 0.50, 1.0)
if CLOTH is not None:
    CLOTH.color = (0.15, 0.15, 0.18, 1.0)
cd = bpy.data.cameras.new("StripCam")
cd.type = 'ORTHO'
cam = bpy.data.objects.new("StripCam", cd)
scene.collection.objects.link(cam)
scene.camera = cam
arm.hide_render = True

# 固定機位：取 t=0 與 t=1 包圍盒的聯集（每格可直接互比）
apply_t(0.0)
co0, _ = eval_arrays(BODY)
apply_t(1.0)
co1, _ = eval_arrays(BODY)
lo = np.minimum(co0.min(axis=0), co1.min(axis=0))
hi = np.maximum(co0.max(axis=0), co1.max(axis=0))
CTR = Vector(((lo + hi) * 0.5).tolist())
SIZE = float(max(hi - lo)) * 1.12


def shot(az_deg, name):
    import math
    cd.ortho_scale = SIZE
    az = math.radians(az_deg)
    d = Vector((math.sin(az), -math.cos(az), 0.14)).normalized()
    cam.location = CTR + d * 6.0
    cam.rotation_euler = (-d).to_track_quat('-Z', 'Y').to_euler()
    scene.render.filepath = os.path.join(SHOTS, name + ".png")
    bpy.ops.render.render(write_still=True)


# ---------------------------------------------------------------- 插值帶
ROWS = ["t\tverdict\tfold\tfold_max_cm\tfold_at\txsect\txs_max_cm\txs_at\tcross\tvol_loss"]
for i, t in enumerate([0.0, 0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.875, 1.0]):
    apply_t(t)
    m = M.measure()
    row = "%.3f\t%s\t%d\t%.1f\t%s\t%d\t%.1f\t%s\t%d\t%.2f" % (
        t, verdict_of(m), m['fold'], m['fold_max_cm'], m['fold_at'],
        m['xsect'], m['xs_max_cm'], m['xs_at'], m['cross'], m['vol_loss'])
    ROWS.append(row)
    log("ROW " + row)
    tag = "t%03d" % round(t * 1000)
    shot(0, "%s_f" % tag)
    shot(90, "%s_s" % tag)
    shot(45, "%s_q" % tag)

out = os.path.join(PROBE, "sitstand.tsv")
with open(out, "w", encoding="utf-8") as f:
    f.write("\n".join(ROWS) + "\n")
log("WROTE %s" % out)
log("DONE")
