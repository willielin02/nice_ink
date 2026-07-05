import unreal

tools = unreal.AssetToolsHelpers.get_asset_tools()

def fbx_task(filename, dest_path, dest_name, combine=True, vertex_colors=False, materials=True):
    task = unreal.AssetImportTask()
    task.filename = filename
    task.destination_path = dest_path
    task.destination_name = dest_name
    task.automated = True
    task.save = True
    task.replace_existing = True
    ui = unreal.FbxImportUI()
    ui.import_mesh = True
    ui.import_as_skeletal = False
    ui.import_animations = False
    ui.import_materials = materials
    ui.import_textures = materials
    smd = ui.static_mesh_import_data
    smd.set_editor_property("combine_meshes", combine)
    smd.set_editor_property("generate_lightmap_u_vs", False)
    smd.set_editor_property("auto_generate_collision", False)
    if vertex_colors:
        smd.set_editor_property("vertex_color_import_option", unreal.VertexColorImportOption.REPLACE)
    task.options = ui
    return task

def tex_task(filename, dest_path, dest_name):
    task = unreal.AssetImportTask()
    task.filename = filename
    task.destination_path = dest_path
    task.destination_name = dest_name
    task.automated = True
    task.save = True
    task.replace_existing = True
    return task

tasks = [
    fbx_task(r"c:\games\Unreal Engine\nice_ink\SourceAssets\Sauna_localyany\sauna_room_repaired.fbx",
             "/Game/Sauna", "SM_SaunaRoom", combine=True, vertex_colors=False, materials=False),
    fbx_task(r"c:\games\Unreal Engine\nice_ink\SourceAssets\char17_static.fbx",
             "/Game/Characters", "SM_Char17", combine=True, vertex_colors=True, materials=False),
]
for key in ["7AF4", "cvd", "caseoh", "ibai", "img1", "img0"]:
    tasks.append(tex_task(
        rf"c:\games\Unreal Engine\nice_ink\Tools\FacePipeline\out\players\{key}\face_texture.png",
        "/Game/Characters/Faces", f"T_Face_{key}"))

tools.import_asset_tasks(tasks)

for t in tasks:
    paths = list(t.get_editor_property("imported_object_paths") or [])
    print("IMPORTED:", t.destination_name, "->", paths[:3])

# 複雜碰撞設定：身體與房間都用 complex-as-simple（UV 命中 + 走路碰撞）
for sm_path in ["/Game/Characters/SM_Char17", "/Game/Sauna/SM_SaunaRoom"]:
    sm = unreal.load_asset(sm_path)
    if sm:
        bs = sm.get_editor_property("body_setup")
        bs.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        unreal.EditorAssetLibrary.save_asset(sm_path)
        print("COLLISION SET:", sm_path)
    else:
        print("MISSING:", sm_path)

print("IMPORT SCRIPT DONE")
