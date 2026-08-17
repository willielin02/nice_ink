# 酒瓶匯入（開場儀式，2026-08-16）：SourceAssets/sumo_bottle.fbx → /Game/Props/SM_Bottle
# 連 glb 內建材質一起匯（glass/whiskey/paper/viko）——首版關掉材質的結果是
# 場中央一個沒有材質的深灰塊（08-16 截圖自查抓到）。
# 碰撞＝無（瓶子不擋人、不擋游標射線——它是純演出道具）。
# Run（**跑前先確認 ini 沒有 robo StartupScripts 行**，否則 harness 空轉堵死 session）：
#   UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript=<本檔>
import unreal

tools = unreal.AssetToolsHelpers.get_asset_tools()

task = unreal.AssetImportTask()
task.filename = r"c:\games\Unreal Engine\nice_ink\SourceAssets\sumo_bottle.fbx"
task.destination_path = "/Game/Props"
task.destination_name = "SM_Bottle"
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
print("IMPORTED: SM_Bottle ->", paths)

sm = unreal.load_asset("/Game/Props/SM_Bottle")
if sm:
    b = sm.get_bounding_box()
    print("BOTTLE bounds min=({:.1f},{:.1f},{:.1f}) max=({:.1f},{:.1f},{:.1f})".format(
        b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z))
    print("BOTTLE materials:", [str(m.material_slot_name) for m in sm.get_editor_property("static_materials")])
    unreal.EditorAssetLibrary.save_asset("/Game/Props/SM_Bottle")
    print("SAVED")
else:
    print("FAIL: SM_Bottle not loadable")
