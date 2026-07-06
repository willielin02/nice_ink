# char17 骨骼版導出（貼臉鎖定的程式化彎腰用——SPEC v3.1 定案 #23）。
# 帶 armature 的 skeletal FBX：引擎內用 PoseableMesh 直接擺 Spine/Neck/Head。
# 保留 UVMap/FaceUV 與 FaceMask 頂點色；材質槽合一；不烘姿勢、不去骨。
#
#   "C:\Program Files\Blender Foundation\Blender 5.1\blender.exe" --background ^
#     "C:\games\Unreal Engine\nice_ink\Content\玩家\nice_ink_player_character17.blend" ^
#     --python build_char17_skeletal_fbx.py
import bpy
from pathlib import Path

OUT = r"c:\games\Unreal Engine\nice_ink\SourceAssets\char17_skeletal.fbx"

# blend 可能存檔於 Pose Mode——export 前一律回 Object Mode
if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')

body = bpy.data.objects["PlusSize_Male_Body_01"]
arm = bpy.data.objects["Skeleton_Plus-size"]
mesh = body.data

# UV0 均勻密度重排（與站姿/睡姿共用同一套 UV）
_uv_src = (Path(__file__).parent / "uv0_uniform.py").read_text(encoding="utf-8")
exec(compile(_uv_src, "uv0_uniform.py", "exec"))
reunwrap_uv0_uniform(body)

# 馬賽克另有用途，骨骼版不帶
mosaic = bpy.data.objects.get("Mosaic")
if mosaic:
    bpy.data.objects.remove(mosaic, do_unlink=True)

print("DIAG body.parent:", body.parent.name if body.parent else None)
print("DIAG body.modifiers:", [(m.type, getattr(m, 'object', None).name if getattr(m, 'object', None) else None) for m in body.modifiers])
print("DIAG mode:", bpy.context.mode)

# 材質槽合一（同 build_char17_fbx）
if len(body.material_slots) > 1:
    for p in mesh.polygons:
        p.material_index = 0
    bpy.context.view_layer.objects.active = body
    while len(body.material_slots) > 1:
        body.active_material_index = len(body.material_slots) - 1
        bpy.ops.object.material_slot_remove()

print("uv layers:", [l.name for l in mesh.uv_layers])
print("color attrs:", [c.name for c in mesh.color_attributes])
print("bones:", len(arm.data.bones), "deform-flagged:", sum(1 for b in arm.data.bones if b.use_deform))
print("vgroups:", len(body.vertex_groups))

bpy.context.view_layer.objects.active = arm
bpy.ops.export_scene.fbx(filepath=OUT, use_selection=False,
    object_types={'ARMATURE', 'MESH'},
    apply_unit_scale=True, apply_scale_options='FBX_SCALE_NONE', path_mode='STRIP',
    use_mesh_modifiers=False,  # 保留 armature 綁定（不把 modifier 烘掉）
    add_leaf_bones=False, colors_type='SRGB',
    armature_nodetype='NULL',
    # 不濾 deform-only：此 rig 的骨頭未必都有 use_deform 旗標，
    # 濾了會把整條骨鏈砍到只剩根（UE 端曾實測只剩 1 根骨）
    use_armature_deform_only=False)
print("EXPORTED:", OUT)
