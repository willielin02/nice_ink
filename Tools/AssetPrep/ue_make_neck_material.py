# 建立 M_NeckStretch（轆轤首伸縮脖專用材質；headless -ExecutePythonScript）
# 公式＝M_InkBodyChar 的 BaseColor 鏈（T3D 逐節點考證 2026-07-16）去掉墨水/貼圖層：
#   dec  = VertexColor.rgb × 2                       （端色編碼=linear/2，同 chroma 貼圖慣例）
#   skin = SkinTone × lerp(1, dec, ChromaStrength)   （身體 skin 支路同構）
#   alb  = lerp(skin, dec, VertexColor.a)            （hair 支路＝絕對髮色 albedo）
#   BaseColor = Desaturation(alb × SkinBrightness, SkinDesat)
#             × lerp(HeadlightFloor, 1, pow(clamp(dot(CameraWS, VertexNormalWS)), HeadlightPower))
# 參數名與 M_InkBodyChar 完全同名 → runtime CopyMaterialUniformParameters 一鍵同步。
import unreal

PKG = "/Game/Characters"
NAME = "M_NeckStretch"

tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary

existing = unreal.EditorAssetLibrary.does_asset_exist(f"{PKG}/{NAME}")
if existing:
    unreal.EditorAssetLibrary.delete_asset(f"{PKG}/{NAME}")
mat = tools.create_asset(NAME, PKG, unreal.Material, unreal.MaterialFactoryNew())
assert mat, "create_asset failed"
mat.set_editor_property("two_sided", True)  # 每幀生成面 winding 保險；法線仍主導光照

def expr(cls, x, y):
    return mel.create_material_expression(mat, cls, x, y)

def con(src, out, dst, inp):
    ok = mel.connect_material_expressions(src, out, dst, inp)
    assert ok, f"connect {src} {out} -> {dst} {inp}"

def sparam(name, default, x, y):
    p = expr(unreal.MaterialExpressionScalarParameter, x, y)
    p.set_editor_property("parameter_name", name)
    p.set_editor_property("default_value", default)
    return p

vc = expr(unreal.MaterialExpressionVertexColor, -1900, -200)

dec = expr(unreal.MaterialExpressionMultiply, -1700, -250)
dec.set_editor_property("const_b", 2.0)
con(vc, "", dec, "A")

chroma_s = sparam("ChromaStrength", 0.6, -1700, -100)
chroma_lerp = expr(unreal.MaterialExpressionLinearInterpolate, -1500, -200)
chroma_lerp.set_editor_property("const_a", 1.0)
con(dec, "", chroma_lerp, "B")
con(chroma_s, "", chroma_lerp, "Alpha")

tone = expr(unreal.MaterialExpressionVectorParameter, -1500, -400)
tone.set_editor_property("parameter_name", "SkinTone")
tone.set_editor_property("default_value", unreal.LinearColor(0.4, 0.22, 0.13, 1.0))
skin = expr(unreal.MaterialExpressionMultiply, -1300, -300)
con(tone, "", skin, "A")
con(chroma_lerp, "", skin, "B")

alb = expr(unreal.MaterialExpressionLinearInterpolate, -1100, -250)
con(skin, "", alb, "A")
con(dec, "", alb, "B")
con(vc, "A", alb, "Alpha")

bright = sparam("SkinBrightness", 2.0, -1100, -80)
lit0 = expr(unreal.MaterialExpressionMultiply, -900, -200)
con(alb, "", lit0, "A")
con(bright, "", lit0, "B")

desat_f = sparam("SkinDesat", 0.15, -900, -50)
desat = expr(unreal.MaterialExpressionDesaturation, -700, -180)
con(lit0, "", desat, "")
con(desat_f, "", desat, "Fraction")

cam = expr(unreal.MaterialExpressionCameraVectorWS, -1500, 200)
nrm = expr(unreal.MaterialExpressionVertexNormalWS, -1500, 320)
dot = expr(unreal.MaterialExpressionDotProduct, -1300, 250)
con(cam, "", dot, "A")
con(nrm, "", dot, "B")
clamp = expr(unreal.MaterialExpressionClamp, -1150, 250)
con(dot, "", clamp, "")
hpow = sparam("HeadlightPower", 1.5, -1150, 400)
powe = expr(unreal.MaterialExpressionPower, -1000, 280)
con(clamp, "", powe, "Base")
con(hpow, "", powe, "Exp")  # Power 的輸入 pin 名=Exp（T3D 屬性名 Exponent 是屬性不是 pin）
hfloor = sparam("HeadlightFloor", 0.55, -1000, 120)
hl = expr(unreal.MaterialExpressionLinearInterpolate, -820, 220)
hl.set_editor_property("const_b", 1.0)
con(hfloor, "", hl, "A")
con(powe, "", hl, "Alpha")

base = expr(unreal.MaterialExpressionMultiply, -520, -50)
con(desat, "", base, "A")
con(hl, "", base, "B")
assert mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)

spec = sparam("SkinSpecular", 0.0, -520, 150)
assert mel.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
rough = sparam("SkinRoughness", 0.55, -520, 260)
assert mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

mel.recompile_material(mat)
ok = unreal.EditorAssetLibrary.save_asset(f"{PKG}/{NAME}")
unreal.log(f"NECK_MATERIAL_DONE save={ok}")
if not ok:
    raise RuntimeError("save failed")
