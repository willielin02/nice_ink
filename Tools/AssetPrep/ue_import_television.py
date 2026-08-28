# Radiola 電視匯入（2026-08-28）：SourceAssets/tv_radiola.fbx → /Game/Props/SM_TvRadiola
# 資產＝"Radiola from Matrix" by Sirenko（CC-BY-4.0；SourceAssets/Television_Sirenko/ATTRIBUTION.txt）
#
# 材質走桑拿房模式（貼圖分開匯＋腳本自建材質）——**不走 FBX 自動材質**：
#   鐵坑 ×2（2026-08-28 實測）：5.7 Interchange 讀不了 FBX 內嵌貼圖（Invalid translator
#   couldn't retrieve a payload）；glb 打包貼圖名是無副檔名的 Image_N＝落地散檔照樣讀不了。
#   正解＝從原始 zip 抽命名乾淨的貼圖，各自 AssetImportTask，材質用 python 建、slot 用腳本指派。
# 碰撞＝自動凸包（家具 BlockAll、shape overlap 打得到）。
# Run（**跑前先確認 ini 沒有 robo StartupScripts 行**）：
#   UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript=<本檔>
import unreal

tools = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

# 舊的壞資產清掉（內嵌失敗那輪的 Image_*/空材質）
for a in list(EAL.list_assets("/Game/Props", recursive=False)):
    name = a.split("/")[-1].split(".")[0]
    if name.startswith("Image_") or name in ("material", "Radiola"):
        EAL.delete_asset(a.split(".")[0])
        print("CLEANED", name)

# ---- 貼圖 ----
TEX = [
    (r"c:\games\Unreal Engine\nice_ink\SourceAssets\tv_radiola_tex\Radiola_baseColor.jpeg", "T_TvRadiola_D", True),
    (r"c:\games\Unreal Engine\nice_ink\SourceAssets\tv_radiola_tex\Radiola_normal.png", "T_TvRadiola_N", False),
    (r"c:\games\Unreal Engine\nice_ink\SourceAssets\tv_radiola_tex\material_baseColor.jpeg", "T_TvLogo_D", True),
    (r"c:\games\Unreal Engine\nice_ink\SourceAssets\tv_radiola_tex\material_normal.png", "T_TvLogo_N", False),
    # AO（凹縫陰影）：內凹螢幕的深度線索住在這張圖裡——fullbright 無真陰影，
    # 不乘 AO 就沒有任何東西告訴眼睛「這裡凹進去」（user 抓邊框/螢幕分不清）
    (r"c:\games\Unreal Engine\nice_ink\SourceAssets\tv_radiola_tex\Radiola_AO.jpeg", "T_TvRadiola_AO", False),
]
for path, name, srgb in TEX:
    t = unreal.AssetImportTask()
    t.filename = path
    t.destination_path = "/Game/Props"
    t.destination_name = name
    t.automated = True
    t.save = True
    t.replace_existing = True
    tools.import_asset_tasks([t])
    tex = unreal.load_asset("/Game/Props/" + name)
    if not tex:
        raise RuntimeError("texture import failed: " + name)
    tex.set_editor_property("srgb", srgb)
    if not srgb and name.endswith("_N"):
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
    EAL.save_asset("/Game/Props/" + name)
    print("TEX", name)


def make_mat(name, d_name, n_name, ao_name=None):
    full = "/Game/Props/" + name
    if EAL.does_asset_exist(full):
        EAL.delete_asset(full)
    mat = tools.create_asset(name, "/Game/Props", unreal.Material, unreal.MaterialFactoryNew())
    d = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -560, 0)
    d.set_editor_property("texture", unreal.load_asset("/Game/Props/" + d_name))
    if ao_name:
        ao = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -560, 200)
        ao.set_editor_property("texture", unreal.load_asset("/Game/Props/" + ao_name))
        ao.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        mul = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -300, 60)
        MEL.connect_material_expressions(d, "RGB", mul, "A")
        MEL.connect_material_expressions(ao, "RGB", mul, "B")
        MEL.connect_material_property(mul, "", unreal.MaterialProperty.MP_BASE_COLOR)
    else:
        MEL.connect_material_property(d, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    n = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -400, 260)
    n.set_editor_property("texture", unreal.load_asset("/Game/Props/" + n_name))
    n.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    MEL.connect_material_property(n, "RGB", unreal.MaterialProperty.MP_NORMAL)
    r = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 480)
    r.set_editor_property("r", 0.72)  # 老木櫃＝偏霧面（fullbright 下鏡面本來就沒戲）
    MEL.connect_material_property(r, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(mat)
    EAL.save_asset(full)
    print("MAT", name)
    return mat


m_body = make_mat("M_TvRadiola", "T_TvRadiola_D", "T_TvRadiola_N", "T_TvRadiola_AO")
m_logo = make_mat("M_TvRadiolaLogo", "T_TvLogo_D", "T_TvLogo_N")

# ---- 網格 ----
task = unreal.AssetImportTask()
task.filename = r"c:\games\Unreal Engine\nice_ink\SourceAssets\tv_radiola.fbx"
task.destination_path = "/Game/Props"
task.destination_name = "SM_TvRadiola"
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
smd.set_editor_property("auto_generate_collision", True)
task.options = ui
tools.import_asset_tasks([task])
print("IMPORTED:", list(task.get_editor_property("imported_object_paths") or []))

sm = unreal.load_asset("/Game/Props/SM_TvRadiola")
if not sm:
    raise RuntimeError("SM_TvRadiola not loadable")
mats = sm.get_editor_property("static_materials")
m_screen = unreal.load_asset("/Game/Props/M_TvScreen")
out = []
for m in mats:
    slot = str(m.material_slot_name)
    if slot == "Radiola":
        use = m_body
    elif slot == "TvGlass":
        use = m_screen   # RT 節目直接上玻璃（TvSet 對這槽建 MID）
    else:
        use = m_logo
    m.set_editor_property("material_interface", use)
    out.append(m)
    print("SLOT", slot, "->", use.get_name())
sm.set_editor_property("static_materials", out)
b = sm.get_bounding_box()
print("TV bounds min=({:.1f},{:.1f},{:.1f}) max=({:.1f},{:.1f},{:.1f})".format(
    b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z))
EAL.save_asset("/Game/Props/SM_TvRadiola")
print("SAVED")
