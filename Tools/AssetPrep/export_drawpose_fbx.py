# 作畫坐姿（DrawPose_Backup2）單幀動畫 FBX 匯出
#
# 用途：把使用者在 base18 手擺的盤腿坐姿搬進引擎。
# 鐵律（v3.8 教訓）：Blender 骨骼絕對旋轉跨不過 FBX 每骨軸向重映射——
# 直寫旋轉值＝蒙皮煎餅。唯一安全路徑＝FBX 動畫匯入讓 UE 自己做軸向換算，
# 再從匯入後的 AnimSequence 讀出 UE 空間的每骨 local transform 烘成標頭。
# 本腳本只負責第一步：套姿勢 → 匯出骨架動畫 FBX（單幀）。
#
# Run: blender --background --python export_drawpose_fbx.py
import bpy
import sys

# 預設＝坐姿基底；lean 端點：blender --background --python 本檔 -- <blend> <action> <out_fbx>
BLEND = r"C:\games\Unreal Engine\nice_ink\SourceAssets\sumo_retopo_base20.blend"
OUT_FBX = r"C:\games\Unreal Engine\nice_ink\SourceAssets\sumo_drawpose.fbx"
ACTION = "DrawPose_Backup2"
if "--" in sys.argv:
    args = sys.argv[sys.argv.index("--") + 1:]
    if len(args) >= 3:
        BLEND, ACTION, OUT_FBX = args[0], args[1], args[2]

bpy.ops.wm.open_mainfile(filepath=BLEND)
if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')

arm = bpy.data.objects["Skeleton_Plus-size"]
act = bpy.data.actions.get(ACTION)
assert act is not None, f"action {ACTION} not found"

# 套上姿勢動作（Blender 5.x slotted action：必須同時指定 slot）
arm.animation_data_create()
arm.animation_data.action = act
slots = list(getattr(act, "slots", []))
if slots:
    arm.animation_data.action_slot = slots[0]

scene = bpy.context.scene
# UE Interchange 要求動畫長度對齊幀界：24fps、兩幀（=正好 1/24 秒），姿勢兩幀相同
scene.render.fps = 24
scene.render.fps_base = 1.0
scene.frame_start = 1
scene.frame_end = 2
scene.frame_set(1)

# 骨集斷言：引擎 SK_Sumo 是 37 骨（Root 之下 36），名字必須全對上
bones = {b.name for b in arm.data.bones}
expected = {
    "Root", "Hips", "Spine", "Spine1", "Neck", "Head",
    "LeftShoulder", "LeftArm", "LeftForeArm", "LeftHand",
    "LeftHandIndex1", "LeftHandIndex2", "LeftHandThumb1", "LeftHandThumb2", "LeftHandProp",
    "RightShoulder", "RightArm", "RightForeArm", "RightHand",
    "RightHandIndex1", "RightHandIndex2", "RightHandThumb1", "RightHandThumb2", "RightHandProp",
    "LeftUpLeg", "LeftLeg", "LeftFoot", "LeftToeBase",
    "RightUpLeg", "RightLeg", "RightFoot", "RightToeBase",
    "Jiggle_Belly", "Jiggle_Chest_L", "Jiggle_Chest_R", "Jiggle_Butt_L", "Jiggle_Butt_R",
}
missing = expected - bones
assert not missing, f"base18 armature missing bones: {sorted(missing)}"

# 只匯出骨架＋動畫（不帶網格：目標 Skeleton 已在引擎裡，動畫按骨名對軌）
for o in bpy.context.view_layer.objects:
    o.select_set(o is arm)
bpy.ops.export_scene.fbx(
    filepath=OUT_FBX,
    use_selection=True,
    object_types={'ARMATURE'},
    apply_unit_scale=True,
    apply_scale_options='FBX_SCALE_NONE',
    path_mode='STRIP',
    add_leaf_bones=False,
    armature_nodetype='NULL',
    use_armature_deform_only=False,
    bake_anim=True,
    bake_anim_use_all_bones=True,
    bake_anim_use_nla_strips=False,
    bake_anim_use_all_actions=False,
    bake_anim_force_startend_keying=True,
    bake_anim_step=1.0,
    bake_anim_simplify_factor=0.0,
)
print("EXPORTED", OUT_FBX)
