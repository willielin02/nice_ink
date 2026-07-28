# M_ReachVeil 生成（07-29 可畫域皮膚遮罩制）：作畫者 client 本地疊在受害者身上的
# veil 殼材質——Unlit 半透明黑、透明度=遮罩貼圖 R×強度、頂點沿法線外推 2.5mm 防
# z-fight。只暗「畫不到的皮膚」；遮罩由 runtime 逐點解算烘出（與收筆閘同源）。
# Run: UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript=<this> -unattended -nosplash
import unreal

PATH, NAME = "/Game/Characters", "M_ReachVeil"
FULL = f"{PATH}/{NAME}"
if unreal.EditorAssetLibrary.does_asset_exist(FULL):
    unreal.EditorAssetLibrary.delete_asset(FULL)

mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
    NAME, PATH, unreal.Material, unreal.MaterialFactoryNew())
mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
# 單面（07-29 終裁）：雙面曾為「繞向翻轉區紗被剔除」開過——但全身半透明殼像素
# 量 ×2＝巡航速度契約邊緣超標（stash A/B 定罪 GPU 拖幀）。翻轉區紗不可見的資訊
# 損失由筆尖 ✕（讀墨閘同一裁決）承擔；遮罩本身用可見性 trace＝判定不受繞向影響。

MEL = unreal.MaterialEditingLibrary

# Emissive＝純黑（veil=把後面的皮膚壓暗，不發光）
col = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -560, -80)
col.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, 0.0, 1.0))
MEL.connect_material_property(col, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

# Opacity＝ReachMask.R × VeilStrength（遮罩非 sRGB、LinearColor 取樣）
tex = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -820, 140)
tex.set_editor_property("parameter_name", "ReachMask")
tex.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
blk = unreal.load_asset("/Engine/EngineResources/Black")
if blk:
    tex.set_editor_property("texture", blk)  # 預設全透明（殼在遮罩烘好前不顯示，防禦性）

strength = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -820, 360)
strength.set_editor_property("parameter_name", "VeilStrength")
strength.set_editor_property("default_value", 0.38)

mul = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -560, 220)
MEL.connect_material_expressions(tex, "R", mul, "A")
MEL.connect_material_expressions(strength, "", mul, "B")
MEL.connect_material_property(mul, "", unreal.MaterialProperty.MP_OPACITY)

# WPO＝頂點法線外推 0.25cm（同網格疊殼防 z-fight；殼永遠貼著皮膚形狀）
nrm = MEL.create_material_expression(mat, unreal.MaterialExpressionVertexNormalWS, -820, 560)
push = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -820, 700)
push.set_editor_property("r", 0.25)
wpo = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -560, 600)
MEL.connect_material_expressions(nrm, "", wpo, "A")
MEL.connect_material_expressions(push, "", wpo, "B")
MEL.connect_material_property(wpo, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

MEL.recompile_material(mat)
unreal.EditorAssetLibrary.save_asset(FULL)
print("REACHVEIL_MAT_DONE", FULL)
