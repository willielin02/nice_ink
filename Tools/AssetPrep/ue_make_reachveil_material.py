# M_ReachVeil 生成（07-29 橢圓制）：作畫者 client 本地疊在受害者身上的 veil 殼
# 材質——每個皮膚像素直接算橢圓公式 (u/A)²+(v/B)²，>1 漸灰（羽化）：光滑解析
# 邊界、零貼圖＝斑在構造上不存在。外觀=業界「停用灰」（SceneColor 去飽和壓暗）；
# 紅色語義留給筆尖 ✕。參數由 runtime 每鎖設定（EllipseCenter/AxisH/AxisV=預除
# 半軸的軸向量）。單面（雙面=全身半透明像素×2 曾涉巡航速度契約嫌疑）。
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

MEL = unreal.MaterialEditingLibrary


def vec_param(name, x, y):
    p = MEL.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, x, y)
    p.set_editor_property("parameter_name", name)
    p.set_editor_property("default_value", unreal.LinearColor(0.0, 0.0, 0.0, 0.0))
    m = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask, x + 180, y)
    m.set_editor_property("r", True)
    m.set_editor_property("g", True)
    m.set_editor_property("b", True)
    m.set_editor_property("a", False)
    MEL.connect_material_expressions(p, "", m, "")
    return m


# Emissive＝去飽和＋壓暗的背景（停用灰）
scene = MEL.create_material_expression(mat, unreal.MaterialExpressionSceneColor, -1080, -220)
desat = MEL.create_material_expression(mat, unreal.MaterialExpressionDesaturation, -820, -220)
MEL.connect_material_expressions(scene, "", desat, "")
frac = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -1080, -80)
frac.set_editor_property("r", 0.8)
MEL.connect_material_expressions(frac, "", desat, "Fraction")
dark = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -820, -80)
dark.set_editor_property("r", 0.62)
mulcol = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -560, -180)
MEL.connect_material_expressions(desat, "", mulcol, "A")
MEL.connect_material_expressions(dark, "", mulcol, "B")

# Opacity＝橢圓公式：e=(dot(d,H/A))²+(dot(d,V/B))²；e≤1 全透明、1→1.35 羽化轉灰
wpos = MEL.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -1600, 200)
center = vec_param("EllipseCenter", -1600, 360)
axh = vec_param("EllipseAxisH", -1600, 520)
axv = vec_param("EllipseAxisV", -1600, 680)
dvec = MEL.create_material_expression(mat, unreal.MaterialExpressionSubtract, -1240, 260)
MEL.connect_material_expressions(wpos, "", dvec, "A")
MEL.connect_material_expressions(center, "", dvec, "B")
dot_h = MEL.create_material_expression(mat, unreal.MaterialExpressionDotProduct, -1040, 420)
MEL.connect_material_expressions(dvec, "", dot_h, "A")
MEL.connect_material_expressions(axh, "", dot_h, "B")
sq_h = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -880, 420)
MEL.connect_material_expressions(dot_h, "", sq_h, "A")
MEL.connect_material_expressions(dot_h, "", sq_h, "B")
dot_v = MEL.create_material_expression(mat, unreal.MaterialExpressionDotProduct, -1040, 580)
MEL.connect_material_expressions(dvec, "", dot_v, "A")
MEL.connect_material_expressions(axv, "", dot_v, "B")
sq_v = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -880, 580)
MEL.connect_material_expressions(dot_v, "", sq_v, "A")
MEL.connect_material_expressions(dot_v, "", sq_v, "B")
esum = MEL.create_material_expression(mat, unreal.MaterialExpressionAdd, -720, 500)
MEL.connect_material_expressions(sq_h, "", esum, "A")
MEL.connect_material_expressions(sq_v, "", esum, "B")
one = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -720, 640)
one.set_editor_property("r", 1.0)
esub = MEL.create_material_expression(mat, unreal.MaterialExpressionSubtract, -560, 520)
MEL.connect_material_expressions(esum, "", esub, "A")
MEL.connect_material_expressions(one, "", esub, "B")
# 銳邊（07-29 user 定案「不需要線，區域標得夠明確就好」）：過渡帶用像素梯度
# 歸一化＝螢幕空間恆定 ~3px 抗鋸齒硬邊——邊界明確、無多餘線元件；斜面/掠射角
# 不糊不變粗（fwidth 版=業界範圍圈的標準做法）。環線制退役。
ddx = MEL.create_material_expression(mat, unreal.MaterialExpressionDDX, -700, 760)
MEL.connect_material_expressions(esum, "", ddx, "")
ddy = MEL.create_material_expression(mat, unreal.MaterialExpressionDDY, -700, 880)
MEL.connect_material_expressions(esum, "", ddy, "")
absx = MEL.create_material_expression(mat, unreal.MaterialExpressionAbs, -580, 760)
MEL.connect_material_expressions(ddx, "", absx, "")
absy = MEL.create_material_expression(mat, unreal.MaterialExpressionAbs, -580, 880)
MEL.connect_material_expressions(ddy, "", absy, "")
fw = MEL.create_material_expression(mat, unreal.MaterialExpressionAdd, -460, 820)
MEL.connect_material_expressions(absx, "", fw, "A")
MEL.connect_material_expressions(absy, "", fw, "B")
fweps = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -460, 960)
fweps.set_editor_property("r", 0.0002)
fwsafe = MEL.create_material_expression(mat, unreal.MaterialExpressionAdd, -340, 880)
MEL.connect_material_expressions(fw, "", fwsafe, "A")
MEL.connect_material_expressions(fweps, "", fwsafe, "B")
pxw = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -340, 1000)
pxw.set_editor_property("r", 3.0)  # 過渡帶寬（螢幕像素）
band = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -220, 920)
MEL.connect_material_expressions(fwsafe, "", band, "A")
MEL.connect_material_expressions(pxw, "", band, "B")
ediv = MEL.create_material_expression(mat, unreal.MaterialExpressionDivide, -420, 560)
MEL.connect_material_expressions(esub, "", ediv, "A")
MEL.connect_material_expressions(band, "", ediv, "B")
clampn = MEL.create_material_expression(mat, unreal.MaterialExpressionClamp, -300, 560)
MEL.connect_material_expressions(ediv, "", clampn, "")
strength = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -300, 700)
strength.set_editor_property("parameter_name", "VeilStrength")
strength.set_editor_property("default_value", 0.6)
mulop = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -160, 600)
MEL.connect_material_expressions(clampn, "", mulop, "A")
MEL.connect_material_expressions(strength, "", mulop, "B")
MEL.connect_material_property(mulop, "", unreal.MaterialProperty.MP_OPACITY)
MEL.connect_material_property(mulcol, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

# WPO＝頂點法線外推 0.25cm（同網格疊殼防 z-fight）
nrm = MEL.create_material_expression(mat, unreal.MaterialExpressionVertexNormalWS, -560, 840)
push = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -560, 980)
push.set_editor_property("r", 0.25)
wpo = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -420, 880)
MEL.connect_material_expressions(nrm, "", wpo, "A")
MEL.connect_material_expressions(push, "", wpo, "B")
MEL.connect_material_property(wpo, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

MEL.recompile_material(mat)
unreal.EditorAssetLibrary.save_asset(FULL)
print("REACHVEIL_MAT_DONE", FULL)
