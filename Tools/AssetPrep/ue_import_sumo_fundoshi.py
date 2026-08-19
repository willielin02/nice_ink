# 褌上身：重匯 SK_Sumo/SM_Sumo（含 Fundoshi 第二 section）＋建 M_Fundoshi ＋按槽名指派。
# 前置：build_sumo_skeletal_fbx.py 已輸出含褌的 sumo_skeletal.fbx；
#       布三貼圖已在 /Game/Characters/Cloth（ue_import_sumo.py 匯過，含 linear/normalmap 設定）。
# Run: UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript=<this>
import unreal

tools = unreal.AssetToolsHelpers.get_asset_tools()
SA = r"c:\games\Unreal Engine\nice_ink\SourceAssets"

def sk_task(filename, dest_path, dest_name):
    task = unreal.AssetImportTask()
    task.filename = filename
    task.destination_path = dest_path
    task.destination_name = dest_name
    task.automated = True
    task.save = True
    task.replace_existing = True
    ui = unreal.FbxImportUI()
    ui.import_mesh = True
    ui.import_as_skeletal = True
    ui.import_animations = False
    ui.import_materials = False
    ui.import_textures = False
    ui.set_editor_property("automated_import_should_detect_type", False)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    ui.set_editor_property("original_import_type", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    # 骨架未變：顯式綁回現有 Skeleton，避免重匯生出第二副
    skel = unreal.load_asset("/Game/Characters/SK_Sumo_Skeleton")
    if skel:
        ui.skeleton = skel
    skd = ui.skeletal_mesh_import_data
    skd.set_editor_property("import_morph_targets", False)
    skd.set_editor_property("vertex_color_import_option", unreal.VertexColorImportOption.REPLACE)
    skd.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    task.options = ui
    return task

def sm_task(filename, dest_path, dest_name):
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
    ui.import_materials = False
    ui.import_textures = False
    ui.set_editor_property("automated_import_should_detect_type", False)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
    ui.set_editor_property("original_import_type", unreal.FBXImportType.FBXIT_STATIC_MESH)
    smd = ui.static_mesh_import_data
    smd.set_editor_property("combine_meshes", True)
    smd.set_editor_property("generate_lightmap_u_vs", False)
    smd.set_editor_property("auto_generate_collision", False)
    smd.set_editor_property("vertex_color_import_option", unreal.VertexColorImportOption.REPLACE)
    task.options = ui
    return task

# 雙密度（2026-08-20）：貼臉只看得到睡姿受害者（SM 靜態單實例）＝高密付得起；
# 站立骨骼身體（SK）沒人湊近＝輕量。兩檔由 fundoshi_shell.py FD_TIER 產出。
tasks = [
    sk_task(SA + r"\sumo_skeletal_lo.fbx", "/Game/Characters", "SK_Sumo"),
    sm_task(SA + r"\sumo_skeletal_hi.fbx", "/Game/Characters", "SM_Sumo"),
]
tools.import_asset_tasks(tasks)
for t in tasks:
    paths = list(t.get_editor_property("imported_object_paths") or [])
    unreal.log(f"IMPORTED: {t.destination_name} -> {paths[:3]}")
    if not paths:
        raise RuntimeError(f"import FAILED: {t.destination_name}")

# --- M_Fundoshi：平鋪布紋三貼圖 ---
MAT_PATH = "/Game/Characters/M_Fundoshi"
mel = unreal.MaterialEditingLibrary
if not unreal.EditorAssetLibrary.does_asset_exist(MAT_PATH):
    mat = tools.create_asset("M_Fundoshi", "/Game/Characters", unreal.Material,
                             unreal.MaterialFactoryNew())
    tc = mel.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -750, 0)
    tc.set_editor_property("u_tiling", 12.0)
    tc.set_editor_property("v_tiling", 12.0)

    color = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -450, -200)
    color.texture = unreal.load_asset("/Game/Characters/Cloth/T_FundoshiColor")
    color.sampler_type = unreal.MaterialSamplerType.SAMPLERTYPE_COLOR

    rough = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -450, 60)
    rough.texture = unreal.load_asset("/Game/Characters/Cloth/T_FundoshiRough")
    rough.sampler_type = unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR

    normal = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -450, 320)
    normal.texture = unreal.load_asset("/Game/Characters/Cloth/T_FundoshiNormal")
    normal.sampler_type = unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL

    for smp in (color, rough, normal):
        mel.connect_material_expressions(tc, "", smp, "UVs")   # UV 輸入 pin 名=「UVs」（舊例確認）
    mel.connect_material_property(color, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(rough, "R", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(normal, "RGB", unreal.MaterialProperty.MP_NORMAL)
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(MAT_PATH)
    unreal.log(f"CREATED: {MAT_PATH}")
else:
    mat = unreal.load_asset(MAT_PATH)
    unreal.log(f"EXISTS: {MAT_PATH}")

# --- 指派槽位（按槽名，不信 section 順序）---
sm = unreal.load_asset("/Game/Characters/SM_Sumo")
names = [str(m.get_editor_property("material_slot_name")) for m in sm.get_editor_property("static_materials")]
unreal.log(f"SM_Sumo slots: {names}")
assert "M_Fundoshi" in names, f"fundoshi slot missing on SM_Sumo: {names}"
sm.set_material(names.index("M_Fundoshi"), mat)
# 複雜碰撞（UV 命中＋走路＋布擋筆）——重匯後重設
bs = sm.get_editor_property("body_setup")
bs.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
unreal.EditorAssetLibrary.save_asset("/Game/Characters/SM_Sumo")
unreal.log("SM_Sumo material+collision set")

sk = unreal.load_asset("/Game/Characters/SK_Sumo")
mats = list(sk.get_editor_property("materials"))
sk_names = [str(m.get_editor_property("material_slot_name")) for m in mats]
unreal.log(f"SK_Sumo slots: {sk_names}")
assert "M_Fundoshi" in sk_names, f"fundoshi slot missing on SK_Sumo: {sk_names}"
idx = sk_names.index("M_Fundoshi")
new_entry = unreal.SkeletalMaterial()
new_entry.set_editor_property("material_interface", mat)
new_entry.set_editor_property("material_slot_name", "M_Fundoshi")
mats[idx] = new_entry
sk.set_editor_property("materials", mats)
unreal.EditorAssetLibrary.save_asset("/Game/Characters/SK_Sumo")
unreal.log("SK_Sumo material set")

skel = sk.get_editor_property("skeleton")
unreal.log(f"SK_Sumo skeleton={skel.get_name() if skel else 'NONE'}")
unreal.log("DONE_IMPORT_FUNDOSHI")
