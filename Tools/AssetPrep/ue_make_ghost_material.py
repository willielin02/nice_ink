# M_GhostBody 生成（直接畫制 ghost：畫畫時其餘人半透明）
# Unlit 半透明單色（fullbright 場景；美術語言=簡單明確）、雙面。
# Run: UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript=<this> -unattended -nosplash
import unreal

PATH, NAME = "/Game/Characters", "M_GhostBody"
FULL = f"{PATH}/{NAME}"
if unreal.EditorAssetLibrary.does_asset_exist(FULL):
    unreal.EditorAssetLibrary.delete_asset(FULL)

mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
    NAME, PATH, unreal.Material, unreal.MaterialFactoryNew())
mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
mat.set_editor_property("two_sided", True)

MEL = unreal.MaterialEditingLibrary
col = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -420, 0)
col.set_editor_property("constant", unreal.LinearColor(0.50, 0.56, 0.66, 1.0))  # 淡青灰
MEL.connect_material_property(col, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
op = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -420, 220)
op.set_editor_property("r", 0.22)
MEL.connect_material_property(op, "", unreal.MaterialProperty.MP_OPACITY)
MEL.recompile_material(mat)
unreal.EditorAssetLibrary.save_asset(FULL)
print("GHOST_MAT_DONE", FULL)
