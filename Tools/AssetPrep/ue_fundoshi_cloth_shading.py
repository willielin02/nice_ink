# 褌布著色修（2026-08-18）：讓 M_Fundoshi 在 fullbright 下真的讀成布。
#
# 病因（已定罪，數字見 SHIP_PLAN 追記）：L_Dojo 零方向光（只有 SkyLight×2）＋
# r.ReflectionMethod=0 ⇒ 均勻天光的漫射照度與法線無關、無 IBL 高光 ⇒
# M_Fundoshi 接上的 Normal/Roughness 在畫面上恆等於沒接，布只剩一張平的 albedo。
# 那張法線圖其實有真起伏（偏離平面角 p95 30.1°），整份被丟掉。
# 皮膚在 SPEC #37 得過同一種病、用材質內假光治好；褌從沒領到這份治療。
#
# 修法＝沿用 #37 那條路，參數配布料：
#   Nw      = Transform(法線貼圖, Tangent->World)          <- 織紋起伏進入著色
#   NdotV   = saturate(dot(Nw, CameraVectorWS))            <- 「朝向鏡頭的程度」
#   Headlight = lerp(ClothLightFloor, 1, pow(NdotV, ClothLightPower))
#   Sheen     = ClothSheenStrength * pow(1-NdotV, ClothSheenPower)   <- 掠角絨毛回光＝布的簽名
#   BaseColor = albedo * (Headlight + Sheen) * ClothBrightness
# 全部 Scalar Parameter，viewport 即時滑。褌不是畫布（BuildTriCache 按槽名整段跳過），
# 「皮膚零烘焙陰影」鐵律管不到這裡——布上的明暗不跟任何筆跡搶注意力。
#
# 同批：三張布貼圖換成可平鋪版（Moisan 週期分解，只扣低頻、織紋高頻 std 動 0.09%）。
# 只新增/改接、**絕不刪節點**（headless delete_material_expression 必崩＝陷阱年鑑）。
#
# Run: UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript=<this>
#      （前置：Config/DefaultEngine.ini 的 robo StartupScripts 行必須不在）
import unreal

SA = r"c:\games\Unreal Engine\nice_ink\SourceAssets"
MAT_PATH = "/Game/Characters/M_Fundoshi"
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()

# 旋鈕預設值（user viewport 域，這裡只給起手值）
DEFAULTS = {
    "ClothTileScale": 1.0,      # ×既有 12x12。實測線徑 1.15mm 已正確 => 預設 1.0 不動
    "ClothLightFloor": 0.45,    # 背向鏡頭面的底亮度（1.0 = 假光全關 = 改動前狀態）
    "ClothLightPower": 1.0,     # 明暗過渡的硬度
    "ClothSheenStrength": 0.35,  # 掠角回光強度
    "ClothSheenPower": 3.0,     # 回光集中在多掠的角度
    "ClothBrightness": 1.0,     # 整體亮度補償（假光會壓暗均值，用這顆對回去）
}

# ---------------------------------------------------------------- 1. 可平鋪貼圖


def tex_task(filename, dest_path, dest_name):
    t = unreal.AssetImportTask()
    t.filename = filename
    t.destination_path = dest_path
    t.destination_name = dest_name
    t.automated = True
    t.save = True
    t.replace_existing = True
    return t


tasks = [
    tex_task(SA + r"\fundoshi_color_tileable.png", "/Game/Characters/Cloth", "T_FundoshiColor"),
    tex_task(SA + r"\fundoshi_rough_tileable.png", "/Game/Characters/Cloth", "T_FundoshiRough"),
    tex_task(SA + r"\fundoshi_normal_tileable.png", "/Game/Characters/Cloth", "T_FundoshiNormal"),
]
tools.import_asset_tasks(tasks)
failed = [t.destination_name for t in tasks
          if not list(t.get_editor_property("imported_object_paths") or [])]
if failed:
    raise RuntimeError(f"tileable texture import FAILED: {failed}")
unreal.log("IMPORTED tileable cloth textures")

# 旗標重設（重匯會回預設）——沿用 ue_import_sumo.py 的慣例
rough = unreal.load_asset("/Game/Characters/Cloth/T_FundoshiRough")
rough.set_editor_property("srgb", False)
unreal.EditorAssetLibrary.save_asset("/Game/Characters/Cloth/T_FundoshiRough")
nrm = unreal.load_asset("/Game/Characters/Cloth/T_FundoshiNormal")
nrm.set_editor_property("srgb", False)
nrm.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
nrm.set_editor_property("flip_green_channel", True)
unreal.EditorAssetLibrary.save_asset("/Game/Characters/Cloth/T_FundoshiNormal")
unreal.log("texture flags restored (rough linear / normal TC_NORMALMAP+flipG)")

# ---------------------------------------------------------------- 2. 材質手術
mat = unreal.load_asset(MAT_PATH)
if not mat:
    raise RuntimeError(f"missing {MAT_PATH}")


def existing_expressions(m):
    """5.7 的節點列舉：UMaterial::Expressions 已搬進 EditorOnlyData，逐路探測。
    全滅＝走整圖重建（孤兒節點不進編譯 shader，07-13 拆舊褌已立先例；
    headless 刪節點必崩，絕不嘗試刪）。"""
    attempts = [
        ("expression_collection",
         lambda: m.get_editor_property("expression_collection").get_editor_property("expressions")),
        ("expressions", lambda: m.get_editor_property("expressions")),
        ("editor_only_data",
         lambda: m.get_editor_property("editor_only_data").get_editor_property("expression_collection").get_editor_property("expressions")),
        ("MaterialEditingLibrary.get_material_expressions",
         lambda: mel.get_material_expressions(m)),
        ("ObjectIterator+outer",
         lambda: [e for e in unreal.ObjectIterator(unreal.MaterialExpression)
                  if e.get_outermost() == m.get_outermost()]),
    ]
    for tag, fn in attempts:
        try:
            got = list(fn())
        except Exception as ex:
            unreal.log(f"  enum[{tag}] 失敗: {type(ex).__name__}")
            continue
        if got:
            unreal.log(f"  enum[{tag}] -> {len(got)} 個節點")
            return got
        unreal.log(f"  enum[{tag}] -> 0")
    return []


def mk(cls, x, y):
    return mel.create_material_expression(mat, cls, x, y)


def scalar(name, x, y):
    p = mk(unreal.MaterialExpressionScalarParameter, x, y)
    p.set_editor_property("parameter_name", name)
    p.set_editor_property("default_value", DEFAULTS[name])
    p.set_editor_property("group", "Cloth")
    return p


# 冪等閘：已修過就不要疊第二份圖
try:
    if mel.get_material_default_scalar_parameter_value(mat, "ClothLightFloor") is not None:
        probe = existing_expressions(mat)
        if any(isinstance(e, unreal.MaterialExpressionScalarParameter) and
               str(e.get_editor_property("parameter_name")) == "ClothLightFloor" for e in probe):
            unreal.log("ALREADY_PATCHED: ClothLightFloor 已存在，跳過建圖")
            raise SystemExit(0)
except SystemExit:
    raise
except Exception:
    pass

# 取樣器：先試著重用既有三顆（列舉得到才有），列舉失敗＝自建全新一組。
# 舊節點會變孤兒——不進編譯 shader＝無害；headless 刪節點必崩（陷阱年鑑），絕不刪。
exprs = existing_expressions(mat)
unreal.log(f"M_Fundoshi 可列舉節點數 = {len(exprs)}")
samplers, texcoord = {}, None
for e in exprs:
    if isinstance(e, unreal.MaterialExpressionTextureSample):
        t = e.get_editor_property("texture")
        if t:
            n = t.get_name()
            for key, tag in (("color", "Color"), ("rough", "Rough"), ("normal", "Normal")):
                if tag in n:
                    samplers[key] = e
    elif isinstance(e, unreal.MaterialExpressionTextureCoordinate):
        texcoord = e

if len(samplers) == 3 and texcoord is not None:
    unreal.log("REUSE: 重用既有取樣器＋TexCoord")
else:
    unreal.log("REBUILD: 列舉不到既有節點，自建取樣器＋TexCoord（舊節點留為孤兒）")
    texcoord = mk(unreal.MaterialExpressionTextureCoordinate, -1150, -60)
    texcoord.set_editor_property("u_tiling", 12.0)
    texcoord.set_editor_property("v_tiling", 12.0)
    SPEC = (("color", "T_FundoshiColor", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -200),
            ("rough", "T_FundoshiRough", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR, 60),
            ("normal", "T_FundoshiNormal", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, 320))
    for key, asset, stype, y in SPEC:
        s = mk(unreal.MaterialExpressionTextureSample, -450, y)
        tex = unreal.load_asset(f"/Game/Characters/Cloth/{asset}")
        if not tex:
            raise RuntimeError(f"missing texture {asset}")
        s.set_editor_property("texture", tex)
        s.set_editor_property("sampler_type", stype)
        samplers[key] = s
    # 粗糙度/法線重新接上（BaseColor 走下面的假光鏈）
    mel.connect_material_property(samplers["rough"], "R", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(samplers["normal"], "RGB", unreal.MaterialProperty.MP_NORMAL)

# --- 2a. tiling 變成旋鈕：TexCoord × ClothTileScale -> 三顆取樣器的 UVs
p_tile = scalar("ClothTileScale", -1150, 120)
uv_mul = mk(unreal.MaterialExpressionMultiply, -950, 20)
mel.connect_material_expressions(texcoord, "", uv_mul, "A")
mel.connect_material_expressions(p_tile, "", uv_mul, "B")
for k in ("color", "rough", "normal"):
    mel.connect_material_expressions(uv_mul, "", samplers[k], "UVs")
unreal.log("tiling -> ClothTileScale 旋鈕（預設 1.0 = 既有 12x12 不變）")

# --- 2b. 法線進入著色：Tangent -> World
xf = mk(unreal.MaterialExpressionTransform, -150, 420)
src = getattr(unreal.MaterialVectorCoordTransformSource, "TRANSFORMSOURCE_TANGENT", None)
dst = getattr(unreal.MaterialVectorCoordTransform, "TRANSFORM_WORLD", None)
if src is None or dst is None:
    raise RuntimeError("Transform enum names not found on this engine build")
xf.set_editor_property("transform_source_type", src)
xf.set_editor_property("transform_type", dst)
mel.connect_material_expressions(samplers["normal"], "RGB", xf, "")

camv = mk(unreal.MaterialExpressionCameraVectorWS, -150, 560)
dot = mk(unreal.MaterialExpressionDotProduct, 60, 480)
mel.connect_material_expressions(xf, "", dot, "A")
mel.connect_material_expressions(camv, "", dot, "B")

# saturate（不同引擎版本節點名不同，逐一試）
sat = None
for cls_name in ("MaterialExpressionSaturate", "MaterialExpressionClamp"):
    cls = getattr(unreal, cls_name, None)
    if cls is None:
        continue
    sat = mk(cls, 220, 480)
    mel.connect_material_expressions(dot, "", sat, "")
    unreal.log(f"saturate node = {cls_name}")
    break
if sat is None:
    raise RuntimeError("no saturate/clamp expression class available")

# --- 2c. 頭燈假光：lerp(floor, 1, pow(NdotV, power))
p_pow = scalar("ClothLightPower", 350, 620)
powr = mk(unreal.MaterialExpressionPower, 520, 500)
mel.connect_material_expressions(sat, "", powr, "Base")
mel.connect_material_expressions(p_pow, "", powr, "Exp")

p_floor = scalar("ClothLightFloor", 520, 320)
one = mk(unreal.MaterialExpressionConstant, 520, 400)
one.set_editor_property("r", 1.0)
lerp = mk(unreal.MaterialExpressionLinearInterpolate, 720, 380)
mel.connect_material_expressions(p_floor, "", lerp, "A")
mel.connect_material_expressions(one, "", lerp, "B")
mel.connect_material_expressions(powr, "", lerp, "Alpha")

# --- 2d. 掠角 sheen：strength * pow(1-NdotV, sheenPower)
inv = mk(unreal.MaterialExpressionOneMinus, 350, 760)
mel.connect_material_expressions(sat, "", inv, "")
p_spow = scalar("ClothSheenPower", 350, 900)
spow = mk(unreal.MaterialExpressionPower, 520, 780)
mel.connect_material_expressions(inv, "", spow, "Base")
mel.connect_material_expressions(p_spow, "", spow, "Exp")
p_str = scalar("ClothSheenStrength", 520, 940)
smul = mk(unreal.MaterialExpressionMultiply, 720, 800)
mel.connect_material_expressions(spow, "", smul, "A")
mel.connect_material_expressions(p_str, "", smul, "B")

# --- 2e. 合成：albedo * (headlight + sheen) * brightness
shade = mk(unreal.MaterialExpressionAdd, 900, 560)
mel.connect_material_expressions(lerp, "", shade, "A")
mel.connect_material_expressions(smul, "", shade, "B")

lit = mk(unreal.MaterialExpressionMultiply, 1080, 300)
mel.connect_material_expressions(samplers["color"], "RGB", lit, "A")
mel.connect_material_expressions(shade, "", lit, "B")

p_bright = scalar("ClothBrightness", 1080, 480)
final = mk(unreal.MaterialExpressionMultiply, 1260, 340)
mel.connect_material_expressions(lit, "", final, "A")
mel.connect_material_expressions(p_bright, "", final, "B")

mel.connect_material_property(final, "", unreal.MaterialProperty.MP_BASE_COLOR)
unreal.log("BaseColor rewired -> albedo * (headlight + sheen) * brightness")

mel.recompile_material(mat)
unreal.EditorAssetLibrary.save_asset(MAT_PATH)
unreal.log(f"SAVED {MAT_PATH}")
unreal.log("DONE_FUNDOSHI_CLOTH_SHADING")
