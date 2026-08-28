# 單一姿勢多角度環繞（2026-08-27）：把 ladder 裡指定的一個姿勢從一圈角度渲出來，
# 外加對準髖關節的特寫——破圖幾乎都發生在腹下/髖/鼠蹊，全身圖看不清楚。
#
# Run: blender --background <master.blend> --python pose_domain_orbit.py -- <tag> <out_dir>
import bpy
import sys
import math
import os
import numpy as np
from mathutils import Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pose_domain_ladder import LADDER  # noqa: E402
from pose_domain_rig import ground_and_balance, apply_ops  # noqa: E402

ARGS = sys.argv[sys.argv.index("--") + 1:]
TAG, OUT = ARGS[0], ARGS[1]
os.makedirs(OUT, exist_ok=True)

arm = bpy.data.objects["Skeleton_Plus-size"]
arm.data.pose_position = "POSE"
if arm.animation_data:
    arm.animation_data.action = None
BODY = bpy.data.objects["SumoRetopo"]
BASE_LOC = tuple(arm.location)
_topo = {}


def reset_pose():
    for pb in arm.pose.bones:
        pb.rotation_mode = 'QUATERNION'
        pb.rotation_quaternion = (1.0, 0.0, 0.0, 0.0)
        pb.location = (0.0, 0.0, 0.0)
        pb.scale = (1.0, 1.0, 1.0)


def eval_arrays():
    dg = bpy.context.evaluated_depsgraph_get()
    ev = BODY.evaluated_get(dg)
    me = ev.to_mesh()
    n = len(me.vertices)
    co = np.empty(n * 3, dtype=np.float64)
    me.vertices.foreach_get("co", co)
    co = co.reshape(n, 3)
    if "t" not in _topo:
        me.calc_loop_triangles()
        t = np.empty(len(me.loop_triangles) * 3, dtype=np.int32)
        me.loop_triangles.foreach_get("vertices", t)
        _topo["t"] = t.reshape(-1, 3)
    ev.to_mesh_clear()
    mw = np.array(BODY.matrix_world)
    return co @ mw[:3, :3].T + mw[:3, 3], _topo["t"]


reset_pose()
bpy.context.view_layer.update()
co0, _ = eval_arrays()
zmin = float(co0[:, 2].min())
FOOT = co0[:, 2] < zmin + 0.02
FOOT_Z = float(co0[FOOT][:, 2].min())
FOOT_CY = float(co0[FOOT][:, 1].mean())

OPS = dict((t, o) for _g, t, _l, o in LADDER)[TAG]
reset_pose()
arm.location = BASE_LOC
apply_ops(arm, OPS)
bpy.context.view_layer.update()
ground_and_balance(arm, eval_arrays, FOOT, FOOT_Z, FOOT_CY, BASE_LOC)
bpy.context.view_layer.update()

co, _ = eval_arrays()
lo, hi = co.min(axis=0), co.max(axis=0)
CTR = Vector(((lo + hi) * 0.5).tolist())
SIZE = float(max(hi - lo)) * 1.10
HIP = arm.matrix_world @ arm.pose.bones["LeftUpLeg"].head
print("BBOX size=%.2f  CTR=(%.2f,%.2f,%.2f)  HIP=(%.2f,%.2f,%.2f)"
      % (SIZE, CTR.x, CTR.y, CTR.z, HIP.x, HIP.y, HIP.z), flush=True)

scene = bpy.context.scene
scene.render.engine = 'BLENDER_WORKBENCH'
scene.render.resolution_x = 640
scene.render.resolution_y = 780
scene.display.shading.light = 'STUDIO'
scene.display.shading.color_type = 'SINGLE'
scene.display.shading.single_color = (0.72, 0.58, 0.50)
scene.display.shading.show_cavity = True

cd = bpy.data.cameras.new("OrbitCam")
cd.type = 'ORTHO'
cam = bpy.data.objects.new("OrbitCam", cd)
scene.collection.objects.link(cam)
scene.camera = cam
arm.hide_render = True


def look(center, az_deg, el_deg, scale, name):
    cd.ortho_scale = scale
    az, el = math.radians(az_deg), math.radians(el_deg)
    d = Vector((math.sin(az) * math.cos(el), -math.cos(az) * math.cos(el), math.sin(el)))
    cam.location = center + d * 6.0
    cam.rotation_euler = (-d).to_track_quat('-Z', 'Y').to_euler()
    scene.render.filepath = os.path.join(OUT, name + ".png")
    bpy.ops.render.render(write_still=True)
    print("SHOT " + name, flush=True)


# 一圈全身（方位每 30°、仰角 8°；az=0＝正面，90＝左側）
for a in range(0, 360, 30):
    look(CTR, a, 8.0, SIZE, "%s_az%03d" % (TAG, a))
# 俯視四角
for a in (0, 90, 180, 270):
    look(CTR, a, 50.0, SIZE, "%s_top%03d" % (TAG, a))
# 髖關節特寫（破圖區）
for a in (0, 45, 90, 135):
    look(Vector(HIP), a, 5.0, 0.55, "%s_hip%03d" % (TAG, a))

print("DONE")
