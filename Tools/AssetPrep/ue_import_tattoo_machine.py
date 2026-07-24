# 刺青機資產匯入（伸縮針制 2026-07-21）：sumo_tattoo_machine/needle.fbx →
# SM_TattooMachine / SM_TattooNeedle（幾何+材質槽名 only；顏色引擎端 MID 依槽名上）。
# Run: UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript=<this> -unattended
import unreal

tools = unreal.AssetToolsHelpers.get_asset_tools()

def fbx_task(filename, dest_name):
    task = unreal.AssetImportTask()
    task.filename = filename
    task.destination_path = "/Game/Characters"
    task.destination_name = dest_name
    task.automated = True
    task.save = True
    task.replace_existing = True
    ui = unreal.FbxImportUI()
    ui.import_mesh = True
    ui.import_as_skeletal = False
    ui.import_animations = False
    ui.import_materials = False
    ui.import_textures = False
    smd = ui.static_mesh_import_data
    smd.set_editor_property("combine_meshes", True)
    smd.set_editor_property("generate_lightmap_u_vs", False)
    smd.set_editor_property("auto_generate_collision", False)
    task.options = ui
    return task

tasks = [
    fbx_task(r"c:\games\Unreal Engine\nice_ink\SourceAssets\sumo_tattoo_machine.fbx", "SM_TattooMachine"),
    fbx_task(r"c:\games\Unreal Engine\nice_ink\SourceAssets\sumo_tattoo_needle.fbx", "SM_TattooNeedle"),
]
tools.import_asset_tasks(tasks)

for t in tasks:
    paths = list(t.get_editor_property("imported_object_paths") or [])
    print("IMPORTED:", t.destination_name, "->", paths[:3])

# 槽名驗證（C++ 依 MaterialSlotName 對色：TattooFrame/Coils/Grip/Brass/Needle）
for p in ["/Game/Characters/SM_TattooMachine", "/Game/Characters/SM_TattooNeedle"]:
    sm = unreal.load_asset(p)
    if not sm:
        print("MISSING:", p)
        continue
    names = [str(m.material_slot_name) for m in sm.static_materials]
    b = sm.get_bounding_box()
    print(f"SLOTS {p}: {names}  bounds min={b.min} max={b.max}")

print("IMPORT SCRIPT DONE")
