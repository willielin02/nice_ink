# 光澤修正（2026-08-18）：user 抓「布似乎會反光、材質看起來奇怪」。
#
# 定罪：我加的 sheen 是 pow(1-NdotV, 3)＝Fresnel 型邊緣項，**峰值剛好落在剪影上**
# ⇒ 一圈刀刃般的亮邊＝乙烯基/乳膠的長相。真布的絨毛回光是**寬而柔**的一片，
# 不是一條亮邊。指數越大越像塑膠。
# 修：指數 3.0 -> 1.5（把亮帶攤開），強度 0.18 -> 0.09（峰值減半）。
# 兩者都是 Scalar Parameter，user 可在 viewport 續調；設 Strength=0 即完全關掉。
import unreal
MAT = "/Game/Characters/M_Fundoshi"
NEW = {"ClothSheenPower": 1.5, "ClothSheenStrength": 0.09}

def save_checked(p):
    if not unreal.EditorAssetLibrary.save_asset(p):
        raise RuntimeError(f"SAVE FAILED: {p}（編輯器/遊戲視窗鎖著檔案？）")
    unreal.log(f"  saved+verified: {p}")

mat = unreal.load_asset(MAT)
exprs = [e for e in unreal.ObjectIterator(unreal.MaterialExpression)
         if e.get_outermost() == mat.get_outermost()]
for name, val in NEW.items():
    hit = [e for e in exprs if isinstance(e, unreal.MaterialExpressionScalarParameter)
           and str(e.get_editor_property("parameter_name")) == name]
    if len(hit) != 1:
        raise RuntimeError(f"{name} 命中 {len(hit)} 個")
    old = hit[0].get_editor_property("default_value")
    hit[0].set_editor_property("default_value", val)
    unreal.log(f"  {name}: {old} -> {val}")
unreal.MaterialEditingLibrary.recompile_material(mat)
save_checked(MAT)
unreal.log("DONE_SHEEN_TUNE")
