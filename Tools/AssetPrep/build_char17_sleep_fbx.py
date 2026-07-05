# 仰躺大字睡姿（SPEC 定案 #18）：char17 擺姿後烘成靜態網格匯出。
# 姿勢＝站立空間中的大字（雙腿外張、雙臂略抬）；躺下由引擎內
# InkBodyComponent 的相對旋轉完成，與 SM_Char17 同一套 pivot 慣例（腳底原點）。
#
#   "C:\Program Files\Blender Foundation\Blender 5.1\blender.exe" --background ^
#     "C:\games\Unreal Engine\nice_ink\Content\玩家\nice_ink_player_character17.blend" ^
#     --python build_char17_sleep_fbx.py
import bpy, math, mathutils

OUT = r"c:\games\Unreal Engine\nice_ink\SourceAssets\char17_sleep.fbx"

body = bpy.data.objects["PlusSize_Male_Body_01"]
arm = bpy.data.objects["Skeleton_Plus-size"]

# armature 資料空間：Y=身高、X=左右、Z=前後（Mixamo 慣例）
def rotate_pb(name, deg, axis='Z'):
    pb = arm.pose.bones[name]
    R = mathutils.Matrix.Rotation(math.radians(deg), 4, axis)
    T = mathutils.Matrix.Translation(pb.matrix.translation.copy())
    pb.matrix = T @ R @ T.inverted() @ pb.matrix
    bpy.context.view_layer.update()

rotate_pb("LeftUpLeg", 16.0)    # 腿外張（大字下盤）
rotate_pb("RightUpLeg", -16.0)
rotate_pb("LeftArm", 18.0)      # 臂上抬（離軀幹更開）
rotate_pb("RightArm", -18.0)

# 姿勢烘成新網格（保留 UVMap/FaceUV 與 FaceMask 頂點色）
dg = bpy.context.evaluated_depsgraph_get()
mesh = bpy.data.meshes.new_from_object(body.evaluated_get(dg), preserve_all_data_layers=True, depsgraph=dg)
obj = bpy.data.objects.new("char17_sleep", mesh)
bpy.context.scene.collection.objects.link(obj)

# 材質槽合一（同 build_char17_fbx）
if len(obj.material_slots) > 1:
    for p in mesh.polygons:
        p.material_index = 0
    bpy.context.view_layer.objects.active = obj
    while len(obj.material_slots) > 1:
        obj.active_material_index = len(obj.material_slots) - 1
        bpy.ops.object.material_slot_remove()

# 世界變換烘入 + 腳底歸零（pivot 慣例與 SM_Char17 一致）
mesh.transform(body.matrix_world)
zmin = min(v.co.z for v in mesh.vertices)
mesh.transform(mathutils.Matrix.Translation((0.0, 0.0, -zmin)))

zs = [v.co.z for v in mesh.vertices]
xs = [v.co.x for v in mesh.vertices]
print(f"sleep z=[{min(zs):.3f},{max(zs):.3f}] x=[{min(xs):.3f},{max(xs):.3f}]")
print("uv layers:", [l.name for l in mesh.uv_layers])
print("color attrs:", [c.name for c in mesh.color_attributes])

bpy.ops.object.select_all(action='DESELECT')
obj.select_set(True)
bpy.context.view_layer.objects.active = obj
bpy.ops.export_scene.fbx(filepath=OUT, use_selection=True, object_types={'MESH'},
    apply_unit_scale=True, apply_scale_options='FBX_SCALE_NONE', path_mode='STRIP',
    use_mesh_modifiers=True, add_leaf_bones=False, colors_type='SRGB')
print("EXPORTED:", OUT)
