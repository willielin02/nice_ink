# M_TvScreen 生成（M_TvBody 已隨方塊電視退役；櫃體材質住 ue_import_television.py）（開場動畫映像管電視 2026-08-27）。
# Body＝Unlit 深灰木箱（fullbright 場景慣例；美術語言=簡單明確）。
# Screen＝Unlit 自發光、貼圖參數 "ScreenTex"（runtime 餵 CanvasRenderTarget；
# 掃描線/閃爍/關機白線全在 RT 繪製端＝材質保持最簡）。
# Run: UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript=<this> -unattended -nosplash
import unreal

PATH = "/Game/Props"

def make_screen():
    full = f"{PATH}/M_TvScreen"
    if unreal.EditorAssetLibrary.does_asset_exist(full):
        unreal.EditorAssetLibrary.delete_asset(full)
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_TvScreen", PATH, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    MEL = unreal.MaterialEditingLibrary
    tex = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -520, 0)
    tex.set_editor_property("parameter_name", "ScreenTex")
    # 亮度增益（映像管微發光；RT 內容已含全部動畫）
    gain = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -240, 0)
    k = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 160)
    k.set_editor_property("r", 8.0)  # 曝光 bias 5.2 下的場景：unlit 自發光要追上受光的墻
    MEL.connect_material_expressions(tex, "RGB", gain, "A")
    MEL.connect_material_expressions(k, "", gain, "B")
    MEL.connect_material_property(gain, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(full)
    print("TV_SCREEN_MAT_DONE", full)


make_screen()
print("TVSET_MATERIALS_DONE")
