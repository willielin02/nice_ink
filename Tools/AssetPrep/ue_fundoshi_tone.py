# M_Fundoshi 讀頂點色 G（巨觀色調）＋墨水契約驗證。
import unreal
MAT = "/Game/Characters/M_Fundoshi"
mel = unreal.MaterialEditingLibrary

def save_checked(p):
    if not unreal.EditorAssetLibrary.save_asset(p):
        raise RuntimeError(f"SAVE FAILED: {p}（編輯器/遊戲視窗鎖著？）")
    unreal.log(f"  saved+verified: {p}")

mat = unreal.load_asset(MAT)
exprs = [e for e in unreal.ObjectIterator(unreal.MaterialExpression)
         if e.get_outermost() == mat.get_outermost()]
if any(isinstance(e, unreal.MaterialExpressionVertexColor) for e in exprs):
    unreal.log("ALREADY_PATCHED: VertexColor 已存在")
else:
    def at(cls, x, y, tol=30.0):
        h = [e for e in exprs if isinstance(e, cls)
             and abs(e.get_editor_property("material_expression_editor_x")-x) <= tol
             and abs(e.get_editor_property("material_expression_editor_y")-y) <= tol]
        if len(h) != 1: raise RuntimeError(f"{cls.__name__}@({x},{y}) 命中 {len(h)}")
        return h[0]
    color = next(e for e in exprs if isinstance(e, unreal.MaterialExpressionTextureSample)
                 and e.get_editor_property("texture")
                 and "Color" in e.get_editor_property("texture").get_name())
    lit = at(unreal.MaterialExpressionMultiply, 1080, 300)
    vc = mel.create_material_expression(mat, unreal.MaterialExpressionVertexColor, 620, 60)
    amt = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, 620, 200)
    amt.set_editor_property("parameter_name", "ClothToneVariation")
    amt.set_editor_property("default_value", 1.0)   # 0=關掉巨觀色調
    amt.set_editor_property("group", "Cloth")
    one = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, 620, 140)
    one.set_editor_property("r", 1.0)
    tl = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, 800, 120)
    mel.connect_material_expressions(one, "", tl, "A")
    mel.connect_material_expressions(vc, "G", tl, "B")
    mel.connect_material_expressions(amt, "", tl, "Alpha")
    tm = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, 940, 180)
    mel.connect_material_expressions(color, "RGB", tm, "A")
    mel.connect_material_expressions(tl, "", tm, "B")
    mel.connect_material_expressions(tm, "", lit, "A")
    unreal.log("albedo x lerp(1, VertexColor.G, ClothToneVariation) -> lit")
    mel.recompile_material(mat)
    save_checked(MAT)

# ---- 墨水契約驗證 ----
sm = unreal.load_asset("/Game/Characters/SM_Sumo")
names = [str(m.get_editor_property("material_slot_name")) for m in sm.get_editor_property("static_materials")]
unreal.log(f"SM slots={names}  total_tris={sm.get_num_triangles(0)}  verts={sm.get_num_vertices(0)}")
sk = unreal.load_asset("/Game/Characters/SK_Sumo")
sknames = [str(m.get_editor_property("material_slot_name")) for m in sk.get_editor_property("materials")]
unreal.log(f"SK slots={sknames}  skeleton={sk.get_editor_property('skeleton').get_name()}")
bs = sm.get_editor_property("body_setup")
unreal.log(f"SM collision={bs.get_editor_property('collision_trace_flag')}")
unreal.log("DONE_TONE")
