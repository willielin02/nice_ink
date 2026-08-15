# SK_Sumo_Whole 匯入（2026-08-15 脖子縫合版；沿用 SK_Sumo 的 Skeleton＝骨樹同源）
# Run (headless): UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript=<this>
import unreal
SA = r"c:\games\Unreal Engine\nice_ink\SourceAssets"
task = unreal.AssetImportTask()
task.filename = SA + r"\sumo_skeletal_whole.fbx"
task.destination_path = "/Game/Characters"
task.destination_name = "SK_Sumo_Whole"
task.automated = True; task.save = True; task.replace_existing = True
ui = unreal.FbxImportUI()
ui.import_mesh = True; ui.import_as_skeletal = True; ui.import_animations = False
ui.import_materials = False; ui.import_textures = False
ui.set_editor_property("automated_import_should_detect_type", False)
ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
ui.set_editor_property("original_import_type", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
existing = unreal.load_asset("/Game/Characters/SK_Sumo")
if existing and existing.skeleton:
    ui.skeleton = existing.skeleton
    unreal.log("reusing skeleton " + existing.skeleton.get_name())
skd = ui.skeletal_mesh_import_data
skd.set_editor_property("import_morph_targets", False)
skd.set_editor_property("vertex_color_import_option", unreal.VertexColorImportOption.REPLACE)
skd.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
task.options = ui
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
sk = unreal.load_asset("/Game/Characters/SK_Sumo_Whole")
if sk:
    unreal.log(f"SK_Sumo_Whole OK skeleton={sk.skeleton.get_name() if sk.skeleton else 'NONE'} mats={[m.material_slot_name for m in sk.materials]}")
    # 材質槽對齊 SK_Sumo（皮膚槽/褌槽同名同序＝C++ 依槽名判斷）
    if existing:
        unreal.log(f"SK_Sumo mats={[m.material_slot_name for m in existing.materials]}")
        sk.set_editor_property("materials", existing.materials)
        unreal.EditorAssetLibrary.save_loaded_asset(sk)
else:
    unreal.log_warning("SK_Sumo_Whole MISSING after import")
