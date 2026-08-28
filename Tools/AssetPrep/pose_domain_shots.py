# 姿勢域邊界截圖（2026-08-27）：把 pose_domain_ladder 的階梯渲成**固定機位**的圖，
# 供使用者「認邊界」。機位固定（不隨姿勢重新取景）＝同一把尺，圖與圖之間可直接比較。
# 姿勢定義與掃描器共用 pose_domain_ladder（同一組姿勢既被量也被渲）。
#
# Run: blender --background <master.blend> --python pose_domain_shots.py -- <out_dir>
import bpy
import sys
import math
import os
from mathutils import Vector, Quaternion

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np  # noqa: E402
from pose_domain_ladder import LADDER  # noqa: E402
from pose_domain_rig import ground_and_balance, apply_ops  # noqa: E402

OUT = sys.argv[sys.argv.index("--") + 1:][0]
os.makedirs(OUT, exist_ok=True)

arm = bpy.data.objects["Skeleton_Plus-size"]
arm.data.pose_position = "POSE"          # master 出廠是 REST
if arm.animation_data:
    arm.animation_data.action = None


def reset_pose():
    for pb in arm.pose.bones:
        pb.rotation_mode = 'QUATERNION'
        pb.rotation_quaternion = (1.0, 0.0, 0.0, 0.0)
        pb.location = (0.0, 0.0, 0.0)
        pb.scale = (1.0, 1.0, 1.0)


BODY = bpy.data.objects["SumoRetopo"]
BASE_LOC = tuple(arm.location)
_topo = {}


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
_co0, _ = eval_arrays()
_zmin = float(_co0[:, 2].min())
FOOT = _co0[:, 2] < _zmin + 0.02
FOOT_Z = float(_co0[FOOT][:, 2].min())
FOOT_CY = float(_co0[FOOT][:, 1].mean())

scene = bpy.context.scene
scene.render.engine = 'BLENDER_WORKBENCH'
scene.render.resolution_x = 520
scene.render.resolution_y = 700
scene.display.shading.light = 'STUDIO'
scene.display.shading.color_type = 'SINGLE'
scene.display.shading.single_color = (0.72, 0.58, 0.50)
scene.display.shading.show_cavity = True

cd = bpy.data.cameras.new("ShotCam")
cd.type = 'ORTHO'
cd.ortho_scale = 2.30                      # 固定取景：所有圖同一把尺
cam = bpy.data.objects.new("ShotCam", cd)
scene.collection.objects.link(cam)
scene.camera = cam
arm.hide_render = True
CTR = Vector((0.0, 0.0, 0.85))

VIEWS = {
    "f": (Vector((0, -6, 0)), (math.radians(90), 0, 0)),
    "s": (Vector((6, 0, 0)), (math.radians(90), 0, math.radians(90))),
}

for group, tag, label, ops in LADDER:
    reset_pose()
    arm.location = BASE_LOC
    apply_ops(arm, ops)
    bpy.context.view_layer.update()
    ground_and_balance(arm, eval_arrays, FOOT, FOOT_Z, FOOT_CY, BASE_LOC)
    bpy.context.view_layer.update()
    for vn, (off, rr) in VIEWS.items():
        cam.location = CTR + off
        cam.rotation_euler = rr
        scene.render.filepath = os.path.join(OUT, "%s_%s.png" % (tag, vn))
        bpy.ops.render.render(write_still=True)
    print("SHOT " + tag, flush=True)

print("DONE %d poses" % len(LADDER))
