# Sumo 角色資產匯入（headless：UnrealEditor-Cmd -ExecutePythonScript）
# - sumo_skeletal.fbx → SK_Sumo（骨骼網格＋Skeleton）與 SM_Sumo（同 FBX 靜態匯入，
#   供現行 static-body 架構過渡；我-9 骨骼化後棄用）
# - 六張 eye_mask_ink_sumo（含靜態禁畫預乘）→ 原地取代 T_EyeMaskInk_<key>（C++ 路徑零改動）
# - 髮三貼圖/髮罩/布三貼圖/銳化遮罩/接觸陰影/布法線 → /Game/Characters/{Hair,Cloth}
# 舊 char17 資產不在此刪除（C++ 切換編譯通過後另行清理）
import unreal

tools = unreal.AssetToolsHelpers.get_asset_tools()
SA = r"c:\games\Unreal Engine\nice_ink\SourceAssets"
FP = r"c:\games\Unreal Engine\nice_ink\Tools\FacePipeline"

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
    # 顯式鎖定型別：automated 匯入預設 DetectImportType 會覆寫 MeshTypeToImport
    ui.set_editor_property("automated_import_should_detect_type", False)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    ui.set_editor_property("original_import_type", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    skd = ui.skeletal_mesh_import_data
    skd.set_editor_property("import_morph_targets", False)
    skd.set_editor_property("vertex_color_import_option", unreal.VertexColorImportOption.REPLACE)
    # 法線=重算（08-05 定罪修正）：SM 匯入走 UE 預設重算平滑法線、SK 舊值
    # IMPORT_NORMALS 吃 Blender split normals——兩資產法線不同源＝骨骼身體上場後
    # 頭燈假光下乳暈硬環/胸口陰影斷層（user 實錘）。SK/SM 法線必須同源。
    skd.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_COMPUTE_NORMALS)
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
    # 關鍵：帶骨 FBX 的 automated 匯入會被 DetectImportType 判成 skeletal，
    # 必須顯式關掉偵測並鎖 STATIC，否則 SM_Sumo 會變成 USkeletalMesh（審查抓到的致命傷）
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
    sk_task(SA + r"\sumo_skeletal.fbx", "/Game/Characters", "SK_Sumo"),
    sm_task(SA + r"\sumo_skeletal.fbx", "/Game/Characters", "SM_Sumo"),
]
# 眼罩（含靜態禁畫）：原地取代——C++ 的 T_EyeMaskInk_<key> 路徑不變
for key in ["7AF4", "cvd", "caseoh", "ibai", "img1", "img0"]:
    tasks.append(tex_task(
        FP + rf"\out\players\{key}\eye_mask_ink_sumo.png",
        "/Game/Characters/Faces", f"T_EyeMaskInk_{key}"))
# 髮
tasks.append(tex_task(SA + r"\hair_tint_hd.png", "/Game/Characters/Hair", "T_HairTint"))
tasks.append(tex_task(SA + r"\hair_rough_hd.png", "/Game/Characters/Hair", "T_HairRough"))
tasks.append(tex_task(SA + r"\hair_normal_hd.png", "/Game/Characters/Hair", "T_HairNormal"))
tasks.append(tex_task(SA + r"\hair_mask.png", "/Game/Characters/Hair", "T_HairMask"))
# 布（褌）
tasks.append(tex_task(SA + r"\fundoshi_color.jpg", "/Game/Characters/Cloth", "T_FundoshiColor"))
tasks.append(tex_task(SA + r"\fundoshi_rough.jpg", "/Game/Characters/Cloth", "T_FundoshiRough"))
tasks.append(tex_task(SA + r"\fundoshi_normal.jpg", "/Game/Characters/Cloth", "T_FundoshiNormal"))
tasks.append(tex_task(SA + r"\fundoshi_mask_sharp.png", "/Game/Characters/Cloth", "T_FundoshiMask"))
tasks.append(tex_task(SA + r"\fundoshi_edge_shadow.png", "/Game/Characters/Cloth", "T_FundoshiEdgeShadow"))
tasks.append(tex_task(SA + r"\body_cloth_normal.png", "/Game/Characters/Cloth", "T_BodyClothNormal"))
tasks.append(tex_task(SA + r"\face_mask.png", "/Game/Characters/Cloth", "T_FaceMask"))

tools.import_asset_tasks(tasks)
failed = []
for t in tasks:
    paths = list(t.get_editor_property("imported_object_paths") or [])
    unreal.log(f"IMPORTED: {t.destination_name} -> {paths[:3]}")
    if not paths:
        failed.append(t.destination_name)
# 眼罩匯入失敗＝舊 char17 佈局殘留在新身上（禁畫錯位）——硬失敗，不許無聲跳過
if any(n.startswith("T_EyeMaskInk_") for n in failed):
    raise RuntimeError(f"eye mask import FAILED: {failed}")
if failed:
    unreal.log_warning(f"IMPORT FAILURES: {failed}")

# 貼圖設定：遮罩/粗糙度/陰影 = 線性；法線 = TC_Normalmap + flipG（Blender OpenGL→UE）
LINEAR = ["/Game/Characters/Hair/T_HairRough", "/Game/Characters/Hair/T_HairMask",
          "/Game/Characters/Cloth/T_FundoshiRough", "/Game/Characters/Cloth/T_FundoshiMask",
          "/Game/Characters/Cloth/T_FundoshiEdgeShadow", "/Game/Characters/Cloth/T_FaceMask"]
for p in LINEAR + [f"/Game/Characters/Faces/T_EyeMaskInk_{k}" for k in
                   ["7AF4", "cvd", "caseoh", "ibai", "img1", "img0"]]:
    tex = unreal.load_asset(p)
    if tex:
        tex.set_editor_property("srgb", False)
        unreal.EditorAssetLibrary.save_asset(p)
        unreal.log(f"LINEAR: {p}")
NORMALS = ["/Game/Characters/Hair/T_HairNormal", "/Game/Characters/Cloth/T_FundoshiNormal",
           "/Game/Characters/Cloth/T_BodyClothNormal"]
for p in NORMALS:
    tex = unreal.load_asset(p)
    if tex:
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("flip_green_channel", True)
        unreal.EditorAssetLibrary.save_asset(p)
        unreal.log(f"NORMAL: {p}")

# SM_Sumo 複雜碰撞（UV 命中＋走路）
sm = unreal.load_asset("/Game/Characters/SM_Sumo")
if sm:
    bs = sm.get_editor_property("body_setup")
    bs.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    unreal.EditorAssetLibrary.save_asset("/Game/Characters/SM_Sumo")
    unreal.log("COLLISION SET: /Game/Characters/SM_Sumo")

# 驗證：SK_Sumo 與其 Skeleton
sk = unreal.load_asset("/Game/Characters/SK_Sumo")
if sk:
    skel = sk.get_editor_property("skeleton")
    unreal.log(f"SK_Sumo OK skeleton={skel.get_name() if skel else 'NONE'}")
else:
    unreal.log_warning("SK_Sumo MISSING after import")
unreal.log("DONE_IMPORT_SUMO")
