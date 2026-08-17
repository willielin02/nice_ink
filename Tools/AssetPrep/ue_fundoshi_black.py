# 稽古廻し（幕下以下＝黑）第一刀：黑 albedo 上身＋sheen 從乘法改加法。
#
# 為什麼一定要改：原式 BaseColor = albedo x (headlight + sheen)。
# 白布上沒事；**黑布上 sheen 被 albedo 乘住＝一起壓成黑的**。
# 而黑布之所以看得出是布，靠的正是掠角那道光澤——黑色沒有色調空間可用。
# 新式：BaseColor = albedo x headlight x brightness + sheenTint x sheen
#      （sheen 變成類鏡面的加法項，浮在黑底上，不被 albedo 壓）。
#
# 節點靠**編輯器座標**指認（建圖時的座標是已知的；5.7 只有 ObjectIterator 那條
# 列舉路通，見 ue_fundoshi_cloth_shading.py）。只改接＋新增，**絕不刪節點**。
#
# Run: UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript=<this>
import unreal

MAT_PATH = "/Game/Characters/M_Fundoshi"
SA = r"c:\games\Unreal Engine\nice_ink\SourceAssets"
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def save_checked(pkg_path):
    """存檔必須驗——跑著的編輯器/遊戲視窗會鎖住 uasset，
    save_asset 回 False 且 log 只留一行 'Failed to move ... to temp directory'，
    腳本卻照樣跑完印 DONE（2026-08-18 實踩：黑 albedo 整刀沒落地卻回報成功）。
    同族血價＝殭屍編輯器鎖 umap（陷阱年鑑）。"""
    ok = unreal.EditorAssetLibrary.save_asset(pkg_path)
    if not ok:
        raise RuntimeError(
            f"SAVE FAILED: {pkg_path} —— 多半是編輯器或遊戲視窗鎖著檔案，全部關掉再跑")
    unreal.log(f"  saved+verified: {pkg_path}")

# 黑布的起手參數（viewport 域，這裡只給起點）
NEW_DEFAULTS = {
    "ClothSheenStrength": 0.18,   # 加法項：0.18 x tint(0.55) ~= 0.10 疊在 0.14 的底上
    "ClothBrightness": 1.0,       # 黑重定向後重新校，先歸 1
}
SHEEN_TINT = unreal.LinearColor(0.55, 0.55, 0.58, 1.0)   # 微冷的中性光澤

# ---------------------------------------------------------- 1. 黑 albedo
t = unreal.AssetImportTask()
t.filename = SA + r"\fundoshi_color_black.png"
t.destination_path = "/Game/Characters/Cloth"
t.destination_name = "T_FundoshiColor"
t.automated = True
t.save = True
t.replace_existing = True
tools.import_asset_tasks([t])
if not list(t.get_editor_property("imported_object_paths") or []):
    raise RuntimeError("black albedo import FAILED")
tex = unreal.load_asset("/Game/Characters/Cloth/T_FundoshiColor")
tex.set_editor_property("srgb", True)
save_checked("/Game/Characters/Cloth/T_FundoshiColor")
unreal.log("IMPORTED black albedo")

# ---------------------------------------------------------- 2. 找節點
mat = unreal.load_asset(MAT_PATH)
exprs = [e for e in unreal.ObjectIterator(unreal.MaterialExpression)
         if e.get_outermost() == mat.get_outermost()]
unreal.log(f"expressions = {len(exprs)}")


def at(cls, x, y, tol=30.0):
    """按建圖時的編輯器座標指認節點（不信順序、不信名字）。"""
    hits = [e for e in exprs if isinstance(e, cls)
            and abs(e.get_editor_property("material_expression_editor_x") - x) <= tol
            and abs(e.get_editor_property("material_expression_editor_y") - y) <= tol]
    if len(hits) != 1:
        raise RuntimeError(f"{cls.__name__} @({x},{y}) 命中 {len(hits)} 個（預期 1）")
    return hits[0]


def scalar_named(name):
    for e in exprs:
        if isinstance(e, unreal.MaterialExpressionScalarParameter) and \
                str(e.get_editor_property("parameter_name")) == name:
            return e
    raise RuntimeError(f"scalar param {name} not found")


color = next(e for e in exprs if isinstance(e, unreal.MaterialExpressionTextureSample)
             and e.get_editor_property("texture")
             and "Color" in e.get_editor_property("texture").get_name())
lerp = at(unreal.MaterialExpressionLinearInterpolate, 720, 380)   # 頭燈
smul = at(unreal.MaterialExpressionMultiply, 720, 800)            # sheen 純量
lit = at(unreal.MaterialExpressionMultiply, 1080, 300)            # albedo x shade
final = at(unreal.MaterialExpressionMultiply, 1260, 340)          # x brightness
unreal.log("located: color / lerp / smul / lit / final")

# 冪等閘
if any(isinstance(e, unreal.MaterialExpressionVectorParameter) and
       str(e.get_editor_property("parameter_name")) == "ClothSheenTint" for e in exprs):
    unreal.log("ALREADY_PATCHED: ClothSheenTint 已存在，跳過建圖")
    raise SystemExit(0)

# ---------------------------------------------------------- 3. 拆乘法、接加法
# lit 的 B 從 shade(頭燈+sheen) 改接純頭燈 => lit = albedo x headlight
mel.connect_material_expressions(lerp, "", lit, "B")

tint = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, 900, 900)
tint.set_editor_property("parameter_name", "ClothSheenTint")
tint.set_editor_property("default_value", SHEEN_TINT)
tint.set_editor_property("group", "Cloth")

sheen_rgb = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, 1120, 860)
mel.connect_material_expressions(tint, "", sheen_rgb, "A")
mel.connect_material_expressions(smul, "", sheen_rgb, "B")

out = mel.create_material_expression(mat, unreal.MaterialExpressionAdd, 1440, 500)
mel.connect_material_expressions(final, "", out, "A")        # albedo x headlight x brightness
mel.connect_material_expressions(sheen_rgb, "", out, "B")    # + tint x sheen（不被 albedo 壓）
mel.connect_material_property(out, "", unreal.MaterialProperty.MP_BASE_COLOR)
unreal.log("BaseColor = albedo x headlight x brightness + sheenTint x sheen")

for name, val in NEW_DEFAULTS.items():
    p = scalar_named(name)
    old = p.get_editor_property("default_value")
    p.set_editor_property("default_value", val)
    unreal.log(f"  {name}: {old} -> {val}")

mel.recompile_material(mat)
save_checked(MAT_PATH)
unreal.log("DONE_FUNDOSHI_BLACK")
