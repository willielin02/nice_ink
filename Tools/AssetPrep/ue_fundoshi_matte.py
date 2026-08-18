# 褌反光根除（2026-08-18）：user 兩次抓「布會反光」。
#
# 我先前只調自己加的 sheen，漏了真正一直開著的東西：
#   **Specular 從未被接過＝停在引擎預設 0.5**，Roughness 吃 T_FundoshiRough(mean 0.84)。
# 皮膚在 SPEC #37 早就把 Specular 歸零（「鏡面蠟膜的物理載體不存在」），褌沒領到。
# 這裡照同一道配方：Specular=0、Roughness=1（全啞光），並把 sheen 預設關掉（0）。
# 之後要一點布的光澤，ClothSheenStrength 往上調即可——先確保「不反光」是預設狀態。
import unreal
MAT = "/Game/Characters/M_Fundoshi"
mel = unreal.MaterialEditingLibrary

def save_checked(p):
    if not unreal.EditorAssetLibrary.save_asset(p):
        raise RuntimeError(f"SAVE FAILED: {p}（編輯器/遊戲視窗鎖著檔案？）")
    unreal.log(f"  saved+verified: {p}")

mat = unreal.load_asset(MAT)
exprs = [e for e in unreal.ObjectIterator(unreal.MaterialExpression)
         if e.get_outermost() == mat.get_outermost()]

# 1) Specular -> 0（常數，接進 MP_SPECULAR）
if not any(isinstance(e, unreal.MaterialExpressionConstant)
           and abs(e.get_editor_property("r") - 0.0) < 1e-6
           and abs(e.get_editor_property("material_expression_editor_x") - 1440) < 30
           for e in exprs):
    z = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, 1440, 760)
    z.set_editor_property("r", 0.0)
    mel.connect_material_property(z, "", unreal.MaterialProperty.MP_SPECULAR)
    unreal.log("Specular -> 0（全啞光，同 SPEC #37 皮膚配方）")

# 2) Roughness -> 1（粗糙度貼圖在無反射環境下本來就無消費者；釘死免留伏筆）
if not any(isinstance(e, unreal.MaterialExpressionConstant)
           and abs(e.get_editor_property("r") - 1.0) < 1e-6
           and abs(e.get_editor_property("material_expression_editor_x") - 1440) < 30
           for e in exprs):
    o = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, 1440, 860)
    o.set_editor_property("r", 1.0)
    mel.connect_material_property(o, "", unreal.MaterialProperty.MP_ROUGHNESS)
    unreal.log("Roughness -> 1")

# 3) sheen 預設關掉
for e in exprs:
    if isinstance(e, unreal.MaterialExpressionScalarParameter) and \
            str(e.get_editor_property("parameter_name")) == "ClothSheenStrength":
        old = e.get_editor_property("default_value")
        e.set_editor_property("default_value", 0.0)
        unreal.log(f"ClothSheenStrength: {old} -> 0.0（要光澤再往上調）")

mel.recompile_material(mat)
save_checked(MAT)
unreal.log("DONE_MATTE")
