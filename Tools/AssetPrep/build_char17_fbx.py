import bpy, math, mathutils
from pathlib import Path

OUT_BODY = r"c:\games\Unreal Engine\nice_ink\SourceAssets\char17_static.fbx"
OUT_MOSAIC = r"c:\games\Unreal Engine\nice_ink\SourceAssets\char17_mosaic.fbx"

if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')

body = bpy.data.objects["PlusSize_Male_Body_01"]
mosaic = bpy.data.objects.get("Mosaic")

# UV0 均勻密度重排（所有網格導出前必跑，見 uv0_uniform.py）
_uv_src = (Path(__file__).parent / "uv0_uniform.py").read_text(encoding="utf-8")
exec(compile(_uv_src, "uv0_uniform.py", "exec"))
reunwrap_uv0_uniform(body)

mesh = body.data
if len(body.material_slots) > 1:
    for p in mesh.polygons:
        p.material_index = 0
    bpy.context.view_layer.objects.active = body
    while len(body.material_slots) > 1:
        body.active_material_index = len(body.material_slots) - 1
        bpy.ops.object.material_slot_remove()

for o in (body, mosaic):
    if o is None: continue
    o.data.transform(o.matrix_world)
    o.parent = None
    o.matrix_world = mathutils.Matrix.Identity(4)
    for m in list(o.modifiers):
        if m.type == 'ARMATURE':
            o.modifiers.remove(m)

zmin = min(v.co.z for v in mesh.vertices)
shift = mathutils.Matrix.Translation((0.0, 0.0, -zmin))
for o in (body, mosaic):
    if o is None: continue
    o.data.transform(shift)

zs = [v.co.z for v in mesh.vertices]
print(f"body z=[{min(zs):.3f},{max(zs):.3f}]")

# 身體單獨匯出（單一 section）
bpy.ops.object.select_all(action='DESELECT')
body.select_set(True)
bpy.context.view_layer.objects.active = body
bpy.ops.export_scene.fbx(filepath=OUT_BODY, use_selection=True, object_types={'MESH'},
    apply_unit_scale=True, apply_scale_options='FBX_SCALE_NONE', path_mode='STRIP',
    use_mesh_modifiers=True, add_leaf_bones=False, colors_type='SRGB')
print("EXPORTED BODY:", OUT_BODY)

if mosaic:
    bpy.ops.object.select_all(action='DESELECT')
    mosaic.select_set(True)
    bpy.context.view_layer.objects.active = mosaic
    bpy.ops.export_scene.fbx(filepath=OUT_MOSAIC, use_selection=True, object_types={'MESH'},
        apply_unit_scale=True, apply_scale_options='FBX_SCALE_NONE', path_mode='STRIP',
        use_mesh_modifiers=True, add_leaf_bones=False)
    print("EXPORTED MOSAIC:", OUT_MOSAIC)
