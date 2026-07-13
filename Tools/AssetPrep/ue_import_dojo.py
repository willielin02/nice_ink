import unreal

# 匯入道場場景（SourceAssets/dojo/dojo.rar 解壓）：
# FBX 合併成單一 SM_Dojo，材質/貼圖跟著 FBX 進來（tex/ 相對路徑）。
# 碰撞照 SM_SaunaRoom 慣例：complex-as-simple（UV 命中＋走路碰撞＋地板探測）。

tools = unreal.AssetToolsHelpers.get_asset_tools()

task = unreal.AssetImportTask()
task.filename = r"c:\games\Unreal Engine\nice_ink\SourceAssets\dojo\dojo.fbx"
task.destination_path = "/Game/Dojo"
task.destination_name = "SM_Dojo"
task.automated = True
task.save = True
task.replace_existing = True
ui = unreal.FbxImportUI()
ui.import_mesh = True
ui.import_as_skeletal = False
ui.import_animations = False
ui.import_materials = True
ui.import_textures = True
smd = ui.static_mesh_import_data
smd.set_editor_property("combine_meshes", True)
smd.set_editor_property("generate_lightmap_u_vs", False)
smd.set_editor_property("auto_generate_collision", False)
task.options = ui

tools.import_asset_tasks([task])
paths = list(task.get_editor_property("imported_object_paths") or [])
print("IMPORTED:", paths[:10])

sm = unreal.load_asset("/Game/Dojo/SM_Dojo")
if sm:
    bs = sm.get_editor_property("body_setup")
    bs.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    unreal.EditorAssetLibrary.save_asset("/Game/Dojo/SM_Dojo")
    bounds = sm.get_bounding_box()
    size = bounds.max - bounds.min
    print("COLLISION SET: /Game/Dojo/SM_Dojo")
    print(f"BOUNDS min=({bounds.min.x:.1f},{bounds.min.y:.1f},{bounds.min.z:.1f}) max=({bounds.max.x:.1f},{bounds.max.y:.1f},{bounds.max.z:.1f})")
    print(f"SIZE cm=({size.x:.1f},{size.y:.1f},{size.z:.1f})")
    print("MATERIAL SLOTS:", [str(m.material_slot_name) for m in sm.static_materials])
else:
    print("MISSING: /Game/Dojo/SM_Dojo")

old = unreal.load_asset("/Game/Sauna/SM_SaunaRoom")
if old:
    b = old.get_bounding_box()
    s = b.max - b.min
    print(f"SAUNA BOUNDS min=({b.min.x:.1f},{b.min.y:.1f},{b.min.z:.1f}) max=({b.max.x:.1f},{b.max.y:.1f},{b.max.z:.1f}) size=({s.x:.1f},{s.y:.1f},{s.z:.1f})")

print("DOJO IMPORT DONE")
